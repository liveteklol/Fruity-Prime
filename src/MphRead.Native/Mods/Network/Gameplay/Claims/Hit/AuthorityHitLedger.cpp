#include "Mods/Network/Gameplay/Claims/Hit/AuthorityHitLedger.hpp"

#include "NativeRuntime/System/BinaryPrimitives.hpp"
#include "NativeRuntime/System/Managed.hpp"

#include <bit>
#include <cstdlib>
#include <limits>

namespace MphRead::Mods::Network
{
    void AuthorityHitLedger::Record(std::int32_t attacker, std::int32_t victim, std::uint32_t frame,
        std::uint32_t ack, std::uint32_t launch, std::int32_t damage, bool used,
        ::MphRead::BeamType beam, std::optional<ShotKey> launchKey)
    {
        if (attacker < 0 || attacker >= static_cast<std::int32_t>(SlotCapacity)
            || victim < 0 || victim >= static_cast<std::int32_t>(SlotCapacity))
        {
            return;
        }
        const auto a = static_cast<std::size_t>(attacker);
        const auto v = static_cast<std::size_t>(victim);
        Entry& entry = _entries[a][v][_next[a][v]];
        entry.Frame = frame;
        entry.Ack = ack;
        entry.Launch = launch;
        entry.Damage = damage;
        entry.Used = used;
        entry.Key = launchKey.has_value() ? *launchKey : ShotKey::For(attacker, launch);
        entry.Beam = beam;
        _next[a][v] = (_next[a][v] + 1) % HistoryDepth;
    }

    std::int32_t AuthorityHitLedger::NearestOffset(std::int32_t attacker, std::int32_t victim,
        std::uint32_t arrived) const
    {
        if (attacker < 0 || attacker >= static_cast<std::int32_t>(SlotCapacity)
            || victim < 0 || victim >= static_cast<std::int32_t>(SlotCapacity))
        {
            return std::numeric_limits<std::int32_t>::min();
        }
        const auto a = static_cast<std::size_t>(attacker);
        const auto v = static_cast<std::size_t>(victim);
        std::int32_t best = std::numeric_limits<std::int32_t>::min();
        for (const Entry& entry : _entries[a][v])
        {
            if (entry.Frame == 0)
            {
                continue;
            }
            const std::int32_t offset = ::MphRead::NativeRuntime::UncheckedSubtract(
                std::bit_cast<std::int32_t>(arrived), std::bit_cast<std::int32_t>(entry.Frame));
            if (best == std::numeric_limits<std::int32_t>::min())
            {
                best = offset;
                continue;
            }
            const auto mathAbs = [](std::int32_t value)
            {
                if (value == std::numeric_limits<std::int32_t>::min())
                {
                    throw ::System::OverflowException();
                }
                return value < 0 ? -value : value;
            };
            if (mathAbs(offset) < mathAbs(best))
            {
                best = offset;
            }
        }
        return best;
    }

    AuthorityHitLedger::Match AuthorityHitLedger::Take(std::int32_t attacker, std::int32_t victim,
        std::uint32_t claimAck, std::uint32_t claimLaunch, std::uint32_t arrived, std::int32_t window,
        std::optional<ShotKey> strictKey, ::MphRead::BeamType strictBeam) noexcept
    {
        if (attacker < 0 || attacker >= static_cast<std::int32_t>(SlotCapacity)
            || victim < 0 || victim >= static_cast<std::int32_t>(SlotCapacity))
        {
            return {};
        }
        PairHistory& entries = _entries[static_cast<std::size_t>(attacker)][static_cast<std::size_t>(victim)];
        if (strictKey)
        {
            // Obstructed shots cannot borrow a nearby open shot's ledger entry.
            for (Entry& entry : entries)
            {
                if (!entry.Used && entry.Frame != 0 && entry.Key == *strictKey && entry.Beam == strictBeam)
                {
                    entry.Used = true;
                    return {MatchKind::Launch, entry.Damage};
                }
            }
            return {};
        }
        if (claimLaunch != 0)
        {
            for (Entry& entry : entries)
            {
                if (entry.Used || entry.Frame == 0)
                {
                    continue;
                }
                if (entry.Launch != 0
                    && std::llabs(static_cast<std::int64_t>(entry.Launch) - claimLaunch) <= LaunchMatchFrames)
                {
                    entry.Used = true;
                    return {MatchKind::Launch, entry.Damage};
                }
            }
        }
        const std::uint32_t floor = arrived > static_cast<std::uint32_t>(window)
            ? arrived - static_cast<std::uint32_t>(window) : 0U;
        Entry* best = nullptr;
        std::int64_t bestGap = std::numeric_limits<std::int64_t>::max();
        for (Entry& entry : entries)
        {
            if (entry.Used)
            {
                continue;
            }
            if (entry.Frame == 0 || entry.Frame < floor || entry.Frame > arrived + static_cast<std::uint32_t>(window))
            {
                continue;
            }
            if (entry.Launch != 0 && claimLaunch != 0)
            {
                continue;
            }
            const std::int64_t gap = std::llabs(static_cast<std::int64_t>(entry.Ack) - claimAck);
            if (gap <= AckMatchFrames && gap < bestGap)
            {
                bestGap = gap;
                best = &entry;
            }
        }
        if (best == nullptr)
        {
            return {};
        }
        best->Used = true;
        return {MatchKind::AckWindow, best->Damage};
    }

    void AuthorityHitLedger::ClearPair(std::int32_t attacker, std::int32_t victim) noexcept
    {
        if (attacker < 0 || attacker >= static_cast<std::int32_t>(SlotCapacity)
            || victim < 0 || victim >= static_cast<std::int32_t>(SlotCapacity))
        {
            return;
        }
        const auto a = static_cast<std::size_t>(attacker);
        const auto v = static_cast<std::size_t>(victim);
        _entries[a][v].fill(Entry{});
        _next[a][v] = 0;
    }

    void AuthorityHitLedger::ClearSlot(std::int32_t slot) noexcept
    {
        if (slot < 0 || slot >= static_cast<std::int32_t>(SlotCapacity))
        {
            return;
        }
        for (std::int32_t other = 0; other < static_cast<std::int32_t>(SlotCapacity); other++)
        {
            ClearPair(slot, other);
            ClearPair(other, slot);
        }
    }

    void AuthorityHitLedger::Reset() noexcept
    {
        _entries = {};
        _next = {};
    }
}
