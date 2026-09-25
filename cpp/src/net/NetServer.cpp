#include "NetServer.h"

#include "NetMaster.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <thread>

namespace fp::net {

namespace {

// GameMode's names, as the rotation file writes them (the C#'s enum), in this port's order.
constexpr const char* modeNames[] = {"Battle", "BattleTeams", "Survival", "SurvivalTeams", "Capture", "Bounty", "BountyTeams", "Nodes",
    "NodesTeams", "Defender", "DefenderTeams", "PrimeHunter"};

// DedicatedServer's timings.
constexpr double StepSeconds = 1 / 60.0;
constexpr int MaxCatchUpSteps = 5;
constexpr double StallSeconds = 0.25;
// The winner's camera, the results (GameState.MatchEndingSeconds) and a second for the fade.
constexpr double EndSequenceSeconds = 3.0 + 10.0 + 1.0;
constexpr double ReadyWaitSeconds = 30.0, AllReadySeconds = 5.0;
constexpr double ChatRatePerSecond = 0.5, ChatBurst = 3;

double clockNow() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

void log(const char* format, auto... args)
{
    const std::time_t t = std::time(nullptr);
    char stamp[16];
    std::strftime(stamp, sizeof stamp, "%H:%M:%S", std::localtime(&t));
    std::printf("[%s] [server] ", stamp);
    if constexpr (sizeof...(args) == 0) {
        std::fputs(format, stdout);
    } else {
        std::printf(format, args...);
    }
    std::putchar('\n');
    std::fflush(stdout);
}

std::string trim(std::string text)
{
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}

} // namespace

// ---- Rotation ------------------------------------------------------------------

Rotation Rotation::single(RotationEntry entry)
{
    Rotation rotation;
    rotation.m_entries = {std::move(entry)};
    return rotation;
}

bool Rotation::load(const std::string& path, Rotation& rotation, std::string& error)
{
    std::ifstream in(path);
    if (!in) {
        error = "cannot read " + path;
        return false;
    }
    std::vector<RotationEntry> entries;
    std::string line;
    while (std::getline(in, line)) {
        if (const auto comment = line.find('#'); comment != std::string::npos) {
            line.resize(comment);
        }
        std::vector<std::string> parts;
        size_t start = 0;
        while (true) {
            const auto bar = line.find('|', start);
            parts.push_back(trim(line.substr(start, bar == std::string::npos ? std::string::npos : bar - start)));
            if (bar == std::string::npos) {
                break;
            }
            start = bar + 1;
        }
        if (parts[0].empty()) {
            continue;
        }
        RotationEntry entry;
        entry.roomKey = parts[0];
        if (parts.size() > 1) {
            std::string name = parts[1];
            std::erase(name, ' ');
            for (int i = 0; i < 12; i++) {
                std::string candidate = modeNames[i];
                if (name.size() == candidate.size()
                    && std::equal(name.begin(), name.end(), candidate.begin(), [](char a, char b) { return std::tolower(a) == std::tolower(b); })) {
                    entry.mode = i;
                }
            }
        }
        if (parts.size() > 2) {
            try {
                entry.timeLimit = std::stof(parts[2]) * 60;
            } catch (...) {
            }
        }
        if (parts.size() > 3) {
            try {
                entry.pointGoal = std::stoi(parts[3]);
            } catch (...) {
            }
        }
        entries.push_back(entry);
    }
    if (entries.empty()) {
        error = path + " has no match in it";
        return false;
    }
    rotation.m_entries = std::move(entries);
    rotation.m_index = 0;
    return true;
}

const RotationEntry& Rotation::advance()
{
    if (m_pending) {
        // Taken rather than stepped over: the cycle's own index does not move.
        m_override = std::move(m_pending);
        m_pending.reset();
        return current();
    }
    m_override.reset(); // the borrowed turn is over: the cycle steps on as it would have
    m_index = (m_index + 1) % m_entries.size();
    return current();
}

void Rotation::playNext(const std::string& roomKey, int mode)
{
    RotationEntry entry = current();
    entry.roomKey = roomKey;
    if (mode >= 0) {
        entry.mode = mode;
    }
    m_pending = entry;
}

int Rotation::modeFor(const std::string& roomKey, int fallback) const
{
    for (const RotationEntry& entry : m_entries) {
        if (entry.roomKey.size() == roomKey.size()
            && std::equal(roomKey.begin(), roomKey.end(), entry.roomKey.begin(), [](char a, char b) { return std::tolower(a) == std::tolower(b); })) {
            return entry.mode;
        }
    }
    return fallback;
}

// ---- Server --------------------------------------------------------------------

Server::Server(Config config, std::unique_ptr<Simulation> simulation)
    : m_config(std::move(config))
    , m_sim(std::move(simulation))
{
    m_config.maxPlayers = std::clamp(m_config.maxPlayers, 2, SlotCapacity);
    // A restarted server is a newer authority than the one before it.
    m_epoch = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                        std::chrono::system_clock::now().time_since_epoch())
                                        .count());
}

