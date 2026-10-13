#include "Mods/Network/Diagnostics/NetDamageMetrics.hpp"

#include "NativeRuntime/System/Managed.hpp"
#include "NativeRuntime/System/Number.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>

using ::MphRead::NativeRuntime::IncrementInPlace;
using ::MphRead::NativeRuntime::MathMax;
using ::MphRead::NativeRuntime::UncheckedAdd;

namespace MphRead::Mods::Network
{
    NetDamageMetricsSnapshot NetDamageMetrics::_snapshot{};

    const NetDamageMetricsSnapshot& NetDamageMetrics::Read() noexcept
    {
        return _snapshot;
    }

    bool NetDamageMetrics::ValidSlot(std::int32_t slot) noexcept
    {
        return slot >= 0 && slot < Slots;
    }

    void NetDamageMetrics::ClearRoomCounters() noexcept
    {
        _snapshot.Resolved.fill(0);
        _snapshot.Replayed.fill(0);
        _snapshot.Fired.fill(0);
        _snapshot.PlayerChecks.fill(0);
        _snapshot.PlayerOverlaps.fill(0);
        _snapshot.PlayerAccepted.fill(0);
        for (auto& row : _snapshot.PlayerOverlapsByShooter)
        {
            row.fill(0);
        }
        _snapshot.AimDrift.fill(0.0);
        _snapshot.WorstDrift.fill(0.0);
        _snapshot.ShockCoilSpawned = 0;
        _snapshot.ShockCoilAcquired = 0;
        _snapshot.BombPlayerChecks = 0;
        _snapshot.BombTeamSkips = 0;
        _snapshot.BombHits = 0;
        _snapshot.DamageByBeam.fill(0);
        _snapshot.HitsByBeam.fill(0);
        _snapshot.BombDamageDealt = 0;
        _snapshot.BombDamageHits = 0;
        _snapshot.BombSpawnCalls = 0;
        _snapshot.BombSpawnMade = 0;
        _snapshot.BombSpawnDetonated = 0;
        _snapshot.BombSpawnStaleCount = 0;
        _snapshot.BombSpawnPoolEmpty = 0;
        _snapshot.BombNearest = std::numeric_limits<float>::max();
        _snapshot.BombRadiusSeen = 0.0F;
    }

    void NetDamageMetrics::ResetForRoomChange() noexcept
    {
        ClearRoomCounters();
    }

    void NetDamageMetrics::ForgetSlot(std::int32_t slot) noexcept
    {
        if (!ValidSlot(slot))
        {
            return;
        }
        const auto index = static_cast<std::size_t>(slot);
        _snapshot.Resolved[index] = 0;
        _snapshot.Replayed[index] = 0;
    }

    void NetDamageMetrics::ResetReplayCount(std::int32_t slot) noexcept
    {
        if (ValidSlot(slot))
        {
            _snapshot.Replayed[static_cast<std::size_t>(slot)] = 0;
        }
    }

    void NetDamageMetrics::RecordFired(std::int32_t slot) noexcept
    {
        if (ValidSlot(slot))
        {
            IncrementInPlace(_snapshot.Fired[static_cast<std::size_t>(slot)]);
        }
    }

    void NetDamageMetrics::RecordFiredMoving() noexcept
    {
        IncrementInPlace(_snapshot.FiredMoving);
    }

    void NetDamageMetrics::RecordFiredStill() noexcept
    {
        IncrementInPlace(_snapshot.FiredStill);
    }

    void NetDamageMetrics::RecordAimDrift(std::int32_t slot, double degrees) noexcept
    {
        if (!ValidSlot(slot))
        {
            return;
        }
        const auto index = static_cast<std::size_t>(slot);
        _snapshot.AimDrift[index] += degrees;
        _snapshot.WorstDrift[index] = MathMax(_snapshot.WorstDrift[index], degrees);
    }

    void NetDamageMetrics::RecordPlayerCheck(std::int32_t slot) noexcept
    {
        if (ValidSlot(slot))
        {
            IncrementInPlace(_snapshot.PlayerChecks[static_cast<std::size_t>(slot)]);
        }
    }

    void NetDamageMetrics::RecordPlayerOverlap(std::int32_t slot) noexcept
    {
        if (ValidSlot(slot))
        {
            IncrementInPlace(_snapshot.PlayerOverlaps[static_cast<std::size_t>(slot)]);
        }
    }

    void NetDamageMetrics::RecordPlayerAccepted(std::int32_t slot) noexcept
    {
        if (ValidSlot(slot))
        {
            IncrementInPlace(_snapshot.PlayerAccepted[static_cast<std::size_t>(slot)]);
        }
    }

    void NetDamageMetrics::RecordProjectileOverlap(std::int32_t shooterSlot, std::int32_t targetSlot,
        std::int32_t localSlot, std::uint32_t frame) noexcept
    {
        if (!ValidSlot(shooterSlot) || !ValidSlot(targetSlot))
        {
            return;
        }
        const auto shooter = static_cast<std::size_t>(shooterSlot);
        const auto target = static_cast<std::size_t>(targetSlot);
        if (targetSlot == localSlot && shooterSlot != targetSlot)
        {
            _snapshot.LastOverlapOnLocal[shooter] = std::max(1U, frame);
            auto& frames = _snapshot.UnseenFrame[shooter];
            for (std::size_t i = 0; i < NetDamageMetricsSnapshot::UnseenDepth; ++i)
            {
                if (frames[i] != 0 && frame - frames[i] <= NetDamageMetricsSnapshot::VisibleWindow)
                {
                    frames[i] = 0;
                    IncrementInPlace(_snapshot.HitsTakenSeen);
                    const auto beam = _snapshot.UnseenBeam[shooter][i];
                    IncrementInPlace(_snapshot.HitsTakenSeenByBeam[beam]);
                    break;
                }
            }
        }
        IncrementInPlace(_snapshot.PlayerOverlapsByShooter[shooter][target]);
    }

