#include "Mods/Network/Gameplay/Replication/NetSmoothing.hpp"

#include "Mods/Network/Session/NetLifecycleTracker.hpp"
#include "Mods/Network/Diagnostics/NetLog.hpp"
#include "Mods/Network/Gameplay/Player/NetPlayerLifecycle.hpp"
#include "Mods/Network/Protocol/NetProtocol.hpp"
#include "Mods/Network/Session/NetSession.hpp"
#include "Mods/Network/Diagnostics/NetTimingDiagnostics.hpp"

#include "NativeRuntime/System/Managed.hpp"
#include "NativeRuntime/System/Number.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace MphRead::Mods::Network
{
    namespace Runtime = ::MphRead::NativeRuntime;
    using OpenTK::Mathematics::Vector3;

    bool NetSmoothing::_enabled = true;
    std::int32_t NetSmoothing::_delay = NetSmoothing::MinDelayFrames;
    std::int32_t NetSmoothing::_sinceGrew = 0;
    double NetSmoothing::_readFrame = 0;
    bool NetSmoothing::_running = false;
    std::int32_t NetSmoothing::_sinceStarved = 0;
    std::int64_t NetSmoothing::_starved = 0;
    std::int64_t NetSmoothing::_interpolated = 0;
    std::int64_t NetSmoothing::_held = 0;
    std::int64_t NetSmoothing::_snaps = 0;
    std::int64_t NetSmoothing::_steps = 0;
    double NetSmoothing::_stepSum = 0;
    float NetSmoothing::_worstStep = 0;
    std::int64_t NetSmoothing::_stalledFrames = 0;
    std::int32_t NetSmoothing::_worstStall = 0;
    std::array<std::int32_t, NetSmoothing::Slots> NetSmoothing::_stallRun{};
    NetSmoothing::Grid<std::uint16_t> NetSmoothing::_life{};
    NetSmoothing::Grid<std::uint16_t> NetSmoothing::_generation{};
    NetSmoothing::Grid<Vector3> NetSmoothing::_position{};
    NetSmoothing::Grid<bool> NetSmoothing::_altForm{};
    NetSmoothing::Grid<std::uint8_t> NetSmoothing::_formDirection{};
    NetSmoothing::Grid<std::uint8_t> NetSmoothing::_formProgress{};
    NetSmoothing::Grid<bool> NetSmoothing::_live{};
    NetSmoothing::Grid<std::uint32_t> NetSmoothing::_trackedStamp{};
    NetSmoothing::Grid<NetSmoothing::TrackedFrame> NetSmoothing::_trackedPositions{};
    std::array<std::uint32_t, NetSmoothing::HistoryFrames> NetSmoothing::_stamp{};
    std::uint32_t NetSmoothing::_newest = 0;
    std::array<Vector3, NetSmoothing::Slots> NetSmoothing::_lastSampled{};
    std::array<bool, NetSmoothing::Slots> NetSmoothing::_sampledSeen{};

    namespace
    {
        [[nodiscard]] float InterpolationAmount(std::uint32_t beforeFrame, std::uint32_t afterFrame,
            std::uint32_t lowerFrame, double fraction) noexcept
        {
            const std::uint32_t span = afterFrame - beforeFrame;
            if (span == 0)
            {
                return 0.0F;
            }
            const double elapsed = static_cast<double>(lowerFrame - beforeFrame) + fraction;
            return static_cast<float>(std::clamp(elapsed / span, 0.0, 1.0));
        }

        template <typename T, std::size_t Rows, std::size_t Columns>
        void ClearGrid(std::array<std::array<T, Columns>, Rows>& grid)
        {
            for (auto& row : grid)
            {
                row.fill(T{});
            }
        }
    }

    void NetSmoothing::ResetSlot(std::int32_t slot)
    {
        if (slot < 0 || slot >= Slots)
        {
            return;
        }
        const auto s = static_cast<std::size_t>(slot);
        for (std::size_t i = 0; i < HistoryFrames; i++)
        {
            _live[s][i] = false;
            _life[s][i] = 0;
            _generation[s][i] = 0;
        }
        _trackedStamp[s].fill(0);
        _trackedPositions[s].fill(TrackedFrame{});
        _sampledSeen[s] = false;
        _stallRun[s] = 0;
    }

    bool NetSmoothing::Active()
    {
        return PresentationOwnsRemoteReplicas() && _running;
    }

    bool NetSmoothing::PresentationOwnsRemoteReplicas() noexcept
    {
        return _enabled && NetSession::Active()
            && !NetSession::IsAuthority() && !NetSession::IsHost();
    }

    void NetSmoothing::Record(std::uint32_t frame, std::span<const PlayerState> states)
    {
        if (!_enabled || frame == 0)
        {
            return;
        }
        // Only an explicit stream change resets this clock.
        if (_running && !NetLifecycleTracker::Newer(frame, _newest))
        {
            return;
        }
        const auto index = static_cast<std::size_t>(frame % HistoryFrames);
        _stamp[index] = frame;
        for (std::size_t i = 0; i < Slots; i++)
        {
            _live[i][index] = false;
        }
        for (const PlayerState& state : states)
        {
            const std::int32_t slot = state.SlotIndex;
            if (slot < 0 || slot >= Slots)
            {
                continue;
            }
            const auto s = static_cast<std::size_t>(slot);
            const bool inPlay = (state.Flags & PlayerState::FlagActive) != 0
                && (state.Flags & PlayerState::FlagSpawned) != 0
                && state.Health > 0;
            _life[s][index] = state.LifeId;
            _generation[s][index] = state.SlotGeneration;
            _live[s][index] = inPlay;
            _position[s][index] = state.Position;
            _altForm[s][index] = (state.Flags & PlayerState::FlagAltForm) != 0;
            ReplicatedFormState form{};
            if (!ReplicatedFormState::TryCreate(_altForm[s][index],
                (state.WeavelFlags & PlayerState::WeavelFlagMorphing) != 0,
                (state.WeavelFlags & PlayerState::WeavelFlagUnmorphing) != 0,
                state.FormTransitionProgress(), form))
            {
                form.AltForm = _altForm[s][index];
            }
            _formDirection[s][index] = static_cast<std::uint8_t>(form.Direction);
            _formProgress[s][index] = form.Progress;
        }
        if (!_running || frame > _newest)
        {
            _newest = frame;
        }
        if (!_running)
        {
            _running = true;
            _readFrame = frame > static_cast<std::uint32_t>(_delay)
                ? static_cast<double>(frame) - _delay : static_cast<double>(frame);
        }
    }

    void NetSmoothing::RecordTrackedPositions(std::uint32_t frame, std::int32_t slot,
        std::span<const TrackedPosition> positions)
    {
        static_assert(TrackedObjectsPerSlot == IntentPacket::BombCount);
        if (!_enabled || frame == 0 || slot < 0 || slot >= Slots)
        {
            return;
        }
        const std::size_t s = static_cast<std::size_t>(slot);
        const std::size_t index = static_cast<std::size_t>(frame % HistoryFrames);
        if (_trackedStamp[s][index] != frame)
        {
            _trackedPositions[s][index] = TrackedFrame{};
        }
        _trackedStamp[s][index] = frame;
        TrackedFrame& samples = _trackedPositions[s][index];
        samples = TrackedFrame{};
        std::size_t count = 0;
        for (const TrackedPosition& sample : positions)
        {
            if (count == samples.size())
            {
                break;
            }
            if (sample.Sequence == 0 || !std::isfinite(sample.Position.X)
                || !std::isfinite(sample.Position.Y) || !std::isfinite(sample.Position.Z))
            {
                continue;
            }
            const bool duplicate = std::any_of(samples.begin(), samples.begin()
                + static_cast<std::ptrdiff_t>(count), [&sample](const TrackedPosition& existing)
                {
                    return existing.Sequence == sample.Sequence;
                });
            if (!duplicate)
            {
                samples[count++] = sample;
            }
        }
    }

    void NetSmoothing::Restart(std::uint32_t frame)
    {
        _stamp.fill(0);
        ClearGrid(_live);
        ClearGrid(_trackedStamp);
        ClearGrid(_trackedPositions);
        _sampledSeen.fill(false);
        _stallRun.fill(0);
        _newest = 0;
        _running = false;
        _sinceStarved = 0;
        _sinceGrew = GrowCooldownFrames;
        NetLog::Event("playout clock re-based on frame " + std::to_string(frame));
    }

    void NetSmoothing::Tick()
    {
        if (!_enabled || !NetSession::Active() || !_running)
        {
            return;
        }
        _readFrame += 1.0;
        const double target = static_cast<double>(_newest) - _delay;
        const double error = target - _readFrame;
        if (std::abs(error) > SnapError)
        {
            // A stall, a rejoin, a rotation, a counter that restarted. Walking
            // across ten frames of error would be four seconds of everybody
            // moving at the wrong speed, which is a worse thing to look at
            // than one jump.
            _readFrame = target;
            Runtime::IncrementInPlace(_snaps);
            NetTimingDiagnostics::Correction();
        }
        else
        {
            _readFrame += error * Correction;
        }
        if (_readFrame > _newest)
        {
            // Past everything that has arrived. The read point is pinned rather
            // than allowed to run on, because the alternative is extrapolating
            // -- and a guessed position puts a player through a wall and then
            // snaps them out of it.
            _readFrame = _newest;
            Runtime::IncrementInPlace(_starved);
            _sinceStarved = 0;
            if (_delay < MaxDelayFrames && _sinceGrew >= GrowCooldownFrames)
            {
                _delay++;
                _sinceGrew = 0;
            }
        }
        else
        {
            Runtime::IncrementInPlace(_sinceStarved);
            if (_sinceStarved > ShrinkAfterFrames && _delay > MinDelayFrames)
            {
                _delay--;
                _sinceStarved = 0;
            }
        }
        Runtime::IncrementInPlace(_sinceGrew);
    }

    bool NetSmoothing::FindSnapshotBracket(std::uint32_t frame, SnapshotBracket& bracket) noexcept
    {
        bracket = {};
        std::uint32_t beforeDistance = 0;
        std::uint32_t afterDistance = 0;
        for (const std::uint32_t stamp : _stamp)
        {
            if (stamp == 0)
            {
                continue;
            }
            if (stamp == frame || NetLifecycleTracker::Newer(frame, stamp))
            {
                const std::uint32_t distance = frame - stamp;
                if (!bracket.HasBefore || distance < beforeDistance)
                {
                    bracket.BeforeFrame = stamp;
                    bracket.HasBefore = true;
                    beforeDistance = distance;
                }
            }
            else
            {
                const std::uint32_t distance = stamp - frame;
                if (!bracket.HasAfter || distance < afterDistance)
                {
                    bracket.AfterFrame = stamp;
                    bracket.HasAfter = true;
                    afterDistance = distance;
                }
            }
        }
        return bracket.HasBefore;
    }

    bool NetSmoothing::Sample(std::int32_t slot, Vector3& position, bool& altForm)
    {
        position = Vector3::Zero;
        altForm = false;
        if (!Active() || slot < 0 || slot >= Slots)
        {
            return false;
        }
        const auto lower = static_cast<std::uint32_t>(std::floor(_readFrame));
        const double fraction = _readFrame - lower;
        SnapshotBracket bracket{};
        if (!FindSnapshotBracket(lower, bracket))
        {
            return false;
        }
        Vector3 a{};
        bool altA = false;
        if (!Lookup(slot, bracket.BeforeFrame, a, altA))
        {
            return false;
        }
        altForm = altA;
        position = a;
        Vector3 b{};
        bool altB = false;
        if (!bracket.HasAfter || !Lookup(slot, bracket.AfterFrame, b, altB))
        {
            // With no newer live sample, keep the last state on this side of
            // the playout clock. A dead or replaced snapshot is a lifecycle
            // boundary and must not be interpolated through.
            Runtime::IncrementInPlace(_held);
            NoteStep(slot, position);
            return true;
        }
        // Packet loss or reordering leaves holes in the authority-frame ring.
        // Bridge the nearest received snapshots instead of falling back to the
        // latest state, which jumps ahead of the shared playout clock.
        const std::uint32_t span = bracket.AfterFrame - bracket.BeforeFrame;
        const float amount = InterpolationAmount(bracket.BeforeFrame, bracket.AfterFrame, lower, fraction);
        if (amount <= 0.0001F || span == 0 || altA != altB)
        {
            // A form change switches the position origin. Hold the near pose
            // until the same presentation clock reaches the new form sample.
            Runtime::IncrementInPlace(_held);
            NoteStep(slot, position);
            return true;
        }
        const Vector3 travel = b - a;
        if (travel.LengthSquared() > SnapDistance * SnapDistance)
        {
            // A teleporter, a respawn, a jump pad's launch frame -- not
            // something to slide across. The near side, not the far one:
            // NetUnlagged.Reconcile makes the same refusal at the other end and
            // falls back to the near side, so both worlds stay the same world.
            position = a;
            Runtime::IncrementInPlace(_held);
            NoteStep(slot, position);
            return true;
        }
        position = a + OpenTK::Mathematics::Multiply(travel, amount);
        Runtime::IncrementInPlace(_interpolated);
        NoteStep(slot, position);
        return true;
    }

    bool NetSmoothing::SampleForm(std::int32_t slot, ReplicatedFormState& form)
    {
        form = ReplicatedFormState{};
        if (!Active() || slot < 0 || slot >= Slots)
        {
            return false;
        }
        const auto lower = static_cast<std::uint32_t>(std::floor(_readFrame));
        const double fraction = _readFrame - lower;
        ReplicatedFormState a{};
        SnapshotBracket bracket{};
        if (!FindSnapshotBracket(lower, bracket) || !LookupForm(slot, bracket.BeforeFrame, a))
        {
            return false;
        }
        form = a;
        if (!bracket.HasAfter)
        {
            return true;
        }
        ReplicatedFormState b{};
        if (!LookupForm(slot, bracket.AfterFrame, b))
        {
            // A non-live snapshot is a lifecycle boundary, not a form sample
            // to blend through.
            return true;
        }
        const std::uint32_t span = bracket.AfterFrame - bracket.BeforeFrame;
        const float amount = InterpolationAmount(bracket.BeforeFrame, bracket.AfterFrame, lower, fraction);
        if (amount <= 0.0001F
            || a.Direction != b.Direction || a.AltForm != b.AltForm
            || a.Direction == ReplicatedFormState::Transition::None)
        {
            return true;
        }
        const float progress = static_cast<float>(a.Progress)
            + (static_cast<float>(b.Progress) - a.Progress) * amount;
        form.Progress = static_cast<std::uint8_t>(std::clamp(
            static_cast<std::int32_t>(std::lround(progress)), 0, 15));
        return true;
    }

    bool NetSmoothing::SampleTrackedPosition(std::int32_t slot, std::uint32_t sequence,
        Vector3& position, bool& present)
    {
        position = Vector3::Zero;
        present = false;
        if (!Active() || slot < 0 || slot >= Slots || sequence == 0)
        {
            return false;
        }

        const std::uint32_t lower = static_cast<std::uint32_t>(std::floor(_readFrame));
        const double fraction = _readFrame - lower;
        std::uint32_t beforeFrame = 0;
        std::uint32_t afterFrame = 0;
        const std::size_t s = static_cast<std::size_t>(slot);
        for (std::uint32_t stamp : _trackedStamp[s])
        {
            if (stamp == 0)
            {
                continue;
            }
            if (stamp == lower || NetLifecycleTracker::Newer(lower, stamp))
            {
                if (beforeFrame == 0 || NetLifecycleTracker::Newer(stamp, beforeFrame))
                {
                    beforeFrame = stamp;
                }
            }
            else if (NetLifecycleTracker::Newer(stamp, lower)
                && (afterFrame == 0 || NetLifecycleTracker::Newer(afterFrame, stamp)))
            {
                afterFrame = stamp;
            }
        }
        if (beforeFrame == 0)
        {
            return false;
        }

        const auto at = [s, sequence](std::uint32_t frame) -> const TrackedPosition*
        {
            const std::size_t index = static_cast<std::size_t>(frame % HistoryFrames);
            if (NetSmoothing::_trackedStamp[s][index] != frame)
            {
                return nullptr;
            }
            const TrackedFrame& samples = NetSmoothing::_trackedPositions[s][index];
            const auto found = std::find_if(samples.begin(), samples.end(), [sequence](const TrackedPosition& sample)
                {
                    return sample.Sequence == sequence;
                });
            return found == samples.end() ? nullptr : &*found;
        };

        const TrackedPosition* a = at(beforeFrame);
        if (a == nullptr)
        {
            return false;
        }
        position = a->Position;
        present = a->Present;
        if (!present)
        {
            return true;
        }
        if (afterFrame == 0)
        {
            return true;
        }

        const TrackedPosition* b = at(afterFrame);
        if (b == nullptr || !b->Present)
        {
            // Keep the last live point until the playout clock reaches the
            // frame on which the owner removed this object.
            return true;
        }
        const std::uint32_t span = afterFrame - beforeFrame;
        if (span == 0)
        {
            return true;
        }
        const float amount = InterpolationAmount(beforeFrame, afterFrame, lower, fraction);
        if (amount <= 0.0001F)
        {
            return true;
        }
        const Vector3 travel = b->Position - a->Position;
        if (travel.LengthSquared() > SnapDistance * SnapDistance)
        {
            return true;
        }
        position = a->Position + OpenTK::Mathematics::Multiply(travel, amount);
        return true;
    }

    bool NetSmoothing::Lookup(std::int32_t slot, std::uint32_t frame, Vector3& position, bool& altForm)
    {
        position = Vector3::Zero;
        altForm = false;
        if (frame == 0 || frame > _newest)
        {
            return false;
        }
        const auto index = static_cast<std::size_t>(frame % HistoryFrames);
        const auto s = static_cast<std::size_t>(slot);
        if (_stamp[index] != frame || !_live[s][index]
            || !NetPlayerLifecycle::Matches(slot, _generation[s][index], _life[s][index]))
        {
            return false;
        }
        position = _position[s][index];
        altForm = _altForm[s][index];
        return std::isfinite(position.X) && std::isfinite(position.Y) && std::isfinite(position.Z);
    }

    bool NetSmoothing::LookupForm(std::int32_t slot, std::uint32_t frame, ReplicatedFormState& form)
    {
        form = ReplicatedFormState{};
        if (frame == 0 || frame > _newest)
        {
            return false;
        }
        const std::size_t index = static_cast<std::size_t>(frame % HistoryFrames);
        const std::size_t s = static_cast<std::size_t>(slot);
        if (_stamp[index] != frame || !_live[s][index]
            || !NetPlayerLifecycle::Matches(slot, _generation[s][index], _life[s][index]))
        {
            return false;
        }
        const auto direction = static_cast<ReplicatedFormState::Transition>(_formDirection[s][index]);
        form.AltForm = _altForm[s][index];
        form.Direction = direction;
        form.Progress = _formProgress[s][index];
        return form.IsValid();
    }

    void NetSmoothing::NoteStep(std::int32_t slot, Vector3 position)
    {
        const auto s = static_cast<std::size_t>(slot);
        if (_sampledSeen[s])
        {
            const float step = OpenTK::Mathematics::Length(position - _lastSampled[s]);
            if (std::isfinite(step))
            {
                Runtime::IncrementInPlace(_steps);
                _stepSum += step;
                if (step > _worstStep)
                {
                    _worstStep = step;
                }
                if (step < 0.0005F)
                {
                    Runtime::IncrementInPlace(_stalledFrames);
                    Runtime::IncrementInPlace(_stallRun[s]);
                    if (_stallRun[s] > _worstStall)
                    {
                        _worstStall = _stallRun[s];
                    }
                }
                else
                {
                    _stallRun[s] = 0;
                }
            }
        }
        _lastSampled[s] = position;
        _sampledSeen[s] = true;
    }

    bool NetSmoothing::AckPoint(std::uint32_t& frame, std::uint8_t& subFrame)
    {
        frame = 0;
        subFrame = 0;
        if (!Active())
        {
            return false;
        }
        const auto lower = static_cast<std::uint32_t>(std::floor(_readFrame));
        if (lower == 0)
        {
            return false;
        }
        frame = lower;
        subFrame = static_cast<std::uint8_t>(Runtime::MathClamp(
            Runtime::ConvertToInt32Net9((_readFrame - lower) * 256.0), 0, 255));
        return true;
    }

    void NetSmoothing::Reset()
    {
        _stamp.fill(0);
        ClearGrid(_live);
        ClearGrid(_trackedStamp);
        ClearGrid(_trackedPositions);
        _sampledSeen.fill(false);
        _stallRun.fill(0);
        _newest = 0;
        _readFrame = 0;
        _running = false;
        _sinceStarved = 0;
        _sinceGrew = GrowCooldownFrames;
        _delay = MinDelayFrames;
        _starved = 0;
        _interpolated = 0;
        _held = 0;
        _snaps = 0;
        _steps = 0;
        _stepSum = 0;
        _worstStep = 0;
        _stalledFrames = 0;
        _worstStall = 0;
    }

    void NetSmoothing::NoteRoomChanged()
    {
        _stamp.fill(0);
        ClearGrid(_live);
        ClearGrid(_trackedStamp);
        ClearGrid(_trackedPositions);
        _sampledSeen.fill(false);
        _stallRun.fill(0);
        _newest = 0;
        _readFrame = 0;
        _running = false;
        _sinceGrew = GrowCooldownFrames;
    }

    std::optional<std::string> NetSmoothing::Describe()
    {
        if (_steps == 0)
        {
            return std::nullopt;
        }
        const double mean = _stepSum / static_cast<double>(_steps);
        const double stalled = 100.0 * static_cast<double>(_stalledFrames) / static_cast<double>(_steps);
        return "puppet smoothing: " + (_enabled ? "on, " + std::to_string(_delay) + " frames of buffer"
                : std::string("off")) + ", "
            + std::to_string(_interpolated) + " interpolated / " + std::to_string(_held) + " held, "
            + std::to_string(_starved) + " starved, "
            + std::to_string(_snaps) + " clock snaps; steps mean " + Runtime::ToString(mean, "F4")
            + " units, worst " + Runtime::ToString(_worstStep, "F3") + ", "
            + Runtime::ToString(stalled, "F1") + "% of frames still (longest run "
            + std::to_string(_worstStall) + ")";
    }
}
