// A level whose collision has more distinct corners than the format's 16-bit
// index (65535) used to be refused outright -- pom_bots, with 87,943 of them,
// crashed the game on launch for want of a collision file. Its corners are
// mostly the same corner written down several times a hair apart, and the
// packer now welds those. A level that is too big for that to help is still
// refused, with the reason.

#include "../Mods/MapGen/MapCollisionPacker.hpp"
#include "../Program.hpp"
#include "../Utility/RepackCollision.hpp"

#include <cstdint>
#include <iostream>
#include <memory>
#include <string_view>
#include <vector>

namespace
{
    using MphRead::Mods::MapGen::MapCollisionPacker;
    using MphRead::Utility::CollisionDataEditor;
    using OpenTK::Mathematics::Vector3;
    using OpenTK::Mathematics::Vector4;
    using FaceList = std::vector<std::shared_ptr<CollisionDataEditor>>;

    bool Check(bool condition, std::string_view name)
    {
        std::cout << (condition ? "PASS " : "FAIL ") << name << '\n';
        return condition;
    }

    // A flat floor of n x n cells, two triangles each. `jitter` is how far each
    // triangle's own copy of a shared corner strays from the others'.
    std::shared_ptr<const FaceList> Floor(int n, float jitter, float cellSize)
    {
        auto faces = std::make_shared<FaceList>();
        std::uint32_t state = 12345;
        const auto noise = [&]
        {
            state = state * 1664525U + 1013904223U;
            return (static_cast<float>(state >> 8) / 16777216.0F - 0.5F) * 2.0F * jitter;
        };
        const auto corner = [&](int x, int z)
        {
            return Vector3(x * cellSize + noise(), 0.0F + noise(), z * cellSize + noise());
        };
        const auto triangle = [&](Vector3 a, Vector3 b, Vector3 c)
        {
            auto face = std::make_shared<CollisionDataEditor>();
            face->Points->push_back(a);
            face->Points->push_back(b);
            face->Points->push_back(c);
            face->Plane = Vector4(0.0F, 1.0F, 0.0F, 0.0F);
            faces->push_back(std::move(face));
        };
        for (int x = 0; x < n; ++x)
        {
            for (int z = 0; z < n; ++z)
            {
                triangle(corner(x, z), corner(x + 1, z), corner(x, z + 1));
                triangle(corner(x + 1, z), corner(x + 1, z + 1), corner(x, z + 1));
            }
        }
        return faces;
    }
}

int main()
{
    bool ok = true;

    {   // Small: nothing to weld, packs as it always did.
        const auto faces = Floor(20, 0.0F, 1.0F);
        ok &= Check(!MapCollisionPacker::Pack(faces).empty(), "a small level packs");
    }

    {   // 200 x 200 cells, 240,000 corners written down, about 40,000 real ones,
        // each copy a hair off the others: over the limit as written, well
        // inside it once the copies are one corner.
        const auto faces = Floor(200, 0.0004F, 1.0F);
        bool packed = false;
        try
        {
            packed = !MapCollisionPacker::Pack(faces).empty();
        }
        catch (const std::exception& error)
        {
            std::cout << "  " << error.what() << '\n';
        }
        ok &= Check(packed, "240,000 corners that are 40,000 corners packs, welded");
    }

    {   // Real detail: every corner is its own, a unit from the next. Nothing a
        // weld small enough to be harmless can do, so it is still refused.
        const auto faces = Floor(200, 0.0F, 50.0F);
        // 40,401 shared corners is under the limit; a floor of triangles that
        // share nothing is not.
        auto apart = std::make_shared<FaceList>();
        float at = 0.0F;
        for (int i = 0; i < 25000; ++i)
        {
            auto face = std::make_shared<CollisionDataEditor>();
            face->Points->push_back(Vector3(at, 0.0F, 0.0F));
            face->Points->push_back(Vector3(at + 20.0F, 0.0F, 0.0F));
            face->Points->push_back(Vector3(at, 0.0F, 20.0F));
            face->Plane = Vector4(0.0F, 1.0F, 0.0F, 0.0F);
            apart->push_back(std::move(face));
            at += 40.0F;
        }
        bool refused = false;
        try
        {
            static_cast<void>(MapCollisionPacker::Pack(apart));
        }
        catch (const MphRead::ProgramException&)
        {
            refused = true;
        }
        ok &= Check(refused, "75,000 corners that really are 75,000 is still refused");
        static_cast<void>(faces);
    }

    std::cout << (ok ? "ALL PASS" : "FAILED") << '\n';
    return ok ? 0 : 1;
}
