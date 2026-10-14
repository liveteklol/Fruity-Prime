#include "Mods/Network/Testing/Combat/DialancheCombatCheck.hpp"
#include "Mods/Network/Gameplay/Claims/Hit/NetHitClaims.hpp"

#include "Mods/Network/Gameplay/Combat/NetDamage.hpp"
#include "Mods/Network/Gameplay/Player/NetPlayerLifecycle.hpp"
#include "Mods/Network/Gameplay/Player/NetPlayerBridge.hpp"
#include "Mods/Network/Session/NetSession.hpp"
#include "Mods/Network/Testing/NetSessionFixture.hpp"
#include "Mods/Network/Testing/ServerSimFixture.hpp"
#include "Mods/Network/Server/Match/ServerSim.hpp"
#include "Entities/DoorEntity.hpp"
#include "Entities/EnemyInstanceEntity.hpp"
#include "Entities/Players/HalfturretEntity.hpp"
#include "Entities/Players/PlayerEntity.hpp"
#include "GameState.hpp"
#include "Scene.hpp"
#include "Sound/Sfx.hpp"
#include "NativeRuntime/System/Console.hpp"
#include "NativeRuntime/System/ExceptionText.hpp"

#include <array>
#include <chrono>
#include <exception>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

namespace MphRead::Mods::Network
{
    namespace Runtime = ::MphRead::NativeRuntime;
    using namespace Entities;
    using OpenTK::Mathematics::Vector3;

    namespace
    {
        class RecordingSound final : public Sound::SfxInstanceBase
        {
        public:
            int Hits = 0;
            void Count(int id) { if (id == static_cast<int>(SfxId::SPIRE_ALT_ATTACK_HIT)) ++Hits; }
            void PlayDgn(int id, Sound::SoundSource*, bool, bool, float, bool, float, float) override { Count(id); }
            void PlayScript(int id, Sound::SoundSource*, bool, float, bool, bool) override { Count(id); }
            int PlaySample(int id, Sound::SoundSource*, std::optional<bool>, bool, float, bool, bool) override
            { Count(id); return -1; }
        };

        // Count the existing Enemy reaction while retaining its real TakeDamage arithmetic.
        class TargetEnemy final : public EnemyInstanceEntity
        {
        public:
            TargetEnemy(Scene* scene, Vector3 position)
                : EnemyInstanceEntity({EnemyType::WarWasp, nullptr}, {}, scene)
            { _health = 100; _hurtVolume = CollisionVolume(position, 0.45F); }
            int Hits = 0;
        protected:
            bool EnemyTakeDamage(EntityBase*) override { ++Hits; return false; }
        };

        PlayerState State(PlayerEntity& player)
        {
            PlayerState state;
            state.SlotIndex = static_cast<std::uint8_t>(player.SlotIndex());
            state.SlotGeneration = NetPlayerLifecycle::Generation(player.SlotIndex());
            state.LifeId = NetPlayerLifecycle::Get(player.SlotIndex());
            state.Health = static_cast<std::uint16_t>(player.Health());
            NetDamage::Write(player.SlotIndex(), state);
            return state;
        }
    }