Server::~Server() { m_transport.close(); }

int Server::layerPlayers() const
{
    // MatchWorldProfile.Resolve: an exact team format lays the room out for its own count.
    const SessionState match = currentDefinition();
    const TeamLayout layout = currentLayout();
    const bool exact = isTeamModeWire(match.mode) && match.format != SessionState::FormatAuto;
    return std::clamp(exact ? layout.totalPlayers() : m_config.maxPlayers, 2, 4);
}

bool Server::teamMode() const { return isTeamModeWire(currentDefinition().mode); }

bool Server::run(const std::atomic<bool>* stop)
{
    if (!m_transport.open(m_config.port)) {
        log("cannot listen on UDP %u: %s", m_config.port, m_transport.error().c_str());
        return false;
    }
    log("listening on UDP %u, up to %d players", m_transport.localPort(), m_config.maxPlayers);
    log("rotation: %zu map(s), starting on %s (%s)", m_config.rotation.size(), m_config.rotation.current().roomKey.c_str(),
        modeNames[m_config.rotation.current().mode]);
    const double start = clockNow();
    m_now = start;
    m_lobbyMatch = definitionFor(m_config.rotation.current());
    if (m_config.lobby) {
        m_phase = SessionState::PhaseLobby;
        log("lobby: players gather here and the owner starts each match");
    } else if (!startMatch(start, m_config.rotation.current())) {
        return false;
    }
    log("this server runs the match itself");
    if (!m_config.masterHost.empty()) {
        m_reporter = std::make_unique<MasterReporter>(m_config.masterHost, m_config.masterPort);
        log("announcing to the directory %s:%u", m_config.masterHost.c_str(), m_config.masterPort);
    }
    if (m_config.hostFirst > 0 && m_config.hostLast >= m_config.hostFirst) {
        m_hosts = std::make_unique<HostPool>(m_config.gameFiles, m_config.hostFirst, m_config.hostLast, m_config.masterHost, m_config.masterPort);
        log("opens games for other players on ports %u-%u", m_config.hostFirst, m_config.hostLast);
    }
    m_running = true;
    m_lastOccupied = start;
    double lastBroadcast = start, lastReport = start;
    while (m_running && (stop == nullptr || !*stop)) {
        const double now = clockNow();
        m_now = now;
        if (!m_peers.empty()) {
            m_lastOccupied = now;
            m_everOccupied = true;
        } else if (m_config.exitWhenEmpty > 0
            && now - m_lastOccupied >= (m_everOccupied ? m_config.exitWhenEmpty : std::max(m_config.exitWhenEmpty, m_config.startupGrace))) {
            log("nobody here for %.0f s: closing", now - m_lastOccupied);
            break;
        }
        if (m_hosts) {
            m_hosts->reap();
        }
        for (const ReceivedPacket& packet : m_transport.drain()) {
            handle(packet, now);
        }
        dropTimedOut(now);
        checkLoadBarrier(now);
        if (m_phase == SessionState::PhaseInMatch || m_phase == SessionState::PhasePostMatch) {
            simulate(now);
        }
        if (m_matchEndedAt >= 0 && now - m_matchEndedAt >= endSequence()) {
            if (m_config.lobby) {
                returnToLobby(now);
            } else if (!advanceMap(now)) {
                break;
            }
        }
        if (now - lastBroadcast >= 1.0) {
            lastBroadcast = now;
            pingPeers(now);
            broadcastSessionState();
            if (m_phase == SessionState::PhaseInMatch || m_phase == SessionState::PhasePostMatch) {
                broadcastMatchState(now);
            }
            broadcastRoster();
            tally(now);
            broadcastVoteState(now);
            if (m_ballotOpen) {
                broadcastMapChoices();
            }
            if (m_reporter) {
                const SessionState match = currentDefinition();
                MasterHeartbeat beat;
                beat.port = m_transport.localPort();
                beat.players = static_cast<uint8_t>(m_peers.size());
                beat.maxPlayers = static_cast<uint8_t>(m_config.maxPlayers);
                beat.mode = match.mode;
                beat.name = m_config.name;
                beat.roomKey = match.roomKey;
                m_reporter->beat(now, beat);
            }
        }
        if (now - lastReport >= 30.0) {
            lastReport = now;
            report(now);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(m_peers.empty() ? 20 : 1));
    }
    log("shutting down");
    if (m_reporter) {
        m_reporter->farewell(m_transport.localPort());
    }
    if (m_hosts) {
        m_hosts->stopAll();
    }
    announce("The server is shutting down.");
    std::vector<uint8_t> none;
    sendAll(PacketType::Bye, none);
    m_transport.close();
    return true;
}

