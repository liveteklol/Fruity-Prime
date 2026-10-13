#pragma once

#include <cstdint>
#include <string>

namespace MphRead::Mods::Network
{
    enum class NetRole : std::int32_t
    {
        Offline = 0,
        Host = 1,
        Client = 2,
        Server = 3
    };

    // NetRole.ToString().
    [[nodiscard]] std::string ToString(NetRole value);
}
