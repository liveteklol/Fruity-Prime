#pragma once

#include "Mods/Network/Domain/MatchDefinition.hpp"

#include "NativeRuntime/System/Net.hpp"

#include <cstdint>
#include <string>

namespace MphRead
{
    enum class GameMode : std::uint8_t;
}

namespace MphRead::Mods::Network
{
    struct ServerStatusPacket;

    struct ServerStatus
    {
        SessionPhase Phase = SessionPhase::Lobby;
        MatchFormat Format = MatchFormat::Auto;
        bool LobbyEnabled = false;
        bool AllowJoinInProgress = false;
        bool Online = false;
        std::string RoomKey{};
        GameMode Mode{};
        std::int32_t Players = 0;
        std::int32_t MaxPlayers = 0;
        float TimeRemaining = 0.0F;
        std::string ServerName{};
        std::int32_t Latency = 0;
        std::string Message{};
        bool Legacy = false;
        std::int32_t Protocol = 0;
        // Whether this server will open a *new* match for you, on a port of
        // its own. False for a server that did not say: hosting is off
        // unless an admin passed -hostports, so silence and no are the same.
        bool CanHost = false;

        [[nodiscard]] static ServerStatus Offline(const std::string& message);
    };

    class NetStatus final
    {
    public:
        NetStatus() = delete;
        NetStatus(const NetStatus&) = delete;
        NetStatus(NetStatus&&) = delete;
        NetStatus& operator=(const NetStatus&) = delete;
        NetStatus& operator=(NetStatus&&) = delete;

        [[nodiscard]] static ServerStatus Query(const std::string& address,
            std::int32_t port, bool allowJoinProbe, std::int32_t timeoutMs = 1200);

        [[nodiscard]] static std::string ModeName(GameMode mode);

    private:
        [[nodiscard]] static ServerStatus JoinProbe(::MphRead::NativeRuntime::SocketHandle socket,
            const ::MphRead::NativeRuntime::EndPoint& endPoint, const std::string& address,
            std::int32_t timeoutMs);

        [[nodiscard]] static ServerStatus Describe(ServerStatusPacket status,
            bool legacy, std::int32_t latency);
    };
}
