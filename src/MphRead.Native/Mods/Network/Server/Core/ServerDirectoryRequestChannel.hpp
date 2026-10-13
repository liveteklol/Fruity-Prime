#pragma once

#include "Mods/Network/Domain/MatchDefinition.hpp"
#include "Mods/Network/Protocol/NetProtocol.hpp"
#include "Mods/Network/Protocol/Packets/SessionPackets.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <string>

namespace System::Net
{
    class IPEndPoint;
}

namespace MphRead::Mods::Network
{
    class NetTransport;
    class HostPool;
    class ServerLobbyState;
    class ServerStateBroadcastChannel;
    struct ReceivedPacket;

    // Answers directory-facing status and host-game requests. Packet policy and
    // serialization stay out of the server lifetime runner.
    class ServerDirectoryRequestChannel final
    {
    public:
        using ScratchBuffer = std::array<std::uint8_t, NetConfig::MaxPacketSize>;

        struct Ports final
        {
            ServerLobbyState* Lobby = nullptr;
            ServerStateBroadcastChannel* State = nullptr;
            HostPool* Hosts = nullptr;
            std::unique_ptr<NetTransport>* Transport = nullptr;
            ScratchBuffer* Scratch = nullptr;
            std::int32_t MaxPlayers = 0;
            const std::string* ServerName = nullptr;
            void* Context = nullptr;
            MatchDefinition (*CurrentDefinition)(void*) = nullptr;
            void (*WriteLog)(void*, const std::string&) = nullptr;
        };

        explicit ServerDirectoryRequestChannel(Ports ports) noexcept;

        void SendStatus(const std::shared_ptr<System::Net::IPEndPoint>& sender, double now);
        void HandleHostRequest(const ReceivedPacket& packet, double now);

    private:
        [[nodiscard]] MatchDefinition CurrentDefinition() const;
        void Log(const std::string& message) const;

        Ports _ports;
    };
}
