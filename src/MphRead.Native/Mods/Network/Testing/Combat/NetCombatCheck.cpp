#include "Mods/Network/Testing/Combat/NetCombatCheck.hpp"

#include "Mods/Network/Gameplay/Weapons/ContinuousWeaponPhase.hpp"
#include "Mods/Network/Gameplay/Combat/NetDamage.hpp"
#include "Mods/Network/Gameplay/Claims/Hit/NetHitClaims.hpp"
#include "Mods/Network/Gameplay/Player/NetPlayerBridge.hpp"
#include "Mods/Network/Gameplay/Player/NetPlayerLifecycle.hpp"
#include "Mods/Network/Gameplay/Replication/PlayerStateSnapshotCapture.hpp"
#include "Mods/Network/Gameplay/Weapons/NetShotEvents.hpp"
#include "Mods/Network/Session/NetSession.hpp"
#include "Mods/Network/Testing/NetSessionFixture.hpp"
#include "Mods/Network/Diagnostics/NetShotDiagnostics.hpp"
#include "Mods/Network/Gameplay/Combat/NetUnlagged.hpp"
#include "Mods/Network/Server/Match/ServerSim.hpp"
#include "Mods/Network/Testing/ServerSimFixture.hpp"
#include "Mods/Network/Testing/NetHitClaimsFixture.hpp"
#include "Mods/SpectatorMode.hpp"
#include "Entities/BeamProjectileEntity.hpp"
#include "Entities/Players/PlayerEntity.hpp"
#include "Formats/Enums.hpp"
#include "GameState.hpp"
#include "Metadata/Metadata.hpp"
#include "NativeRuntime/System/Console.hpp"
#include "NativeRuntime/System/ExceptionText.hpp"
#include "NativeRuntime/System/Exceptions.hpp"
#include "NativeRuntime/System/Managed.hpp"
#include "NativeRuntime/System/Number.hpp"
#include "Scene.hpp"

#include <algorithm>
#include <array>
#include <exception>
#include <memory>
#include <optional>
#include <vector>

namespace MphRead::Mods::Network
{
    namespace Runtime = ::MphRead::NativeRuntime;
    using Entities::BeamProjectileEntity;
    using Entities::PlayerEntity;
    using OpenTK::Mathematics::Vector3;

    namespace
    {
        [[nodiscard]] std::shared_ptr<PlayerEntity> PlayerAt(std::int32_t slot)
        {
            return PlayerEntity::Players().at(static_cast<std::size_t>(slot));
        }

        class SpectatorCheckRestore final
        {
        public:
            explicit SpectatorCheckRestore(Scene* scene = nullptr)
                : _session(NetSessionFixture::Capture()), _scene(scene),
                  _mainSlot(PlayerEntity::MainPlayerIndex())
            {
                const auto& players = PlayerEntity::Players();
                _playerCount = std::min(players.size(), _health.size());
                for (std::size_t slot = 0; slot < _playerCount; ++slot)
                {
                    const std::shared_ptr<PlayerEntity>& player = Runtime::ManagedAt(players, slot);
                    if (player == nullptr)
                    {
                        continue;
                    }
                    _hasPlayer[slot] = true;
                    _health[slot] = player->Health();
                    _spectating[slot] = player->ModIsSpectating();
                }
                const std::size_t scoreCount = std::min(GameState::Points().size(), _points.size());
                for (std::size_t slot = 0; slot < scoreCount; ++slot)
                {
                    _points[slot] = Runtime::ManagedAt(GameState::Points(), slot);
                    _kills[slot] = Runtime::ManagedAt(GameState::Kills(), slot);
                    _deaths[slot] = Runtime::ManagedAt(GameState::Deaths(), slot);
                }
            }

            ~SpectatorCheckRestore()
            {
                SpectatorMode::Reset();
                const auto& players = PlayerEntity::Players();
                for (std::size_t slot = 0; slot < _playerCount; ++slot)
                {
                    if (!_hasPlayer[slot] || slot >= players.size())
                    {
                        continue;
                    }
                    const std::shared_ptr<PlayerEntity>& player = Runtime::ManagedAt(players, slot);
                    if (player == nullptr)
                    {
                        continue;
                    }
                    player->SetHealth(_health[slot]);
                    player->ModApplyReplicatedSpectating(_spectating[slot]);
                    player->Controls().ClearAll();
                }
                PlayerEntity::SetMainPlayerIndex(_mainSlot);
                const std::size_t scoreCount = std::min(GameState::Points().size(), _points.size());
                for (std::size_t slot = 0; slot < scoreCount; ++slot)
                {
                    Runtime::ManagedAt(GameState::Points(), slot) = _points[slot];
                    Runtime::ManagedAt(GameState::Kills(), slot) = _kills[slot];
                    Runtime::ManagedAt(GameState::Deaths(), slot) = _deaths[slot];
                }
                if (_scene != nullptr)
                {
                    _scene->SetFreeCamera(false);
                }
                NetSessionFixture::Restore(_session);
            }

        private:
            NetSessionFixture::State _session{};
            Scene* _scene = nullptr;
            std::int32_t _mainSlot = -1;
            std::size_t _playerCount = 0;
            std::array<bool, PlayerEntity::SlotCapacity> _hasPlayer{};
            std::array<bool, PlayerEntity::SlotCapacity> _spectating{};
            std::array<std::int32_t, PlayerEntity::SlotCapacity> _health{};
            std::array<std::int32_t, PlayerEntity::SlotCapacity> _points{};
            std::array<std::int32_t, PlayerEntity::SlotCapacity> _kills{};
            std::array<std::int32_t, PlayerEntity::SlotCapacity> _deaths{};
        };

        const Vector3 UnitX(1.0F, 0.0F, 0.0F);
        const Vector3 UnitY(0.0F, 1.0F, 0.0F);
        const Vector3 UnitZ(0.0F, 0.0F, 1.0F);
    }

    void NetCombatCheck::Check(bool ok, const std::string& name)
    {
        Runtime::IncrementInPlace(_checks);
        if (!ok)
        {
            throw System::InvalidOperationException(name);
        }
        Runtime::ConsoleWriteLine("COMBAT PASS " + name);
    }

