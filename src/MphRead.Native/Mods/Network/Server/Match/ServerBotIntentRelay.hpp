#pragma once

#include "Mods/Network/Protocol/NetProtocol.hpp"

#include <array>
#include <cstdint>

namespace MphRead::Mods::Network
{
    class NetTransport;
    class ServerPeerRegistry;

    // Serializes the authority's bot actions into the SlotIntent stream.
    // Bot policy and the server's socket/peer lifetimes stay with their owners.
    class ServerBotIntentRelay final
    {
    public:
        void RelayFrame(std::uint32_t frame, std::uint16_t matchId, std::uint64_t authorityEpoch,
            std::int32_t maxPlayers,
            NetTransport* transport, const ServerPeerRegistry& peers);

    private:
        std::uint32_t _lastFrame = 0;
        std::array<std::uint8_t, NetConfig::MaxPacketSize> _scratch{};
    };
}
