#pragma once

#include <cstdint>

namespace MphRead::Entities
{
    class PlayerEntity;
}

namespace MphRead::Mods::Network
{
    // Replicates the disrupt timer; the player/HUD policy owns the one-time
    // local distortion and sound when a disruption begins.
    class DisruptStatusEffectPolicy final
    {
    public:
        DisruptStatusEffectPolicy() = delete;

        static void ApplyAuthoritativeHit(Entities::PlayerEntity& victim);
        [[nodiscard]] static std::uint16_t Capture(const Entities::PlayerEntity& player);
        [[nodiscard]] static std::uint16_t Current(const Entities::PlayerEntity& player);
        static void Start(Entities::PlayerEntity& player, std::uint16_t frames);
        static void Update(Entities::PlayerEntity& player, std::uint16_t frames);
        static void End(Entities::PlayerEntity& player);
    };
}
