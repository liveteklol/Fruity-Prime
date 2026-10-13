#pragma once

#include <cstdint>
#include <span>

namespace MphRead::Mods::Network
{
    class ClientConnectionHealth;
    class SessionAuthorityState;
    class SessionMatchIdentity;
    class SessionRuntimeState;

    // Applies handshake and connection-admission packets to the session owners.
    class SessionAdmissionChannel final
    {
    public:
        SessionAdmissionChannel() = delete;

        using StreamMatcher = bool (*)(std::uint16_t, std::uint64_t);

        static void HandleWelcome(std::span<const std::uint8_t> payload, std::uint32_t clientId,
            SessionRuntimeState& runtime, SessionMatchIdentity& matchIdentity,
            ClientConnectionHealth& connectionHealth, SessionAuthorityState& authority,
            StreamMatcher matchesStream);
        static void HandleAuthority(std::span<const std::uint8_t> payload,
            SessionRuntimeState& runtime, SessionAuthorityState& authority,
            StreamMatcher matchesStream);
        static void HandleRefused(std::span<const std::uint8_t> payload,
            std::int32_t localSlot, ClientConnectionHealth& connectionHealth);
    };
}