    std::int32_t DialancheCombatCheck::Run(const std::string& room, std::int32_t peerA, std::int32_t peerB)
    {
        // This check drives the authority's own Dialanche collision, which is
        // what -servershots and bot attackers still use: with shooter-
        // authoritative hits the attacking player's machine would claim it.
        NetHitClaims::ShooterHits(false);
        std::string reason;
        if (!ServerSim::Available(reason)) { Runtime::ConsoleWriteLine("DIALANCHE FAIL " + reason); return 1; }
        ServerSim sim;
        std::vector<std::uint8_t> snapshot;
        if (!sim.Start(room, GameMode::Battle, 3, [&snapshot](std::span<const std::uint8_t> bytes)
            { snapshot.assign(bytes.begin(), bytes.end()); }, [] {}, MakeNetSessionServerSimulationPort())) return 1;
        const auto originalSound = Sound::Sfx::Instance();
        const auto originalSave = GameState::StorySave;
        int result = 1, checks = 0;
        const auto check = [&checks](bool ok, const std::string& name)
        {
            if (!ok) throw std::runtime_error(name);
            ++checks;
            Runtime::ConsoleWriteLine("DIALANCHE PASS " + name);
        };
        try
        {
            MatchStatePacket match;
            match.MatchId = 1; match.AuthorityEpoch = 1; match.RoomKey = room;
            match.Mode = static_cast<std::uint8_t>(GameMode::Battle);
            NetSession::ApplyMatchState(match, false);
            auto roster = RosterPacket::Create();
            roster.MatchId = 1; roster.AuthorityEpoch = 1; roster.Revision = 1; roster.Count = 3;
            const std::array hunters{Hunter::Spire, Hunter::Weavel, Hunter::Samus};
            for (std::uint8_t slot = 0; slot < 3; ++slot)
            {
                (*roster.Slots)[slot] = slot; (*roster.Generations)[slot] = 1;
                (*roster.Hunters)[slot] = static_cast<std::uint8_t>(hunters[slot]);
                (*roster.Names)[slot] = "DIALANCHE" + std::to_string(slot);
            }
            NetSession::ApplyRoster(roster);
            for (int i = 0; i < 120; ++i) sim.Step();
            auto& attacker = *PlayerEntity::Players()[0];
            auto& weavel = *PlayerEntity::Players()[1];
            auto& victim = *PlayerEntity::Players()[2];
            auto& scene = ServerSimFixture::RequireScene(sim);
            check(attacker.ModIsInPlay() && weavel.ModIsInPlay() && victim.ModIsInPlay()
                && sim.StepFailures() == 0, "headless authority spawns Spire/Weavel/Samus");
            auto sound = std::make_shared<RecordingSound>();
            Sound::Sfx::_instance = sound;
            const Vector3 contact(10, 10, 10), far(50, 10, 10);
            const auto reset = [&]
            {
                GameState::Mode(GameMode::Battle);
                attacker.SetIsBot(false); attacker._doubleDmgTimer = 0;
                attacker._flags2 |= PlayerFlags2::AltAttack;
                attacker.Position = Vector3(0, 10, 10);
                attacker._volume = CollisionVolume(attacker.Position, 0.45F);
                attacker._dialancheNativeCollision.Reset(contact);
                // Continuous rocks intentionally disagree: every target must use the saved sample.
                attacker._spireRockPosL = attacker._spireRockPosR = far;
                for (auto* player : {&weavel, &victim})
                {
                    player->SetHealth(100); player->_spawnInvulnTimer = 0;
                    player->_damageInvulnTimer = 100; // NoDmgInvuln must still bypass this.
                    player->_flags2 &= ~(PlayerFlags2::Halfturret | PlayerFlags2::AltAttack);
                    player->Position = contact;
                    player->_volume = CollisionVolume(contact, 0.45F);
                }
                weavel._volume = CollisionVolume(far, 0.45F);
                NetDamage::Reset(); sound->Hits = 0;
            };
            const auto collide = [&](std::uint64_t frame) { scene._frameCount = frame; attacker.CheckPlayerCollision(); };
            reset();
            std::unique_ptr<NetTransport> wire;
            if (peerA > 0 && peerB > 0) wire = std::make_unique<NetTransport>(0);
            const auto publish = [&](std::uint32_t frame)
            {
                if (!wire) return;
                NetSessionFixture::SetFrame(frame);
                NetSession::BroadcastSnapshot(scene); // The production snapshot serializer and damage history.
                for (int repeat = 0; repeat < 4; ++repeat)
                {
                    for (const auto port : {peerA, peerB}) wire->Send(System::Net::IPEndPoint::Loopback(port), PacketType::Snapshot, snapshot);
                    std::this_thread::sleep_for(std::chrono::milliseconds(25));
                }
            };
            publish(200);
            const auto baseline = State(victim);
            collide(201); check(victim.Health() == 100 && sound->Hits == 0, "odd sibling skips damage and SFX");
            collide(202); check(victim.Health() == 92 && NetDamageMetrics::Read().Resolved[2] == 1 && sound->Hits == 1,
                "two substeps: 100->92 / one event / left+right OR / saved geometry");
            const auto first = State(victim);
            publish(202);
            collide(203); collide(204);
            check(victim.Health() == 84 && NetDamageMetrics::Read().Resolved[2] == 2 && sound->Hits == 2,
                "next native interval: 92->84 / one additional event and SFX");
            const auto second = State(victim);
            publish(204);
            for (int replica = 0; replica < 2; ++replica)
            {
                victim.SetHealth(100); NetDamageMetrics::ResetReplayCount(2); NetDamage::BeginLife(2, baseline);
                for (const auto& published : {first, first, second, second})
                {
                    std::array<std::uint8_t, PlayerState::Size> bytes{};
                    published.Write(bytes);
                    NetDamage::Replay(victim, PlayerState::Read(bytes));
                }
                check(victim.Health() == 84 && NetDamageMetrics::Read().Replayed[2] == 2,
                    "serialized client replica " + std::to_string(replica) + ": HP84/events2; duplicate packets ignored");
            }
            reset();
            victim._volume = CollisionVolume(contact, 0.45F); collide(205);
            victim._volume = CollisionVolume(far, 0.45F); collide(206);
            check(victim.Health() == 100 && sound->Hits == 0, "contact only on nonnative sibling is ignored");
            victim._volume = CollisionVolume(contact, 0.45F); collide(208);
            check(victim.Health() == 92, "contact on native tick lands");
            reset();
            weavel._volume = CollisionVolume(contact, 0.45F); collide(210);
            check(weavel.Health() == 92 && victim.Health() == 92 && sound->Hits == 2,
                "multiple targets each receive one hit in same tick");
            reset(); attacker._doubleDmgTimer = 120; collide(212);
            check(victim.Health() == 84 && NetDamageMetrics::Read().Resolved[2] == 1, "Double Damage stays 16 through TakeDamage");
            for (int level = 0; level < 3; ++level)
            {
                reset(); GameState::Mode(GameMode::SinglePlayer); attacker.SetIsBot(true);
                attacker.SetBotLevel(level); GameState::EncounterState()[0] = 0;
                collide(213); collide(214);
                const int damage = level == 0 ? 2 : level == 1 ? 3 : 5;
                check(victim.Health() == 100 - damage && sound->Hits == 1,
                    "story bot damage " + std::to_string(damage) + " once per native tick");
            }
            reset();
            weavel._volume = CollisionVolume(contact, 0.45F);
            victim._volume = CollisionVolume(far, 0.45F);
            weavel._flags2 |= PlayerFlags2::Halfturret;
            weavel._halfturret->Position = contact; weavel._halfturret->SetHealth(100);
            collide(215); weavel.CheckPlayerCollision();
            collide(216); weavel.CheckPlayerCollision();
            check(weavel.Health() == 88 && weavel._halfturret->Health() == 96
                && NetDamageMetrics::Read().Resolved[1] == 2 && sound->Hits == 2,
                "body + Halfturret separate channels: body8, turret split4+4");
            reset(); victim._volume = CollisionVolume(far, 0.45F);
            weavel._halfturret->Position = contact + Vector3(0.949F, 0, 0); weavel._halfturret->SetHealth(100);
            scene._frameCount = 218;
            PlayerEntity::CheckAltAttackHit1(&attacker, &weavel, true);
            check(weavel._halfturret->Health() == 96, "nominal turret radius 0.45 inside boundary");
            weavel._halfturret->Position = contact + Vector3(0.951F, 0, 0);
            PlayerEntity::CheckAltAttackHit1(&attacker, &weavel, true);
            check(weavel._halfturret->Health() == 96, "nominal turret radius 0.45 outside boundary");
            reset();
            TargetEnemy enemy(&scene, contact);
            scene._frameCount = 219; check(!attacker.CheckAltAttackHitEnemy1(&enemy), "Enemy skips odd sibling");
            scene._frameCount = 220; check(attacker.CheckAltAttackHitEnemy1(&enemy), "Enemy extension hits sampled rocks");
            scene._frameCount = 221; (void)attacker.CheckAltAttackHitEnemy1(&enemy);
            check(enemy.Health() == 92 && enemy.Hits == 1 && sound->Hits == 1, "Enemy interval: one damage and dedicated SFX");
            scene._frameCount = 222; (void)attacker.CheckAltAttackHitEnemy1(&enemy);
            check(enemy.Health() == 84 && enemy.Hits == 2 && sound->Hits == 2, "Enemy next native tick hits again");
            // Same tick source-before/source-after and target-before/target-after all see the previous rocks.
            reset(); TargetEnemy orderedEnemy(&scene, contact);
            weavel._flags2 |= PlayerFlags2::Halfturret;
            weavel._halfturret->Position = contact; weavel._halfturret->SetHealth(100);
            attacker._dialancheNativeCollision.Record(110, contact, contact);
            scene._frameCount = 222;
            attacker.CheckPlayerCollision();
            attacker._dialancheNativeCollision.Record(111, far, far);
            PlayerEntity::CheckAltAttackHit1(&attacker, &weavel, true);
            (void)attacker.CheckAltAttackHitEnemy1(&orderedEnemy);
            check(victim.Health() == 92 && orderedEnemy.Health() == 92 && weavel._halfturret->Health() == 96,
                "Player before / Halfturret+Enemy after current pose record see same older pose");
            GameState::Mode(GameMode::SinglePlayer);
            GameState::StorySave = std::make_shared<::MphRead::StorySave>();
            DoorEntityData data(EntityDataHeader(static_cast<std::uint16_t>(EntityType::Door), 0,
                contact, Vector3(0, 1, 0), Vector3(0, 0, 1)), std::nullopt,
                8, DoorType::Standard, 255, 0, 1, 0, 0, std::nullopt, std::nullopt);
            DoorEntity door(data, "", &scene);
            door.SetFlags(door.Flags() | DoorFlags::Locked);
            sound->Hits = 0;
            // Door uses the same history as Player/Halfturret/Enemy above, including the hidden current sample.
            scene._frameCount = 221; attacker.AltAttackHitDoor(&door);
            check(!TestFlag(door.Flags(), DoorFlags::ShotOpen) && sound->Hits == 0, "Door skips odd sibling");
            scene._frameCount = 222; attacker.AltAttackHitDoor(&door);
            check(TestFlag(door.Flags(), DoorFlags::ShotOpen) && TestFlag(door.Flags(), DoorFlags::Unlocked)
                && sound->Hits == 1, "Door sampled pose: ShotOpen / palette8 Unlock / dedicated SFX");
            door.SetFlags(door.Flags() & ~DoorFlags::ShotOpen);
            scene._frameCount = 223; attacker.AltAttackHitDoor(&door);
            check(!TestFlag(door.Flags(), DoorFlags::ShotOpen) && sound->Hits == 1, "Door sibling cannot repeat reaction");
            attacker._dialancheNativeCollision.Record(112, contact, contact);
            scene._frameCount = 224; attacker.AltAttackHitDoor(&door);
            check(!TestFlag(door.Flags(), DoorFlags::ShotOpen) && sound->Hits == 1,
                "Door next native tick sees older far sample, not current contact sample");
            scene._frameCount = 226; attacker.AltAttackHitDoor(&door);
            check(TestFlag(door.Flags(), DoorFlags::ShotOpen) && sound->Hits == 2,
                "Door following native tick exposes contact sample and reacts once");
            attacker._altAttackCooldown = 0; attacker.EndAltAttack();
            check(!TestFlag(attacker.Flags2(), PlayerFlags2::AltAttack) && attacker._altAttackCooldown == 0,
                "Spire EndAltAttack clears flag without cooldown");
            check(NetConfig::ProtocolVersion == 19, "network protocol is 19");
            Runtime::ConsoleWriteLine("DIALANCHE PASS " + std::to_string(checks) + " production assertions | EU1.1 0200B55C/0200B808");
            result = 0;
        }
        catch (...) { Runtime::ConsoleErrorWriteLine("DIALANCHE FAIL " + Runtime::ExceptionToString(std::current_exception())); }
        GameState::Mode(GameMode::Battle);
        Sound::Sfx::_instance = originalSound;
        GameState::StorySave = originalSave;
        sim.Stop();
        return result;
    }

