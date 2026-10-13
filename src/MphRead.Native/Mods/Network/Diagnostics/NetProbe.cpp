#include "Mods/Network/Diagnostics/NetProbe.hpp"

#include "NativeRuntime/System/DateTime.hpp"
#include "NativeRuntime/System/Net.hpp"
#include "Mods/Network/Protocol/NetProtocol.hpp"
#include "NativeRuntime/System/Managed.hpp"

#include <cstdint>
#include <exception>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using ::MphRead::NativeRuntime::CSharpTryFinally;

namespace
{
    using MphRead::Mods::Network::NetProbeResult;

    std::string EndPointPrefix(
        const ::MphRead::NativeRuntime::EndPoint& endPoint,
        std::int32_t port)
    {
        return ::MphRead::NativeRuntime::AddressToString(endPoint.Address) + ":" + std::to_string(port);
    }
}

namespace MphRead::Mods::Network
{
    NetProbeResult NetProbe::Probe(const std::string& address, std::int32_t port,
        std::int32_t timeoutMs)
    {
        ::MphRead::NativeRuntime::EndPoint endPoint;
        try
        {
            std::vector<::MphRead::NativeRuntime::Address> resolved
                = ::MphRead::NativeRuntime::DnsGetHostAddresses(address);
            if (resolved.empty())
            {
                return {false, "Could not resolve " + address + "."};
            }

            const ::MphRead::NativeRuntime::Address* ipv4 = nullptr;
            for (const ::MphRead::NativeRuntime::Address& candidate : resolved)
            {
                if (candidate.Family == ::MphRead::NativeRuntime::AddressFamily::InterNetwork)
                {
                    ipv4 = &candidate;
                    break;
                }
            }
            if (ipv4 == nullptr)
            {
                return {false, address + " has no IPv4 address."};
            }
            endPoint = ::MphRead::NativeRuntime::CreateIPEndPoint(*ipv4, port);
        }
        catch (const std::exception& ex)
        {
            return {false, "Could not resolve " + address + ": " + ex.what()};
        }

        const ::MphRead::NativeRuntime::SocketHandle socket
            = ::MphRead::NativeRuntime::UdpClientCreateInterNetwork();
        return CSharpTryFinally(
            [&]() -> NetProbeResult
            {
                // This assignment is deliberately outside the socket-operation
                // try/catch, matching the C# source. A setter failure propagates,
                // but the using-scope still disposes the socket.
                ::MphRead::NativeRuntime::UdpClientSetReceiveTimeout(socket, timeoutMs);

                try
                {
                    const std::uint8_t hello[] = {
                        static_cast<std::uint8_t>(PacketType::Hello),
                        static_cast<std::uint8_t>(NetConfig::ProtocolVersion)
                    };
                    ::MphRead::NativeRuntime::UdpClientSend(socket, hello, 2, endPoint);

                    ::MphRead::NativeRuntime::EndPoint from = ::MphRead::NativeRuntime::CreateIPv4AnyEndPoint();
                    constexpr std::int64_t ticksPerMillisecond = 10'000;
                    const std::int64_t deadline = ::MphRead::NativeRuntime::DateTimeUtcNowTicks()
                        + static_cast<std::int64_t>(timeoutMs) * ticksPerMillisecond;

                    while (::MphRead::NativeRuntime::DateTimeUtcNowTicks() < deadline)
                    {
                        std::vector<std::uint8_t> reply
                            = ::MphRead::NativeRuntime::UdpClientReceive(socket, from);
                        if (reply.size() >= 2
                            && reply[0] == static_cast<std::uint8_t>(PacketType::Welcome))
                        {
                            const std::uint8_t bye[] = {static_cast<std::uint8_t>(PacketType::Bye)};
                            ::MphRead::NativeRuntime::UdpClientSend(socket, bye, 1, endPoint);
                            return {true, "Connected to " + EndPointPrefix(endPoint, port)
                                + " -- server assigned slot "
                                + std::to_string(reply[1]) + "."};
                        }

                        if (!reply.empty()
                            && reply[0] >= static_cast<std::uint8_t>(PacketType::Hello)
                            && reply[0] <= static_cast<std::uint8_t>(PacketType::Authority))
                        {
                            continue;
                        }
                        return {false, EndPointPrefix(endPoint, port)
                            + " replied, but not as an MphRead server."};
                    }

                    const std::uint8_t bye[] = {static_cast<std::uint8_t>(PacketType::Bye)};
                    ::MphRead::NativeRuntime::UdpClientSend(socket, bye, 1, endPoint);
                    return {false, EndPointPrefix(endPoint, port)
                        + " answered but never assigned a slot."};
                }
                catch (const ::MphRead::NativeRuntime::SocketException& ex)
                {
                    if (::MphRead::NativeRuntime::SocketErrorIsTimeout(ex))
                    {
                        return {false, "No reply from " + EndPointPrefix(endPoint, port)
                            + ". The server may be down, or UDP " + std::to_string(port)
                            + " may be blocked by a firewall."};
                    }
                    return {false, "Could not reach " + EndPointPrefix(endPoint, port)
                        + ": " + ex.what()};
                }
                catch (const std::exception& ex)
                {
                    return {false, "Could not reach " + EndPointPrefix(endPoint, port)
                        + ": " + ex.what()};
                }
            },
            [&]()
            {
                ::MphRead::NativeRuntime::UdpClientDispose(socket);
            });
    }
}
