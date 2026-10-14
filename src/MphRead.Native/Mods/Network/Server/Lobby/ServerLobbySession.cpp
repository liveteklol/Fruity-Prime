#include "Mods/Network/Server/Lobby/ServerLobbySession.hpp"

#include "Mods/Network/Server/Lobby/LobbyRules.hpp"
#include "Mods/Network/Server/Lobby/ServerLobbyCommandProcessor.hpp"
#include "Mods/Network/Server/Lobby/ServerLobbyLoadBarrier.hpp"
#include "Mods/Network/Server/Lobby/ServerLobbyOwnershipPolicy.hpp"
#include "Mods/Network/Session/NetLifecycleTracker.hpp"
#include "Mods/Network/Transport/NetTransport.hpp"
#include "Mods/Multiplayer/TeamLayout.hpp"
#include "NativeRuntime/System/Globalization.hpp"

#include <cstdio>
#include <exception>
#include <optional>
#include <span>
#include <utility>

namespace MphRead::Mods::Network
{
    ServerLobbySession::ServerLobbySession(Ports ports) noexcept
        : _ports(ports)
    {
    }

    MatchDefinition ServerLobbySession::CurrentDefinition() const
    {
        return _ports.CurrentDefinition != nullptr
            ? _ports.CurrentDefinition(_ports.Context) : MatchDefinition{};
    }

    MatchDefinition ServerLobbySession::DefinitionFor(const RotationEntry& entry) const
    {
        return _ports.DefinitionFor != nullptr
            ? _ports.DefinitionFor(_ports.Context, entry) : MatchDefinition{};
    }

    void ServerLobbySession::Log(const std::string& message) const
    {
        if (_ports.WriteLog != nullptr)
        {
            _ports.WriteLog(_ports.Context, message);
        }
    }

    void ServerLobbySession::TouchRevision(const std::string& reason)
    {
        const std::uint16_t previous = _ports.Lobby->AdvanceSessionRevision();
        Log("[lobby] revision " + std::to_string(previous) + " -> "
            + std::to_string(_ports.Lobby->SessionRevision()) + ": " + reason);
        _ports.State->BroadcastSessionState();
        _ports.State->BroadcastRoster();
    }

    void ServerLobbySession::SetPhase(SessionPhase phase)
    {
        Log("[lobby] phase " + ToString(_ports.Lobby->Phase()) + " -> " + ToString(phase)
            + ", match " + std::to_string(*_ports.MatchId));
        _ports.Lobby->SetPhase(phase);
        TouchRevision("phase changed");
    }

    void ServerLobbySession::ClaimOwner(const PeerHandle& peer,
        const ::MphRead::NativeRuntime::Guid& ownerToken)
    {
        if (!ServerLobbyOwnershipPolicy::TryClaim(*_ports.Lobby, peer, ownerToken))
        {
            return;
        }
        TouchRevision("owner = slot " + std::to_string(peer->SlotIndex));
    }

