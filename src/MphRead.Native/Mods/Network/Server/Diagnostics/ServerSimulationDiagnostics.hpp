#pragma once

#include <string>
#include <vector>

namespace MphRead::Mods::Network
{
    class ServerSimulationController;

    // Collects low-frequency simulation and gameplay subsystem diagnostics for
    // the dedicated-server status report.
    class ServerSimulationDiagnostics final
    {
    public:
        ServerSimulationDiagnostics() = delete;

        [[nodiscard]] static std::vector<std::string> Describe(
            const ServerSimulationController& simulation);
    };
}
