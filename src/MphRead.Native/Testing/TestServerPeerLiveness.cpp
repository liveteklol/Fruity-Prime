#include "Mods/Network/Server/Core/ServerPeerLiveness.hpp"

#include <cstdint>
#include <iostream>
#include <limits>

namespace
{
    using MphRead::Mods::Network::ServerPeerLiveness;

    bool Check(bool condition, const char* name)
    {
        if (!condition)
        {
            std::cerr << "FAIL " << name << '\n';
            return false;
        }
        return true;
    }
}

int main()
{
    ServerPeerLiveness health;
    bool passed = true;
    passed &= Check(!health.TimedOut(30.0, 30.0) && health.TimedOut(30.001, 30.0),
        "timeout preserves the strict greater-than boundary");
    health.MarkSeen(12.0);
    passed &= Check(!health.TimedOut(42.0, 30.0) && health.TimedOut(42.001, 30.0),
        "accepted peer traffic refreshes the timeout clock");

    passed &= Check(health.CanSendPing(0.0), "a peer with no pending ping is immediately pingable");
    const std::uint8_t firstId = health.BeginPing(50.0);
    passed &= Check(firstId == 1 && !health.CanSendPing(54.999)
        && health.CanSendPing(55.0), "pending pings retry at the five-second boundary");
    passed &= Check(!health.AcceptPong(0, 50.1) && health.PingMilliseconds() == 0,
        "a stale pong cannot complete a pending ping");
    passed &= Check(health.AcceptPong(firstId, 50.1) && health.PingMilliseconds() == 100,
        "a matching pong records RTT and completes the probe");
    passed &= Check(!health.AcceptPong(firstId, 50.2), "duplicate pongs are ignored");

    const std::uint8_t secondId = health.BeginPing(60.0);
    passed &= Check(health.AcceptPong(secondId, 60.2) && health.PingMilliseconds() == 133,
        "successive RTT samples retain the existing two-thirds smoothing");

    std::uint8_t id = secondId;
    for (std::uint32_t i = 0; i < std::numeric_limits<std::uint8_t>::max(); i++)
    {
        id = health.BeginPing(70.0 + i * ServerPeerLiveness::PingRetrySeconds);
        (void)health.AcceptPong(id, 70.0 + i * ServerPeerLiveness::PingRetrySeconds);
    }
    passed &= Check(id == 1, "ping sequence wraps with the existing byte width");

    if (!passed)
    {
        return 1;
    }
    std::cout << "ServerPeerLiveness PASS\n";
    return 0;
}
