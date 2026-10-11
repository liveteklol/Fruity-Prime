#include "NetDamage.hpp"
#include "HitLocation.hpp"

#include "../../GameState.hpp"
#include "../../Entities/BeamProjectileEntity.hpp"
#include "../../Entities/Players/HalfturretEntity.hpp"
#include "NetHitClaims.hpp"
#include "NetHitPrediction.hpp"
#include "NetHooks.hpp"
#include "NetLifecycleTracker.hpp"
#include "NetLog.hpp"
#include "NetPlayerBridge.hpp"
#include "NetPlayerLifecycle.hpp"
#include "NetSession.hpp"
#include "NetShotDiagnostics.hpp"
#include "NetTimingDiagnostics.hpp"
#include "../../NativeRuntime/System/Managed.hpp"
#include "../../NativeRuntime/System/Number.hpp"
#include "../../Formats/Types.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <string>

using ::MphRead::NativeRuntime::IncrementInPlace;
using ::MphRead::NativeRuntime::MathClamp;
using ::MphRead::NativeRuntime::MathMax;
using ::MphRead::NativeRuntime::UncheckedAdd;
using ::OpenTK::Mathematics::IsZero;
using ::OpenTK::Mathematics::Length;
using ::OpenTK::Mathematics::LengthSquared;
using ::OpenTK::Mathematics::Multiply;

namespace MphRead::Mods::Network
{
    namespace
    {
        namespace Runtime = ::MphRead::NativeRuntime;

        [[nodiscard]] std::string F2(float value)
        {
            return Runtime::ToString(value, "F2");
        }

        [[nodiscard]] std::string Coordinates(OpenTK::Mathematics::Vector3 value)
        {
            return "(" + F2(value.X) + "," + F2(value.Y) + "," + F2(value.Z) + ")";
        }
    }

    void NetDamage::NoteFired(Entities::PlayerEntity& shooter,
        OpenTK::Mathematics::Vector3 shotVec, OpenTK::Mathematics::Vector3 aimVec)
    {
        if (!NetSession::Active())
        {
            return;
        }
        const std::int32_t slot = shooter.SlotIndex();
        if (slot < 0 || slot >= Slots)
        {
            return;
        }
        const auto index = static_cast<std::size_t>(slot);
        IncrementInPlace(Fired[index]);
        if (slot == NetHooks::LocalSlot())
        {
            if (LengthSquared(shooter.Speed()) > 0.0004F)
            {
                IncrementInPlace(FiredMoving);
            }
            else
            {
                IncrementInPlace(FiredStill);
            }
        }
        if (NetLog::Enabled())
        {
            const OpenTK::Mathematics::Vector3 muzzle = shooter.ModMuzzlePos();
            const OpenTK::Mathematics::Vector3 position = shooter.Position;
            const float gap = Length(muzzle - position);
            if (gap > 3.0F)
            {
                NetLog::Event("[muzzle] slot " + std::to_string(slot) + " fired from "
                    + Coordinates(muzzle) + " while standing at " + Coordinates(position)
                    + " -- " + Runtime::ToString(gap, "F1") + " units apart");
            }
            const std::uint32_t spawned = NetPlayerBridge::SpawnFrame[index];
            if (spawned != 0 && NetSession::NetFrame() - spawned < 60)
            {
                NetLog::Event("[spawnfire] slot " + std::to_string(slot) + " fired "
                    + std::to_string(NetSession::NetFrame() - spawned) + " frame(s) after spawning, from "
                    + Coordinates(position) + " hp=" + std::to_string(shooter.Health())
                    + " shot=" + Coordinates(shotVec)
                    + " aim=" + Coordinates(aimVec));
            }
        }
        if (LengthSquared(shotVec) > 0.0001F && LengthSquared(aimVec) > 0.0001F)
        {
            const float dot = MathClamp(
                OpenTK::Mathematics::Vector3::Dot(shotVec.Normalized(), aimVec.Normalized()), -1.0F, 1.0F);
            const double degrees = std::acos(static_cast<double>(dot)) * 180.0 / std::numbers::pi;
            AimDrift[index] += degrees;
            WorstDrift[index] = MathMax(WorstDrift[index], degrees);
        }
    }

