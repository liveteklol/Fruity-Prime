#include "Mods/Network/Integration/Hooks/NetHooks.hpp"
#include "Mods/Network/Gameplay/Bombs/NetBombs.hpp"
#include "Mods/Network/Testing/Combat/HitRig.hpp"
#include "Mods/Network/Gameplay/Combat/NetHitPrediction.hpp"

#include "Mods/Network/Replay/DemoPlayback.hpp"
#include "Mods/Network/Testing/Maps/MapAudit.hpp"
#include "Mods/Network/Diagnostics/NetDiagnostics.hpp"
#include "Mods/Network/Diagnostics/NetLog.hpp"
#include "Mods/Network/Session/NetMatchEnd.hpp"
#include "Mods/Network/Session/NetMatchSync.hpp"
#include "Mods/Network/Gameplay/Player/NetInputHistory.hpp"
#include "Mods/Network/Gameplay/Player/NetPlayerBridge.hpp"
#include "Mods/Network/Bots/ServerBots.hpp"
#include "Mods/Network/Gameplay/Weapons/NetShotEvents.hpp"
#include "Mods/Network/Gameplay/Player/NetPlayerSetup.hpp"
#include "Mods/Network/Protocol/NetProtocol.hpp"
#include "Mods/Network/Session/NetRoomChange.hpp"
#include "Mods/Network/Session/NetSession.hpp"
#include "Mods/Network/Session/NetSlotManager.hpp"
#include "Mods/Network/Gameplay/Replication/NetSmoothing.hpp"
#include "Mods/Network/Diagnostics/NetTimingDiagnostics.hpp"
#include "Mods/Network/Testing/Harness/NetTestScript.hpp"
#include "Mods/Network/Gameplay/Player/PlayerColors.hpp"
#include "Mods/SpectatorMode.hpp"
#include "Entities/Players/PlayerEntity.hpp"
#include "Scene.hpp"
#include "NativeRuntime/System/Managed.hpp"
#include "Formats/Types.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>

using ::MphRead::NativeRuntime::RequireReference;
using ::MphRead::TestFlag;

namespace
{
}

namespace MphRead::Mods::Network
{
    std::int32_t NetHooks::LocalSlot()
    {
        if (DemoPlayback::IsActive() || NetSession::Role() == NetRole::Server)
        {
            return -1;
        }
        if (NetSession::Active() && NetSession::LocalSlot() >= 0)
        {
            return NetSession::LocalSlot();
        }
        return 0;
    }

    bool NetHooks::IsPuppet(Entities::PlayerEntity& player)
    {
        return (NetSession::Active() || DemoPlayback::IsActive())
            && player.SlotIndex() != LocalSlot();
    }

    bool NetHooks::ZoomIsReported(Entities::PlayerEntity& player)
    {
        return IsPuppet(player) && !player.IsBot();
    }

    bool NetHooks::KeepSlotAlive(Entities::PlayerEntity&)
    {
        return NetSession::Active();
    }

    bool NetHooks::SnapshotPositions()
    {
        return _snapshotOwnsPuppets && !NetSession::IsAuthority()
            && NetSession::SnapshotAge() <= SnapshotStaleFrames;
    }

    void NetHooks::AfterRemoteMovement(Entities::PlayerEntity& player)
    {
        if (!NetSession::Active() || !NetRoomChange::GameplayReady()
            || player.SlotIndex() == NetSession::LocalSlot())
        {
            return;
        }
        const std::int32_t slot = player.SlotIndex();
        if (slot < 0 || static_cast<std::size_t>(slot) >= NetSession::RemotePlayerSlots)
        {
            return;
        }
        if (!TestFlag(player.LoadFlags(), Entities::LoadFlags::Spawned) || player.Health() <= 0)
        {
            return;
        }
        const auto s = static_cast<std::size_t>(slot);
        if (SnapshotPositions())
        {
            if (NetSession::HasRemoteState(slot))
            {
                NetTimingDiagnostics::Position(slot, true);
                NetPlayerBridge::RestoreSnapshotPosition(player, NetSession::RemoteState(s));
            }
            return;
        }
        if (!NetSession::IsAuthority() && !_pinPuppetsOnClients)
        {
            return;
        }
        if (!NetSession::HasRemoteIntent(slot))
        {
            return;
        }
        if (!NetSession::IsAuthority())
        {
            NetTimingDiagnostics::Position(slot, false);
        }
        NetPlayerBridge::RestoreReportedPosition(player, NetSession::RemoteIntent(s));
    }

