#pragma once

#include "Mods/Network/Protocol/NetProtocol.hpp"
#include "Mods/Network/Protocol/SessionProtocol.hpp"
#include "Mods/Network/Session/SessionPorts.hpp"

#include <optional>

#include "Formats/Enums.hpp"
#include <cstdint>
#include <memory>
#include <string>

namespace MphRead
{
    class Scene;
}

namespace MphRead::Mods::Network
{
    class ServerSimFixture;

    class ServerSim final
    {
        // Diagnostic scene access is kept behind one narrow read-only fixture.
        friend class ServerSimFixture;

    public:
        ServerSim() = default;
        ServerSim(const ServerSim&) = delete;
        ServerSim(ServerSim&&) = delete;
        ServerSim& operator=(const ServerSim&) = delete;
        ServerSim& operator=(ServerSim&&) = delete;

        void Advance(double now);

        [[nodiscard]] bool Running() const noexcept { return _scene != nullptr; }
        [[nodiscard]] std::string Room() const;

        [[nodiscard]] static bool Available(std::string& reason);

        [[nodiscard]] bool Start(const std::string& roomKey, GameMode mode,
            std::int32_t maxPlayers, SnapshotSink sink,
            std::function<void()> matchEnded,
            ServerSimulationSessionPort sessionPort,
            std::optional<RosterPacket> roster = std::nullopt,
            std::optional<SessionStatePacket> session = std::nullopt);

        void Step();
        void Stop();

        [[nodiscard]] std::string DescribeUnlagged() const;
        [[nodiscard]] std::string DescribeRewindDepths() const;
        [[nodiscard]] std::optional<std::string> DescribeClaims() const;
        [[nodiscard]] std::string DescribeAgreement() const;
        [[nodiscard]] std::string DescribeShots() const;
        [[nodiscard]] std::string Describe() const;

        [[nodiscard]] std::int64_t Frames() const noexcept { return _frames; }
        [[nodiscard]] double StepSeconds() const noexcept { return _stepSeconds; }
        [[nodiscard]] double WorstStepSeconds() const noexcept { return _worstStepSeconds; }
        [[nodiscard]] std::int64_t OverrunSteps() const noexcept { return _overrunSteps; }
        [[nodiscard]] std::int64_t StepFailures() const noexcept { return _stepFailures; }
        [[nodiscard]] std::int64_t DroppedSteps() const noexcept { return _droppedSteps; }
        [[nodiscard]] std::int64_t Stalls() const noexcept { return _stalls; }

    private:
        static constexpr double StallSeconds = 0.25;

        std::shared_ptr<Scene> _scene{};
        std::string _room{};
        GameMode _mode = GameMode::Battle;

        std::int64_t _frames = 0;
        double _stepSeconds = 0.0;
        double _worstStepSeconds = 0.0;
        std::int64_t _overrunSteps = 0;
        std::int64_t _stepFailures = 0;
        std::int64_t _droppedSteps = 0;
        std::int64_t _stalls = 0;

        ServerSimulationSessionPort _sessionPort{};
        bool _sessionStarted = false;

        double _accumulator = 0.0;
        double _lastAdvance = -1.0;
    };
}
