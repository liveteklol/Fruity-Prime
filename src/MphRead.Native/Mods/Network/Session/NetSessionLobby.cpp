#include "Mods/Network/Session/NetSession.hpp"
#include "Mods/Network/Session/SessionLobbySynchronizer.hpp"
#include "Mods/Network/Session/SessionMatchReset.hpp"

#include "Mods/Network/Replay/DemoPlayback.hpp"
#include "Mods/Network/Gameplay/Player/NetPlayerLifecycle.hpp"
#include "Mods/Network/Session/NetSlotManager.hpp"
#include "Mods/Network/Gameplay/Player/PlayerColors.hpp"

#include "GameState.hpp"
#include "Mods/Chat/NetChat.hpp"
#include "NativeRuntime/System/Managed.hpp"
#include "NativeRuntime/System/Stopwatch.hpp"

namespace MphRead::Mods::Network
{
    namespace Runtime = ::MphRead::NativeRuntime;

    SessionPhase NetSession::CurrentSessionPhase() noexcept
    {
        return _context._clientLobby.Phase();
    }

    std::uint16_t NetSession::SessionRevision() noexcept
    {
        return _context._clientLobby.SessionRevision();
    }

    std::optional<MatchDefinition> NetSession::ActiveMatchDefinition()
    {
        return _context._clientLobby.ActiveMatchDefinition();
    }

    bool NetSession::LocalIsLobbyOwner() noexcept
    {
        return _context._clientLobby.IsLocalOwner(_context._runtime.LocalSlot());
    }

    bool NetSession::IsInLobby() noexcept { return _context._clientLobby.IsInLobby(); }
    bool NetSession::IsStarting() noexcept { return _context._clientLobby.IsStarting(); }
    bool NetSession::IsPlaying() noexcept { return _context._clientLobby.IsPlaying(); }
    bool NetSession::IsPostMatch() noexcept { return _context._clientLobby.IsPostMatch(); }
    bool NetSession::CanEditLobby() noexcept { return _context._clientLobby.CanEditLobby(_context._runtime.LocalSlot()); }

    bool NetSession::PersistentLobby() noexcept
    {
        return _context._clientLobby.Persistent();
    }

    bool NetSession::FreezeGameplay() noexcept
    {
        return _context._clientLobby.FreezeGameplay();
    }

    bool NetSession::ShouldLoadMatch() noexcept
    {
        return _context._clientLobby.ShouldLoadMatch(_context._runtime.LocalSlot());
    }

    std::int32_t NetSession::ConnectionPort()
    {
        return _context._connection.IsOpen() ? _context._connection.LocalPort() : -1;
    }

    bool NetSession::SessionTimedOut()
    {
        return IsClient() && !DemoPlayback::IsActive() && _context._connection.HasHostEndpoint()
            && _context._connectionHealth.TimedOut(Clock(), NetConfig::TimeoutSeconds);
    }

    double NetSession::Clock()
    {
        return static_cast<double>(Runtime::StopwatchGetTimestamp()) / static_cast<double>(Runtime::StopwatchFrequency());
    }

    void NetSession::Pump(double time)
    {
        Update(time);
    }

    bool NetSession::SendLobbyCommand(LobbyCommandType type, std::uint8_t targetSlot,
        std::int8_t team, bool ready, std::optional<SessionStatePacket> configuration)
    {
        if (!Active() || !_context._clientLobby.HasSession() || _context._clientLobby.HasPendingCommand())
        {
            return false;
        }
        LobbyCommandPacket command{};
        command.CommandId = _context._clientLobby.NextCommandId();
        command.ExpectedRevision = SessionRevision();
        command.Type = type;
        command.TargetSlot = targetSlot;
        command.TeamIndex = team;
        command.Ready = ready;
        command.Configuration = configuration.value_or(*_context._clientLobby.Session());
        _context._clientLobby.QueueCommand(command, Clock());
        _context._clientLobby.SetMessage("Waiting for server...");
        SendLobbyPacket(command);
        return true;
    }

    void NetSession::SendLobbyPacket(const LobbyCommandPacket& command)
    {
        command.Write(_context._connection.Buffer());
        if (_context._connection.HasHostEndpoint() && _context._connection.IsOpen())
        {
            _context._connection.SendToHost(PacketType::LobbyCommand,
                std::span<const std::uint8_t>(_context._connection.Buffer()).first(LobbyCommandPacket::Size));
        }
    }

    void NetSession::PumpLobby(double now)
    {
        _context._clientLobby.RetryPendingCommands(now,
            [](const LobbyCommandPacket& command) { NetSession::SendLobbyPacket(command); });
        if (_context._clientLobby.LoadedMatchRetryDue(now, 0.25, IsStarting()))
        {
            MarkMatchLoaded();
        }
        // Identity updates are also eventually reliable, without a second identity protocol.
        if (_context._clientLobby.IdentityUpdateDue(now, 1.0))
        {
            SendIdentify();
        }
    }