    void NetDamage::NotePlayerOverlap(Entities::EntityBase* owner, Entities::PlayerEntity& target)
    {
        auto* shooter = dynamic_cast<Entities::PlayerEntity*>(owner);
        if (!NetSession::Active() || shooter == nullptr)
        {
            return;
        }
        const std::int32_t shooterSlot = shooter->SlotIndex();
        const std::int32_t targetSlot = target.SlotIndex();
        if (shooterSlot >= 0 && shooterSlot < Slots && targetSlot == NetHooks::LocalSlot() && shooterSlot != targetSlot)
        {
            const std::uint32_t now = NetSession::NetFrame();
            LastOverlapOnLocal[static_cast<std::size_t>(shooterSlot)] = std::max(1U, now);
            // An impact drawn just after the damage it belongs to.
            auto& frames = UnseenFrame[static_cast<std::size_t>(shooterSlot)];
            for (std::size_t i = 0; i < UnseenDepth; i++)
            {
                if (frames[i] != 0 && now - frames[i] <= VisibleWindow)
                {
                    frames[i] = 0;
                    IncrementInPlace(HitsTakenSeen);
                    IncrementInPlace(HitsTakenSeenByBeam[UnseenBeam[static_cast<std::size_t>(shooterSlot)][i]]);
                    break;
                }
            }
        }
        if (shooterSlot >= 0 && shooterSlot < Slots && targetSlot >= 0 && targetSlot < Slots)
        {
            IncrementInPlace(PlayerOverlapsByShooter[static_cast<std::size_t>(shooterSlot)]
                [static_cast<std::size_t>(targetSlot)]);
        }
    }

    void NetDamage::ResetForRoomChange()
    {
        Resolved.fill(0);
        Replayed.fill(0);
        Fired.fill(0);
        NetShotDiagnostics::Reset();
        NetTimingDiagnostics::Reset();
        PlayerChecks.fill(0);
        PlayerOverlaps.fill(0);
        PlayerAccepted.fill(0);
        for (auto& row : PlayerOverlapsByShooter)
        {
            row.fill(0);
        }
        AimDrift.fill(0.0);
        WorstDrift.fill(0.0);
        ShockCoilSpawned = 0;
        ShockCoilAcquired = 0;
        BombPlayerChecks = 0;
        BombTeamSkips = 0;
        BombHits = 0;
        DamageByBeam.fill(0);
        HitsByBeam.fill(0);
        BombDamageDealt = 0;
        BombDamageHits = 0;
        BombSpawnCalls = 0;
        BombSpawnMade = 0;
        BombSpawnDetonated = 0;
        BombSpawnStaleCount = 0;
        BombSpawnPoolEmpty = 0;
        BombNearest = std::numeric_limits<float>::max();
        BombRadiusSeen = 0.0F;
        _replaying = false;
        _replayBeam = MphRead::BeamType::None;
    }

    void NetDamage::ForgetSlot(std::int32_t slot)
    {
        if (slot < 0 || slot >= Slots)
        {
            return;
        }
        const auto index = static_cast<std::size_t>(slot);
        _history[index].fill(DamageEvent{});
        _sequence[index] = 0;
        _attacker[index] = NoSlot;
        _beam[index] = NoBeam;
        _flags[index] = 0;
        _direction[index] = OpenTK::Mathematics::Vector3::Zero;
        _lastSeen[index] = 0;
        _everSeen[index] = false;
        Resolved[index] = 0;
        Replayed[index] = 0;
    }

    void NetDamage::NoteRespawn(std::int32_t slot, std::uint16_t sequence)
    {
        if (slot < 0 || slot >= Slots)
        {
            return;
        }
        _everSeen[static_cast<std::size_t>(slot)] = true;
        _lastSeen[static_cast<std::size_t>(slot)] = sequence;
    }

