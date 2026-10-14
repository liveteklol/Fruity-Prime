#include "Mods/Network/Server/Match/ServerSnapshotChannel.hpp"

#include "Mods/Network/Server/Match/ServerSnapshotValidator.hpp"
#include "Mods/Network/Transport/NetTransport.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace MphRead::Mods::Network
{
    ServerSnapshotChannel::ServerSnapshotChannel(ServerPeerRegistry& peers,
        ServerAuthorityState& authority, ServerSlotIdentity& slots, ServerSnapshotStream& stream,
        ServerSimulationController& simulation, ServerLobbyState& lobby,
        std::uint16_t& matchId) noexcept
        : _peers(peers),
          _authority(authority),
          _slots(slots),
          _stream(stream),
          _simulation(simulation),
          _lobby(lobby),
          _matchId(matchId)
    {
    }

    void ServerSnapshotChannel::Receive(const ReceivedPacket& packet, double now, NetTransport* transport)
    {
        // Snapshots are emitted by the server simulation only. A client can
        // submit firing intents and hit claims, but cannot publish world state.
    }

    void ServerSnapshotChannel::Publish(std::span<const std::uint8_t> payload, NetTransport* transport)
    {
        static_cast<void>(_stream.CacheLatest(payload));
        for (const ServerPeerRegistry::PeerHandle& peer : _peers.All())
        {
            if (transport != nullptr)
            {
                transport->Send(peer->EndPoint, PacketType::Snapshot, payload);
            }
        }
    }

    void ServerSnapshotChannel::SendLatest(
        const ServerPeerRegistry::PeerHandle& peer, NetTransport* transport) const
    {
        if (peer != nullptr && _stream.HasLatest() && transport != nullptr)
        {
            transport->Send(peer->EndPoint, PacketType::Snapshot, _stream.Latest());
        }
    }

}
