#include "NetHooks.hpp"
#include "NetBombs.hpp"
#include "HitRig.hpp"
#include "NetHitPrediction.hpp"

#include "DemoPlayback.hpp"
#include "MapAudit.hpp"
#include "NetDiagnostics.hpp"
#include "NetLog.hpp"
#include "NetMatchEnd.hpp"
#include "NetMatchSync.hpp"
#include "NetPlayerBridge.hpp"
#include "NetShotEvents.hpp"
#include "NetPlayerSetup.hpp"
#include "NetProtocol.hpp"
#include "NetRoomChange.hpp"
#include "NetSession.hpp"
#include "NetSlotManager.hpp"
#include "NetSmoothing.hpp"
#include "NetTimingDiagnostics.hpp"
#include "NetTestScript.hpp"
#include "PlayerColors.hpp"
#include "../SpectatorMode.hpp"
#include "../../Entities/Players/PlayerEntity.hpp"
#include "../../Scene.hpp"
#include "../../NativeRuntime/System/Managed.hpp"
#include "../../Formats/Types.hpp"

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
        return _snapshotOwnsPuppets && !NetSession::IsAuthority() && !NetSession::IsHost()
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
        if (slot < 0 || static_cast<std::size_t>(slot) >= NetSession::RemoteIntents.size())
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
            if (NetSession::RemoteStateValid[s])
            {
                NetTimingDiagnostics::Position(slot, true);
                NetPlayerBridge::RestoreSnapshotPosition(player, NetSession::RemoteStates[s]);
            }
            return;
        }
        if (!NetSession::IsAuthority() && !_pinPuppetsOnClients)
        {
            return;
        }
        if (!NetSession::RemoteIntentValid[s])
        {
            return;
        }
        if (!NetSession::IsHost() && !NetSession::IsAuthority())
        {
            NetTimingDiagnostics::Position(slot, false);
        }
        NetPlayerBridge::RestoreReportedPosition(player, NetSession::RemoteIntents[s]);
    }

    OpenTK::Mathematics::Vector3 NetHooks::RemoteShotOrigin(
        Entities::PlayerEntity& player, OpenTK::Mathematics::Vector3 current)
    {
        if (!NetSession::IsAuthority() || player.SlotIndex() == NetSession::LocalSlot()
            || player.SlotIndex() < 0
            || static_cast<std::size_t>(player.SlotIndex()) >= NetSession::RemoteIntents.size())
        {
            return current;
        }
        // The shot event being fired: its own ray, however late it arrived.
        if (const auto event = NetShotEvents::FiringRay(player); event.has_value())
        {
            return event->Origin;
        }
        const IntentPacket& intent = NetSession::RemoteIntents.at(static_cast<std::size_t>(player.SlotIndex()));
        // The shooter's own ray, when the intent that pulled this trigger
        // carries it -- but never one far from where the authority has them.
        if (intent.HasShot && OpenTK::Mathematics::LengthSquared(intent.ShotOrigin - intent.Position) < 9.0F)
        {
            return intent.ShotOrigin;
        }
        return current
            + NetSession::RemoteIntents.at(static_cast<std::size_t>(player.SlotIndex())).Position
            - player.Position;
    }

    OpenTK::Mathematics::Vector3 NetHooks::DrawnRemoteShot(
        Entities::PlayerEntity& player, OpenTK::Mathematics::Vector3 origin, OpenTK::Mathematics::Vector3 current)
    {
        if (!NetSession::Active() || NetSession::IsAuthority() || NetSession::IsHost()
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
        if (NetSession::IsAuthority() && player.SlotIndex() != NetSession::LocalSlot()
            && player.SlotIndex() >= 0
            && static_cast<std::size_t>(player.SlotIndex()) < NetSession::RemoteIntents.size()
            && NetPlayerBridge::AimTrusted(player.SlotIndex()))
        {
            if (const auto event = NetShotEvents::FiringRay(player); event.has_value())
            {
                return event->Direction.Normalized();
            }
            const IntentPacket& intent = NetSession::RemoteIntents.at(static_cast<std::size_t>(player.SlotIndex()));
            if (intent.HasShot && OpenTK::Mathematics::LengthSquared(intent.ShotOrigin - intent.Position) < 9.0F)
            {
                return intent.ShotDirection.Normalized();
            }
            const OpenTK::Mathematics::Vector3 aim
                = NetSession::RemoteIntents.at(
                    static_cast<std::size_t>(player.SlotIndex())).Aim;
            const float lengthSquared = aim.X * aim.X + aim.Y * aim.Y + aim.Z * aim.Z;
            if (lengthSquared > 0.0001F)
            {
                return aim.Normalized();
            }
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
            && ::MphRead::NativeRuntime::ManagedAt(NetSession::RemoteStateValid, slot))
        {
            NetPlayerBridge::RestoreSnapshotPosition(player,
                ::MphRead::NativeRuntime::ManagedAt(NetSession::RemoteStates, slot));
        }
        if (TestFlag(player.LoadFlags(), Entities::LoadFlags::Active)
            && ::MphRead::NativeRuntime::ManagedAt(NetSession::RemoteIntentValid, slot))
        {
            if (TestFlag(player.LoadFlags(), Entities::LoadFlags::Spawned)
                && player.Health() > 0
                && NetRoomChange::GameplayReady()
                && !SnapshotPositions()
                && NetSession::RemoteIntentAge(slot) <= StaleIntentFrames)
            {
                NetPlayerBridge::ApplyReportedPosition(
                    player, ::MphRead::NativeRuntime::ManagedAt(NetSession::RemoteIntents, slot));
            }
            NetPlayerBridge::ApplyIntent(
                player, ::MphRead::NativeRuntime::ManagedAt(NetSession::RemoteIntents, slot));
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
        if (slot < 0 || static_cast<std::size_t>(slot) >= NetSession::SlotOccupied.size())
        {
            return false;
        }
        if (!NetSession::IsHost() && !NetSession::IsAuthority())
        {
            return false;
        }
        return slot == NetSession::LocalSlot()
            || NetSession::SlotOccupied.at(static_cast<std::size_t>(slot));
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
            if (Mods::SpectatorMode::IsSpectating())
            {
                player->ModSetSpectating(true);
            }
            if (!Mods::SpectatorMode::IsSpectating())
            {
                HitRig::SetScene(&scene);
                NetPlayerBridge::SetScene(&scene);
                NetTestScript::Apply(player);
            }
            NetPlayerBridge::RecordPresses(*player);
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

    void NetHooks::AfterSimulation()
    {
        if (!NetSession::Active() || !NetRoomChange::GameplayReady())
        {
            return;
        }
        if (_intentPending)
        {
            _intentPending = false;
            NetPlayerBridge::AttachLocalShot(_pendingIntent);
            NetShotEvents::Attach(_pendingIntent);
            NetBombs::Attach(_pendingIntent);
            NetSession::SendIntent(_pendingIntent);
        }
        if (!NetSession::IsAuthority() && !NetSession::IsHost())
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

        if (NetSession::IsHost() || NetSession::IsAuthority())
        {
            NetSession::BroadcastSnapshot();
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
            if (::MphRead::NativeRuntime::ManagedAt(NetSession::RemoteStateValid, i))
            {
                const PlayerState& state = ::MphRead::NativeRuntime::ManagedAt(NetSession::RemoteStates, i);
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
            if (!::MphRead::NativeRuntime::ManagedAt(NetSession::RemoteStateValid, i))
            {
                continue;
            }
            Entities::PlayerEntity& player = RequireReference(
                Entities::PlayerEntity::Players().at(static_cast<std::size_t>(i)));
            if (TestFlag(player.LoadFlags(), Entities::LoadFlags::Active))
            {
                NetPlayerBridge::ApplyState(
                    player,
                    ::MphRead::NativeRuntime::ManagedAt(NetSession::RemoteStates, i),
                    i == NetSession::LocalSlot());
            }
        }
        NetSession::NoteStatesApplied();
    }
}
