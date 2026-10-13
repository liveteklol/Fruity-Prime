#pragma once

namespace MphRead::Mods::Network
{
    class DedicatedServer;
    struct ReceivedPacket;

    // Routes server datagrams to the server's connection, lobby and match services.
    class ServerPacketDispatcher final
    {
    public:
        ServerPacketDispatcher() = delete;

        static void Dispatch(DedicatedServer& server, const ReceivedPacket& packet, double now);
    };
}
