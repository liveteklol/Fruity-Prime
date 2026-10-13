#pragma once

#include "NativeRuntime/OpenTK/Mathematics.hpp"

#include <cstdint>

namespace MphRead
{
    class Scene;
}

namespace MphRead::Mods::Network
{
    // Owns the network replica's cached shadow projection and visibility policy.
    class ReplicaShadowVisibility final
    {
    public:
        ReplicaShadowVisibility() = delete;

        struct Projection final
        {
            const MphRead::Scene* scene = nullptr;
            const void* roomIdentity = nullptr;
            std::int32_t roomId = -1;
            bool hasGround = false;
            float height = 0.0F;
        };

        struct FadeState final
        {
            float currentAlpha = 0.0F;
            std::int32_t health = 0;
            float deathCountdown = 0.0F;
            std::uint16_t respawnTimer = 0;
            std::uint16_t respawnTime = 0;
        };

        [[nodiscard]] static Projection FindProjection(MphRead::Scene* scene,
            OpenTK::Mathematics::Vector3 bodyCenter);
        [[nodiscard]] static bool MatchesScene(const Projection& projection,
            const MphRead::Scene* scene) noexcept;
        [[nodiscard]] static bool ShouldForceBody(bool remoteReplica,
            const Projection& projection, const FadeState& fade) noexcept;
    };
}
