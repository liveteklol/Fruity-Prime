#pragma once

#include "NativeRuntime/OpenTK/Mathematics.hpp"

#include <cstdint>

namespace MphRead
{
    class Scene;
}

namespace MphRead::Mods::Network
{
    // Owns the replica visibility policy based on the shadow queued by PlayerDraw.
    class ReplicaShadowVisibility final
    {
    public:
        ReplicaShadowVisibility() = delete;

        struct DrawState final
        {
            const MphRead::Scene* scene = nullptr;
            const void* roomIdentity = nullptr;
            std::int32_t roomId = -1;
            bool shadowDrawn = false;
        };

        [[nodiscard]] static DrawState CaptureVisibleShadow(MphRead::Scene* scene);
        [[nodiscard]] static bool MatchesScene(const DrawState& state,
            const MphRead::Scene* scene) noexcept;
        [[nodiscard]] static bool ShouldForceBody(bool remoteReplica,
            const DrawState& state) noexcept;
    };
}
