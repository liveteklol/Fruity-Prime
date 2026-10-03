#include "../RendererGpuMesh.hpp"

#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

namespace
{
    using namespace MphRead;

    template <typename T>
    concept HasLegacyListId = requires(T value)
    {
        value.ListId;
    };

    static_assert(!HasLegacyListId<Mesh>,
        "Mesh must not own a backend/display-list identifier.");

    [[noreturn]] void Fail(std::string_view message)
    {
        throw std::runtime_error(std::string(message));
    }

    void Expect(bool value, std::string_view message)
    {
        if (!value)
        {
            Fail(message);
        }
    }

    class FakeGpuMesh final : public GpuMeshResource
    {
    public:
        FakeGpuMesh(std::int32_t& destroyed, std::int32_t& drawn)
            : _destroyed(destroyed), _drawn(drawn) {}

        ~FakeGpuMesh() override { ++_destroyed; }
        void Draw() override { ++_drawn; }

    private:
        std::int32_t& _destroyed;
        std::int32_t& _drawn;
    };

    void TestDrawPlanPreservesRanges()
    {
        RendererGeometry geometry{};
        geometry.Indices.resize(18);
        geometry.Ranges = {
            {ScenePrimitiveTopology::Triangles, 0, 3},
            {ScenePrimitiveTopology::Quads, 3, 4},
            {ScenePrimitiveTopology::TriangleStrip, 7, 5},
            {ScenePrimitiveTopology::QuadStrip, 12, 6}
        };

        const GpuMeshDrawPlan plan = BuildGpuMeshDrawPlan(geometry);
        Expect(plan.IndexCount == 18, "draw plan index count");
        Expect(plan.Ranges.size() == 4, "draw plan range count");
        Expect(plan.Ranges[0].Topology == ScenePrimitiveTopology::Triangles
            && plan.Ranges[0].FirstIndex == 0 && plan.Ranges[0].IndexCount == 3
            && plan.Ranges[0].IndexByteOffset == 0, "triangle range");
        Expect(plan.Ranges[1].Topology == ScenePrimitiveTopology::Quads
            && plan.Ranges[1].FirstIndex == 3 && plan.Ranges[1].IndexCount == 4
            && plan.Ranges[1].IndexByteOffset == 3 * sizeof(std::uint32_t), "quad range");
        Expect(plan.Ranges[2].Topology == ScenePrimitiveTopology::TriangleStrip
            && plan.Ranges[2].FirstIndex == 7 && plan.Ranges[2].IndexCount == 5
            && plan.Ranges[2].IndexByteOffset == 7 * sizeof(std::uint32_t), "triangle-strip range");
        Expect(plan.Ranges[3].Topology == ScenePrimitiveTopology::QuadStrip
            && plan.Ranges[3].FirstIndex == 12 && plan.Ranges[3].IndexCount == 6
            && plan.Ranges[3].IndexByteOffset == 12 * sizeof(std::uint32_t), "quad-strip range");
    }

    void TestTransientIndexSequencePreservesSubmissionOrder()
    {
        std::vector<std::uint32_t> indices(8, 0xFFFFFFFFU);
        BuildTransientIndexSequence(indices);
        for (std::size_t i = 0; i < indices.size(); ++i)
        {
            Expect(indices[i] == i, "transient IBO must preserve submitted vertex order");
        }

        std::vector<std::uint32_t> empty{};
        BuildTransientIndexSequence(empty);
        Expect(empty.empty(), "empty transient draw keeps an empty IBO");
    }

    void TestPrimitiveWindingAndIncompleteTails()
    {
        const std::vector<std::uint32_t> input{10, 11, 12, 13, 14, 15, 16};
        const auto scene = [&](ScenePrimitiveTopology topology, std::vector<std::uint32_t> expected) {
            std::vector<std::uint32_t> result{99};
            expected.insert(expected.begin(), 99);
            AppendSceneTriangleIndices(result, input, topology);
            Expect(result == expected, "triangle lowering preserves winding and ignores incomplete primitives");
        };
        scene(ScenePrimitiveTopology::Triangles, {10,11,12,13,14,15});
        scene(ScenePrimitiveTopology::Quads, {10,11,12,10,12,13});
        scene(ScenePrimitiveTopology::TriangleStrip, {10,11,12,12,11,13,12,13,14,14,13,15,14,15,16});
        scene(ScenePrimitiveTopology::QuadStrip, {10,11,13,10,13,12,12,13,15,12,15,14});
        std::vector<std::uint32_t> result;
        AppendTransientDrawIndices(result, std::span(input).first(4), TransientPrimitiveTopology::LineLoop);
        Expect(result == std::vector<std::uint32_t>{10,11,11,12,12,13,13,10}, "line loop closes exactly once");
        result.clear();
        AppendTransientDrawIndices(result, std::span(input).first(4), TransientPrimitiveTopology::TriangleFan);
        Expect(result == std::vector<std::uint32_t>{10,11,12,10,12,13}, "fan keeps its common first vertex");
        result.clear();
        AppendSceneTriangleIndices(result, {}, ScenePrimitiveTopology::TriangleStrip);
        AppendTransientDrawIndices(result, std::span(input).first(1), TransientPrimitiveTopology::LineLoop);
        Expect(result.empty(), "incomplete primitives emit no indices");
    }

