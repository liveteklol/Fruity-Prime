#include "Mods/Network/Gameplay/Claims/Pickup/PickupClaimOutbox.hpp"

#include "Mods/Network/Gameplay/Claims/Common/ClaimResend.hpp"

#include <algorithm>

namespace MphRead::Mods::Network
{
    std::uint16_t PickupClaimOutbox::Enqueue(PickupClaimPacket claim) noexcept
    {
        const auto free = std::find_if(_entries.begin(), _entries.end(), [](const Entry& entry)
        {
            return !entry.Live;
        });
        if (free == _entries.end())
        {
            return 0;
        }

        _nextId = static_cast<std::uint16_t>(_nextId + 1U == 0x10000U ? 1U : _nextId + 1U);
        claim.ClaimId = _nextId;
        *free = Entry{ true, claim, 0, 0, 0 };
        _declared++;
        return _nextId;
    }

    std::int32_t PickupClaimOutbox::Compose(std::span<std::uint8_t> dest, std::int32_t resendInterval)
    {
        if (dest.empty())
        {
            return 0;
        }

        std::size_t offset = 1;
        std::int32_t count = 0;
        for (Entry& entry : _entries)
        {
            if (!entry.Live || count == PickupClaimPacket::MaxPerPacket
                || offset + PickupClaimPacket::Size > dest.size()
                || !ClaimResendDue(entry.Sends, MaxSends, entry.Age, entry.LastSentAge, resendInterval))
            {
                continue;
            }
            entry.Claim.Write(dest.subspan(offset, PickupClaimPacket::Size));
            offset += PickupClaimPacket::Size;
            entry.Sends++;
            entry.LastSentAge = entry.Age;
            count++;
        }

        if (count == 0)
        {
            return 0;
        }
        dest[0] = static_cast<std::uint8_t>(count);
        return static_cast<std::int32_t>(offset);
    }

    bool PickupClaimOutbox::Resolve(std::uint16_t claimId) noexcept
    {
        for (Entry& entry : _entries)
        {
            if (entry.Live && entry.Claim.ClaimId == claimId)
            {
                entry.Live = false;
                return true;
            }
        }
        return false;
    }

    void PickupClaimOutbox::Reset() noexcept
    {
        _entries = {};
        _declared = 0;
        _unanswered = 0;
        // Keep the sequence across match/session resets so a delayed verdict
        // cannot alias the first claim in the next stream.
    }

    std::size_t PickupClaimOutbox::PendingCount() const noexcept
    {
        return static_cast<std::size_t>(std::count_if(_entries.begin(), _entries.end(), [](const Entry& entry)
        {
            return entry.Live;
        }));
    }
}
