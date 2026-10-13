#include "AimCheck.hpp"
#include "Mods/Network/Server/Match/ServerSim.hpp"
#include "Mods/Network/Session/NetSession.hpp"
#include "../../Entities/Players/PlayerEntity.hpp"
#include "../../GameState.hpp"
#include "../../Scene.hpp"
#include "../../NativeRuntime/System/ExceptionText.hpp"
#include "../Input/HostTouch.hpp"
#include "../Render/FrameTiming.hpp"
#include "Mods/Network/Gameplay/Player/NetPlayerBridge.hpp"
#include "Mods/Network/Gameplay/Player/NetPlayerLifecycle.hpp"
#include "Mods/Network/Integration/Hooks/NetHooks.hpp"
#include <chrono>
#include <cstdlib>
#include <cmath>
#include <limits>
#include <iostream>
#include <stdexcept>

namespace MphRead::Mods::Diagnostics
{
    void AimCheck::ReportClock(const Entities::PlayerEntity& player)
    {
        std::cout << "[aim clock] simulation_step=" << player._scene->FrameCount()
            << " native_touch_sequence=" << player._input.TouchSample().NativeSampleSequence()
            << " native_touch_new=" << player._input.TouchSample().NewNativeSampleThisStep() << '\n';
    }

    std::int32_t AimCheck::Run(const std::string& room)
    {
        Network::ServerSim sim;
        int checks = 0;
        const auto check = [&](bool ok, const char* name)
        {
            if (!ok) throw std::runtime_error(name);
            ++checks; std::cout << "AIM PASS " << name << '\n';
        };
        int result = 1;
        try
        {
            if (!sim.Start(room, GameMode::Battle, 4, [](auto) {}, [] {},
                    Network::MakeNetSessionServerSimulationPort()))
                throw std::runtime_error("asset-backed scene could not start");
            auto roster = Network::RosterPacket::Create();
            roster.MatchId = 1; roster.AuthorityEpoch = 1; roster.Revision = 1; roster.Count = 4;
            for (std::uint8_t i = 0; i < 8; ++i)
            {
                (*roster.Slots)[i] = i; (*roster.Generations)[i] = 1;
                (*roster.Hunters)[i] = static_cast<std::uint8_t>(Hunter::Samus);
                (*roster.Names)[i] = "AIM" + std::to_string(i);
            }
            Network::MatchStatePacket match{};
            match.MatchId = 1; match.AuthorityEpoch = 1; match.RoomKey = room;
            match.Mode = static_cast<std::uint8_t>(GameMode::Battle);
            Network::NetSession::ApplyMatchState(match, false);
            Network::NetSession::ApplyRoster(roster);
            for (int i = 0; i < 120; ++i) sim.Step();
            auto& player = *Entities::PlayerEntity::Players()[0];
            if (!player.ModIsInPlay() || player.Health() <= 0)
                throw std::runtime_error("baseline player did not spawn");
            player.SetIsBot(false);
            Network::NetSession::Stop();
            Entities::PlayerEntity::SetMainPlayerIndex(0);
            player._aimTrace.Enabled = true;
            player._flags1 &= ~Entities::PlayerFlags1::NoAimInput;
            player._frozenTimer = 0; player._field6D0 = false;
            // Independent of the saved aim style (controls.txt).
            player.Controls().SetNativeAim(false); player.Controls().NativeControl() = {};
            player.Controls().ClearAll();
            player._input.Suspend();
            for (int mask = 0; mask < 4; ++mask)
            {
                player.Controls().SetMouseAim((mask & 1) != 0);
                player.Controls().SetKeyboardAim((mask & 2) != 0);
                player._aimTrace.Clear();
                player.ProcessBiped();
                check(player._aimTrace.Get(Input::AimOperation::Follow) == 2U, "one selected zero-input axis pair, pad-only included");
                std::cout << "AIM candidate mouse=" << (mask & 1) << " dual=" << ((mask >> 1) & 1)
                    << " pitch=" << player._aimTrace.Get(Input::AimOperation::Pitch)
                    << " yaw=" << player._aimTrace.Get(Input::AimOperation::Yaw)
                    << " follow=" << player._aimTrace.Get(Input::AimOperation::Follow) << '\n';
            }
            using Keys = OpenTK::Windowing::GraphicsLibraryFramework::Keys;
            using Vector3 = OpenTK::Mathematics::Vector3;
            OpenTK::Windowing::GraphicsLibraryFramework::KeyboardState keyboard;
            OpenTK::Windowing::GraphicsLibraryFramework::MouseState mouse;
            const auto reset = [&]
            {
                player.Controls().ClearAll(); player.ModForgetInputDeltas();
                player.Controls().SetNativeAim(false); player.Controls().NativeControl() = {};
                player._aimFrame = {}; player._gunVec1 = player._facingVector = Vector3(0, 0, 1);
                player._gunVec2 = Vector3(1, 0, 0); player._upVector = Vector3(0, 1, 0);
                player._aimY = 0; player._equipInfo->Zoomed = false;
                player._flags1 &= ~Entities::PlayerFlags1::NoAimInput;
                mouse = {}; keyboard = {};
                Entities::PlayerEntity::ProcessInput(keyboard, mouse, false);
            };
            player.Controls().SetMouseAim(true); player.Controls().SetKeyboardAim(true);
            for (const int fps : {60, 120, 240, 540, 1000})
            {
                reset(); Render::FrameTiming::Reset();
                int simSteps = 0;
                const auto start = std::chrono::steady_clock::now();
                for (int frame = 0; frame < fps; ++frame)
                {
                    mouse.X = 120.0F * (frame + 1) / fps;
                    const auto steps = Render::FrameTiming::Advance(1.0 / fps);
                    for (int step = 0; step < steps; ++step)
                    {
                        Entities::PlayerEntity::ProcessInput(keyboard, mouse, false);
                        player.ProcessBiped(); ++simSteps;
                    }
                }
                // Finish the common exact-second boundary. Binary64 repeated
                // additions can leave one substep infinitesimally pending;
                // no new mouse motion is injected during this drain.
                const auto drain = Render::FrameTiming::Advance(1e-10);
                for (int step = 0; step < drain; ++step)
                { Entities::PlayerEntity::ProcessInput(keyboard, mouse, false); player.ProcessBiped(); ++simSteps; }
                const float yaw = std::atan2(player._gunVec1.X, player._gunVec1.Z) * 180.0F / 3.14159265358979323846F;
                check(simSteps == 60 && std::fabs(yaw + 30.0F) < 0.002F, "synthetic render schedule preserves mouse total and sim60");
                std::cout << "AIM schedule fps=" << fps << " sim=" << simSteps << " yaw=" << yaw
                    << " cpu_us=" << std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count() << '\n';
            }
            reset(); mouse.X = 40;
            for (int i = 0; i < 5; ++i)
            { Entities::PlayerEntity::ProcessInput(keyboard, mouse, false); player.ProcessBiped(); }
            check(std::fabs(std::atan2(player._gunVec1.X, player._gunVec1.Z) * 180 / 3.14159265F + 10) < 0.002F,
                "catch-up5 consumes cumulative host delta once");
            Entities::PlayerEntity::ProcessInput(keyboard, mouse, true);
            mouse.X += 400;
            Entities::PlayerEntity::ProcessInput(keyboard, mouse, false);
            check(player._input.MouseDeltaX() == 0, "Suspend flushes menu motion");
            reset(); player.Controls().SetMouseAim(false);
            keyboard.SetKeyDown(Keys::Left, true);
            Entities::PlayerEntity::ProcessInput(keyboard, mouse, false);
            player.ProcessBiped();
            check(std::fabs(player._nativeDual.X - 3.2F) < 0.00001F && player._buttonAimX == 0, "production Human consumes old before first producer");
            Entities::PlayerEntity::ProcessInput(keyboard, mouse, false); player.ProcessBiped();
            check(std::fabs(player._nativeDual.X - 3.24F) < 0.00001F && std::fabs(player._buttonAimX - 1.6F) < 0.00001F, "Direct keyboard next old velocity at sim60");
            player.Controls().SetMouseAim(true);
            keyboard.SetKeyDown(Keys::Left, false);
            Entities::PlayerEntity::ProcessInput(keyboard, mouse, false); player.ProcessBiped();
            check(player._aimFrame.Source == Input::AimSource::Dual && player._nativeDual.X > 0 && player._nativeDual.X < 3.24F,
                "keyboard release retains decay with Mouse Aim enabled");
            mouse.X += 4;
            Entities::PlayerEntity::ProcessInput(keyboard, mouse, false); player.ProcessBiped();
            check(player._aimFrame.Source == Input::AimSource::Mouse && player._nativeDual.X == 0,
                "new mouse motion takes ownership from keyboard decay");
            reset(); player.Controls().SetNativeAim(true); mouse.X = 40;
            for (int i = 0; i < 2; ++i)
            {
                Entities::PlayerEntity::ProcessInput(keyboard, mouse, false); player.PrepareAimInput();
                player._aimTrace.Clear(); player.ApplyLocalAim(false);
                check(player._aimFrame.Source == Input::AimSource::Mouse && player._aimFrame.Native
                    && player._aimTrace.Get(Input::AimOperation::Yaw) == 1, "Classic mouse applies DS rules on every 60 Hz step");
                mouse.X += 40;
            }
            reset(); player.Controls().SetNativeAim(true); player.Controls().SetMouseAim(false);
            keyboard.SetKeyDown(Keys::Right, true);
            for (int i = 0; i < 2; ++i)
            {
                Entities::PlayerEntity::ProcessInput(keyboard, mouse, false); player.PrepareAimInput();
                player._aimTrace.Clear(); player.ApplyLocalAim(false);
                check(player._aimTrace.Get(Input::AimOperation::Yaw) == 1, "Classic keys respond on every 60 Hz step");
            }
            reset(); player.Controls().SetNativeAim(true); Input::HostTouch::Publish(true, 128, 96);
            Entities::PlayerEntity::ProcessInput(keyboard, mouse, false); player.PrepareAimInput();
            check(player._aimFrame.Source != Input::AimSource::Touch, "Classic touch does not wait for the 30 Hz sample");
            Input::HostTouch::Withdraw();
            reset(); player.Controls().SetNativeAim(true); player.Controls().NativeControl().RomCadence = true;
            keyboard.SetKeyDown(Keys::Right, true);
            // Scene frame120 is a native gameplay tick, independently of Touch phase.
            Entities::PlayerEntity::ProcessInput(keyboard, mouse, false);
            player.PrepareAimInput(); player._aimTrace.Clear(); player.ApplyLocalAim(false);
            check(player._aimTrace.Get(Input::AimOperation::Yaw) == 1 && !player._input.TouchSample().NewNativeSampleThisStep(), "Dual scene phase is independent of Touch sibling phase");
            Entities::PlayerEntity::ProcessInput(keyboard, mouse, false);
            player.PrepareAimInput(); player.ApplyLocalAim(false);
            check(std::fabs(player._nativeDual.X + 3.28F) < 0.00001F, "Native Human Biped producer floor");
            player.Controls().NativeControl().Flag84E = 1;
            Entities::PlayerEntity::ProcessInput(keyboard, mouse, false);
            Entities::PlayerEntity::ProcessInput(keyboard, mouse, false);
            player.PrepareAimInput(); player._aimTrace.Clear(); player.ApplyLocalAim(false);
            check(std::fabs(player._nativeDual.X + 3.28F) < 0.00001F && player._aimTrace.Get(Input::AimOperation::Yaw) == 1,
                "Native84E preserves consumer and skips producer");
            player.Controls().NativeControl().Flag84E = 0;
            reset(); player.Controls().SetNativeAim(true); player.Controls().NativeControl().RomCadence = true;
            keyboard.SetKeyDown(Keys::Right, true);
            Entities::PlayerEntity::ProcessInput(keyboard, mouse, false);
            Entities::CameraInfo freeCamera;
            freeCamera.Facing = Vector3(0, 0, 1);
            player.ApplyNativeFreeCamera(freeCamera);
            check(std::fabs(player._nativeDual.X + 3.2F) < 0.00001F && freeCamera.Facing.X < -0.02F,
                "FreeCamera consumes same-step producer on first press");
            const float firstStep = freeCamera.Facing.X;
            Entities::PlayerEntity::ProcessInput(keyboard, mouse, false);
            player.ApplyNativeFreeCamera(freeCamera);
            check(freeCamera.Facing.X < firstStep - 0.02F, "FreeCamera keys turn on every 60 Hz step");
            {
                // Death camera: the Free camera a player is switched to on death.
                reset(); player.Controls().SetMouseAim(true);
                player.SwitchCamera(Entities::CameraType::Free, player._cameraInfo->Position + Vector3(0, 0, 1));
                player._camSwitchTimer = static_cast<std::uint16_t>(player.Values().CamSwitchTime * 2);
                player._cameraInfo->Facing = Vector3(0, 0, 1);
                const auto before = player._cameraInfo->Facing;
                for (int i = 0; i < 4; ++i)
                {
                    mouse.X += 40;
                    Entities::PlayerEntity::ProcessInput(keyboard, mouse, false);
                    player.UpdateCamera();
                }
                std::cout << "AIM deathcam facing " << before.X << "," << before.Z << " -> "
                    << player._cameraInfo->Facing.X << "," << player._cameraInfo->Facing.Z
                    << " delta=" << player._input.MouseDeltaX() << '\n';
                check(std::fabs(player._cameraInfo->Facing.X - before.X) > 0.05F, "death camera turns with the mouse");
                player.Controls().SetNativeAim(true);
                player._cameraInfo->Facing = Vector3(0, 0, 1);
                for (int i = 0; i < 4; ++i)
                {
                    mouse.X += 40;
                    Entities::PlayerEntity::ProcessInput(keyboard, mouse, false);
                    player.UpdateCamera();
                }
                check(std::fabs(player._cameraInfo->Facing.X - before.X) > 0.05F, "Classic death camera turns with the mouse");
                player.Controls().SetNativeAim(false);
                player.SwitchCamera(Entities::CameraType::First, player._facingVector);
            }
            {
                // The wheel is a running total: a respawn or a new match (no
                // previous snapshot) must not read the whole total as a notch.
                reset();
                mouse.Scroll = OpenTK::Mathematics::Vector2(0, 7);
                Entities::PlayerEntity::ProcessInput(keyboard, mouse, false);
                player.ModForgetInputDeltas();
                Entities::PlayerEntity::ProcessInput(keyboard, mouse, false);
                const bool phantom = Entities::PlayerEntity::_isScrollingUp || Entities::PlayerEntity::_isScrollingDown;
                mouse.Scroll = OpenTK::Mathematics::Vector2(0, 8);
                Entities::PlayerEntity::ProcessInput(keyboard, mouse, false);
                check(!phantom && Entities::PlayerEntity::_isScrollingUp,
                    "no phantom wheel notch after input is reset; a real notch still reads");
                mouse.Scroll = {};
            }
            {
                // Audit A5: pad only (no mouse or key aim), stick centred.
                reset(); player.Controls().SetMouseAim(false); player.Controls().SetKeyboardAim(false);
                player._facingVector = Vector3(0.1F, 0, 1).Normalized();
                const float before = player._facingVector.X;
                Entities::PlayerEntity::ProcessInput(keyboard, mouse, false);
                player.PrepareAimInput(); player._aimTrace.Clear(); player.ApplyLocalAim(false);
                check(player._aimFrame.Source == Input::AimSource::Gamepad
                    && player._aimTrace.Get(Input::AimOperation::Follow) == 2 && player._facingVector.X < before,
                    "pad-only centred stick still follows on each axis");
                player.Controls().SetMouseAim(true); player.Controls().SetKeyboardAim(true);
            }
            {
                // Audit A2: a bot's Biped aim turns it (PlayerInput's bot consumer).
                reset(); player._isBot = true;
                player._buttonAimX = 5.0F; player._buttonAimY = 0.0F;
                player._aimTrace.Clear(); player.ProcessBiped();
                const float yaw = std::atan2(player._gunVec1.X, player._gunVec1.Z) * 180.0F / 3.14159265F;
                player._isBot = false; player._buttonAimX = 0;
                check(player._aimFrame.Owner == Input::AimOwner::Bot && std::fabs(yaw) > 4.0F
                    && player._aimTrace.Get(Input::AimOperation::Yaw) >= 1, "bot Biped aim turns the bot");
            }
            {
                // Respawn re-arms the Power Beam whatever was held at death.
                reset();
                player._availableWeapons[MphRead::BeamType::Missile] = true;
                player._ammo[1] = 50;
                (void)player.TryEquipWeapon(MphRead::BeamType::Missile, true);
                // Death while holding the Missile, then the game's own spawn.
                player.TakeDamage(1000, Entities::DamageFlags::Death | Entities::DamageFlags::IgnoreInvuln,
                    std::nullopt, nullptr);
                const auto atDeath = player.CurrentWeapon();
                // "Press fire to respawn" with a one-frame click, as a player does.
                auto& shootBind = player.Controls().Shoot();
                shootBind.SetType(Entities::ButtonType::Mouse);
                shootBind.SetMouseButton(OpenTK::Windowing::GraphicsLibraryFramework::MouseButton::Left);
                MphRead::BeamType afterSpawn = MphRead::BeamType::None;
                int spawnedAt = -1;
                for (int frame = 0; frame < 900; ++frame)
                {
                    const bool click = (player.Health() == 0 && frame % 40 == 39) || (spawnedAt >= 0 && frame <= spawnedAt + 3);
                    mouse.SetButtonDown(OpenTK::Windowing::GraphicsLibraryFramework::MouseButton::Left, click);
                    Entities::PlayerEntity::ProcessInput(keyboard, mouse, false);
                    const bool wasDead = player.Health() == 0;
                    (void)player.Process();
                    if (wasDead && player.Health() > 0) { spawnedAt = frame; afterSpawn = player.CurrentWeapon(); }
                    if (spawnedAt >= 0 && frame > spawnedAt + 30) break;
                    if (spawnedAt >= 0 && player.CurrentWeapon() != MphRead::BeamType::PowerBeam)
                    {
                        std::cout << "AIM respawn weapon -> " << static_cast<int>(player.CurrentWeapon())
                            << " " << (frame - spawnedAt) << " frames after spawn\n";
                        break;
                    }
                }
                mouse.SetButtonDown(OpenTK::Windowing::GraphicsLibraryFramework::MouseButton::Left, false);
                std::cout << "AIM respawn spawnedAt=" << spawnedAt << '\n';
                std::cout << "AIM respawn death=" << static_cast<int>(atDeath) << '\n';
                std::cout << "AIM respawn weapon spawn=" << static_cast<int>(afterSpawn)
                    << " after30=" << static_cast<int>(player.CurrentWeapon())
                    << " selection=" << static_cast<int>(player._weaponSelection) << '\n';
                check(afterSpawn == MphRead::BeamType::PowerBeam && player.CurrentWeapon() == MphRead::BeamType::PowerBeam,
                    "respawn holds the Power Beam");
            }
            reset(); player._aimFrame.Native = true;
            player._facingVector = Vector3(0.1F, 0, 1).Normalized();
            const float beforeZero = player._facingVector.X;
            player._aimTrace.Clear(); player.UpdateAimY(0); player.UpdateAimX(0);
            check(player._aimTrace.Get(Input::AimOperation::Follow) == 2 && player._facingVector.X < beforeZero,
                "zero Pitch and Yaw each preserve actual Facing Follow");
            player._equipInfo->Zoomed = true; player._aimTrace.Clear();
            player.UpdateAimY(0); player.UpdateAimX(0);
            check(player._aimTrace.Get(Input::AimOperation::Follow) == 1 && player._aimTrace.Get(Input::AimOperation::Snap) == 1
                && OpenTK::Mathematics::Equal(player._facingVector, player._gunVec1), "zero Zoom Pitch Follow and Yaw Snap stay asymmetric");
            reset(); Input::HostTouch::Publish(true, 128, 96);
            Entities::PlayerEntity::ProcessInput(keyboard, mouse, false); player.PrepareAimInput();
            check(player._aimFrame.Source != Input::AimSource::Touch && !player._aimFrame.Native,
                "modern aim leaves a host touch on the 60 Hz pointer path");
            reset(); player.Controls().SetNativeAim(true); player.Controls().NativeControl().Flags = 0x22;
            player.Controls().NativeControl().RomCadence = true;
            unsigned touchPairs = 0;
            for (int i = 0; i < 12; ++i)
            {
                Entities::PlayerEntity::ProcessInput(keyboard, mouse, false); player.PrepareAimInput();
                player._aimTrace.Clear(); player.ApplyLocalAim(false);
                touchPairs += player._aimTrace.Get(Input::AimOperation::Follow) == 2;
            }
            check(touchPairs > 0 && touchPairs <= 6, "held zero-delta Touch retains axis Follow at native sample cadence");
            Input::HostTouch::Publish(false, 128, 96);
            Entities::PlayerEntity::ProcessInput(keyboard, mouse, false);
            Entities::PlayerEntity::ProcessInput(keyboard, mouse, false);
            player.PrepareAimInput(); player._aimTrace.Clear(); player.ApplyLocalAim(false);
            check(player._aimTrace.Get(Input::AimOperation::Follow) == 0, "lifted Touch skips its consumer instead of following as held zero");
            Input::HostTouch::Withdraw();
            player.Controls().SetNativeAim(false);
            reset(); player.Controls().SetMouseAim(true);
            for (int slot = 1; slot < 4; ++slot)
            {
                auto& remote = *Entities::PlayerEntity::Players()[slot];
                remote.SetIsBot(false); remote._aimTrace.Enabled = true;
                remote._input.Suspend(); remote.PrepareAimInput();
                check(remote._input.MouseDeltaX() == 0, "other slots never receive local host delta");
                remote.SetIsBot(true); remote.PrepareAimInput();
                check(remote._aimFrame.Owner == Input::AimOwner::Bot, "BOT owner has priority over host source");
            }
            Network::NetSession::StartServerAuthority([](auto) {}, [] {});
            Network::NetSession::ApplyMatchState(match, false);
            Network::NetSession::ApplyRoster(roster);
            Network::NetSession::Update(0);
            Network::NetSession::Update(0);
            for (int slot = 0; slot < 4; ++slot)
            {
                auto& remote = *Entities::PlayerEntity::Players()[slot]; remote.SetIsBot(false);
                Network::NetPlayerLifecycle::OnSpawn(remote);
                Network::NetPlayerBridge::NoteSpawn(slot);
                Network::IntentPacket intent{};
                intent.MatchId = 1; intent.AuthorityEpoch = 1;
                intent.SlotGeneration = Network::NetPlayerLifecycle::Generation(slot);
                intent.LifeId = Network::NetPlayerLifecycle::Get(slot);
                intent.Frame = 1; intent.Buttons = Network::IntentButtons::InPlayState;
                intent.AckFrame = Network::NetSession::NetFrame();
                intent.Aim = Vector3(0.3F, 0.1F, 1);
                Network::NetSession::AcceptSlotIntent(slot, intent);
                Network::NetPlayerBridge::ApplyIntent(remote, intent);
                remote._aimTrace.Enabled = true; remote._aimTrace.Clear();
                remote.PrepareAimInput(); remote.ApplyModAim();
                const auto facing = remote._facingVector;
                remote.CameraInfo()->Position = Vector3(3, 4, 5);
                remote.ModRefreshNetworkAim();
                check(remote._aimTrace.Get(Input::AimOperation::Follow) == 1
                    && OpenTK::Mathematics::Equal(facing, remote._facingVector), "4slot camera projection has no second Follow");
                const auto target = remote.CameraInfo()->Position + OpenTK::Mathematics::Multiply(remote._gunVec1, Fixed::ToFloat(remote.Values().AimDistance));
                check(OpenTK::Mathematics::Equal(target, remote._aimPosition), "post-camera target uses actual camera position");
                const auto accepted = Network::NetSession::RemoteIntent(static_cast<std::size_t>(slot)).Aim;
                for (int fault = 0; fault < 6; ++fault)
                {
                    auto invalid = intent;
                    invalid.Frame = 2; invalid.Aim = Vector3(1, 0, 0);
                    if (fault == 0) invalid.Frame = 1; // duplicate
                    if (fault == 1) invalid.Frame = 0; // reordered
                    if (fault == 2) ++invalid.SlotGeneration;
                    if (fault == 3) ++invalid.LifeId;
                    if (fault == 4) ++invalid.MatchId;
                    if (fault == 5) ++invalid.AuthorityEpoch;
                    Network::NetSession::AcceptSlotIntent(slot, invalid);
                    check(OpenTK::Mathematics::Equal(accepted, Network::NetSession::RemoteIntent(static_cast<std::size_t>(slot)).Aim),
                        "4slot duplicate/reorder/generation/life/stream rejects changed aim");
                }
                Network::NetPlayerBridge::NoteSpawn(slot);
                remote._aimTrace.Clear(); remote.ApplyModAim();
                check(remote._aimTrace.Get(Input::AimOperation::Follow) == 0, "spawn ACK hold prevents remote rotation and Follow");
                auto behind = intent; behind.AckFrame = Network::NetSession::NetFrame() - 1;
                Network::NetPlayerBridge::ApplyIntent(remote, behind);
                check(!Network::NetPlayerBridge::AimTrusted(slot), "ACK before spawn preserves aim hold");
                Network::NetPlayerBridge::ApplyIntent(remote, intent);
                check(Network::NetPlayerBridge::AimTrusted(slot), "ACK at spawn releases aim hold");
            }
            for (int frame = 0; frame <= Network::NetHooks::StaleIntentFrames; ++frame) Network::NetSession::Update(0);
            for (int slot = 0; slot < 4; ++slot)
            {
                auto& remote = *Entities::PlayerEntity::Players()[slot];
                remote._aimTrace.Clear(); remote.ApplyModAim(); remote.ModRefreshNetworkAim();
                check(remote._aimTrace.Get(Input::AimOperation::Follow) == 0
                    && remote._aimTrace.Get(Input::AimOperation::Projection) == 1,
                    "expired intent stops admission but retains camera projection of committed aim");
            }
            const auto gun = player._gunVec1;
            player.ModSetAim(Vector3(std::numeric_limits<float>::infinity(), 0, 1));
            check(OpenTK::Mathematics::Equal(gun, player._gunVec1), "nonfinite snapshot cannot poison gun state");
            Network::NetSession::Stop();
            reset(); player.UpdateAimX(0.0001F);
            check(player._gunVec1.X > 0.000001F && player._gunVec1.X < 0.000002F,
                "sub-LUT yaw retains PC float precision");
            reset(); player.UpdateAimY(0.0001F);
            check(player._gunVec1.Y > 0.000001F && player._gunVec1.Y < 0.000002F,
                "sub-LUT pitch retains PC float precision");
            reset(); player._aimFrame.Exact = false;
            player.CameraInfo()->Position = Vector3(0, 0, 0);
            player._aimPosition = Vector3(0, 0, Fixed::ToFloat(player.Values().AimDistance));
            const auto previousTarget = player._aimPosition;
            player.UpdateAimX(20); player.UpdateAimY(10);
            check(OpenTK::Mathematics::Equal(previousTarget, player._aimPosition),
                "NonExact consumers update basis independently of Target maintenance");
            check(std::fabs(Vector3::Dot(player._facingVector, player._gunVec2)) < 0.00001F
                && std::fabs(Vector3::Dot(player._facingVector, player._upVector)) < 0.00001F,
                "NonExact float basis remains orthogonal");
            player.MaintainNonExactAimTarget();
            check(!OpenTK::Mathematics::Equal(previousTarget, player._aimPosition)
                && std::fabs(OpenTK::Mathematics::LengthSquared(player._gunVec1) - 1) < 0.00001F,
                "NonExact maintenance updates Target and normalized Gun direction");
            checks += CheckModes(player);
            if (std::getenv("FRUITY_AIM_BENCH"))
            {
                player._aimTrace.Enabled = false;
                Benchmark(player);
                player._aimTrace.Enabled = true;
            }
            checks += CheckWeapons(sim);
            if (sim.StepFailures() != 0) throw std::runtime_error("simulation failures");
            sim.Stop();
            checks += CheckHunterWeapons(room);
            std::cout << "AIM PASS " << checks << " production checks; synthetic schedules; ROM/GPU acceptance NOT_RUN\n";
            result = 0;
        }
        catch (...) { std::cerr << "AIM FAIL " << NativeRuntime::ExceptionToString(std::current_exception()) << '\n'; }
        sim.Stop(); Network::NetSession::Stop();
        return result;
    }
}