    LobbyResultCode ServerLobbySession::ExecuteCommand(const PeerHandle& peer,
        const LobbyCommandPacket& command, std::string& reason)
    {
        const RosterPacket roster = command.Type == LobbyCommandType::StartMatch
            ? _ports.State->BuildRoster() : RosterPacket{};
        ServerLobbyCommandExecution execution = ServerLobbyCommandProcessor::Execute(
            *_ports.Lobby, *_ports.Peers, peer, command, _ports.MaxPlayers, roster,
            &ServerMapVoting::ResolveRoomKey);
        reason = std::move(execution.Reason);
        if (execution.Result != LobbyResultCode::Ok)
        {
            return execution.Result;
        }

        if (execution.NextAction == ServerLobbyCommandExecution::Action::StartMatch)
        {
            const double buildStarted = _ports.Session->Clock.ReadClock();
            const std::uint16_t previousMatch = *_ports.MatchId;
            *_ports.MatchId = NetLifecycleTracker::Next(*_ports.MatchId);
            try
            {
                if (_ports.StartSimulation != nullptr)
                {
                    _ports.StartSimulation(_ports.Context);
                }
            }
            catch (const std::exception& ex)
            {
                *_ports.MatchId = previousMatch;
                _ports.Simulation->Stop();
                Log(std::string("[lobby] map load failed: ") + ex.what());
                reason = "The server could not load this map.";
                return LobbyResultCode::MapUnavailable;
            }

            _ports.Snapshots->ResetIncomingSequence();
            _ports.Slots->ResetLives();
            for (const PeerHandle& connected : _ports.Peers->All())
            {
                connected->Intent.Reset();
            }
            *_ports.MatchEndedAt = -1;
            _ports.Snapshots->ClearLatest();
            _ports.Lobby->ResetLoadBarrier();
            for (const PeerHandle& participant : _ports.Peers->All())
            {
                _ports.Lobby->ExpectLoadedSlot(participant->SlotIndex);
                participant->Lobby.ClearPostMatchReady();
            }
            _ports.Lobby->SetStartDeadline(*_ports.Now
                + (_ports.Session->Clock.ReadClock() - buildStarted) + 15);
            SetPhase(SessionPhase::Starting);
            if (_ports.SyncSimulationState != nullptr)
            {
                _ports.SyncSimulationState(_ports.Context, *_ports.Now);
            }
            char mask[3];
            std::snprintf(mask, sizeof(mask), "%02X", _ports.Lobby->ExpectedLoadedSlots());
            Log(std::string("[lobby] waiting for slots mask ") + mask);
            return LobbyResultCode::Ok;
        }

        if (execution.NextAction == ServerLobbyCommandExecution::Action::KickPeer
            && execution.TargetPeer != nullptr)
        {
            if (_ports.SendRefusal != nullptr)
            {
                _ports.SendRefusal(_ports.Context, execution.TargetPeer->EndPoint, RefusedPacket::ReasonKicked);
            }
            if (_ports.RemovePeer != nullptr)
            {
                _ports.RemovePeer(_ports.Context, execution.TargetPeer, "removed by lobby owner");
            }
        }

        TouchRevision("slot " + std::to_string(peer->SlotIndex) + ": " + ToString(command.Type));
        return LobbyResultCode::Ok;
    }

    void ServerLobbySession::HandleCommand(const ReceivedPacket& packet, double now)
    {
        const PeerHandle peer = _ports.Peers->Find(packet.Sender);
        LobbyCommandPacket command{};
        if (peer == nullptr || !LobbyCommandPacket::TryRead(packet.Payload(), command))
        {
            return;
        }
        peer->Liveness.MarkSeen(now);
        const std::optional<LobbyCommandResultPacket> previous = peer->LobbyCommands.Find(command.CommandId);
        LobbyCommandResultPacket result{};
        if (!previous.has_value())
        {
            std::string reason;
            const LobbyResultCode code = ExecuteCommand(peer, command, reason);
            result.CommandId = command.CommandId;
            result.ResultCode = code;
            result.CurrentRevision = _ports.Lobby->SessionRevision();
            result.Reason = reason;
            static_cast<void>(peer->LobbyCommands.Remember(result));
            if (code != LobbyResultCode::Ok)
            {
                Log("[lobby] slot " + std::to_string(peer->SlotIndex) + " "
                    + ToString(command.Type) + " denied: " + reason);
            }
        }
        else
        {
            result = *previous;
        }
        result.Write(*_ports.Scratch);
        if (_ports.Transport != nullptr && *_ports.Transport != nullptr)
        {
            (*_ports.Transport)->Send(peer->EndPoint, PacketType::LobbyCommandResult,
                std::span<const std::uint8_t>(*_ports.Scratch).first(LobbyCommandResultPacket::Size));
        }
        _ports.State->BroadcastSessionState();
        _ports.State->BroadcastRoster();
    }

    void ServerLobbySession::HandleMatchLoaded(const ReceivedPacket& packet, double now)
    {
        const PeerHandle peer = _ports.Peers->Find(packet.Sender);
        MatchLoadedPacket loaded{};
        if (peer == nullptr || !MatchLoadedPacket::TryRead(packet.Payload(), loaded)
            || loaded.MatchId != *_ports.MatchId)
        {
            return;
        }
        peer->Liveness.MarkSeen(now);
        if (!ServerLobbyLoadBarrier::AcceptLoaded(
                *_ports.Lobby, peer->SlotIndex, loaded.MatchId, *_ports.MatchId))
        {
            return;
        }
        TouchRevision("slot " + std::to_string(peer->SlotIndex)
            + " loaded match " + std::to_string(*_ports.MatchId));
        CheckLoadBarrier(now);
    }

