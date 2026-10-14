#include "Mods/Network/Gameplay/Weapons/ShotEventLedger.hpp"

#include "Mods/Network/Session/NetLifecycleTracker.hpp"

namespace MphRead::Mods::Network
{
    void ShotEventLedger::Note(const IntentPacket::ShotEvent& event) noexcept
    {
        if (!event.IsValid())
        {
            return;
        }
        IntentPacket::ShotEvent& kept = _events[event.Sequence % Capacity];
        if (kept.Sequence == event.Sequence)
        {
            // Every intent repeats the event; one that says something else
            // under the same sequence is not the shot that was kept.
            IntentPacket::ShotEvent merged{};
            if (!kept.TryMergeHomingDecision(event, merged))
            {
                ++_conflicts;
            }
            else
            {
                kept = merged;
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
