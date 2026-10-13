#include "Mods/Network/Testing/Combat/NetCombatCheck.hpp"

#include "Mods/Network/Gameplay/Claims/Hit/NetHitClaims.hpp"
#include "Mods/Network/Session/NetSession.hpp"
#include "Mods/Network/Gameplay/Weapons/ContinuousWeaponPhase.hpp"
#include "Mods/Network/Gameplay/Combat/NetUnlagged.hpp"
#include "../Entities/BeamProjectileEntity.hpp"
#include "../Entities/Players/PlayerEntity.hpp"
#include "../Mods/Combat/SyluxMuzzleGuard.hpp"
#include "../NativeRuntime/System/Console.hpp"
#include "../NativeRuntime/System/Managed.hpp"
#include "../Scene.hpp"
#include "../Utility/Rng.hpp"
#include "SyluxGuardWeaponFixture.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace MphRead::Mods::Network
{
    void NetCombatCheck::SyluxMuzzleGuardCases(Scene& scene)
    {
        using namespace Entities;
        using namespace OpenTK::Mathematics;
        using Guard = Combat::SyluxMuzzleGuard;
        using Metrics = Combat::SyluxMuzzleGuardMetrics;
        auto shooterPtr = PlayerEntity::Players().at(0);
        auto& shooter = *shooterPtr;
        auto& victim = *PlayerEntity::Players().at(1);
        const auto oldEquip = shooter._equipInfo;
        const auto oldHunter = shooter._hunter;
        const auto oldVictimPosition = static_cast<Vector3>(victim.Position);
        const bool oldEnabled = Guard::Enabled, oldMetrics = Metrics::Enabled;
        auto equip = std::make_shared<EquipInfo>();
        equip->Beams = oldEquip->Beams;
        int ammo = 1000;
        equip->GetAmmo = [&ammo] { return ammo; };
        equip->SetAmmo = [&ammo](int value) { ammo = value; };
        const auto clear = [&]
        {
            auto& beams = *equip->Beams;
            for (int i = 0; i < beams.Length(); ++i)
            {
                auto beam = beams[i];
                beam->Destroy();
                scene.RemoveEntity(beam);
            }
        };
        const auto restore = [&]
        {
            clear();
            shooter._equipInfo = oldEquip;
            shooter._hunter = oldHunter;
            victim.Position = oldVictimPosition;
            Guard::Enabled = oldEnabled;
            Metrics::Enabled = oldMetrics;
            PrepareClaims();
        };
        try
        {
            Guard::Enabled = true;
            Metrics::Enabled = true;
            shooter._equipInfo = equip;
            shooter._hunter = Hunter::Sylux;
            const Vector3 point = static_cast<Vector3>(shooter.Position) + Vector3(0, 1, 0);
            std::optional<Combat::BeamObstacleHit> wall;
            for (Vector3 dir : {Vector3(1, 0, 0), Vector3(-1, 0, 0), Vector3(0, 0, 1),
                Vector3(0, 0, -1), Vector3(0, -1, 0), Vector3(0, 1, 0)})
            {
                wall = Combat::TraceFirstBeamObstacle(point, point + ScaleVector(dir, 100), scene);
                if (wall) break;
            }
            Check(wall.has_value(), "Sylux guard fixture finds an actual room obstacle");
            const Vector3 normal = Normalize(wall->Collision.Plane.Xyz());
            NativeRuntime::ConsoleWriteLine("SYLUX fixture wall normal=" + normal.ToString());
            const Vector3 start = wall->Collision.Position + ScaleVector(normal, Guard::Length / 2);
            const Vector3 end = wall->Collision.Position - ScaleVector(normal, Guard::Length / 2);
            const auto shortHit = Combat::TraceFirstBeamObstacle(start, end, scene);
            Check(shortHit && std::fabs(shortHit->Collision.Distance - 0.5F) < 0.01F,
                "Sylux finite interval selects room surface at its midpoint");
            Check(!Combat::TraceFirstBeamObstacle(start, start + ScaleVector(normal, Guard::Length), scene),
                "Sylux guard ignores a wall behind both endpoints");
            Check(!Combat::TraceFirstBeamObstacle(end, end - ScaleVector(normal, Guard::Length), scene),
                "Sylux guard does not extend back past 0x651");
            Check(!Combat::TraceFirstBeamObstacle(end, start, scene), "map backface remains one-sided");
            const Vector3 tangent = Normalize(Vector3::Cross(normal,
                std::fabs(normal.Y) < 0.9F ? Vector3(0, 1, 0) : Vector3(1, 0, 0)));
            const Vector3 diagonal = ScaleVector(normal, 0.8F) + ScaleVector(tangent, 0.6F);
            const auto diagonalHit = Combat::TraceFirstBeamObstacle(
                wall->Collision.Position + ScaleVector(diagonal, Guard::Length / 2),
                wall->Collision.Position - ScaleVector(diagonal, Guard::Length / 2), scene);
            Check(diagonalHit && std::fabs(diagonalHit->Collision.Distance - 0.5F) < 0.01F,
                "diagonal short interval retains first contact");
            const auto base = Weapons::Current->at(static_cast<int>(BeamType::PowerBeam));
            const auto fixtureFlags = base->Flags | WeaponFlags::CanCharge | WeaponFlags::PartialCharge;
            const auto spawn = [&](std::shared_ptr<WeaponInfo> weapon, int available, int charge,
                std::uint64_t traces, bool success, const char* label, std::optional<Vector3> guard = std::nullopt,
                BeamProjectileEntity* parent = nullptr)
            {
                if (parent == nullptr) clear();
                equip->Weapon = std::move(weapon);
                equip->ChargeLevel = static_cast<std::uint16_t>(charge);
                ammo = available;
                Metrics::Counters = {};
                const auto result = BeamProjectileEntity::Spawn(shooterPtr, equip, end, -normal,
                    BeamSpawnFlags::NoMuzzle, shooter.NodeRef, &scene, parent, guard.value_or(start));
                Check((result != BeamResultFlags::NoSpawn) == success
                    && Metrics::Counters.TraceInvoked == traces, std::string("Sylux ammo/query gate/") + label);
            };
            const auto single = Testing::GuardWeapon(*base, fixtureFlags, 1);
            spawn(single, 9, 0, 0, false, "uncharged insufficient");
            spawn(single, 39, 40, 0, false, "full charge insufficient");
            spawn(single, 29, 30, 0, false, "partial charge interpolated cost 30");
            spawn(single, 0, 0, 0, false, "empty paid shot");
            spawn(single, 10, 0, 1, true, "exact uncharged cost");
            Check(ammo == 0, "exact cost uses existing ammo subtraction");
            spawn(single, 30, 30, 1, true, "exact partial charge cost");
            Check(ammo == 0, "partial charge uses actual interpolated cost");
            spawn(single, -1, 0, 1, true, "negative ammo remains unlimited");
            spawn(single, 100, 0, 0, false, "invalid long interval fails closed", end + normal);
            Check(ammo == 100, "invalid coordinates cannot consume ammo or bypass guard");
            spawn(Testing::GuardWeapon(*base, fixtureFlags, 0), 100, 0, 0, true, "zero projectiles");
            spawn(Testing::GuardWeapon(*base, fixtureFlags, 3), 100, 0, 1, true, "three pellets share trace");
            Check(Metrics::Counters.HitApplied == 3, "pellets each resolve their normal wall collision");
            spawn(single, 100, 0, 1, true, "prepare ricochet parent");
            std::shared_ptr<BeamProjectileEntity> parent;
            auto& pool = *equip->Beams;
            for (int i = 0; i < pool.Length(); ++i)
                if (pool[i]->Owner() == shooterPtr && pool[i]->Lifespan() > 0) { parent = pool[i]; break; }
            Check(parent != nullptr, "ricochet parent is initialized");
            spawn(single, 100, 0, 0, true, "ricochet child never retraces muzzle", start, parent.get());
            shooter._hunter = Hunter::Noxus;
            spawn(single, 100, 0, 0, true, "Noxus never traces");
            shooter._hunter = Hunter::Sylux;
            spawn(Testing::GuardWeapon(*base, fixtureFlags & ~WeaponFlags::SurfaceCollision, 1),
                100, 0, 0, true, "non-surface weapon retains intentional traversal");
            Guard::Enabled = false;
            spawn(single, 100, 0, 0, true, "rollback switch restores open spawn");
            Guard::Enabled = true;

            // Seed real remote firing phases; Spawn remains the only cost gate.
            const auto continuous = Testing::GuardWeapon(*base, fixtureFlags | WeaponFlags::Continuous, 1);
            std::uint32_t frame = NetSession::NetFrame() + 32;
            for (int expectedCost : {0, 1})
            {
                while (ContinuousWeaponPhase::Amount(10, ++frame, false) != expectedCost) {}
                auto intent = Intent(shooter, frame, true, true);
                NetSession::AcceptSlotIntent(0, intent);
                SharedContinuousWeaponPhase().ResetSlot(0);
                spawn(continuous, 0, 0, expectedCost == 0 ? 1 : 0, expectedCost == 0,
                    expectedCost == 0 ? "continuous zero-cost phase with ammo 0" : "continuous paid phase with ammo 0");
            }

            victim.Position = end - ScaleVector(normal, 2.0F);
            victim._spawnInvulnTimer = 0;
            for (int beam = 0; beam < 9; ++beam)
            {
                for (bool charged : {false, true})
                {
                    victim.SetHealth(99);
                    auto weapon = Weapons::Current->at(beam);
                    spawn(weapon, 1000, charged && TestFlag(weapon->Flags, WeaponFlags::CanCharge) ? weapon->FullCharge * 2 : 0,
                        TestFlag(weapon->Flags, WeaponFlags::SurfaceCollision) ? 1 : 0, true,
                        (MphRead::ToString(static_cast<BeamType>(beam)) + (charged ? "/charged" : "/normal")).c_str());
                    Check(victim.Health() == 99, "obstructed spawn cannot damage victim behind impact plane");
                    auto& beams = *equip->Beams;
                    for (int i = 0; i < beams.Length(); ++i)
                    {
                        auto b = beams[i];
                        if (b->Lifespan() <= 0 || b->Owner() != shooterPtr) continue;
                        Check(b->Target() == nullptr, "obstructed spawn cannot acquire homing target");
                        Check(LengthSquared(b->SpawnPosition() - end) < 1e-8F
                            || (LengthSquared(b->SpawnPosition() - shortHit->Collision.Position) < 1e-8F
                                && b->ModLaunchKey() == ShotKey::For(0, NetUnlagged::LaunchFrameFor(shooter))),
                            "physical spawn origin is unchanged or wall ricochet child retains launch identity");
                        (void)b->Process();
                    }
                    Check(victim.Health() == 99, "obstructed beam first process cannot attack behind wall");
                }
            }
            clear();
            victim.Position = start + ScaleVector(normal, 0.5F);
            victim.SetHealth(99);
            // The guard's question is whether the splash still lands, so the
            // authority resolves this shot itself; with shooter-authoritative
            // hits its copy of a human's shot never damages anyone (the
            // shooter's claim does).
            const bool shooterHits = NetHitClaims::ShooterHits();
            NetHitClaims::ShooterHits(false);
            spawn(Weapons::Current->at(static_cast<int>(BeamType::Missile)), 1000, 0, 1, true,
                "front-side splash remains legal");
            NetHitClaims::ShooterHits(shooterHits);
            Check(victim.Health() < 99, "blocked missile preserves actual legal front-side splash damage");
            clear();
            victim.Position = oldVictimPosition;

            // Real TryFireWeapon hooks on every authority slot and on a BOT.
            for (int slot = 0; slot < 4; ++slot)
            {
                auto player = PlayerEntity::Players().at(static_cast<std::size_t>(slot));
                const auto hunter = player->_hunter;
                const bool bot = player->_isBot;
                player->_hunter = Hunter::Sylux;
                player->ModArmWeapon(BeamType::PowerBeam);
                player->ModSetAmmo(400, 50);
                player->_equipInfo->ChargeLevel = 0;
                auto intent = Intent(*player, ++frame, true, true);
                NetSession::AcceptSlotIntent(slot, intent);
                player->_aimVec = -normal;
                player->_gunDrawPos = end + ScaleVector(normal, 0.5F);
                player->_muzzlePos = end;
                player->_aimPosition = end - ScaleVector(normal, 10);
                player->_gunAnimation = GunAnimation::Idle;
                player->_timeSinceShot = 0;
                player->Controls().Shoot().SetIsDown(true);
                player->Controls().Shoot().SetIsPressed(true);
                Metrics::Counters = {};
                Check(!player->TryFireWeapon() && Metrics::Counters.TraceInvoked == 0,
                    "Sylux cooldown returns before query/slot " + std::to_string(slot));
                player->_timeSinceShot = 1000;
                player->_gunAnimation = GunAnimation::UpDown;
                Check(!player->TryFireWeapon() && Metrics::Counters.TraceInvoked == 0,
                    "Sylux lowered gun returns before query/slot " + std::to_string(slot));
                player->_gunAnimation = GunAnimation::Idle;
                Check(player->TryFireWeapon() && Metrics::Counters.TraceInvoked == 1 && Metrics::Counters.HitApplied == 1,
                    "Sylux actual authority TryFireWeapon blocks/slot " + std::to_string(slot));
                Check(LengthSquared(player->_muzzlePos - end) == 0
                    && LengthSquared(player->_gunDrawPos - (end + ScaleVector(normal, 0.5F))) == 0,
                    "gun model and muzzle effect positions stay unchanged/slot " + std::to_string(slot));
                if (slot == 0)
                {
                    player->_isBot = true;
                    player->_timeSinceShot = 1000;
                    Metrics::Counters = {};
                    Check(player->TryFireWeapon() && Metrics::Counters.TraceInvoked == 1 && Metrics::Counters.HitApplied == 1,
                        "Sylux BOT shares the production muzzle policy");
                }
                player->_hunter = hunter;
                player->_isBot = bot;
            }
            clear();
            equip->Weapon = single;
            const Vector3 openOrigin = start + ScaleVector(normal, Guard::Length * 2);
            Metrics::Counters = {};
            ammo = 100;
            Check(BeamProjectileEntity::Spawn(shooterPtr, equip, openOrigin, -normal, BeamSpawnFlags::NoMuzzle,
                shooter.NodeRef, &scene, nullptr, openOrigin + ScaleVector(normal, Guard::Length)) != BeamResultFlags::NoSpawn
                && Metrics::Counters.TraceInvoked == 1 && Metrics::Counters.TraceNoHit == 1,
                "reused blocked projectile pool returns to the ordinary open shot path");
            clear();

            for (bool recordAfterClaim : {false, true})
            {
                PrepareClaims();
                const auto claim = Claim(0, NetSession::NetFrame() - 1);
                const auto key = ShotKey::For(0, claim.LaunchFrame);
                if (!recordAfterClaim) NetHitClaims::NoteMuzzleObstruction(key, BeamType::Imperialist, *shortHit);
                Receive(0, claim);
                if (recordAfterClaim) NetHitClaims::NoteMuzzleObstruction(key, BeamType::Imperialist, *shortHit);
                for (int tick = 0; tick < NetHitClaims::MaxGraceFrames + 2; ++tick)
                {
                    NetSession::Update(NetSession::NetFrame() / 60.0);
                    NetHitClaims::Tick();
                }
                Check(victim.Health() == 1 && NetHitClaims::AppliedHere() == 0 && NetHitClaims::RefusedHere() == 1,
                    "wall claim cannot resurrect through delayed ApplyOne before/after record");
                Receive(0, claim);
                NetHitClaims::Tick();
                Check(victim.Health() == 1 && NetHitClaims::RefusedHere() == 1, "reordered/repeated refused wall claim stays refused");
            }
            PrepareClaims();
            auto claim = Claim(0, NetSession::NetFrame() - 1);
            const auto key = ShotKey::For(0, claim.LaunchFrame);
            NetHitClaims::NoteMuzzleObstruction(key, BeamType::Imperialist, *shortHit);
            NetHitClaims::NoteAuthorityHit(0, 1, claim.LaunchFrame, 1, BeamType::Imperialist, key);
            Receive(0, claim);
            NetHitClaims::Tick();
            Check(NetHitClaims::DuplicateHere() == 1 && NetHitClaims::RefusedHere() == 0,
                "legal authority splash or ricochet ledger settles obstructed claim");

            PrepareClaims();
            claim = Claim(0, NetSession::NetFrame() - 1);
            NetHitClaims::NoteMuzzleObstruction(ShotKey::For(0, claim.LaunchFrame), BeamType::Imperialist, *shortHit);
            NetHitClaims::NoteAuthorityHit(0, 1, claim.LaunchFrame + 1, 1, BeamType::Imperialist);
            Receive(0, claim);
            for (int tick = 0; tick < NetHitClaims::MaxGraceFrames + 2; ++tick)
            {
                NetSession::Update(NetSession::NetFrame() / 60.0);
                NetHitClaims::Tick();
            }
            Check(NetHitClaims::DuplicateHere() == 0 && NetHitClaims::RefusedHere() == 1,
                "blocked claim cannot borrow the ledger of a nearby open launch");

            // Measure the actual short trace in this loaded room, with logging held constant.
            Metrics::Counters = {};
            for (int i = 0; i < 2048; ++i) (void)Metrics::Trace(start, end, scene);
            auto samples = Metrics::Counters.TraceNanoseconds;
            std::sort(samples.begin(), samples.end());
            NativeRuntime::ConsoleWriteLine("SYLUX TRACE ns p50=" + std::to_string(samples[1024])
                + " p95=" + std::to_string(samples[1945]) + " p99=" + std::to_string(samples[2027])
                + " queries=" + std::to_string(Metrics::Counters.TraceInvoked));

            // Equal open-shot workloads with diagnostics disabled in both arms.
            // Pool cleanup is outside the timed Spawn call; rendering is not measured.
            Metrics::Enabled = false;
            equip->Weapon = Weapons::Current->at(static_cast<int>(BeamType::PowerBeam));
            equip->ChargeLevel = 0;
            const auto rng1 = Rng::Rng1(), rng2 = Rng::Rng2();
            std::uint32_t baselineRng1 = 0, baselineRng2 = 0;
            Vector3 baselinePosition{}, baselineVelocity{};
            for (bool enabled : {false, true})
            {
                Guard::Enabled = enabled;
                Rng::SetRng1(rng1); Rng::SetRng2(rng2);
                std::array<std::int64_t, 1024> spawnSamples{};
                for (int i = -64; i < static_cast<int>(spawnSamples.size()); ++i)
                {
                    clear();
                    ammo = 1000;
                    equip->SmokeLevel = 0;
                    const auto before = std::chrono::steady_clock::now();
                    const auto result = BeamProjectileEntity::Spawn(shooterPtr, equip, openOrigin, -normal,
                        BeamSpawnFlags::NoMuzzle, shooter.NodeRef, &scene, nullptr,
                        openOrigin + ScaleVector(normal, Guard::Length));
                    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::steady_clock::now() - before).count();
                    if (result == BeamResultFlags::NoSpawn) throw std::runtime_error("profile shot did not spawn");
                    if (i >= 0) spawnSamples[static_cast<std::size_t>(i)] = elapsed;
                }
                std::shared_ptr<BeamProjectileEntity> liveBeam;
                for (int i = 0; i < pool.Length(); ++i)
                    if (pool[i]->Owner() == shooterPtr && pool[i]->Lifespan() > 0) { liveBeam = pool[i]; break; }
                Check(liveBeam != nullptr, "profile open shot retains a live projectile");
                if (!enabled)
                {
                    baselineRng1 = Rng::Rng1(); baselineRng2 = Rng::Rng2();
                    baselinePosition = liveBeam->SpawnPosition(); baselineVelocity = liveBeam->Velocity();
                }
                else
                    Check(Rng::Rng1() == baselineRng1 && Rng::Rng2() == baselineRng2
                        && LengthSquared(liveBeam->SpawnPosition() - baselinePosition) == 0
                        && LengthSquared(liveBeam->Velocity() - baselineVelocity) == 0,
                        "open shot OFF/ON preserves RNG state origin and velocity");
                std::int64_t total = 0;
                for (auto ns : spawnSamples) total += ns;
                std::sort(spawnSamples.begin(), spawnSamples.end());
                int entityCount = 0;
                auto entities = scene.Entities().GetEnumerator();
                while (entities.MoveNext()) ++entityCount;
                NativeRuntime::ConsoleWriteLine(std::string("SYLUX SPAWN ") + (enabled ? "ON" : "OFF")
                    + " ns mean=" + std::to_string(total / spawnSamples.size())
                    + " p50=" + std::to_string(spawnSamples[512]) + " p95=" + std::to_string(spawnSamples[972])
                    + " p99=" + std::to_string(spawnSamples[1013]) + " samples=1024 shots/frame=1 entities="
                    + std::to_string(entityCount) + " headless=1");
            }
            Rng::SetRng1(rng1); Rng::SetRng2(rng2);
            restore();
        }
        catch (...) { restore(); throw; }
    }
}
