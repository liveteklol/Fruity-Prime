#pragma once

#include "Entities/Players/PlayerEntity.hpp"
#include "Formats/Enums.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace MphRead::Mods::Network
{
    struct NetDamageMetricsSnapshot final
    {
        static constexpr std::size_t SlotCount = Entities::PlayerEntity::SlotCapacity;
        static constexpr std::size_t UnseenDepth = 16;
        static constexpr std::uint32_t VisibleWindow = 45;

        std::array<std::int32_t, SlotCount> Resolved{};
        std::array<std::int32_t, SlotCount> Replayed{};
        std::array<std::int32_t, SlotCount> Fired{};
        std::int64_t FiredMoving = 0;
        std::int64_t FiredStill = 0;
        std::array<std::int32_t, SlotCount> PlayerChecks{};
        std::array<std::int32_t, SlotCount> PlayerOverlaps{};
        std::array<std::int32_t, SlotCount> PlayerAccepted{};
        std::array<std::array<std::int32_t, SlotCount>, SlotCount> PlayerOverlapsByShooter{};
        std::array<std::uint32_t, SlotCount> LastOverlapOnLocal{};
        std::int64_t HitsTaken = 0;
        std::int64_t HitsTakenSeen = 0;
        std::array<std::int64_t, 11> HitsTakenByBeam{};
        std::array<std::int64_t, 11> HitsTakenSeenByBeam{};
        std::array<std::array<std::uint32_t, UnseenDepth>, SlotCount> UnseenFrame{};
        std::array<std::array<std::uint8_t, UnseenDepth>, SlotCount> UnseenBeam{};
        std::array<double, SlotCount> AimDrift{};
        std::array<double, SlotCount> WorstDrift{};
        std::int32_t ShockCoilSpawned = 0;
        std::int32_t ShockCoilAcquired = 0;
        std::int32_t BombPlayerChecks = 0;
        std::int32_t BombTeamSkips = 0;
        std::int32_t BombHits = 0;
        std::array<std::int32_t, static_cast<std::size_t>(MphRead::BeamType::Enemy) + 1> DamageByBeam{};
        std::array<std::int32_t, static_cast<std::size_t>(MphRead::BeamType::Enemy) + 1> HitsByBeam{};
        std::int32_t BombDamageDealt = 0;
        std::int32_t BombDamageHits = 0;
        std::int32_t BombSpawnCalls = 0;
        std::int32_t BombSpawnMade = 0;
        std::int32_t BombSpawnDetonated = 0;
        std::int32_t BombSpawnStaleCount = 0;
        std::int32_t BombSpawnPoolEmpty = 0;
        float BombNearest = std::numeric_limits<float>::max();
        float BombRadiusSeen = 0.0F;
    };

    // Owns the mutable counters used by network combat diagnostics. Consumers
    // can inspect a const snapshot and record observations through named APIs.
    class NetDamageMetrics final
    {
    public:
        NetDamageMetrics() = delete;

        [[nodiscard]] static const NetDamageMetricsSnapshot& Read() noexcept;

        static void ResetForRoomChange() noexcept;
        static void ForgetSlot(std::int32_t slot) noexcept;
        static void ResetReplayCount(std::int32_t slot) noexcept;

        static void RecordFired(std::int32_t slot) noexcept;
        static void RecordFiredMoving() noexcept;
        static void RecordFiredStill() noexcept;
        static void RecordAimDrift(std::int32_t slot, double degrees) noexcept;
        static void RecordPlayerCheck(std::int32_t slot) noexcept;
        static void RecordPlayerOverlap(std::int32_t slot) noexcept;
        static void RecordPlayerAccepted(std::int32_t slot) noexcept;
        static void RecordProjectileOverlap(std::int32_t shooterSlot, std::int32_t targetSlot,
            std::int32_t localSlot, std::uint32_t frame) noexcept;
        static void RecordShockCoil(bool acquired) noexcept;
        static void RecordBombTeamSkip() noexcept;
        static void RecordBombPlayerCheck() noexcept;
        static void RecordBombCandidate(float distance, float radius) noexcept;
        static void RecordBombHit() noexcept;
        static void RecordBombSpawnCall() noexcept;
        static void RecordBombSpawnDetonated() noexcept;
        static void RecordBombSpawnStale() noexcept;
        static void RecordBombSpawnPoolEmpty() noexcept;
        static void RecordBombSpawnMade() noexcept;
        static void RecordBombDamage(std::int32_t amount) noexcept;
        static void RecordBeamDamage(std::int32_t beam, std::int32_t amount) noexcept;
        static void RecordResolved(std::int32_t slot) noexcept;
        [[nodiscard]] static bool RecordDamageTaken(std::int32_t attackerSlot, std::uint8_t beam,
            std::uint32_t frame) noexcept;
        static void RecordUnseenDamage(std::int32_t attackerSlot, std::uint8_t beam,
            std::uint32_t frame) noexcept;
        static void RecordReplayed(std::int32_t slot) noexcept;

    private:
        static constexpr std::int32_t Slots = static_cast<std::int32_t>(NetDamageMetricsSnapshot::SlotCount);

        [[nodiscard]] static bool ValidSlot(std::int32_t slot) noexcept;
        static void ClearRoomCounters() noexcept;

        static NetDamageMetricsSnapshot _snapshot;
    };
}