    std::int32_t DialancheCombatCheck::RunPeer(const std::string& room, std::int32_t port)
    {
        ServerSim sim;
        if (!sim.Start(room, GameMode::Battle, 3, [](std::span<const std::uint8_t>) {}, [] {},
                MakeNetSessionServerSimulationPort())) return 1;
        int result = 1;
        try
        {
            MatchStatePacket match;
            match.MatchId = 1; match.AuthorityEpoch = 1; match.RoomKey = room;
            match.Mode = static_cast<std::uint8_t>(GameMode::Battle);
            auto roster = RosterPacket::Create();
            roster.MatchId = 1; roster.AuthorityEpoch = 1; roster.Revision = 1; roster.Count = 3;
            const std::array hunters{Hunter::Spire, Hunter::Weavel, Hunter::Samus};
            for (std::uint8_t slot = 0; slot < 3; ++slot)
            {
                (*roster.Slots)[slot] = slot; (*roster.Generations)[slot] = 1;
                (*roster.Hunters)[slot] = static_cast<std::uint8_t>(hunters[slot]);
                (*roster.Names)[slot] = "DIALANCHE" + std::to_string(slot);
            }
            NetSession::ApplyMatchState(match, false); NetSession::ApplyRoster(roster);
            for (int i = 0; i < 120; ++i) sim.Step();
            NetSession::StartPlayback(); // Feed captured real UDP snapshots through the existing client packet path.
            NetSession::ApplyMatchState(match, false); NetSession::ApplyRoster(roster);
            NetTransport wire(port);
            Runtime::ConsoleWriteLine("DIALANCHE PEER ready UDP " + std::to_string(wire.LocalPort()));
            auto& victim = *PlayerEntity::Players()[2];
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
            bool sawBaseline = false, sawFirst = false;
            while (std::chrono::steady_clock::now() < deadline)
            {
                for (const auto packet : wire.Drain())
                {
                    if (packet.Type() != PacketType::Snapshot) continue;
                    NetSession::InjectPlaybackPacket(packet.Data, packet.Length);
                    NetSession::Pump();
                    if (!NetSession::HasRemoteState(victim.SlotIndex())) continue;
                    NetPlayerBridge::ApplyState(victim,
                        NetSession::RemoteState(static_cast<std::size_t>(victim.SlotIndex())), false);
                    victim._spawnInvulnTimer = 0;
                    const auto frame = NetSession::LastSnapshotFrame();
                    if (frame == 200) sawBaseline |= victim.Health() == 100 && NetDamageMetrics::Read().Replayed[2] == 0;
                    if (frame == 202) sawFirst |= victim.Health() == 92 && NetDamageMetrics::Read().Replayed[2] == 1;
                    if (frame == 204)
                    {
                        const bool ok = sawBaseline && sawFirst && victim.Health() == 84
                            && NetDamageMetrics::Read().Replayed[2] == 2 && NetSession::SnapshotsReceived() == 3;
                        Runtime::ConsoleWriteLine(std::string("DIALANCHE PEER ") + (ok ? "PASS" : "FAIL")
                            + " HP=" + std::to_string(victim.Health()) + " events=" + std::to_string(NetDamageMetrics::Read().Replayed[2])
                            + " snapshots=" + std::to_string(NetSession::SnapshotsReceived()));
                        result = ok ? 0 : 1;
                        break;
                    }
                }
                if (NetSession::LastSnapshotFrame() == 204) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            wire.Dispose();
        }
        catch (...) { Runtime::ConsoleErrorWriteLine("DIALANCHE PEER FAIL " + Runtime::ExceptionToString(std::current_exception())); }
        sim.Stop();
        return result;
    }
}
