#pragma once

#include "Mods/Network/Domain/MatchDefinition.hpp"
#include "Mods/Network/Protocol/SessionProtocol.hpp"
#include "Mods/Network/Session/SessionPorts.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace MphRead::Mods::Network
{
    class ServerSim;

    // Owns the authoritative game simulation's process lifetime and forwards
    // its measurements without exposing ServerSim to the server runner.
    class ServerSimulationController final
    {
    public:
        ServerSimulationController();
        ServerSimulationController(const ServerSimulationController&) = delete;
        ServerSimulationController(ServerSimulationController&&) = delete;
        ServerSimulationController& operator=(const ServerSimulationController&) = delete;
        ServerSimulationController& operator=(ServerSimulationController&&) = delete;
        ~ServerSimulationController();

        [[nodiscard]] bool Start(const MatchDefinition& definition, std::int32_t maxPlayers,
            SnapshotSink snapshots, std::function<void()> matchEnded,
            ServerSimulationSessionPort session, std::optional<RosterPacket> roster,
            std::optional<SessionStatePacket> state);
        [[nodiscard]] static bool Available(std::string& reason);
        [[nodiscard]] bool HasSimulation() const noexcept;
        [[nodiscard]] bool Running() const noexcept;
        void Advance(double now);
        void Stop();

        [[nodiscard]] std::string Describe() const;
        [[nodiscard]] std::string DescribeUnlagged() const;
        [[nodiscard]] std::string DescribeRewindDepths() const;
        [[nodiscard]] std::optional<std::string> DescribeClaims() const;
        [[nodiscard]] std::string DescribeAgreement() const;
        [[nodiscard]] std::string DescribeShots() const;

    private:
        std::unique_ptr<ServerSim> _simulation{};
    };
}
