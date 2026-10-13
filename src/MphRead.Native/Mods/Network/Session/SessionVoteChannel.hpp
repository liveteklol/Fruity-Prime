#pragma once

#include "Mods/Network/Session/SessionConnection.hpp"
#include "Mods/Network/Session/SessionRuntimeState.hpp"

#include <cstdint>
#include <string>

namespace MphRead::Mods::Network
{
    // Encodes client map-vote and map-pick commands for the host channel.
    class SessionVoteChannel final
    {
    public:
        SessionVoteChannel() = delete;

        static void SendVote(SessionConnection& connection, NetRole role,
            std::uint8_t kind, const std::string& roomKey);
        static void SendMapPick(SessionConnection& connection, NetRole role, const std::string& roomKey);
    };
}
