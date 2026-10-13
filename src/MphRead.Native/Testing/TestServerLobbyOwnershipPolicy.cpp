#include "Mods/Network/Server/Lobby/ServerLobbyOwnershipPolicy.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string_view>

namespace
{
    using namespace MphRead::Mods::Network;

    bool Check(bool condition, std::string_view name)
    {
        if (!condition)
        {
            std::cerr << "FAIL " << name << '\n';
            return false;
        }
        return true;
    }
}

int main()
{
    using namespace MphRead::Mods::Network;
    using MphRead::NativeRuntime::Guid;

    ServerLobbyState state;
    state.SetPhase(SessionPhase::Lobby);
    state.SetPolicy(ServerSessionPolicy::Continuous);

    auto owner = std::make_shared<ServerPeerRegistry::Peer>();
    owner->ClientId = 10;
    auto guest = std::make_shared<ServerPeerRegistry::Peer>();
    guest->ClientId = 20;
    auto successor = std::make_shared<ServerPeerRegistry::Peer>();
    successor->ClientId = 30;

    std::array<std::uint8_t, 16> tokenBytes{};
    tokenBytes[0] = 1;
    const Guid token(tokenBytes);
    state.SetOwnerToken(token);

    bool passed = true;
    passed &= Check(!ServerLobbyOwnershipPolicy::TryClaim(state, owner, token)
        && state.LobbyOwnerClientId() == 0, "continuous servers do not assign lobby ownership");
    state.SetPolicy(ServerSessionPolicy::Lobby);
    passed &= Check(!ServerLobbyOwnershipPolicy::TryClaim(state, owner, Guid::Empty())
        && state.OwnerToken() == token, "an incorrect owner token leaves ownership unchanged");
    passed &= Check(ServerLobbyOwnershipPolicy::TryClaim(state, owner, token)
        && state.LobbyOwnerClientId() == owner->ClientId
        && state.OwnerToken() == Guid::Empty(), "the matching token claims ownership once");
    passed &= Check(!ServerLobbyOwnershipPolicy::TryClaim(state, guest, token)
        && state.LobbyOwnerClientId() == owner->ClientId, "a second peer cannot replace the owner by claiming");

    passed &= Check(!ServerLobbyOwnershipPolicy::TryTransfer(state, guest, successor)
        && state.LobbyOwnerClientId() == owner->ClientId, "a guest cannot transfer ownership");
    passed &= Check(ServerLobbyOwnershipPolicy::TryTransfer(state, owner, guest)
        && state.LobbyOwnerClientId() == guest->ClientId, "the owner may transfer to another peer");
    passed &= Check(!ServerLobbyOwnershipPolicy::TryTransfer(state, guest, guest)
        && state.LobbyOwnerClientId() == guest->ClientId, "ownership cannot transfer to the same peer");

    passed &= Check(!ServerLobbyOwnershipPolicy::ReassignAfterRemoval(state, owner->ClientId, successor->ClientId)
        && state.LobbyOwnerClientId() == guest->ClientId, "removing a non-owner does not replace ownership");
    passed &= Check(ServerLobbyOwnershipPolicy::ReassignAfterRemoval(state, guest->ClientId, successor->ClientId)
        && state.LobbyOwnerClientId() == successor->ClientId, "owner removal selects the supplied successor");
    passed &= Check(ServerLobbyOwnershipPolicy::ReassignAfterRemoval(state, successor->ClientId, 0)
        && state.LobbyOwnerClientId() == 0, "removing the final owner clears ownership");

    return passed ? 0 : 1;
}
