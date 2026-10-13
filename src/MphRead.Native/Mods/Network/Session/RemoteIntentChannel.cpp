#include "Mods/Network/Session/RemoteIntentChannel.hpp"

#include "Mods/Network/Session/ClientReplicaStream.hpp"
#include "Mods/Network/Session/NetLifecycleTracker.hpp"
#include "Mods/Network/Session/PeerRegistry.hpp"
#include "Mods/Network/Session/RemotePlayerChannel.hpp"
#include "Mods/Network/Session/SessionTrafficMetrics.hpp"
#include "Mods/Network/Transport/NetTransport.hpp"
#include "Mods/Network/Protocol/Packets/RealtimePackets.hpp"
#include "Mods/Network/Gameplay/Player/NetPlayerLifecycle.hpp"
#include "Mods/Network/Gameplay/Claims/Hit/NetHitClaims.hpp"
#include "Mods/Network/Gameplay/Bombs/NetBombs.hpp"
#include "Mods/Network/Gameplay/Weapons/NetShotEvents.hpp"
#include "Mods/Network/Diagnostics/NetLog.hpp"
#include "Mods/Network/Diagnostics/NetShotDiagnostics.hpp"
#include "Mods/Network/Domain/ShotKey.hpp"
#include "NativeRuntime/System/Managed.hpp"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <span>
#include <string>

using ::MphRead::NativeRuntime::HasFlag;

namespace MphRead::Mods::Network
{
    void RemoteIntentChannel::ReceiveHostIntent(const ReceivedPacket& packet, double time, std::uint32_t frame)
    {
        const std::span<const std::uint8_t> payload = packet.Payload();
        if (payload.size() < IntentPacket::Size)
        {
            return;
        }
        std::shared_ptr<PeerRegistry::Peer> peer = SessionPeers().FindByEndpoint(packet.Sender);
        if (peer == nullptr || peer->SlotIndex() < 0)
        {
            return;
        }
        const IntentPacket intent = IntentPacket::Read(payload);
        if (!NetPlayerLifecycle::AcceptIntent(peer->SlotIndex(), intent))
        {
            return;
        }
        // Shot and bomb events may have arrived on an older intent frame;
        // consume their sequence data before rejecting stale player state.
        NetShotEvents::Receive(peer->SlotIndex(), intent);
        NetBombs::Receive(peer->SlotIndex(), intent);
        if (peer->LastIntentFrame() != 0 && !NetLifecycleTracker::Newer(intent.Frame, peer->LastIntentFrame()))
        {
            return;
        }
        peer->RecordLatestIntent(intent, time);
        NetHitClaims::RecordIntent(peer->SlotIndex(), intent);
        RemotePlayers().StoreIntent(peer->SlotIndex(), intent, std::max(frame, 1U));
    }

    void RemoteIntentChannel::ReceiveAuthorityIntent(const ReceivedPacket& packet, std::int32_t localSlot,
        std::uint32_t frame, ClientReplicaStream& replicaStream, SessionTrafficMetrics& traffic)
    {
        const std::span<const std::uint8_t> payload = packet.Payload();
        if (payload.size() < 1 + IntentPacket::Size)
        {
            return;
        }
        const std::int32_t slot = payload[0];
        if (slot < 0 || slot >= static_cast<std::int32_t>(RemotePlayerChannel::SlotCapacity) || slot == localSlot)
        {
            return;
        }
        AcceptAuthorityIntent(slot, IntentPacket::Read(payload.subspan(1)), localSlot, frame,
            replicaStream, traffic);
    }

    void RemoteIntentChannel::AcceptAuthorityIntent(std::int32_t slot, IntentPacket intent,
        std::int32_t localSlot, std::uint32_t frame, ClientReplicaStream& replicaStream,
        SessionTrafficMetrics& traffic)
    {
        if (slot < 0 || slot >= static_cast<std::int32_t>(RemotePlayerChannel::SlotCapacity) || slot == localSlot)
        {
            return;
        }
        if (!NetPlayerLifecycle::AcceptIntent(slot, intent))
        {
            return;
        }
        // Preserve the event-before-state ordering for reordered datagrams.
        NetShotEvents::Receive(slot, intent);
        NetBombs::Receive(slot, intent);
        if (!replicaStream.AcceptIntent(slot, intent.Frame))
        {
            return;
        }
        NetHitClaims::RecordIntent(slot, intent);
        RemotePlayers().StoreIntent(slot, intent, std::max(frame, 1U));
        traffic.RecordIntentReceived();
        if (NetLog::Enabled() && HasFlag(intent.Buttons, IntentButtons::Shoot))
        {
            NetShotDiagnostics::Trace("intent", ShotKey::For(slot, intent.AckFrame),
                static_cast<BeamType>(intent.WeaponSelect),
                "intentFrame=" + std::to_string(intent.Frame) + " intentLife=" + std::to_string(intent.LifeId)
                + " inPlay=" + (HasFlag(intent.Buttons, IntentButtons::InPlayState) ? "True" : "False")
                + " shoot=true");
        }
    }
}
