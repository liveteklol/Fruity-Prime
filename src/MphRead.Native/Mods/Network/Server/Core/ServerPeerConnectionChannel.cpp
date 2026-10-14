#include "Mods/Network/Server/Core/ServerPeerConnectionChannel.hpp"

#include "Mods/Network/Server/Core/ServerChatChannel.hpp"
#include "Mods/Network/Server/Core/ServerHandshake.hpp"
#include "Mods/Network/Server/Core/ServerPeerIdentityChannel.hpp"
#include "Mods/Network/Server/Core/ServerPeerLiveness.hpp"
#include "Mods/Network/Server/Core/ServerStateBroadcastChannel.hpp"
#include "Mods/Network/Server/Lobby/LobbyRules.hpp"
#include "Mods/Network/Server/Lobby/ServerLobbyCommandProcessor.hpp"
#include "Mods/Network/Server/Lobby/ServerLobbySession.hpp"
#include "Mods/Network/Server/Lobby/ServerMapVoting.hpp"
#include "Mods/Network/Server/Match/ServerBotController.hpp"
#include "Mods/Network/Server/Match/ServerSnapshotChannel.hpp"
#include "Mods/Network/Server/Match/ServerSimulationController.hpp"
#include "Mods/Network/Transport/NetTransport.hpp"
#include "Mods/Network/Session/NetLifecycleTracker.hpp"
#include "NativeRuntime/System/BinaryPrimitives.hpp"
#include "GameState.hpp"

#include <algorithm>
#include <optional>
#include <span>

namespace MphRead::Mods::Network
{
    namespace Runtime = ::MphRead::NativeRuntime;
    using Peer = ServerPeerRegistry::Peer;

    namespace
    {
        [[nodiscard]] std::span<const std::uint8_t> First(
            const ServerPeerConnectionChannel::ScratchBuffer& scratch, std::size_t count)
        {
            return std::span<const std::uint8_t>(scratch.data(), count);
        }
    }

    ServerPeerConnectionChannel::ServerPeerConnectionChannel(Ports ports) noexcept
        : _ports(ports)
    {
    }

    MatchDefinition ServerPeerConnectionChannel::CurrentDefinition() const
    {
        return _ports.CurrentDefinition != nullptr
            ? _ports.CurrentDefinition(_ports.Context) : MatchDefinition{};
    }

    void ServerPeerConnectionChannel::Log(const std::string& message) const
    {
        if (_ports.WriteLog != nullptr)
        {
            _ports.WriteLog(_ports.Context, message);
        }
    }

    void ServerPeerConnectionChannel::SendRefusal(
        const std::shared_ptr<System::Net::IPEndPoint>& endpoint, std::uint8_t reason) const
    {
        RefusedPacket refusal{};
        refusal.Reason = reason;
        refusal.Players = static_cast<std::uint8_t>(_ports.Peers->All().size());
        refusal.MaxPlayers = static_cast<std::uint8_t>(_ports.MaxPlayers);
        refusal.Write(*_ports.Scratch);
        if (_ports.Transport != nullptr && *_ports.Transport != nullptr)
        {
            (*_ports.Transport)->Send(endpoint, PacketType::Refused, First(*_ports.Scratch, RefusedPacket::Size));
        }
    }

    void ServerPeerConnectionChannel::Announce(const std::string& message) const
    {
        NetTransport* transport = _ports.Transport != nullptr ? _ports.Transport->get() : nullptr;
        ServerChatChannel::Announce(*_ports.Peers, transport, *_ports.Scratch, message);
    }

