#pragma once

#include <cstdint>
#include <string>

namespace MphRead::Mods::Network
{
    // Asset-backed production collision/damage regression, without drawing.
    class DialancheCombatCheck final
    {
    public:
        static std::int32_t Run(const std::string& room, std::int32_t peerA = 0, std::int32_t peerB = 0);
        static std::int32_t RunPeer(const std::string& room, std::int32_t port);
    };
}
