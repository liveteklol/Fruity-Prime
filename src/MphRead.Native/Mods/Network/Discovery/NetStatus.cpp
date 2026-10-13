#include "Mods/Network/Discovery/NetStatus.hpp"

#include "NativeRuntime/System/Net.hpp"
#include "Mods/Network/Protocol/NetProtocol.hpp"
#include "Formats/Formats.hpp"
#include "Metadata/Metadata.hpp"
#include "NativeRuntime/System/ExceptionText.hpp"
#include "NativeRuntime/System/Globalization.hpp"
#include "NativeRuntime/System/Managed.hpp"

#include <array>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <cerrno>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

using ::MphRead::NativeRuntime::CSharpTryFinally;
using ::MphRead::NativeRuntime::CharIsWhiteSpace;
using ::MphRead::NativeRuntime::StringIsNullOrWhiteSpace;

namespace MphRead::Mods::Network::Detail
{
    std::int64_t NetStatusDateTimeUtcNowTicks()
    {
        constexpr std::int64_t unixEpochTicks = 621355968000000000LL;
        const auto sinceUnixEpoch = std::chrono::system_clock::now().time_since_epoch();
        const std::int64_t ticks = std::chrono::duration_cast<std::chrono::nanoseconds>(
            sinceUnixEpoch).count() / 100;
        return unixEpochTicks + ticks;
    }

    std::int32_t NetStatusProtocolVersion()
    {
        return NetConfig::ProtocolVersion;
    }

    bool NetStatusIsDefinedGameMode(std::int32_t value)
    {
        return value >= 0 && ::MphRead::IsDefinedGameMode(static_cast<std::uint64_t>(value));
    }

    GameMode NetStatusBattleGameMode()
    {
        return GameMode::Battle;
    }

    std::string NetStatusGameModeToString(GameMode mode)
    {
        return ::MphRead::ToString(mode);
    }
}
namespace
{
    using MphRead::Mods::Network::ServerStatus;

    [[nodiscard]] std::int32_t StopwatchElapsedMilliseconds(
        std::chrono::steady_clock::time_point started) noexcept
    {
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started).count();
        const std::uint32_t low = static_cast<std::uint32_t>(
            static_cast<std::uint64_t>(elapsed));
        return std::bit_cast<std::int32_t>(low);
    }
}

namespace MphRead::Mods::Network
{
    ServerStatus ServerStatus::Offline(const std::string& message)
    {
        ServerStatus status;
        status.RoomKey = "";
        status.ServerName = "";
        status.Latency = -1;
        status.Message = message;
        return status;
    }

    ServerStatus NetStatus::Query(const std::string& address, std::int32_t port,
        bool allowJoinProbe, std::int32_t timeoutMs)
    {
        if (StringIsNullOrWhiteSpace(address))
        {
            return ServerStatus::Offline("No server address.");
        }

        ::MphRead::NativeRuntime::EndPoint endPoint;
        try
        {
            std::vector<::MphRead::NativeRuntime::Address> resolved
                = ::MphRead::NativeRuntime::DnsGetHostAddresses(address);
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
                return ServerStatus::Offline("Cannot find " + address + ".");
            }
            endPoint = ::MphRead::NativeRuntime::CreateIPEndPoint(*ipv4, port);
        }
        catch (...)
        {
            return ServerStatus::Offline("Cannot find " + address + ".");
        }

