#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string>

namespace MphRead::Mods::Network
{
    class NetTransport;
    class ServerPeerRegistry;
    struct ReceivedPacket;
    struct ServerPeer;

    // Owns server-side chat validation, policy, and packet relay.
    class ServerChatChannel final
    {
    public:
        using PeerHandle = std::shared_ptr<ServerPeer>;
        using LogSink = void (*)(const std::string& message);

        ServerChatChannel() = delete;

        static void Tell(NetTransport* transport, std::span<std::uint8_t> scratch,
            const PeerHandle& peer, const std::string& text);
        static void Receive(const ReceivedPacket& packet, double now,
            ServerPeerRegistry& peers, NetTransport* transport,
            std::span<std::uint8_t> scratch, bool teamMode, LogSink log);
        static void Announce(ServerPeerRegistry& peers, NetTransport* transport,
            std::span<std::uint8_t> scratch, const std::string& text);
    };
}
