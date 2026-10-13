#pragma once

#include "Mods/Network/Domain/MatchDefinition.hpp"
#include "Mods/Network/Protocol/NetProtocol.hpp"
#include "Mods/Network/Server/Core/ServerAuthorityState.hpp"
#include "Mods/Network/Server/Core/ServerClaimChannel.hpp"
#include "Mods/Network/Server/Core/ServerPeerRegistry.hpp"
#include "Mods/Network/Server/Core/ServerSlotIdentity.hpp"
#include "Mods/Network/Server/Lobby/ServerLobbyState.hpp"
#include "Mods/Network/Server/Match/MapRotation.hpp"
#include "Mods/Network/Server/Match/ServerSimulationController.hpp"
#include "Mods/Network/Server/Match/ServerSnapshotStream.hpp"
#include "Mods/Network/Session/SessionPorts.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace MphRead::Mods::Network
{
    class NetTransport;
    class ServerLobbySession;
    class ServerMapVoting;
    class ServerSnapshotChannel;
    class ServerStateBroadcastChannel;
    struct ReceivedPacket;

    // Owns match start/end and map-rotation effects. The server's main loop
    // decides when these transitions run; this owner applies each transition.
    class ServerMatchLifecycle final
    {
    public:
        using ScratchBuffer = std::array<std::uint8_t, NetConfig::MaxPacketSize>;

        struct Ports final
        {
            ServerLobbyState* Lobby = nullptr;
            ServerAuthorityState* Authority = nullptr;
            ServerPeerRegistry* Peers = nullptr;
            ServerSlotIdentity* Slots = nullptr;
            ServerSnapshotStream* SnapshotStream = nullptr;
            ServerSnapshotChannel* SnapshotChannel = nullptr;
            ServerMapVoting* Voting = nullptr;
            ServerLobbySession* LobbySession = nullptr;
            ServerStateBroadcastChannel* State = nullptr;
            ServerSimulationController* Simulation = nullptr;
            ServerClaimChannel* Claims = nullptr;
            DedicatedServerSessionPort* Session = nullptr;
            const std::shared_ptr<MapRotation>* Rotation = nullptr;
            std::unique_ptr<NetTransport>* Transport = nullptr;
            ScratchBuffer* Scratch = nullptr;
            std::int32_t MaxPlayers = 0;
            bool* RunsTheMatch = nullptr;
            std::uint16_t* MatchId = nullptr;
            double* Now = nullptr;
            double* MatchStarted = nullptr;
            double* MatchEndedAt = nullptr;
            double EndSequenceSeconds = 0;
            void* Context = nullptr;
            MatchDefinition (*CurrentDefinition)(void*) = nullptr;
            void (*WriteLog)(void*, const std::string&) = nullptr;
        };

        explicit ServerMatchLifecycle(Ports ports) noexcept;

        [[nodiscard]] static double EndSequenceSeconds();
        [[nodiscard]] bool Simulating() const;
        [[nodiscard]] double EndSequenceFor() const;
        void EndMatch(double now, const std::string& reason);
        void AdvanceMap(double now);
        // The room the match rotated onto cannot be stepped (every step throws):
        // go back to the last map that worked, or the first that has not failed.
        // False when there is nowhere to go back to.
        bool RecoverFromBrokenRoom(double now);
        void StartSimulation();
        void SynchronizeSimulationState(double now);
        void HandleMatchEnd(const ReceivedPacket& packet, double now);

    private:
        static constexpr double ReadyWaitSeconds = 30.0;
        static constexpr double AllReadySeconds = 5.0;
        static constexpr double RecoveryCooldownSeconds = 10.0;

        [[nodiscard]] MatchDefinition CurrentDefinition() const;

        std::string _previousRoom{};
        GameMode _previousMode = GameMode::Battle;
        std::vector<std::string> _brokenRooms{};
        double _recoveredAt = -1000.0;
        void Log(const std::string& message) const;

        Ports _ports;
    };
}
