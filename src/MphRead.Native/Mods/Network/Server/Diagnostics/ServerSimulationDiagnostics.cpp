#include "Mods/Network/Server/Diagnostics/ServerSimulationDiagnostics.hpp"

#include "Mods/Network/Gameplay/Claims/Pickup/NetPickupClaims.hpp"
#include "Mods/Network/Gameplay/Pickups/NetPowerUps.hpp"
#include "Mods/Network/Gameplay/Weapons/NetShotEvents.hpp"
#include "Mods/Network/Server/Match/ServerSimulationController.hpp"
#include "NativeRuntime/System/Globalization.hpp"
#include "NativeRuntime/System/Runtime.hpp"

#include <optional>

namespace MphRead::Mods::Network
{
    namespace Runtime = ::MphRead::NativeRuntime;

    std::vector<std::string> ServerSimulationDiagnostics::Describe(
        const ServerSimulationController& simulation)
    {
        std::vector<std::string> lines;
        lines.reserve(10);
        lines.push_back(simulation.Describe());
        lines.push_back(simulation.DescribeUnlagged());
        lines.push_back(simulation.DescribeRewindDepths());
        if (const std::optional<std::string> claims = simulation.DescribeClaims(); claims.has_value())
        {
            lines.push_back(*claims);
        }
        lines.push_back(simulation.DescribeShots());
        lines.push_back(NetPickupClaims::Describe());
        lines.push_back(NetPowerUps::Describe());
        if (const std::optional<std::string> events = NetShotEvents::Describe(); events.has_value())
        {
            lines.push_back(*events);
        }
        const std::vector<std::string> agreement = Runtime::StringSplit(simulation.DescribeAgreement(), '\n');
        lines.insert(lines.end(), agreement.begin(), agreement.end());
        return lines;
    }
}