    void NetDamage::Reset()
    {
        for (auto& row : _history)
        {
            row.fill(DamageEvent{});
        }
        _sequence.fill(0);
        _attacker.fill(NoSlot);
        _beam.fill(NoBeam);
        _flags.fill(0);
        _direction.fill(OpenTK::Mathematics::Vector3::Zero);
        _lastSeen.fill(0);
        _everSeen.fill(false);
        Resolved.fill(0);
        Replayed.fill(0);
        Fired.fill(0);
        NetShotDiagnostics::Reset();
        NetTimingDiagnostics::Reset();
        PlayerChecks.fill(0);
        PlayerOverlaps.fill(0);
        PlayerAccepted.fill(0);
        for (auto& row : PlayerOverlapsByShooter)
        {
            row.fill(0);
        }
        AimDrift.fill(0.0);
        WorstDrift.fill(0.0);
        ShockCoilSpawned = 0;
        ShockCoilAcquired = 0;
        BombPlayerChecks = 0;
        BombTeamSkips = 0;
        BombHits = 0;
        DamageByBeam.fill(0);
        HitsByBeam.fill(0);
        BombDamageDealt = 0;
        BombDamageHits = 0;
        BombSpawnCalls = 0;
        BombSpawnMade = 0;
        BombSpawnDetonated = 0;
        BombSpawnStaleCount = 0;
        BombSpawnPoolEmpty = 0;
        BombNearest = std::numeric_limits<float>::max();
        BombRadiusSeen = 0.0F;
        _replaying = false;
        _replayBeam = MphRead::BeamType::None;
    }

    bool NetDamage::Suppress(Entities::PlayerEntity& victim,
        Entities::EntityBase* source, Entities::DamageFlags flags)
    {
        return Refuses(victim, source, flags, true);
    }

    bool NetDamage::ResolvedHere(Entities::PlayerEntity& victim,
        Entities::EntityBase* source, Entities::DamageFlags flags)
    {
        return !Refuses(victim, source, flags, false);
    }

    bool NetDamage::Refuses(Entities::PlayerEntity& victim,
        Entities::EntityBase* source, Entities::DamageFlags flags, bool noting)
    {
        if (!NetSession::Active() || _replaying)
        {
            return false;
        }
        auto* projectile = dynamic_cast<Entities::BeamProjectileEntity*>(source);
        if (projectile != nullptr && !NetPlayerLifecycle::CurrentProjectile(*projectile))
        {
            return true;
        }
        if (NetSession::IsHost() || NetSession::IsAuthority())
        {
            // Shooter-authoritative hits: what a remote player's own machine
            // resolved arrives as a claim and is applied from it. The copy
            // simulated here, a round trip later against a different world,
            // would be a second opinion -- the one players saw as a hit with
            // no hit marker, or a kill taken back.
            if (NetHitClaims::ShooterHits() && !NetHitClaims::ApplyingClaimNow() && !_replaying)
            {
                Entities::PlayerEntity* owner = NetHitPrediction::OwnerOf(source);
                // Both ends human: a bot has no machine of its own to resolve
                // hits on it, so hits on bots stay the authority's.
                // A remote player's hits on itself are its claims too.
                if (owner != nullptr && !owner->IsBot() && !victim.IsBot()
                    && owner->SlotIndex() != NetSession::LocalSlot() && owner->SlotIndex() >= 0
                    && static_cast<std::size_t>(owner->SlotIndex()) < NetSession::SlotOccupied.size()
                    && NetSession::SlotOccupied[static_cast<std::size_t>(owner->SlotIndex())])
                {
                    if (noting)
                    {
                        NetHitClaims::NoteServerCopySuppressed();
                    }
                    return true;
                }
            }
            if (projectile != nullptr && projectile->ModLaunchFrame != 0)
            {
                Entities::PlayerEntity* owner = dynamic_cast<Entities::PlayerEntity*>(projectile->Owner().get());
                if (owner == nullptr)
                {
                    auto* turret = dynamic_cast<Entities::HalfturretEntity*>(projectile->Owner().get());
                    owner = turret != nullptr ? turret->Owner().get() : nullptr;
                }
                if (owner != nullptr && NetHitClaims::AlreadyRescued(
                    owner->SlotIndex(), victim.SlotIndex(), projectile->ModLaunchFrame, projectile->ModLaunchKey()))
                {
                    return true;
                }
            }
            return false;
        }
        if (projectile != nullptr && projectile->ModLaunchKey().ShooterSlot >= 0
            && !NetPlayerLifecycle::Matches(projectile->ModLaunchKey().ShooterSlot,
                projectile->ModLaunchKey().Generation, projectile->ModLaunchKey().LifeId))
        {
            return true;
        }
        return !NetHitPrediction::Predicts(victim, source, flags);
    }

