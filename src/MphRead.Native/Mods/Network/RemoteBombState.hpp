#pragma once

#include "NetProtocol.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace MphRead::Mods::Network
{
    // One remote player's bombs, as their own machine reports them
    // (IntentPacket::Bombs): which this machine's copy has yet to lay, and
    // which of the copy's it must detonate because the owner's are gone.
    //
    // A copy used to lay bombs from its owner's replayed presses. A press is
    // an action whose meaning depends on state -- below three Lockjaw bombs
    // it lays one, at three it detonates them all -- so one lost or replayed
    // twice put a different set of bombs on every machine: a bomb nobody
    // here could see still hurt, and one still drawn here was long gone on
    // the owner's. A reported set is a state, and a state cannot be lost.
    class RemoteBombState final
    {
    public:
        struct Plan final
        {
            // Reported bombs not laid here yet, oldest first; one already gone
            // on the owner's is to be laid and set off at once.
            std::array<IntentPacket::Bomb, IntentPacket::BombCount> Lay{};
            std::size_t LayCount = 0;
            // Bombs held here the owner no longer reports standing.
            std::array<std::uint32_t, 8> Detonate{};
            std::size_t DetonateCount = 0;
        };

        // The newest intent only: an older one's set is out of date, and
        // would detonate a bomb laid since.
        void Receive(const IntentPacket& intent) noexcept;
        // Whether this player reports bombs at all: a slot that never sent
        // a report (a bot, an older build) lays its own from its controls.
        [[nodiscard]] bool Active() const noexcept { return _active; }
        // Given the sequences this copy holds now, what to lay and detonate.
        // Laying is only planned: Laid records one that was actually placed,
        // so a bomb that cannot be placed yet (a Lockjaw chain still going
        // off) is placed when it can, and never twice.
        [[nodiscard]] Plan Reconcile(std::span<const std::uint32_t> held) const noexcept;
        void Laid(std::uint32_t sequence) noexcept;
        // Where the owner has a bomb standing now, if it does: a bomb that
        // moves (a Stinglarva going for a target) is where its owner says.
        [[nodiscard]] std::optional<OpenTK::Mathematics::Vector3> StandingAt(std::uint32_t sequence) const noexcept;
        // A new occupant: their sequence starts again.
        void Reset() noexcept { *this = RemoteBombState{}; }

    private:
        std::array<IntentPacket::Bomb, IntentPacket::BombCount> _reported{};
        std::uint8_t _count = 0;
        std::uint32_t _newestFrame = 0;
        std::uint32_t _lastLaid = 0;
        bool _active = false;
    };
}
