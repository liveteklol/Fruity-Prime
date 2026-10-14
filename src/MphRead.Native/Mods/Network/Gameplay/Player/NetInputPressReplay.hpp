#pragma once

#include "Mods/Network/Domain/NetworkCapacity.hpp"
#include "Mods/Network/Protocol/Packets/RealtimePackets.hpp"

#include <array>
#include <cstdint>

namespace MphRead::Mods::Network
{
    struct NetInputPressReplayResult final
    {
        IntentButtons Pressed = IntentButtons::None;
        std::int32_t ShootAge = 0;
        std::int32_t AltAttackAge = 0;
    };

    // Deduplicates the bounded press history received with each player intent.
    // This is separate from NetInputHistory, which records the local sender's
    // edges for retransmission.
    class NetInputPressReplay final
    {
    public:
        NetInputPressReplay() = delete;

        // The caller validates slot generation/life before consuming. A new
        // generation or life starts a fresh replay cursor automatically.
        static NetInputPressReplayResult Consume(std::int32_t slot, const IntentPacket& intent) noexcept;
        // Advances the replay cursor without returning edges for an intent
        // that is not allowed to drive combat or movement.
        static void Discard(std::int32_t slot, const IntentPacket& intent) noexcept;
        static void ForgetSlot(std::int32_t slot) noexcept;
        static void Reset() noexcept;

    private:
        struct Cursor final
        {
            std::uint32_t LastFrame = 0;
            std::uint16_t SlotGeneration = 0;
            std::uint16_t LifeId = 0;
            bool Initialized = false;
        };

        static std::array<Cursor, NetworkSlotCapacity> _cursors;
    };
}
