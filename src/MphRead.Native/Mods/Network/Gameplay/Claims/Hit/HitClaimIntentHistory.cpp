#include "Mods/Network/Gameplay/Claims/Hit/HitClaimIntentHistory.hpp"

namespace MphRead::Mods::Network
{
    void HitClaimIntentHistory::Record(std::int32_t slot, const IntentPacket& intent) noexcept
    {
        if (!HasSlot(slot))
        {
            return;
        }

        const std::size_t index = static_cast<std::size_t>(slot);
        std::uint32_t& latestAck = _latestAck[index];
        if (intent.AckFrame > latestAck || intent.AckFrame + 600U < latestAck)
        {
            latestAck = intent.AckFrame;
        }

        _ackFrames[index][intent.Frame % AckFrameCapacity] = AckRecord{intent.Frame, intent.AckFrame};
        if (intent.HasShot)
        {
            _shotRays[index][_shotRayNext[index]] = ClaimShotRay{
                intent.AckFrame, intent.ShotOrigin, intent.ShotDirection.Normalized() };
            _shotRayNext[index] = (_shotRayNext[index] + 1) % ShotRayCapacity;
        }
    }

    bool HitClaimIntentHistory::HasSlot(std::int32_t slot) const noexcept
    {
        return slot >= 0 && slot < static_cast<std::int32_t>(SlotCapacity);
    }

    std::uint32_t HitClaimIntentHistory::LatestAck(std::int32_t slot) const noexcept
    {
        if (!HasSlot(slot))
        {
            return 0;
        }
        return _latestAck[static_cast<std::size_t>(slot)];
    }

    std::optional<std::uint32_t> HitClaimIntentHistory::AckForFrame(
        std::int32_t slot, std::uint32_t frame) const noexcept
    {
        if (!HasSlot(slot))
        {
            return std::nullopt;
        }

        const AckRecord& record = _ackFrames[static_cast<std::size_t>(slot)][frame % AckFrameCapacity];
        return record.IntentFrame == frame ? std::optional<std::uint32_t>(record.AckFrame) : std::nullopt;
    }

    const ClaimShotRay* HitClaimIntentHistory::NearestRay(
        std::int32_t slot, std::uint32_t launch, std::uint32_t maxGapExclusive) const noexcept
    {
        if (!HasSlot(slot))
        {
            return nullptr;
        }

        const std::array<ClaimShotRay, ShotRayCapacity>& rays = _shotRays[static_cast<std::size_t>(slot)];
        const ClaimShotRay* nearest = nullptr;
        std::uint32_t bestGap = maxGapExclusive;
        for (const ClaimShotRay& ray : rays)
        {
            const std::uint32_t gap = ray.Ack > launch ? ray.Ack - launch : launch - ray.Ack;
            if (ray.Ack != 0 && gap < bestGap)
            {
                bestGap = gap;
                nearest = &ray;
            }
        }
        return nearest;
    }

    void HitClaimIntentHistory::Reset() noexcept
    {
        for (auto& rays : _shotRays)
        {
            rays.fill(ClaimShotRay{});
        }
        _shotRayNext.fill(0);
        _latestAck.fill(0);
        for (auto& acks : _ackFrames)
        {
            acks.fill(AckRecord{});
        }
    }
}