    void NetDamageMetrics::RecordShockCoil(bool acquired) noexcept
    {
        IncrementInPlace(_snapshot.ShockCoilSpawned);
        if (acquired)
        {
            IncrementInPlace(_snapshot.ShockCoilAcquired);
        }
    }

    void NetDamageMetrics::RecordBombTeamSkip() noexcept
    {
        IncrementInPlace(_snapshot.BombTeamSkips);
    }

    void NetDamageMetrics::RecordBombPlayerCheck() noexcept
    {
        IncrementInPlace(_snapshot.BombPlayerChecks);
    }

    void NetDamageMetrics::RecordBombCandidate(float distance, float radius) noexcept
    {
        if (distance < _snapshot.BombNearest)
        {
            _snapshot.BombNearest = distance;
        }
        if (radius > _snapshot.BombRadiusSeen)
        {
            _snapshot.BombRadiusSeen = radius;
        }
    }

    void NetDamageMetrics::RecordBombHit() noexcept
    {
        IncrementInPlace(_snapshot.BombHits);
    }

    void NetDamageMetrics::RecordBombSpawnCall() noexcept
    {
        IncrementInPlace(_snapshot.BombSpawnCalls);
    }

    void NetDamageMetrics::RecordBombSpawnDetonated() noexcept
    {
        IncrementInPlace(_snapshot.BombSpawnDetonated);
    }

    void NetDamageMetrics::RecordBombSpawnStale() noexcept
    {
        IncrementInPlace(_snapshot.BombSpawnStaleCount);
    }

    void NetDamageMetrics::RecordBombSpawnPoolEmpty() noexcept
    {
        IncrementInPlace(_snapshot.BombSpawnPoolEmpty);
    }

    void NetDamageMetrics::RecordBombSpawnMade() noexcept
    {
        IncrementInPlace(_snapshot.BombSpawnMade);
    }

    void NetDamageMetrics::RecordBombDamage(std::int32_t amount) noexcept
    {
        _snapshot.BombDamageDealt = UncheckedAdd(_snapshot.BombDamageDealt, amount);
        IncrementInPlace(_snapshot.BombDamageHits);
    }

    void NetDamageMetrics::RecordBeamDamage(std::int32_t beam, std::int32_t amount) noexcept
    {
        if (beam < 0 || beam >= static_cast<std::int32_t>(_snapshot.DamageByBeam.size()))
        {
            return;
        }
        const auto index = static_cast<std::size_t>(beam);
        _snapshot.DamageByBeam[index] = UncheckedAdd(_snapshot.DamageByBeam[index], amount);
        IncrementInPlace(_snapshot.HitsByBeam[index]);
    }

    void NetDamageMetrics::RecordResolved(std::int32_t slot) noexcept
    {
        if (ValidSlot(slot))
        {
            IncrementInPlace(_snapshot.Resolved[static_cast<std::size_t>(slot)]);
        }
    }

    bool NetDamageMetrics::RecordDamageTaken(std::int32_t attackerSlot, std::uint8_t beam,
        std::uint32_t frame) noexcept
    {
        if (!ValidSlot(attackerSlot))
        {
            return false;
        }
        IncrementInPlace(_snapshot.HitsTaken);
        const auto beamIndex = static_cast<std::size_t>(beam == 0xFFU || beam > 9 ? 10 : beam);
        IncrementInPlace(_snapshot.HitsTakenByBeam[beamIndex]);
        const std::uint32_t seen = _snapshot.LastOverlapOnLocal[static_cast<std::size_t>(attackerSlot)];
        if (seen != 0 && frame - seen <= NetDamageMetricsSnapshot::VisibleWindow)
        {
            IncrementInPlace(_snapshot.HitsTakenSeen);
            IncrementInPlace(_snapshot.HitsTakenSeenByBeam[beamIndex]);
            return true;
        }
        return false;
    }

    void NetDamageMetrics::RecordUnseenDamage(std::int32_t attackerSlot, std::uint8_t beam,
        std::uint32_t frame) noexcept
    {
        if (!ValidSlot(attackerSlot))
        {
            return;
        }
        auto& frames = _snapshot.UnseenFrame[static_cast<std::size_t>(attackerSlot)];
        std::size_t oldest = 0;
        for (std::size_t i = 1; i < NetDamageMetricsSnapshot::UnseenDepth; ++i)
        {
            if (frames[i] < frames[oldest])
            {
                oldest = i;
            }
        }
        frames[oldest] = std::max(1U, frame);
        _snapshot.UnseenBeam[static_cast<std::size_t>(attackerSlot)][oldest] = beam;
    }

    void NetDamageMetrics::RecordReplayed(std::int32_t slot) noexcept
    {
        if (ValidSlot(slot))
        {
            IncrementInPlace(_snapshot.Replayed[static_cast<std::size_t>(slot)]);
        }
    }
}
