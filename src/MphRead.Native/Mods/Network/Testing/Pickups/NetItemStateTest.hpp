#pragma once

namespace MphRead::Mods::Network
{
    // The health-snapshot tail and the objective clocks, against bytes
    // written by hand: every validation rule and the round trip.
    class NetItemStateTest final
    {
    public:
        NetItemStateTest() = delete;

        static void Run();
    };
}
