#include "Mods/Network/Diagnostics/NetTimingDiagnostics.hpp"

#include "Mods/Network/Session/NetSession.hpp"

#include "Entities/Players/PlayerEntity.hpp"
#include "NativeRuntime/System/Managed.hpp"
#include "NativeRuntime/System/Number.hpp"
#include "NativeRuntime/System/Stopwatch.hpp"

#include <algorithm>

namespace MphRead::Mods::Network
{
    namespace Runtime = ::MphRead::NativeRuntime;
    static_assert(::MphRead::Entities::PlayerEntity::SlotCapacity == 8);

    std::int64_t NetTimingDiagnostics::SnapshotPositionFrames = 0;
    std::int64_t NetTimingDiagnostics::IntentFallbackEntries = 0;
    std::int64_t NetTimingDiagnostics::IntentFallbackFrames = 0;
    std::int64_t NetTimingDiagnostics::LongestIntentFallback = 0;
    std::int64_t NetTimingDiagnostics::CorrectionsAfterLocalStall = 0;
    std::int64_t NetTimingDiagnostics::CorrectionsWithoutLocalStall = 0;
    double NetTimingDiagnostics::SnapshotIntervalMs = 0;
    double NetTimingDiagnostics::SimulationIntervalMs = 0;
    double NetTimingDiagnostics::WorstSnapshotIntervalMs = 0;
    double NetTimingDiagnostics::WorstSimulationIntervalMs = 0;
    std::int64_t NetTimingDiagnostics::_snapshotAt = 0;
    std::int64_t NetTimingDiagnostics::_simulationAt = 0;
    std::int64_t NetTimingDiagnostics::_stallAt = 0;
    std::array<std::int64_t, 8> NetTimingDiagnostics::_fallback{};
    std::array<std::uint32_t, 8> NetTimingDiagnostics::_positionFrame{};
    std::array<bool, 8> NetTimingDiagnostics::_positionSeen{};

    void NetTimingDiagnostics::Simulation()
    {
        const std::int64_t now = Runtime::StopwatchGetTimestamp();
        if (_simulationAt != 0)
        {
            SimulationIntervalMs = Runtime::TimeSpanTotalMilliseconds(
                Runtime::StopwatchGetElapsedTicks(_simulationAt, now));
            WorstSimulationIntervalMs = std::max(WorstSimulationIntervalMs, SimulationIntervalMs);
            if (SimulationIntervalMs > 50)
            {
                _stallAt = now;
            }
        }
        _simulationAt = now;
    }

    void NetTimingDiagnostics::Snapshot(std::int64_t now)
    {
        if (_snapshotAt != 0 && now >= _snapshotAt)
        {
            SnapshotIntervalMs = Runtime::TimeSpanTotalMilliseconds(
                Runtime::StopwatchGetElapsedTicks(_snapshotAt, now));
            WorstSnapshotIntervalMs = std::max(WorstSnapshotIntervalMs, SnapshotIntervalMs);
        }
        _snapshotAt = now;
    }

    void NetTimingDiagnostics::Correction()
    {
        // This is attribution by observed timing, not proof of a network fault.
        if (_stallAt != 0
            && Runtime::TimeSpanTotalMilliseconds(Runtime::StopwatchGetElapsedTicks(_stallAt)) < 250)
        {
            Runtime::IncrementInPlace(CorrectionsAfterLocalStall);
        }
        else
        {
            Runtime::IncrementInPlace(CorrectionsWithoutLocalStall);
        }
    }

    void NetTimingDiagnostics::Position(std::int32_t slot, bool snapshot)
    {
        if (slot < 0 || slot >= static_cast<std::int32_t>(_fallback.size()))
        {
            return;
        }
        const auto i = static_cast<std::size_t>(slot);
        if (_positionSeen[i] && _positionFrame[i] == NetSession::NetFrame())
        {
            return;
        }
        _positionSeen[i] = true;
        _positionFrame[i] = NetSession::NetFrame();
        if (snapshot)
        {
            Runtime::IncrementInPlace(SnapshotPositionFrames);
            _fallback[i] = 0;
        }
        else
        {
            const std::int64_t previousFallback = _fallback[i];
            Runtime::IncrementInPlace(_fallback[i]);
            if (previousFallback == 0)
            {
                Runtime::IncrementInPlace(IntentFallbackEntries);
            }
            Runtime::IncrementInPlace(IntentFallbackFrames);
            LongestIntentFallback = std::max(LongestIntentFallback, _fallback[i]);
        }
    }

    void NetTimingDiagnostics::ForgetSlot(std::int32_t slot)
    {
        if (slot >= 0 && slot < static_cast<std::int32_t>(_fallback.size()))
        {
            _fallback[static_cast<std::size_t>(slot)] = 0;
            _positionSeen[static_cast<std::size_t>(slot)] = false;
        }
    }

    void NetTimingDiagnostics::Reset()
    {
        SnapshotPositionFrames = IntentFallbackEntries = IntentFallbackFrames = LongestIntentFallback = 0;
        CorrectionsAfterLocalStall = CorrectionsWithoutLocalStall = 0;
        SnapshotIntervalMs = SimulationIntervalMs = WorstSnapshotIntervalMs = WorstSimulationIntervalMs = 0;
        _snapshotAt = _simulationAt = _stallAt = 0;
        _fallback.fill(0);
        _positionSeen.fill(false);
    }

    std::string NetTimingDiagnostics::Describe()
    {
        return "positions: snapshots=" + Runtime::ToString(SnapshotPositionFrames)
            + " fallback entries=" + Runtime::ToString(IntentFallbackEntries)
            + " frames=" + Runtime::ToString(IntentFallbackFrames)
            + " longest=" + Runtime::ToString(LongestIntentFallback) + "; "
            + "timing ms: snapshot=" + Runtime::ToString(SnapshotIntervalMs, "F1")
            + " worst=" + Runtime::ToString(WorstSnapshotIntervalMs, "F1")
            + " simulation=" + Runtime::ToString(SimulationIntervalMs, "F1")
            + " worst=" + Runtime::ToString(WorstSimulationIntervalMs, "F1") + "; "
            + "playout snaps: after local stall=" + Runtime::ToString(CorrectionsAfterLocalStall)
            + " without local stall=" + Runtime::ToString(CorrectionsWithoutLocalStall);
    }
}
