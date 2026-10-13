#include "Mods/Network/Server/Core/ServerHandshake.hpp"

#include "NativeRuntime/System/Net.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <memory>

namespace
{
    using MphRead::Mods::Network::ConnectionSecret;
    using MphRead::Mods::Network::ServerHandshake;
    using MphRead::Mods::Network::ServerPeerRegistry;
    using MphRead::Mods::Network::ServerSlotIdentity;

    bool IsBotReserved(std::int32_t slot)
    {
        return slot == 1 || slot == 5;
    }

    bool Check(bool condition, const char* name)
    {
        if (!condition)
        {
            std::cerr << "FAIL " << name << '\n';
            return false;
        }
        return true;
    }

    ConnectionSecret Secret(std::uint8_t seed)
    {
        ConnectionSecret value{};
        for (std::size_t i = 0; i < value.size(); ++i)
        {
            value[i] = static_cast<std::uint8_t>(seed + i);
        }
        return value;
    }
}

int main()
{
    ServerPeerRegistry peers;
    ServerSlotIdentity slotIdentity;
    const auto originalEndpoint = System::Net::IPEndPoint::Loopback(31001);
    const auto reboundEndpoint = System::Net::IPEndPoint::Loopback(31002);
    const ConnectionSecret heldSecret = Secret(10);
    const ConnectionSecret wrongSecret = Secret(20);
    constexpr std::int32_t Slot = 3;
    constexpr std::int32_t Capacity = 8;
    constexpr std::uint32_t ClientId = 42;
    bool passed = true;

    const auto registered = ServerHandshake::Register(peers, slotIdentity, originalEndpoint,
        Slot, Capacity, &IsBotReserved, ClientId, heldSecret, 1);
    passed &= Check(registered != nullptr && peers.Count() == 1,
        "registration adds one authenticated peer");
    passed &= Check(registered != nullptr && registered->SlotIndex == Slot
        && registered->ClientId == ClientId && registered->Secret == heldSecret,
        "registration binds the selected slot, identity, and connection secret");
    passed &= Check(registered != nullptr && registered->Lobby.TeamIndex() == 1,
        "registration applies the selected team before publishing the peer");
    passed &= Check(slotIdentity.Generation(static_cast<std::size_t>(Slot)) != 0,
        "registration advances the slot generation");
    passed &= Check(ServerHandshake::Register(peers, slotIdentity, reboundEndpoint,
        1, Capacity, &IsBotReserved, ClientId + 1, heldSecret, 0) == nullptr
        && peers.Count() == 1,
        "registration refuses a slot reserved for a server bot");
    passed &= Check(ServerHandshake::Register(peers, slotIdentity, reboundEndpoint,
        Slot, Capacity, &IsBotReserved, ClientId + 1, heldSecret, 0) == nullptr
        && peers.Count() == 1,
        "registration refuses a slot already occupied by a human");
    passed &= Check(slotIdentity.Generation(1) == 0,
        "a refused registration does not advance a bot-reserved slot generation");

    const auto existing = ServerHandshake::Resolve(peers, originalEndpoint, ClientId, wrongSecret);
    passed &= Check(existing.Kind == ServerHandshake::ResolutionKind::Existing
        && existing.Peer == registered,
        "the current endpoint remains bound to its client identity");

    const auto conflict = ServerHandshake::Resolve(peers, originalEndpoint, ClientId + 1, heldSecret);
    passed &= Check(conflict.Kind == ServerHandshake::ResolutionKind::EndpointConflict
        && conflict.Peer == registered,
        "a different identity on an occupied endpoint is reported for lifecycle removal");

    const auto rejected = ServerHandshake::Resolve(peers, reboundEndpoint, ClientId, wrongSecret);
    passed &= Check(rejected.Kind == ServerHandshake::ResolutionKind::InvalidSecret
        && rejected.Peer == registered && registered->EndPoint == originalEndpoint,
        "an invalid secret cannot move an existing slot");

    const auto rebound = ServerHandshake::Resolve(peers, reboundEndpoint, ClientId, heldSecret);
    passed &= Check(rebound.Kind == ServerHandshake::ResolutionKind::Rebound
        && rebound.Peer == registered && rebound.PreviousEndpoint == originalEndpoint
        && registered->EndPoint == reboundEndpoint,
        "a valid secret rebinds the existing peer without reallocating its slot");
    passed &= Check(ServerHandshake::Resolve(peers, reboundEndpoint, ClientId, heldSecret).Kind
        == ServerHandshake::ResolutionKind::Existing,
        "the rebound endpoint is recognized on the next hello");

    const std::uint16_t previousGeneration = slotIdentity.Generation(static_cast<std::size_t>(Slot));
    passed &= Check(peers.Remove(registered), "disconnect removes the prior peer before a fresh join");
    passed &= Check(ServerHandshake::Resolve(peers, reboundEndpoint, ClientId, heldSecret).Kind
        == ServerHandshake::ResolutionKind::NotFound,
        "a removed peer cannot be rebound from stale connection state");
    const auto rejoined = ServerHandshake::Register(peers, slotIdentity, reboundEndpoint,
        Slot, Capacity, &IsBotReserved, ClientId, heldSecret, 1);
    passed &= Check(rejoined != nullptr
        && slotIdentity.Generation(static_cast<std::size_t>(Slot)) != previousGeneration,
        "a fresh rejoin advances generation so packets from the prior occupant are stale");

    if (!passed)
    {
        return 1;
    }
    std::cout << "ServerHandshake PASS\n";
    return 0;
}
