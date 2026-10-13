#pragma once

#include "Mods/Network/Server/Core/ServerPeerRegistry.hpp"
#include "Mods/Network/Server/Lobby/ServerLobbyState.hpp"

namespace MphRead::Mods::Network
{
    // Owns lobby-owner claim, transfer, and disconnect succession rules.
    class ServerLobbyOwnershipPolicy final
    {
    public:
        ServerLobbyOwnershipPolicy() = delete;

        [[nodiscard]] static bool TryClaim(
            ServerLobbyState& state,
            const ServerPeerRegistry::PeerHandle& peer,
            const NativeRuntime::Guid& ownerToken) noexcept;

        [[nodiscard]] static bool TryTransfer(
            ServerLobbyState& state,
            const ServerPeerRegistry::PeerHandle& actor,
            const ServerPeerRegistry::PeerHandle& target) noexcept;

        [[nodiscard]] static bool ReassignAfterRemoval(
            ServerLobbyState& state,
            std::uint32_t removedClientId,
            std::uint32_t successorClientId) noexcept;
    };
}