    void NetDamage::Note(Entities::PlayerEntity& victim,
        Entities::PlayerEntity* attacker, MphRead::BeamType beam,
        Entities::DamageFlags flags,
        std::optional<OpenTK::Mathematics::Vector3> direction,
        std::uint32_t amount, bool fromBomb, std::uint32_t launchFrame, std::optional<ShotKey> launchKey)
    {
        if (beam == MphRead::BeamType::None && _claimedBeam != MphRead::BeamType::None)
        {
            beam = _claimedBeam;
        }
        if (_applyingClaim)
        {
            launchFrame = NetHitClaims::CurrentClaimLaunch();
        }
        if (!NetSession::Active() || _replaying || NetHitPrediction::Predicting())
        {
            return;
        }
        const std::int32_t signedAmount = std::bit_cast<std::int32_t>(amount);
        if (fromBomb)
        {
            BombDamageDealt = UncheckedAdd(BombDamageDealt, signedAmount);
            IncrementInPlace(BombDamageHits);
        }
        else
        {
            const std::int32_t beamIndex = static_cast<std::int32_t>(beam);
            if (beamIndex >= 0 && beamIndex < static_cast<std::int32_t>(DamageByBeam.size()))
            {
                const auto index = static_cast<std::size_t>(beamIndex);
                DamageByBeam[index] = UncheckedAdd(DamageByBeam[index], signedAmount);
                IncrementInPlace(HitsByBeam[index]);
            }
        }
        const std::int32_t slot = victim.SlotIndex();
        if (slot < 0 || slot >= Slots)
        {
            return;
        }
        const auto index = static_cast<std::size_t>(slot);
        const auto weapon = static_cast<std::size_t>(NetShotDiagnostics::Bucket(beam));
        IncrementInPlace(NetShotDiagnostics::AuthorityHits[weapon]);
        NetShotDiagnostics::AuthorityDamage[weapon] = UncheckedAdd(
            NetShotDiagnostics::AuthorityDamage[weapon], static_cast<std::int64_t>(amount));
        if (Runtime::HasFlag(flags, Entities::DamageFlags::Headshot))
        {
            IncrementInPlace(NetShotDiagnostics::AuthorityHeadshots[weapon]);
        }
        if (attacker != nullptr && NetLog::Enabled())
        {
            NetShotDiagnostics::Trace("authority-hit",
                launchKey.value_or(ShotKey::For(attacker->SlotIndex(), launchFrame)), beam,
                "victim=" + std::to_string(slot) + " damage=" + std::to_string(amount));
        }
        _sequence[index] = NetLifecycleTracker::Next(_sequence[index]);
        if (NetLog::Enabled())
        {
            NetLog::Event("[damage-publish] epoch=" + std::to_string(NetSession::AuthorityEpoch())
                + " match=" + std::to_string(NetSession::CurrentMatchId())
                + " victim=" + std::to_string(slot) + "/" + std::to_string(NetPlayerLifecycle::Generation(slot))
                + "/" + std::to_string(NetPlayerLifecycle::Get(slot))
                + " event=" + std::to_string(_sequence[index])
                + " shooter=" + (attacker != nullptr ? std::to_string(attacker->SlotIndex()) : std::string())
                + " launch=" + std::to_string(launchFrame));
        }
        IncrementInPlace(Resolved[index]);
        if (NetLog::Enabled())
        {
            std::string line = "[resolve] slot " + std::to_string(attacker != nullptr ? attacker->SlotIndex() : -1)
                + " hit slot " + std::to_string(slot) + " for " + std::to_string(amount) + " with "
                + ::MphRead::ToString(beam) + " (launch " + std::to_string(launchFrame) + "), health "
                + std::to_string(victim.Health()) + " -> "
                + std::to_string(std::max(0, Runtime::UncheckedSubtract(victim.Health(), signedAmount)));
            if (attacker != nullptr)
            {
                const OpenTK::Mathematics::Vector3 position = attacker->Position;
                line += " | shooter " + Coordinates(position) + " hp=" + std::to_string(attacker->Health());
            }
            const OpenTK::Mathematics::Vector3 victimPosition = victim.Position;
            line += " | victim " + Coordinates(victimPosition);
            NetLog::Event(line);
        }
        NetHitClaims::NoteAuthorityHit(attacker != nullptr ? attacker->SlotIndex() : -1, slot, launchFrame,
            signedAmount, beam, launchKey);
        _attacker[index] = attacker != nullptr && attacker->SlotIndex() >= 0 && attacker->SlotIndex() < Slots
            ? static_cast<std::uint8_t>(attacker->SlotIndex())
            : NoSlot;
        _beam[index] = beam == MphRead::BeamType::None ? NoBeam : static_cast<std::uint8_t>(beam);
        _flags[index] = static_cast<std::uint8_t>(static_cast<std::int32_t>(flags) & RelayedFlags);
        _direction[index] = ClampImpulse(direction.value_or(OpenTK::Mathematics::Vector3::Zero));
        auto& history = _history[index];
        for (std::size_t i = 0; i < PlayerState::DamageHistory - 1; i++)
        {
            history[i] = history[i + 1];
        }
        DamageEvent latest{};
        latest.EventId = _sequence[index];
        latest.AttackerSlot = _attacker[index];
        latest.AttackerGeneration = launchKey.has_value() ? launchKey->Generation
            : attacker != nullptr ? NetPlayerLifecycle::Generation(attacker->SlotIndex()) : std::uint16_t{0};
        latest.Damage = static_cast<std::uint16_t>(std::min<std::uint32_t>(amount, 0xFFFFU));
        latest.Beam = _beam[index];
        latest.Flags = _flags[index];
        latest.Direction = _direction[index];
        // Which shot, and where on the body the shooter saw it land: the
        // victim's machine shows that same shot arriving there.
        _impactLaunch[index] = static_cast<std::uint8_t>(launchFrame & 0xFFU);
        _impact[index] = NetHitClaims::ApplyingClaimNow() ? NetHitClaims::CurrentClaimImpact() : ImpactOffset{};
        history[PlayerState::DamageHistory - 1] = latest;
    }

