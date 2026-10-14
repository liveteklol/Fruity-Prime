#pragma once

#include "Formats/Enums.hpp"
#include "Mods/Network/Protocol/Packets/RealtimePackets.hpp"
#include "NativeRuntime/OpenTK/Mathematics.hpp"

#include <cstdint>
#include <span>

namespace MphRead::Mods::Network
{
    struct HomingTargetSnapshot final
    {
        IntentPacket::ShotTargetReference Reference{};
        EntityType Type{};
        OpenTK::Mathematics::Vector3 Position{};
        std::int32_t TeamIndex = -1;
        std::int32_t OwnerSlot = -1;
        std::uint32_t Flags = 0;
        bool Targetable = false;
        bool AltForm = false;
        bool Morphing = false;
    };

    struct HomingTargetSelectionRequest final
    {
        IntentPacket::ShotTargetReference ExcludedTarget{};
        OpenTK::Mathematics::Vector3 Origin{};
        OpenTK::Mathematics::Vector3 Direction{};
        EntityType OwnerType{};
        BeamType Beam{};
        std::int32_t OwnerSlot = -1;
        std::int32_t OwnerTeam = -1;
        float Range = 0.0F;
        float Tolerance = 0.0F;
        bool Continuous = false;
    };

    class HomingTargetSelection final
    {
    public:
        HomingTargetSelection() = delete;

        [[nodiscard]] static bool IsEligible(const HomingTargetSnapshot& candidate,
            const HomingTargetSelectionRequest& request) noexcept;
        [[nodiscard]] static bool CanAcquire(const HomingTargetSnapshot& candidate,
            const HomingTargetSelectionRequest& request) noexcept;
        [[nodiscard]] static IntentPacket::ShotTargetReference Select(
            std::span<const HomingTargetSnapshot> candidates,
            const HomingTargetSelectionRequest& request) noexcept;
    };
}
