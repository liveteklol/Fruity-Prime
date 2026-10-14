#include "Mods/Network/Server/Core/ServerStateBroadcastChannel.hpp"

#include "Mods/Network/Server/Match/ServerMatchStateBuilder.hpp"
#include "Mods/Network/Server/Match/ServerRosterBuilder.hpp"
#include "Mods/Network/Server/Match/ServerSnapshotChannel.hpp"
#include "Mods/Network/Server/Lobby/ServerSessionStateBuilder.hpp"
#include "Mods/Network/Transport/NetTransport.hpp"
#include "NativeRuntime/System/BinaryPrimitives.hpp"

#include <span>

namespace MphRead::Mods::Network
{
    namespace Runtime = ::MphRead::NativeRuntime;

    ServerStateBroadcastChannel::ServerStateBroadcastChannel(Ports ports) noexcept
        : _ports(ports)
    {
    }

    MatchDefinition ServerStateBroadcastChannel::CurrentDefinition() const
    {
        return _ports.ReadDefinition != nullptr
            ? _ports.ReadDefinition(_ports.DefinitionContext) : MatchDefinition{};
    }

    SessionStatePacket ServerStateBroadcastChannel::BuildSessionState() const
    {
        return ServerSessionStateBuilder::Build(*_ports.Lobby, *_ports.Peers,
            CurrentDefinition(), *_ports.MatchId, _ports.Authority->Epoch(), _ports.MaxPlayers);
    }

    void ServerStateBroadcastChannel::BroadcastSessionState()
    {
        const SessionStatePacket state = BuildSessionState();
        if (_ports.Simulation->HasSimulation())
        {
            _ports.Session->ApplySessionState(state);
        }
        state.Write(*_ports.Scratch);
        for (const ServerPeerRegistry::PeerHandle& peer : _ports.Peers->All())
        {
            if (_ports.Transport != nullptr && *_ports.Transport != nullptr)
            {
                (*_ports.Transport)->Send(peer->EndPoint, PacketType::SessionState,
                    std::span<const std::uint8_t>(*_ports.Scratch).first(SessionStatePacket::Size));
            }
        }
    }

    MatchStatePacket ServerStateBroadcastChannel::BuildMatchState(double now) const
    {
        return ServerMatchStateBuilder::Build(CurrentDefinition(), _ports.Lobby->Phase(), now,
            *_ports.MatchStarted, *_ports.MatchEndedAt >= 0, _ports.Peers->All().size(),
            *_ports.MatchId, _ports.Authority->Epoch(), _ports.DamageLevel,
            (*_ports.Rotation)->Next()->RoomKey);
    }

    void ServerStateBroadcastChannel::BroadcastMatchState(double now)
    {
        if (_ports.Simulation->HasSimulation())
        {
            _ports.Session->ApplySessionState(BuildSessionState());
        }
        const MatchStatePacket state = BuildMatchState(now);
        if (_ports.Simulation->HasSimulation())
        {
            _ports.Session->ApplyMatchState(state, false);
        }
        if (_ports.Peers->All().empty())
        {
            return;
        }
        state.Write(*_ports.Scratch);
        for (const ServerPeerRegistry::PeerHandle& peer : _ports.Peers->All())
        {
            if (_ports.Transport != nullptr && *_ports.Transport != nullptr)
            {
                (*_ports.Transport)->Send(peer->EndPoint, PacketType::MatchState,
                    std::span<const std::uint8_t>(*_ports.Scratch).first(MatchStatePacket::Size));
            }
        }
    }

    RosterPacket ServerStateBroadcastChannel::BuildRoster()
    {
        return _rosterBuilder.Build(*_ports.MatchId, _ports.Authority->Epoch(),
            _ports.Lobby->SessionRevision(), _ports.MaxPlayers, *_ports.Peers, *_ports.Slots);
    }

    void ServerStateBroadcastChannel::BroadcastRoster()
    {
        RosterPacket roster = BuildRoster();
        if (_ports.Simulation->HasSimulation())
        {
            _ports.Session->ApplyMatchState(BuildMatchState(*_ports.Now), false);
            _ports.Session->ApplyRoster(roster);
        }
        if (_ports.Peers->All().empty())
        {
            return;
        }
        roster.Write(*_ports.Scratch);
        for (const ServerPeerRegistry::PeerHandle& peer : _ports.Peers->All())
        {
            if (_ports.Transport != nullptr && *_ports.Transport != nullptr)
            {
                (*_ports.Transport)->Send(peer->EndPoint, PacketType::Roster,
                    std::span<const std::uint8_t>(*_ports.Scratch).first(RosterPacket::Size));
            }
        }
    }

}
