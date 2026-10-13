#pragma once

#include "Mods/Network/Domain/MatchDefinition.hpp"
#include "Mods/Network/Protocol/NetProtocol.hpp"
#include "Mods/Network/Server/Core/ServerAuthorityState.hpp"
#include "Mods/Network/Server/Core/ServerPeerRegistry.hpp"
#include "Mods/Network/Server/Core/ServerSlotIdentity.hpp"
#include "Mods/Network/Server/Lobby/ServerLobbyState.hpp"
#include "Mods/Network/Server/Match/ServerSnapshotStream.hpp"

#include <array>
#include <atomic>
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
    class ServerLobbySession;
    class ServerMapVoting;
    class ServerSnapshotChannel;
    class ServerSimulationController;
    class ServerStateBroadcastChannel;
    struct ReceivedPacket;

    // Owns peer admission, rebinding, identity refresh and removal effects.
    // Protocol, peer storage, slot generations and state publication stay with
    // their respective owners and are reached only through these ports.
    class ServerPeerConnectionChannel final
    {
    public:
        using ScratchBuffer = std::array<std::uint8_t, NetConfig::MaxPacketSize>;
        using PeerHandle = ServerPeerRegistry::PeerHandle;

        struct Ports final
        {
            ServerPeerRegistry* Peers = nullptr;
            ServerSlotIdentity* Slots = nullptr;
            ServerAuthorityState* Authority = nullptr;
            ServerSnapshotStream* Snapshots = nullptr;
            ServerSnapshotChannel* SnapshotChannel = nullptr;
            ServerLobbyState* Lobby = nullptr;
            ServerMapVoting* Voting = nullptr;
            ServerLobbySession* LobbySession = nullptr;
            ServerStateBroadcastChannel* State = nullptr;
            ServerSimulationController* Simulation = nullptr;
            std::unique_ptr<NetTransport>* Transport = nullptr;
            ScratchBuffer* Scratch = nullptr;
            std::atomic<bool>* EverOccupied = nullptr;
            std::int32_t MaxPlayers = 0;
            bool* RunsTheMatch = nullptr;
            std::uint16_t* MatchId = nullptr;
            double* Now = nullptr;
            double* MatchStarted = nullptr;
            double* MatchEndedAt = nullptr;
            void* Context = nullptr;
            MatchDefinition (*CurrentDefinition)(void*) = nullptr;
            void (*WriteLog)(void*, const std::string&) = nullptr;
        };

        explicit ServerPeerConnectionChannel(Ports ports) noexcept;

        void HandleHello(const ReceivedPacket& packet, double now);
        void HandleIdentify(const ReceivedPacket& packet, double now);
        void HandlePong(const ReceivedPacket& packet, double now);
        void HandlePing(const ReceivedPacket& packet);
        void PingPeers(double now);
        void HandleBye(const ReceivedPacket& packet);
        void DropTimedOut(double now);
        void Remove(const PeerHandle& peer, const std::string& reason);
        void SendRefusal(const std::shared_ptr<System::Net::IPEndPoint>& endpoint,
            std::uint8_t reason) const;

    private:
        [[nodiscard]] MatchDefinition CurrentDefinition() const;
        void Log(const std::string& message) const;
        void Announce(const std::string& message) const;
        Ports _ports;
    };
}