void Server::report(double now)
{
    const float left = m_sim->timeRemaining();
    std::string line = std::to_string(m_peers.size()) + " peer(s) connected, map " + currentDefinition().roomKey;
    static const char* phases[] = {"lobby", "starting", "in match", "results"};
    line += std::string(" (") + phases[std::min<int>(m_phase, 3)] + ")";
    if (left >= 0 && m_matchEndedAt < 0) {
        line += ", " + std::to_string(static_cast<int>(left)) + " s left";
    }
    if (m_transport.dropped() > 0) {
        line += ", " + std::to_string(m_transport.dropped()) + " packet(s) dropped";
    }
    log("%s", line.c_str());
    if (m_steps > 0) {
        log("sim: %s, %lld step(s), %.2f ms mean, %.1f ms worst, %lld overrun, %lld dropped", m_sim->describe().c_str(), m_steps,
            m_stepSeconds / m_steps * 1000, m_worstStep * 1000, m_overruns, m_dropped);
    }
    (void)now;
}

// ---- the match -----------------------------------------------------------------

RotationEntry Server::entryFor(const SessionState& definition) const
{
    RotationEntry entry;
    entry.roomKey = definition.roomKey;
    entry.mode = std::max(portMode(definition.mode), 0);
    entry.timeLimit = definition.timeLimit;
    entry.pointGoal = definition.pointGoal;
    const TeamLayout layout = resolveTeamLayout(definition.mode, definition.format, definition.customTeams);
    entry.teamCount = layout.teamCount >= 2 ? layout.teamCount : 2;
    return entry;
}

bool Server::startMatch(double now, const RotationEntry& entry)
{
    if (!m_sim->start(entry, currentDefinition().rule(SessionState::RuleFriendlyFire), layerPlayers())) {
        log("cannot run the match: the room \"%s\" would not load", entry.roomKey.c_str());
        return false;
    }
    m_simStarted = true;
    m_matchStarted = now;
    m_matchEndedAt = -1;
    if (!m_config.lobby) {
        m_phase = SessionState::PhaseInMatch;
    }
    m_frame = 0;
    m_lastAdvance = -1;
    m_accumulator = 0;
    m_lastSnapshot.clear();
    for (Peer& peer : m_peers) {
        peer.lastIntentFrame = 0;
        peer.postMatchReady = false;
    }
    syncOccupants();
    return true;
}

void Server::simulate(double now)
{
    // ServerSim.Advance: the steps the wall clock says are owed, at exactly 60 Hz.
    if (!m_simStarted || m_peers.empty()) {
        m_lastAdvance = -1;
        return;
    }
    if (m_lastAdvance < 0) {
        m_lastAdvance = now;
        return;
    }
    const double elapsed = now - m_lastAdvance;
    m_lastAdvance = now;
    if (elapsed > StallSeconds) {
        m_accumulator = 0; // a stall, not a slow pass
        return;
    }
    m_accumulator += elapsed;
    int steps = 0;
    std::array<uint8_t, MaxPacketSize> snapshot{};
    while (m_accumulator >= StepSeconds && steps < MaxCatchUpSteps) {
        m_accumulator -= StepSeconds;
        steps++;
        m_frame++;
        const double begin = clockNow();
        const size_t length = m_sim->step(m_frame, m_matchId, m_epoch, snapshot);
        const double cost = clockNow() - begin;
        m_steps++;
        m_stepSeconds += cost;
        m_worstStep = std::max(m_worstStep, cost);
        if (cost > StepSeconds) {
            m_overruns++;
        }
        if (length > 0) {
            m_lastSnapshot.assign(snapshot.begin(), snapshot.begin() + static_cast<long>(length));
            sendAll(PacketType::Snapshot, std::span<const uint8_t>(snapshot.data(), length));
        }
        sendVerdicts();
        if (m_matchEndedAt < 0 && m_sim->over()) {
            endMatch(now, "the match is over");
        }
    }
    if (m_accumulator >= StepSeconds) {
        m_dropped += static_cast<long long>(m_accumulator / StepSeconds);
        m_accumulator = 0;
    }
}