    void ServerPeerConnectionChannel::HandleHello(const ReceivedPacket& packet, double now)
    {
        const std::span<const std::uint8_t> payload = packet.Payload();
        if (payload.empty() || payload[0] != NetConfig::ProtocolVersion)
        {
            Log("rejected " + packet.Sender->ToString() + ": protocol mismatch");
            SendRefusal(packet.Sender, RefusedPacket::ReasonProtocol);
            return;
        }
        const HelloPacket hello = HelloPacket::Read(payload);
        const std::uint32_t clientId = hello.ClientId;
        ServerHandshake::Resolution resolution = ServerHandshake::Resolve(
            *_ports.Peers, packet.Sender, clientId, hello.Secret);
        if (resolution.Kind == ServerHandshake::ResolutionKind::EndpointConflict)
        {
            Remove(resolution.Peer, "replaced connection");
            resolution = ServerHandshake::Resolve(*_ports.Peers, packet.Sender, clientId, hello.Secret);
            if (resolution.Kind == ServerHandshake::ResolutionKind::EndpointConflict)
            {
                SendRefusal(packet.Sender, RefusedPacket::ReasonIdentity);
                return;
            }
        }
        if (resolution.Kind == ServerHandshake::ResolutionKind::InvalidSecret)
        {
            const PeerHandle& peer = resolution.Peer;
            Log("refused to move slot " + std::to_string(peer->SlotIndex) + " ("
                + peer->Profile.Name() + ") to " + packet.Sender->ToString()
                + ": the hello does not carry its connection secret");
            SendRefusal(packet.Sender, RefusedPacket::ReasonIdentity);
            return;
        }
        if (resolution.Kind == ServerHandshake::ResolutionKind::Rebound)
        {
            const PeerHandle& peer = resolution.Peer;
            Log("slot " + std::to_string(peer->SlotIndex) + " (" + peer->Profile.Name() + ") came back on "
                + packet.Sender->ToString() + ", was " + resolution.PreviousEndpoint->ToString());
        }
        PeerHandle peer = std::move(resolution.Peer);
        if (peer == nullptr)
        {
            const std::int32_t preferredSlot = hello.Slot == HelloPacket::AnySlot
                ? -1 : static_cast<std::int32_t>(hello.Slot);
            const std::int32_t slot = _ports.Peers->SelectFreeSlot(preferredSlot, _ports.MaxPlayers,
                +[](std::int32_t candidate) { return ServerBotController::Holds(candidate); });
            if (slot < 0)
            {
                Log("rejected " + packet.Sender->ToString() + ": session full");
                SendRefusal(packet.Sender, RefusedPacket::ReasonFull);
                return;
            }
            if (_ports.Peers->All().empty() && _ports.Lobby->Policy() == ServerSessionPolicy::Continuous)
            {
                *_ports.MatchStarted = now;
                *_ports.MatchEndedAt = -1;
                _ports.Lobby->SetPhase(SessionPhase::InMatch);
                _ports.Voting->CloseBallot();
                *_ports.MatchId = NetLifecycleTracker::Next(*_ports.MatchId);
                _ports.Snapshots->ResetIncomingSequence();
                _ports.Snapshots->ClearLatest();
                _ports.Slots->ResetLives();
            }
            const MatchDefinition definition = CurrentDefinition();
            if (_ports.Lobby->Phase() == SessionPhase::InMatch
                && !_ports.Lobby->AllowJoinInProgress())
            {
                SendRefusal(packet.Sender, RefusedPacket::ReasonInMatch);
                return;
            }
            const std::int8_t team = ServerLobbyCommandProcessor::ChooseTeam(*_ports.Peers, definition);
            if (LobbyRules::TeamCount(definition) > 0 && team < 0)
            {
                SendRefusal(packet.Sender, RefusedPacket::ReasonFull);
                return;
            }
            peer = ServerHandshake::Register(*_ports.Peers, *_ports.Slots, packet.Sender,
                slot, _ports.MaxPlayers,
                +[](std::int32_t candidate) { return ServerBotController::Holds(candidate); },
                clientId, hello.Secret, team);
            if (peer == nullptr)
            {
                Log("rejected " + packet.Sender->ToString() + ": slot admission became unavailable");
                SendRefusal(packet.Sender, RefusedPacket::ReasonFull);
                return;
            }
            _ports.EverOccupied->store(true);
            Log(packet.Sender->ToString() + " joined as slot " + std::to_string(slot));
            if (*_ports.RunsTheMatch && _ports.Simulation->HasSimulation()
                && _ports.Snapshots->HasLatest()
                && _ports.Transport != nullptr && *_ports.Transport != nullptr)
            {
                _ports.SnapshotChannel->SendLatest(peer, _ports.Transport->get());
            }
        }
        peer->ClientId = clientId;
        _ports.LobbySession->ClaimOwner(peer, hello.OwnerToken);
        peer->Liveness.MarkSeen(now);
        (*_ports.Scratch)[0] = static_cast<std::uint8_t>(peer->SlotIndex);
        Runtime::WriteUInt32LittleEndian(std::span(*_ports.Scratch).subspan(1), clientId);
        Runtime::WriteUInt16LittleEndian(std::span(*_ports.Scratch).subspan(5), *_ports.MatchId);
        Runtime::WriteUInt64LittleEndian(std::span(*_ports.Scratch).subspan(7), _ports.Authority->Epoch());
        Runtime::WriteUInt16LittleEndian(std::span(*_ports.Scratch).subspan(15),
            _ports.Slots->Generation(static_cast<std::size_t>(peer->SlotIndex)));
        if (_ports.Transport != nullptr && *_ports.Transport != nullptr)
        {
            (*_ports.Transport)->Send(peer->EndPoint, PacketType::Welcome, First(*_ports.Scratch, 17));
        }
        MatchStatePacket state = _ports.State->BuildMatchState(now);
        state.Write(*_ports.Scratch);
        if (_ports.Transport != nullptr && *_ports.Transport != nullptr)
        {
            (*_ports.Transport)->Send(peer->EndPoint, PacketType::MatchState,
                First(*_ports.Scratch, MatchStatePacket::Size));
        }
        _ports.LobbySession->TouchRevision("peer slot " + std::to_string(peer->SlotIndex) + " connected");
    }