    OpenTK::Mathematics::Vector3 NetHooks::RemoteShotOrigin(
        Entities::PlayerEntity& player, OpenTK::Mathematics::Vector3 current)
    {
        if (!NetSession::Active() || player.SlotIndex() == NetSession::LocalSlot()
            || player.SlotIndex() < 0
            || static_cast<std::size_t>(player.SlotIndex()) >= NetSession::RemotePlayerSlots)
        {
            return current;
        }
        // Draw the projectile from the muzzle position that actually fired it,
        // even when this copy is still smoothing the remote player's body.
        // FiringRay enforces the same proximity bound before exposing the ray.
        if (const auto event = NetShotEvents::FiringRay(player); event.has_value())
        {
            return event->Origin;
        }
        if (!NetSession::IsAuthority())
        {
            return current;
        }
        const auto slot = static_cast<std::size_t>(player.SlotIndex());
        const IntentPacket& intent = NetSession::RemoteIntent(slot);
        // The shooter's own ray, when the intent that pulled this trigger
        // carries it -- but never one far from where the authority has them.
        if (intent.HasShot && OpenTK::Mathematics::LengthSquared(intent.ShotOrigin - intent.Position) < 9.0F)
        {
            return intent.ShotOrigin;
        }
        return current
            + intent.Position
            - player.Position;
    }

    OpenTK::Mathematics::Vector3 NetHooks::DrawnRemoteShot(
        Entities::PlayerEntity& player, OpenTK::Mathematics::Vector3 origin, OpenTK::Mathematics::Vector3 current)
    {
        if (!NetSession::Active() || NetSession::IsAuthority()
            || player.SlotIndex() == NetSession::LocalSlot() || player.IsBot())
        {
            return current;
        }
        // A remote player's shot drawn here: their exact ray when it came with
        // the intent, turned onto this player when it was aimed at them in
        // the world the shooter was looking at -- from the gun drawn here, so
        // the shot that hits is the shot seen arriving.
        OpenTK::Mathematics::Vector3 from{};
        OpenTK::Mathematics::Vector3 direction{};
        std::uint32_t ack = 0;
        NetPlayerBridge::ShooterRay(player, origin, from, direction, ack);
        if (!(direction.LengthSquared() > 0.0001F))
        {
            return current;
        }
        OpenTK::Mathematics::Vector3 coil = direction;
        if (NetPlayerBridge::CoilAimFor(player, origin, coil))
        {
            return coil;
        }
        const OpenTK::Mathematics::Vector3 turned = NetPlayerBridge::RetargetAtLocal(player, origin, direction, ack, from);
        return (turned - direction).LengthSquared() == 0.0F ? current : turned;
    }

    OpenTK::Mathematics::Vector3 NetHooks::RemoteShotDirection(
        Entities::PlayerEntity& player, OpenTK::Mathematics::Vector3 current)
    {
        if (!NetSession::Active() || player.SlotIndex() == NetSession::LocalSlot()
            || player.SlotIndex() < 0
            || static_cast<std::size_t>(player.SlotIndex()) >= NetSession::RemotePlayerSlots)
        {
            return current;
        }
        // The shot event carries the exact ray used by the firing client.
        // Use it on every replica so a smoothed puppet's current aim does not
        // redirect the projectile after its origin has already been corrected.
        if (const auto event = NetShotEvents::FiringRay(player); event.has_value())
        {
            return event->Direction.Normalized();
        }
        const std::int32_t slot = player.SlotIndex();
        if (!NetSession::IsAuthority() || !NetPlayerBridge::AimTrusted(slot))
        {
            return current;
        }
        const IntentPacket& intent = NetSession::RemoteIntent(static_cast<std::size_t>(slot));
        if (intent.HasShot && OpenTK::Mathematics::LengthSquared(intent.ShotOrigin - intent.Position) < 9.0F)
        {
            return intent.ShotDirection.Normalized();
        }
        const OpenTK::Mathematics::Vector3 aim = intent.Aim;
        const float lengthSquared = aim.X * aim.X + aim.Y * aim.Y + aim.Z * aim.Z;
        if (lengthSquared > 0.0001F)
        {
            return aim.Normalized();
        }
        return current;
    }

