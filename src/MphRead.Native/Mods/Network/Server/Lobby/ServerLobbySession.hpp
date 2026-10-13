#pragma once

#include "Mods/Network/Protocol/NetProtocol.hpp"
#include "Mods/Network/Server/Core/ServerAuthorityState.hpp"
#include "Mods/Network/Server/Core/ServerPeerRegistry.hpp"
#include "Mods/Network/Server/Core/ServerSlotIdentity.hpp"
#include "Mods/Network/Server/Lobby/ServerLobbyState.hpp"
#include "Mods/Network/Server/Lobby/ServerMapVoting.hpp"
#include "Mods/Network/Server/Core/ServerStateBroadcastChannel.hpp"
#include "Mods/Network/Server/Match/MapRotation.hpp"
#include "Mods/Network/Server/Match/ServerRosterBuilder.hpp"
#include "Mods/Network/Server/Match/ServerSnapshotStream.hpp"
#include "Mods/Network/Server/Match/ServerSimulationController.hpp"
#include "Mods/Network/Session/SessionPorts.hpp"
#include "NativeRuntime/System/Guid.hpp"

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
    struct ReceivedPacket;

    // Coordinates lobby commands, load barriers, owner transitions, and the
    // session-state projection. Cross-domain match/server effects enter only
    // through the fixed callback ports below.
    class ServerLobbySession final
    {
    public:
        using ScratchBuffer = std::array<std::uint8_t, NetConfig::MaxPacketSize>;
        using PeerHandle = ServerPeerRegistry::PeerHandle;

        struct Ports final
        {
            void* Context = nullptr;
            ServerLobbyState* Lobby = nullptr;
            ServerPeerRegistry* Peers = nullptr;
            ServerSlotIdentity* Slots = nullptr;
            ServerSnapshotStream* Snapshots = nullptr;
            ServerSimulationController* Simulation = nullptr;
            ServerStateBroadcastChannel* State = nullptr;
            ServerMapVoting* Voting = nullptr;
            DedicatedServerSessionPort* Session = nullptr;
            const std::shared_ptr<MapRotation>* Rotation = nullptr;
            std::unique_ptr<NetTransport>* Transport = nullptr;
            ScratchBuffer* Scratch = nullptr;
            std::int32_t MaxPlayers = 0;
            std::uint16_t* MatchId = nullptr;
            double* Now = nullptr;
            double* MatchStarted = nullptr;
            double* MatchEndedAt = nullptr;

            MatchDefinition (*CurrentDefinition)(void*) = nullptr;
            MatchDefinition (*DefinitionFor)(void*, const RotationEntry&) = nullptr;
            void (*StartSimulation)(void*) = nullptr;
            void (*SyncSimulationState)(void*, double) = nullptr;
            void (*RemovePeer)(void*, const PeerHandle&, const std::string&) = nullptr;
            void (*SendRefusal)(void*, const std::shared_ptr<System::Net::IPEndPoint>&,
                std::uint8_t) = nullptr;
            void (*WriteLog)(void*, const std::string&) = nullptr;
        };

        explicit ServerLobbySession(Ports ports) noexcept;

        void TouchRevision(const std::string& reason);
        void SetPhase(SessionPhase phase);
        void ClaimOwner(const PeerHandle& peer, const ::MphRead::NativeRuntime::Guid& ownerToken);
        void HandleCommand(const ReceivedPacket& packet, double now);
        void HandleMatchLoaded(const ReceivedPacket& packet, double now);
        void CheckLoadBarrier(double now);
        void HandleMatchLoadFailed(const ReceivedPacket& packet);
        void ReturnToLobby();
        void PeerRemoved(const PeerHandle& peer);

    private:
        [[nodiscard]] MatchDefinition CurrentDefinition() const;
        [[nodiscard]] MatchDefinition DefinitionFor(const RotationEntry& entry) const;
        [[nodiscard]] LobbyResultCode ExecuteCommand(const PeerHandle& peer,
            const LobbyCommandPacket& command, std::string& reason);
        void Log(const std::string& message) const;

        Ports _ports;
    };
}
