#pragma once

#include <cstdint>

namespace MphRead::Mods::Network
{
    class NetPlayerSetup final
    {
    public:
        NetPlayerSetup() = delete;

        static void Reset();
        static void ApplyOnce();

    private:
        static std::int32_t CountActive();

        static bool _applied;
    };
}
