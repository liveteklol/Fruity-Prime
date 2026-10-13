#pragma once

#include "Mods/Network/Server/Core/ServerPeerRegistry.hpp"
#include "Mods/Network/Server/Lobby/ServerLobbyState.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace MphRead::Mods::Network
{
    struct LobbyCommandPacket;
    struct RosterPacket;

    struct ServerLobbyCommandExecution final
    {
        enum class Action : std::uint8_t { None, StartMatch, KickPeer };

        LobbyResultCode Result = LobbyResultCode::Ok;
        Action NextAction = Action::None;
        ServerPeerRegistry::PeerHandle TargetPeer{};
        std::string Reason{};
    };

    // Owns lobby command authorization and policy mutations. Server effects
    // such as loading a map, sending a refusal, or removing a connection are
    // returned to the dedicated-server lifecycle owner.
    class ServerLobbyCommandProcessor final
    {
    public:
        using RoomKeyResolver = std::optional<std::string> (*)(const std::string&);

        ServerLobbyCommandProcessor() = delete;

        [[nodiscard]] static ServerLobbyCommandExecution Execute(
            ServerLobbyState& state,
            ServerPeerRegistry& peers,
            const ServerPeerRegistry::PeerHandle& actor,
            const LobbyCommandPacket& command,
            std::int32_t maxPlayers,
            const RosterPacket& roster,
            RoomKeyResolver resolveRoomKey);

        static void InvalidateReady(ServerPeerRegistry& peers) noexcept;
        static void NormalizeTeams(ServerPeerRegistry& peers, const MatchDefinition& match);
        [[nodiscard]] static std::int8_t ChooseTeam(
            const ServerPeerRegistry& peers, const MatchDefinition& match,
            const ServerPeerRegistry::Peer* exclude = nullptr);
    };
}