    OpenTK::Mathematics::Vector3 NetDamage::ClampImpulse(OpenTK::Mathematics::Vector3 impulse)
    {
        if (!std::isfinite(impulse.X) || !std::isfinite(impulse.Y) || !std::isfinite(impulse.Z))
        {
            return OpenTK::Mathematics::Vector3::Zero;
        }
        const float length = Length(impulse);
        if (length <= MaxImpulse)
        {
            return impulse;
        }
        NetLog::Event("knockback clamped from " + Runtime::ToString(length, "0.##") + " to "
            + Runtime::ToString(MaxImpulse));
        return Multiply(impulse, MaxImpulse / length);
    }

    void NetDamage::ReplayDeath(Entities::PlayerEntity& player)
    {
        const bool wasReplaying = _replaying;
        _replaying = true;
        SaveScores();
        try
        {
            player.TakeDamage(1, Entities::DamageFlags::Death | Entities::DamageFlags::NoDmgInvuln,
                std::nullopt, nullptr);
        }
        catch (...)
        {
            RestoreScores();
            _replaying = wasReplaying;
            throw;
        }
        RestoreScores();
        _replaying = wasReplaying;
    }

    void NetDamage::SaveScores()
    {
        std::copy_n(GameState::Points().begin(), Slots, _savedPoints.begin());
        std::copy_n(GameState::Kills().begin(), Slots, _savedKills.begin());
        std::copy_n(GameState::Deaths().begin(), Slots, _savedDeaths.begin());
    }

    void NetDamage::RestoreScores()
    {
        std::copy_n(_savedPoints.begin(), Slots, GameState::Points().begin());
        std::copy_n(_savedKills.begin(), Slots, GameState::Kills().begin());
        std::copy_n(_savedDeaths.begin(), Slots, GameState::Deaths().begin());
    }

    void NetDamage::Write(std::int32_t slot, PlayerState& state)
    {
        if (slot < 0 || slot >= Slots)
        {
            return;
        }
        const auto index = static_cast<std::size_t>(slot);
        state.DamageEventId = _sequence[index];
        state.Damage0 = _history[index][0];
        state.Damage1 = _history[index][1];
        state.Damage2 = _history[index][2];
        state.Damage3 = _history[index][3];
        state.AttackerSlot = _attacker[index];
        state.DamageBeam = _beam[index];
        state.DamageFlags = _flags[index];
        state.HitDirection = _direction[index];
        state.ImpactLaunchLow = _impactLaunch[index];
        state.Impact = _impact[index];
    }

