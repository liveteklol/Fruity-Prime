#pragma once

#include <cstdint>

namespace MphRead::Entities
{
    enum class DamageFlags : std::int32_t;
    class PlayerEntity;
}

namespace MphRead::Mods::Network
{
    // Owns authoritative ignition/source and the authority-only DOT rule;
    // snapshot synchronization only replicates remaining time and presentation.
    class BurnStatusEffectPolicy final
    {
    public:
        BurnStatusEffectPolicy() = delete;

        static void ApplyAuthoritativeHit(Entities::PlayerEntity& victim,
            Entities::PlayerEntity& source, std::uint32_t launchFrame);
        [[nodiscard]] static bool IsAuthorityOnlyDamageTick(Entities::DamageFlags flags) noexcept;
        [[nodiscard]] static std::uint16_t Capture(const Entities::PlayerEntity& player);
        [[nodiscard]] static std::uint16_t Current(const Entities::PlayerEntity& player);
        static void Start(Entities::PlayerEntity& player, std::uint16_t frames);
        static void Update(Entities::PlayerEntity& player, std::uint16_t frames);
        static void End(Entities::PlayerEntity& player);
    };
}
