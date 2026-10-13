#pragma once

#include "Mods/Network/Protocol/ConnectionSecret.hpp"

#include <cstdint>

namespace MphRead::Mods::Network
{
    class ClientLobbyState;
    class SessionConnection;
    class SessionPlayerIdentity;
    class SessionRuntimeState;

    // Encodes the client's authenticated Hello and identity announcement packets.
    class SessionClientHandshake final
    {
    public:
        SessionClientHandshake() = delete;

        [[nodiscard]] static std::uint32_t NewClientId();
        [[nodiscard]] static ConnectionSecret NewSecret();
        static void SendHello(SessionConnection& connection,
            const SessionRuntimeState& runtime, const ClientLobbyState& lobby,
            std::uint32_t clientId, const ConnectionSecret& secret);
        static void SendIdentify(SessionConnection& connection,
            const SessionPlayerIdentity& identity);
    };
}
