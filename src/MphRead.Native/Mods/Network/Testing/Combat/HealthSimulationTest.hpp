#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace MphRead::Mods::Network
{
    // A health pickup taken and respawned on a simulating authority, and a
    // replica that converges on it from nothing but the snapshot tail.
    class HealthSimulationTest final
    {
    public:
        HealthSimulationTest() = delete;

        [[nodiscard]] static std::int32_t Run(const std::string& room);

    private:
        [[nodiscard]] static std::vector<std::uint8_t> Tail(const std::vector<std::uint8_t>& snapshot);
    };
}
