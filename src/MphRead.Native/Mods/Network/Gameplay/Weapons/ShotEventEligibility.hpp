#pragma once

#include "Mods/Network/Session/NetLifecycleTracker.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace MphRead::Mods::Network
{
    [[nodiscard]] inline bool IsValidShotEventFrame(std::uint32_t eventFrame,
        std::uint32_t intentFrame) noexcept
    {
        return eventFrame != 0 && intentFrame != 0
            && (eventFrame == intentFrame || !NetLifecycleTracker::Newer(eventFrame, intentFrame));
    }

    enum class ShotEventEligibility : std::uint8_t
    {
        Pending,
        Allowed,
        Denied
    };

    // Tracks authority-accepted combat-state transitions in the sender's
    // intent clock. The history spans lives for one occupant so delayed shots
    // keep the eligibility they had when fired; a generation change resets it.
    class ShotEventEligibilityTimeline final
    {
    public:
        // Keep one accepted state transition for every frame in the rewind
        // window; claims can arrive well after the shot event was fired.
        static constexpr std::size_t Capacity = 128;

        void Record(std::uint16_t generation, std::uint16_t lifeId,
            std::uint32_t frame, bool eligible) noexcept
        {
            if (generation == 0 || lifeId == 0 || frame == 0)
            {
                return;
            }
            if (!_hasIdentity || generation != _generation)
            {
                Reset();
                _generation = generation;
                _hasIdentity = true;
            }
            if (_hasLatestFrame && frame != _latestFrame
                && !NetLifecycleTracker::Newer(frame, _latestFrame))
            {
                return;
            }
            if (_hasLatestFrame && frame == _latestFrame)
            {
                // Two identities or states accepted at one sender frame are
                // ambiguous; fail closed for both sides of that boundary.
                if (lifeId != _latestLifeId || eligible != _latestEligibility)
                {
                    _latestEligibility = false;
                    _latestLifeId = lifeId;
                    if (_count > 0)
                    {
                        _entries[_count - 1] = {frame, lifeId, false};
                    }
                }
                return;
            }

            _hasLatestFrame = true;
            _latestFrame = frame;
            _latestLifeId = lifeId;
            _latestEligibility = eligible;
            if (_count == 0 || _entries[_count - 1].LifeId != lifeId
                || _entries[_count - 1].Eligible != eligible)
            {
                Append({frame, lifeId, eligible});
            }
        }

        [[nodiscard]] ShotEventEligibility EvaluateFrame(std::uint16_t generation,
            std::uint32_t frame) const noexcept
        {
            if (!_hasIdentity || generation != _generation || !_hasLatestFrame
                || _count == 0 || frame == 0)
            {
                return ShotEventEligibility::Pending;
            }
            if (frame != _latestFrame && NetLifecycleTracker::Newer(frame, _latestFrame))
            {
                return ShotEventEligibility::Pending;
            }

            const Entry* entry = AtOrBefore(frame);
            // Before the first accepted sample, preserve a legitimate delayed
            // shot from this occupant only when the first accepted state was
            // in play; the queue separately limits its age.
            return (entry == nullptr ? _entries[0].Eligible : entry->Eligible)
                ? ShotEventEligibility::Allowed : ShotEventEligibility::Denied;
        }

        [[nodiscard]] ShotEventEligibility Evaluate(std::uint16_t generation,
            std::uint16_t lifeId, std::uint32_t frame) const noexcept
        {
            if (!_hasIdentity || generation != _generation || !_hasLatestFrame
                || _count == 0 || lifeId == 0 || frame == 0)
            {
                return ShotEventEligibility::Pending;
            }
            if (frame != _latestFrame && NetLifecycleTracker::Newer(frame, _latestFrame))
            {
                return ShotEventEligibility::Pending;
            }

            const Entry* entry = AtOrBefore(frame);
            if (entry == nullptr)
            {
                return _entries[0].LifeId == lifeId && _entries[0].Eligible
                    ? ShotEventEligibility::Allowed : ShotEventEligibility::Denied;
            }
            if (entry->LifeId != lifeId)
            {
                return ShotEventEligibility::Denied;
            }
            return entry->Eligible ? ShotEventEligibility::Allowed : ShotEventEligibility::Denied;
        }

        [[nodiscard]] bool LatestAllowsContinuousFire(std::uint16_t generation,
            std::uint16_t lifeId) const noexcept
        {
            return _hasIdentity && generation == _generation && lifeId == _latestLifeId
                && _hasLatestFrame && _latestEligibility;
        }

        void Reset() noexcept
        {
            _entries = {};
            _count = 0;
            _generation = 0;
            _latestLifeId = 0;
            _latestFrame = 0;
            _latestEligibility = false;
            _hasIdentity = false;
            _hasLatestFrame = false;
        }

    private:
        struct Entry final
        {
            std::uint32_t Frame = 0;
            std::uint16_t LifeId = 0;
            bool Eligible = false;
        };

        [[nodiscard]] const Entry* AtOrBefore(std::uint32_t frame) const noexcept
        {
            const Entry* found = nullptr;
            for (std::size_t i = 0; i < _count; ++i)
            {
                const Entry& entry = _entries[i];
                if (entry.Frame == frame || NetLifecycleTracker::Newer(frame, entry.Frame))
                {
                    found = &entry;
                }
                else
                {
                    break;
                }
            }
            return found;
        }

        void Append(Entry entry) noexcept
        {
            if (_count == Capacity)
            {
                for (std::size_t i = 1; i < _count; ++i)
                {
                    _entries[i - 1] = _entries[i];
                }
                --_count;
            }
            _entries[_count++] = entry;
        }

        std::array<Entry, Capacity> _entries{};
        std::size_t _count = 0;
        std::uint16_t _generation = 0;
        std::uint16_t _latestLifeId = 0;
        std::uint32_t _latestFrame = 0;
        bool _latestEligibility = false;
        bool _hasIdentity = false;
        bool _hasLatestFrame = false;
    };
}
