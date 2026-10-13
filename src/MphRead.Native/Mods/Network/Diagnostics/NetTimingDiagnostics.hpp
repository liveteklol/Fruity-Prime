#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace MphRead::Mods::Network
{
    class NetTimingDiagnostics final
    {
    public:
        NetTimingDiagnostics() = delete;

        static void Simulation();
        static void Snapshot(std::int64_t now);
        static void Correction();
        static void Position(std::int32_t slot, bool snapshot);
        static void ForgetSlot(std::int32_t slot);
        static void Reset();
        [[nodiscard]] static std::string Describe();

    private:
        static std::int64_t SnapshotPositionFrames;
        static std::int64_t IntentFallbackEntries;
        static std::int64_t IntentFallbackFrames;
        static std::int64_t LongestIntentFallback;
        static std::int64_t CorrectionsAfterLocalStall;
        static std::int64_t CorrectionsWithoutLocalStall;
        static double SnapshotIntervalMs;
        static double SimulationIntervalMs;
        static double WorstSnapshotIntervalMs;
        static double WorstSimulationIntervalMs;

        static std::int64_t _snapshotAt;
        static std::int64_t _simulationAt;
        static std::int64_t _stallAt;
        static std::array<std::int64_t, 8> _fallback;
        static std::array<std::uint32_t, 8> _positionFrame;
        static std::array<bool, 8> _positionSeen;
    };
}
