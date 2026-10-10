#include "RemoteShotQueue.hpp"

#include "NetLifecycleTracker.hpp"
#include "../../Formats/Enums.hpp"

#include <algorithm>

namespace MphRead::Mods::Network
{
    namespace
    {
        // Whether 0 -- a sequence no sender uses -- lies in [from, from + span).
        [[nodiscard]] constexpr bool CoversZero(std::uint32_t from, std::uint32_t span) noexcept
        {
            return static_cast<std::uint32_t>(0U - from) < span;
        }
    }

    ShotQueueStats& ShotQueueStats::operator+=(const ShotQueueStats& other) noexcept
    {
        Received += other.Received;
        Fired += other.Fired;
        Stale += other.Stale;
        Overflow += other.Overflow;
        Abandoned += other.Abandoned;
        Gaps += other.Gaps;
        Recovered += other.Recovered;
        Lost += other.Lost;
        OutOfOrder += other.OutOfOrder;
        Waiting += other.Waiting;
        Pending += other.Pending;
        Overdue += other.Overdue;
        return *this;
    }

    void RemoteShotQueue::Receive(const IntentPacket& intent) noexcept
    {
        _active = true;
        if (_newestIntentFrame == 0 || NetLifecycleTracker::Newer(intent.Frame, _newestIntentFrame))
        {
            _newestIntentFrame = intent.Frame;
        }
        // The first intent from a sender repeats shots fired before this
        // machine was watching: where the sequence is, not shots to fire.
        const bool watching = _lastSequence != 0;
        const std::size_t length = std::min<std::size_t>(intent.ShotHistoryLength, intent.ShotHistory.size());
        for (std::size_t i = 0; i < length; ++i)
        {
            const IntentPacket::ShotEvent& event = intent.ShotHistory[i];
            if (event.Sequence == 0
                || event.WeaponId > static_cast<std::uint8_t>(::MphRead::BeamType::OmegaCannon))
            {
                continue;
            }
            if (_lastSequence == 0 || NetLifecycleTracker::Newer(event.Sequence, _lastSequence))
            {
                if (!watching && intent.Frame - event.Frame > FreshFrames)
                {
                    _lastSequence = event.Sequence;
                    continue;
                }
                if (_lastSequence != 0)
                {
                    AwaitSkipped(_lastSequence + 1U, event.Sequence);
                }
                _lastSequence = event.Sequence;
                Enqueue(event);
            }
            else if (TakeSkipped(event.Sequence))
            {
                // Skipped by an intent that overtook this one: late, not lost.
                ++_stats.Recovered;
                Enqueue(event);
            }
        }
        ExpireSkipped();
    }

    std::optional<IntentPacket::ShotEvent> RemoteShotQueue::Next() noexcept
    {
        while (_count > 0 && _newestIntentFrame - _queue[0].Frame > FreshFrames)
        {
            ++_stats.Stale;
            PopFront();
        }
        if (_count == 0)
        {
            return std::nullopt;
        }
        return _queue[0];
    }

    void RemoteShotQueue::Consume() noexcept
    {
        if (_count == 0)
        {
            return;
        }
        const std::uint32_t sequence = _queue[0].Sequence;
        if (_lastFired != 0 && NetLifecycleTracker::Newer(_lastFired, sequence))
        {
            ++_stats.OutOfOrder;
        }
        else
        {
            _lastFired = sequence;
        }
        ++_stats.Fired;
        PopFront();
    }

    void RemoteShotQueue::BeginLife() noexcept
    {
        if (!_lived)
        {
            _lived = true;
            return;
        }
        _stats.Abandoned += _count;
        _stats.Lost += _skippedCount;
        _count = 0;
        _skippedCount = 0;
        _lastFired = 0;
    }

    ShotQueueStats RemoteShotQueue::Retire() noexcept
    {
        _stats.Abandoned += _count;
        _stats.Lost += _skippedCount;
        const ShotQueueStats stats = _stats;
        *this = RemoteShotQueue{};
        return stats;
    }

    ShotQueueStats RemoteShotQueue::Stats() const noexcept
    {
        ShotQueueStats stats = _stats;
        stats.Waiting = _count;
        stats.Pending = _skippedCount;
        for (std::size_t i = 0; i < _count; ++i)
        {
            if (_newestIntentFrame - _queue[i].Frame > FreshFrames)
            {
                ++stats.Overdue;
            }
        }
        return stats;
    }

    void RemoteShotQueue::Enqueue(const IntentPacket::ShotEvent& event) noexcept
    {
        ++_stats.Received;
        if (_count == _queue.size())
        {
            ++_stats.Overflow;
            PopFront();
        }
        // In sequence order: a recovered event goes before the newer ones.
        std::size_t at = _count;
        while (at > 0 && NetLifecycleTracker::Newer(_queue[at - 1].Sequence, event.Sequence))
        {
            _queue[at] = _queue[at - 1];
            --at;
        }
        _queue[at] = event;
        ++_count;
    }

    void RemoteShotQueue::PopFront() noexcept
    {
        std::rotate(_queue.begin(), _queue.begin() + 1, _queue.begin() + static_cast<std::ptrdiff_t>(_count));
        --_count;
    }

    void RemoteShotQueue::AwaitSkipped(std::uint32_t from, std::uint32_t to) noexcept
    {
        // [from, to): every sequence the sender used in between, once each.
        const std::uint32_t span = to - from;
        _stats.Gaps += span - (CoversZero(from, span) ? 1U : 0U);
        // Only the newest MissingWindow can still be carried by an intent in
        // flight; anything before them is lost already.
        const std::uint32_t tracked = std::min(span, MissingWindow);
        const std::uint32_t untracked = span - tracked;
        _stats.Lost += untracked - (CoversZero(from, untracked) ? 1U : 0U);
        for (std::uint32_t sequence = to - tracked; sequence != to; ++sequence)
        {
            if (sequence != 0)
            {
                Await(sequence);
            }
        }
    }

    void RemoteShotQueue::Await(std::uint32_t sequence) noexcept
    {
        if (_skippedCount == _skipped.size())
        {
            ++_stats.Lost;
            std::rotate(_skipped.begin(), _skipped.begin() + 1, _skipped.end());
            --_skippedCount;
        }
        _skipped[_skippedCount++] = sequence;
    }

    bool RemoteShotQueue::TakeSkipped(std::uint32_t sequence) noexcept
    {
        for (std::size_t i = 0; i < _skippedCount; ++i)
        {
            if (_skipped[i] == sequence)
            {
                std::rotate(_skipped.begin() + static_cast<std::ptrdiff_t>(i),
                    _skipped.begin() + static_cast<std::ptrdiff_t>(i) + 1,
                    _skipped.begin() + static_cast<std::ptrdiff_t>(_skippedCount));
                --_skippedCount;
                return true;
            }
        }
        return false;
    }

    void RemoteShotQueue::ExpireSkipped() noexcept
    {
        std::size_t kept = 0;
        for (std::size_t i = 0; i < _skippedCount; ++i)
        {
            if (_lastSequence - _skipped[i] > MissingWindow)
            {
                ++_stats.Lost;
            }
            else
            {
                _skipped[kept++] = _skipped[i];
            }
        }
        _skippedCount = kept;
    }
}
