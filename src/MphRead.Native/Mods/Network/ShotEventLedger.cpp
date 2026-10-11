#include "ShotEventLedger.hpp"

#include "NetLifecycleTracker.hpp"

namespace MphRead::Mods::Network
{
    namespace
    {
        [[nodiscard]] bool SameShot(const IntentPacket::ShotEvent& a, const IntentPacket::ShotEvent& b) noexcept
        {
            return a.Frame == b.Frame && a.WeaponId == b.WeaponId && a.Charge == b.Charge
                && a.AckFrame == b.AckFrame && OpenTK::Mathematics::Equal(a.Origin, b.Origin)
                && OpenTK::Mathematics::Equal(a.Direction, b.Direction);
        }
    }

    void ShotEventLedger::Note(const IntentPacket::ShotEvent& event) noexcept
    {
        if (event.Sequence == 0)
        {
            return;
        }
        IntentPacket::ShotEvent& kept = _events[event.Sequence % Capacity];
        if (kept.Sequence == event.Sequence)
        {
            // Every intent repeats the event; one that says something else
            // under the same sequence is not the shot that was kept.
            if (!SameShot(kept, event))
            {
                ++_conflicts;
            }
            return;
        }
        // A late intent's event Capacity shots older shares this entry: it
        // must not take the place of the newer shot claims will name.
        if (kept.Sequence != 0 && !NetLifecycleTracker::Newer(event.Sequence, kept.Sequence))
        {
            ++_staleRefused;
            return;
        }
        kept = event;
    }

    std::optional<IntentPacket::ShotEvent> ShotEventLedger::Find(std::uint32_t sequence) const noexcept
    {
        if (sequence == 0)
        {
            return std::nullopt;
        }
        const IntentPacket::ShotEvent& event = _events[sequence % Capacity];
        if (event.Sequence != sequence)
        {
            return std::nullopt;
        }
        return event;
    }
}