        const ::MphRead::NativeRuntime::SocketHandle socket
            = ::MphRead::NativeRuntime::UdpClientCreateInterNetwork();
        return CSharpTryFinally(
            [&]() -> ServerStatus
            {
                // Deliberately outside the operation try/catch, like the C#
                // ReceiveTimeout assignment. A setter failure propagates.
                ::MphRead::NativeRuntime::UdpClientSetReceiveTimeout(socket, timeoutMs);

                try
                {
                    const auto started = std::chrono::steady_clock::now();
                    const std::uint8_t query[] = {
                        static_cast<std::uint8_t>(PacketType::StatusQuery),
                        static_cast<std::uint8_t>(Detail::NetStatusProtocolVersion())
                    };
                    ::MphRead::NativeRuntime::UdpClientSend(socket, query, 2, endPoint);

                    ::MphRead::NativeRuntime::EndPoint from
                        = ::MphRead::NativeRuntime::CreateIPv4AnyEndPoint();
                    constexpr std::int64_t ticksPerMillisecond = 10'000;
                    const std::int64_t deadline = Detail::NetStatusDateTimeUtcNowTicks()
                        + static_cast<std::int64_t>(timeoutMs) * ticksPerMillisecond;
                    while (Detail::NetStatusDateTimeUtcNowTicks() < deadline)
                    {
                        std::vector<std::uint8_t> reply
                            = ::MphRead::NativeRuntime::UdpClientReceive(socket, from);
                        if (!::MphRead::NativeRuntime::EndPointEquals(from, endPoint))
                        {
                            continue;
                        }
                        if (reply.size() >= 1U
                                + static_cast<std::size_t>(ServerStatusPacket::Size)
                            && reply[0]
                                == static_cast<std::uint8_t>(PacketType::StatusReply))
                        {
                            ServerStatusPacket status = ServerStatusPacket::Read(
                                std::span<const std::uint8_t>(reply).subspan(1));
                            const std::int32_t latency
                                = StopwatchElapsedMilliseconds(started);
                            return Describe(std::move(status), false, latency);
                        }
                    }
                }
                catch (const ::MphRead::NativeRuntime::SocketException&)
                {
                }
                catch (...)
                {
                    const std::string message
                        = ::MphRead::NativeRuntime::ExceptionMessage(std::current_exception());
                    return ServerStatus::Offline(
                        "Cannot reach " + address + ": " + message);
                }

                return allowJoinProbe
                    ? JoinProbe(socket, endPoint, address, timeoutMs)
                    : ServerStatus::Offline(
                        "No answer from " + address + ":" + std::to_string(port) + ".");
            },
            [&]()
            {
                ::MphRead::NativeRuntime::UdpClientDispose(socket);
            });
    }

    ServerStatus NetStatus::JoinProbe(::MphRead::NativeRuntime::SocketHandle socket,
        const ::MphRead::NativeRuntime::EndPoint& endPoint, const std::string& address,
        std::int32_t timeoutMs)
    {
        try
        {
            const std::uint8_t hello[] = {
                static_cast<std::uint8_t>(PacketType::Hello),
                static_cast<std::uint8_t>(Detail::NetStatusProtocolVersion()),
                0xFFU
            };
            ::MphRead::NativeRuntime::UdpClientSend(socket, hello, 3, endPoint);

            ::MphRead::NativeRuntime::EndPoint from
                = ::MphRead::NativeRuntime::CreateIPv4AnyEndPoint();
            constexpr std::int64_t ticksPerMillisecond = 10'000;
            const std::int64_t deadline = Detail::NetStatusDateTimeUtcNowTicks()
                + static_cast<std::int64_t>(timeoutMs) * ticksPerMillisecond;
            bool welcomed = false;
            while (Detail::NetStatusDateTimeUtcNowTicks() < deadline)
            {
                std::vector<std::uint8_t> reply
                    = ::MphRead::NativeRuntime::UdpClientReceive(socket, from);
                if (!::MphRead::NativeRuntime::EndPointEquals(from, endPoint))
                {
                    continue;
                }
                if (!reply.empty()
                    && reply[0] == static_cast<std::uint8_t>(PacketType::Welcome))
                {
                    welcomed = true;
                    continue;
                }
                if (reply.size() >= 1U
                        + static_cast<std::size_t>(MatchStatePacket::Size)
                    && reply[0] == static_cast<std::uint8_t>(PacketType::MatchState))
                {
                    const std::uint8_t bye[] = {
                        static_cast<std::uint8_t>(PacketType::Bye)
                    };
                    ::MphRead::NativeRuntime::UdpClientSend(socket, bye, 1, endPoint);

                    MatchStatePacket match = MatchStatePacket::Read(
                        std::span<const std::uint8_t>(reply).subspan(1));
                    if (match.PlayerCount > 0)
                    {
                        --match.PlayerCount;
                    }

                    ServerStatusPacket status;
                    status.Match = std::move(match);
                    status.MaxPlayers = 0;
                    status.ServerName = std::string{};
                    return Describe(std::move(status), true, -1);
                }
            }

            if (welcomed)
            {
                const std::uint8_t bye[] = {
                    static_cast<std::uint8_t>(PacketType::Bye)
                };
                ::MphRead::NativeRuntime::UdpClientSend(socket, bye, 1, endPoint);

                ServerStatus status;
                status.Online = true;
                status.RoomKey = "";
                status.ServerName = "";
                status.Latency = -1;
                status.Legacy = true;
                status.Message
                    = "Online \xC2\xB7 the server did not say what is running.";
                return status;
            }
        }
        catch (...)
        {
        }

        return ServerStatus::Offline(
            "No answer from " + address + ". It may be off, "
            + "or a firewall may be blocking UDP.");
    }

    ServerStatus NetStatus::Describe(
        ServerStatusPacket status, bool legacy, std::int32_t latency)
    {
        MatchStatePacket match = status.Match;
        GameMode mode = Detail::NetStatusIsDefinedGameMode(
                static_cast<std::int32_t>(match.Mode))
            ? static_cast<GameMode>(static_cast<std::int32_t>(match.Mode))
            : Detail::NetStatusBattleGameMode();

        const std::string rawRoom = match.RoomKey.value();
        std::string room = rawRoom;
        if (!room.empty())
        {
            auto [meta, ignored] = Metadata::GetRoomByName(room);
            (void)ignored;
            if (meta != nullptr && meta->InGameName.has_value())
            {
                room = *meta->InGameName;
            }
        }

        std::string players;
        if (status.MaxPlayers > 0)
        {
            players = std::to_string(static_cast<unsigned int>(match.PlayerCount))
                + "/" + std::to_string(static_cast<unsigned int>(status.MaxPlayers))
                + " players";
        }
        else if (match.PlayerCount == 0)
        {
            players = "nobody playing yet";
        }
        else
        {
            players = match.PlayerCount == 1
                ? "1 player"
                : std::to_string(static_cast<unsigned int>(match.PlayerCount))
                    + " players";
        }

        std::string message = !room.empty()
            ? room + " \xC2\xB7 " + ModeName(mode) + " \xC2\xB7 " + players
            : players;
        const std::string serverName = status.ServerName.value_or("");
        if (!serverName.empty())
        {
            message = serverName + " \xC2\xB7 " + message;
        }

        ServerStatus result;
        result.Online = true;
        result.RoomKey = rawRoom;
        result.ServerName = serverName;
        result.Mode = mode;
        result.Players = static_cast<std::int32_t>(match.PlayerCount);
        result.MaxPlayers = static_cast<std::int32_t>(status.MaxPlayers);
        result.TimeRemaining = match.TimeRemaining;
        result.Latency = latency;
        result.Legacy = legacy;
        result.Protocol = static_cast<std::int32_t>(status.Protocol);
        result.Phase = status.Phase;
        result.Format = status.Format;
        result.LobbyEnabled = status.LobbyEnabled;
        result.AllowJoinInProgress = status.AllowJoinInProgress;
        result.CanHost = (status.Flags & ServerStatusPacket::FlagCanHost) != 0;
        result.Message = std::move(message);
        return result;
    }

    std::string NetStatus::ModeName(GameMode mode)
    {
        const std::string name = Detail::NetStatusGameModeToString(mode);
        std::string builder;
        builder.reserve(name.size() + 4U);
        for (std::size_t index = 0; index < name.size(); ++index)
        {
            const char ch = name[index];
            if (index > 0 && ch >= 'A' && ch <= 'Z')
            {
                builder.push_back(' ');
            }
            builder.push_back(ch);
        }
        return builder;
    }
}
