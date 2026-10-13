#include "Mods/Network/Session/SessionPeerMaintenance.hpp"

#include "Mods/Network/Session/PeerRegistry.hpp"
#include "Mods/Network/Session/RemotePlayerChannel.hpp"
#include "Mods/Network/Gameplay/Player/NetPlayerLifecycle.hpp"
#include "NativeRuntime/System/Console.hpp"
#include "NativeRuntime/System/Globalization.hpp"

namespace MphRead::Mods::Network
{
    namespace Runtime = ::MphRead::NativeRuntime;

    void SessionPeerMaintenance::HandleBye(ReceivedPacket packet, const Ports& ports)
    {
        if (ports.role == NetRole::Host)
        {
            const std::shared_ptr<PeerRegistry::Peer> peer = SessionPeers().FindByEndpoint(packet.Sender);
            if (peer != nullptr)
            {
                Runtime::ConsoleWriteLine("[net] peer " + peer->Endpoint()->ToString()
                    + " left (slot " + Runtime::ToString(peer->SlotIndex()) + ")");
                RemotePlayers().InvalidateIntent(peer->SlotIndex());
                (void)SessionPeers().Remove(peer);
                static_cast<void>(NetPlayerLifecycle::SetOccupant(peer->SlotIndex(), 0));
                ports.broadcastHostControl();
            }
        }
        else
        {
            Runtime::ConsoleWriteLine("[net] host closed the session");
            ports.stopSession();
        }
    }

    void SessionPeerMaintenance::DropTimedOut(double time, const Ports& ports)
    {
        for (std::int32_t index = static_cast<std::int32_t>(SessionPeers().Count()) - 1; index >= 0; --index)
        {
            const std::shared_ptr<PeerRegistry::Peer> peer = SessionPeers().At(static_cast<std::size_t>(index));
            if (time - peer->LastSeenTime() > NetConfig::TimeoutSeconds)
            {
                Runtime::ConsoleWriteLine("[net] peer " + peer->Endpoint()->ToString()
                    + " timed out (slot " + Runtime::ToString(peer->SlotIndex()) + ")");
                RemotePlayers().InvalidateIntent(peer->SlotIndex());
                (void)SessionPeers().Remove(peer);
                static_cast<void>(NetPlayerLifecycle::SetOccupant(peer->SlotIndex(), 0));
                ports.broadcastHostControl();
            }
        }
    }
}
