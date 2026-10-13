#include "Mods/Network/Server/Core/ServerPeerIdentityChannel.hpp"

#include "Mods/Network/Server/Core/ServerPeerIdentity.hpp"
#include "Mods/Network/Server/Lobby/ServerLobbyState.hpp"
#include "Mods/Network/Transport/NetTransport.hpp"

namespace MphRead::Mods::Network
{
    std::optional<ServerPeerIdentityChannel::Change> ServerPeerIdentityChannel::Receive(
        const ReceivedPacket& packet, double now, ServerPeerRegistry& peers,
        const ServerLobbyState& lobby)
    {
        const ServerPeerRegistry::PeerHandle peer = peers.Find(packet.Sender);
        if (peer == nullptr)
        {
            return std::nullopt;
        }

        peer->Liveness.MarkSeen(now);
        if (lobby.Phase() == SessionPhase::Starting
            && lobby.IsExpectedLoadedSlot(peer->SlotIndex))
        {
            return std::nullopt;
        }

        const std::optional<ServerPlayerIdentity> identity = ServerPeerIdentity::Parse(packet.Payload());
        if (!identity.has_value())
        {
            return std::nullopt;
        }

        const ServerPeerProfile::Update update = peer->Profile.Apply(*identity);
        if (!update.Changed)
        {
            return std::nullopt;
        }
        if (update.AppearanceChanged)
        {
            peer->Lobby.InvalidateLobbyReady();
        }
        return Change{peer, update.FirstName};
    }
}
