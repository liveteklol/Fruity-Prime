#include "WeavelAltFormCheck.hpp"

#include "../Network/NetHooks.hpp"
#include "../Network/NetPlayerBridge.hpp"
#include "../Network/NetPlayerLifecycle.hpp"
#include "../Network/NetHitPrediction.hpp"
#include "../Network/NetRoomChange.hpp"
#include "../Network/NetSession.hpp"
#include "../../Entities/Players/HalfturretEntity.hpp"
#include "../../GameState.hpp"
#include "../../Scene.hpp"
#include "../../NativeRuntime/System/Console.hpp"

#include <array>
#include <stdexcept>

namespace MphRead::Mods::Diagnostics
{
    using namespace Network;
    using namespace Entities;
    using OpenTK::Mathematics::Vector3;

    void WeavelAltFormCheck::CheckLocalSnapshotPrediction(Scene& scene,
        PlayerEntity& owner, HalfturretEntity& turret)
    {
        int checks = 0;
        const auto check = [&checks](bool ok, const std::string& name)
        {
            if (!ok) throw std::runtime_error("Local snapshot: " + name);
            ++checks;
            NativeRuntime::ConsoleWriteLine("WEAVEL LOCAL SNAPSHOT PASS " + name);
        };
        const auto savedRole = NetSession::_role;
        const auto savedAuthority = NetSession::_isAuthority;
        const auto savedLocal = NetSession::_localSlot;
        const auto savedFrame = NetSession::_netFrame;
        const auto savedValid = NetSession::RemoteStateValid;
        const auto savedStates = NetSession::RemoteStates;
        const auto restore = [&]
        {
            NetSession::_role = savedRole;
            NetSession::_isAuthority = savedAuthority;
            NetSession::_localSlot = savedLocal;
            NetSession::_netFrame = savedFrame;
            NetSession::RemoteStateValid = savedValid;
            NetSession::RemoteStates = savedStates;
        };
        try
        {
            NetSession::_role = NetRole::Client;
            NetSession::_isAuthority = false;
            NetSession::_localSlot = owner.SlotIndex();
            NetSession::_netFrame = 1001;
            NetSession::RemoteStateValid.fill(false);
            check(NetSession::IsClient() && !NetSession::IsAuthority()
                && NetSession::LocalSlot() == owner.SlotIndex() && NetRoomChange::GameplayReady(),
                "fixture is a non-authority client with the local Weavel slot");
            // Let the real hook perform one-time setup before starting a local
            // transition. No snapshot is valid during this preparation call.
            NetHooks::AfterInput(scene);
            PlayerState state;
            state.SlotIndex = static_cast<std::uint8_t>(owner.SlotIndex());
            state.SlotGeneration = NetPlayerLifecycle::Generation(owner.SlotIndex());
            state.LifeId = NetPlayerLifecycle::Get(owner.SlotIndex());
            state.Flags = PlayerState::FlagSpawned;
            state.Health = 100;
            state.Position = owner.Position;
            state.Facing = owner.FacingVector();
            check(state.SlotGeneration != 0 && state.LifeId != 0, "fixture has a real active network life");
            NetPlayerBridge::ApplyState(owner, state, true); // establish this life before prediction

            const std::array<std::string, 3> paths{"ApplyState local", "AfterInput", "AfterSimulation"};
            for (std::size_t path = 0; path < paths.size(); ++path)
            {
                NetSession::_localSlot = owner.SlotIndex();
                const auto apply = [&](PlayerState snapshot, bool local)
                {
                    NetHitPrediction::Reset(); // no unrelated predicted healing/debit in this fixture
                    NetSession::RemoteStates[state.SlotIndex] = snapshot;
                    NetSession::RemoteStateValid[state.SlotIndex] = true;
                    const auto applied = NetSession::StatesApplied();
                    if (path == 0) NetPlayerBridge::ApplyState(owner, snapshot, local);
                    else if (path == 1) NetHooks::AfterInput(scene);
                    else NetHooks::AfterSimulation();
                    if (path != 0)
                        check(NetSession::StatesApplied() == applied + 1,
                            paths[path] + " reaches ApplyRemoteStates");
                };
                const auto biped = [&]
                {
                    owner.ModForceWeavelState(false, false);
                    owner.SetHealth(100);
                    owner.Controls().ClearAll();
                    owner._flags1 |= PlayerFlags1::Standing;
                    owner._spawnInvulnTimer = owner._damageInvulnTimer = 0;
                    state.Flags = PlayerState::FlagSpawned;
                    state.WeavelFlags = 0;
                    state.HalfturretHealth = 0;
                    state.Position = owner.Position;
                    state.HalfturretPosition = Vector3(100, 100, 100);
                };

                biped();
                owner.EnterAltForm();
                check(owner.IsMorphing() && turret.Health() == 50, paths[path] + " starts a real local morph");
                apply(state, true);
                check(owner.IsMorphing() && !owner.IsUnmorphing() && turret.Health() == 50,
                    paths[path] + " stale Biped cannot cancel morph or kill its turret");

                biped();
                owner.ModForceWeavelState(true, true);
                turret.ApplyNetworkState(50, Vector3(2, 20, 3), true);
                state.Flags |= PlayerState::FlagAltForm;
                state.Health = 78;
                apply(state, true);
                check(owner.IsAltForm() && turret.Health() == 50
                    && TypeExtensions::TestFlag(owner.Flags2(), PlayerFlags2::Halfturret),
                    paths[path] + " stale inactive report cannot kill a live local turret");
                check(owner.Health() == 78, paths[path] + " retains LocalHealthFor authority correction");
                check(OpenTK::Mathematics::Equal(turret.Position, Vector3(2, 20, 3)) && turret.Grounded(),
                    paths[path] + " stale inactive report cannot rewind turret physics");

                // Other players' hits on the turret land on the authority only:
                // a lower health is damage taken there, a higher one only an
                // older report. Position and footing stay the owner's.
                state.WeavelFlags = PlayerState::WeavelFlagTurretActive; // stale airborne report
                state.HalfturretHealth = 47;
                state.HalfturretPosition = Vector3(4, 23, 5);
                apply(state, true);
                check(turret.Health() == 47 && turret.Grounded()
                    && OpenTK::Mathematics::Equal(turret.Position, Vector3(2, 20, 3)),
                    paths[path] + " active report lowers local turret HP, never its position/grounded");
                state.HalfturretHealth = 49;
                apply(state, true);
                check(turret.Health() == 47, paths[path] + " an older, higher turret HP cannot raise it");

                turret.Die();
                apply(state, true);
                check(owner.IsAltForm() && turret.Health() == 0
                    && !TypeExtensions::TestFlag(owner.Flags2(), PlayerFlags2::Halfturret),
                    paths[path] + " stale active report cannot respawn a dead local turret");

                // The same hook identifies slot0 as remote when another slot is
                // local. Explicit active/dead/biped reconciliation still applies.
                NetSession::_localSlot = 1;
                biped();
                state.Flags |= PlayerState::FlagAltForm;
                state.WeavelFlags = PlayerState::WeavelFlagTurretActive | PlayerState::WeavelFlagTurretGrounded;
                state.HalfturretHealth = 47;
                state.HalfturretPosition = Vector3(4, 23, 5);
                apply(state, false);
                check((owner.IsAltForm() || owner.IsMorphing()) && turret.Health() == 47 && turret.Grounded()
                    && OpenTK::Mathematics::Equal(turret.Position, state.HalfturretPosition),
                    paths[path] + " remote active turret is reconciled without splitting HP");
                check(owner.Health() == state.Health, paths[path] + " remote activation does not split authority HP");
                state.WeavelFlags = 0; state.HalfturretHealth = 0;
                apply(state, false);
                check((owner.IsAltForm() || owner.IsMorphing()) && turret.Health() == 0,
                    paths[path] + " remote Alt with dead turret remains dead");
                state.Flags &= ~PlayerState::FlagAltForm;
                apply(state, false);
                check(!owner.IsAltForm() && turret.Health() == 0 && owner.Health() == state.Health,
                    paths[path] + " remote Biped reconciliation remains idempotent without HP merge");
            }
            NativeRuntime::ConsoleWriteLine("WEAVEL LOCAL SNAPSHOT PASS " + std::to_string(checks) + " checks");
        }
        catch (...) { restore(); throw; }
        restore();
    }
}
