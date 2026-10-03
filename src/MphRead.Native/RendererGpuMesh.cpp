#include "RendererGpuMesh.hpp"

#include <limits>
#include <stdexcept>

namespace MphRead
{
    GpuMeshDrawPlan BuildGpuMeshDrawPlan(const RendererGeometry& geometry)
    {
        if (geometry.Indices.size() > std::numeric_limits<std::uint32_t>::max())
        {
            throw std::overflow_error("GPU mesh index count exceeds uint32_t.");
        }

        GpuMeshDrawPlan plan{};
        plan.IndexCount = static_cast<std::uint32_t>(geometry.Indices.size());
        plan.Ranges.reserve(geometry.Ranges.size());
        for (const ScenePrimitiveRange& range : geometry.Ranges)
        {
            const std::uint64_t end = static_cast<std::uint64_t>(range.FirstIndex)
                + static_cast<std::uint64_t>(range.IndexCount);
            if (end > geometry.Indices.size())
            {
                throw std::out_of_range("GPU mesh primitive range exceeds the index buffer.");
            }
            plan.Ranges.push_back(GpuMeshDrawRange{
                range.Topology,
                range.FirstIndex,
                range.IndexCount,
                static_cast<std::size_t>(range.FirstIndex) * sizeof(std::uint32_t)
            });
        }
        return plan;
    }

    void BuildTransientIndexSequence(std::span<std::uint32_t> indices)
    {
        if (indices.size() > std::numeric_limits<std::uint32_t>::max())
        {
            throw std::overflow_error("Transient geometry vertex count exceeds uint32_t.");
        }
        for (std::size_t i = 0; i < indices.size(); ++i)
        {
            indices[i] = static_cast<std::uint32_t>(i);
        }
    }

    void AppendSceneTriangleIndices(std::vector<std::uint32_t>& output,
        std::span<const std::uint32_t> input, ScenePrimitiveTopology topology)
    {
        const auto n = input.size();
        switch (topology)
        {
        case ScenePrimitiveTopology::Triangles:
            output.insert(output.end(), input.begin(), input.begin() + static_cast<std::ptrdiff_t>(n / 3 * 3));
            break;
        case ScenePrimitiveTopology::Quads:
            for (std::size_t i = 0; i + 3 < n; i += 4)
                output.insert(output.end(), {input[i], input[i + 1], input[i + 2], input[i], input[i + 2], input[i + 3]});
            break;
        case ScenePrimitiveTopology::TriangleStrip:
            for (std::size_t i = 0; i + 2 < n; ++i)
                if (i % 2 == 0) output.insert(output.end(), {input[i], input[i + 1], input[i + 2]});
                else output.insert(output.end(), {input[i + 1], input[i], input[i + 2]});
            break;
        case ScenePrimitiveTopology::QuadStrip:
            for (std::size_t i = 0; i + 3 < n; i += 2)
                output.insert(output.end(), {input[i], input[i + 1], input[i + 3], input[i], input[i + 3], input[i + 2]});
            break;
        default: throw std::invalid_argument("Unknown scene primitive topology.");
        }
    }

    void AppendTransientDrawIndices(std::vector<std::uint32_t>& output,
        std::span<const std::uint32_t> input, TransientPrimitiveTopology topology)
    {
        switch (topology)
        {
        case TransientPrimitiveTopology::LineLoop:
            if (input.size() >= 2)
                for (std::size_t i = 0; i < input.size(); ++i)
                    output.insert(output.end(), {input[i], input[(i + 1) % input.size()]});
            break;
        case TransientPrimitiveTopology::TriangleFan:
            for (std::size_t i = 1; i + 1 < input.size(); ++i)
                output.insert(output.end(), {input[0], input[i], input[i + 1]});
            break;
        case TransientPrimitiveTopology::Triangles:
            AppendSceneTriangleIndices(output, input, ScenePrimitiveTopology::Triangles); break;
        case TransientPrimitiveTopology::TriangleStrip:
            AppendSceneTriangleIndices(output, input, ScenePrimitiveTopology::TriangleStrip); break;
        case TransientPrimitiveTopology::Quads:
            AppendSceneTriangleIndices(output, input, ScenePrimitiveTopology::Quads); break;
        case TransientPrimitiveTopology::QuadStrip:
            AppendSceneTriangleIndices(output, input, ScenePrimitiveTopology::QuadStrip); break;
        default: throw std::invalid_argument("Unknown transient primitive topology.");
        }
    }

    std::size_t GpuMeshCache::KeyHash::operator()(const Key& key) const noexcept
    {
        const std::size_t modelHash = std::hash<const void*>{}(key.ModelIdentity);
        const std::size_t meshHash = std::hash<const void*>{}(key.MeshIdentity);
        return modelHash ^ (meshHash + static_cast<std::size_t>(0x9e3779b9U)
            + (modelHash << 6U) + (modelHash >> 2U));
    }

    std::shared_ptr<GpuMeshResource> GpuMeshCache::Find(
        const void* modelIdentity, const void* meshIdentity)
    {
        const Key key{modelIdentity, meshIdentity};
        const auto found = _entries.find(key);
        if (found == _entries.end())
        {
            return {};
        }
        if (found->second.ModelLifetime.expired()
            || found->second.MeshLifetime.expired())
        {
            _entries.erase(found);
            return {};
        }
        return found->second.Resource;
    }

    std::shared_ptr<GpuMeshResource> GpuMeshCache::GetOrCreate(
        const std::shared_ptr<const void>& modelLifetime,
        const std::shared_ptr<const void>& meshLifetime,
        const Factory& factory)
    {
        if (!modelLifetime)
        {
            throw std::invalid_argument("GPU mesh cache requires a live model identity.");
        }
        if (!meshLifetime)
        {
            throw std::invalid_argument("GPU mesh cache requires a live mesh identity.");
        }
        if (!factory)
        {
            throw std::invalid_argument("GPU mesh cache requires a resource factory.");
        }

        const Key key{modelLifetime.get(), meshLifetime.get()};
        if (const std::shared_ptr<GpuMeshResource> existing
            = Find(key.ModelIdentity, key.MeshIdentity))
        {
            return existing;
        }

        std::shared_ptr<GpuMeshResource> resource = factory();
        if (!resource)
        {
            throw std::runtime_error("GPU mesh factory returned no resource.");
        }
        _entries.insert_or_assign(key, Entry{modelLifetime, meshLifetime, resource});
        return resource;
    }

    void GpuMeshCache::EraseModel(const void* modelIdentity)
    {
        for (auto it = _entries.begin(); it != _entries.end();)
        {
            if (it->first.ModelIdentity == modelIdentity)
            {
                it = _entries.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void GpuMeshCache::PruneExpired()
    {
        for (auto it = _entries.begin(); it != _entries.end();)
        {
            if (it->second.ModelLifetime.expired()
                || it->second.MeshLifetime.expired())
            {
                it = _entries.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void GpuMeshCache::Clear() noexcept
    {
        _entries.clear();
    }

    std::size_t GpuMeshCache::Size() const noexcept
    {
        return _entries.size();
    }
}