void Server::sendVerdicts()
{
    // HitVerdictPacket: a slot's answers, stamped with its occupant and life.
    for (const Peer& peer : m_peers) {
        const auto verdicts = m_sim->takeVerdicts(peer.slot);
        if (verdicts.empty()) {
            continue;
        }
        std::array<uint8_t, HitVerdict::HeaderSize + HitVerdict::MaxPerPacket * HitVerdict::EntrySize> buffer{};
        const size_t count = std::min<size_t>(verdicts.size(), HitVerdict::MaxPerPacket);
        buffer[0] = static_cast<uint8_t>(count);
        writeU16(buffer, 1, m_matchId);
        writeU64(buffer, 3, m_epoch);
        writeU16(buffer, 11, m_generations[peer.slot]);
        writeU16(buffer, 13, m_sim->lifeId(peer.slot));
        for (size_t i = 0; i < count; i++) {
            const size_t at = HitVerdict::HeaderSize + i * HitVerdict::EntrySize;
            writeU16(buffer, at, verdicts[i].first);
            buffer[at + 2] = verdicts[i].second;
        }
        send(peer.endpoint, PacketType::HitVerdict,
            std::span<const uint8_t>(buffer.data(), HitVerdict::HeaderSize + count * HitVerdict::EntrySize));
    }
}

double Server::endSequence() const
{
    // EndSequenceFor: the results stay up until everybody is ready, within limits.
    bool all = true;
    for (const Peer& peer : m_peers) {
        all = all && peer.postMatchReady;
    }
    return std::max(EndSequenceSeconds, m_peers.empty() || all ? AllReadySeconds : ReadyWaitSeconds);
}

void Server::endMatch(double now, const char* reason)
{
    if (m_matchEndedAt >= 0 || m_phase != SessionState::PhaseInMatch) {
        return;
    }
    m_matchEndedAt = now;
    for (Peer& peer : m_peers) {
        peer.postMatchReady = false;
    }
    m_phase = SessionState::PhasePostMatch;
    touchSession();
    openBallot();
    log("match over on %s (%s); %s in %.0f to %.0f s, as soon as everybody is ready%s", currentDefinition().roomKey.c_str(), reason,
        m_config.lobby ? "the lobby" : m_config.rotation.next().roomKey.c_str(), EndSequenceSeconds, ReadyWaitSeconds,
        m_ballotOpen ? "; ballot open" : "");
    broadcastMatchState(now);
    broadcastMapChoices();
}

bool Server::advanceMap(double now)
{
    const RotationEntry& entry = m_config.rotation.advance();
    m_matchId = nextSerial(m_matchId);
    m_phase = SessionState::PhaseInMatch;
    normalizeTeams();
    if (m_voteRunning) {
        m_voteRunning = false;
        m_voteResolvedAt = now;
        m_voteResult = VoteState::StateFailed;
        for (Peer& peer : m_peers) {
            peer.ballot = 0;
        }
    }
    closeBallot();
    broadcastMapChoices();
    log("rotating to %s (%s, %.1f min, %d pts)", entry.roomKey.c_str(), modeNames[entry.mode], entry.timeLimit / 60, entry.pointGoal);
    if (!startMatch(now, entry)) {
        return false;
    }
    touchSession();
    broadcastMatchState(now, PacketType::MapChange);
    return true;
}

void Server::touchSession()
{
    m_sessionRevision++;
    broadcastSessionState();
    broadcastRoster();
}

MatchState Server::buildMatchState(double now) const
{
    const SessionState entry = currentDefinition();
    const bool ending = m_matchEndedAt >= 0;
    const bool waiting = m_phase == SessionState::PhaseLobby || m_phase == SessionState::PhaseStarting;
    MatchState state;
    state.mode = entry.mode;
    const float left = m_sim->timeRemaining();
    state.timeRemaining = waiting ? entry.timeLimit : ending || entry.timeLimit <= 0 || left < 0 ? 0 : left;
    state.timeElapsed = waiting ? 0 : static_cast<float>(now - m_matchStarted);
    state.playerCount = static_cast<uint8_t>(m_peers.size());
    state.flags = static_cast<uint8_t>((ending ? MatchState::FlagEnding : MatchState::FlagInProgress)
        | (entry.rule(SessionState::RuleFriendlyFire) ? MatchState::FlagFriendlyFire : 0) | MatchState::ruleFlags(1, false));
    state.pointGoal = entry.pointGoal;
    state.matchId = m_matchId;
    state.authorityEpoch = m_epoch;
    state.roomKey = entry.roomKey;
    state.nextRoomKey = m_config.rotation.next().roomKey;
    return state;
}

