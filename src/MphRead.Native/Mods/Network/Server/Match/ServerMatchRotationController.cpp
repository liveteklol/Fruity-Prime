#include "Mods/Network/Server/Match/ServerMatchRotationController.hpp"

#include "Mods/Network/Server/Core/ServerPeerRegistry.hpp"
#include "Mods/Network/Server/Core/ServerSlotIdentity.hpp"
#include "Mods/Network/Server/Lobby/LobbyRules.hpp"
#include "Mods/Network/Server/Lobby/ServerLobbyCommandProcessor.hpp"
#include "Mods/Network/Server/Lobby/ServerLobbyState.hpp"
#include "Mods/Network/Server/Match/ServerSnapshotStream.hpp"
#include "Mods/Network/Session/NetLifecycleTracker.hpp"

#include <algorithm>
#include <cstdint>

namespace MphRead::Mods::Network
{
    void ServerMatchRotationController::Advance(const MatchDefinition& nextDefinition,
        double now, ServerLobbyState& lobby, ServerPeerRegistry& peers,
        ServerSlotIdentity& slotIdentity, ServerSnapshotStream& snapshots,
        std::uint16_t& matchId, double& matchStarted, double& matchEndedAt)
    {
        lobby.SetMapWorldProfile(LobbyRules::ResolveWorldProfile(nextDefinition,
            std::max<std::int32_t>(2, static_cast<std::int32_t>(peers.All().size()))));
        ServerLobbyCommandProcessor::NormalizeTeams(peers, nextDefinition);

        matchStarted = now;
        matchEndedAt = -1;
        matchId = NetLifecycleTracker::Next(matchId);
        snapshots.ResetIncomingSequence();
        slotIdentity.ResetLives();
        for (const ServerPeerRegistry::PeerHandle& peer : peers.All())
        {
            peer->Intent.Reset();
        }
    }
}