    bool NetHooks::TryApplyRemoteInput(
        Entities::PlayerEntity& player, std::int32_t slot)
    {
        if (!NetSession::Active() || slot == LocalSlot())
        {
            return false;
        }
        if (TestFlag(player.LoadFlags(), Entities::LoadFlags::Spawned) && player.Health() > 0
            && NetRoomChange::GameplayReady() && SnapshotPositions()
            && ::MphRead::Mods::Network::NetSession::HasRemoteState(slot))
        {
            NetPlayerBridge::RestoreSnapshotPosition(player,
                ::MphRead::Mods::Network::NetSession::RemoteState(static_cast<std::size_t>(slot)));
        }
        if (TestFlag(player.LoadFlags(), Entities::LoadFlags::Active)
            && ::MphRead::Mods::Network::NetSession::HasRemoteIntent(slot))
        {
            if (TestFlag(player.LoadFlags(), Entities::LoadFlags::Spawned)
                && player.Health() > 0
                && NetRoomChange::GameplayReady()
                && !SnapshotPositions()
                && NetSession::RemoteIntentAge(slot) <= StaleIntentFrames)
            {
                NetPlayerBridge::ApplyReportedPosition(
                    player, ::MphRead::Mods::Network::NetSession::RemoteIntent(static_cast<std::size_t>(slot)));
            }
            NetPlayerBridge::ApplyIntent(
                player, ::MphRead::Mods::Network::NetSession::RemoteIntent(static_cast<std::size_t>(slot)));
        }
        return true;
    }

    bool NetHooks::ForceSpawn(Entities::PlayerEntity& player)
    {
        if (MapAudit::ForceEveryone())
        {
            return true;
        }
        if (!NetSession::Active())
        {
            return false;
        }
        const std::int32_t slot = player.SlotIndex();
        if (slot < 0 || static_cast<std::size_t>(slot) >= SessionRoster::SlotCapacity)
        {
            return false;
        }
        if (!NetSession::IsAuthority())
        {
            return false;
        }
        return slot == NetSession::LocalSlot()
            || SessionSlots().IsOccupied(slot);
    }

    void NetHooks::AfterInput(MphRead::Scene& scene)
    {
        if (!NetSession::Active())
        {
            return;
        }

        NetTimingDiagnostics::Simulation();
        NetMatchEnd::Sync();
        NetRoomChange::Sync(scene);
        NetDiagnostics::Report(static_cast<double>(NetSession::NetFrame()) / 60.0);
        NetPlayerSetup::ApplyOnce();
        NetMatchSync::Apply();
        NetSlotManager::Sync();
        if (NetSession::IsClient() && !NetSession::IsAuthority() && NetRoomChange::GameplayReady())
        {
            ApplyRemoteStates();
        }
        PlayerColors::Resolve();
        NetLog::Snapshot(static_cast<double>(NetSession::NetFrame()) / 60.0, scene);

        if (NetSession::IsAuthority() && NetSession::ConsumeAuthorityStateSync())
        {
            ApplyRemoteStates();
        }
        if (NetSession::IsClient() && !NetSession::IsAuthority() && NetRoomChange::GameplayReady())
        {
            const auto& players = Entities::PlayerEntity::Players();
            for (std::int32_t slot = 0; slot < static_cast<std::int32_t>(players.size()); ++slot)
            {
                const std::shared_ptr<Entities::PlayerEntity>& bot = players[static_cast<std::size_t>(slot)];
                if (slot != NetSession::LocalSlot() && bot != nullptr && bot->IsBot()
                    && TestFlag(bot->LoadFlags(), Entities::LoadFlags::Active)
                    && NetSession::HasRemoteIntent(slot))
                {
                    // The entity input loop runs local AI for bot slots. Apply
                    // the authority's intent after that loop and before the
                    // simulation consumes the controls.
                    NetPlayerBridge::ApplyIntent(*bot, NetSession::RemoteIntent(static_cast<std::size_t>(slot)));
                }
            }
        }
        if (NetSession::IsAuthority() && NetRoomChange::GameplayReady() && ServerBots::Count() > 0)
        {
            const auto& players = Entities::PlayerEntity::Players();
            for (std::int32_t slot = 0; slot < static_cast<std::int32_t>(players.size()); ++slot)
            {
                const std::shared_ptr<Entities::PlayerEntity>& bot = players[static_cast<std::size_t>(slot)];
                if (ServerBots::Holds(slot) && bot != nullptr
                    && TestFlag(bot->LoadFlags(), Entities::LoadFlags::Active))
                {
                    // AI input edges are sampled after PlayerAi::ProcessInput
                    // and before the game consumes them, once per simulation frame.
                    NetInputHistory::Record(*bot);
                    if (NetLog::Enabled() && bot->Controls().AltAttack().IsPressed())
                    {
                        NetLog::Event("[bot-input] captured alt-attack slot=" + std::to_string(slot)
                            + " frame=" + std::to_string(NetSession::NetFrame())
                            + " form=" + bot->ModFormState());
                    }
                }
            }
        }
        if (NetSession::LocalSlot() < 0 || !NetSession::IsClient() || !NetRoomChange::GameplayReady())
        {
            return;
        }

        const std::int32_t local = NetSession::LocalSlot();
        const std::shared_ptr<Entities::PlayerEntity> player
            = local < static_cast<std::int32_t>(Entities::PlayerEntity::Players().size())
            ? Entities::PlayerEntity::Players().at(static_cast<std::size_t>(local))
            : nullptr;
        if (player != nullptr
            && TestFlag(player->LoadFlags(), Entities::LoadFlags::Active))
        {
            const bool spectatorView = Mods::SpectatorMode::IsSpectating();
            if (spectatorView)
            {
                // Keep local controls out of simulation while the view request
                // is pending; the replicated PlayerEntity flag stays authority-owned.
                player->Controls().ClearAll();
            }
            else
            {
                HitRig::SetScene(&scene);
                NetPlayerBridge::SetScene(&scene);
                NetTestScript::Apply(player);
                NetPlayerBridge::RecordPresses(*player);
            }
            if (NetSession::NetFrame() % NetConfig::IntentSendInterval == 0)
            {
                // Captured now, before the simulation, so the position and
                // the buttons are the ones the frame starts from; sent after
                // it (AfterSimulation), carrying the ray the frame fired.
                if (_intentPending)
                {
                    NetSession::SendIntent(_pendingIntent);
                }
                _pendingIntent = NetPlayerBridge::CaptureIntent(*player);
                _pendingIntent.Frame = std::max(1U, NetSession::NetFrame());
                _intentPending = true;
            }
        }
        else
        {
            NetSession::SendIntent(IntentPacket{});
        }
    }