    void TestCacheUsesLiveModelAndMeshIdentity()
    {
        GpuMeshCache cache{};
        auto modelA = std::make_shared<std::int32_t>(1);
        auto modelB = std::make_shared<std::int32_t>(2);
        auto meshA0 = std::make_shared<std::int32_t>(7);
        auto meshA1 = std::make_shared<std::int32_t>(7);
        auto meshB0 = std::make_shared<std::int32_t>(7);
        std::shared_ptr<const void> modelLifeA = modelA;
        std::shared_ptr<const void> modelLifeB = modelB;
        std::shared_ptr<const void> meshLifeA0 = meshA0;
        std::shared_ptr<const void> meshLifeA1 = meshA1;
        std::shared_ptr<const void> meshLifeB0 = meshB0;
        std::int32_t destroyed = 0;
        std::int32_t drawn = 0;
        std::int32_t factoryCalls = 0;

        auto make = [&]()
        {
            ++factoryCalls;
            return std::make_shared<FakeGpuMesh>(destroyed, drawn);
        };

        std::weak_ptr<GpuMeshResource> firstWeak;
        {
            const auto first = cache.GetOrCreate(modelLifeA, meshLifeA0, make);
            firstWeak = first;
            const auto again = cache.GetOrCreate(modelLifeA, meshLifeA0, make);
            const auto sameDlistDifferentMesh = cache.GetOrCreate(modelLifeA, meshLifeA1, make);
            const auto sameDlistDifferentModel = cache.GetOrCreate(modelLifeB, meshLifeB0, make);
            Expect(first == again, "same live model/mesh identity must hit cache");
            Expect(first != sameDlistDifferentMesh,
                "distinct Mesh objects must not alias even when DlistId matches");
            Expect(first != sameDlistDifferentModel,
                "distinct Model objects must not alias");
            Expect(factoryCalls == 3, "three distinct model/mesh identities");
            first->Draw();
            Expect(drawn == 1, "cached resource draw");
        }

        cache.EraseModel(modelA.get());
        Expect(cache.Size() == 1, "erase model removes all model-A meshes");
        Expect(firstWeak.expired(), "erased model releases its GPU resource");
        Expect(destroyed == 2, "model erase destroys both model-A mesh resources");

        std::weak_ptr<GpuMeshResource> reloadedWeak;
        {
            const auto reloaded = cache.GetOrCreate(modelLifeA, meshLifeA0, make);
            reloadedWeak = reloaded;
            const auto reloadedAgain = cache.GetOrCreate(modelLifeA, meshLifeA0, make);
            Expect(reloaded == reloadedAgain,
                "reacquired live model/mesh identity must hit the replacement cache entry");
            Expect(factoryCalls == 4,
                "reacquiring after model erase must create exactly one replacement resource");
            Expect(cache.Size() == 2,
                "reacquiring erased model adds a fresh entry alongside other live models");
        }
        cache.EraseModel(modelA.get());
        Expect(cache.Size() == 1, "second model erase removes the replacement entry");
        Expect(reloadedWeak.expired(), "second model erase releases the replacement GPU resource");
        Expect(destroyed == 3, "replacement resource is destroyed exactly once");

        modelB.reset();
        modelLifeB.reset();
        meshB0.reset();
        meshLifeB0.reset();
        cache.PruneExpired();
        Expect(cache.Size() == 0, "expired model/mesh identity is pruned");
        Expect(destroyed == 4, "expired identity releases resource once");

        auto modelC = std::make_shared<std::int32_t>(3);
        auto meshC = std::make_shared<std::int32_t>(9);
        std::shared_ptr<const void> modelLifeC = modelC;
        std::shared_ptr<const void> meshLifeC = meshC;
        (void)cache.GetOrCreate(modelLifeC, meshLifeC, make);
        cache.Clear();
        Expect(cache.Size() == 0, "cache clear");
        Expect(destroyed == 5, "clear releases resource exactly once");
    }
}

int main()
{
    try
    {
        TestDrawPlanPreservesRanges();
        TestTransientIndexSequencePreservesSubmissionOrder();
        TestPrimitiveWindingAndIncompleteTails();
        TestCacheUsesLiveModelAndMeshIdentity();
        std::cout << "RendererGpuMesh tests passed.\n";
        return 0;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "RendererGpuMesh test failure: " << ex.what() << '\n';
        return 1;
    }
}
