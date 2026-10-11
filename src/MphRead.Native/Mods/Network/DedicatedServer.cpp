#include "DedicatedServer.hpp"

#include "../../GameState.hpp"
#include "../../Metadata/Metadata.hpp"
#include "../../Program.hpp"
#include "../../NativeRuntime/System/BinaryPrimitives.hpp"
#include "../../NativeRuntime/System/Console.hpp"
#include "../../NativeRuntime/System/DateTime.hpp"
#include "../../NativeRuntime/System/Encoding.hpp"
#include "../../NativeRuntime/System/Globalization.hpp"
#include "../../NativeRuntime/System/Number.hpp"
#include "../../NativeRuntime/System/Runtime.hpp"
#include "../Launcher/Portable/LaunchPlan.hpp"
#include "../Update/ServerUpdate.hpp"
#include "LobbyRules.hpp"
#include "MapRotation.hpp"
#include "NetHealthSync.hpp"
#include "NetHitClaims.hpp"
#include "NetShotEvents.hpp"
#include "NetLifecycleTracker.hpp"
#include "NetMaster.hpp"
#include "NetMatchTimeSync.hpp"
#include "NetPlayerLifecycle.hpp"
#include "NetSession.hpp"
#include "NetTransport.hpp"
#include "ServerSim.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace MphRead::Mods::Network
{
    namespace Runtime = ::MphRead::NativeRuntime;

    namespace
    {
        [[nodiscard]] std::span<const std::uint8_t> First(
            const std::array<std::uint8_t, NetConfig::MaxPacketSize>& scratch, std::size_t count)
        {
            return std::span<const std::uint8_t>(scratch.data(), count);
        }

        [[nodiscard]] std::span<std::uint8_t> From(
            std::array<std::uint8_t, NetConfig::MaxPacketSize>& scratch, std::size_t start)
        {
            return std::span<std::uint8_t>(scratch).subspan(start);
        }
    }

    double DedicatedServer::EndSequenceSeconds()
    {
        return 3.0 + GameState::MatchEndingSeconds + 1.0;
    }

    DedicatedServer::DedicatedServer(std::int32_t port, std::int32_t maxPlayers,
        std::shared_ptr<MapRotation> rotation)
        : _voteMode(GameMode::Battle),
          _port(port),
          _maxPlayers(std::clamp(maxPlayers, 2, Entities::PlayerEntity::SlotCapacity)),
          _rotation(rotation != nullptr ? std::move(rotation) : std::make_shared<MapRotation>()),
          _boundPort(port),
          _authorityEpoch(static_cast<std::uint64_t>(Runtime::DateTimeUtcNowTicks())),
          _serverName(Runtime::EnvironmentMachineName())
    {
        _hosts.Log = [](const std::string& message) { Log(message); };
        _hosts.ReporterFactory = [this]() -> std::shared_ptr<MasterReporter>
        {
            return _reporter == nullptr
                ? nullptr
                : std::make_shared<MasterReporter>(_reporter->Host(), _reporter->Port());
        };
    }

    DedicatedServer::~DedicatedServer() = default;

    bool DedicatedServer::Simulating() const
    {
        return _sim != nullptr && _sim->Running();
    }

    double DedicatedServer::EndSequenceFor() const
    {
        bool all = true;
        std::int32_t counted = 0;
        for (const std::shared_ptr<Peer>& peer : _peers)
        {
            counted++;
            if (!peer->PostMatchReady)
            {
                all = false;
                break;
            }
        }
        const double wait = counted == 0 || all ? AllReadySeconds : ReadyWaitSeconds;
        return std::max(EndSequenceSeconds(), wait);
    }

    void DedicatedServer::Run(std::stop_token cancel)
    {
        _transport = std::make_unique<NetTransport>(_port);
        _boundPort.store(_transport->LocalPort());
        _listening.store(true);
        _running.store(true);
        Log("listening on UDP " + std::to_string(_transport->LocalPort()) + ", up to "
            + std::to_string(_maxPlayers) + " players");
        _lobbyMatch = DefinitionFor(*_rotation->Current());
        _phase = _sessionPolicy == ServerSessionPolicy::Lobby ? SessionPhase::Lobby : SessionPhase::InMatch;
        const std::uint16_t listenPort = static_cast<std::uint16_t>(_transport->LocalPort());
        try
        {
            if (_phase == SessionPhase::InMatch)
            {
                StartSimulation();
            }
            Log(Simulating()
                ? "this server runs the match itself"
                : "hosted game: the first client to connect runs the match");
            Log("rotation: " + std::to_string(_rotation->Entries().size()) + " map(s), starting on "
                + _rotation->Current()->ToString());
            Log(_hosts.Describe());
            const auto clock = std::chrono::steady_clock::now();
            double lastReport = 0;
            double lastStateBroadcast = 0;
            _matchStarted = 0;
            while (_running.load() && !cancel.stop_requested())
            {
                const double now = std::chrono::duration<double>(std::chrono::steady_clock::now() - clock).count();
                _now = now;
                for (ReceivedPacket packet : _transport->Drain())
                {
                    Handle(packet, now);
                }
                DropTimedOut(now);
                CheckLoadBarrier(now);
                if ((_phase == SessionPhase::InMatch || _phase == SessionPhase::PostMatch) && _sim != nullptr)
                {
                    _sim->Advance(now);
                }
                const float limit = CurrentDefinition().TimeLimitSeconds;
                if (_phase == SessionPhase::InMatch && _matchEndedAt < 0 && limit > 0 && !_peers.empty()
                    && now - _matchStarted >= limit)
                {
                    EndMatch(now, "time limit");
                }
                else if (_matchEndedAt >= 0 && now - _matchEndedAt >= EndSequenceFor())
                {
                    if (_sessionPolicy == ServerSessionPolicy::Lobby)
                    {
                        ReturnToLobby();
                    }
                    else
                    {
                        AdvanceMap(now);
                    }
                }
                if (now - lastStateBroadcast >= 1.0)
                {
                    lastStateBroadcast = now;
                    PingPeers(now);
                    BroadcastSessionState();
                    if (_phase == SessionPhase::InMatch || _phase == SessionPhase::PostMatch)
                    {
                        BroadcastMatchState(now);
                    }
                    BroadcastRoster();
                    Tally(now);
                    BroadcastVoteState(now);
                    if (_ballotOpen)
                    {
                        BroadcastMapChoices();
                    }
                    if (_authority != nullptr && !_runsTheMatch)
                    {
                        NotifyAuthority(_authority);
                    }
                    if (_reporter != nullptr)
                    {
                        const MatchDefinition definition = CurrentDefinition();
                        _reporter->Beat(now, _serverName, listenPort, static_cast<std::uint8_t>(_peers.size()),
                            static_cast<std::uint8_t>(_maxPlayers), static_cast<std::uint8_t>(definition.Mode),
                            definition.RoomKey.value_or(""));
                    }
                }
                _hosts.Reap(now);
                if (_autoUpdate && Update::ServerUpdate::ShouldRestart(
                    static_cast<std::int32_t>(_peers.size()) + _hosts.Count()))
                {
                    Log("shutting down to come back on the new build");
                    _running.store(false);
                    break;
                }
                if (now - lastReport >= 30)
                {
                    lastReport = now;
                    std::string line = std::to_string(_peers.size()) + " peer(s) connected"
                        + (Simulating() ? std::string(", authority = this server")
                            : _authority != nullptr ? ", authority = slot " + std::to_string(_authority->SlotIndex)
                            : std::string(", no authority"))
                        + ", map " + CurrentDefinition().RoomKey.value_or("");
                    if (limit > 0)
                    {
                        line += ", " + Runtime::ToString(std::max(0.0, static_cast<double>(limit) - (now - _matchStarted)), "0")
                            + " s left";
                    }
                    if (_transport != nullptr && _transport->PacketsDropped() > 0)
                    {
                        line += ", " + std::to_string(_transport->PacketsDropped()) + " packet(s) dropped";
                    }
                    Log(line);
                    if (_sim != nullptr)
                    {
                        Log("sim: " + _sim->Describe());
                        Log("sim: " + _sim->DescribeUnlagged());
                        Log("sim: " + _sim->DescribeRewindDepths());
                        if (const std::optional<std::string> claimLine = _sim->DescribeClaims(); claimLine.has_value())
                        {
                            Log("sim: " + *claimLine);
                        }
                        Log("sim: " + _sim->DescribeShots());
                        if (const std::optional<std::string> events = NetShotEvents::Describe(); events.has_value())
                        {
                            Log("sim: " + *events);
                        }
                        for (const std::string& agreement : Runtime::StringSplit(_sim->DescribeAgreement(), '\n'))
                        {
                            Log("sim: " + agreement);
                        }
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(_peers.empty() && !Simulating() ? 20 : 1));
            }
        }
        catch (...)
        {
            Shutdown(listenPort);
            throw;
        }
        Shutdown(listenPort);
    }

    void DedicatedServer::Shutdown(std::uint16_t listenPort)
    {
        Log("shutting down");
        _hosts.StopAll("the server is shutting down");
        _running.store(false);
        try
        {
            if (_reporter != nullptr)
            {
                _reporter->Farewell(listenPort);
            }
        }
        catch (...)
        {
        }
        if (_reporter != nullptr)
        {
            _reporter->Dispose();
        }
        _reporter.reset();
        if (_transport != nullptr)
        {
            _transport->Dispose();
        }
        _transport.reset();
        _boundPort.store(_port);
        _listening.store(false);
        if (_sim != nullptr)
        {
            _sim->Stop();
        }
        _sim.reset();
        NetHitClaims::VerdictSink(nullptr);
    }

    void DedicatedServer::EndMatch(double now, const std::string& reason)
    {
        if (_matchEndedAt >= 0 || _phase != SessionPhase::InMatch)
        {
            return;
        }
        _matchEndedAt = now;
        for (const std::shared_ptr<Peer>& peer : _peers)
        {
            peer->PostMatchReady = false;
        }
        SetPhase(SessionPhase::PostMatch);
        OpenBallot();
        Log("match over on " + CurrentDefinition().RoomKey.value_or("") + " (" + reason + "); "
            + _rotation->Next()->RoomKey + " in " + Runtime::ToString(EndSequenceFor(), "0") + " s"
            + (_ballotOpen ? "; ballot open" : ""));
        BroadcastMatchState(now);
        BroadcastMapChoices();
    }

    void DedicatedServer::AdvanceMap(double now)
    {
        const std::shared_ptr<const RotationEntry> entry = _rotation->Advance();
        _phase = SessionPhase::InMatch;
        FreezeMapWorldProfile();
        NormalizeTeams();
        _matchStarted = now;
        _matchEndedAt = -1;
        _matchId = NetLifecycleTracker::Next(_matchId);
        _snapshotSeen = false;
        _slotLives.fill(0);
        for (const std::shared_ptr<Peer>& connected : _peers)
        {
            connected->LastIntentFrame = 0;
        }
        TouchLobbyRevision("rotation advanced");
        if (_voteRunning)
        {
            _voteRunning = false;
            _voteResolvedAt = now;
            _voteResult = VoteStatePacket::StateFailed;
            for (const std::shared_ptr<Peer>& peer : _peers)
            {
                peer->Ballot = 0;
            }
        }
        for (const std::shared_ptr<Peer>& peer : _peers)
        {
            peer->PostMatchReady = false;
        }
        CloseBallot();
        BroadcastMapChoices();
        Log("rotating to " + entry->ToString() + ", resources "
            + Multiplayer::ToString(_mapWorldProfile.Resources) + " for " + std::to_string(_peers.size()) + " player(s)");
        // The new world before the map change, here and to every client, so
        // the room is loaded with it on both ends.
        if (_sim != nullptr)
        {
            NetSession::ApplySessionState(BuildSessionState());
        }
        BroadcastSessionState();
        MatchStatePacket state = BuildState(now);
        if (_sim != nullptr)
        {
            NetSession::ApplyMatchState(state, true);
        }
        state.Write(_scratch);
        for (const std::shared_ptr<Peer>& peer : _peers)
        {
            if (_transport != nullptr)
            {
                _transport->Send(peer->EndPoint, PacketType::MapChange, First(_scratch, MatchStatePacket::Size));
            }
        }
    }

    MatchStatePacket DedicatedServer::BuildState(double now) const
    {
        const MatchDefinition entry = CurrentDefinition();
        const float elapsed = _phase == SessionPhase::Lobby || _phase == SessionPhase::Starting
            ? 0.0F : static_cast<float>(now - _matchStarted);
        const bool ending = _matchEndedAt >= 0;
        MatchStatePacket state{};
        state.Mode = static_cast<std::uint8_t>(entry.Mode);
        state.TimeRemaining = ending || entry.TimeLimitSeconds <= 0
            ? 0.0F
            : std::max(0.0F, static_cast<float>(entry.TimeLimitSeconds) - elapsed);
        state.TimeElapsed = elapsed;
        state.PlayerCount = static_cast<std::uint8_t>(_peers.size());
        state.Flags = static_cast<std::uint8_t>((ending ? MatchStatePacket::FlagEnding : MatchStatePacket::FlagInProgress)
            | (entry.FriendlyFire ? MatchStatePacket::FlagFriendlyFire : 0)
            | (entry.ShadowFreeze ? 0 : MatchStatePacket::FlagNoShadowFreeze)
            | MatchStatePacket::RuleFlags(DamageLevel(), entry.AffinityWeapons));
        state.PointGoal = entry.PointGoal;
        state.MatchId = _matchId;
        state.AuthorityEpoch = _authorityEpoch;
        state.RoomKey = entry.RoomKey;
        state.NextRoomKey = _rotation->Next()->RoomKey;
        return state;
    }

    void DedicatedServer::BroadcastMatchState(double now)
    {
        if (_sim != nullptr)
        {
            NetSession::ApplySessionState(BuildSessionState());
        }
        MatchStatePacket state = BuildState(now);
        if (_sim != nullptr)
        {
            NetSession::ApplyMatchState(state, false);
        }
        if (_peers.empty())
        {
            return;
        }
        state.Write(_scratch);
        for (const std::shared_ptr<Peer>& peer : _peers)
        {
            if (_transport != nullptr)
            {
                _transport->Send(peer->EndPoint, PacketType::MatchState, First(_scratch, MatchStatePacket::Size));
            }
        }
    }

    void DedicatedServer::FreezeMapWorldProfile()
    {
        // The pickups follow the people actually playing, not the server's
        // capacity: an 8-slot server with three in it is a three-player map.
        _mapWorldProfile = LobbyRules::ResolveWorldProfile(CurrentDefinition(),
            std::max<std::int32_t>(2, static_cast<std::int32_t>(_peers.size())));
    }

    void DedicatedServer::StartSimulation()
    {
        if (!_runsTheMatch)
        {
            return;
        }
        std::string why;
        if (!ServerSim::Available(why))
        {
            Log("cannot run the match: " + why);
            Log("a dedicated server runs the match itself now, so this one will not "
                "start. Put the game files on this machine and paths.txt beside "
                "the binary -- see SERVER.md");
            throw ProgramException("the server cannot run the match: " + why);
        }
        auto sim = std::make_shared<ServerSim>();
        const MatchDefinition entry = CurrentDefinition();
        const std::string room = entry.RoomKey.value_or("");
        if (!sim->Start(room, entry.Mode, _maxPlayers,
                [this](std::span<const std::uint8_t> payload) { SendSnapshot(payload); },
                [this]() { EndMatch(_now, "score"); }, BuildRoster(), BuildSessionState()))
        {
            Log("cannot run the match: the room \"" + room + "\" would not load");
            throw ProgramException("the server could not load \"" + room + "\"");
        }
        _sim = sim;
        NetHitClaims::VerdictSink([this](std::int32_t slot, std::span<const std::pair<std::uint16_t, std::uint8_t>> verdicts)
        {
            SendVerdicts(slot, verdicts);
        });
        SyncSimulationState(_now);
    }

    void DedicatedServer::SendSnapshot(std::span<const std::uint8_t> payload)
    {
        _lastSnapshot = std::make_shared<std::vector<std::uint8_t>>(payload.begin(), payload.end());
        for (const std::shared_ptr<Peer>& peer : _peers)
        {
            if (_transport != nullptr)
            {
                _transport->Send(peer->EndPoint, PacketType::Snapshot, payload);
            }
        }
    }

    void DedicatedServer::SyncSimulationState(double now)
    {
        if (_sim == nullptr)
        {
            return;
        }
        NetSession::ApplySessionState(BuildSessionState());
        NetSession::ApplyMatchState(BuildState(now), false);
        NetSession::ApplyRoster(BuildRoster());
    }

    void DedicatedServer::Stop() noexcept
    {
        _running.store(false);
    }

    void DedicatedServer::Handle(const ReceivedPacket& packet, double now)
    {
        switch (packet.Type())
        {
        case PacketType::LobbyCommand:
            HandleLobbyCommand(packet, now);
            break;
        case PacketType::MatchLoaded:
            HandleMatchLoaded(packet, now);
            break;
        case PacketType::MatchLoadFailed:
            HandleMatchLoadFailed(packet);
            break;
        case PacketType::Hello:
            HandleHello(packet, now);
            break;
        case PacketType::Intent:
            HandleIntent(packet, now);
            break;
        case PacketType::Snapshot:
            HandleSnapshot(packet, now);
            break;
        case PacketType::Bye:
            HandleBye(packet);
            break;
        case PacketType::Identify:
            HandleIdentify(packet, now);
            break;
        case PacketType::Ping:
            if (_transport != nullptr)
            {
                _transport->Send(packet.Sender, PacketType::Pong, {});
            }
            break;
        case PacketType::Pong:
            HandlePong(packet, now);
            break;
        case PacketType::HostRequest:
            HandleHostRequest(packet, now);
            break;
        case PacketType::StatusQuery:
            SendStatus(packet.Sender, now);
            break;
        case PacketType::MatchEnd:
            HandleMatchEnd(packet, now);
            break;
        case PacketType::Chat:
            HandleChat(packet, now);
            break;
        case PacketType::Vote:
            HandleVote(packet, now);
            break;
        case PacketType::MapPick:
            HandleMapPick(packet, now);
            break;
        case PacketType::HitClaim:
            HandleHitClaim(packet, now);
            break;
        default:
            break;
        }
    }

    void DedicatedServer::HandleHitClaim(const ReceivedPacket& packet, double now)
    {
        if (_phase != SessionPhase::InMatch)
        {
            return;
        }
        const std::shared_ptr<Peer> peer = Find(packet.Sender);
        if (peer == nullptr || peer->SlotIndex < 0 || !Simulating())
        {
            return;
        }
        peer->LastSeen = now;
        NetHitClaims::Receive(peer->SlotIndex, packet.Payload());
    }

    void DedicatedServer::SendVerdicts(std::int32_t slot,
        std::span<const std::pair<std::uint16_t, std::uint8_t>> verdicts)
    {
        if (verdicts.empty() || _transport == nullptr)
        {
            return;
        }
        for (const std::shared_ptr<Peer>& peer : _peers)
        {
            if (peer->SlotIndex != slot)
            {
                continue;
            }
            HitVerdictPacket::Write(_scratch, verdicts, NetSession::CurrentMatchId(), NetSession::AuthorityEpoch(),
                NetPlayerLifecycle::Generation(slot), NetPlayerLifecycle::Get(slot));
            _transport->Send(peer->EndPoint, PacketType::HitVerdict,
                First(_scratch, HitVerdictPacket::HeaderSize + verdicts.size() * HitVerdictPacket::EntrySize));
            return;
        }
    }

    void DedicatedServer::HandleVote(const ReceivedPacket& packet, double now)
    {
        const std::shared_ptr<Peer> peer = Find(packet.Sender);
        const std::span<const std::uint8_t> payload = packet.Payload();
        if (peer == nullptr || peer->SlotIndex < 0 || payload.size() < static_cast<std::size_t>(VotePacket::Size))
        {
            return;
        }
        peer->LastSeen = now;
        if (!_allowMapVotes)
        {
            return;
        }
        const VotePacket vote = VotePacket::Read(payload);
        if (vote.Kind == VotePacket::KindPropose)
        {
            StartVote(peer, vote.RoomKey.value_or(""), now);
            return;
        }
        if (!_voteRunning || (vote.Kind != VotePacket::KindYes && vote.Kind != VotePacket::KindNo))
        {
            return;
        }
        if (peer->Ballot != 0)
        {
            return;
        }
        peer->Ballot = vote.Kind;
        BroadcastVoteState(now);
        Tally(now);
    }

    void DedicatedServer::OpenBallot()
    {
        _ballotOpen = _allowMapVotes;
        _tallyRooms.clear();
        _tallyVotes.clear();
        for (const std::shared_ptr<Peer>& peer : _peers)
        {
            peer->Pick.clear();
        }
    }

    void DedicatedServer::CloseBallot()
    {
        _ballotOpen = false;
        _tallyRooms.clear();
        _tallyVotes.clear();
        for (const std::shared_ptr<Peer>& peer : _peers)
        {
            peer->Pick.clear();
        }
    }

    void DedicatedServer::HandleMapPick(const ReceivedPacket& packet, double now)
    {
        const std::shared_ptr<Peer> peer = Find(packet.Sender);
        const std::span<const std::uint8_t> payload = packet.Payload();
        if (peer == nullptr || peer->SlotIndex < 0 || payload.size() < static_cast<std::size_t>(MapPickPacket::Size))
        {
            return;
        }
        peer->LastSeen = now;
        if (!_ballotOpen)
        {
            return;
        }
        std::string key = MapPickPacket::Read(payload).RoomKey.value_or("");
        if (!key.empty())
        {
            const std::optional<std::string> resolved = ResolveRoomKey(key);
            if (!resolved.has_value()
                || Runtime::StringEqualsOrdinalIgnoreCase(*resolved, CurrentDefinition().RoomKey.value_or("")))
            {
                Tell(peer, !resolved.has_value() ? "no map called \"" + key + "\"" : std::string("that is the map you are on"));
                return;
            }
            key = *resolved;
        }
        if (Runtime::StringEqualsOrdinalIgnoreCase(peer->Pick, key))
        {
            return;
        }
        const std::string was = peer->Pick;
        peer->Pick = key;
        Recount();
        if (!key.empty() && VotesFor(key) == 1)
        {
            const std::string who = !peer->Name.empty() ? peer->Name : "Player" + std::to_string(peer->SlotIndex + 1);
            Announce(who + " wants " + key + " next -- pick it to agree; "
                "the map with the most votes is the one loaded");
        }
        else if (key.empty() && !was.empty())
        {
            Log("slot " + std::to_string(peer->SlotIndex) + " took back its pick of " + was);
        }
        ApplyLeader();
        BroadcastMapChoices();
    }

    std::int32_t DedicatedServer::VotesFor(const std::string& roomKey) const
    {
        for (std::size_t i = 0; i < _tallyRooms.size(); i++)
        {
            if (Runtime::StringEqualsOrdinalIgnoreCase(_tallyRooms[i], roomKey))
            {
                return _tallyVotes[i];
            }
        }
        return 0;
    }

    void DedicatedServer::Recount()
    {
        _tallyRooms.clear();
        _tallyVotes.clear();
        for (const std::shared_ptr<Peer>& peer : _peers)
        {
            const std::string& key = peer->Pick;
            if (key.empty())
            {
                continue;
            }
            std::int32_t at = -1;
            for (std::size_t j = 0; j < _tallyRooms.size(); j++)
            {
                if (Runtime::StringEqualsOrdinalIgnoreCase(_tallyRooms[j], key))
                {
                    at = static_cast<std::int32_t>(j);
                    break;
                }
            }
            if (at < 0)
            {
                _tallyRooms.push_back(key);
                _tallyVotes.push_back(1);
            }
            else
            {
                _tallyVotes[static_cast<std::size_t>(at)]++;
            }
        }
        for (std::size_t i = 1; i < _tallyRooms.size(); i++)
        {
            for (std::size_t j = i; j > 0 && _tallyVotes[j] > _tallyVotes[j - 1]; j--)
            {
                std::swap(_tallyVotes[j], _tallyVotes[j - 1]);
                std::swap(_tallyRooms[j], _tallyRooms[j - 1]);
            }
        }
    }

    void DedicatedServer::ApplyLeader()
    {
        if (_tallyRooms.empty() || _tallyVotes[0] <= 0)
        {
            _rotation->ClearPending();
            return;
        }
        _rotation->PlayNext(_tallyRooms[0], ModeForRoom(_tallyRooms[0]));
    }

    void DedicatedServer::BroadcastMapChoices()
    {
        if (_peers.empty())
        {
            return;
        }
        const std::size_t count = std::min(_tallyRooms.size(), static_cast<std::size_t>(MapChoicesPacket::MaxChoices));
        auto keys = std::make_shared<std::vector<std::optional<std::string>>>(count);
        auto votes = std::make_shared<std::vector<std::uint8_t>>(count);
        for (std::size_t i = 0; i < count; i++)
        {
            (*keys)[i] = _tallyRooms[i];
            (*votes)[i] = static_cast<std::uint8_t>(std::clamp(_tallyVotes[i], 0, 255));
        }
        MapChoicesPacket packet{};
        packet.Open = static_cast<std::uint8_t>(_ballotOpen ? 1 : 0);
        packet.Count = static_cast<std::uint8_t>(count);
        packet.RoomKeys = keys;
        packet.Votes = votes;
        packet.Eligible = static_cast<std::uint8_t>(std::clamp(static_cast<std::int32_t>(_peers.size()), 0, 255));
        packet.Write(_scratch);
        for (const std::shared_ptr<Peer>& peer : _peers)
        {
            if (_transport != nullptr)
            {
                _transport->Send(peer->EndPoint, PacketType::MapChoices, First(_scratch, MapChoicesPacket::Size));
            }
        }
    }

    void DedicatedServer::ReviewPicks()
    {
        if (!_ballotOpen)
        {
            return;
        }
        Recount();
        ApplyLeader();
        BroadcastMapChoices();
    }

    void DedicatedServer::StartVote(const std::shared_ptr<Peer>& peer, const std::string& roomKey, double now)
    {
        if (_phase != SessionPhase::InMatch)
        {
            return;
        }
        if (_voteRunning)
        {
            Tell(peer, "a vote is already running");
            return;
        }
        if (static_cast<std::int32_t>(_peers.size()) < VoteMinimumPlayers)
        {
            Tell(peer, "not enough players to hold a vote");
            return;
        }
        // The cooldowns keep one player from spamming the others with votes;
        // nobody is spammed by a player who is alone.
        const bool alone = _peers.size() == 1;
        const double sinceVote = now - _voteResolvedAt;
        if (!alone && sinceVote < VoteCooldownSeconds)
        {
            Tell(peer, "another vote may be called in " + Runtime::ToString(VoteCooldownSeconds - sinceVote, "0") + " s");
            return;
        }
        const double sinceMine = now - peer->LastProposal;
        if (!alone && sinceMine < ProposalCooldownSeconds)
        {
            Tell(peer, "you may propose again in " + Runtime::ToString(ProposalCooldownSeconds - sinceMine, "0") + " s");
            return;
        }
        const std::optional<std::string> resolved = ResolveRoomKey(roomKey);
        if (!resolved.has_value())
        {
            Tell(peer, "no map called \"" + roomKey + "\"");
            return;
        }
        if (Runtime::StringEqualsOrdinalIgnoreCase(*resolved, CurrentDefinition().RoomKey.value_or("")))
        {
            Tell(peer, "that is the map you are on");
            return;
        }
        _voteRunning = true;
        _voteRoom = *resolved;
        _voteMode = ModeForRoom(*resolved);
        _voteProposer = !peer->Name.empty() ? peer->Name : "Player" + std::to_string(peer->SlotIndex + 1);
        _voteProposerSlot = peer->SlotIndex;
        _voteStartedAt = now;
        _voteResult = VoteStatePacket::StateIdle;
        peer->LastProposal = now;
        for (const std::shared_ptr<Peer>& other : _peers)
        {
            other->Ballot = 0;
        }
        peer->Ballot = VotePacket::KindYes;
        Announce(_voteProposer + " proposes " + *resolved + " -- F1 to accept, F2 to deny");
        Log("vote started by slot " + std::to_string(peer->SlotIndex) + " for " + *resolved + " ("
            + ::MphRead::ToString(_voteMode) + ")");
        BroadcastVoteState(now);
        Tally(now);
    }

    void DedicatedServer::Tally(double now)
    {
        if (!_voteRunning)
        {
            return;
        }
        const auto [yes, no, eligible, needed] = CountVotes();
        const std::string count = std::to_string(yes) + " of " + std::to_string(eligible);
        if (yes >= needed)
        {
            ResolveVote(now, true, count);
            return;
        }
        if (eligible - no < needed)
        {
            ResolveVote(now, false, count);
            return;
        }
        if (now - _voteStartedAt >= VoteSeconds)
        {
            ResolveVote(now, false, count);
        }
    }

    std::tuple<std::int32_t, std::int32_t, std::int32_t, std::int32_t> DedicatedServer::CountVotes() const
    {
        std::int32_t yes = 0;
        std::int32_t no = 0;
        for (const std::shared_ptr<Peer>& peer : _peers)
        {
            if (peer->Ballot == VotePacket::KindYes)
            {
                yes++;
            }
            else if (peer->Ballot == VotePacket::KindNo)
            {
                no++;
            }
        }
        const std::int32_t eligible = static_cast<std::int32_t>(_peers.size());
        const std::int32_t needed = std::max(1, static_cast<std::int32_t>(std::ceil(eligible * VoteThreshold)));
        return {yes, no, eligible, needed};
    }

    void DedicatedServer::ResolveVote(double now, bool passed, const std::string& count)
    {
        const std::string room = _voteRoom;
        const GameMode mode = _voteMode;
        _voteRunning = false;
        _voteResolvedAt = now;
        _voteResult = passed ? VoteStatePacket::StatePassed : VoteStatePacket::StateFailed;
        for (const std::shared_ptr<Peer>& peer : _peers)
        {
            peer->Ballot = 0;
        }
        if (passed)
        {
            Announce("vote passed (" + count + ") -- changing to " + room);
            Log("vote passed (" + count + ") for " + room);
            _rotation->PlayNext(room, mode);
            if (_sessionPolicy == ServerSessionPolicy::Lobby)
            {
                EndMatch(now, "map vote passed");
            }
            else
            {
                AdvanceMap(now);
            }
        }
        else
        {
            Announce("vote failed (" + count + ") -- staying on " + CurrentDefinition().RoomKey.value_or(""));
            Log("vote failed (" + count + ") for " + room);
        }
        BroadcastVoteState(now);
    }

    void DedicatedServer::ReviewVote(double now)
    {
        if (!_voteRunning)
        {
            return;
        }
        if (static_cast<std::int32_t>(_peers.size()) < VoteMinimumPlayers)
        {
            ResolveVote(now, false, "not enough players");
            return;
        }
        Tally(now);
    }

    std::optional<std::string> DedicatedServer::ResolveRoomKey(const std::string& roomKey)
    {
        if (Runtime::StringIsNullOrWhiteSpace(roomKey))
        {
            return std::nullopt;
        }
        const std::string wanted = Runtime::StringTrim(roomKey);
        for (const auto& entry : Metadata::RoomMetadata)
        {
            if (Runtime::RequireReference(entry.second).Multiplayer
                && Runtime::StringEqualsOrdinalIgnoreCase(entry.first, wanted))
            {
                return entry.first;
            }
        }
        return std::nullopt;
    }

    GameMode DedicatedServer::ModeForRoom(const std::string& roomKey) const
    {
        for (const std::shared_ptr<const RotationEntry>& entry : _rotation->Entries())
        {
            if (Runtime::StringEqualsOrdinalIgnoreCase(entry->RoomKey, roomKey))
            {
                return entry->Mode;
            }
        }
        return CurrentDefinition().Mode;
    }

    void DedicatedServer::BroadcastVoteState(double now)
    {
        if (_peers.empty())
        {
            return;
        }
        VoteStatePacket state{};
        state.State = _voteRunning ? VoteStatePacket::StateRunning : _voteResult;
        state.RoomKey = _voteRunning ? _voteRoom : std::string{};
        state.Proposer = _voteRunning ? _voteProposer : std::string{};
        if (_voteRunning)
        {
            const auto [yes, no, eligible, needed] = CountVotes();
            state.Yes = static_cast<std::uint8_t>(yes);
            state.No = static_cast<std::uint8_t>(no);
            state.Eligible = static_cast<std::uint8_t>(eligible);
            state.Needed = static_cast<std::uint8_t>(needed);
            state.Seconds = static_cast<std::uint16_t>(std::max(0.0, VoteSeconds - (now - _voteStartedAt)));
        }
        else if (_allowMapVotes)
        {
            const double wait = VoteCooldownSeconds - (now - _voteResolvedAt);
            state.Seconds = static_cast<std::uint16_t>(std::clamp(wait, 0.0, 65535.0));
        }
        else
        {
            state.Seconds = 0xFFFF;
        }
        state.Write(_scratch);
        for (const std::shared_ptr<Peer>& peer : _peers)
        {
            if (_transport != nullptr)
            {
                _transport->Send(peer->EndPoint, PacketType::VoteState, First(_scratch, VoteStatePacket::Size));
            }
        }
    }

    void DedicatedServer::Tell(const std::shared_ptr<Peer>& peer, const std::string& text)
    {
        ChatPacket chat{};
        chat.Slot = 0xFF;
        chat.Kind = ChatPacket::KindSystem;
        chat.Name = std::string{};
        chat.Text = text;
        chat.Write(_scratch);
        if (_transport != nullptr)
        {
            _transport->Send(peer->EndPoint, PacketType::Chat, First(_scratch, ChatPacket::Size));
        }
    }

    void DedicatedServer::HandleChat(const ReceivedPacket& packet, double now)
    {
        const std::shared_ptr<Peer> peer = Find(packet.Sender);
        const std::span<const std::uint8_t> payload = packet.Payload();
        if (peer == nullptr || peer->SlotIndex < 0 || payload.size() < static_cast<std::size_t>(ChatPacket::Size))
        {
            return;
        }
        peer->LastSeen = now;
        ChatPacket chat = ChatPacket::Read(payload);
        const std::string text = chat.Text.value_or(std::string{});
        if (text.empty())
        {
            return;
        }
        peer->ChatCredit = std::min(ChatBurst, peer->ChatCredit + (now - peer->ChatCreditAt) * ChatRatePerSecond);
        peer->ChatCreditAt = now;
        if (peer->ChatCredit < 1)
        {
            if (peer->ChatDropped++ == 0)
            {
                Log("chat from slot " + std::to_string(peer->SlotIndex) + " (" + peer->EndPoint->ToString()
                    + ") dropped: too fast");
            }
            return;
        }
        peer->ChatCredit -= 1;
        peer->ChatDropped = 0;
        chat.Slot = static_cast<std::uint8_t>(peer->SlotIndex);
        chat.Name = !peer->Name.empty() ? peer->Name : "Player" + std::to_string(peer->SlotIndex);
        const bool teamOnly = chat.Kind == ChatPacket::KindTeam && GameState::IsTeamMode(CurrentDefinition().Mode)
            && peer->TeamIndex >= 0;
        chat.Kind = teamOnly ? ChatPacket::KindTeam : ChatPacket::KindSay;
        chat.Write(_scratch);
        for (const std::shared_ptr<Peer>& other : _peers)
        {
            if (other != peer && (!teamOnly || other->TeamIndex == peer->TeamIndex) && _transport != nullptr)
            {
                _transport->Send(other->EndPoint, PacketType::Chat, First(_scratch, ChatPacket::Size));
            }
        }
        Log("chat " + chat.Name.value_or(std::string{}) + ": " + text);
    }

    void DedicatedServer::Announce(const std::string& text)
    {
        if (_peers.empty())
        {
            return;
        }
        ChatPacket chat{};
        chat.Slot = 0xFF;
        chat.Kind = ChatPacket::KindSystem;
        chat.Name = std::string{};
        chat.Text = text;
        chat.Write(_scratch);
        for (const std::shared_ptr<Peer>& peer : _peers)
        {
            if (_transport != nullptr)
            {
                _transport->Send(peer->EndPoint, PacketType::Chat, First(_scratch, ChatPacket::Size));
            }
        }
    }

    void DedicatedServer::SendStatus(const std::shared_ptr<System::Net::IPEndPoint>& sender, double now)
    {
        const MatchDefinition definition = CurrentDefinition();
        ServerStatusPacket status{};
        status.Match = BuildState(now);
        status.Phase = _phase;
        status.Format = definition.Format;
        status.LobbyEnabled = _sessionPolicy == ServerSessionPolicy::Lobby;
        status.AllowJoinInProgress = _allowJoinInProgress;
        status.MaxPlayers = static_cast<std::uint8_t>(_maxPlayers);
        status.Protocol = static_cast<std::uint8_t>(NetConfig::ProtocolVersion);
        status.ServerName = _serverName;
        status.Flags = static_cast<std::uint8_t>(_hosts.CanHost() ? ServerStatusPacket::FlagCanHost : 0);
        status.Write(_scratch);
        if (_transport != nullptr)
        {
            _transport->Send(sender, PacketType::StatusReply, First(_scratch, ServerStatusPacket::SizeWithFlags));
        }
    }

    void DedicatedServer::HandleHostRequest(const ReceivedPacket& packet, double now)
    {
        HostReplyPacket reply{};
        const std::span<const std::uint8_t> payload = packet.Payload();
        if (payload.size() < static_cast<std::size_t>(HostRequestPacket::Size))
        {
            reply.Reason = "malformed request";
        }
        else
        {
            const HostRequestPacket request = HostRequestPacket::Read(payload);
            if (request.Protocol != NetConfig::ProtocolVersion)
            {
                reply.Reason = "this server speaks protocol " + std::to_string(NetConfig::ProtocolVersion)
                    + ", your build speaks " + std::to_string(request.Protocol);
            }
            else if (!_hosts.CanHost())
            {
                reply.Reason = "this server does not open new games";
            }
            else
            {
                reply = _hosts.Start(request, packet.Sender, now);
            }
        }
        reply.Write(_scratch);
        if (_transport != nullptr)
        {
            _transport->Send(packet.Sender, PacketType::HostReply, First(_scratch, HostReplyPacket::Size));
        }
        if (!reply.Started)
        {
            Log("refused a game for " + packet.Sender->ToString() + ": " + reply.Reason.value_or(""));
        }
    }

    void DedicatedServer::HandleHello(const ReceivedPacket& packet, double now)
    {
        const std::span<const std::uint8_t> payload = packet.Payload();
        if (payload.empty() || payload[0] != NetConfig::ProtocolVersion)
        {
            Log("rejected " + packet.Sender->ToString() + ": protocol mismatch");
            SendRefusal(packet.Sender, RefusedPacket::ReasonProtocol);
            return;
        }
        const std::uint32_t clientId = payload.size() >= 6
            ? Runtime::ReadUInt32LittleEndian(payload.subspan(2, 4)) : 0U;
        std::shared_ptr<Peer> peer = Find(packet.Sender);
        if (peer != nullptr && peer->ClientId != clientId)
        {
            Remove(peer, "replaced connection");
            peer = nullptr;
        }
        if (peer == nullptr && clientId != 0)
        {
            for (const std::shared_ptr<Peer>& candidate : _peers)
            {
                if (candidate->ClientId == clientId)
                {
                    peer = candidate;
                    Log("slot " + std::to_string(peer->SlotIndex) + " (" + peer->Name + ") came back on "
                        + packet.Sender->ToString() + ", was " + peer->EndPoint->ToString());
                    peer->EndPoint = packet.Sender;
                    break;
                }
            }
        }
        if (peer == nullptr)
        {
            std::int32_t slot = -1;
            if (payload.size() >= 2 && payload[1] != 0xFF && payload[1] < _maxPlayers && SlotFree(payload[1]))
            {
                slot = payload[1];
            }
            if (slot < 0)
            {
                slot = NextFreeSlot();
            }
            if (slot < 0)
            {
                Log("rejected " + packet.Sender->ToString() + ": session full");
                SendRefusal(packet.Sender, RefusedPacket::ReasonFull);
                return;
            }
            if (_peers.empty() && _sessionPolicy == ServerSessionPolicy::Continuous)
            {
                _matchStarted = now;
                _matchEndedAt = -1;
                _phase = SessionPhase::InMatch;
                CloseBallot();
                _matchId = NetLifecycleTracker::Next(_matchId);
                _snapshotSeen = false;
                _lastSnapshot = nullptr;
                _slotLives.fill(0);
            }
            if (_phase == SessionPhase::InMatch && !_allowJoinInProgress)
            {
                SendRefusal(packet.Sender, RefusedPacket::ReasonInMatch);
                return;
            }
            const std::int8_t team = ChooseTeam(CurrentDefinition());
            if (LobbyRules::TeamCount(CurrentDefinition()) > 0 && team < 0)
            {
                SendRefusal(packet.Sender, RefusedPacket::ReasonFull);
                return;
            }
            const auto s = static_cast<std::size_t>(slot);
            _slotGenerations[s] = NetLifecycleTracker::Next(_slotGenerations[s]);
            _slotLives[s] = 0;
            peer = std::make_shared<Peer>();
            peer->EndPoint = packet.Sender;
            peer->SlotIndex = slot;
            peer->ClientId = clientId;
            peer->TeamIndex = team;
            _peers.push_back(peer);
            _peerCount.store(static_cast<std::int32_t>(_peers.size()));
            _everOccupied.store(true);
            if (_authority == nullptr && !_runsTheMatch)
            {
                _authority = peer;
                _authorityEpoch++;
                _snapshotSeen = false;
                Log(packet.Sender->ToString() + " joined as slot " + std::to_string(slot) + " (authority)");
                NotifyAuthority(peer);
            }
            else
            {
                Log(packet.Sender->ToString() + " joined as slot " + std::to_string(slot));
                if (Simulating() && _lastSnapshot != nullptr && _transport != nullptr)
                {
                    _transport->Send(peer->EndPoint, PacketType::Snapshot, *_lastSnapshot);
                }
            }
        }
        peer->ClientId = clientId;
        ClaimOwner(peer, payload);
        peer->LastSeen = now;
        _scratch[0] = static_cast<std::uint8_t>(peer->SlotIndex);
        Runtime::WriteUInt32LittleEndian(From(_scratch, 1), clientId);
        Runtime::WriteUInt16LittleEndian(From(_scratch, 5), _matchId);
        Runtime::WriteUInt64LittleEndian(From(_scratch, 7), _authorityEpoch);
        Runtime::WriteUInt16LittleEndian(From(_scratch, 15), _slotGenerations[static_cast<std::size_t>(peer->SlotIndex)]);
        if (_transport != nullptr)
        {
            _transport->Send(peer->EndPoint, PacketType::Welcome, First(_scratch, 17));
        }
        MatchStatePacket state = BuildState(now);
        state.Write(_scratch);
        if (_transport != nullptr)
        {
            _transport->Send(peer->EndPoint, PacketType::MatchState, First(_scratch, MatchStatePacket::Size));
        }
        TouchLobbyRevision("peer slot " + std::to_string(peer->SlotIndex) + " connected");
    }

    void DedicatedServer::HandleMatchEnd(const ReceivedPacket& packet, double now)
    {
        const std::shared_ptr<Peer> peer = Find(packet.Sender);
        if (peer == nullptr)
        {
            return;
        }
        peer->LastSeen = now;
        if (Simulating() || peer != _authority)
        {
            return;
        }
        const std::span<const std::uint8_t> payload = packet.Payload();
        if (payload.size() != 10
            || Runtime::ReadUInt16LittleEndian(payload) != _matchId
            || Runtime::ReadUInt64LittleEndian(payload.subspan(2)) != _authorityEpoch)
        {
            return;
        }
        EndMatch(now, "a player reached the goal");
    }

    void DedicatedServer::SendRefusal(const std::shared_ptr<System::Net::IPEndPoint>& to, std::uint8_t reason)
    {
        RefusedPacket refusal{};
        refusal.Reason = reason;
        refusal.Players = static_cast<std::uint8_t>(_peers.size());
        refusal.MaxPlayers = static_cast<std::uint8_t>(_maxPlayers);
        refusal.Write(_scratch);
        if (_transport != nullptr)
        {
            _transport->Send(to, PacketType::Refused, First(_scratch, RefusedPacket::Size));
        }
    }

    void DedicatedServer::HandleIdentify(const ReceivedPacket& packet, double now)
    {
        const std::shared_ptr<Peer> peer = Find(packet.Sender);
        if (peer == nullptr)
        {
            return;
        }
        peer->LastSeen = now;
        const std::span<const std::uint8_t> payload = packet.Payload();
        if (payload.size() < 2)
        {
            return;
        }
        if (_phase == SessionPhase::Starting && (_expectedLoadedSlots & (1 << peer->SlotIndex)) != 0)
        {
            return;
        }
        const std::uint8_t hunter = payload[0];
        const std::uint8_t color = payload[1];
        if (hunter >= Launcher::Hunters::Playable || color > 3)
        {
            return;
        }
        // Encoding.ASCII.GetString(...).TrimEnd('\0').Trim()
        std::string name = Runtime::AsciiGetString(payload.subspan(2));
        while (!name.empty() && name.back() == '\0')
        {
            name.pop_back();
        }
        name = Runtime::StringTrim(name);
        if (name.empty())
        {
            return;
        }
        if (static_cast<std::int32_t>(name.size()) > RosterPacket::MaxNameBytes)
        {
            name = name.substr(0, RosterPacket::MaxNameBytes);
        }
        if (peer->Name == name && peer->Hunter == hunter && peer->Color == color)
        {
            return;
        }
        const bool firstName = peer->Name.empty();
        if (peer->Hunter != hunter || peer->Color != color)
        {
            peer->LobbyReady = false;
        }
        peer->Name = name;
        peer->Hunter = hunter;
        peer->Color = color;
        Log("slot " + std::to_string(peer->SlotIndex) + " is \"" + name + "\" playing "
            + ::MphRead::ToString(static_cast<Hunter>(hunter)) + " in suit " + std::to_string(color + 1));
        if (firstName)
        {
            Announce(name + " joined");
        }
        TouchLobbyRevision("slot " + std::to_string(peer->SlotIndex) + " identity changed");
    }

    void DedicatedServer::NotifyAuthority(const std::shared_ptr<Peer>& peer)
    {
        MatchStatePacket match = BuildState(_now);
        match.Write(_scratch);
        if (_transport == nullptr)
        {
            return;
        }
        _transport->Send(peer->EndPoint, PacketType::MatchState, First(_scratch, MatchStatePacket::Size));
        RosterPacket roster = BuildRoster();
        roster.Write(_scratch);
        _transport->Send(peer->EndPoint, PacketType::Roster, First(_scratch, RosterPacket::Size));
        if (_lastSnapshot != nullptr)
        {
            SnapshotHeader header = SnapshotHeader::Read(*_lastSnapshot);
            if (header.MatchId == _matchId)
            {
                std::vector<std::uint8_t> seed = *_lastSnapshot;
                header.AuthorityEpoch = _authorityEpoch;
                header.Frame = 0;
                header.Write(seed);
                _transport->Send(peer->EndPoint, PacketType::Snapshot, seed);
            }
        }
        _scratch[0] = static_cast<std::uint8_t>(peer->SlotIndex);
        Runtime::WriteUInt16LittleEndian(From(_scratch, 1), _matchId);
        Runtime::WriteUInt64LittleEndian(From(_scratch, 3), _authorityEpoch);
        Runtime::WriteUInt16LittleEndian(From(_scratch, 11), _slotGenerations[static_cast<std::size_t>(peer->SlotIndex)]);
        _transport->Send(peer->EndPoint, PacketType::Authority, First(_scratch, 13));
    }

    void DedicatedServer::PingPeers(double now)
    {
        for (const std::shared_ptr<Peer>& peer : _peers)
        {
            if (peer->PingPending && now - peer->PingSentAt < 5)
            {
                continue;
            }
            peer->PingId++;
            peer->PingSentAt = now;
            peer->PingPending = true;
            _scratch[0] = peer->PingId;
            if (_transport != nullptr)
            {
                _transport->Send(peer->EndPoint, PacketType::Ping, First(_scratch, 1));
            }
        }
    }

    void DedicatedServer::HandlePong(const ReceivedPacket& packet, double now)
    {
        const std::shared_ptr<Peer> peer = Find(packet.Sender);
        if (peer == nullptr || !peer->PingPending)
        {
            return;
        }
        const std::span<const std::uint8_t> payload = packet.Payload();
        if (payload.empty() || payload[0] != peer->PingId)
        {
            return;
        }
        peer->PingPending = false;
        peer->LastSeen = now;
        std::int32_t rtt = Runtime::ConvertToInt32Net9(Runtime::RoundToEven((now - peer->PingSentAt) * 1000));
        rtt = std::clamp(rtt, 0, 9999);
        peer->Ping = peer->Ping == 0 ? rtt : (peer->Ping * 2 + rtt) / 3;
    }

    RosterPacket DedicatedServer::BuildRoster()
    {
        RosterPacket roster = RosterPacket::Create();
        roster.MatchId = _matchId;
        roster.AuthorityEpoch = _authorityEpoch;
        roster.Revision = ++_rosterRevision;
        roster.SessionRevision = _sessionRevision;
        auto& slots = Runtime::RequireReference(roster.Slots);
        auto& generations = Runtime::RequireReference(roster.Generations);
        auto& teams = Runtime::RequireReference(roster.Teams);
        auto& ready = Runtime::RequireReference(roster.LobbyReady);
        auto& hunters = Runtime::RequireReference(roster.Hunters);
        auto& colors = Runtime::RequireReference(roster.Colors);
        auto& pings = Runtime::RequireReference(roster.Pings);
        auto& names = Runtime::RequireReference(roster.Names);
        for (std::size_t i = 0; i < _peers.size() && i < static_cast<std::size_t>(RosterPacket::MaxSlots); i++)
        {
            const Peer& peer = *_peers[i];
            const auto at = static_cast<std::size_t>(roster.Count);
            slots[at] = static_cast<std::uint8_t>(peer.SlotIndex);
            generations[at] = _slotGenerations[static_cast<std::size_t>(peer.SlotIndex)];
            teams[at] = peer.TeamIndex;
            ready[at] = peer.LobbyReady;
            hunters[at] = peer.Hunter;
            colors[at] = peer.Color;
            pings[at] = static_cast<std::uint16_t>(std::clamp(peer.Ping, 0, 9999));
            names[at] = !peer.Name.empty() ? peer.Name : "Player" + std::to_string(peer.SlotIndex + 1);
            roster.Count++;
        }
        return roster;
    }

    void DedicatedServer::BroadcastRoster()
    {
        RosterPacket roster = BuildRoster();
        if (_sim != nullptr)
        {
            NetSession::ApplyMatchState(BuildState(_now), false);
            NetSession::ApplyRoster(roster);
        }
        if (_peers.empty())
        {
            return;
        }
        roster.Write(_scratch);
        for (const std::shared_ptr<Peer>& peer : _peers)
        {
            if (_transport != nullptr)
            {
                _transport->Send(peer->EndPoint, PacketType::Roster, First(_scratch, RosterPacket::Size));
            }
        }
    }

    void DedicatedServer::HandleIntent(const ReceivedPacket& packet, double now)
    {
        if (_phase == SessionPhase::Lobby || _phase == SessionPhase::Starting)
        {
            return;
        }
        const std::shared_ptr<Peer> peer = Find(packet.Sender);
        if (peer == nullptr || (_authority == nullptr && !Simulating()))
        {
            return;
        }
        const std::span<const std::uint8_t> payload = packet.Payload();
        if (payload.size() < static_cast<std::size_t>(IntentPacket::Size)
            || payload.size() > static_cast<std::size_t>(IntentPacket::BombFullSize))
        {
            return;
        }
        peer->LastSeen = now;
        const auto s = static_cast<std::size_t>(peer->SlotIndex);
        const IntentPacket intent = IntentPacket::Read(payload);
        const std::uint16_t life = _sim != nullptr ? NetPlayerLifecycle::Get(peer->SlotIndex) : _slotLives[s];
        if (intent.MatchId != _matchId || intent.AuthorityEpoch != _authorityEpoch
            || intent.SlotGeneration != _slotGenerations[s] || intent.LifeId != life)
        {
            return;
        }
        if (_sim != nullptr)
        {
            NetSession::AcceptSlotIntent(peer->SlotIndex, intent);
        }
        if (peer->LastIntentFrame != 0 && !NetLifecycleTracker::Newer(intent.Frame, peer->LastIntentFrame))
        {
            return;
        }
        peer->LastIntentFrame = intent.Frame;
        peer->PostMatchReady = Runtime::HasFlag(intent.Buttons, IntentButtons::ReadyState);
        _scratch[0] = static_cast<std::uint8_t>(peer->SlotIndex);
        std::copy(payload.begin(), payload.end(), _scratch.begin() + 1);
        for (const std::shared_ptr<Peer>& other : _peers)
        {
            if (other != peer && _transport != nullptr)
            {
                _transport->Send(other->EndPoint, PacketType::SlotIntent, First(_scratch, payload.size() + 1));
            }
        }
    }

    void DedicatedServer::HandleSnapshot(const ReceivedPacket& packet, double now)
    {
        if (_phase == SessionPhase::Lobby || _phase == SessionPhase::Starting)
        {
            return;
        }
        const std::shared_ptr<Peer> peer = Find(packet.Sender);
        if (peer == nullptr)
        {
            return;
        }
        peer->LastSeen = now;
        if (Simulating() || peer != _authority)
        {
            return;
        }
        const std::span<const std::uint8_t> payload = packet.Payload();
        if (payload.size() < static_cast<std::size_t>(SnapshotHeader::Size))
        {
            return;
        }
        const SnapshotHeader header = SnapshotHeader::Read(payload);
        const std::size_t timeOffset = SnapshotHeader::Size + static_cast<std::size_t>(header.PlayerCount) * PlayerState::Size;
        const std::size_t healthOffset = timeOffset + NetMatchTimeSync::Size;
        if (header.MatchId != _matchId || header.AuthorityEpoch != _authorityEpoch
            || header.PlayerCount > Entities::PlayerEntity::SlotCapacity
            || healthOffset > payload.size()
            || !NetMatchTimeSync::Validate(payload.subspan(timeOffset, NetMatchTimeSync::Size))
            || !NetHealthSync::Validate(payload.subspan(healthOffset))
            || Runtime::ReadUInt16LittleEndian(payload.subspan(healthOffset)) != _matchId
            || (_snapshotSeen && !NetLifecycleTracker::Newer(header.Frame, _snapshotFrame)))
        {
            return;
        }
        std::int32_t occupied = 0;
        for (std::int32_t i = 0; i < header.PlayerCount; i++)
        {
            const PlayerState state = PlayerState::Read(
                payload.subspan(SnapshotHeader::Size + static_cast<std::size_t>(i) * PlayerState::Size));
            if (state.SlotIndex >= _slotLives.size() || (occupied & (1 << state.SlotIndex)) != 0
                || state.SlotGeneration != _slotGenerations[state.SlotIndex])
            {
                return;
            }
            occupied |= 1 << state.SlotIndex;
        }
        for (std::int32_t i = 0; i < header.PlayerCount; i++)
        {
            const PlayerState state = PlayerState::Read(
                payload.subspan(SnapshotHeader::Size + static_cast<std::size_t>(i) * PlayerState::Size));
            _slotLives[state.SlotIndex] = state.LifeId;
        }
        _snapshotSeen = true;
        _snapshotFrame = header.Frame;
        _lastSnapshot = std::make_shared<std::vector<std::uint8_t>>(payload.begin(), payload.end());
        for (const std::shared_ptr<Peer>& other : _peers)
        {
            if (other != peer && _transport != nullptr)
            {
                _transport->Send(other->EndPoint, PacketType::Snapshot, payload);
            }
        }
    }

    void DedicatedServer::HandleBye(const ReceivedPacket& packet)
    {
        const std::shared_ptr<Peer> peer = Find(packet.Sender);
        if (peer != nullptr)
        {
            Remove(peer, "left");
        }
    }

    void DedicatedServer::DropTimedOut(double now)
    {
        for (std::int32_t i = static_cast<std::int32_t>(_peers.size()) - 1; i >= 0; i--)
        {
            if (i >= static_cast<std::int32_t>(_peers.size()))
            {
                continue;
            }
            const std::shared_ptr<Peer> peer = _peers[static_cast<std::size_t>(i)];
            if (now - peer->LastSeen > NetConfig::TimeoutSeconds)
            {
                Remove(peer, "timed out");
            }
        }
    }

    void DedicatedServer::Remove(const std::shared_ptr<Peer>& peer, const std::string& reason)
    {
        const std::shared_ptr<Peer> removed = peer;
        const auto found = std::find(_peers.begin(), _peers.end(), removed);
        if (found != _peers.end())
        {
            _peers.erase(found);
        }
        _peerCount.store(static_cast<std::int32_t>(_peers.size()));
        LobbyPeerRemoved(removed);
        BroadcastRoster();
        ReviewVote(_now);
        ReviewPicks();
        Log(removed->EndPoint->ToString() + " " + reason + " (slot " + std::to_string(removed->SlotIndex) + ")");
        if (!removed->Name.empty())
        {
            Announce(removed->Name + " " + reason);
        }
        if (_runsTheMatch || _authority != removed)
        {
            return;
        }
        _authority = !_peers.empty() ? _peers[0] : nullptr;
        _authorityEpoch++;
        _snapshotSeen = false;
        BroadcastMatchState(_now);
        BroadcastRoster();
        Log(_authority != nullptr
            ? "authority moved to slot " + std::to_string(_authority->SlotIndex)
            : std::string("no peers left; waiting for a new authority"));
        if (_authority != nullptr)
        {
            NotifyAuthority(_authority);
        }
    }

    std::shared_ptr<DedicatedServer::Peer> DedicatedServer::Find(
        const std::shared_ptr<System::Net::IPEndPoint>& endPoint) const
    {
        for (const std::shared_ptr<Peer>& peer : _peers)
        {
            if (peer->EndPoint != nullptr && endPoint != nullptr && peer->EndPoint->Equals(*endPoint))
            {
                return peer;
            }
        }
        return nullptr;
    }

    bool DedicatedServer::SlotFree(std::int32_t slot) const
    {
        return std::none_of(_peers.begin(), _peers.end(),
            [slot](const std::shared_ptr<Peer>& peer) { return peer->SlotIndex == slot; });
    }

    std::int32_t DedicatedServer::NextFreeSlot() const
    {
        for (std::int32_t slot = 0; slot < _maxPlayers; slot++)
        {
            if (SlotFree(slot))
            {
                return slot;
            }
        }
        return -1;
    }

    void DedicatedServer::Log(const std::string& message)
    {
        Runtime::ConsoleWriteLine("[" + Runtime::DateTimeToString(Runtime::DateTimeNow(), "HH:mm:ss")
            + "] [server] " + message);
    }
}
