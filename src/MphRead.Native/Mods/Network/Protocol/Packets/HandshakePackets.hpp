#pragma once

#include "Mods/Network/Protocol/ConnectionSecret.hpp"
#include "NativeRuntime/System/Guid.hpp"
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace MphRead::Mods::Network
{
    struct RefusedPacket
    {
        static constexpr std::int32_t Size = 3;
        static constexpr std::uint8_t ReasonFull = 1;
        static constexpr std::uint8_t ReasonProtocol = 2;
        static constexpr std::uint8_t ReasonKicked = 3;
        static constexpr std::uint8_t ReasonInMatch = 4;
        // A Hello naming a connection already held, without its secret.
        static constexpr std::uint8_t ReasonIdentity = 5;

        std::uint8_t Reason = 0;
        std::uint8_t Players = 0;
        std::uint8_t MaxPlayers = 0;

        void Write(std::span<std::uint8_t> dest) const;
        [[nodiscard]] static RefusedPacket Read(std::span<const std::uint8_t> src);
        [[nodiscard]] std::string Describe(const std::optional<std::string>& where) const;
    };

    // A client asking for a slot, or saying where it now is.
    struct HelloPacket
    {
        static constexpr std::int32_t Size = 38;
        static constexpr std::uint8_t AnySlot = 0xFF;

        std::uint8_t Protocol = 0;
        // The slot this client held before, or AnySlot.
        std::uint8_t Slot = AnySlot;
        std::uint32_t ClientId = 0;
        // Room ownership for a lobby this client asked a directory to open;
        // nothing to do with who the connection belongs to.
        ::MphRead::NativeRuntime::Guid OwnerToken{};
        ConnectionSecret Secret{};

        void Write(std::span<std::uint8_t> dest) const;
        // What a short hello (a reachability probe) leaves out reads as
        // unset -- and a connection opened without a secret is never moved.
        [[nodiscard]] static HelloPacket Read(std::span<const std::uint8_t> src);
    };

    // A client's own player touched an item a spawner put out (protocol 29).
    // The authority checks it against the spawn it names and where it had
    // that player, and grants it once -- to whoever it reaches first.
}
