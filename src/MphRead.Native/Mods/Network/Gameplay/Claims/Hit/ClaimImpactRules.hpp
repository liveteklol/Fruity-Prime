#pragma once

#include "NativeRuntime/OpenTK/Mathematics.hpp"

namespace MphRead::Mods::Network
{
    // Geometry facts collected from the authority's victim and claim packet.
    // The rule can be checked without an Entity, Scene, or network session.
    struct ClaimImpactProfile final
    {
        OpenTK::Mathematics::Vector3 Offset{};
        float MinY = 0.0F;
        float MaxY = 0.0F;
        float Radius = 0.0F;
        bool Known = false;
        bool TurretHit = false;
        bool Splash = false;
        bool AltForm = false;
        bool Headshot = false;
    };

    class ClaimImpactRules final
    {
    public:
        ClaimImpactRules() = delete;

        [[nodiscard]] static bool Plausible(const ClaimImpactProfile& profile) noexcept;
    };
}