    void NetDamage::BeginLife(std::int32_t slot, const PlayerState& state)
    {
        const auto index = static_cast<std::size_t>(slot);
        Runtime::ManagedAt(_lastLife, slot) = state.LifeId;
        _lastGeneration[index] = state.SlotGeneration;
        _everSeen[index] = true;
        _lastSeen[index] = state.DamageEventId;
    }

    void NetDamage::Replay(Entities::PlayerEntity& player, const PlayerState& state)
    {
        const std::int32_t slot = player.SlotIndex();
        if (slot < 0 || slot >= Slots)
        {
            return;
        }
        if (!NetPlayerLifecycle::Matches(slot, state.SlotGeneration, state.LifeId))
        {
            IncrementInPlace(NetPlayerLifecycle::OldLifeDamage);
            return;
        }
        const auto index = static_cast<std::size_t>(slot);
        if (!_everSeen[index] || _lastLife[index] != state.LifeId || _lastGeneration[index] != state.SlotGeneration)
        {
            BeginLife(slot, state);
            return;
        }
        for (std::int32_t i = 0; i < PlayerState::DamageHistory; i++)
        {
            const DamageEvent hit = state.EventAt(i);
            if (hit.EventId == 0
                || (_lastSeen[index] != 0 && !NetLifecycleTracker::Newer(hit.EventId, _lastSeen[index])))
            {
                continue;
            }
            _lastSeen[index] = hit.EventId;
            PlayerState feedback = state;
            feedback.AttackerSlot = hit.AttackerGeneration != 0
                && NetPlayerLifecycle::Generation(hit.AttackerSlot) == hit.AttackerGeneration
                ? hit.AttackerSlot : NoSlot;
            feedback.DamageBeam = hit.Beam;
            feedback.DamageFlags = hit.Flags;
            feedback.HitDirection = hit.Direction;
            feedback.Health = hit.EventId == state.DamageEventId ? state.Health
                : static_cast<std::uint16_t>(std::max(1, player.Health() - static_cast<std::int32_t>(hit.Damage)));
            if (NetLog::Enabled())
            {
                NetLog::Event("[damage-replay] epoch=" + std::to_string(NetSession::AuthorityEpoch())
                    + " match=" + std::to_string(NetSession::CurrentMatchId())
                    + " victim=" + std::to_string(slot) + "/" + std::to_string(state.SlotGeneration)
                    + "/" + std::to_string(state.LifeId) + " event=" + std::to_string(hit.EventId)
                    + " shooter=" + std::to_string(hit.AttackerSlot) + "/" + std::to_string(hit.AttackerGeneration));
            }
            if (slot != NetHooks::LocalSlot() && feedback.AttackerSlot != NoSlot
                && static_cast<std::int32_t>(feedback.AttackerSlot) != slot
                && static_cast<std::int32_t>(feedback.AttackerSlot) != NetHooks::LocalSlot())
            {
                // Two other players: this machine is watching A hit B.
                HitLocation::ObservedDamage(static_cast<std::int32_t>(feedback.AttackerSlot), player, hit.Beam,
                    static_cast<std::int32_t>(hit.Damage),
                    (hit.Flags & static_cast<std::int32_t>(Entities::DamageFlags::Headshot)) != 0);
                NetPlayerBridge::ConfirmIncoming(static_cast<std::int32_t>(feedback.AttackerSlot), slot, hit.Beam,
                    hit.EventId == state.DamageEventId, state.ImpactLaunchLow, state.Impact,
                    (hit.Flags & static_cast<std::int32_t>(Entities::DamageFlags::Headshot)) != 0);
            }
            if (slot == NetHooks::LocalSlot() && feedback.AttackerSlot == NoSlot)
            {
                // Nobody's: the world, a self-hit -- or a burn tick nobody owns.
                HitLocation::Damage(-1, player, hit.Beam, static_cast<std::int32_t>(hit.Damage), false);
            }
            if (slot == NetHooks::LocalSlot() && feedback.AttackerSlot != NoSlot
                && static_cast<std::int32_t>(feedback.AttackerSlot) != slot
                && static_cast<std::size_t>(feedback.AttackerSlot) < LastOverlapOnLocal.size())
            {
                HitLocation::Damage(static_cast<std::int32_t>(feedback.AttackerSlot), player, hit.Beam,
                    static_cast<std::int32_t>(hit.Damage),
                    (hit.Flags & static_cast<std::int32_t>(Entities::DamageFlags::Headshot)) != 0);
                NetPlayerBridge::ConfirmIncoming(static_cast<std::int32_t>(feedback.AttackerSlot), slot, hit.Beam,
                    hit.EventId == state.DamageEventId, state.ImpactLaunchLow, state.Impact,
                    (hit.Flags & static_cast<std::int32_t>(Entities::DamageFlags::Headshot)) != 0);
                IncrementInPlace(HitsTaken);
                const auto beam = static_cast<std::size_t>(feedback.DamageBeam == NoBeam || feedback.DamageBeam > 9
                    ? 10 : feedback.DamageBeam);
                IncrementInPlace(HitsTakenByBeam[beam]);
                const std::uint32_t seen = LastOverlapOnLocal[static_cast<std::size_t>(feedback.AttackerSlot)];
                if (seen != 0 && NetSession::NetFrame() - seen <= VisibleWindow)
                {
                    IncrementInPlace(HitsTakenSeen);
                    IncrementInPlace(HitsTakenSeenByBeam[beam]);
                }
                else
                {
                    if (!NetPlayerBridge::ConfirmedImpacts())
                    {
                        NetPlayerBridge::SteerIncoming(static_cast<std::int32_t>(feedback.AttackerSlot));
                    }
                    auto& frames = UnseenFrame[static_cast<std::size_t>(feedback.AttackerSlot)];
                    std::size_t oldest = 0;
                    for (std::size_t i = 1; i < UnseenDepth; i++)
                    {
                        if (frames[i] < frames[oldest]) oldest = i;
                    }
                    frames[oldest] = std::max(1U, NetSession::NetFrame());
                    UnseenBeam[static_cast<std::size_t>(feedback.AttackerSlot)][oldest] = static_cast<std::uint8_t>(beam);
                }
            }
            ReplayEvent(player, feedback);
        }
    }

