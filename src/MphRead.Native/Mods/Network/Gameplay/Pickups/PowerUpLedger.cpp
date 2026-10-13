#include "Mods/Network/Gameplay/Pickups/PowerUpLedger.hpp"

#include <algorithm>

namespace MphRead::Mods::Network
{
    PowerUpLedger::Grant PowerUpLedger::Begin(std::int32_t slot, PowerUp kind, std::uint32_t start,
        std::uint16_t duration, std::uint16_t life) noexcept
    {
        if (!Valid(slot))
        {
            return Grant{};
        }
        Record& record = _records[static_cast<std::size_t>(slot)][IndexOf(kind)];
        record.Sequence = NextGrantSequence(record.Sequence);
        Grant& running = record.Grants[record.Newest];
        if (running.Sequence != 0 && running.Expire > start)
        {
            // Picked up again while it ran: the old one ends where this begins.
            running.Expire = std::max(running.Start, start);
        }
        record.Newest = (record.Newest + 1) % History;
        record.Grants[record.Newest] = Grant{ record.Sequence, start, start + duration, life };
        return record.Grants[record.Newest];
    }

    void PowerUpLedger::End(std::int32_t slot, PowerUp kind, std::uint32_t frame) noexcept
    {
        if (!Valid(slot))
        {
            return;
        }
        Record& record = _records[static_cast<std::size_t>(slot)][IndexOf(kind)];
        Grant& running = record.Grants[record.Newest];
        if (running.Sequence != 0 && running.Expire > frame)
        {
            running.Expire = std::max(running.Start, frame);
        }
    }

    void PowerUpLedger::EndAll(std::int32_t slot, std::uint32_t frame) noexcept
    {
        for (std::size_t k = 0; k < PowerUpCount; k++)
        {
            End(slot, static_cast<PowerUp>(k), frame);
        }
    }

    PowerUpLedger::Grant PowerUpLedger::Current(std::int32_t slot, PowerUp kind) const noexcept
    {
        if (!Valid(slot))
        {
            return Grant{};
        }
        const Record& record = _records[static_cast<std::size_t>(slot)][IndexOf(kind)];
        return record.Grants[record.Newest];
    }

    bool PowerUpLedger::RanAt(std::int32_t slot, PowerUp kind, std::uint32_t frame, std::uint16_t life) const noexcept
    {
        if (!Valid(slot))
        {
            return false;
        }
        const Record& record = _records[static_cast<std::size_t>(slot)][IndexOf(kind)];
        return std::any_of(record.Grants.begin(), record.Grants.end(), [&](const Grant& grant)
        {
            return grant.Life == life && grant.RunningAt(frame);
        });
    }

    void PowerUpLedger::Forget(std::int32_t slot) noexcept
    {
        if (Valid(slot))
        {
            _records[static_cast<std::size_t>(slot)] = {};
        }
    }

    void PowerUpLedger::Reset() noexcept
    {
        _records = {};
    }
}