    void NetSession::ApplySessionState(SessionStatePacket state)
    {
        const SessionLobbyTransition transition = SessionLobbySynchronizer::Apply(_context._clientLobby, _context._matchIdentity, state);
        if (!transition.Accepted)
        {
            return;
        }
        if (transition.ResetMatch)
        {
            ResetMatchState();
        }
        if (transition.MatchState.has_value())
        {
            ApplyMatchState(*transition.MatchState, false);
        }
    }

    void NetSession::ApplyLobbyResult(const LobbyCommandResultPacket& result)
    {
        if (!_context._clientLobby.AcknowledgeCommand(result.CommandId))
        {
            return;
        }
        _context._clientLobby.SetMessage(result.ResultCode == LobbyResultCode::Ok ? std::string() : result.Reason);
    }

    void NetSession::MarkMatchLoaded()
    {
        if (!PersistentLobby() || !_context._clientLobby.HasSession() || !_context._connection.HasHostEndpoint())
        {
            return;
        }
        const std::optional<std::uint16_t> matchId = _context._clientLobby.MarkMatchLoaded(Clock(), 0.25);
        if (!matchId.has_value())
        {
            return;
        }
        MatchLoadedPacket{*matchId}.Write(_context._connection.Buffer());
        if (_context._connection.IsOpen())
        {
            _context._connection.SendToHost(PacketType::MatchLoaded,
                std::span<const std::uint8_t>(_context._connection.Buffer()).first(MatchLoadedPacket::Size));
        }
    }

    void NetSession::ReportMatchLoadFailed(const std::string& reason)
    {
        if (!_context._clientLobby.HasSession() || !_context._connection.HasHostEndpoint())
        {
            return;
        }
        MatchLoadFailedPacket{_context._clientLobby.Session()->MatchId, reason}.Write(_context._connection.Buffer());
        if (_context._connection.IsOpen())
        {
            _context._connection.SendToHost(PacketType::MatchLoadFailed,
                std::span<const std::uint8_t>(_context._connection.Buffer()).first(MatchLoadFailedPacket::Size));
        }
    }

    // The socket, local slot, identity, authoritative roster and lobby state survive this reset.
    void NetSession::ResetMatchState()
    {
        SessionMatchReset::Apply(RemotePlayers(), _context._replicaStream);
    }

    void NetSession::ResetLobbySession()
    {
        _context._clientLobby.Reset();
        _context._replicaStream.ResetLobby();
        SessionSlots().ResetLobby();
        Mods::Chat::NetChat::Clear();
    }

    RosterPacket NetSession::LobbyRoster()
    {
        RosterPacket roster = RosterPacket::Create();
        roster.Revision = _context._replicaStream.RosterRevision();
        roster.SessionRevision = _context._clientLobby.RosterSessionRevision();
        roster.MatchId = CurrentMatchId();
        roster.AuthorityEpoch = AuthorityEpoch();
        auto& slots = Runtime::RequireReference(roster.Slots);
        auto& teams = Runtime::RequireReference(roster.Teams);
        auto& generations = Runtime::RequireReference(roster.Generations);
        auto& ready = Runtime::RequireReference(roster.LobbyReady);
        auto& names = Runtime::RequireReference(roster.Names);
        auto& hunters = Runtime::RequireReference(roster.Hunters);
        auto& colors = Runtime::RequireReference(roster.Colors);
        auto& pings = Runtime::RequireReference(roster.Pings);
        for (std::int32_t slot = 0; slot < static_cast<std::int32_t>(SlotCapacity); slot++)
        {
            const auto index = static_cast<std::size_t>(slot);
            if (!SessionSlots().IsOccupied(slot))
            {
                continue;
            }
            const std::int32_t at = roster.Count++;
            Runtime::ManagedAt(slots, at) = static_cast<std::uint8_t>(slot);
            Runtime::ManagedAt(teams, at) = SessionSlots().TeamAt(slot);
            Runtime::ManagedAt(generations, at) = NetPlayerLifecycle::Generation(slot);
            Runtime::ManagedAt(ready, at) = SessionSlots().LobbyReady(slot);
            Runtime::ManagedAt(names, at) = GameState::Nicknames()[slot];
            Runtime::ManagedAt(hunters, at) = static_cast<std::uint8_t>(SessionSlots().HunterAt(index));
            Runtime::ManagedAt(colors, at) = static_cast<std::uint8_t>(PlayerColors::ChoiceFor(slot));
            Runtime::ManagedAt(pings, at) = static_cast<std::uint16_t>(SessionSlots().PingAt(slot));
        }
        return roster;
    }
}
