#pragma once

#include "Mods/Network/Protocol/NetProtocol.hpp"
#include "Mods/Network/Protocol/Packets/SessionPackets.hpp"
#include "Mods/Network/Server/Core/ServerAuthorityState.hpp"
#include "Mods/Network/Server/Core/ServerPeerRegistry.hpp"
#include "Mods/Network/Server/Core/ServerSlotIdentity.hpp"
#include "Mods/Network/Server/Lobby/ServerLobbyState.hpp"
#include "Mods/Network/Server/Match/MapRotation.hpp"
#include "Mods/Network/Server/Match/ServerRosterBuilder.hpp"
#include "Mods/Network/Server/Match/ServerSimulationController.hpp"
#include "Mods/Network/Session/SessionPorts.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <string>

namespace MphRead::Mods::Network
{
    class NetTransport;
    class ServerSnapshotChannel;

    // Owns construction and publication of the authoritative state packets sent
    // to connected peers. Match/lobby policy remains with their state owners.
    class ServerStateBroadcastChannel final
    {
    public:
        using ScratchBuffer = std::array<std::uint8_t, NetConfig::MaxPacketSize>;

        struct Ports final
        {
            ServerLobbyState* Lobby = nullptr;
            ServerPeerRegistry* Peers = nullptr;
            ServerAuthorityState* Authority = nullptr;
            ServerSlotIdentity* Slots = nullptr;
            ServerSnapshotChannel* Snapshots = nullptr;
            ServerSimulationController* Simulation = nullptr;
            DedicatedServerSessionPort* Session = nullptr;
            const std::shared_ptr<MapRotation>* Rotation = nullptr;
            std::unique_ptr<NetTransport>* Transport = nullptr;
            ScratchBuffer* Scratch = nullptr;
            std::int32_t MaxPlayers = 0;
            std::int32_t DamageLevel = 1;
            std::uint16_t* MatchId = nullptr;
            double* Now = nullptr;
            double* MatchStarted = nullptr;
            double* MatchEndedAt = nullptr;
            void* DefinitionContext = nullptr;
            MatchDefinition (*ReadDefinition)(void*) = nullptr;
        };

        explicit ServerStateBroadcastChannel(Ports ports) noexcept;

        [[nodiscard]] SessionStatePacket BuildSessionState() const;
        void BroadcastSessionState();
        [[nodiscard]] MatchStatePacket BuildMatchState(double now) const;
        void BroadcastMatchState(double now);
        [[nodiscard]] RosterPacket BuildRoster();
        void BroadcastRoster();
        void NotifyAuthority(const ServerPeerRegistry::PeerHandle& peer);

    private:
        [[nodiscard]] MatchDefinition CurrentDefinition() const;

        Ports _ports;
        ServerRosterBuilder _rosterBuilder{};
    };
}
