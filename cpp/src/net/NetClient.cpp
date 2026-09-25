#include "NetClient.h"

#include <QtGlobal>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <random>
#include <thread>

namespace fp::net {

namespace {

// Seconds of silence from the server before saying hello again (NetSession.SilenceBeforeRejoin).
constexpr double SilenceBeforeRejoin = 5.0;

bool sane(const Vec3& v)
{
    for (float c : v) {
        if (!std::isfinite(c) || std::fabs(c) >= 100000) {
            return false;
        }
    }
    return true;
}

LifecycleTracker::State stateOf(const PlayerState& s)
{
    using S = LifecycleTracker::State;
    return (s.flags & PlayerState::FlagSpectating) ? S::Spectating : s.lifeId == 0 ? S::WaitingToSpawn : s.health == 0 ? S::Dead : S::Alive;
}

} // namespace

// ---- LifecycleTracker ----------------------------------------------------------

void LifecycleTracker::setOccupant(uint16_t generation)
{
    m_generation = generation;
    resetLife();
}

void LifecycleTracker::resetLife()
{
    m_lifeId = 0;
    m_state = m_generation == 0 ? State::Empty : State::WaitingToSpawn;
    m_dead = false;
}

bool LifecycleTracker::accept(uint16_t generation, uint16_t life, State state, bool& newLife)
{
    newLife = false;
    if (generation == 0 || generation != m_generation) {
        return false;
    }
    if (life != m_lifeId && (life == 0 || (m_lifeId != 0 && !newer16(life, m_lifeId)))) {
        return false; // an older life
    }
    if (life == 0 && state != State::WaitingToSpawn && state != State::Spectating) {
        return false;
    }
    if (life == m_lifeId && m_dead && state == State::Alive) {
        return false; // the dead do not stand up in the same life
    }
    newLife = life != m_lifeId;
    if (newLife) {
        m_lifeId = life;
        m_dead = false;
    }
    m_dead = m_dead || state == State::Dead;
    m_state = state;
    return true;
}

// ---- Smoothing -----------------------------------------------------------------

void Smoothing::reset()
{
    m_cells = {};
    m_stamp = {};
    m_newest = 0;
    m_readFrame = 0;
    m_running = false;
    m_sinceStarved = 0;
    m_sinceGrew = 30;
    m_delay = MinDelay;
}

void Smoothing::record(uint32_t frame, const std::vector<PlayerState>& states)
{
    if (frame == 0 || (m_running && !newer32(frame, m_newest))) {
        return;
    }
    const size_t index = frame % HistoryFrames;
    m_stamp[index] = frame;
    for (auto& slot : m_cells) {
        slot[index].live = false;
    }
    for (const PlayerState& s : states) {
        if (s.slot >= SlotCapacity) {
            continue;
        }
        Cell& cell = m_cells[s.slot][index];
        cell.live = (s.flags & PlayerState::FlagActive) && (s.flags & PlayerState::FlagSpawned) && s.health > 0;
        cell.life = s.lifeId;
        cell.generation = s.slotGeneration;
        cell.position = s.position;
        cell.alt = (s.flags & PlayerState::FlagAltForm) != 0;
    }
    if (!m_running || frame > m_newest) {
        m_newest = frame;
    }
    if (!m_running) {
        m_running = true;
        m_readFrame = frame > static_cast<uint32_t>(m_delay) ? frame - m_delay : frame;
    }
}

void Smoothing::tick()
{
    if (!m_running) {
        return;
    }
    // Advanced a frame, then steered a twentieth of the way onto newest - delay;
    // snapped when ten frames off (a stall, a rotation).
    m_readFrame += 1.0;
    const double target = static_cast<double>(m_newest) - m_delay;
    const double error = target - m_readFrame;
    if (std::fabs(error) > 10.0) {
        m_readFrame = target;
    } else {
        m_readFrame += error * 0.05;
    }
    if (m_readFrame > m_newest) {
        // Past everything that has arrived: held, never extrapolated.
        m_readFrame = m_newest;
        m_starved++;
        m_sinceStarved = 0;
        if (m_delay < MaxDelay && m_sinceGrew >= 30) {
            m_delay++;
            m_sinceGrew = 0;
        }
    } else if (++m_sinceStarved > 240 && m_delay > MinDelay) {
        m_delay--;
        m_sinceStarved = 0;
    }
    m_sinceGrew++;
}

bool Smoothing::lookup(int slot, uint32_t frame, const std::array<LifecycleTracker, SlotCapacity>& lives, Vec3& position, bool& alt) const
{
    if (frame == 0 || frame > m_newest) {
        return false;
    }
    const size_t index = frame % HistoryFrames;
    const Cell& cell = m_cells[slot][index];
    if (m_stamp[index] != frame || !cell.live || cell.generation == 0 || lives[slot].generation() != cell.generation
        || lives[slot].lifeId() != cell.life) {
        return false;
    }
    position = cell.position;
    alt = cell.alt;
    return sane(position);
}

bool Smoothing::sample(int slot, const std::array<LifecycleTracker, SlotCapacity>& lives, Vec3& position, bool& altForm) const
{
    if (!m_running || slot < 0 || slot >= SlotCapacity) {
        return false;
    }
    const auto lower = static_cast<uint32_t>(std::floor(m_readFrame));
    const float fraction = static_cast<float>(m_readFrame - lower);
    Vec3 a, b;
    bool altA = false, altB = false;
    if (!lookup(slot, lower, lives, a, altA)) {
        return false;
    }
    altForm = altA;
    position = a;
    if (fraction <= 0.0001f || !lookup(slot, lower + 1, lives, b, altB) || altA != altB) {
        return true; // nothing beyond, or a change of form: hold the near one
    }
    const Vec3 travel = b - a;
    if (dot(travel, travel) > 4.0f * 4.0f) {
        return true; // a teleport or a respawn, not something to slide across
    }
    position = a + travel * fraction;
    return true;
}

bool Smoothing::ackPoint(uint32_t& frame, uint8_t& subFrame) const
{
    if (!m_running) {
        return false;
    }
    const auto lower = static_cast<uint32_t>(std::floor(m_readFrame));
    if (lower == 0) {
        return false;
    }
    frame = lower;
    subFrame = static_cast<uint8_t>(std::clamp(static_cast<int>((m_readFrame - lower) * 256.0), 0, 255));
    return true;
}

// ---- Client --------------------------------------------------------------------

Client::Client()
{
    // Who this client is for as long as the program runs, so a reconnection
    // from a new address keeps its slot. Zero means "did not say".
    std::random_device random;
    m_clientId = random();
    if (m_clientId == 0) {
        m_clientId = 1;
    }
}

Client::~Client() { disconnect(); }

double Client::clock() const
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool Client::connect(const std::string& host, uint16_t port, const std::string& name, int hunter, int color)
{
    disconnect();
    const std::optional<Endpoint> server = Endpoint::resolve(host, port);
    if (!server) {
        qWarning("[net] cannot resolve %s", host.c_str());
        return false;
    }
    if (!m_transport.open(0)) {
        qWarning("[net] %s", m_transport.error().c_str());
        return false;
    }
    m_server = *server;
    m_serverDescription = host + ":" + std::to_string(port);
    m_name = name.substr(0, Roster::MaxNameBytes);
    m_hunter = hunter;
    m_color = color;
    m_localSlot = -1;
    m_frame = 0;
    m_lastServerPacket = clock();
    qInfo("[net] joining %s (%s) as \"%s\"", m_serverDescription.c_str(), m_server.toString().c_str(), m_name.c_str());
    sendHello();
    sendIdentify();
    return true;
}

void Client::disconnect()
{
    m_recorder.reset();
    m_playback.reset();
    if (m_transport.isOpen()) {
        send(PacketType::Bye, {});
        m_transport.close();
    }
}

std::string Client::refusedReason() const { return m_refused ? m_refused->describe(m_serverDescription) : std::string(); }

void Client::sendHello()
{
    std::array<uint8_t, 22> hello{};
    hello[0] = ProtocolVersion;
    // The slot already held, so a reconnection gets it back.
    hello[1] = m_localSlot >= 0 ? static_cast<uint8_t>(m_localSlot) : 0xFF;
    writeU32(hello, 2, m_clientId);
    // 6..21: the lobby owner token (zero for none).
    std::copy(m_ownerToken.begin(), m_ownerToken.end(), hello.begin() + 6);
    send(PacketType::Hello, hello);
}

void Client::sendIdentify()
{
    std::vector<uint8_t> payload{static_cast<uint8_t>(m_hunter), static_cast<uint8_t>(std::clamp(m_color, 0, 3))};
    for (char c : m_name) {
        payload.push_back(static_cast<uint8_t>(c < 32 || c > 126 ? '?' : c));
    }
    send(PacketType::Identify, payload);
}

void Client::setIdentity(int hunter, int color)
{
    m_hunter = hunter;
    m_color = color;
    sendIdentify();
}

void Client::update()
{
    if (m_playback) {
        m_frame++;
        pumpPlayback();
        return;
    }
    if (!m_transport.isOpen()) {
        return;
    }
    m_frame++;
    for (const ReceivedPacket& packet : m_transport.drain()) {
        if (m_recorder && packet.sender == m_server) {
            record(packet.data);
        }
        handle(packet);
    }
    if (!m_transport.isOpen()) {
        return; // Bye
    }
    const double now = clock();
    if ((m_localSlot < 0 || m_reAnnounced) && m_frame % 60 == 0) {
        sendHello(); // still waiting to be admitted
        sendIdentify();
    } else if (m_frame % 60 == 0 && now - m_lastServerPacket > SilenceBeforeRejoin) {
        // The server has forgotten us (restarted, or dropped us while we loaded).
        m_reAnnounced = true;
        if (!m_connectionLost) {
            m_connectionLost = true;
            m_chat.push_back({0, Chat::KindSystem, "", "Connection lost, retrying..."});
        }
        qInfo("[net] no word from the server for %.1f s; saying hello again", now - m_lastServerPacket);
        sendHello();
        sendIdentify();
    } else if (m_frame % 120 == 0 && m_localSlot >= 0 && m_hasRoster && m_slots[m_localSlot].name != m_name) {
        sendIdentify(); // the one sent with the join was lost
    }
    pumpLobby(now);
}

void Client::applySessionState(const SessionState& state)
{
    // NetSession.ApplySessionState: never an older authority, match or revision.
    if (state.matchId == 0 || state.authorityEpoch == 0) {
        return;
    }
    if (m_match && (state.authorityEpoch < m_match->authorityEpoch
            || (state.authorityEpoch == m_match->authorityEpoch && state.matchId != m_match->matchId && !newer16(state.matchId, m_match->matchId)))) {
        return;
    }
    if (m_session) {
        if (state.authorityEpoch != m_session->authorityEpoch && state.authorityEpoch < m_session->authorityEpoch) {
            return;
        }
        if (state.authorityEpoch == m_session->authorityEpoch && state.revision != m_session->revision
            && !newer16(state.revision, m_session->revision)) {
            return;
        }
    }
    const bool newMatch = !m_session || m_session->matchId != state.matchId;
    m_session = state;
    if (state.entityLayerPlayers >= 2 && state.entityLayerPlayers <= 4) {
        m_entityLayerPlayers = state.entityLayerPlayers;
    }
    if (!m_match || m_match->matchId != state.matchId || m_match->authorityEpoch != state.authorityEpoch) {
        // A lobby says which match comes before any MatchState does.
        MatchState match;
        match.roomKey = state.roomKey;
        match.mode = state.mode;
        match.authorityEpoch = state.authorityEpoch;
        match.pointGoal = state.pointGoal;
        match.timeRemaining = state.timeLimit;
        match.matchId = state.matchId;
        match.flags = static_cast<uint8_t>(MatchState::FlagInProgress | (state.rule(SessionState::RuleFriendlyFire) ? MatchState::FlagFriendlyFire : 0)
            | (state.rule(SessionState::RuleShadowFreeze) ? 0 : MatchState::FlagNoShadowFreeze)
            | MatchState::ruleFlags(1, state.rule(SessionState::RuleAffinityWeapons)));
        applyMatchState(match);
    }
    if (newMatch) {
        m_loadedMatch = 0;
    }
}

bool Client::shouldLoadMatch() const
{
    // NetSession.ShouldLoadMatch: a match is on, or starting with this player in it.
    if (!m_session) {
        return m_match.has_value();
    }
    return m_session->phase == SessionState::PhaseInMatch || m_session->phase == SessionState::PhasePostMatch
        || (m_session->phase == SessionState::PhaseStarting && m_localSlot >= 0
            && (m_session->expectedParticipants & (1 << m_localSlot)) != 0);
}

bool Client::sendLobbyCommand(uint8_t type, uint8_t targetSlot, int8_t team, bool ready, const SessionState* configuration)
{
    if (!m_transport.isOpen() || !m_session || m_pendingLobby) {
        return false;
    }
    PendingLobby pending;
    pending.command.commandId = ++m_nextCommandId == 0 ? ++m_nextCommandId : m_nextCommandId;
    pending.command.expectedRevision = m_session->revision;
    pending.command.type = type;
    pending.command.targetSlot = targetSlot;
    pending.command.teamIndex = team;
    pending.command.ready = ready;
    pending.command.configuration = configuration != nullptr ? *configuration : *m_session;
    pending.sentAt = clock();
    m_pendingLobby = pending;
    m_lobbyMessage = "Waiting for server...";
    std::array<uint8_t, LobbyCommand::Size> buffer{};
    pending.command.write(buffer);
    send(PacketType::LobbyCommand, buffer);
    return true;
}

void Client::pumpLobby(double now)
{
    // NetSession.PumpLobby: a command is repeated until it is answered, the
    // load barrier is told, and the identity kept fresh.
    if (m_pendingLobby && now - m_pendingLobby->sentAt >= std::min(1.0, 0.25 * (m_pendingLobby->attempts + 1))) {
        if (m_pendingLobby->attempts >= 4) {
            m_lobbyMessage = "The server did not acknowledge the command. Check the current lobby and try again.";
            m_pendingLobby.reset();
        } else {
            m_pendingLobby->attempts++;
            m_pendingLobby->sentAt = now;
            std::array<uint8_t, LobbyCommand::Size> buffer{};
            m_pendingLobby->command.write(buffer);
            send(PacketType::LobbyCommand, buffer);
        }
    }
    if (m_session && m_loadedMatch == m_session->matchId && m_session->phase == SessionState::PhaseStarting && now - m_lastLoadAck >= 0.25) {
        markMatchLoaded();
    }
    if (m_session && m_session->policy == SessionState::PolicyLobby && now - m_lastIdentity >= 1) {
        m_lastIdentity = now;
        sendIdentify();
    }
    // The results screen's pick, repeated in case one was lost.
    if (m_mapChoices && m_mapChoices->open && !m_mapPick.empty() && now - m_lastPickSent >= 1) {
        m_lastPickSent = now;
        std::array<uint8_t, MapPickSize> buffer{};
        writeText(buffer, m_mapPick);
        send(PacketType::MapPick, buffer);
    }
}

void Client::markMatchLoaded()
{
    if (!m_session || m_session->policy != SessionState::PolicyLobby) {
        return;
    }
    const double now = clock();
    if (m_loadedMatch == m_session->matchId && now - m_lastLoadAck < 0.25) {
        return;
    }
    m_loadedMatch = m_session->matchId;
    m_lastLoadAck = now;
    std::array<uint8_t, MatchLoadedSize> buffer{};
    writeU16(buffer, 0, m_loadedMatch);
    send(PacketType::MatchLoaded, buffer);
}

void Client::reportMatchLoadFailed(const std::string& reason)
{
    if (!m_session) {
        return;
    }
    std::array<uint8_t, MatchLoadFailedSize> buffer{};
    writeU16(buffer, 0, m_session->matchId);
    writeText(std::span<uint8_t>(buffer).subspan(2, 96), reason);
    send(PacketType::MatchLoadFailed, buffer);
}

void Client::sendVote(uint8_t kind, const std::string& roomKey)
{
    if (kind != Vote::KindPropose) {
        if (!m_vote || m_vote->state != VoteState::StateRunning || m_voteAnswered) {
            return;
        }
        m_voteAnswered = true;
    }
    Vote vote{kind, roomKey};
    std::array<uint8_t, Vote::Size> buffer{};
    vote.write(buffer);
    send(PacketType::Vote, buffer);
}

void Client::sendMapPick(const std::string& roomKey)
{
    m_mapPick = roomKey;
    m_lastPickSent = clock();
    std::array<uint8_t, MapPickSize> buffer{};
    writeText(buffer, roomKey);
    send(PacketType::MapPick, buffer);
}

void Client::handle(const ReceivedPacket& packet)
{
    if (packet.sender != m_server || packet.data.empty()) {
        return;
    }
    m_lastServerPacket = clock();
    if (m_connectionLost) {
        m_connectionLost = false;
        m_chat.push_back({0, Chat::KindSystem, "", "Reconnected."});
    }
    const std::span<const uint8_t> p(packet.data.data() + 1, packet.data.size() - 1);
    switch (static_cast<PacketType>(packet.data[0])) {
    case PacketType::Welcome:
        handleWelcome(p);
        break;
    case PacketType::Refused:
        if (!p.empty() && (m_localSlot < 0 || p[0] == Refused::ReasonKicked)) {
            m_refused = Refused::read(p);
            qWarning("[net] refused: %s", refusedReason().c_str());
        }
        break;
    case PacketType::Roster:
        handleRoster(p);
        break;
    case PacketType::MatchState:
    case PacketType::MapChange:
        handleMatchState(p);
        break;
    case PacketType::SessionState: {
        SessionState session;
        if (SessionState::tryRead(p, session)) {
            applySessionState(session);
        }
        break;
    }
    case PacketType::VoteState:
        if (p.size() >= static_cast<size_t>(VoteState::Size)) {
            const VoteState vote = VoteState::read(p);
            if (vote.state != VoteState::StateRunning || !m_vote || m_vote->roomKey != vote.roomKey
                || m_vote->state != VoteState::StateRunning) {
                m_voteAnswered = false; // a new question
            }
            m_vote = vote;
        }
        break;
    case PacketType::MapChoices:
        if (p.size() >= static_cast<size_t>(MapChoices::Size)) {
            m_mapChoices = MapChoices::read(p);
            if (!m_mapChoices->open) {
                m_mapPick.clear();
            }
        }
        break;
    case PacketType::LobbyCommandResult: {
        LobbyCommandResult result;
        if (LobbyCommandResult::tryRead(p, result) && m_pendingLobby && m_pendingLobby->command.commandId == result.commandId) {
            m_pendingLobby.reset();
            m_lobbyMessage = result.result == LobbyCommandResult::Ok ? std::string() : result.reason;
        }
        break;
    }
    case PacketType::Snapshot:
        handleSnapshot(p);
        break;
    case PacketType::SlotIntent:
        handleSlotIntent(p);
        break;
    case PacketType::Chat:
        if (p.size() >= static_cast<size_t>(Chat::Size)) {
            Chat chat = Chat::read(p);
            if (!chat.text.empty()) {
                m_chat.push_back(chat);
            }
        }
        break;
    case PacketType::HitVerdict:
        if (m_verdicts.size() < 64) {
            m_verdicts.emplace_back(p.begin(), p.end());
        }
        break;
    case PacketType::Bye:
        qInfo("[net] the server closed the session");
        m_chat.push_back({0, Chat::KindSystem, "", "The server closed the session."});
        m_transport.close();
        break;
    default:
        break; // Authority (never from a dedicated server that runs the match), votes, lobby: not yet
    }
}

void Client::handleWelcome(std::span<const uint8_t> p)
{
    if (p.size() != 17 || p[0] >= SlotCapacity || readU32(p, 1) != m_clientId) {
        return;
    }
    if (m_match && !matchesStream(readU16(p, 5), readU64(p, 7))) {
        return;
    }
    const int slot = p[0];
    const uint16_t generation = readU16(p, 15);
    const uint16_t current = m_lives[slot].generation();
    if (generation == 0 || (current != 0 && generation != current && !newer16(generation, current))) {
        return;
    }
    setOccupant(slot, generation);
    m_reAnnounced = false;
    if (m_localSlot != slot) {
        if (m_localSlot >= 0) {
            qInfo("[net] came back as slot %d, was slot %d", slot, m_localSlot);
        }
        m_localSlot = slot;
        qInfo("[net] joined as slot %d", slot);
    }
}

void Client::setOccupant(int slot, uint16_t generation)
{
    if (m_lives[slot].generation() == generation) {
        return;
    }
    forgetSlot(slot);
    m_lives[slot].setOccupant(generation);
    m_slotChanged[slot] = true;
}

void Client::forgetSlot(int slot)
{
    m_intentValid[slot] = false;
    m_intentArrived[slot] = 0;
    m_lastIntentFrame[slot] = 0;
    m_stateValid[slot] = false;
}

void Client::handleRoster(std::span<const uint8_t> p)
{
    Roster roster;
    if (!Roster::tryRead(p, roster) || !matchesStream(roster.matchId, roster.authorityEpoch)
        || (m_hasRoster && !newer32(roster.revision, m_rosterRevision))) {
        return;
    }
    for (int i = 0; i < roster.count; i++) {
        const Roster::Entry& e = roster.entries[i];
        const uint16_t previous = m_lives[e.slot].generation();
        if (e.generation == 0 || (previous != 0 && e.generation != previous && !newer16(e.generation, previous))) {
            return;
        }
    }
    m_hasRoster = true;
    m_rosterRevision = roster.revision;
    std::array<bool, SlotCapacity> present{};
    for (int i = 0; i < roster.count; i++) {
        const Roster::Entry& e = roster.entries[i];
        present[e.slot] = true;
        setOccupant(e.slot, e.generation);
        SlotInfo& info = m_slots[e.slot];
        info.occupied = true;
        info.hunter = e.hunter;
        info.color = e.color;
        info.team = e.team;
        info.ping = e.ping;
        info.name = e.name;
        info.lobbyReady = e.lobbyReady;
    }
    for (int slot = 0; slot < SlotCapacity; slot++) {
        if (!present[slot]) {
            m_slots[slot] = {};
            setOccupant(slot, 0);
        }
    }
}

void Client::handleMatchState(std::span<const uint8_t> p)
{
    if (p.size() < static_cast<size_t>(MatchState::Size)) {
        return;
    }
    applyMatchState(MatchState::read(p));
}

void Client::applyMatchState(const MatchState& state)
{
    if (state.matchId == 0 || state.authorityEpoch == 0) {
        return;
    }
    if (m_match) {
        const MatchState& previous = *m_match;
        if (state.authorityEpoch != previous.authorityEpoch && state.authorityEpoch < previous.authorityEpoch) {
            return; // an older authority
        }
        if (state.authorityEpoch == previous.authorityEpoch && state.matchId != previous.matchId && !newer16(state.matchId, previous.matchId)) {
            return; // an older match
        }
        if (state.authorityEpoch == previous.authorityEpoch && state.matchId == previous.matchId && previous.ending() && !state.ending()) {
            return; // a stale packet cannot reopen a finished round
        }
    }
    const bool newMatch = !m_match || state.matchId != m_match->matchId;
    const bool newEpoch = !m_match || state.authorityEpoch != m_match->authorityEpoch;
    const bool hadMatch = m_match.has_value();
    const std::string previousRoom = m_match ? m_match->roomKey : std::string();
    m_match = state;
    m_matchStateCount++;
    if (newMatch || newEpoch) {
        m_hasSnapshot = false;
        m_lastSnapshotFrame = m_snapshotArrived = m_appliedSnapshotFrame = 0;
        m_hasRoster = false;
        m_lastIntentFrame = {};
        m_intentValid = {};
        m_stateValid = {};
        m_smoothing.reset();
        if (newEpoch && hadMatch) {
            // A new simulation restarts its occupant and life counters: rebuilt
            // from this epoch's roster and first snapshot.
            for (int slot = 0; slot < SlotCapacity; slot++) {
                setOccupant(slot, 0);
            }
            m_reAnnounced = true;
            sendHello();
        } else if (newMatch) {
            for (LifecycleTracker& life : m_lives) {
                life.resetLife();
            }
        }
        m_newMatch = true;
    }
    if (newMatch || previousRoom != state.roomKey) {
        qInfo("[net] server map: %s (mode %d, %.0f s left, match %u)", state.roomKey.c_str(), state.mode, state.timeRemaining,
            state.matchId);
    }
}

bool Client::acceptState(const PlayerState& state)
{
    const int slot = state.slot;
    if (slot >= SlotCapacity || !sane(state.position) || !sane(state.speed) || !sane(state.facing)) {
        return false;
    }
    if (state.health > 0 && (!(state.flags & PlayerState::FlagSpawned) || state.lifeId == 0)) {
        return false;
    }
    bool fresh = false;
    if (!m_lives[slot].accept(state.slotGeneration, state.lifeId, stateOf(state), fresh)) {
        m_refusedStates++;
        return false;
    }
    return true;
}

void Client::handleSnapshot(std::span<const uint8_t> p)
{
    if (p.size() < static_cast<size_t>(SnapshotHeader::Size)) {
        return;
    }
    const SnapshotHeader header = SnapshotHeader::read(p);
    const size_t timeOffset = SnapshotHeader::Size + static_cast<size_t>(header.playerCount) * PlayerState::Size;
    const size_t healthOffset = timeOffset + MatchTimeSyncSize;
    if (header.playerCount > SlotCapacity || healthOffset > p.size() || !matchesStream(header.matchId, header.authorityEpoch)) {
        return;
    }
    // NetMatchTimeSync.Validate and NetHealthSync.Validate: the whole tail, or nothing.
    for (size_t at = timeOffset; at < healthOffset; at += 4) {
        const float value = readF32(p, at);
        if (!std::isfinite(value) || value < -1) {
            return;
        }
    }
    const std::span<const uint8_t> health = p.subspan(healthOffset);
    if (health.size() < static_cast<size_t>(HealthSyncHeaderSize) || health[2] > HealthSyncMaxSpawns
        || health.size() != static_cast<size_t>(HealthSyncHeaderSize + health[2] * HealthSyncEntrySize) || readU16(health, 0) != matchId()) {
        return;
    }
    int occupied = 0;
    for (int i = 0; i < header.playerCount; i++) {
        const int slot = p[SnapshotHeader::Size + i * PlayerState::Size];
        if (slot >= SlotCapacity || (occupied & (1 << slot))) {
            return;
        }
        occupied |= 1 << slot;
    }
    if (m_hasSnapshot && !newer32(header.frame, m_lastSnapshotFrame)) {
        m_outOfOrder++;
        return;
    }
    m_hasSnapshot = true;
    m_lastSnapshotFrame = header.frame;
    m_snapshotArrived = std::max<uint32_t>(m_frame, 1);
    m_snapshotsReceived++;
    m_rng1 = header.rng1;
    m_rng2 = header.rng2;
    m_stateValid = {};
    std::vector<PlayerState> accepted;
    for (int i = 0; i < header.playerCount; i++) {
        const PlayerState state = PlayerState::read(p.subspan(SnapshotHeader::Size + i * PlayerState::Size));
        if (acceptState(state)) {
            m_states[state.slot] = state;
            m_stateValid[state.slot] = true;
            accepted.push_back(state);
        }
    }
    m_smoothing.record(header.frame, accepted);
    for (int i = 0; i < SlotCapacity; i++) {
        m_time[i] = readF32(p, timeOffset + i * 8);
        m_teamTime[i] = readF32(p, timeOffset + i * 8 + 4);
    }
    m_healthSpawns.clear();
    for (size_t at = HealthSyncHeaderSize; at < health.size(); at += HealthSyncEntrySize) {
        const uint8_t flags = health[at + 2];
        HealthSpawnState s;
        s.id = readI16(health, at);
        s.available = flags & 1;
        s.active = flags & 2;
        s.pickerSlot = static_cast<int8_t>(((flags >> 2) & 0xF) - 1);
        s.cooldown = readU16(health, at + 3);
        s.spawnCount = readU16(health, at + 5);
        m_healthSpawns.push_back(s);
    }
}

void Client::handleSlotIntent(std::span<const uint8_t> p)
{
    if (p.size() < static_cast<size_t>(1 + Intent::Size)) {
        return;
    }
    const int slot = p[0];
    if (slot >= SlotCapacity || slot == m_localSlot) {
        return;
    }
    const Intent intent = Intent::read(p.subspan(1));
    // Identity before ordering: another occupant's or an older life's input is refused.
    if (!matchesStream(intent.matchId, intent.authorityEpoch) || intent.slotGeneration != m_lives[slot].generation() || intent.lifeId == 0
        || intent.lifeId != m_lives[slot].lifeId()) {
        return;
    }
    if (m_lastIntentFrame[slot] != 0 && !newer32(intent.frame, m_lastIntentFrame[slot])) {
        m_outOfOrder++;
        return;
    }
    m_lastIntentFrame[slot] = intent.frame;
    m_intents[slot] = intent;
    m_intentValid[slot] = true;
    m_intentArrived[slot] = std::max<uint32_t>(m_frame, 1);
}

void Client::sendIntent(Intent intent)
{
    if (!m_transport.isOpen() || m_localSlot < 0) {
        return;
    }
    intent.frame = m_frame;
    intent.matchId = matchId();
    intent.authorityEpoch = authorityEpoch();
    intent.slotGeneration = m_lives[m_localSlot].generation();
    intent.lifeId = m_lives[m_localSlot].lifeId();
    intent.ackFrame = m_appliedSnapshotFrame != 0 ? m_appliedSnapshotFrame : m_lastSnapshotFrame;
    intent.ackSubFrame = 0;
    uint32_t readFrame;
    uint8_t readSub;
    if (m_smoothing.ackPoint(readFrame, readSub)) {
        intent.ackFrame = readFrame;
        intent.ackSubFrame = readSub;
    }
    intent.hasState = true;
    std::array<uint8_t, Intent::FullSize> buffer{};
    intent.write(buffer);
    send(PacketType::Intent, buffer);
    m_intentsSent++;
    if (m_recorder) {
        // DemoRecorder.RecordOwnIntent: the server never relays this player's
        // own input back to it, so it goes in as the relay would have.
        std::array<uint8_t, 2 + Intent::FullSize> relayed{};
        relayed[0] = static_cast<uint8_t>(PacketType::SlotIntent);
        relayed[1] = static_cast<uint8_t>(m_localSlot);
        std::copy(buffer.begin(), buffer.end(), relayed.begin() + 2);
        record(relayed);
    }
}

bool Client::startRecording(const std::filesystem::path& path)
{
    if (m_playback || !m_transport.isOpen()) {
        return false;
    }
    m_recorder = DemoWriter::create(path);
    m_recordStart = m_frame;
    if (m_recorder) {
        qInfo("[demo] recording to %s", path.string().c_str());
    }
    return m_recorder != nullptr;
}

void Client::record(std::span<const uint8_t> packet)
{
    m_recorder->write(m_frame > m_recordStart ? m_frame - m_recordStart : 0, packet);
}

bool Client::openDemo(const std::filesystem::path& path, std::string& error)
{
    // DemoPlayback.Join: read until the match is known (and the roster has had
    // two seconds to arrive), then start again from the first frame with
    // what that taught kept -- the room, who is in which slot.
    auto reader = DemoReader::open(path);
    if (!reader) {
        error = "That file isn't a demo this build recognises (wrong extension, damaged, or from a different build).";
        return false;
    }
    if (reader->protocol() != ProtocolVersion) {
        error = "This demo uses an incompatible network protocol (" + std::to_string(reader->protocol()) + ", this build "
            + std::to_string(ProtocolVersion) + ").";
        return false;
    }
    disconnect();
    m_server = {0x7F000001, 1};
    m_serverDescription = "demo " + path.filename().string();
    m_localSlot = -1;
    m_frame = 0;
    m_playback = std::move(reader);
    m_playbackFrame = 0;
    m_playbackPending = m_playback->next();
    const bool hadRecords = m_playbackPending.has_value();
    long knownAt = -1;
    while (m_playbackFrame < 60 * 20) {
        m_frame++;
        pumpPlayback();
        if (m_match && !m_match->roomKey.empty()) {
            if (knownAt < 0) {
                knownAt = m_playbackFrame;
            } else if (m_playbackFrame - knownAt >= 120 || !m_playbackPending) {
                break;
            }
        } else if (!m_playbackPending) {
            break;
        }
    }
    if (!m_match || m_match->roomKey.empty()) {
        error = hadRecords ? "That demo has no match info in its first few seconds." : "That demo file is empty -- nothing was ever recorded to it.";
        m_playback.reset();
        return false;
    }
    // Rewind: the stream is replayed from the start into what was learned.
    m_playback = DemoReader::open(path);
    m_playbackFrame = 0;
    m_playbackPending = m_playback ? m_playback->next() : std::nullopt;
    m_frame = 0;
    m_hasRoster = false;
    m_rosterRevision = 0;
    for (LifecycleTracker& life : m_lives) {
        life.resetLife();
    }
    m_hasSnapshot = false;
    m_lastSnapshotFrame = m_snapshotArrived = m_appliedSnapshotFrame = 0;
    m_lastIntentFrame = {};
    m_intentValid = {};
    m_stateValid = {};
    m_smoothing.reset();
    m_snapshotsReceived = 0;
    m_newMatch = true;
    qInfo("[demo] playing %s: %s", path.string().c_str(), m_match->roomKey.c_str());
    return true;
}

void Client::pumpPlayback()
{
    // DemoPlayback.PumpFrame: one frame's worth of the recording per simulated frame.
    if (m_frame > 1) {
        m_playbackFrame++;
    }
    while (m_playbackPending && m_playbackPending->frame <= m_playbackFrame) {
        ReceivedPacket packet;
        packet.sender = m_server;
        packet.data = std::move(m_playbackPending->data);
        handle(packet);
        m_playbackPending = m_playback->next();
    }
}

void Client::sendChat(const std::string& text)
{
    if (!m_transport.isOpen() || text.empty()) {
        return;
    }
    Chat chat;
    chat.slot = static_cast<uint8_t>(std::max(m_localSlot, 0));
    chat.kind = Chat::KindSay;
    chat.name = m_name;
    chat.text = text;
    std::array<uint8_t, Chat::Size> buffer{};
    chat.write(buffer);
    send(PacketType::Chat, buffer);
}

std::optional<ServerStatus> queryStatus(const std::string& host, uint16_t port, int timeoutMs, int& latencyMs)
{
    const std::optional<Endpoint> server = Endpoint::resolve(host, port);
    Transport transport;
    if (!server || !transport.open(0)) {
        return std::nullopt;
    }
    const auto start = std::chrono::steady_clock::now();
    const uint8_t version = ProtocolVersion;
    transport.send(*server, static_cast<uint8_t>(PacketType::StatusQuery), {&version, 1});
    while (std::chrono::steady_clock::now() - start < std::chrono::milliseconds(timeoutMs)) {
        for (const ReceivedPacket& packet : transport.drain()) {
            if (packet.sender == *server && packet.data.size() >= 1 + static_cast<size_t>(ServerStatus::Size)
                && packet.data[0] == static_cast<uint8_t>(PacketType::StatusReply)) {
                latencyMs = static_cast<int>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
                return ServerStatus::read(std::span<const uint8_t>(packet.data).subspan(1));
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return std::nullopt;
}

} // namespace fp::net
