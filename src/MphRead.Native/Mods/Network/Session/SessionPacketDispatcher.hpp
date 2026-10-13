#pragma once

#include "Mods/Network/Transport/NetTransport.hpp"

namespace MphRead::Mods::Network
{
    // Routes validated session datagrams to the owning session/feature channel.
    class SessionPacketDispatcher final
    {
    public:
        SessionPacketDispatcher() = delete;

        static void Dispatch(ReceivedPacket packet, double time);
    };
}