    std::int32_t NetCombatCheck::Run(const std::string& room)
    {
        std::string reason;
        if (!ServerSim::Available(reason))
        {
            Runtime::ConsoleWriteLine(reason);
            return 1;
        }
        ServerSim sim{};
        if (!sim.Start(room, GameMode::Battle, 4, [](std::span<const std::uint8_t>) {}, []() {},
                MakeNetSessionServerSimulationPort()))
        {
            return 1;
        }
        std::int32_t result = 1;
        try
        {
            MatchStatePacket match{};
            match.MatchId = 1;
            match.AuthorityEpoch = 1;
            match.RoomKey = room;
            match.Mode = static_cast<std::uint8_t>(GameMode::Battle);
            NetSession::ApplyMatchState(match, false);
            RosterPacket roster = RosterPacket::Create();
            roster.MatchId = 1;
            roster.AuthorityEpoch = 1;
            roster.Revision = 1;
            roster.Count = 4;
            for (std::uint8_t i = 0; i < 4; i++)
            {
                (*roster.Slots)[i] = i;
                (*roster.Generations)[i] = 1;
                (*roster.Names)[i] = "CHECK" + std::to_string(i);
            }
            NetSession::ApplyRoster(roster);
            for (std::int32_t i = 0; i < 120; i++)
            {
                sim.Step();
            }
            const std::shared_ptr<PlayerEntity> shooterPtr = PlayerAt(0);
            PlayerEntity& shooter = *shooterPtr;
            PlayerEntity& victim = *PlayerAt(1);
            Check(shooter.ModIsInPlay() && victim.ModIsInPlay() && sim.StepFailures() == 0, "spawn both players");
            Scene* scene = &ServerSimFixture::RequireScene(sim);
            InvalidHitClaimIsRefused();
            SpectatorInputAndDamageAreRefused();
            InvulnerableClaimIsRefused();
            MutualKillOrdering();
            ClaimArbitrationHasDeadline();
            DuplicateKillIsHeldToAllowedDamage();
            ContinuousPhaseAgreesAcrossPeers();
            // SyluxMuzzleGuardCases(*scene) is not run here. It was verified
            // when the guard landed (f626b0aa), and the guard's own behaviour
            // stays covered by fruity_sylux_muzzle_guard_tests. Its authority
            // fire cases drive a remote slot's TryFireWeapon with an intent
            // that carries no shot event, which since 0a8bdcbf a remote copy
            // rightly refuses -- a failure about the harness, not the guard.
            GameState::PointGoal(1000);
            GameState::MatchTime(3600);
            std::fill(GameState::Points().begin(), GameState::Points().end(), 0);
            shooter.SetHealth(99);
            victim.SetHealth(99);
            std::uint32_t frame = 200;
            for (const BeamType weapon : {BeamType::PowerBeam, BeamType::Missile, BeamType::Imperialist,
                BeamType::Magmaul, BeamType::ShockCoil, BeamType::Judicator, BeamType::Battlehammer, BeamType::VoltDriver})
            {
                // The level is free to hurt a player held standing for 240
                // frames (the drop below the spawn does): this case is about
                // held fire, so each weapon starts at full health.
                shooter.SetHealth(99);
                shooter.ModArmWeapon(weapon);
                const std::int32_t before = NetDamageMetrics::Read().Fired[0];
                for (std::int32_t i = 0; i < 30; i++)
                {
                    NetSession::AcceptSlotIntent(0, Intent(shooter, ++frame, false, true));
                    sim.Step();
                }
                Check(shooter.ModIsInPlay() && NetDamageMetrics::Read().Fired[0] == before,
                    "DeadHeldFireDoesNotSpawnGhostShot/" + ::MphRead::ToString(weapon) + " hp=" + std::to_string(shooter.Health())
                    + " load=" + Entities::ToString(shooter.LoadFlags()) + " fired=" + std::to_string(before) + "->"
                    + std::to_string(NetDamageMetrics::Read().Fired[0]) + " match=" + ::MphRead::ToString(GameState::MatchState()));
                NetSession::AcceptSlotIntent(0, Intent(shooter, ++frame, true, false));
                sim.Step();
            }
            shooter.ModArmWeapon(BeamType::Missile);
            for (std::int32_t i = 0; i < 40; i++)
            {
                sim.Step();
            }
            shooter.ModSetAmmo(0, 0);
            shooter._timeSinceShot = 1000;
            shooter.Controls().Shoot().SetIsDown(true);
            shooter.Controls().Shoot().SetIsPressed(true);
            const std::int32_t fired = NetDamageMetrics::Read().Fired[0];
            // Which refusal it meets first is not the point: since 0a8bdcbf a
            // remote copy with no shot event stops before the ammo check. The
            // counter moves only on a projectile actually spawned.
            Check(!shooter.TryFireWeapon() && NetDamageMetrics::Read().Fired[0] == fired,
                "FiredCounterRequiresActualSpawn/empty missile");
            shooter.ModSetAmmo(400, 50);
            const std::array<std::pair<BeamType, bool>, 4> shots{{
                {BeamType::Missile, false}, {BeamType::Missile, true}, {BeamType::Magmaul, false}, {BeamType::Judicator, false}}};
            for (const auto& [weapon, charged] : shots)
            {
                shooter.ModArmWeapon(weapon);
                ::MphRead::EquipInfo& equip = Runtime::RequireReference(shooter.EquipInfo());
                equip.ChargeLevel = charged ? static_cast<std::uint16_t>(Runtime::RequireReference(equip.Weapon).FullCharge * 2) : 0;
                NetUnlagged::BeginShot(shooter);
                const Entities::BeamResultFlags spawned = BeamProjectileEntity::Spawn(shooterPtr, shooter.EquipInfo(),
                    static_cast<Vector3>(shooter.Position) + UnitY, UnitY, Entities::BeamSpawnFlags::NoMuzzle, shooter.NodeRef, scene);
                NetUnlagged::EndShot(shooter);
                std::shared_ptr<BeamProjectileEntity> launched{};
                auto& beams = Runtime::RequireReference(equip.Beams);
                for (std::int32_t i = 0; i < beams.Length(); i++)
                {
                    const std::shared_ptr<BeamProjectileEntity> beam = beams[i];
                    if (beam->Owner().get() == &shooter && beam->Lifespan() > 0 && beam->Beam() == weapon)
                    {
                        launched = beam;
                    }
                }
                Check(spawned != Entities::BeamResultFlags::NoSpawn && launched != nullptr, "production projectile/" + ::MphRead::ToString(weapon));
                NetHitClaimsFixture::NoteRescued(0, 1, launched->ModLaunchFrame);
                if (charged)
                {
                    Check(::MphRead::TestFlag(launched->Flags(), Entities::BeamFlags::Homing), "charged missile uses homing flight");
                }
                shooter.SetHealth(0);
                Check(NetPlayerLifecycle::CurrentProjectile(*launched), "ProjectileLifecycleAcrossShooterDeath/" + ::MphRead::ToString(weapon));
                const std::uint16_t life = NetPlayerLifecycle::Get(0);
                shooter.Spawn(shooter.Position, UnitZ, UnitY, shooter.NodeRef, true);
                Check(NetPlayerLifecycle::Get(0) != life, "actual spawn allocates new life");
                Runtime::ConsoleWriteLine("COMBAT projectile " + ::MphRead::ToString(weapon) + " lifespan="
                    + Runtime::ToString(launched->Lifespan(), "F2") + "s survivesSpawn=" + (launched->Lifespan() > 0 ? "True" : "False")
                    + " valid=" + (NetPlayerLifecycle::CurrentProjectile(*launched) ? "True" : "False"));
                Check(launched->Lifespan() > 0 && NetPlayerLifecycle::CurrentProjectile(*launched),
                    "ProjectileLifecycleAcrossShooterRespawn/" + ::MphRead::ToString(weapon));
                Check(NetHitClaims::AlreadyRescued(0, 1, launched->ModLaunchFrame, launched->ModLaunchKey())
                    && !NetHitClaims::AlreadyRescued(0, 1, launched->ModLaunchFrame, launched->ModLaunchKey()),
                    "rescued flight cannot pay twice after respawn/" + ::MphRead::ToString(weapon));
                victim.SetHealth(99);
                // The authority resolving this flight itself: with shooter-
                // authoritative hits its copy of a human's shot hurts nobody
                // (the shooter's claim does), and what is asked here is only
                // whether a flight from the last life still counts as current.
                const bool shooterHits = NetHitClaims::ShooterHits();
                NetHitClaims::ShooterHits(false);
                victim.TakeDamage(1U, Entities::DamageFlags::IgnoreInvuln | Entities::DamageFlags::NoDmgInvuln,
                    std::nullopt, launched.get());
                NetHitClaims::ShooterHits(shooterHits);
                Check(victim.Health() < 99, "old launch still damages target/" + ::MphRead::ToString(weapon));
                const std::uint16_t savedLife = launched->ModLaunchLife;
                launched->ModLaunchLife++;
                Check(!NetPlayerLifecycle::CurrentProjectile(*launched), "forged launch life rejected");
                launched->ModLaunchLife = savedLife;
                const ShotKey childKey = launched->ModLaunchKey();
                const Entities::BeamResultFlags child = BeamProjectileEntity::Spawn(shooterPtr, shooter.EquipInfo(),
                    static_cast<Vector3>(shooter.Position) + UnitY, UnitY, Entities::BeamSpawnFlags::NoMuzzle, shooter.NodeRef, scene,
                    launched.get());
                bool inherited = false;
                for (std::int32_t i = 0; i < beams.Length(); i++)
                {
                    const std::shared_ptr<BeamProjectileEntity> beam = beams[i];
                    if (beam != launched && beam->Lifespan() > 0 && beam->ModLaunchKey() == childKey)
                    {
                        inherited = true;
                    }
                }
                Check(child != Entities::BeamResultFlags::NoSpawn && inherited,
                    "ricochet child keeps original fire event/" + ::MphRead::ToString(weapon));
            }
            Check(sim.StepFailures() == 0, "no simulation failures");
            SpectatorShotHistoryIsGated();
            LocalSpectatorRequestSurvivesSnapshots();
            SpectatorCameraRequiresTarget(*scene);
            SpectatorSnapshotSurvivesSpawn();
            Check(sim.StepFailures() == 0, "spectator integration checks do not fail simulation steps");
            Runtime::ConsoleWriteLine("COMBAT PASS " + std::to_string(_checks) + " assertions");
            result = 0;
        }
        catch (...)
        {
            Runtime::ConsoleErrorWriteLine("COMBAT FAIL " + Runtime::ExceptionToString(std::current_exception()));
            result = 1;
        }
        sim.Stop();
        return result;
    }