    void NetDamage::ReplayEvent(Entities::PlayerEntity& player, const PlayerState& state)
    {
        const std::int32_t slot = player.SlotIndex();
        constexpr std::int32_t landed = 1;
        IncrementInPlace(Runtime::ManagedAt(Replayed, slot));
        const bool lethal = state.Health == 0;
        const bool mine = static_cast<std::int32_t>(state.AttackerSlot) == NetHooks::LocalSlot();
        const bool authorityHeadshot = (state.DamageFlags & static_cast<std::int32_t>(Entities::DamageFlags::Headshot)) != 0;
        const bool predicted = mine && NetHitPrediction::Confirm(slot, landed, authorityHeadshot);
        if (player.Health() <= 0)
        {
            return; // already down here; the respawn is what matters next
        }
        Entities::PlayerEntity* attacker = static_cast<std::size_t>(state.AttackerSlot)
                < Entities::PlayerEntity::Players().size()
            ? Entities::PlayerEntity::Players()[static_cast<std::size_t>(state.AttackerSlot)].get()
            : nullptr;
        if (predicted && !lethal)
        {
            return;
        }
        std::int32_t amount = std::max(1, player.Health() - static_cast<std::int32_t>(state.Health));
        if (!lethal)
        {
            amount = std::min(amount, std::max(0, player.Health() - 1));
        }
        Entities::DamageFlags flags = static_cast<Entities::DamageFlags>(static_cast<std::int32_t>(state.DamageFlags))
            | Entities::DamageFlags::NoDmgInvuln;
        if (lethal)
        {
            flags |= Entities::DamageFlags::Death;
        }
        const OpenTK::Mathematics::Vector3 impulse = ClampImpulse(state.HitDirection);
        const std::optional<OpenTK::Mathematics::Vector3> direction = IsZero(impulse)
            ? std::nullopt : std::optional<OpenTK::Mathematics::Vector3>(impulse);
        _replaying = true;
        _replayBeam = state.DamageBeam == NoBeam
            ? MphRead::BeamType::None
            : static_cast<MphRead::BeamType>(std::bit_cast<std::int8_t>(state.DamageBeam));
        SaveScores();
        try
        {
            player.TakeDamage(static_cast<std::uint32_t>(amount), flags, direction, attacker);
        }
        catch (...)
        {
            RestoreScores();
            _replaying = false;
            _replayBeam = MphRead::BeamType::None;
            throw;
        }
        RestoreScores();
        _replaying = false;
        _replayBeam = MphRead::BeamType::None;
    }
}