    void NetHooks::AfterSimulation(Scene& scene)
    {
        if (!NetSession::Active() || !NetRoomChange::GameplayReady())
        {
            return;
        }
        if (_intentPending)
        {
            _intentPending = false;
            NetPlayerBridge::AttachLocalShot(_pendingIntent);
            NetShotEvents::Attach(_pendingIntent, NetSession::LocalSlot());
            NetBombs::AttachFor(NetSession::LocalSlot(), _pendingIntent, _pendingIntent.Frame);
            NetSession::SendIntent(_pendingIntent);
        }
        if (!NetSession::IsAuthority())
        {
            NetPlayerBridge::TickConfirms();
        }
        NetSmoothing::Tick();

        for (std::int32_t i = 0;
            i < static_cast<std::int32_t>(Entities::PlayerEntity::Players().size());
            ++i)
        {
            Entities::PlayerEntity& player = RequireReference(
                Entities::PlayerEntity::Players().at(static_cast<std::size_t>(i)));
            if (TestFlag(player.LoadFlags(), Entities::LoadFlags::Active))
            {
                player.ModRecordNetworkPosition(NetSession::NetFrame());
                player.ModRepairVectors();
            }
        }

        if (NetSession::IsAuthority())
        {
            NetSession::BroadcastSnapshot(scene);
        }
        else if (NetSession::IsClient())
        {
            ApplyRemoteStates();
        }
    }

    void NetHooks::ApplyRemoteStates()
    {
        if (!NetRoomChange::GameplayReady())
        {
            return;
        }
        // Deaths first: a snapshot that brings the authority's own count of a
        // kill shown here must not also have it added again, whichever of the
        // two players it happens to apply first.
        for (std::int32_t i = 0; i < static_cast<std::int32_t>(Entities::PlayerEntity::Players().size()); ++i)
        {
            if (::MphRead::Mods::Network::NetSession::HasRemoteState(i))
            {
                const PlayerState& state = ::MphRead::Mods::Network::NetSession::RemoteState(static_cast<std::size_t>(i));
                if (state.Health == 0 || (state.Flags & PlayerState::FlagSpawned) == 0)
                {
                    NetHitPrediction::ForgetShownKill(i);
                }
            }
        }
        for (std::int32_t i = 0;
            i < static_cast<std::int32_t>(Entities::PlayerEntity::Players().size());
            ++i)
        {
            if (!::MphRead::Mods::Network::NetSession::HasRemoteState(i))
            {
                continue;
            }
            Entities::PlayerEntity& player = RequireReference(
                Entities::PlayerEntity::Players().at(static_cast<std::size_t>(i)));
            if (TestFlag(player.LoadFlags(), Entities::LoadFlags::Active))
            {
                NetPlayerBridge::ApplyState(
                    player,
                    ::MphRead::Mods::Network::NetSession::RemoteState(static_cast<std::size_t>(i)),
                    i == NetSession::LocalSlot());
            }
        }
        NetSession::NoteStatesApplied();
    }
}