SessionState Server::buildSessionState() const
{
    SessionState s = currentDefinition();
    s.phase = m_phase;
    s.policy = m_config.lobby ? SessionState::PolicyLobby : SessionState::PolicyContinuous;
    s.revision = m_sessionRevision;
    s.matchId = m_matchId;
    s.authorityEpoch = m_epoch;
    s.ownerSlot = 0xFF;
    for (const Peer& peer : m_peers) {
        if (m_ownerClientId != 0 && peer.clientId == m_ownerClientId) {
            s.ownerSlot = static_cast<uint8_t>(peer.slot);
        }
    }
    s.maxPlayers = static_cast<uint8_t>(m_config.maxPlayers);
    s.rules = static_cast<uint16_t>(s.rules | (m_config.requireReady ? SessionState::RuleRequireReady : 0)
        | (m_config.allowJoinInProgress ? SessionState::RuleAllowJoinInProgress : 0) | (m_lockTeams ? SessionState::RuleLockTeams : 0));
    s.expectedParticipants = m_expectedLoaded;
    s.loadedParticipants = m_loaded;
    // MatchWorldProfile.Resolve
    const TeamLayout layout = currentLayout();
    const bool exact = isTeamModeWire(s.mode) && s.format != SessionState::FormatAuto;
    const int players = std::clamp(exact ? layout.totalPlayers() : m_config.maxPlayers, 2, 8);
    s.entityLayerPlayers = static_cast<uint8_t>(std::min(players, 4));
    s.resources = players == 2 ? 0 : players <= 4 ? 1 : 2;
    return s;
}

Roster Server::buildRoster()
{
    Roster roster;
    roster.matchId = m_matchId;
    roster.authorityEpoch = m_epoch;
    roster.revision = ++m_rosterRevision;
    roster.sessionRevision = m_sessionRevision;
    for (const Peer& peer : m_peers) {
        if (roster.count >= SlotCapacity) {
            break;
        }
        Roster::Entry& e = roster.entries[roster.count++];
        e.slot = static_cast<uint8_t>(peer.slot);
        e.generation = m_generations[peer.slot];
        e.team = peer.team;
        e.hunter = peer.hunter;
        e.color = peer.color;
        e.ping = static_cast<uint16_t>(std::clamp(peer.ping, 0, 9999));
        e.lobbyReady = peer.lobbyReady;
        e.name = peer.name.empty() ? "Player" + std::to_string(peer.slot + 1) : peer.name;
    }
    return roster;
}

void Server::sendAll(PacketType type, std::span<const uint8_t> payload, const Peer* except)
{
    for (const Peer& peer : m_peers) {
        if (&peer != except) {
            send(peer.endpoint, type, payload);
        }
    }
}

void Server::broadcastMatchState(double now, PacketType type)
{
    std::array<uint8_t, MatchState::Size> buffer{};
    buildMatchState(now).write(buffer);
    sendAll(type, buffer);
}

void Server::broadcastSessionState()
{
    std::array<uint8_t, SessionState::Size> buffer{};
    buildSessionState().write(buffer);
    sendAll(PacketType::SessionState, buffer);
}

void Server::broadcastRoster()
{
    std::array<uint8_t, Roster::Size> buffer{};
    buildRoster().write(buffer);
    sendAll(PacketType::Roster, buffer);
}

void Server::syncOccupants()
{
    std::array<Occupant, SlotCapacity> occupants{};
    for (const Peer& peer : m_peers) {
        Occupant& o = occupants[peer.slot];
        o.occupied = true;
        o.hunter = peer.hunter;
        o.color = peer.color;
        o.team = peer.team;
        o.generation = m_generations[peer.slot];
        o.name = peer.name.empty() ? "Player" + std::to_string(peer.slot + 1) : peer.name;
    }
    m_sim->setOccupants(occupants);
}

void Server::pingPeers(double now)
{
    for (Peer& peer : m_peers) {
        if (peer.pingPending && now - peer.pingSentAt < 5) {
            continue; // still waiting on the last one
        }
        peer.pingId++;
        peer.pingSentAt = now;
        peer.pingPending = true;
        send(peer.endpoint, PacketType::Ping, std::span<const uint8_t>(&peer.pingId, 1));
    }
}

void Server::announce(const std::string& text)
{
    Chat chat;
    chat.slot = 0xFF;
    chat.kind = Chat::KindSystem;
    chat.text = text;
    std::array<uint8_t, Chat::Size> buffer{};
    chat.write(buffer);
    sendAll(PacketType::Chat, buffer);
}

// ---- peers ---------------------------------------------------------------------

Server::Peer* Server::find(const Endpoint& endpoint)
{
    for (Peer& peer : m_peers) {
        if (peer.endpoint == endpoint) {
            return &peer;
        }
    }
    return nullptr;
}

bool Server::slotFree(int slot) const
{
    return std::none_of(m_peers.begin(), m_peers.end(), [slot](const Peer& p) { return p.slot == slot; });
}

