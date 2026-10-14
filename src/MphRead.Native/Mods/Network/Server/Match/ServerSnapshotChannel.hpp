#pragma once

#include "Mods/Network/Server/Core/ServerAuthorityState.hpp"
#include "Mods/Network/Server/Core/ServerPeerRegistry.hpp"
#include "Mods/Network/Server/Core/ServerSlotIdentity.hpp"
#include "Mods/Network/Server/Lobby/ServerLobbyState.hpp"
#include "Mods/Network/Server/Match/ServerSimulationController.hpp"
#include "Mods/Network/Server/Match/ServerSnapshotStream.hpp"

#include <cstdint>
#include <span>

namespace MphRead::Mods::Network
{
    class NetTransport;
    struct ReceivedPacket;

    // Owns snapshot ingress validation/relay and authoritative snapshot
    // publication while ServerSnapshotStream owns retained bytes and ordering.
    class ServerSnapshotChannel final
    {
    public:
        ServerSnapshotChannel(ServerPeerRegistry& peers, ServerAuthorityState& authority,
            ServerSlotIdentity& slots, ServerSnapshotStream& stream,
            ServerSimulationController& simulation, ServerLobbyState& lobby,
            std::uint16_t& matchId) noexcept;

        void Receive(const ReceivedPacket& packet, double now, NetTransport* transport);
        void Publish(std::span<const std::uint8_t> payload, NetTransport* transport);
        void SendLatest(const ServerPeerRegistry::PeerHandle& peer, NetTransport* transport) const;


    private:
        ServerPeerRegistry& _peers;
        ServerAuthorityState& _authority;
        ServerSlotIdentity& _slots;
        ServerSnapshotStream& _stream;
        ServerSimulationController& _simulation;
        ServerLobbyState& _lobby;
        std::uint16_t& _matchId;
    };
}
