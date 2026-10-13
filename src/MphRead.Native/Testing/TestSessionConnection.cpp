#include "Mods/Network/Session/SessionConnection.hpp"

#include <iostream>
#include <string_view>

namespace
{
    bool Check(bool condition, std::string_view name)
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
    using MphRead::Mods::Network::SessionConnection;
    using System::Net::IPEndPoint;

    bool passed = true;
    SessionConnection connection;
    passed &= Check(!connection.IsOpen() && !connection.HasHostEndpoint()
        && connection.LocalPort() == -1,
        "connection begins closed");

    connection.Open(0);
    const std::int32_t firstPort = connection.LocalPort();
    std::uint8_t* const buffer = connection.Buffer().data();
    passed &= Check(connection.IsOpen() && firstPort > 0,
        "open binds an ephemeral UDP port");

    const auto host = IPEndPoint::Loopback(27888);
    connection.SetHostEndpoint(host);
    passed &= Check(connection.HasHostEndpoint() && connection.IsHost(host)
        && !connection.IsHost(IPEndPoint::Loopback(27889)),
        "host sender matching uses endpoint value equality");

    const std::int32_t reboundFrom = connection.Rebind();
    passed &= Check(reboundFrom == firstPort && connection.IsOpen()
        && connection.LocalPort() > 0 && connection.HasHostEndpoint()
        && connection.IsHost(host) && connection.Buffer().data() == buffer,
        "rebind replaces socket while retaining endpoint and scratch storage");

    connection.Close();
    passed &= Check(!connection.IsOpen() && !connection.HasHostEndpoint()
        && connection.LocalPort() == -1,
        "close releases socket and endpoint");
    passed &= Check(connection.Rebind() == -1,
        "rebind of closed connection is a no-op");

    return passed ? 0 : 1;
}
