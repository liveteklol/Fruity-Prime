#pragma once

#include <cstdint>

namespace MphRead::Mods::Network
{
    class NetScoreboard final
    {
    public:
        NetScoreboard() = delete;

        static void ForgetSlot(std::int32_t slot);
    };
}
