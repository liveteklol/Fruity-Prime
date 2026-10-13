#include "Mods/Network/Server/Match/ServerSimulationController.hpp"

#include "Mods/Network/Server/Match/ServerSim.hpp"

#include <utility>

namespace MphRead::Mods::Network
{
    ServerSimulationController::ServerSimulationController() = default;
    ServerSimulationController::~ServerSimulationController() = default;

    bool ServerSimulationController::Available(std::string& reason)
    {
        return ServerSim::Available(reason);
    }

    bool ServerSimulationController::Start(const MatchDefinition& definition,
        std::int32_t maxPlayers, SnapshotSink snapshots, std::function<void()> matchEnded,
        ServerSimulationSessionPort session, std::optional<RosterPacket> roster,
        std::optional<SessionStatePacket> state)
    {
        auto simulation = std::make_unique<ServerSim>();
        if (!simulation->Start(definition.RoomKey.value_or(""), definition.Mode, maxPlayers,
                std::move(snapshots), std::move(matchEnded), std::move(session),
                std::move(roster), std::move(state)))
        {
            return false;
        }
        _simulation = std::move(simulation);
        return true;
    }

    bool ServerSimulationController::Running() const noexcept
    {
        return HasSimulation() && _simulation->Running();
    }

    bool ServerSimulationController::HasSimulation() const noexcept
    {
        return _simulation != nullptr;
    }

    void ServerSimulationController::Advance(double now)
    {
        if (_simulation != nullptr)
        {
            _simulation->Advance(now);
        }
    }

    void ServerSimulationController::Stop()
    {
        if (_simulation != nullptr)
        {
            _simulation->Stop();
            _simulation.reset();
        }
    }

    std::string ServerSimulationController::Describe() const
    {
        return _simulation != nullptr ? _simulation->Describe() : std::string{};
    }

    std::string ServerSimulationController::DescribeUnlagged() const
    {
        return _simulation != nullptr ? _simulation->DescribeUnlagged() : std::string{};
    }

    std::string ServerSimulationController::DescribeRewindDepths() const
    {
        return _simulation != nullptr ? _simulation->DescribeRewindDepths() : std::string{};
    }

    std::optional<std::string> ServerSimulationController::DescribeClaims() const
    {
        return _simulation != nullptr ? _simulation->DescribeClaims() : std::nullopt;
    }

    std::string ServerSimulationController::DescribeAgreement() const
    {
        return _simulation != nullptr ? _simulation->DescribeAgreement() : std::string{};
    }

    std::string ServerSimulationController::DescribeShots() const
    {
        return _simulation != nullptr ? _simulation->DescribeShots() : std::string{};
    }
}
