#pragma once

#include "NetProtocol.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace MphRead::Mods::Network
{
    // What became of a remote player's shot events on this machine. Kept
    // across lives, not across occupants: it describes one connection.
    //
    // Every event received ends in exactly one of Fired, Stale, Overflow,
    // Abandoned, or is still Waiting:
    //   Received == Fired + Stale + Overflow + Abandoned + Waiting
    // and every sequence skipped by a newer one in exactly one of Recovered
    // (then counted in Received), Lost, or still Pending:
    //   Gaps == Recovered + Lost + Pending
    struct ShotQueueStats final
    {
        // Distinct events that arrived, each counted once however many
        // intents repeated it.
        std::uint64_t Received = 0;
        // Fired by the player's copy here.
        std::uint64_t Fired = 0;
        // Too old by the time the copy could fire it (in the ball, holding
        // on to an enemy).
        std::uint64_t Stale = 0;
        // Pushed out of a full queue.
        std::uint64_t Overflow = 0;
        // Still queued when the life they were fired in ended.
        std::uint64_t Abandoned = 0;
        // Sequences an event arrived ahead of: possibly lost, possibly late.
        std::uint64_t Gaps = 0;
        // Of those, the ones a later (reordered) intent still delivered.
        std::uint64_t Recovered = 0;
        // Of those, the ones that never came.
        std::uint64_t Lost = 0;
        // Fired after a newer shot had already been fired: delivered late,
        // by a reordered intent. Counted, never waited for.
        std::uint64_t OutOfOrder = 0;
        // Right now: queued and not yet fired, and skipped and still awaited.
        std::uint64_t Waiting = 0;
        std::uint64_t Pending = 0;
        // Of Waiting, those already past FreshFrames: nothing asked for them
        // in time (Next would have dropped them stale), so they are shots the
        // copy could not fire, not the tail of a run still in flight.
        std::uint64_t Overdue = 0;

        // Events that arrived and were never fired.
        [[nodiscard]] std::uint64_t Unfired() const noexcept { return Stale + Overflow + Abandoned; }

        ShotQueueStats& operator+=(const ShotQueueStats& other) noexcept;
    };

    // One remote player's shot events, as this machine received them.
    //
    // Every intent repeats the sender's last few events (IntentPacket::
    // ShotHistory), so the same event arrives many times and an intent that
    // is lost or refused costs nothing. Each is queued once, by sequence, in
    // firing order -- a sequence skipped and then delivered by a reordered
    // intent too. The player's copy fires each as it arrives, with the
    // event's weapon and charge, and fires nothing without one.
    class RemoteShotQueue final
    {
    public:
        static constexpr std::size_t Capacity = 8;
        // How long after its frame (the sender's clock, against the newest
        // intent received) an event may still be fired: past it, the copy
        // could not fire it (morphed, holding on to an enemy) and a late shot
        // is worse than none.
        static constexpr std::uint32_t FreshFrames = 30;
        // How far behind the newest sequence a skipped one is still awaited:
        // past it, no intent that could carry it is still in flight.
        static constexpr std::uint32_t MissingWindow = 16;

        void Receive(const IntentPacket& intent) noexcept;
        // The oldest unspent event, after dropping stale ones.
        [[nodiscard]] std::optional<IntentPacket::ShotEvent> Next() noexcept;
        // The copy fired the oldest event.
        void Consume() noexcept;
        // Whether this player sends shot events at all: a slot that never
        // sent an intent (a bot) keeps firing from its own controls.
        [[nodiscard]] bool Active() const noexcept { return _active; }
        // The player's copy begins a life. What the last one left unfired is
        // abandoned; the sequence carries on, since the sender never reuses
        // one, so the old life's shots repeated by the next intents are not
        // taken for new ones. The first life seen keeps what arrived before
        // it: those are its own shots.
        void BeginLife() noexcept;
        // The occupant leaves: what was waiting is abandoned and what was
        // awaited lost. Returns their statistics, which leave with them, and
        // starts over for the next occupant, whose sequence starts again.
        [[nodiscard]] ShotQueueStats Retire() noexcept;
        [[nodiscard]] ShotQueueStats Stats() const noexcept;

    private:
        void Enqueue(const IntentPacket::ShotEvent& event) noexcept;
        void PopFront() noexcept;
        void AwaitSkipped(std::uint32_t from, std::uint32_t to) noexcept;
        void Await(std::uint32_t sequence) noexcept;
        [[nodiscard]] bool TakeSkipped(std::uint32_t sequence) noexcept;
        void ExpireSkipped() noexcept;

        std::array<IntentPacket::ShotEvent, Capacity> _queue{};
        std::size_t _count = 0;
        std::array<std::uint32_t, MissingWindow> _skipped{};
        std::size_t _skippedCount = 0;
        std::uint32_t _lastSequence = 0;
        std::uint32_t _lastFired = 0;
        std::uint32_t _newestIntentFrame = 0;
        bool _active = false;
        bool _lived = false;
        ShotQueueStats _stats{};
    };
}
