#pragma once

#include "Mods/Network/Domain/MatchDefinition.hpp"
#include "Mods/Network/Session/SessionConnection.hpp"
#include "Mods/Network/Session/SessionPlayerIdentity.hpp"
#include "Mods/Network/Session/SessionRuntimeState.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace MphRead::Mods::Network
{
    // Owns chat packet validation, host relay policy, and local chat routing.
    class SessionChatChannel final
    {
    public:
        SessionChatChannel() = delete;
        using ActiveMatchProvider = std::optional<MatchDefinition> (*)();

        static void Receive(ReceivedPacket packet, double time, SessionConnection& connection,
            const SessionRuntimeState& runtime);
        static void Send(const std::string& value, SessionConnection& connection, NetRole role,
            std::int32_t localSlot, const std::string& playerName, ActiveMatchProvider activeMatch);
    };
}