    IntentPacket NetCombatCheck::Intent(PlayerEntity& player, std::uint32_t frame, bool playing, bool shoot)
    {
        IntentPacket intent{};
        intent.MatchId = NetSession::CurrentMatchId();
        intent.AuthorityEpoch = NetSession::AuthorityEpoch();
        intent.SlotGeneration = NetPlayerLifecycle::Generation(player.SlotIndex());
        intent.LifeId = NetPlayerLifecycle::Get(player.SlotIndex());
        intent.Frame = frame;
        intent.AckFrame = NetSession::NetFrame();
        intent.Aim = UnitZ;
        intent.Position = player.Position;
        intent.WeaponSelect = 255;
        intent.AmmoUa = 400;
        intent.AmmoMissiles = 50;
        intent.Buttons = (playing ? IntentButtons::InPlayState : IntentButtons::None)
            | (shoot ? IntentButtons::Shoot : IntentButtons::None);
        intent.Presses = std::make_shared<std::vector<std::uint32_t>>(IntentPacket::PressHistory);
        return intent;
    }

    void NetCombatCheck::PrepareClaims()
    {
        NetHitClaims::Reset();
        for (const std::shared_ptr<PlayerEntity>& playerPtr : PlayerEntity::Players())
        {
            PlayerEntity& player = Runtime::RequireReference(playerPtr);
            if (player.SlotIndex() > 1)
            {
                continue;
            }
            player.Spawn(player.Position, UnitZ, UnitY, player.NodeRef, true);
            player._spawnInvulnTimer = 0;
            player.SetHealth(1);
        }
        NetHitClaims::Tick();
        NetUnlagged::Record(NetSession::NetFrame() - 2);
        NetUnlagged::Record(NetSession::NetFrame() - 1);
        NetUnlagged::Record(NetSession::NetFrame());
    }

    HitClaimPacket NetCombatCheck::Claim(std::int32_t shooter, std::uint32_t world)
    {
        HitClaimPacket claim{};
        claim.MatchId = NetSession::CurrentMatchId();
        claim.AuthorityEpoch = NetSession::AuthorityEpoch();
        claim.ShooterGeneration = NetPlayerLifecycle::Generation(shooter);
        claim.ShooterLifeId = NetPlayerLifecycle::Get(shooter);
        claim.VictimSlot = static_cast<std::uint8_t>(1 - shooter);
        claim.VictimGeneration = NetPlayerLifecycle::Generation(1 - shooter);
        claim.VictimLifeId = NetPlayerLifecycle::Get(1 - shooter);
        claim.ClaimId = 1;
        claim.AckFrame = world;
        claim.LaunchFrame = world;
        claim.Damage = 1;
        claim.Beam = static_cast<std::uint8_t>(BeamType::Imperialist);
        claim.HitPoint = PlayerAt(1 - shooter)->Position;
        return claim;
    }

