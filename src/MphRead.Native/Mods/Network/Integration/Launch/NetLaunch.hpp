#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "Mods/Multiplayer/MatchWorldProfile.hpp"
#include <stop_token>
#include "NativeRuntime/System/Guid.hpp"

namespace MphRead
{
    enum class GameMode : std::uint8_t;
    enum class Hunter : std::uint8_t;
    class Scene;
    class RenderWindow;
}

namespace MphRead::Mods::Network
{
    struct NetLaunchServerRoom
    {
        std::string RoomKey;
        GameMode Mode;
    };

    class NetLaunch final
    {
    public:
        NetLaunch() = delete;

        static bool Connect(const std::string& address, std::int32_t port,
            const std::string& playerName, Hunter hunter,
            std::int32_t timeoutMs = 8000, std::int32_t color = -1,
            NativeRuntime::Guid ownerToken = {},
            std::stop_token cancellationToken = {});
        static bool Join(const std::string& address, std::int32_t port,
            const std::string& playerName, Hunter hunter,
            std::int32_t timeoutMs = 8000, std::int32_t color = -1);
        static bool TickTerminalLobby(RenderWindow& window);

        static const std::string& LastJoinError();
        static void DisableCheatsForMatch();
        static std::optional<NetLaunchServerRoom> ServerRoom();

        [[nodiscard]] static Multiplayer::MatchWorldProfile WorldProfile();
        [[nodiscard]] static std::int32_t RoomPlayerCount();

        static void BuildPlayers(Scene& scene, Hunter localHunter,
            std::int32_t localRecolor, bool teams = false,
            std::optional<std::int32_t> localSlot = std::nullopt);

    private:
        static void PollTerminalInput();

        static bool _terminalLobby;
        static std::string _terminalInput;
        static std::string _lastJoinError;

        static std::string DescribeJoinFailure(const std::string& address,
            std::int32_t port);
    };
}
