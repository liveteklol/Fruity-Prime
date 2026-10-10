#include "RemoteBombState.hpp"

#include "NetLifecycleTracker.hpp"

#include <algorithm>
#include <cmath>

namespace MphRead::Mods::Network
{
    void RemoteBombState::Receive(const IntentPacket& intent) noexcept
    {
        if (!intent.HasBombs)
        {
            return;
        }
        if (_newestFrame != 0 && !NetLifecycleTracker::Newer(intent.Frame, _newestFrame))
        {
            return;
        }
        _newestFrame = intent.Frame;
        _active = true;
        _count = 0;
        const std::size_t length = std::min<std::size_t>(intent.BombsLength, intent.Bombs.size());
        for (std::size_t i = 0; i < length; ++i)
        {
            const IntentPacket::Bomb& bomb = intent.Bombs[i];
            if (bomb.Sequence == 0 || !std::isfinite(bomb.Position.X) || !std::isfinite(bomb.Position.Y)
                || !std::isfinite(bomb.Position.Z))
            {
                continue;
            }
            _reported[_count++] = bomb;
        }
    }

    RemoteBombState::Plan RemoteBombState::Reconcile(std::span<const std::uint32_t> held) const noexcept
    {
        Plan plan{};
        for (std::size_t i = 0; i < _count; ++i)
        {
            const IntentPacket::Bomb& bomb = _reported[i];
            if (_lastLaid == 0 || NetLifecycleTracker::Newer(bomb.Sequence, _lastLaid))
            {
                plan.Lay[plan.LayCount++] = bomb;
            }
        }
        std::sort(plan.Lay.begin(), plan.Lay.begin() + static_cast<std::ptrdiff_t>(plan.LayCount),
            [](const IntentPacket::Bomb& a, const IntentPacket::Bomb& b)
            {
                return NetLifecycleTracker::Newer(b.Sequence, a.Sequence);
            });
        if (!_active)
        {
            return plan;
        }
        for (const std::uint32_t sequence : held)
        {
            if (sequence == 0 || plan.DetonateCount == plan.Detonate.size())
            {
                continue;
            }
            const bool standing = std::any_of(_reported.begin(), _reported.begin() + _count,
                [sequence](const IntentPacket::Bomb& bomb) { return bomb.Sequence == sequence && !bomb.Gone; });
            if (!standing)
            {
                plan.Detonate[plan.DetonateCount++] = sequence;
            }
        }
        return plan;
    }

    std::optional<OpenTK::Mathematics::Vector3> RemoteBombState::StandingAt(std::uint32_t sequence) const noexcept
    {
        for (std::size_t i = 0; i < _count; ++i)
        {
            if (_reported[i].Sequence == sequence && !_reported[i].Gone)
            {
                return _reported[i].Position;
            }
        }
        return std::nullopt;
    }

    void RemoteBombState::Laid(std::uint32_t sequence) noexcept
    {
        if (_lastLaid == 0 || NetLifecycleTracker::Newer(sequence, _lastLaid))
        {
            _lastLaid = sequence;
        }
    }
}