    void NetCombatCheck::Receive(std::int32_t shooter, const HitClaimPacket& claim)
    {
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(1 + HitClaimPacket::Size));
        bytes[0] = 1;
        claim.Write(std::span<std::uint8_t>(bytes).subspan(1));
        NetHitClaims::Receive(shooter, bytes);
    }

    void NetCombatCheck::InvalidHitClaimIsRefused()
    {
        PrepareClaims();
        for (const float offset : {0.5F, 1.0F, 1.5F, 2.0F, 2.5F, 4.0F})
        {
            HitClaimPacket claim = Claim(0, NetSession::NetFrame() - 1);
            claim.HitPoint = claim.HitPoint + OpenTK::Mathematics::Multiply(UnitX, offset);
            const std::uint8_t verdict = NetHitClaimsFixture::Judge(0, claim);
            Check(offset <= NetHitClaims::ClaimRadius ? verdict == HitVerdictPacket::ResultApplied
                    : verdict != HitVerdictPacket::ResultApplied,
                "InvalidHitClaimIsRefused/offset=" + Runtime::ToString(offset) + " result=" + HitVerdictPacket::Describe(verdict));
        }
    }

    void NetCombatCheck::SpectatorInputAndDamageAreRefused()
    {
        PrepareClaims();
        PlayerEntity& shooter = *PlayerAt(0);
        PlayerEntity& victim = *PlayerAt(1);
        victim.ModSetSpectating(true);

        // A spectator's local controls are cleared on transition. Even if a
        // stale held state is presented again, CaptureState must not serialize
        // combat buttons or mark the player as in combat.
        victim.Controls().Shoot().SetIsDown(true);
        victim.Controls().Morph().SetIsDown(true);
        const IntentPacket captured = NetPlayerBridge::StateIntent(victim);
        Check(Runtime::HasFlag(captured.Buttons, IntentButtons::SpectatingState)
            && !Runtime::HasFlag(captured.Buttons, IntentButtons::InPlayState)
            && !Runtime::HasFlag(captured.Buttons, IntentButtons::Shoot)
            && !Runtime::HasFlag(captured.Buttons, IntentButtons::Morph),
            "SpectatorIntentDoesNotReportCombatOrHeldActions");

        // The authority normalizes contradictory state bits and advances the
        // press cursor without replaying the old shooting/morph edges.
        IntentPacket contradictory = Intent(victim, NetSession::NetFrame() + 1, true, true);
        contradictory.Buttons |= IntentButtons::SpectatingState | IntentButtons::Morph;
        (*contradictory.Presses)[0] = static_cast<std::uint32_t>(IntentButtons::Shoot);
        NetPlayerBridge::ApplyIntent(victim, contradictory);
        Check(victim.ModIsSpectating() && !victim.ModIsInPlay()
            && !victim.Controls().Shoot().IsDown() && !victim.Controls().Shoot().IsPressed()
            && !victim.Controls().Morph().IsDown() && !victim.Controls().Morph().IsPressed(),
            "ContradictorySpectatorIntentCannotDriveGameplay");

        IntentPacket rejoin = Intent(victim, NetSession::NetFrame() + 2, true, false);
        (*rejoin.Presses)[0] = static_cast<std::uint32_t>(IntentButtons::Shoot);
        NetPlayerBridge::ApplyIntent(victim, rejoin);
        Check(!victim.ModIsSpectating() && victim.ModIsInPlay()
            && !victim.Controls().Shoot().IsPressed(),
            "RejoinDropsPressHistoryAccumulatedWhileSpectating");

        victim.SetHealth(99);
        const std::int32_t deathsBefore = Runtime::ManagedAt(GameState::Deaths(), 1);
        const Entities::DamageFlags lethal = Entities::DamageFlags::Death
            | Entities::DamageFlags::IgnoreInvuln | Entities::DamageFlags::NoDmgInvuln;
        victim.ModSetSpectating(true);
        Check(NetDamage::Suppress(victim, &shooter, lethal),
            "SpectatorNetDamagePolicyRefusesDamageBeforeReplayOrOfflineShortcuts");
        victim.TakeDamage(99U, lethal, std::nullopt, &shooter);
        const HitClaimPacket claim = Claim(0, NetSession::NetFrame() - 1);
        Check(NetHitClaimsFixture::Judge(0, claim) == HitVerdictPacket::ResultDeadVictim,
            "SpectatorClaimIsRefusedByAuthorityEligibilityCheck");
        Receive(0, claim);
        Check(victim.Health() == 99
            && Runtime::ManagedAt(GameState::Deaths(), 1) == deathsBefore
            && NetHitClaims::AppliedHere() == 0,
            "SpectatorDamageAndAuthorityClaimCannotChangeHealthOrDeaths");

        victim.ModSetSpectating(false);
        victim.TakeDamage(1U, Entities::DamageFlags::IgnoreInvuln
            | Entities::DamageFlags::NoDmgInvuln, std::nullopt, nullptr);
        Check(victim.ModIsInPlay() && victim.Health() < 99,
            "RejoinedPlayerCanReceiveDamageAgain");

        PrepareClaims();
        PlayerEntity& claimVictim = *PlayerAt(1);
        const HitClaimPacket pending = Claim(0, NetSession::NetFrame() - 1);
        Receive(0, pending);
        claimVictim.ModSetSpectating(true);
        for (std::int32_t i = 0; i < NetHitClaims::MaxGraceFrames + 2; i++)
        {
            NetSession::Update(NetSession::NetFrame() / 60.0);
            NetHitClaims::Tick();
        }
        Check(claimVictim.ModIsSpectating() && claimVictim.Health() == 1
            && NetHitClaims::AppliedHere() == 0,
            "ClaimAcceptedBeforeSpectatorTransitionIsRecheckedBeforeDamage");
        PrepareClaims();
    }

    void NetCombatCheck::SpectatorSnapshotSurvivesSpawn()
    {
        constexpr std::int32_t slot = 2;
        PlayerEntity& player = *PlayerAt(slot);
        const std::uint16_t generation = NetPlayerLifecycle::Generation(slot);
        const std::uint16_t lifeId = NetPlayerLifecycle::Get(slot);
        Check(generation != 0 && lifeId != 0, "spectator snapshot test has an assigned identity");

        PlayerState spectator = PlayerStateSnapshotCapture::Capture(player, slot, generation, lifeId);
        spectator.Flags = static_cast<std::uint8_t>(spectator.Flags
            | PlayerState::FlagActive | PlayerState::FlagSpawned | PlayerState::FlagSpectating);
        spectator.Health = static_cast<std::uint16_t>(std::max(player.Health(), 1));
        spectator.Deaths = static_cast<std::uint16_t>(Runtime::ManagedAt(GameState::Deaths(), slot));
        const std::int32_t health = spectator.Health;
        const std::int32_t deaths = spectator.Deaths;

        NetPlayerBridge::ForgetSlot(slot);
        NetPlayerBridge::ApplyState(player, spectator, false);
        Check(player.ModIsSpectating() && !player.ModIsInPlay()
            && player.Health() == health && Runtime::ManagedAt(GameState::Deaths(), slot) == deaths,
            "first spectator snapshot remains authoritative after Spawn");
        Check(::MphRead::TestFlag(player.Flags2(), Entities::PlayerFlags2::NoShotsFired),
            "spectator snapshot preserves Spawn's NoShotsFired initialization");

        PlayerState playing = spectator;
        playing.Flags = static_cast<std::uint8_t>(playing.Flags & ~PlayerState::FlagSpectating);
        NetPlayerBridge::ApplyState(player, playing, false);
        Check(!player.ModIsSpectating() && player.ModIsInPlay(),
            "same-life normal snapshot exits spectator state");
        NetPlayerBridge::ApplyState(player, spectator, false);
        Check(player.ModIsSpectating() && !player.ModIsInPlay(),
            "same-life spectator snapshot enters spectator state");
        NetPlayerBridge::ApplyState(player, playing, false);
        Check(!player.ModIsSpectating() && player.ModIsInPlay(),
            "same-life normal snapshot restores combat eligibility");

        player.SetHealth(0);
        NetPlayerBridge::ForgetSlot(slot);
        NetPlayerBridge::ApplyState(player, spectator, false);
        Check(player.ModIsSpectating() && !player.ModIsInPlay()
            && player.Health() == health && Runtime::ManagedAt(GameState::Deaths(), slot) == deaths,
            "same-life health resynchronization that re-enters Spawn reapplies spectator state");

        const std::uint16_t nextGeneration = NetLifecycleTracker::Next(generation);
        Check(!player.IsBot(), "occupant shot-isolation test uses a remote human slot");
        NetShotEvents::Forget(slot);
        const std::uint32_t oldShotFrame = std::max(1U, NetSession::NetFrame() + 1U);
        NetShotEvents::RecordAcceptedIntent(slot, generation, lifeId, oldShotFrame, true);
        IntentPacket oldOccupantIntent = Intent(player, oldShotFrame, true, false);
        oldOccupantIntent.SlotGeneration = generation;
        oldOccupantIntent.LifeId = lifeId;
        IntentPacket::ShotEvent oldOccupantShot{};
        oldOccupantShot.Sequence = 1;
        oldOccupantShot.Frame = oldShotFrame;
        oldOccupantShot.WeaponId = static_cast<std::uint8_t>(BeamType::PowerBeam);
        oldOccupantIntent.ShotHistory[0] = oldOccupantShot;
        oldOccupantIntent.ShotHistoryLength = 1;
        NetShotEvents::Receive(slot, oldOccupantIntent);
        Check(NetShotEvents::FiringEvent(player).has_value(),
            "the current occupant has an eligible queued shot before a roster change");
        NetPlayerLifecycle::SetOccupant(slot, nextGeneration);
        Check(!NetShotEvents::FiringEvent(player).has_value()
            && NetShotEvents::Stats(slot).Waiting == 0
            && NetShotEvents::Stats(slot).Abandoned == 1,
            "a roster generation change retires the prior occupant's queued shot");
        const bool oldIntentAccepted = NetPlayerLifecycle::AcceptIntent(slot, oldOccupantIntent);
        if (oldIntentAccepted)
        {
            NetShotEvents::Receive(slot, oldOccupantIntent);
        }
        Check(!oldIntentAccepted && NetShotEvents::Stats(slot).Waiting == 0,
            "a late old-generation intent is rejected before its shot history reaches the queue");
        PlayerState replacement = spectator;
        replacement.SlotGeneration = nextGeneration;
        replacement.LifeId = 1;
        Check(NetPlayerLifecycle::AcceptState(replacement, NetSession::NetFrame() + 1),
            "replacement occupant's first spectator snapshot is accepted");
        NetPlayerBridge::ApplyState(player, replacement, false);
        Check(player.ModIsSpectating() && !player.ModIsInPlay()
            && player.Health() == health && Runtime::ManagedAt(GameState::Deaths(), slot) == deaths,
            "new slot generation keeps its first spectator snapshot after Spawn");

        const std::int32_t replacementHealth = player.Health();
        NetPlayerBridge::ApplyState(player, spectator, false);
        Check(player.Health() == replacementHealth && player.ModIsSpectating(),
            "a prior occupant's snapshot cannot overwrite the replacement generation");
    }

    void NetCombatCheck::SpectatorShotHistoryIsGated()
    {
        constexpr std::int32_t slot = 3;
        PlayerEntity& shooter = *PlayerAt(slot);
        Check(!shooter.IsBot(), "shot spectator test uses a remote human slot");
        shooter.SetHealth(std::max(shooter.Health(), 1));
        shooter.ModSetSpectating(false);
        NetShotEvents::Forget(slot);

        const std::uint16_t generation = NetPlayerLifecycle::Generation(slot);
        const std::uint16_t lifeId = NetPlayerLifecycle::Get(slot);
        Check(generation != 0 && lifeId != 0, "shot spectator test has an assigned identity");
        const std::uint32_t firstFrame = std::max(1U, NetSession::NetFrame() + 1U);
        auto shotIntent = [&](std::uint32_t frame, std::uint32_t sequence,
            bool spectating, bool contradictoryInPlay)
        {
            IntentPacket intent = Intent(shooter, frame, !spectating || contradictoryInPlay, false);
            intent.SlotGeneration = generation;
            intent.LifeId = lifeId;
            if (spectating)
            {
                intent.Buttons |= IntentButtons::SpectatingState;
            }
            IntentPacket::ShotEvent event{};
            event.Sequence = sequence;
            event.Frame = frame;
            event.AckFrame = frame;
            event.WeaponId = static_cast<std::uint8_t>(BeamType::PowerBeam);
            event.Origin = shooter.Position;
            event.Direction = UnitZ;
            intent.ShotHistory[0] = event;
            intent.ShotHistoryLength = 1;
            return intent;
        };

        const IntentPacket beforeSpectating = shotIntent(firstFrame, 1, false, false);
        NetShotEvents::Receive(slot, beforeSpectating);
        Check(!NetShotEvents::FiringEvent(shooter).has_value()
            && NetShotEvents::Stats(slot).Waiting == 1,
            "a shot waits until its carrying intent passes the authority acceptance boundary");
        NetPlayerBridge::ApplyIntent(shooter, beforeSpectating);
        const std::optional<IntentPacket::ShotEvent> firstAcceptedShot = NetShotEvents::FiringEvent(shooter);
        Check(firstAcceptedShot.has_value() && firstAcceptedShot->Sequence == 1,
            "a normally accepted shot becomes eligible for the remote copy");
        NetShotEvents::Fired(shooter, NetShotEvents::FiredShot{BeamType::PowerBeam, false, {}, {}});

        const IntentPacket entersSpectating = shotIntent(firstFrame + 10, 3, true, true);
        NetShotEvents::Receive(slot, entersSpectating);
        Check(!NetShotEvents::FiringEvent(shooter).has_value()
            && NetShotEvents::Stats(slot).Waiting == 1
            && NetShotEvents::Stats(slot).Ineligible == 0,
            "a new shot cannot fire ahead of its unaccepted spectator transition");
        NetPlayerBridge::ApplyIntent(shooter, entersSpectating);
        Check(shooter.ModIsSpectating() && !shooter.ModIsInPlay(),
            "authority normalizes contradictory spectator and in-play intent bits");

        Check(!NetShotEvents::FiringEvent(shooter).has_value()
            && NetShotEvents::Stats(slot).Ineligible == 1,
            "a shot reported at the spectator boundary is rejected exactly once");

        // Sequence 2 was fired before the boundary but its intent arrived
        // behind sequence 3. The skipped-sequence recovery must retain it.
        const IntentPacket delayedBeforeSpectating = shotIntent(firstFrame + 5, 2, false, false);
        NetShotEvents::Receive(slot, delayedBeforeSpectating);
        const std::optional<IntentPacket::ShotEvent> delayed = NetShotEvents::FiringEvent(shooter);
        Check(delayed.has_value() && delayed->Sequence == 2
            && NetShotEvents::Stats(slot).Recovered == 1,
            "an out-of-order shot fired before the accepted spectator boundary remains eligible");
        NetShotEvents::Fired(shooter, NetShotEvents::FiredShot{BeamType::PowerBeam, false, {}, {}});

        NetShotEvents::Receive(slot, entersSpectating);
        Check(!NetShotEvents::FiringEvent(shooter).has_value()
            && NetShotEvents::Stats(slot).Ineligible == 1,
            "duplicate spectator shot history cannot enqueue or reject the same event twice");

        const IntentPacket stalePlaying = shotIntent(firstFrame, 1, false, false);
        NetShotEvents::Receive(slot, stalePlaying);
        Check(shooter.ModIsSpectating() && !NetShotEvents::FiringEvent(shooter).has_value(),
            "an older playing intent can repeat a shot but cannot roll back accepted spectator state");

        shooter.ModArmWeapon(BeamType::ShockCoil);
        Check(!NetShotEvents::MayFire(shooter),
            "continuous weapons cannot fire while the accepted remote player state is spectating");

        const IntentPacket rejoin = shotIntent(firstFrame + 20, 4, false, false);
        NetShotEvents::Receive(slot, rejoin);
        Check(!NetShotEvents::FiringEvent(shooter).has_value()
            && NetShotEvents::Stats(slot).Waiting == 1,
            "a rejoin shot waits for authority acceptance of the in-play transition");
        NetPlayerBridge::ApplyIntent(shooter, rejoin);
        const std::optional<IntentPacket::ShotEvent> rejoinedShot = NetShotEvents::FiringEvent(shooter);
        Check(!shooter.ModIsSpectating() && shooter.ModIsInPlay()
            && rejoinedShot.has_value() && rejoinedShot->Sequence == 4
            && NetShotEvents::MayFire(shooter),
            "accepted rejoin restores the normal event and continuous-fire paths");
        NetShotEvents::Fired(shooter, NetShotEvents::FiredShot{BeamType::PowerBeam, false, {}, {}});

        IntentPacket notInPlay = shotIntent(firstFrame + 30, 5, false, false);
        notInPlay.Buttons = static_cast<IntentButtons>(
            static_cast<std::uint32_t>(notInPlay.Buttons)
            & ~static_cast<std::uint32_t>(IntentButtons::InPlayState));
        NetShotEvents::Receive(slot, notInPlay);
        Check(!NetShotEvents::FiringEvent(shooter).has_value()
            && NetShotEvents::Stats(slot).Waiting == 1,
            "a shot waits while the authority has not accepted its out-of-play state");
        NetPlayerBridge::ApplyIntent(shooter, notInPlay);
        Check(shooter.ModIsInPlay() && !NetShotEvents::MayFire(shooter)
            && !NetShotEvents::FiringEvent(shooter).has_value(),
            "continuous and event shots honor the accepted in-play bit as well as entity state");
        NetShotEvents::Forget(slot);
    }

    void NetCombatCheck::LocalSpectatorRequestSurvivesSnapshots()
    {
        constexpr std::int32_t slot = 0;
        SpectatorCheckRestore restore{};
        PlayerEntity& player = *PlayerAt(slot);
        SpectatorMode::Reset();
        NetSessionFixture::SetRole(NetRole::Client);
        NetSessionFixture::SetLocalSlot(slot);
        NetSessionFixture::RevokeAuthority();

        player.ModApplyReplicatedSpectating(false);
        player.Controls().Shoot().SetIsDown(true);
        NetPlayerBridge::ForgetSlot(slot);
        const std::uint16_t generation = NetPlayerLifecycle::Generation(slot);
        const std::uint16_t lifeId = NetPlayerLifecycle::Get(slot);
        PlayerState normal = PlayerStateSnapshotCapture::Capture(player, slot, generation, lifeId);
        normal.Flags = static_cast<std::uint8_t>(normal.Flags | PlayerState::FlagActive | PlayerState::FlagSpawned);
        normal.Flags = static_cast<std::uint8_t>(normal.Flags & ~PlayerState::FlagSpectating);
        normal.Health = static_cast<std::uint16_t>(std::max(player.Health(), 1));
        NetPlayerBridge::ApplyState(player, normal, true);

        SpectatorMode::Start(false);
        Check(SpectatorMode::IsSpectating() && !player.ModIsSpectating(),
            "local spectator request is immediate without changing authority-owned entity state");
        player.Controls().Shoot().SetIsDown(true);
        NetPlayerBridge::ApplyState(player, normal, true);
        IntentPacket spectatorIntent = NetPlayerBridge::StateIntent(player);
        Check(SpectatorMode::IsSpectating()
            && player.Controls().Shoot().IsDown()
            && Runtime::HasFlag(spectatorIntent.Buttons, IntentButtons::SpectatingState)
            && !Runtime::HasFlag(spectatorIntent.Buttons, IntentButtons::InPlayState)
            && !Runtime::HasFlag(spectatorIntent.Buttons, IntentButtons::Shoot),
            "a delayed normal snapshot cannot cancel local spectator view or its masked intent");

        PlayerState authoritativeSpectator = normal;
        authoritativeSpectator.Flags |= PlayerState::FlagSpectating;
        NetPlayerBridge::ApplyState(player, authoritativeSpectator, true);
        SpectatorMode::Rejoin();
        Check(!SpectatorMode::IsSpectating() && player.ModIsSpectating(),
            "local rejoin request stays distinct from the last accepted spectator snapshot");
        player.Controls().Shoot().SetIsDown(true);
        NetPlayerBridge::ApplyState(player, authoritativeSpectator, true);
        const IntentPacket rejoinIntent = NetPlayerBridge::StateIntent(player);
        Check(!SpectatorMode::IsSpectating()
            && player.Controls().Shoot().IsDown()
            && !Runtime::HasFlag(rejoinIntent.Buttons, IntentButtons::SpectatingState)
            && Runtime::HasFlag(rejoinIntent.Buttons, IntentButtons::Shoot)
            && Runtime::HasFlag(rejoinIntent.Buttons, IntentButtons::InPlayState),
            "a delayed spectator snapshot cannot cancel local rejoin intent");

        NetPlayerBridge::ApplyState(player, normal, true);
        Check(!SpectatorMode::IsSpectating() && !player.ModIsSpectating(),
            "authority-confirmed rejoin updates the local entity and view");
    }

    void NetCombatCheck::SpectatorCameraRequiresTarget(Scene& scene)
    {
        constexpr std::int32_t localSlot = 0;
        SpectatorCheckRestore restore{&scene};
        SpectatorMode::Reset();
        NetSessionFixture::SetRole(NetRole::Client);
        NetSessionFixture::SetLocalSlot(localSlot);
        NetSessionFixture::RevokeAuthority();
        Check(PlayerEntity::Players().size() > 2 && PlayerAt(1) != nullptr && PlayerAt(2) != nullptr,
            "camera integration test has two remote players");
        for (std::int32_t slot = 2; slot < static_cast<std::int32_t>(PlayerEntity::Players().size()); ++slot)
        {
            const std::shared_ptr<PlayerEntity> player = PlayerAt(slot);
            if (player != nullptr)
            {
                player->ModSetSpectating(true);
            }
        }
        PlayerAt(1)->SetHealth(std::max(PlayerAt(1)->Health(), 1));
        PlayerAt(1)->ModSetSpectating(false);

        SpectatorMode::Start(true);
        scene.ApplySpectatorCameraRequest();
        Check(SpectatorMode::IsSpectating() && PlayerEntity::MainPlayerIndex() == 1
            && scene.CameraMode() == CameraMode::Player,
            "spectator camera follows a selected live player");
        scene.SetFreeCamera(true);
        Check(scene.IsFreeCam(), "free camera can start from the current spectator target pose");

        PlayerAt(1)->SetHealth(0);
        SpectatorMode::Update();
        scene.ApplySpectatorCameraRequest();
        Check(!SpectatorMode::CanUsePlayerCamera()
            && scene.CameraMode() == CameraMode::Roam && scene.IsFreeCam(),
            "target loss moves to a usable free camera");
        scene.ToggleFreeCamera();
        Check(scene.CameraMode() == CameraMode::Roam && scene.IsFreeCam()
            && scene.AllowCameraMovement(),
            "direct free-camera toggle cannot return to Player mode without a valid target");

        PlayerAt(2)->SetHealth(std::max(PlayerAt(2)->Health(), 1));
        PlayerAt(2)->ModSetSpectating(false);
        SpectatorMode::CycleNext();
        scene.ApplySpectatorCameraRequest();
        Check(PlayerEntity::MainPlayerIndex() == 2 && SpectatorMode::CanUsePlayerCamera()
            && scene.CameraMode() == CameraMode::Player,
            "a new live entrant can become the spectator camera target");
    }

    void NetCombatCheck::MutualKillOrdering()
    {
        for (const std::int32_t earlier : {-1, 0, 1})
        {
            for (const bool reverse : {false, true})
            {
                for (const std::int32_t arrivalGap : {0, 1, 8})
                {
                    PrepareClaims();
                    const std::uint32_t world = NetSession::NetFrame() - 1;
                    const HitClaimPacket a = Claim(0, earlier == 0 ? world - 1 : world);
                    const HitClaimPacket b = Claim(1, earlier == 1 ? world - 1 : world);
                    if (reverse)
                    {
                        Receive(1, b);
                    }
                    else
                    {
                        Receive(0, a);
                    }
                    for (std::int32_t i = 0; i < arrivalGap; i++)
                    {
                        NetSession::Update(NetSession::NetFrame() / 60.0);
                        NetHitClaims::Tick();
                    }
                    if (reverse)
                    {
                        Receive(0, a);
                    }
                    else
                    {
                        Receive(1, b);
                    }
                    for (std::int32_t i = 0; i < NetHitClaims::MaxGraceFrames + 2; i++)
                    {
                        NetSession::Update(NetSession::NetFrame() / 60.0);
                        NetHitClaims::Tick();
                    }
                    const bool aDead = PlayerAt(0)->Health() == 0;
                    const bool bDead = PlayerAt(1)->Health() == 0;
                    const auto text = [](bool value) { return value ? std::string("True") : std::string("False"); };
                    // Shooter-favouring arbitration: two shots fired before
                    // either player died are a trade, whichever world each was
                    // aimed in and whichever claim arrived first.
                    Check(aDead && bDead,
                        "MutualKillOrdering/earlier=" + std::to_string(earlier) + " reversed=" + text(reverse)
                        + " arrivalGap=" + std::to_string(arrivalGap) + " ADead=" + text(aDead) + " BDead=" + text(bDead));
                }
            }
        }
        // The one shot that is late: fired once its shooter was already dead.
        PrepareClaims();
        Receive(0, Claim(0, NetSession::NetFrame() - 1));
        for (std::int32_t i = 0; i < NetHitClaims::MaxGraceFrames + 2 && PlayerAt(1)->Health() > 0; i++)
        {
            NetSession::Update(NetSession::NetFrame() / 60.0);
            NetHitClaims::Tick();
        }
        HitClaimPacket late = Claim(1, NetSession::NetFrame());
        late.ClaimId = 2;
        Receive(1, late);
        for (std::int32_t i = 0; i < NetHitClaims::MaxGraceFrames + 2; i++)
        {
            NetSession::Update(NetSession::NetFrame() / 60.0);
            NetHitClaims::Tick();
        }
        Check(PlayerAt(1)->Health() == 0 && PlayerAt(0)->Health() > 0,
            "MutualKillOrdering: a shot fired after its shooter's death does not count");
        PrepareClaims();
    }

    void NetCombatCheck::InvulnerableClaimIsRefused()
    {
        PrepareClaims();
        PlayerEntity& victim = *PlayerAt(1);
        victim._spawnInvulnTimer = 1000;
        const HitClaimPacket claim = Claim(0, NetSession::NetFrame() - 1);
        Receive(0, claim);
        for (std::int32_t i = 0; i < NetHitClaims::MaxGraceFrames + 2; i++)
        {
            NetSession::Update(NetSession::NetFrame() / 60.0);
            NetHitClaims::Tick();
        }
        Check(victim.Health() == 1 && NetHitClaims::AppliedHere() == 0
            && !NetHitClaims::AlreadyRescued(0, 1, claim.LaunchFrame),
            "invulnerable target cannot be reported or prepaid as rescued damage");
        const auto bucket = static_cast<std::size_t>(NetShotDiagnostics::Bucket(BeamType::Imperialist));
        const std::int64_t refused = NetShotDiagnostics::Read().Refusals[bucket];
        const std::int64_t declared = NetShotDiagnostics::Read().Claims[bucket];
        Receive(0, claim);
        NetHitClaims::Tick();
        Check(NetShotDiagnostics::Read().Refusals[bucket] == refused
            && NetShotDiagnostics::Read().Claims[bucket] == declared,
            "repeated claim does not duplicate per-weapon outcome counters");
        PrepareClaims();
    }

    void NetCombatCheck::DuplicateKillIsHeldToAllowedDamage()
    {
        PrepareClaims();
        PlayerEntity& victim = *PlayerAt(1);
        victim.SetHealth(99);
        const std::uint32_t world = NetSession::NetFrame() - 1;
        // The authority resolved this very shot itself, short of a kill.
        NetHitClaimsFixture::NoteLedger(0, 1, world, world, 8, false, BeamType::Imperialist);
        HitClaimPacket claim = Claim(0, world);
        claim.Flags = static_cast<std::uint8_t>(claim.Flags | HitClaimPacket::FlagLethal);
        // Inflated, but under the coarse ceiling every claim is checked against
        // on arrival: only the settled damage can stop it.
        claim.Damage = static_cast<std::uint16_t>(NetHitClaimsFixture::MaxDamageFor(0, claim.Beam) - 1);
        Check(claim.Damage >= victim.Health(), "DuplicateKill precondition: the claimed damage would kill");
        const std::int64_t duplicates = NetHitClaims::DuplicateHere();
        const std::int64_t capped = NetHitClaims::CappedHere();
        Receive(0, claim);
        for (std::int32_t i = 0; i < NetHitClaims::MaxGraceFrames + 2; i++)
        {
            NetSession::Update(NetSession::NetFrame() / 60.0);
            NetHitClaims::Tick();
        }
        Check(NetHitClaims::DuplicateHere() == duplicates + 1, "DuplicateKill: the claim matched the authority's own hit");
        Check(NetHitClaims::CappedHere() == capped + 1, "DuplicateKill: its damage was held to what the shot could do");
        Check(victim.Health() == 99, "DuplicateKill: an inflated lethal claim did not finish the victim (health "
            + std::to_string(victim.Health()) + ")");
        PrepareClaims();
    }

    void NetCombatCheck::ClaimArbitrationHasDeadline()
    {
        PrepareClaims();
        PlayerEntity& victim = *PlayerAt(1);
        victim._spawnInvulnTimer = 1000;
        const std::uint32_t firstWorld = NetSession::NetFrame() - 1;
        std::uint8_t verdict = 255;
        const NetHitClaims::VerdictWriter previousSink = NetHitClaims::VerdictSink();
        NetHitClaims::VerdictSink([&verdict](std::int32_t slot, std::span<const std::pair<std::uint16_t, std::uint8_t>> entries)
        {
            for (const auto& entry : entries)
            {
                if (slot == 0 && entry.first == 1)
                {
                    verdict = entry.second;
                }
            }
        });
        try
        {
            Receive(0, Claim(0, firstWorld));
            for (std::uint32_t tick = 1; tick <= 2 * NetHitClaims::MaxGraceFrames + 1; tick++)
            {
                NetSession::Update(NetSession::NetFrame() / 60.0);
                if (tick % 8 == 0)
                {
                    NetUnlagged::Record(NetSession::NetFrame());
                    HitClaimPacket claim = Claim(0, NetSession::NetFrame());
                    claim.ClaimId = static_cast<std::uint16_t>(tick + 1);
                    claim.LaunchFrame = firstWorld - tick;
                    Receive(0, claim);
                }
                NetHitClaims::Tick();
            }
            Check(verdict == HitVerdictPacket::ResultNoDamage,
                "earlier claim stream cannot starve a verdict past the arbitration deadline");
        }
        catch (...)
        {
            NetHitClaims::VerdictSink(previousSink);
            PrepareClaims();
            throw;
        }
        NetHitClaims::VerdictSink(previousSink);
        PrepareClaims();
    }

    void NetCombatCheck::ContinuousPhaseAgreesAcrossPeers()
    {
        const std::array<std::pair<std::int32_t, std::vector<std::int32_t>>, 2> goldens{{
            {10, {6, 12, 18, 24, 30, 38, 44, 50, 56, 62}},
            {15, {4, 8, 12, 16, 20, 24, 28, 34, 38, 42, 46, 50, 54, 58, 62}}}};
        for (const auto& [damage, phases] : goldens)
        {
            bool exact = true;
            for (std::int32_t phase = 0; phase < 64; phase++)
            {
                const bool listed = std::find(phases.begin(), phases.end(), phase) != phases.end();
                exact &= ContinuousWeaponPhase::Amount(damage, static_cast<std::uint64_t>(phase), true) == (listed ? 1 : 0);
            }
            Check(exact, "continuous golden damage cadence/" + std::to_string(damage));
        }
        for (const std::int32_t damage : {10, 15, 32, 47, 64})
        {
            for (const std::int32_t phaseOffset : {0, 1, 5, 19})
            {
                ContinuousWeaponPhase owner(2);
                ContinuousWeaponPhase authority(2);
                ContinuousWeaponPhase observer(2);
                bool agrees = true;
                for (std::uint32_t tick = 1; tick <= 192; tick++)
                {
                    const std::uint32_t logical = 100 + static_cast<std::uint32_t>(phaseOffset) + tick;
                    bool shared = false;
                    const std::uint64_t a = owner.Resolve(0, tick, true, true, logical, true, 0, 0, shared);
                    const std::uint32_t age = tick % 6;
                    const std::uint64_t b = authority.Resolve(0, tick + 700, true, false, 9000, true, logical - age, age, shared);
                    const std::uint64_t c = observer.Resolve(0, tick + 1300, true, false, 5000, true, logical, 0, shared);
                    agrees &= a == b && b == c
                        && ContinuousWeaponPhase::Amount(damage, a, true) == ContinuousWeaponPhase::Amount(damage, b, true)
                        && ContinuousWeaponPhase::Amount(damage, a, false) == ContinuousWeaponPhase::Amount(damage, c, false);
                }
                Check(agrees, "ContinuousPhaseAgreesAcrossPeers/damage=" + std::to_string(damage)
                    + " initialOffset=" + std::to_string(phaseOffset));
            }
        }
    }
}
