#include "Mods/Network/Gameplay/Claims/Hit/RescuedHitLedger.hpp"

namespace MphRead::Mods::Network
{
    void RescuedHitLedger::Record(std::uint8_t attacker, std::uint8_t victim, ShotKey key,
        std::uint16_t victimGeneration, std::uint16_t victimLife, std::uint32_t frame) noexcept
    {
        for (Entry& entry : _entries)
        {
            if (entry.Owed > 0 && entry.Key == key && entry.Victim == victim
                && entry.VictimGeneration == victimGeneration && entry.VictimLife == victimLife)
            {
                entry.Owed++;
                entry.RecordedAt = frame;
                return;
            }
        }

        Entry& entry = _entries[_next];
        _next = (_next + 1) % Capacity;
        entry = Entry{attacker, victim, key, victimGeneration, victimLife, frame, 1};
    }

    bool RescuedHitLedger::Consume(ShotKey key, std::uint8_t victim,
        std::uint16_t victimGeneration, std::uint16_t victimLife, std::uint32_t frame) noexcept
    {
        for (Entry& entry : _entries)
        {
            if (entry.Owed == 0 || entry.Key != key || entry.Victim != victim
                || entry.VictimGeneration != victimGeneration || entry.VictimLife != victimLife)
            {
                continue;
            }
            if (frame - entry.RecordedAt > MaxAgeFrames)
            {
                entry.Owed = 0;
                continue;
            }
            entry.Owed--;
            return true;
        }
        return false;
    }

    void RescuedHitLedger::ForgetSlot(std::uint8_t slot, bool preserveAttackerFlights) noexcept
    {
        for (Entry& entry : _entries)
        {
            if ((!preserveAttackerFlights && entry.Attacker == slot) || entry.Victim == slot)
            {
                entry.Owed = 0;
            }
        }
    }

    void RescuedHitLedger::Clear() noexcept
    {
        for (Entry& entry : _entries)
        {
            entry.Owed = 0;
        }
    }

    void RescuedHitLedger::Reset() noexcept
    {
        _entries.fill(Entry{});
        _next = 0;
    }
}