int Server::nextFreeSlot() const
{
    for (int slot = 0; slot < m_config.maxPlayers; slot++) {
        if (slotFree(slot)) {
            return slot;
        }
    }
    return -1;
}

int8_t Server::chooseTeam(const SessionState& match, const Peer* exclude) const
{
    // TeamRules.ChooseTeam: the emptiest team for its size in the match's layout.
    const TeamLayout layout = resolveTeamLayout(match.mode, match.format, match.customTeams);
    std::array<int, 4> counts{};
    for (const Peer& peer : m_peers) {
        if (&peer != exclude && peer.team >= 0 && peer.team < layout.teamCount) {
            counts[peer.team]++;
        }
    }
    return static_cast<int8_t>(net::chooseTeam(layout, counts));
}

void Server::normalizeTeams()
{
    for (Peer& peer : m_peers) {
        peer.team = -1;
    }
    const SessionState match = currentDefinition();
    for (Peer& peer : m_peers) {
        peer.team = chooseTeam(match, &peer);
    }
}

void Server::sendRefusal(const Endpoint& to, uint8_t reason)
{
    Refused refused;
    refused.reason = reason;
    refused.players = static_cast<uint8_t>(m_peers.size());
    refused.maxPlayers = static_cast<uint8_t>(m_config.maxPlayers);
    std::array<uint8_t, Refused::Size> buffer{};
    refused.write(buffer);
    send(to, PacketType::Refused, buffer);
}

void Server::remove(size_t index, const std::string& reason)
{
    const Peer peer = m_peers[index];
    m_peers.erase(m_peers.begin() + static_cast<long>(index));
    m_sessionRevision++;
    syncOccupants();
    log("%s %s (slot %d)", peer.endpoint.toString().c_str(), reason.c_str(), peer.slot);
    if (!peer.name.empty()) {
        announce(peer.name + " " + reason);
    }
    if (m_config.lobby) {
        lobbyPeerRemoved(peer, m_now);
    }
    reviewVote(m_now);
    reviewPicks();
    broadcastSessionState();
    broadcastRoster();
}

void Server::dropTimedOut(double now)
{
    for (size_t i = m_peers.size(); i-- > 0;) {
        if (now - m_peers[i].lastSeen > TimeoutSeconds) {
            remove(i, "timed out");
        }
    }
}

void Server::handle(const ReceivedPacket& packet, double now)
{
    if (packet.data.empty()) {
        return;
    }
    const std::span<const uint8_t> p(packet.data.data() + 1, packet.data.size() - 1);
    const auto type = static_cast<PacketType>(packet.data[0]);
    if (type == PacketType::Hello) {
        handleHello(packet.sender, p, now);
        return;
    }
    if (type == PacketType::StatusQuery) {
        sendStatus(packet.sender, now);
        return;
    }
    if (type == static_cast<PacketType>(PacketHostRequest)) {
        HostReply reply;
        const auto request = HostRequest::read(p);
        if (!request) {
            reply.reason = "malformed request";
        } else if (request->protocol != ProtocolVersion) {
            reply.reason = "this server speaks protocol " + std::to_string(ProtocolVersion);
        } else if (!m_hosts) {
            reply.reason = "this server does not open games for other players";
        } else {
            reply = m_hosts->start(*request, packet.sender);
        }
        std::array<uint8_t, HostReply::Size> buffer{};
        reply.write(buffer);
        send(packet.sender, static_cast<PacketType>(PacketHostReply), buffer);
        return;
    }
    Peer* peer = find(packet.sender);
    if (peer == nullptr) {
        return;
    }
    switch (type) {
    case PacketType::Identify:
        peer->lastSeen = now;
        handleIdentify(*peer, p);
        break;
    case PacketType::Intent:
        peer->lastSeen = now;
        handleIntent(*peer, p);
        break;
    case PacketType::Chat:
        peer->lastSeen = now;
        handleChat(*peer, p, now);
        break;
    case PacketType::Pong:
        handlePong(*peer, p, now);
        break;
    case PacketType::HitClaim:
        peer->lastSeen = now;
        if (m_simStarted && peer->slot >= 0) {
            m_sim->hitClaims(peer->slot, p);
        }
        break;
    case PacketType::Vote:
        peer->lastSeen = now;
        handleVote(*peer, p, now);
        break;
    case PacketType::MapPick:
        peer->lastSeen = now;
        handleMapPick(*peer, p);
        break;
    case PacketType::LobbyCommand:
        peer->lastSeen = now;
        handleLobbyCommand(*peer, p, now);
        break;
    case PacketType::MatchLoaded:
        peer->lastSeen = now;
        handleMatchLoaded(*peer, p, now);
        break;
    case PacketType::MatchLoadFailed:
        if (p.size() == static_cast<size_t>(MatchLoadFailedSize) && readU16(p, 0) == m_matchId) {
            remove(static_cast<size_t>(peer - m_peers.data()), "could not load the match: " + readText(p.subspan(2, 96)));
        }
        break;
    case PacketType::Bye:
        remove(static_cast<size_t>(peer - m_peers.data()), "left");
        break;
    default:
        // Snapshots and match ends from a client (this server runs the match),
        // votes, map picks, hit claims and the lobby: not handled.
        peer->lastSeen = now;
        break;
    }
}

