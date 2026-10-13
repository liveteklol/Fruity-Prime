#pragma once

#include "Mods/Network/Gameplay/StatusEffects/FreezePredictionLedger.hpp"

#include <cstdint>

namespace MphRead::Entities
{
    class PlayerEntity;
}

namespace MphRead::Mods::Network
{
    // Freeze behavior layered on the common timed-effect lifecycle. It keeps
    // prediction acknowledgement and the action/ice presentation policy here.
    class FreezeStatusEffectPolicy final
    {
    public:
        FreezeStatusEffectPolicy() = delete;

        [[nodiscard]] static std::uint16_t Capture(const Entities::PlayerEntity& player);
        [[nodiscard]] static std::uint16_t Current(const Entities::PlayerEntity& player);
        static void ApplyAuthoritativeHit(Entities::PlayerEntity& victim);
        static void Start(Entities::PlayerEntity& player, std::int32_t slot, std::uint16_t frames);
        static void Update(Entities::PlayerEntity& player, std::int32_t slot, std::uint16_t frames);
        static void End(Entities::PlayerEntity& player);
        // This machine's own hit froze its copy of `slot`: kept until the
        // authority's freeze arrives, a round trip later.
        static void NotePredicted(std::int32_t slot);
        [[nodiscard]] static bool AwaitingAuthority(std::int32_t slot);
        static void ForgetSlot(std::int32_t slot);
        static void Reset();

    private:
        inline static FreezePredictionLedger _predictions{};
    };
}
