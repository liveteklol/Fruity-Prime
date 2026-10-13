#include "Mods/Network/Server/Match/ServerMatchRotationController.hpp"

#include "Mods/Multiplayer/MatchWorldProfile.hpp"
#include "Mods/Network/Server/Core/ServerPeerRegistry.hpp"
#include "Mods/Network/Server/Core/ServerSlotIdentity.hpp"
#include "Mods/Network/Server/Lobby/ServerLobbyState.hpp"
#include "Mods/Network/Server/Match/ServerSnapshotStream.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <memory>
#include <utility>

namespace
{
    using MphRead::Mods::Multiplayer::MatchWorldProfile;
    using MphRead::Mods::Network::MatchDefinition;
    using MphRead::Mods::Network::ServerLobbyState;
    using MphRead::Mods::Network::ServerMatchRotationController;
    using MphRead::Mods::Network::ServerPeerRegistry;
    using MphRead::Mods::Network::ServerSlotIdentity;
    using MphRead::Mods::Network::ServerSnapshotStream;
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
}

int main()
{
    ServerLobbyState lobby;
    lobby.SetPhase(SessionPhase::InMatch);
    ServerPeerRegistry peers;
    for (std::int32_t slot = 0; slot < 3; ++slot)
    {
        auto peer = std::make_shared<ServerPeerRegistry::Peer>();
        peer->SlotIndex = slot;
        peer->Lobby.SetPostMatchReady(true);
        static_cast<void>(peer->Intent.Observe(70 + static_cast<std::uint32_t>(slot), false));
        peers.Add(std::move(peer));
    }

    ServerSlotIdentity slots;
    slots.EnsureGeneration(0);
    slots.SetLife(0, 12);
    ServerSnapshotStream snapshots;
    const std::array<std::uint8_t, 1> previousSnapshot{1};
    bool passed = true;
    passed &= Check(snapshots.AcceptIncoming(100, previousSnapshot)
        && !snapshots.CanAcceptIncoming(1),
        "a prior map's authority sequence is active before rotation");

    MatchDefinition next{};
    next.Mode = MphRead::GameMode::Battle;
    next.Format = MphRead::Mods::Network::MatchFormat::FreeForAll;
    next.RoomKey = "rotation-test";
    std::uint16_t matchId = 8;
    double matchStarted = 1.0;
    double matchEndedAt = 2.0;
    ServerMatchRotationController::Advance(next, 25.0, lobby, peers, slots,
        snapshots, matchId, matchStarted, matchEndedAt);

    passed &= Check(lobby.Phase() == SessionPhase::InMatch,
        "rotation starts the next match phase");
    passed &= Check(lobby.MapWorldProfile() == MatchWorldProfile::Resolve(3),
        "the next world profile follows connected players rather than server capacity");
    passed &= Check(matchId == 9 && matchStarted == 25.0 && matchEndedAt == -1.0,
        "rotation advances match identity and clears the ended timestamp");
    passed &= Check(snapshots.CanAcceptIncoming(1),
        "rotation resets the incoming snapshot sequence for the new map");
    passed &= Check(slots.Generation(0) == 1 && slots.Life(0) == 0,
        "rotation preserves occupant generation and resets per-map lives");
    for (std::int32_t slot = 0; slot < 3; ++slot)
    {
        passed &= Check(peers.All()[static_cast<std::size_t>(slot)]->Intent.LastFrame() == 0,
            "rotation clears each peer's prior-map intent frame");
    }

    if (!passed)
    {
        return 1;
    }
    std::cout << "ServerMatchRotationController PASS\n";
    return 0;
}
