#include "Mods/Network/Server/Lobby/ServerLobbyCommandProcessor.hpp"

#include "Mods/Multiplayer/TeamLayout.hpp"
#include "Mods/Network/Server/Lobby/LobbyRules.hpp"
#include "Mods/Network/Server/Lobby/ServerLobbyOwnershipPolicy.hpp"
#include "Mods/Network/Protocol/SessionProtocol.hpp"
#include "Mods/Network/Protocol/Packets/SessionPackets.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

namespace MphRead::Mods::Network
{
    using Peer = ServerPeerRegistry::Peer;
    using PeerHandle = ServerPeerRegistry::PeerHandle;

    std::int8_t ServerLobbyCommandProcessor::ChooseTeam(
        const ServerPeerRegistry& peers, const MatchDefinition& match, const Peer* exclude)
    {
        const Multiplayer::TeamLayout layout = LobbyRules::ResolveTeamLayout(match);
        std::array<std::int32_t, 4> counts{};
        for (const PeerHandle& peer : peers.All())
        {
            if (peer.get() != exclude && peer->Lobby.TeamIndex() >= 0 && peer->Lobby.TeamIndex() < layout.TeamCount)
            {
                counts[static_cast<std::size_t>(peer->Lobby.TeamIndex())]++;
            }
        }
        return Multiplayer::TeamRules::ChooseTeam(layout, counts);
    }

    void ServerLobbyCommandProcessor::NormalizeTeams(
        ServerPeerRegistry& peers, const MatchDefinition& match)
    {
        for (const PeerHandle& peer : peers.All())
        {
            peer->Lobby.SetTeamIndex(-1);
        }
        for (const PeerHandle& peer : peers.All())
        {
            peer->Lobby.SetTeamIndex(ChooseTeam(peers, match, peer.get()));
        }
    }

    void ServerLobbyCommandProcessor::InvalidateReady(ServerPeerRegistry& peers) noexcept
    {
        for (const PeerHandle& peer : peers.All())
        {
            peer->Lobby.InvalidateLobbyReady();
        }
    }