void Server::handleHello(const Endpoint& from, std::span<const uint8_t> p, double now)
{
    if (p.empty() || p[0] != ProtocolVersion) {
        log("rejected %s: protocol mismatch", from.toString().c_str());
        sendRefusal(from, Refused::ReasonProtocol);
        return;
    }
    const uint32_t clientId = p.size() >= 6 ? readU32(p, 2) : 0;
    Peer* peer = find(from);
    if (peer != nullptr && peer->clientId != clientId) {
        remove(static_cast<size_t>(peer - m_peers.data()), "replaced connection");
        peer = nullptr;
    }
    if (peer == nullptr && clientId != 0) {
        // The same player from another address (a new NAT binding): the slot is kept.
        for (Peer& other : m_peers) {
            if (other.clientId == clientId) {
                log("slot %d (%s) came back on %s, was %s", other.slot, other.name.c_str(), from.toString().c_str(),
                    other.endpoint.toString().c_str());
                other.endpoint = from;
                peer = &other;
                break;
            }
        }
    }
    if (peer == nullptr) {
        int slot = -1;
        if (p.size() >= 2 && p[1] != 0xFF && p[1] < m_config.maxPlayers && slotFree(p[1])) {
            slot = p[1]; // the slot it held before this server dropped it
        }
        if (slot < 0) {
            slot = nextFreeSlot();
        }
        if (slot < 0) {
            log("rejected %s: session full", from.toString().c_str());
            sendRefusal(from, Refused::ReasonFull);
            return;
        }
        if (m_peers.empty() && !m_config.lobby) {
            // The first arrival on an empty server gets a fresh match, not the
            // tail of one that ran with nobody there.
            m_matchId = nextSerial(m_matchId);
            closeBallot();
            if (!startMatch(now, m_config.rotation.current())) {
                m_running = false;
                return;
            }
        }
        if (m_phase == SessionState::PhaseInMatch && !m_config.allowJoinInProgress) {
            sendRefusal(from, Refused::ReasonInMatch);
            return;
        }
        const int8_t team = chooseTeam(currentDefinition(), nullptr);
        if (currentLayout().teamCount > 0 && team < 0) {
            sendRefusal(from, Refused::ReasonFull);
            return;
        }
        m_generations[slot] = nextSerial(m_generations[slot]);
        Peer fresh;
        fresh.endpoint = from;
        fresh.slot = slot;
        fresh.clientId = clientId;
        fresh.lastSeen = now;
        fresh.chatCreditAt = now;
        fresh.team = team;
        m_peers.push_back(fresh);
        peer = &m_peers.back();
        log("%s joined as slot %d", from.toString().c_str(), slot);
        if (!m_lastSnapshot.empty()) {
            send(from, PacketType::Snapshot, m_lastSnapshot); // a world to stand in until the next one
        }
        syncOccupants();
    }
    peer->clientId = clientId;
    peer->lastSeen = now;
    claimOwner(*peer, p);
    // Answered on every Hello: the first Welcome may have been lost.
    std::array<uint8_t, 17> welcome{};
    welcome[0] = static_cast<uint8_t>(peer->slot);
    writeU32(welcome, 1, clientId);
    writeU16(welcome, 5, m_matchId);
    writeU64(welcome, 7, m_epoch);
    writeU16(welcome, 15, m_generations[peer->slot]);
    send(from, PacketType::Welcome, welcome);
    std::array<uint8_t, MatchState::Size> state{};
    buildMatchState(now).write(state);
    send(from, PacketType::MatchState, state);
    touchSession();
}

