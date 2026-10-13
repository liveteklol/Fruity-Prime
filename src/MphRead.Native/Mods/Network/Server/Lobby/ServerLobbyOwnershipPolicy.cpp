#include "Mods/Network/Server/Lobby/ServerLobbyOwnershipPolicy.hpp"

namespace MphRead::Mods::Network
{
    bool ServerLobbyOwnershipPolicy::TryClaim(
        ServerLobbyState& state,
        const ServerPeerRegistry::PeerHandle& peer,
        const NativeRuntime::Guid& ownerToken) noexcept
    {
        if (peer == nullptr
            || state.Policy() != ServerSessionPolicy::Lobby
            || state.LobbyOwnerClientId() != 0
            || peer->ClientId == 0)
        {
            return false;
        }

        const NativeRuntime::Guid& expectedToken = state.OwnerToken();
        if (expectedToken != NativeRuntime::Guid::Empty() && ownerToken != expectedToken)
        {
            return false;
        }

        state.SetLobbyOwnerClientId(peer->ClientId);
        state.SetOwnerToken(NativeRuntime::Guid::Empty());
        return true;
    }

    bool ServerLobbyOwnershipPolicy::TryTransfer(
        ServerLobbyState& state,
        const ServerPeerRegistry::PeerHandle& actor,
        const ServerPeerRegistry::PeerHandle& target) noexcept
    {
        if (actor == nullptr || target == nullptr || actor == target
            || actor->ClientId == 0 || actor->ClientId != state.LobbyOwnerClientId()
            || state.Policy() != ServerSessionPolicy::Lobby
            || state.Phase() != SessionPhase::Lobby)
        {
            return false;
        }

        state.SetLobbyOwnerClientId(target->ClientId);
        return true;
    }

    bool ServerLobbyOwnershipPolicy::ReassignAfterRemoval(
        ServerLobbyState& state,
        std::uint32_t removedClientId,
        std::uint32_t successorClientId) noexcept
    {
        if (state.LobbyOwnerClientId() != removedClientId)
        {
            return false;
        }

        state.SetLobbyOwnerClientId(successorClientId);
        return true;
    }
}
