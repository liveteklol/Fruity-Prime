#include "Mods/Network/Server/Core/ServerPeerIdentityChannel.hpp"

#include "Mods/Network/Server/Lobby/ServerLobbyState.hpp"
#include "Mods/Network/Transport/NetTransport.hpp"
#include "NativeRuntime/System/Net.hpp"

#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace
{
    using MphRead::Mods::Network::ReceivedPacket;
    using MphRead::Mods::Network::ServerLobbyState;
    using MphRead::Mods::Network::ServerPeerIdentityChannel;
    using MphRead::Mods::Network::ServerPeerRegistry;
    using MphRead::Mods::Network::SessionPhase;

    bool Check(bool condition, const char* name)
    {
        if (!condition)
        {
            std::cerr << "FAIL " << name << '\n';
            return false;
        }
        return true;
    }

    ReceivedPacket IdentityPacket(const std::shared_ptr<System::Net::IPEndPoint>& endpoint,
        std::uint8_t hunter, std::uint8_t color, const char* name)
    {
        auto payload = std::make_shared<std::vector<std::uint8_t>>();
        payload->push_back(static_cast<std::uint8_t>(MphRead::Mods::Network::PacketType::Identify));
        payload->push_back(hunter);
        payload->push_back(color);
        for (const unsigned char character : std::string(name))
        {
            payload->push_back(character);
        }
        return ReceivedPacket(endpoint, payload, static_cast<std::int32_t>(payload->size()));
    }
}

int main()
{
    ServerPeerRegistry peers;
    ServerLobbyState lobby;
    const auto firstEndpoint = System::Net::IPEndPoint::Loopback(31101);
    const auto secondEndpoint = System::Net::IPEndPoint::Loopback(31102);
    auto first = std::make_shared<ServerPeerRegistry::Peer>();
    first->EndPoint = firstEndpoint;
    first->SlotIndex = 0;
    first->Lobby.SetLobbyReady(true);
    peers.Add(first);

    bool passed = true;
    const auto initial = IdentityPacket(firstEndpoint, 0, 1, "Alice");
    const auto firstChange = ServerPeerIdentityChannel::Receive(initial, 20.0, peers, lobby);
    passed &= Check(firstChange.has_value() && firstChange->Peer == first && firstChange->FirstName,
        "a valid first identity is applied and reported for announcement");
    passed &= Check(first->Profile.Name() == "Alice" && first->Profile.Hunter() == 0
        && first->Profile.Color() == 1 && !first->Lobby.LobbyReady(),
        "identity appearance updates the profile and invalidates lobby readiness");
    passed &= Check(!first->Liveness.TimedOut(24.0, 5.0),
        "a connected identity packet refreshes peer liveness");

    const auto unchanged = ServerPeerIdentityChannel::Receive(initial, 21.0, peers, lobby);
    passed &= Check(!unchanged.has_value(), "an identical identity creates no second update");
    const auto malformed = IdentityPacket(firstEndpoint, 255, 3, "Mallory");
    passed &= Check(!ServerPeerIdentityChannel::Receive(malformed, 22.0, peers, lobby).has_value()
        && first->Profile.Name() == "Alice",
        "an invalid hunter cannot mutate the current profile");

    lobby.SetPhase(SessionPhase::Starting);
    lobby.ExpectLoadedSlot(0);
    const auto locked = IdentityPacket(firstEndpoint, 2, 2, "Changed");
    passed &= Check(!ServerPeerIdentityChannel::Receive(locked, 23.0, peers, lobby).has_value()
        && first->Profile.Name() == "Alice",
        "an expected loaded peer cannot change identity during match startup");

    auto second = std::make_shared<ServerPeerRegistry::Peer>();
    second->EndPoint = secondEndpoint;
    second->SlotIndex = 1;
    peers.Add(second);
    const auto secondChange = IdentityPacket(secondEndpoint, 2, 2, "Bob");
    const auto appliedDuringStart = ServerPeerIdentityChannel::Receive(secondChange, 24.0, peers, lobby);
    passed &= Check(appliedDuringStart.has_value() && appliedDuringStart->Peer == second
        && second->Profile.Name() == "Bob",
        "startup identity lock applies only to peers in the load barrier");

    const auto unknown = IdentityPacket(System::Net::IPEndPoint::Loopback(31103), 1, 0, "Unknown");
    passed &= Check(!ServerPeerIdentityChannel::Receive(unknown, 25.0, peers, lobby).has_value(),
        "identity packets from an unknown endpoint do not create a peer");

    if (!passed)
    {
        return 1;
    }
    std::cout << "ServerPeerIdentityChannel PASS\n";
    return 0;
}
