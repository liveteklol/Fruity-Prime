#include "Mods/Network/Gameplay/Claims/Hit/HitClaimOutbox.hpp"
#include "Mods/Network/Gameplay/Claims/Common/ClaimResend.hpp"

#include "NativeRuntime/System/BinaryPrimitives.hpp"
#include "NativeRuntime/System/Managed.hpp"

#include <algorithm>

namespace MphRead::Mods::Network
{
    namespace Runtime = ::MphRead::NativeRuntime;

    std::uint16_t HitClaimOutbox::Enqueue(Entry entry) noexcept
    {
        std::size_t index = Capacity;
        for (std::size_t i = 0; i < Capacity; i++)
        {
            if (!_entries[i].Live)
            {
                index = i;
                break;
            }
        }
        if (index == Capacity)
        {
            index = 0;
            for (std::size_t i = 1; i < Capacity; i++)
            {
                if (_entries[i].Age > _entries[index].Age)
                {
                    index = i;
                }
            }
            _unanswered++;
        }

        entry.Id = _nextId;
        entry.Age = 0;
        entry.Sends = 0;
        entry.LastSentAge = 0;
        entry.Live = true;
        _entries[index] = entry;

        const std::uint16_t id = _nextId;
        _nextId++;
        if (_nextId == 0)
        {
            _nextId = 1;
        }
        _declared++;
        return id;
    }

    std::int32_t HitClaimOutbox::Compose(std::span<std::uint8_t> destination, std::int32_t resendInterval)
    {
        std::int32_t count = 0;
        std::size_t offset = 1;
        for (Entry& entry : _entries)
        {
            if (count >= HitClaimPacket::MaxPerPacket)
            {
                break;
            }
            if (!entry.Live || !ClaimResendDue(entry.Sends, MaxSends, entry.Age, entry.LastSentAge, resendInterval))
            {
                continue;
            }

            HitClaimPacket packet;
            packet.MatchId = entry.MatchId;
            packet.AuthorityEpoch = entry.AuthorityEpoch;
            packet.ShooterGeneration = entry.ShooterGeneration;
            packet.ShooterLifeId = entry.ShooterLifeId;
            packet.VictimGeneration = entry.VictimGeneration;
            packet.VictimLifeId = entry.VictimLifeId;
            packet.ClaimId = entry.Id;
            packet.Frame = entry.Frame;
            packet.AckFrame = entry.AckFrame;
            packet.LaunchFrame = entry.LaunchFrame;
            packet.ShotSequence = entry.ShotSequence;
            packet.VictimSlot = entry.VictimSlot;
            packet.Beam = entry.Beam;
            packet.Damage = entry.Damage;
            packet.TurretDamage = entry.TurretDamage;
            packet.Cause = entry.Cause;
            packet.Flags = entry.Flags;
            packet.HitPoint = entry.HitPoint;
            packet.Impulse = entry.Impulse;
            packet.Impact = entry.Impact;
            packet.Write(Runtime::SpanSlice(destination, offset));
            offset += HitClaimPacket::Size;

            if (entry.Sends > 0)
            {
                _resends++;
            }
            entry.Sends++;
            entry.LastSentAge = entry.Age;
            count++;
        }
        if (count == 0)
        {
            return 0;
        }
        Runtime::ManagedAt(destination, 0) = static_cast<std::uint8_t>(count);
        return static_cast<std::int32_t>(offset);
    }

    HitClaimOutbox::Entry* HitClaimOutbox::FindLive(std::uint16_t id) noexcept
    {
        const auto found = std::find_if(_entries.begin(), _entries.end(), [id](const Entry& entry)
        {
            return entry.Live && entry.Id == id;
        });
        return found == _entries.end() ? nullptr : &*found;
    }

    void HitClaimOutbox::Complete(std::uint16_t id) noexcept
    {
        if (Entry* entry = FindLive(id))
        {
            entry->Live = false;
        }
    }

    void HitClaimOutbox::ForgetSlot(std::int32_t slot, std::int32_t localSlot) noexcept
    {
        for (Entry& entry : _entries)
        {
            if (entry.VictimSlot == slot || slot == localSlot)
            {
                entry.Live = false;
            }
        }
    }

    void HitClaimOutbox::ClearPending() noexcept
    {
        _entries.fill(Entry{});
    }

    void HitClaimOutbox::Reset() noexcept
    {
        ClearPending();
        _nextId = 1;
        _declared = 0;
        _unanswered = 0;
        _resends = 0;
    }
}