    ServerLobbyCommandExecution ServerLobbyCommandProcessor::Execute(
        ServerLobbyState& state,
        ServerPeerRegistry& peers,
        const PeerHandle& actor,
        const LobbyCommandPacket& command,
        std::int32_t maxPlayers,
        const RosterPacket& roster,
        RoomKeyResolver resolveRoomKey)
    {
        ServerLobbyCommandExecution execution{};
        const auto reject = [&execution](LobbyResultCode code, std::string reason)
        {
            execution.Result = code;
            execution.Reason = std::move(reason);
            return execution;
        };

        if (actor == nullptr)
        {
            return reject(LobbyResultCode::TargetNotFound, "That player has left.");
        }
        if (state.Policy() != ServerSessionPolicy::Lobby || state.Phase() != SessionPhase::Lobby)
        {
            return reject(LobbyResultCode::InvalidPhase, "Wait until the server returns to the lobby.");
        }
        const bool owner = actor->ClientId == state.LobbyOwnerClientId() && actor->ClientId != 0;
        if (command.Type != LobbyCommandType::SetReady && command.Type != LobbyCommandType::SetTeam && !owner)
        {
            return reject(LobbyResultCode::NotOwner, "Only the lobby owner can do that.");
        }
        if (command.ExpectedRevision != state.SessionRevision())
        {
            return reject(LobbyResultCode::StaleRevision,
                "The lobby changed. Review the updated settings and try again.");
        }

        switch (command.Type)
        {
        case LobbyCommandType::SetReady:
            actor->Lobby.SetLobbyReady(command.Ready);
            break;
        case LobbyCommandType::SetTeam:
        {
            PeerHandle target{};
            for (const PeerHandle& candidate : peers.All())
            {
                if (candidate->SlotIndex == command.TargetSlot)
                {
                    target = candidate;
                    break;
                }
            }
            if (target == nullptr)
            {
                return reject(LobbyResultCode::TargetNotFound, "That player has left.");
            }
            if (target != actor && !owner)
            {
                return reject(LobbyResultCode::NotOwner, "Only the owner can move another player.");
            }
            if (state.LockTeams() && !owner)
            {
                return reject(LobbyResultCode::NotOwner, "Team changes are locked by the owner.");
            }
            const MatchDefinition& match = state.LobbyMatch();
            const std::int8_t requestedTeam = command.TeamIndex == -1
                ? ChooseTeam(peers, match, target.get()) : command.TeamIndex;
            if (command.TeamIndex < -1 || requestedTeam < 0 || requestedTeam >= LobbyRules::TeamCount(match))
            {
                return reject(LobbyResultCode::InvalidTeam, "Choose a team for the current format.");
            }
            const auto onTeam = std::count_if(peers.All().begin(), peers.All().end(),
                [&target, requestedTeam](const PeerHandle& peer)
                {
                    return peer != target && peer->Lobby.TeamIndex() == requestedTeam;
                });
            if (onTeam >= LobbyRules::TeamCapacity(match, requestedTeam))
            {
                return reject(LobbyResultCode::TeamFull, "That team is full.");
            }
            target->Lobby.MoveToTeam(requestedTeam);
            break;
        }
        case LobbyCommandType::UpdateMatch:
        {
            const MatchDefinition proposed = command.Configuration.Match;
            const LobbyResultCode valid = LobbyRules::ValidateDefinition(proposed, execution.Reason);
            if (valid != LobbyResultCode::Ok)
            {
                execution.Result = valid;
                return execution;
            }
            const std::optional<std::string> room = resolveRoomKey != nullptr
                ? resolveRoomKey(proposed.RoomKey.value_or("")) : std::nullopt;
            if (!room.has_value())
            {
                return reject(LobbyResultCode::MapUnavailable, "The server does not have that map.");
            }
            const Multiplayer::TeamLayout proposedLayout = LobbyRules::ResolveTeamLayout(proposed);
            if (proposedLayout.TeamCount > 0
                && (proposedLayout.TotalPlayers() < static_cast<std::int32_t>(peers.All().size())
                    || (LobbyRules::ExactTeams(proposed) && proposedLayout.TotalPlayers() > maxPlayers)))
            {
                return reject(LobbyResultCode::InvalidConfiguration,
                    "The layout must fit the connected roster and server player limit.");
            }
            const bool topologyChanged = !(proposedLayout == LobbyRules::ResolveTeamLayout(state.LobbyMatch()));
            MatchDefinition normalized = proposed;
            normalized.RoomKey = *room;
            state.SetLobbyMatch(std::move(normalized));
            state.SetSessionOptions(command.Configuration.RequireReady(),
                command.Configuration.AllowJoinInProgress());
            state.SetLockTeams(command.Configuration.LockTeams());
            if (topologyChanged)
            {
                NormalizeTeams(peers, state.LobbyMatch());
            }
            InvalidateReady(peers);
            break;
        }
        case LobbyCommandType::StartMatch:
        {
            const LobbyResultCode start = LobbyRules::Validate(
                state.LobbyMatch(), roster, state.RequireReady(), execution.Reason);
            if (start != LobbyResultCode::Ok)
            {
                execution.Result = start;
                return execution;
            }
            state.FreezeMatch(state.LobbyMatch());
            state.FreezeWorldProfile(LobbyRules::ResolveWorldProfile(state.FrozenMatch(),
                std::max<std::int32_t>(2, static_cast<std::int32_t>(peers.All().size()))));
            execution.NextAction = ServerLobbyCommandExecution::Action::StartMatch;
            return execution;
        }
        case LobbyCommandType::KickPlayer:
        case LobbyCommandType::TransferOwner:
        {
            PeerHandle selected{};
            for (const PeerHandle& candidate : peers.All())
            {
                if (candidate->SlotIndex == command.TargetSlot)
                {
                    selected = candidate;
                    break;
                }
            }
            if (selected == nullptr || selected == actor)
            {
                return reject(LobbyResultCode::TargetNotFound, "Choose another connected player.");
            }
            if (command.Type == LobbyCommandType::TransferOwner)
            {
                if (!ServerLobbyOwnershipPolicy::TryTransfer(state, actor, selected))
                {
                    return reject(LobbyResultCode::NotOwner, "Only the lobby owner can transfer ownership.");
                }
            }
            else
            {
                execution.NextAction = ServerLobbyCommandExecution::Action::KickPeer;
                execution.TargetPeer = std::move(selected);
            }
            break;
        }
        }
        return execution;
    }
}
