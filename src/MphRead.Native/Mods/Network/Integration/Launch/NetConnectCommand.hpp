#pragma once

#include "Formats/Enums.hpp"

#include <cstdint>
#include <string>

namespace MphRead::Mods::Network
{
    class NetConnectCommand final
    {
    public:
        NetConnectCommand() = delete;

        static void Run(const std::string& host, std::int32_t port,
            const std::string& playerName, Hunter hunter, std::int32_t recolor);
    };
}