    void ServerPeerConnectionChannel::HandleIdentify(const ReceivedPacket& packet, double now)
    {
        const std::optional<ServerPeerIdentityChannel::Change> change
            = ServerPeerIdentityChannel::Receive(packet, now, *_ports.Peers, *_ports.Lobby);
        if (!change.has_value())
        {
            return;
        }
        const PeerHandle& peer = change->Peer;
        const std::string& name = peer->Profile.Name();
        const std::uint8_t hunter = peer->Profile.Hunter();
        const std::uint8_t color = peer->Profile.Color();
        Log("slot " + std::to_string(peer->SlotIndex) + " is \"" + name + "\" playing "
            + ::MphRead::ToString(static_cast<Hunter>(hunter)) + " in suit " + std::to_string(color + 1));
        if (change->FirstName)
        {
            Announce(name + " joined");
        }
        _ports.LobbySession->TouchRevision("slot " + std::to_string(peer->SlotIndex) + " identity changed");
    }

    void ServerPeerConnectionChannel::HandlePong(const ReceivedPacket& packet, double now)
    {
        const PeerHandle peer = _ports.Peers->Find(packet.Sender);
        if (peer == nullptr)
        {
            return;
        }
        const std::span<const std::uint8_t> payload = packet.Payload();
        if (!payload.empty())
        {
            static_cast<void>(peer->Liveness.AcceptPong(payload[0], now));
        }
    }

    void ServerPeerConnectionChannel::HandlePing(const ReceivedPacket& packet)
    {
        if (_ports.Transport != nullptr && *_ports.Transport != nullptr)
        {
            (*_ports.Transport)->Send(packet.Sender, PacketType::Pong, {});
        }
    }

    void ServerPeerConnectionChannel::PingPeers(double now)
    {
        for (const PeerHandle& peer : _ports.Peers->All())
        {
            if (!peer->Liveness.CanSendPing(now))
            {
                continue;
            }
            (*_ports.Scratch)[0] = peer->Liveness.BeginPing(now);
            if (_ports.Transport != nullptr && *_ports.Transport != nullptr)
            {
                (*_ports.Transport)->Send(peer->EndPoint, PacketType::Ping, First(*_ports.Scratch, 1));
            }
        }
    }

    void ServerPeerConnectionChannel::HandleBye(const ReceivedPacket& packet)
    {
        const PeerHandle peer = _ports.Peers->Find(packet.Sender);
        if (peer != nullptr)
        {
            Remove(peer, "left");
        }
    }

    void ServerPeerConnectionChannel::DropTimedOut(double now)
    {
        for (std::int32_t i = static_cast<std::int32_t>(_ports.Peers->All().size()) - 1; i >= 0; i--)
        {
            if (i >= static_cast<std::int32_t>(_ports.Peers->All().size()))
            {
                continue;
            }
            const PeerHandle peer = _ports.Peers->All()[static_cast<std::size_t>(i)];
            if (peer->Liveness.TimedOut(now, NetConfig::TimeoutSeconds))
            {
                Remove(peer, "timed out");
            }
        }
    }

    void ServerPeerConnectionChannel::Remove(const PeerHandle& peer, const std::string& reason)
    {
        const PeerHandle removed = peer;
        _ports.Voting->ForgetPeer(removed->ClientId);
        static_cast<void>(_ports.Peers->Remove(removed));
        _ports.LobbySession->PeerRemoved(removed);
        _ports.State->BroadcastRoster();
        _ports.Voting->ReviewVote(*_ports.Now);
        _ports.Voting->ReviewPicks();
        Log(removed->EndPoint->ToString() + " " + reason + " (slot " + std::to_string(removed->SlotIndex) + ")");
        if (!removed->Profile.Name().empty())
        {
            Announce(removed->Profile.Name() + " " + reason);
        }
    }
}
