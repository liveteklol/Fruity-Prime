#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace MphRead::Mods::Network
{
    struct ServerPlayerIdentity final
    {
        std::string Name{};
        std::uint8_t Hunter = 0;
        std::uint8_t Color = 0;
    };

    // Parses and validates the client's identity payload independently of peer
    // liveness, lobby updates, and announcements.
    class ServerPeerIdentity final
    {
    public:
        [[nodiscard]] static std::optional<ServerPlayerIdentity> Parse(std::span<const std::uint8_t> payload);
    };
}
