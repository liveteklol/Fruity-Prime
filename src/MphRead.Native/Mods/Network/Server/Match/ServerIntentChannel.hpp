#pragma once

#include "Mods/Network/Protocol/NetProtocol.hpp"
#include "Mods/Network/Server/Core/ServerAuthorityState.hpp"
#include "Mods/Network/Server/Core/ServerPeerRegistry.hpp"
#include "Mods/Network/Server/Core/ServerSlotIdentity.hpp"
#include "Mods/Network/Server/Lobby/ServerLobbyState.hpp"
#include "Mods/Network/Server/Match/ServerSimulationController.hpp"
#include "Mods/Network/Session/SessionPorts.hpp"

#include <array>
#include <cstdint>

namespace MphRead::Mods::Network
{
    class NetTransport;
    struct ReceivedPacket;

    // Validates an authenticated peer's intents, applies authority-local input,
    // and relays accepted intent history to the other connected peers.
    class ServerIntentChannel final
    {
    public:
        using ScratchBuffer = std::array<std::uint8_t, NetConfig::MaxPacketSize>;

        ServerIntentChannel(ServerPeerRegistry& peers, ServerAuthorityState& authority,
            ServerSlotIdentity& slots, ServerSimulationController& simulation,
            DedicatedServerSessionPort& session, ServerLobbyState& lobby,
            std::uint16_t& matchId, ScratchBuffer& scratch) noexcept;

        void Receive(const ReceivedPacket& packet, double now, NetTransport* transport);

    private:
        ServerPeerRegistry& _peers;
        ServerAuthorityState& _authority;
        ServerSlotIdentity& _slots;
        ServerSimulationController& _simulation;
        DedicatedServerSessionPort& _session;
        ServerLobbyState& _lobby;
        std::uint16_t& _matchId;
        ScratchBuffer& _scratch;
    };
}
