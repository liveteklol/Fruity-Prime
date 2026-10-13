#pragma once

#include "Mods/Network/Server/Lobby/ServerLobbyState.hpp"

#include <cstdint>

namespace MphRead::Mods::Network
{
    struct ServerLobbyLoadDecision final
    {
        enum class Action : std::uint8_t { Ignore, Wait, StartMatch };

        Action NextAction = Action::Ignore;
        bool DeadlineElapsed = false;
    };

    // Owns the policy for accepting match-load acknowledgements and releasing
    // the lobby's start barrier. Packet I/O and match startup remain server effects.
    class ServerLobbyLoadBarrier final
    {
    public:
        ServerLobbyLoadBarrier() = delete;

        [[nodiscard]] static bool AcceptLoaded(
            ServerLobbyState& state,
            std::int32_t slot,
            std::uint16_t packetMatchId,
            std::uint16_t currentMatchId) noexcept;

        [[nodiscard]] static ServerLobbyLoadDecision Evaluate(
            const ServerLobbyState& state, double now) noexcept;
    };
}
