#pragma once

#include "Mods/Network/Protocol/NetProtocol.hpp"

#include <array>
#include <cstdint>

namespace MphRead::Mods::Network
{
    // This machine's own shot events: a sequence number per shot, with the
    // weapon, charge, world and ray it left with, taken the moment it is
    // fired. The last few ride every intent (IntentPacket::ShotHistory),
    // newest last. Continuous fire makes no events -- a Shock Coil would push
    // every other shot out each frame.
    class LocalShotLog final
    {
    public:
        // The newest shot recorded since the last Reset: what the projectiles
        // spawned right after it are stamped with.
        [[nodiscard]] const IntentPacket::ShotEvent* Latest() const noexcept
        {
            return _length > 0 ? &_history[_length - 1] : nullptr;
        }
        // Gives the shot its sequence and keeps it.
        std::uint32_t Record(IntentPacket::ShotEvent shot) noexcept;
        void SetHomingTarget(std::uint32_t sequence,
            const IntentPacket::ShotTargetReference& target) noexcept;
        // How long a shot is repeated: as long as a receiver may still fire it
        // (RemoteShotQueue::FreshFrames, checked where both are known).
        static constexpr std::uint32_t RepeatFrames = 30;
        // The history, into an intent about to be sent on frame `now`: the
        // shots fired in the last RepeatFrames, oldest first.
        void Fill(IntentPacket& intent, std::uint32_t now) const noexcept;
        void Reset() noexcept;
        // How many shots this machine has numbered: the sequence of the last.
        [[nodiscard]] std::uint32_t Sent() const noexcept { return _sequence; }

    private:
        [[nodiscard]] static constexpr std::uint32_t Advance(std::uint32_t sequence) noexcept
        {
            return sequence + 1U == 0U ? 1U : sequence + 1U;
        }

        std::array<IntentPacket::ShotEvent, IntentPacket::ShotHistoryCount> _history{};
        std::uint8_t _length = 0;
        // Not reset with the history: a sequence is never reused, so a copy
        // that kept an older one cannot mistake a new shot for it.
        std::uint32_t _sequence = 0;
    };
}
