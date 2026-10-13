#pragma once

#include "Mods/Network/Gameplay/Claims/Hit/ClaimDamageBound.hpp"

#include <cstdint>

namespace MphRead::Mods::Network
{
    enum class ClaimDamageKind : std::uint8_t
    {
        Direct,
        Splash,
        Headshot
    };

    // Authority facts needed to bound one claimed hit. Constructed by the
    // gameplay adapter, then consumed by a deterministic policy.
    struct ClaimDamageProfile final
    {
        ChargeAmounts Direct{};
        ChargeAmounts Splash{};
        ChargeAmounts Headshot{};
        float Effectiveness = 1.0F;
        float Level = 1.0F;
        std::int32_t MinCharge = 0;
        std::int32_t FullCharge = 0;
        std::uint8_t ShotCharge = 0xFF;
        bool HasWeapon = false;
        bool HasShotCharge = false;
        bool CanCharge = false;
        bool PartialCharge = false;
        bool PrimeHunter = false;
    };

    struct ClaimDamageSettlement final
    {
        std::uint16_t Damage = 0;
        std::uint16_t TurretDamage = 0;
        std::uint8_t Flags = 0;
        std::int32_t Claimed = 0;
        std::int32_t Allowed = 0;
        bool Undoubled = false;
        bool Capped = false;
    };

    // Applies the authority's immutable weapon, charge, resistance, match and
    // granted-power-up facts to a claimed damage split. This class owns no
    // game state and does not log or apply damage to entities.
    class ClaimDamagePolicy final
    {
    public:
        [[nodiscard]] static ClaimDamageSettlement Settle(const ClaimDamageProfile& profile,
            ClaimDamageKind kind, std::uint16_t damage, std::uint16_t turretDamage,
            std::uint8_t flags, bool doubleDamageGranted) noexcept;

    private:
        [[nodiscard]] static std::int32_t Allowed(const ClaimDamageProfile& profile,
            ClaimDamageKind kind, bool doubled) noexcept;
    };
}