void Server::handleIdentify(Peer& peer, std::span<const uint8_t> p)
{
    if (p.size() < 2 || p[0] >= 7 || p[1] > 3) {
        return;
    }
    std::string name;
    for (size_t i = 2; i < p.size() && p[i] != 0; i++) {
        name.push_back(p[i] < 32 || p[i] > 126 ? '?' : static_cast<char>(p[i]));
    }
    name = trim(name).substr(0, Roster::MaxNameBytes);
    if (name.empty() || (peer.name == name && peer.hunter == p[0] && peer.color == p[1])) {
        return;
    }
    const bool firstName = peer.name.empty();
    peer.name = name;
    peer.hunter = p[0];
    peer.color = p[1];
    log("slot %d is \"%s\" playing hunter %d in suit %d", peer.slot, name.c_str(), peer.hunter, peer.color + 1);
    if (firstName) {
        announce(name + " joined");
    }
    syncOccupants();
    touchSession();
}

void Server::handleIntent(Peer& peer, std::span<const uint8_t> p)
{
    if (p.size() < static_cast<size_t>(Intent::Size) || p.size() > static_cast<size_t>(Intent::FullSize)) {
        return;
    }
    const Intent intent = Intent::read(p);
    if (intent.matchId != m_matchId || intent.authorityEpoch != m_epoch || intent.slotGeneration != m_generations[peer.slot]
        || intent.lifeId != m_sim->lifeId(peer.slot)) {
        return; // another match, occupant or life
    }
    m_sim->intent(peer.slot, intent);
    if (peer.lastIntentFrame != 0 && !newer32(intent.frame, peer.lastIntentFrame)) {
        return; // UDP reordered it: not relayed
    }
    peer.lastIntentFrame = intent.frame;
    peer.postMatchReady = (intent.buttons & Buttons::ReadyState) != 0;
    // To everybody else, tagged with the sender's slot: input is what makes a
    // player do what a position cannot say -- fire, morph, bomb.
    std::array<uint8_t, 1 + Intent::FullSize> relay{};
    relay[0] = static_cast<uint8_t>(peer.slot);
    std::copy(p.begin(), p.end(), relay.begin() + 1);
    sendAll(PacketType::SlotIntent, std::span<const uint8_t>(relay.data(), p.size() + 1), &peer);
}

void Server::handleChat(Peer& peer, std::span<const uint8_t> p, double now)
{
    if (p.size() < static_cast<size_t>(Chat::Size)) {
        return;
    }
    Chat chat = Chat::read(p);
    if (chat.text.empty()) {
        return;
    }
    // A leaky bucket: two quick lines pass, a hundred do not.
    peer.chatCredit = std::min(ChatBurst, peer.chatCredit + (now - peer.chatCreditAt) * ChatRatePerSecond);
    peer.chatCreditAt = now;
    if (peer.chatCredit < 1) {
        if (peer.chatDropped++ == 0) {
            log("chat from slot %d dropped: too fast", peer.slot);
        }
        return;
    }
    peer.chatCredit -= 1;
    peer.chatDropped = 0;
    chat.slot = static_cast<uint8_t>(peer.slot);
    chat.name = peer.name.empty() ? "Player" + std::to_string(peer.slot) : peer.name;
    const bool teamOnly = chat.kind == Chat::KindTeam && teamMode() && peer.team >= 0;
    chat.kind = teamOnly ? Chat::KindTeam : Chat::KindSay;
    std::array<uint8_t, Chat::Size> buffer{};
    chat.write(buffer);
    for (const Peer& other : m_peers) {
        if (&other != &peer && (!teamOnly || other.team == peer.team)) {
            send(other.endpoint, PacketType::Chat, buffer);
        }
    }
    log("chat %s: %s", chat.name.c_str(), chat.text.c_str());
}

void Server::handlePong(Peer& peer, std::span<const uint8_t> p, double now)
{
    if (!peer.pingPending || p.empty() || p[0] != peer.pingId) {
        return; // a late answer to an earlier ping
    }
    peer.pingPending = false;
    peer.lastSeen = now;
    const int rtt = std::clamp(static_cast<int>(std::lround((now - peer.pingSentAt) * 1000)), 0, 9999);
    peer.ping = peer.ping == 0 ? rtt : (peer.ping * 2 + rtt) / 3;
    m_sim->setPing(peer.slot, peer.ping);
}

void Server::sendStatus(const Endpoint& to, double now)
{
    ServerStatus status;
    status.match = buildMatchState(now);
    status.maxPlayers = static_cast<uint8_t>(m_config.maxPlayers);
    status.protocol = ProtocolVersion;
    status.name = m_config.name;
    status.phase = m_phase;
    status.format = currentDefinition().format;
    status.lobbyEnabled = m_config.lobby;
    status.flags = m_hosts ? 1 : 0; // ServerStatusFlags.CanHost: it opens games for other players
    status.allowJoinInProgress = m_config.allowJoinInProgress;
    std::array<uint8_t, ServerStatus::SizeWithFlags> buffer{};
    status.write(buffer);
    send(to, PacketType::StatusReply, buffer);
}

} // namespace fp::net
