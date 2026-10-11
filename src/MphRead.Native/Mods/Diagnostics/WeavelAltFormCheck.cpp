#include "WeavelAltFormCheck.hpp"
#include "../Network/ServerSim.hpp"
#include "../Network/NetPlayerBridge.hpp"
#include "../Network/NetPlayerLifecycle.hpp"
#include "../Network/NetHealthSync.hpp"
#include "../Network/NetMatchTimeSync.hpp"
#include "../Network/NetDamage.hpp"
#include "../Network/DedicatedServer.hpp"
#include "../Network/NetTransport.hpp"
#include "../Network/MapRotation.hpp"
#include "../../Entities/Players/HalfturretEntity.hpp"
#include "../../Entities/ItemSpawnEntity.hpp"
#include "../../GameState.hpp"
#include "../../Scene.hpp"
#include "../../NativeRuntime/System/Console.hpp"
#include "../../NativeRuntime/System/ExceptionText.hpp"
#include "../../NativeRuntime/System/BinaryPrimitives.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#include <chrono>
#include <thread>

namespace MphRead::Mods::Diagnostics
{
    using namespace Network;
    using namespace Entities;
    using OpenTK::Mathematics::Vector3;
    namespace Runtime = NativeRuntime;

    namespace
    {
        bool LegacyProtocolRefused(const std::string& room)
        {
            // Exercise the real UDP transport and DedicatedServer handshake.
            // Hosted mode keeps this protocol check independent of gameplay.
            auto server = std::make_shared<DedicatedServer>(0, 8,
                MapRotation::SingleMatch(room, GameMode::Battle, 0, 0));
            server->RunsTheMatch(false);
            std::exception_ptr failure;
            std::jthread worker([server, &failure](std::stop_token stop)
            {
                try { server->Run(stop); }
                catch (...) { failure = std::current_exception(); }
            });
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (!server->Listening() && std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            bool refused = false;
            if (server->Listening())
            {
                NetTransport client(0);
                std::array<std::uint8_t, 22> hello{};
                hello[0] = 16;
                hello[1] = 255;
                hello[2] = 1;
                const auto endpoint = System::Net::IPEndPoint::Loopback(server->BoundPort());
                client.Send(endpoint, PacketType::Hello, hello);
                while (!refused && std::chrono::steady_clock::now() < deadline)
                {
                    for (const auto packet : client.Drain())
                    {
                        if (packet.Type() == PacketType::Refused && packet.Payload().size() == RefusedPacket::Size)
                            refused = RefusedPacket::Read(packet.Payload()).Reason == RefusedPacket::ReasonProtocol;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
                client.Dispose();
            }
            worker.request_stop();
            worker.join();
            if (failure) std::rethrow_exception(failure);
            return refused;
        }
    }

    std::int32_t WeavelAltFormCheck::Run(const std::string& room)
    {
        ServerSim sim;
        std::vector<std::uint8_t> snapshot;
        if (!sim.Start(room, GameMode::Battle, 8, [&snapshot](std::span<const std::uint8_t> bytes)
            { snapshot.assign(bytes.begin(), bytes.end()); }, [] {})) return 1;
        const auto originalSave = GameState::StorySave;
        int checks = 0, result = 1;
        const auto check = [&checks](bool ok, const std::string& name)
        {
            if (!ok) throw std::runtime_error(name);
            ++checks;
            Runtime::ConsoleWriteLine("WEAVEL PASS " + name);
        };
        try
        {
            check(LegacyProtocolRefused(room), "old16 refused by real UDP DedicatedServer (ReasonProtocol)");
            MatchStatePacket match;
            match.MatchId = 1; match.AuthorityEpoch = 1; match.RoomKey = room;
            match.Mode = static_cast<std::uint8_t>(GameMode::Battle);
            NetSession::ApplyMatchState(match, false);
            auto roster = RosterPacket::Create();
            roster.MatchId = 1; roster.AuthorityEpoch = 1; roster.Revision = 1; roster.Count = 3;
            for (std::uint8_t i = 0; i < 8; ++i)
            {
                (*roster.Slots)[i] = i; (*roster.Generations)[i] = 1;
                (*roster.Hunters)[i] = static_cast<std::uint8_t>(i == 0 ? Hunter::Weavel : Hunter::Samus);
                (*roster.Names)[i] = "WEAVEL" + std::to_string(i);
            }
            NetSession::ApplyRoster(roster);
            for (int i = 0; i < 120; ++i) sim.Step();
            auto& owner = *PlayerEntity::Players()[0];
            auto& victim = *PlayerEntity::Players()[1];
            auto& scene = *sim._scene;
            auto turret = owner.Halfturret();
            check(owner.Hunter() == Hunter::Weavel && owner.ModIsInPlay() && sim.StepFailures() == 0,
                "asset-backed authority spawns Weavel");
            owner.SetIsBot(false);
            const auto fresh = [&](int health)
            {
                GameState::Mode(GameMode::Battle);
                owner.ModForceWeavelState(false, false);
                owner.SetHealth(health); owner._spawnInvulnTimer = owner._damageInvulnTimer = 0;
                owner._frozenTimer = 0; owner._altAttackCooldown = 0;
                owner.Controls().ClearAll();
                owner._flags1 |= PlayerFlags1::Standing;
                owner.ModForceWeavelState(true, true);
            };
            fresh(100);
            const Vector3 initialTurretPosition = turret->Position;
            check(owner.Health() == 50 && turret->Health() == 50, "100 splits owner50 / turret50");
            const Vector3 altPosition = owner.Position;
            owner.ModForceForm(true); owner.ModForceForm(true);
            check(owner.Health() == 50 && turret->Health() == 50
                && OpenTK::Mathematics::Equal(owner.Position, altPosition), "duplicate force Alt preserves HP and position");
            owner.ModForceWeavelState(false, false);
            check(owner.Health() == 100 && !owner.IsAltForm(), "living turret merges once on forced biped");
            owner.ModForceForm(false);
            check(owner.Health() == 100, "duplicate biped cannot merge twice");
            owner.EnterAltForm();
            check(owner.IsMorphing() && !owner.IsAltForm() && owner.Health() == 50, "morph transition splits before Alt form finalization");
            owner.ModForceForm(true);
            check(owner.IsAltForm() && owner.Health() == 50 && turret->Health() == 50, "force completes morph without splitting again");
            owner.ExitAltForm();
            check(owner.Health() == 100 && owner.IsUnmorphing(), "normal exit merges living HP exactly once");
            const Vector3 exitingPosition = owner.Position;
            owner.ModForceForm(false);
            check(owner.Health() == 100 && !owner.IsUnmorphing()
                && OpenTK::Mathematics::Equal(owner.Position, exitingPosition), "force finalizes unmorph without another merge or centre shift");
            owner.EnterAltForm(); owner.ModForceForm(false);
            check(owner.Health() == 100 && !owner.IsMorphing(), "cancelled morph restores living turret HP once");
            owner.ModForceWeavelState(true, false); owner.ModForceForm(true);
            check(owner.IsAltForm() && owner.Health() == 100 && turret->Health() == 0, "explicit dead Alt restoration does not create or split a turret");
            fresh(101);
            check(owner.Health() == 51 && turret->Health() == 50, "101 splits owner51 / turret50");
            turret->Die(); owner.ModForceForm(true);
            check(owner.IsAltForm() && turret->Health() == 0 && owner.Health() == 51,
                "dead turret remains dead when Alt is forced again");
            owner.ModForceForm(false);
            check(owner.Health() == 51, "dead turret contributes no HP on exit");

            fresh(101);
            owner.SetTimeSinceShot(80);
            turret->OnTakeDamage(PlayerEntity::Players()[1], 100);
            turret->OnFrozen(); turret->OnSetOnFire();
            const auto burnEffect = turret->_burnEffect;
            check(burnEffect != nullptr, "asset-backed burn effect exists before re-entry");
            turret->_ySpeed = -0.7F;
            turret->EquipInfo()->UnchargedDamage(3); turret->EquipInfo()->DmgDirTypes = {0, 0};
            owner.ExitAltForm(); owner.EnterAltForm(); owner.ModForceForm(true);
            check(turret->Target() == nullptr && turret->CooldownFactorRaw() == 6144
                && turret->_freezeTimer == 0 && turret->_burnTimer == 0 && turret->_burnEffect == nullptr
                && turret->_ySpeed == 0 && turret->_targetTimer == 0, "real exit/re-enter clears previous life state");
            check(turret->EquipInfo()->UnchargedDamage() == turret->EquipInfo()->Weapon->UnchargedDamage
                && turret->EquipInfo()->DmgDirTypes == std::array<std::uint8_t, 2>{255, 255},
                "real re-entry restores default weapon overrides");
            check(owner.NativeTimeSinceShot() == 40, "Alt re-entry preserves existing shot timer");

            // Real ProcessInput consumes edges after the production timer advance.
            const auto input = [&](std::uint64_t frame, bool pressed, bool held)
            {
                scene._frameCount = frame;
                owner._prevPosition = owner.Position;
                owner._prevSpeed = owner.Speed();
                owner._volume = CollisionVolume::Move(owner._volumeUnxf, static_cast<Vector3>(owner.Position));
                owner.Controls().AltAttack().SetIsPressed(pressed);
                owner.Controls().AltAttack().SetIsDown(held);
                owner.AdvanceNativeWeaponTimers(); owner.ProcessInput();
            };
            owner._flags1 &= ~PlayerFlags1::Standing;
            owner._altAttackCooldown = 1; input(200, true, true);
            check(TypeExtensions::TestFlag(owner.Flags2(), PlayerFlags2::AltAttack), "native cooldown1 plus press starts lunge");
            owner.EndAltAttack(); owner._altAttackCooldown = 2; input(202, true, true);
            check(!TypeExtensions::TestFlag(owner.Flags2(), PlayerFlags2::AltAttack), "native cooldown2 plus press rejects lunge");
            input(204, false, true);
            check(owner._altAttackCooldown == 0 && !TypeExtensions::TestFlag(owner.Flags2(), PlayerFlags2::AltAttack),
                "rejected edge is consumed; holding cannot retry at cooldown0");
            input(205, true, true);
            check(!TypeExtensions::TestFlag(owner.Flags2(), PlayerFlags2::AltAttack), "odd input cannot launch immediately");
            input(206, false, true);
            check(TypeExtensions::TestFlag(owner.Flags2(), PlayerFlags2::AltAttack), "odd press survives until next native decision");
            owner.EndAltAttack();
            check(owner._altAttackCooldown == owner.Values().AltAttackCooldown, "Weavel cooldown uses native metadata units");
            owner._altAttackCooldown = 0; input(207, true, true); input(208, false, true);
            check(TypeExtensions::TestFlag(owner.Flags2(), PlayerFlags2::AltAttack), "lunge is active before forced biped");
            owner.ModForceForm(false);
            check(!TypeExtensions::TestFlag(owner.Flags2(), PlayerFlags2::AltAttack), "biped finalization ends active lunge");
            fresh(100); owner._flags1 &= ~PlayerFlags1::Standing;
            GameState::Mode(GameMode::SinglePlayer); owner.SetIsBot(true); GameState::EncounterState()[0] = 1;
            owner.Position = Vector3(0, 100, 0);
            owner.SetSpeed(Vector3{}); input(210, true, true);
            check(TypeExtensions::TestFlag(owner.Flags2(), PlayerFlags2::AltAttack)
                && std::fabs(owner.Speed().Y - (Fixed::ToFloat(1843) + owner._gravity / 2)) < 0.000001F,
                "Story native lunge plus 60Hz movement gravity: Y=" + std::to_string(owner.Speed().Y)
                    + " gravity=" + std::to_string(owner._gravity) + " flags=" + std::to_string(static_cast<unsigned>(owner.Flags2())));
            check(std::fabs(std::hypot(owner.Speed().X, owner.Speed().Z) - Fixed::ToFloat(1228) * 0.98F) < 0.000001F,
                "Story horizontal fixed constant1228 survives normal 60Hz speed damping");
            owner.EndAltAttack(); check(owner._altAttackCooldown == 10, "Story encounter1 cooldown is ten native ticks");
            GameState::Mode(GameMode::Battle); owner.SetIsBot(false);

            fresh(100); owner._flags1 &= ~PlayerFlags1::Standing;
            IntentPacket intent;
            intent.SlotGeneration = NetPlayerLifecycle::Generation(0); intent.LifeId = NetPlayerLifecycle::Get(0);
            intent.Frame = 210; intent.Aim = owner.FacingVector();
            intent.Buttons = IntentButtons::InPlayState | IntentButtons::AltFormState;
            intent.Presses = std::make_shared<std::vector<std::uint32_t>>(IntentPacket::PressHistory);
            NetPlayerBridge::ApplyIntent(owner, intent); // establish the press-history baseline
            intent.Frame = 211; intent.Buttons |= IntentButtons::AltAttack;
            (*intent.Presses)[0] = static_cast<std::uint32_t>(IntentButtons::AltAttack);
            NetPlayerBridge::ApplyIntent(owner, intent);
            input(211, owner.Controls().AltAttack().IsPressed(), owner.Controls().AltAttack().IsDown());
            intent.Frame = 212; (*intent.Presses)[1] = (*intent.Presses)[0]; (*intent.Presses)[0] = 0;
            NetPlayerBridge::ApplyIntent(owner, intent);
            input(212, owner.Controls().AltAttack().IsPressed(), owner.Controls().AltAttack().IsDown());
            check(TypeExtensions::TestFlag(owner.Flags2(), PlayerFlags2::AltAttack), "remote press history reaches the common native input latch");

            fresh(100);
            owner.SetTimeSinceShot(0); owner._altAttackCooldown = 2; scene._frameCount = 214;
            check(turret->Process(), "turret processes before player in native phase");
            owner.AdvanceNativeWeaponTimers();
            check(owner.NativeTimeSinceShot() == 1 && owner._altAttackCooldown == 1,
                "turret/player order advances shared native clocks only once");
            owner.SetTimeSinceShot(0); scene._frameCount = 215; owner.AdvanceNativeWeaponTimers();
            check(owner.NativeTimeSinceShot() == 0, "shot timer does not advance on odd sibling");
            scene._frameCount = 216; owner.AdvanceNativeWeaponTimers();
            check(owner.NativeTimeSinceShot() == 1, "shot timer advances once per native tick");
            owner._nativeTimeSinceShot = 255; owner.AdvanceNativeWeaponTimers();
            check(owner.NativeTimeSinceShot() == 255, "shot timer saturates at u8 maximum");
            for (std::size_t i = 1; i < PlayerEntity::Players().size(); ++i) PlayerEntity::Players()[i]->SetHealth(0);
            turret->_target.reset(); turret->_cooldownFactorRaw = 2867; turret->_freezeTimer = 15;
            const auto process = [&](std::uint64_t frame) { scene._frameCount = frame; check(turret->Process(), "turret remains active"); };
            const auto frozen = turret->_freezeTimer; process(217);
            check(turret->_freezeTimer == frozen && turret->CooldownFactorRaw() == 2867, "odd sibling leaves native state untouched");
            for (int i = 0; i < 15; ++i) process(218 + i * 2);
            check(turret->CooldownFactorRaw() == 2867 && turret->_freezeTimer == 0, "freeze pauses factor recovery for all fifteen native ticks");
            for (int i = 0; i < 53; ++i) process(248 + i * 2);
            check(turret->CooldownFactorRaw() == 6100, "production Process recovery at native tick53");
            process(354); check(turret->CooldownFactorRaw() == 6144, "production Process recovery at native tick54");
            turret->Position = Vector3(0, 100, 0); turret->_grounded = false; turret->_ySpeed = 0;
            process(355); check(turret->Position.Y == 100, "odd step has no gravity or position integration");
            process(356); check(std::fabs(turret->Position.Y - 99.98F) < 0.00001F && turret->_ySpeed == -0.02F,
                "native step integrates gravity and position once");
            process(358); check(std::fabs(turret->Position.Y - 99.94F) < 0.00002F, "second native physics step uses accumulated velocity");
            turret->_grounded = true;
            turret->_burnTimer = 150; turret->_freezeTimer = 0; owner.SetHealth(100); turret->SetHealth(50);
            for (int i = 0; i < 5; ++i) process(360 + i * 2);
            check(owner.Health() == 100 && turret->Health() == 50, "burn has no damage before timer multiple of eight");
            process(370); check(turret->_burnTimer == 144 && (owner.Health() < 100 || turret->Health() < 50),
                "burn applies real proxy damage at native timer144");
            for (int i = 6; i < 150; ++i) process(360 + i * 2);
            check(turret->_burnTimer == 0 && owner.Health() + turret->Health() == 131,
                "150 native burn ticks cause exactly19 damage ticks at multiples of8");
            turret->Position = initialTurretPosition + Vector3(0, 3, 0); turret->_grounded = false; turret->_ySpeed = 0;
            for (int i = 0; i < 120 && !turret->Grounded(); ++i) process(700 + i * 2);
            check(turret->Grounded() && turret->_ySpeed == 0, "native sphere sweep lands the turret and clears vertical velocity");

            // Scene::UpdateScene expires HUD messages before processing entities.
            // A one-frame energy message must be renewed on both 60 Hz siblings.
            fresh(100); turret->_grounded = true; turret->_cooldownFactorRaw = 2867;
            check(PlayerEntity::Main().get() == &owner && scene.FrameTime() > 0.001F,
                "HUD fixture uses the main player and real simulation frame duration");
            for (const auto& message : owner._hudMessageQueue) message->Lifetime = 0;
            const auto energyMessages = [&]()
            {
                return std::count_if(owner._hudMessageQueue.begin(), owner._hudMessageQueue.end(),
                    [](const auto& message) { return message->Lifetime > 0 && message->Position.Y == 150; });
            };
            for (std::uint64_t frame = 901; frame <= 908; ++frame)
            {
                owner.ProcessHudMessageQueue();
                check(energyMessages() == 0, "previous one-frame energy HUD expires before entity processing");
                const auto factor = turret->CooldownFactorRaw();
                process(frame);
                check(energyMessages() == 1, "energy HUD stays visible on every native and nonnative sibling");
                check(turret->CooldownFactorRaw() == factor + (frame % 2 == 0 ? 61 : 0),
                    "60Hz HUD refresh keeps gameplay recovery at30Hz");
            }
            turret->SetHealth(49); owner.ProcessHudMessageQueue(); process(909);
            check(std::any_of(owner._hudMessageQueue.begin(), owner._hudMessageQueue.end(),
                [](const auto& message) { return message->Lifetime > 0 && message->Position.Y == 150
                    && std::u16string(message->Text.data()).find(u"49") != std::u16string::npos
                    && message->Category == 0 && message->DialogHide; }),
                "odd sibling refreshes current energy without blink category or dialog changes");
            PlayerEntity::SetMainPlayerIndex(1);
            owner.ProcessHudMessageQueue(); process(911);
            check(energyMessages() == 0, "another player's turret does not publish to the local HUD");
            PlayerEntity::SetMainPlayerIndex(0);
            turret->Die(); owner.ProcessHudMessageQueue(); scene._frameCount = 913;
            check(!turret->Process() && energyMessages() == 0, "dead turret stops refreshing energy HUD");

            fresh(100); victim.SetHealth(100); victim.Position = static_cast<Vector3>(turret->Position) + Vector3(5, 0, 0);
            victim._curAlpha = 1; victim.SetTeamIndex(-1); owner.SetTeamIndex(-1);
            turret->OnTakeDamage(PlayerEntity::Players()[1], 0);
            turret->_grounded = true; owner._nativeTimeSinceShot = 255;
            GameState::Mode(GameMode::SinglePlayer); owner.SetIsBot(true); GameState::EncounterState()[0] = 1;
            process(400); check(turret->_cooldownTimer == 65 && owner.NativeTimeSinceShot() == 255, "Story gate begins at65 without firing");
            for (int i = 0; i < 5; ++i) process(402 + i * 2);
            check(turret->_cooldownTimer == 60 && owner.NativeTimeSinceShot() == 255, "Story gate at60 still rejects fire");
            process(412); check(turret->_cooldownTimer == 59 && owner.NativeTimeSinceShot() == 0
                && turret->EquipInfo()->UnchargedDamage() == 3, "Story gate below60 spawns and resets shared native shot timer");
            owner._nativeTimeSinceShot = 255; process(413);
            check(owner.NativeTimeSinceShot() == 255 && turret->_cooldownTimer == 59,
                "odd sibling cannot fire even with an eligible target and ready shot timer");
            GameState::Mode(GameMode::Battle); owner.SetIsBot(false);

            fresh(101); turret->ApplyNetworkState(50, Vector3(2, 20, 3), true); NetDamage::Reset();
            const auto publish = [&](std::uint32_t frame)
            {
                NetSession::_netFrame = frame; NetSession::BroadcastSnapshot();
                check(!snapshot.empty() && snapshot.size() + 1 <= NetConfig::MaxPacketSize, "production snapshot fits bounded datagram");
                return snapshot;
            };
            const auto readOwner = [&](const std::vector<std::uint8_t>& bytes)
            {
                const auto header = SnapshotHeader::Read(bytes);
                for (int i = 0; i < header.PlayerCount; ++i)
                {
                    const auto state = PlayerState::Read(std::span(bytes).subspan(SnapshotHeader::Size + i * PlayerState::Size));
                    if (state.SlotIndex == 0) return state;
                }
                throw std::runtime_error("Weavel missing from production snapshot");
            };
            auto activeBytes = publish(500); auto active = readOwner(activeBytes);
            check(active.HalfturretHealth == 50 && active.WeavelFlags == 3 && active.Health == 51
                && OpenTK::Mathematics::Equal(active.HalfturretPosition, Vector3(2, 20, 3)), "publisher sends explicit active turret HP/position/grounded");
            turret->Die(); auto deadBytes = publish(501); auto dead = readOwner(deadBytes);
            check((dead.Flags & PlayerState::FlagAltForm) != 0 && dead.WeavelFlags == 0, "publisher preserves Alt with dead turret");
            owner.ModForceForm(false); owner.SetHealth(100);
            NetPlayerBridge::ApplyState(owner, active, false);
            check(owner.Health() == 51 && turret->Health() == 50 && (owner.IsAltForm() || owner.IsMorphing()) && turret->Grounded(),
                "replica activation uses authority HP without a second split");
            const Vector3 replicaPosition = owner.Position;
            NetPlayerBridge::ApplyState(owner, active, false);
            check(owner.Health() == 51 && turret->Health() == 50
                && OpenTK::Mathematics::Equal(owner.Position, replicaPosition), "duplicate active snapshot is idempotent");
            auto airborne = active;
            airborne.WeavelFlags = PlayerState::WeavelFlagTurretActive;
            airborne.HalfturretHealth = 47; airborne.HalfturretPosition = Vector3(4, 23, 5);
            NetPlayerBridge::ApplyState(owner, airborne, false);
            check(owner.Health() == 51 && turret->Health() == 47 && !turret->Grounded()
                && OpenTK::Mathematics::Equal(turret->Position, airborne.HalfturretPosition), "active replica updates airborne physical report and HP separately from player");
            NetPlayerBridge::ApplyState(owner, dead, false); NetPlayerBridge::ApplyState(owner, dead, false);
            check((owner.IsAltForm() || owner.IsMorphing()) && turret->Health() == 0 && owner.Health() == 51, "replica Alt plus dead turret never respawns");
            auto biped = dead; biped.Flags &= ~PlayerState::FlagAltForm; biped.Health = 100;
            NetPlayerBridge::ApplyState(owner, biped, false); NetPlayerBridge::ApplyState(owner, biped, false);
            check(!owner.IsAltForm() && owner.Health() == 100 && turret->Health() == 0, "biped snapshots do not merge authority HP twice");
            auto stale = active; stale.LifeId = static_cast<std::uint16_t>(active.LifeId - 1);
            NetPlayerBridge::ApplyState(owner, stale, false);
            check(!owner.IsAltForm() && turret->Health() == 0, "old life cannot revive a turret");
            auto malformed = active; malformed.HalfturretPosition.X = std::numeric_limits<float>::infinity();
            NetPlayerBridge::ApplyState(owner, malformed, false);
            check(!owner.IsAltForm() && owner.Health() == 100, "invalid physical report is rejected before lifecycle mutation");
            const auto receive = [&](const std::vector<std::uint8_t>& bytes)
            {
                auto wire = std::make_shared<std::vector<std::uint8_t>>(bytes.size() + 1);
                (*wire)[0] = static_cast<std::uint8_t>(PacketType::Snapshot);
                std::copy(bytes.begin(), bytes.end(), wire->begin() + 1);
                NetSession::HandleSnapshot(ReceivedPacket({}, wire, static_cast<std::int32_t>(wire->size())));
            };
            receive(activeBytes); receive(deadBytes); receive(activeBytes); receive(deadBytes);
            check(NetSession::RemoteStateValid[0] && NetSession::RemoteStates[0].WeavelFlags == 0
                && NetSession::LastSnapshotFrame() == 501, "receiver rejects duplicated and reordered snapshots");
            NetPlayerBridge::ApplyState(owner, NetSession::RemoteStates[0], false);
            check((owner.IsAltForm() || owner.IsMorphing()) && turret->Health() == 0, "reordered old active packet cannot revive dead replica");

            // Eight active players plus the maximum 56 health spawners must all fit.
            roster.Count = 8; roster.Revision = 2; NetSession::ApplyRoster(roster);
            for (int i = 0; i < 120; ++i) sim.Step();
            auto spawns = NetHealthSync::RegisteredSpawns();
            check(!spawns.empty(), "room supplies health data for maximum packet fixture");
            NetHealthSync::BeginRoom();
            for (int i = 0; i < NetHealthSync::MaxSpawns; ++i)
            {
                auto spawn = std::make_shared<ItemSpawnEntity>(spawns[0]->Data(), "", &scene);
                spawn->Id = i; spawn->Active = true; NetHealthSync::Register(*spawn);
            }
            // The health subset takes whatever room eight players leave in a
            // snapshot, so how many snapshots a full rotation takes follows
            // from the player state's size -- it is derived, not assumed.
            std::set<int> reported;
            std::size_t perSnapshot = 0;
            int snapshots = 0;
            for (; snapshots < NetHealthSync::MaxSpawns
                && reported.size() < static_cast<std::size_t>(NetHealthSync::MaxSpawns); ++snapshots)
            {
                const auto bytes = publish(800 + static_cast<std::uint32_t>(snapshots));
                check(SnapshotHeader::Read(bytes).PlayerCount == 8, "maximum snapshot retains every player");
                check(bytes.size() + 1 <= static_cast<std::size_t>(NetConfig::MaxPacketSize),
                    "maximum snapshot fits MaxPacketSize");
                const auto tail = std::span(bytes).subspan(SnapshotHeader::Size + 8 * PlayerState::Size + NetMatchTimeSync::Size);
                check(NetHealthSync::Validate(tail), "bounded health subset validates");
                NetHealthSync::Receive(tail);
                perSnapshot = (tail.size() - NetHealthSync::HeaderSize) / NetHealthSync::EntrySize;
                for (std::size_t off = NetHealthSync::HeaderSize; off < tail.size(); off += NetHealthSync::EntrySize)
                    reported.insert(Runtime::ReadInt16LittleEndian(tail.subspan(off)));
            }
            const std::size_t expected = perSnapshot == 0 ? 0
                : (static_cast<std::size_t>(NetHealthSync::MaxSpawns) + perSnapshot - 1) / perSnapshot;
            check(reported.size() == static_cast<std::size_t>(NetHealthSync::MaxSpawns)
                && static_cast<std::size_t>(snapshots) == expected,
                "rotating health reports cover all 56 in " + std::to_string(expected) + " snapshots of "
                    + std::to_string(perSnapshot));
            for (int i = 0; i < NetHealthSync::MaxSpawns; ++i)
            {
                HealthSpawnState state;
                check(NetHealthSync::TryGet(static_cast<std::int16_t>(i), state) && state.Active,
                    "partial receiver preserves previously reported health spawners");
            }
            check(sim.StepFailures() == 0, "asset-backed scene retains zero simulation failures");
            Runtime::ConsoleWriteLine("WEAVEL PASS " + std::to_string(checks) + " production checks | EU1.1 phases1-8");
            CheckLocalSnapshotPrediction(scene, owner, *turret);
            CheckEnemyExtension(scene, owner, *turret);
            result = 0;
        }
        catch (...) { Runtime::ConsoleErrorWriteLine("WEAVEL FAIL " + Runtime::ExceptionToString(std::current_exception())); }
        GameState::Mode(GameMode::Battle); GameState::StorySave = originalSave;
        sim.Stop(); NetSession::Stop();
        return result;
    }
}
