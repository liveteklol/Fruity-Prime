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
        if (_lobby.Phase() == SessionPhase::Lobby || _lobby.Phase() == SessionPhase::Starting)
        {
            return;
        }

        const ServerPeerRegistry::PeerHandle peer = _peers.Find(packet.Sender);
        if (peer == nullptr)
        {
            return;
        }

        peer->Liveness.MarkSeen(now);
        if (_simulation.Running() || !_authority.IsAuthority(peer))
        {
            return;
        }

        const std::span<const std::uint8_t> payload = packet.Payload();
        const std::optional<ValidatedServerSnapshot> snapshot = ServerSnapshotValidator::Validate(
            payload, _matchId, _authority.Epoch(), _slots);
        if (!snapshot.has_value() || !_stream.AcceptIncoming(snapshot->Header.Frame, payload))
        {
            return;
        }

        for (std::size_t i = 0; i < snapshot->SlotCount; ++i)
        {
            _slots.SetLife(snapshot->SlotLives[i].SlotIndex, snapshot->SlotLives[i].LifeId);
        }
        for (const ServerPeerRegistry::PeerHandle& other : _peers.All())
        {
            if (other != peer && transport != nullptr)
            {
                transport->Send(other->EndPoint, PacketType::Snapshot, payload);
            }
        }
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

    void ServerSnapshotChannel::SendAuthoritySeed(
        const ServerPeerRegistry::PeerHandle& peer, NetTransport* transport) const
    {
        if (peer == nullptr || !_stream.HasLatest() || transport == nullptr)
        {
            return;
        }

        const std::span<const std::uint8_t> latest = _stream.Latest();
        SnapshotHeader header = SnapshotHeader::Read(latest);
        if (header.MatchId != _matchId)
        {
            return;
        }

        std::vector<std::uint8_t> seed(latest.begin(), latest.end());
        header.AuthorityEpoch = _authority.Epoch();
        header.Frame = 0;
        header.Write(seed);
        transport->Send(peer->EndPoint, PacketType::Snapshot, seed);
    }
}