    void ServerLobbySession::CheckLoadBarrier(double now)
    {
        const ServerLobbyLoadDecision decision = ServerLobbyLoadBarrier::Evaluate(*_ports.Lobby, now);
        if (decision.NextAction != ServerLobbyLoadDecision::Action::StartMatch)
        {
            return;
        }
        Log(decision.DeadlineElapsed
            ? "[lobby] load timeout; late clients may join in progress" : "[lobby] all clients loaded");
        *_ports.MatchStarted = now;
        SetPhase(SessionPhase::InMatch);
        _ports.State->BroadcastMatchState(now);
    }

    void ServerLobbySession::HandleMatchLoadFailed(const ReceivedPacket& packet)
    {
        const PeerHandle peer = _ports.Peers->Find(packet.Sender);
        MatchLoadFailedPacket failed{};
        if (peer == nullptr || !MatchLoadFailedPacket::TryRead(packet.Payload(), failed)
            || failed.MatchId != *_ports.MatchId)
        {
            return;
        }
        if (_ports.RemovePeer != nullptr)
        {
            _ports.RemovePeer(_ports.Context, peer, "could not load match: " + failed.Reason);
        }
    }

    void ServerLobbySession::ReturnToLobby()
    {
        _ports.Simulation->Stop();
        _ports.Snapshots->ClearLatest();
        MatchDefinition next = DefinitionFor(*(*_ports.Rotation)->Advance());
        const MatchDefinition& frozen = _ports.Lobby->FrozenMatch();
        next.Format = frozen.Format;
        next.CustomTeams = frozen.CustomTeams;
        next.FriendlyFire = frozen.FriendlyFire;
        next.AffinityWeapons = frozen.AffinityWeapons;
        next.ShadowFreeze = frozen.ShadowFreeze;
        next.Instagib = frozen.Instagib;
        std::string ignored;
        if (LobbyRules::ValidateDefinition(next, ignored) != LobbyResultCode::Ok)
        {
            next.Format = MatchFormat::Auto;
        }
        _ports.Lobby->SetLobbyMatch(std::move(next));
        *_ports.MatchEndedAt = -1;
        _ports.Lobby->ResetLoadBarrier();
        _ports.Voting->CloseBallot();
        _ports.Voting->BroadcastMapChoices();
        ServerLobbyCommandProcessor::InvalidateReady(*_ports.Peers);
        const SessionPhase previous = _ports.Lobby->Phase();
        _ports.Lobby->SetPhase(SessionPhase::Lobby);
        ServerLobbyCommandProcessor::NormalizeTeams(*_ports.Peers, CurrentDefinition());
        _ports.Lobby->SetPhase(previous);
        SetPhase(SessionPhase::Lobby);
    }

    void ServerLobbySession::PeerRemoved(const PeerHandle& peer)
    {
        _ports.Lobby->RemoveLoadedSlot(peer->SlotIndex);
        if (_ports.Lobby->LobbyOwnerClientId() == peer->ClientId)
        {
            const std::uint32_t successor = !_ports.Peers->All().empty()
                ? _ports.Peers->All()[0]->ClientId : 0;
            static_cast<void>(ServerLobbyOwnershipPolicy::ReassignAfterRemoval(
                *_ports.Lobby, peer->ClientId, successor));
        }
        TouchRevision("slot " + std::to_string(peer->SlotIndex) + " left; owner "
            + std::to_string(_ports.Lobby->LobbyOwnerClientId()));
        if (_ports.Lobby->Phase() == SessionPhase::Starting)
        {
            std::string why;
            if (LobbyRules::Validate(_ports.Lobby->FrozenMatch(), _ports.State->BuildRoster(), false, why)
                != LobbyResultCode::Ok)
            {
                Log("[lobby] start cancelled: " + why);
                _ports.Simulation->Stop();
                _ports.Snapshots->ClearLatest();
                ServerLobbyCommandProcessor::InvalidateReady(*_ports.Peers);
                SetPhase(SessionPhase::Lobby);
            }
            else
            {
                CheckLoadBarrier(*_ports.Now);
            }
        }
    }
}
