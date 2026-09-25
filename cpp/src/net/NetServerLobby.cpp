// DedicatedServer's votes and its persistent lobby (the C#'s LobbyCommands.cs
// and the voting half of DedicatedServer.cs).

#include "NetServer.h"

#include "formats/Rooms.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>

namespace fp::net {

namespace {

// Quake's rules for a mid-match vote: 70% of the players, 30 s to answer,
// 90 s between votes and 3 minutes between one player's proposals.
constexpr double VoteThreshold = 0.70, VoteSeconds = 30.0, VoteCooldownSeconds = 90.0, ProposalCooldownSeconds = 180.0;
constexpr int VoteMinimumPlayers = 2;
// How long a lobby start waits for everybody to load before playing without them.
constexpr double LoadWaitSeconds = 15.0;
constexpr const char* modeNames[] = {"Battle", "BattleTeams", "Survival", "SurvivalTeams", "Capture", "Bounty", "BountyTeams", "Nodes",
    "NodesTeams", "Defender", "DefenderTeams", "PrimeHunter"};

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

bool sameKey(const std::string& a, const std::string& b)
{
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) { return std::tolower(x) == std::tolower(y); });
}

// DedicatedServer.ResolveRoomKey: a multiplayer room's own key, whatever its case.
std::string resolveRoomKey(std::string key)
{
    key.erase(0, key.find_first_not_of(' '));
    key.erase(key.find_last_not_of(' ') + 1);
    for (const RoomMetadata& room : allRooms()) {
        if (room.multiplayer && !room.firstHunt && !room.hybrid && sameKey(room.name, key)) {
            return room.name;
        }
    }
    return {};
}

std::string displayName(const std::string& name, int slot) { return name.empty() ? "Player" + std::to_string(slot + 1) : name; }

} // namespace

// ---- the match definition ------------------------------------------------------

SessionState Server::definitionFor(const RotationEntry& entry) const
{
    SessionState d;
    d.roomKey = entry.roomKey;
    d.mode = static_cast<uint8_t>(wireMode(entry.mode));
    d.format = m_config.format;
    d.timeLimit = static_cast<uint16_t>(std::clamp(static_cast<int>(entry.timeLimit), 0, 0xFFFF));
    d.pointGoal = static_cast<uint16_t>(std::clamp(entry.pointGoal, 0, 0xFFFF));
    d.rules = static_cast<uint16_t>((m_config.friendlyFire ? SessionState::RuleFriendlyFire : 0) | SessionState::RuleShadowFreeze);
    if (!validateDefinition(d).empty()) {
        d.format = SessionState::FormatAuto;
    }
    return d;
}

SessionState Server::currentDefinition() const
{
    if (m_config.lobby) {
        return m_phase == SessionState::PhaseLobby ? m_lobbyMatch : m_frozenMatch;
    }
    return definitionFor(m_config.rotation.current());
}

TeamLayout Server::currentLayout() const
{
    const SessionState d = currentDefinition();
    return resolveTeamLayout(d.mode, d.format, d.customTeams);
}

// ---- votes -------------------------------------------------------------------------

void Server::tell(const Peer& peer, const std::string& text)
{
    Chat chat;
    chat.slot = 0xFF;
    chat.kind = Chat::KindSystem;
    chat.text = text;
    std::array<uint8_t, Chat::Size> buffer{};
    chat.write(buffer);
    send(peer.endpoint, PacketType::Chat, buffer);
}

void Server::handleVote(Peer& peer, std::span<const uint8_t> p, double now)
{
    if (p.size() < static_cast<size_t>(Vote::Size) || !m_config.allowMapVotes) {
        return;
    }
    const Vote vote = Vote::read(p);
    if (vote.kind == Vote::KindPropose) {
        startVote(peer, vote.roomKey, now);
        return;
    }
    if (!m_voteRunning || (vote.kind != Vote::KindYes && vote.kind != Vote::KindNo) || peer.ballot != 0) {
        return;
    }
    peer.ballot = vote.kind;
    broadcastVoteState(now);
    tally(now);
}

void Server::startVote(Peer& peer, const std::string& roomKey, double now)
{
    if (m_phase != SessionState::PhaseInMatch) {
        return;
    }
    if (m_voteRunning) {
        tell(peer, "a vote is already running");
        return;
    }
    if (static_cast<int>(m_peers.size()) < VoteMinimumPlayers) {
        tell(peer, "not enough players to hold a vote");
        return;
    }
    if (const double since = now - m_voteResolvedAt; since < VoteCooldownSeconds) {
        tell(peer, "another vote may be called in " + std::to_string(static_cast<int>(VoteCooldownSeconds - since)) + " s");
        return;
    }
    if (const double since = now - peer.lastProposal; since < ProposalCooldownSeconds) {
        tell(peer, "you may propose again in " + std::to_string(static_cast<int>(ProposalCooldownSeconds - since)) + " s");
        return;
    }
    const std::string resolved = resolveRoomKey(roomKey);
    if (resolved.empty()) {
        tell(peer, "no map called \"" + roomKey + "\"");
        return;
    }
    if (sameKey(resolved, currentDefinition().roomKey)) {
        tell(peer, "that is the map you are on");
        return;
    }
    m_voteRunning = true;
    m_voteRoom = resolved;
    m_voteMode = m_config.rotation.modeFor(resolved, std::max(portMode(currentDefinition().mode), 0));
    m_voteProposer = displayName(peer.name, peer.slot);
    m_voteStartedAt = now;
    m_voteResult = VoteState::StateIdle;
    peer.lastProposal = now;
    for (Peer& other : m_peers) {
        other.ballot = 0;
    }
    peer.ballot = Vote::KindYes;
    announce(m_voteProposer + " proposes " + resolved + " -- F1 to accept, F2 to deny");
    log("vote started by slot %d for %s (%s)", peer.slot, resolved.c_str(), modeNames[m_voteMode]);
    broadcastVoteState(now);
    tally(now);
}

void Server::countVotes(int& yes, int& no, int& eligible, int& needed) const
{
    yes = no = 0;
    for (const Peer& peer : m_peers) {
        yes += peer.ballot == Vote::KindYes;
        no += peer.ballot == Vote::KindNo;
    }
    eligible = static_cast<int>(m_peers.size());
    needed = std::max(1, static_cast<int>(std::ceil(eligible * VoteThreshold)));
}

void Server::tally(double now)
{
    if (!m_voteRunning) {
        return;
    }
    int yes, no, eligible, needed;
    countVotes(yes, no, eligible, needed);
    const std::string count = std::to_string(yes) + " of " + std::to_string(eligible);
    if (yes >= needed) {
        resolveVote(now, true, count);
    } else if (eligible - no < needed || now - m_voteStartedAt >= VoteSeconds) {
        resolveVote(now, false, count);
    }
}

void Server::resolveVote(double now, bool passed, const std::string& count)
{
    const std::string room = m_voteRoom;
    m_voteRunning = false;
    m_voteResolvedAt = now;
    m_voteResult = passed ? VoteState::StatePassed : VoteState::StateFailed;
    for (Peer& peer : m_peers) {
        peer.ballot = 0;
    }
    if (passed) {
        announce("vote passed (" + count + ") -- changing to " + room);
        log("vote passed (%s) for %s", count.c_str(), room.c_str());
        m_config.rotation.playNext(room, m_voteMode);
        if (m_config.lobby) {
            endMatch(now, "map vote passed");
        } else {
            advanceMap(now);
        }
    } else {
        announce("vote failed (" + count + ") -- staying on " + currentDefinition().roomKey);
        log("vote failed (%s) for %s", count.c_str(), room.c_str());
    }
    broadcastVoteState(now);
}

void Server::reviewVote(double now)
{
    if (!m_voteRunning) {
        return;
    }
    if (static_cast<int>(m_peers.size()) < VoteMinimumPlayers) {
        resolveVote(now, false, "not enough players");
        return;
    }
    tally(now);
}

void Server::broadcastVoteState(double now)
{
    if (m_peers.empty()) {
        return;
    }
    VoteState state;
    state.state = m_voteRunning ? VoteState::StateRunning : m_voteResult;
    if (m_voteRunning) {
        int yes, no, eligible, needed;
        countVotes(yes, no, eligible, needed);
        state.roomKey = m_voteRoom;
        state.proposer = m_voteProposer;
        state.yes = static_cast<uint8_t>(yes);
        state.no = static_cast<uint8_t>(no);
        state.eligible = static_cast<uint8_t>(eligible);
        state.needed = static_cast<uint8_t>(needed);
        state.seconds = static_cast<uint16_t>(std::max(0.0, VoteSeconds - (now - m_voteStartedAt)));
    } else if (m_config.allowMapVotes) {
        state.seconds = static_cast<uint16_t>(std::clamp(VoteCooldownSeconds - (now - m_voteResolvedAt), 0.0, 65535.0));
    } else {
        state.seconds = 0xFFFF;
    }
    std::array<uint8_t, VoteState::Size> buffer{};
    state.write(buffer);
    sendAll(PacketType::VoteState, buffer);
}

// ---- the results screen's ballot -----------------------------------------------

void Server::openBallot()
{
    m_ballotOpen = m_config.allowMapVotes;
    m_tally.clear();
    for (Peer& peer : m_peers) {
        peer.pick.clear();
    }
}

void Server::closeBallot()
{
    m_ballotOpen = false;
    m_tally.clear();
    for (Peer& peer : m_peers) {
        peer.pick.clear();
    }
}

void Server::handleMapPick(Peer& peer, std::span<const uint8_t> p)
{
    if (p.size() < static_cast<size_t>(MapPickSize) || !m_ballotOpen) {
        return;
    }
    std::string key = readText(p.subspan(0, MapPickSize));
    if (!key.empty()) {
        const std::string resolved = resolveRoomKey(key);
        if (resolved.empty() || sameKey(resolved, currentDefinition().roomKey)) {
            tell(peer, resolved.empty() ? "no map called \"" + key + "\"" : "that is the map you are on");
            return;
        }
        key = resolved;
    }
    if (sameKey(peer.pick, key)) {
        return;
    }
    const std::string was = peer.pick;
    peer.pick = key;
    recount();
    int votes = 0;
    for (const auto& [room, count] : m_tally) {
        if (sameKey(room, key)) {
            votes = count;
        }
    }
    if (!key.empty() && votes == 1) {
        announce(displayName(peer.name, peer.slot) + " wants " + key + " next -- pick it to agree; the map with the most votes is the one loaded");
    } else if (key.empty() && !was.empty()) {
        log("slot %d took back its pick of %s", peer.slot, was.c_str());
    }
    applyLeader();
    broadcastMapChoices();
}

void Server::recount()
{
    // Most votes first; ties in the order they were first picked.
    m_tally.clear();
    for (const Peer& peer : m_peers) {
        if (peer.pick.empty()) {
            continue;
        }
        auto it = std::find_if(m_tally.begin(), m_tally.end(), [&](const auto& entry) { return sameKey(entry.first, peer.pick); });
        if (it == m_tally.end()) {
            m_tally.emplace_back(peer.pick, 1);
        } else {
            it->second++;
        }
    }
    std::stable_sort(m_tally.begin(), m_tally.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
}

void Server::applyLeader()
{
    // The leader is the next map as the picks arrive, so NEXT is one fact.
    if (m_tally.empty() || m_tally[0].second <= 0) {
        m_config.rotation.clearPending();
        return;
    }
    m_config.rotation.playNext(m_tally[0].first, m_config.rotation.modeFor(m_tally[0].first, std::max(portMode(currentDefinition().mode), 0)));
}

void Server::reviewPicks()
{
    if (!m_ballotOpen) {
        return;
    }
    recount();
    applyLeader();
    broadcastMapChoices();
}

void Server::broadcastMapChoices()
{
    if (m_peers.empty()) {
        return;
    }
    MapChoices choices;
    choices.open = m_ballotOpen;
    choices.eligible = static_cast<uint8_t>(std::min<size_t>(m_peers.size(), 255));
    for (const auto& [room, votes] : m_tally) {
        if (choices.choices.size() < static_cast<size_t>(MapChoices::MaxChoices)) {
            choices.choices.emplace_back(room, static_cast<uint8_t>(std::clamp(votes, 0, 255)));
        }
    }
    std::array<uint8_t, MapChoices::Size> buffer{};
    choices.write(buffer);
    sendAll(PacketType::MapChoices, buffer);
}

// ---- the lobby -------------------------------------------------------------------

void Server::invalidateLobbyReady()
{
    for (Peer& peer : m_peers) {
        peer.lobbyReady = false;
    }
}

void Server::setPhase(uint8_t phase)
{
    static const char* names[] = {"Lobby", "Starting", "InMatch", "PostMatch"};
    log("[lobby] phase %s -> %s, match %u", names[std::min<int>(m_phase, 3)], names[std::min<int>(phase, 3)], m_matchId);
    m_phase = phase;
    touchSession();
}

void Server::claimOwner(Peer& peer, std::span<const uint8_t> hello)
{
    // The first client in owns the lobby -- or the one that presents the owner token, when there is one.
    if (!m_config.lobby || m_ownerClientId != 0 || peer.clientId == 0) {
        return;
    }
    const bool hasToken = std::any_of(m_config.ownerToken.begin(), m_config.ownerToken.end(), [](uint8_t b) { return b != 0; });
    if (hasToken && (hello.size() != 22 || !std::equal(m_config.ownerToken.begin(), m_config.ownerToken.end(), hello.begin() + 6))) {
        return;
    }
    m_ownerClientId = peer.clientId;
    m_config.ownerToken = {};
    log("[lobby] owner = slot %d", peer.slot);
    touchSession();
}

std::string Server::validateStart(const SessionState& match, bool requireReady, uint8_t& code) const
{
    // LobbyRules.Validate
    std::string reason = validateDefinition(match);
    if (!reason.empty()) {
        code = LobbyCommandResult::InvalidConfiguration;
        return reason;
    }
    const TeamLayout layout = resolveTeamLayout(match.mode, match.format, match.customTeams);
    const bool exact = isTeamModeWire(match.mode) && match.format != SessionState::FormatAuto;
    const int required = exact ? layout.totalPlayers() : match.format == SessionState::FormatFreeForAll ? 2 : 1;
    const int count = static_cast<int>(m_peers.size());
    if (count < required || (exact && count != required)) {
        code = LobbyCommandResult::NotEnoughPlayers;
        return exact ? layout.describe() + " requires exactly " + std::to_string(required) + " players."
                     : "At least " + std::to_string(required) + " players must join.";
    }
    std::array<int, 4> counts{};
    for (const Peer& peer : m_peers) {
        if (layout.teamCount > 0) {
            if (peer.team < 0 || peer.team >= layout.teamCount) {
                code = LobbyCommandResult::InvalidTeam;
                return "Every player needs a valid team.";
            }
            counts[peer.team]++;
        }
        if (requireReady && !peer.lobbyReady) {
            code = LobbyCommandResult::PlayersNotReady;
            return "Waiting for " + displayName(peer.name, peer.slot) + " to ready.";
        }
    }
    for (int team = 0; team < layout.teamCount; team++) {
        const int capacity = layout.capacity[team];
        if (counts[team] > capacity || (exact && counts[team] != capacity)) {
            code = LobbyCommandResult::InvalidTeam;
            return std::string("Team ") + static_cast<char>('A' + team) + " needs " + std::to_string(capacity) + " players (currently "
                + std::to_string(counts[team]) + ").";
        }
    }
    code = LobbyCommandResult::Ok;
    return "Ready to start.";
}

void Server::handleLobbyCommand(Peer& peer, std::span<const uint8_t> p, double now)
{
    LobbyCommand command;
    if (!LobbyCommand::tryRead(p, command)) {
        return;
    }
    const Endpoint who = peer.endpoint; // a kick moves the peers about
    LobbyCommandResult result;
    if (auto it = peer.commands.find(command.commandId); it != peer.commands.end()) {
        result = it->second; // a resend: the same answer
    } else {
        std::string reason;
        const uint8_t code = executeLobbyCommand(peer, command, reason, now);
        result.commandId = command.commandId;
        result.result = code;
        result.currentRevision = m_sessionRevision;
        result.reason = reason;
        Peer* still = find(who);
        if (still == nullptr) {
            return;
        }
        if (still->commands.size() >= 64) {
            still->commands.erase(still->commandOrder.front());
            still->commandOrder.pop_front();
        }
        still->commands.emplace(command.commandId, result);
        still->commandOrder.push_back(command.commandId);
        if (code != LobbyCommandResult::Ok) {
            log("[lobby] slot %d command %u denied: %s", still->slot, command.type, reason.c_str());
        }
    }
    std::array<uint8_t, LobbyCommandResult::Size> buffer{};
    result.write(buffer);
    send(who, PacketType::LobbyCommandResult, buffer);
    broadcastSessionState();
    broadcastRoster();
}

uint8_t Server::executeLobbyCommand(Peer& peer, const LobbyCommand& command, std::string& reason, double now)
{
    if (!m_config.lobby || m_phase != SessionState::PhaseLobby) {
        reason = "Wait until the server returns to the lobby.";
        return LobbyCommandResult::InvalidPhase;
    }
    const bool owner = peer.clientId != 0 && peer.clientId == m_ownerClientId;
    if (command.type != LobbyCommand::SetReady && command.type != LobbyCommand::SetTeam && !owner) {
        reason = "Only the lobby owner can do that.";
        return LobbyCommandResult::NotOwner;
    }
    if (command.expectedRevision != m_sessionRevision) {
        reason = "The lobby changed. Review the updated settings and try again.";
        return LobbyCommandResult::StaleRevision;
    }
    const int slot = peer.slot;
    switch (command.type) {
    case LobbyCommand::SetReady:
        peer.lobbyReady = command.ready;
        break;
    case LobbyCommand::SetTeam: {
        Peer* target = nullptr;
        for (Peer& other : m_peers) {
            if (other.slot == command.targetSlot) {
                target = &other;
            }
        }
        if (target == nullptr) {
            reason = "That player has left.";
            return LobbyCommandResult::TargetNotFound;
        }
        if (target != &peer && !owner) {
            reason = "Only the owner can move another player.";
            return LobbyCommandResult::NotOwner;
        }
        if (m_lockTeams && !owner) {
            reason = "Team changes are locked by the owner.";
            return LobbyCommandResult::NotOwner;
        }
        const TeamLayout layout = resolveTeamLayout(m_lobbyMatch.mode, m_lobbyMatch.format, m_lobbyMatch.customTeams);
        const int requested = command.teamIndex == -1 ? chooseTeam(m_lobbyMatch, target) : command.teamIndex;
        if (command.teamIndex < -1 || requested < 0 || requested >= layout.teamCount) {
            reason = "Choose a team for the current format.";
            return LobbyCommandResult::InvalidTeam;
        }
        const int already = static_cast<int>(
            std::count_if(m_peers.begin(), m_peers.end(), [&](const Peer& p) { return &p != target && p.team == requested; }));
        if (already >= layout.capacity[requested]) {
            reason = "That team is full.";
            return LobbyCommandResult::TeamFull;
        }
        target->team = static_cast<int8_t>(requested);
        target->lobbyReady = false;
        break;
    }
    case LobbyCommand::UpdateMatch: {
        SessionState proposed = command.configuration;
        reason = validateDefinition(proposed);
        if (!reason.empty()) {
            return LobbyCommandResult::InvalidConfiguration;
        }
        const std::string room = resolveRoomKey(proposed.roomKey);
        if (room.empty()) {
            reason = "The server does not have that map.";
            return LobbyCommandResult::MapUnavailable;
        }
        const TeamLayout layout = resolveTeamLayout(proposed.mode, proposed.format, proposed.customTeams);
        const bool exact = isTeamModeWire(proposed.mode) && proposed.format != SessionState::FormatAuto;
        if (layout.teamCount > 0
            && (layout.totalPlayers() < static_cast<int>(m_peers.size()) || (exact && layout.totalPlayers() > m_config.maxPlayers))) {
            reason = "The layout must fit the connected roster and server player limit.";
            return LobbyCommandResult::InvalidConfiguration;
        }
        const bool topologyChanged = !(layout == resolveTeamLayout(m_lobbyMatch.mode, m_lobbyMatch.format, m_lobbyMatch.customTeams));
        proposed.roomKey = room;
        m_config.requireReady = proposed.rule(SessionState::RuleRequireReady);
        m_config.allowJoinInProgress = proposed.rule(SessionState::RuleAllowJoinInProgress);
        m_lockTeams = proposed.rule(SessionState::RuleLockTeams);
        // The match's own rules; the session's are kept apart.
        proposed.rules = static_cast<uint16_t>(proposed.rules
            & (SessionState::RuleFriendlyFire | SessionState::RuleAffinityWeapons | SessionState::RuleShadowFreeze
                | SessionState::RuleHideOpponentHealth));
        proposed.phase = 0;
        proposed.expectedParticipants = proposed.loadedParticipants = 0;
        m_lobbyMatch = proposed;
        if (topologyChanged) {
            normalizeTeams();
        }
        invalidateLobbyReady();
        break;
    }
    case LobbyCommand::StartMatch: {
        uint8_t code = 0;
        reason = validateStart(m_lobbyMatch, m_config.requireReady, code);
        if (code != LobbyCommandResult::Ok) {
            return code;
        }
        m_frozenMatch = m_lobbyMatch;
        const uint16_t previous = m_matchId;
        m_matchId = nextSerial(m_matchId);
        const double built = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        m_phase = SessionState::PhaseStarting; // currentDefinition() is the frozen one from here
        if (!startMatch(now, entryFor(m_frozenMatch))) {
            m_matchId = previous;
            m_phase = SessionState::PhaseLobby;
            reason = "The server could not load this map.";
            return LobbyCommandResult::MapUnavailable;
        }
        const double cost = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count() - built;
        m_expectedLoaded = m_loaded = 0;
        for (Peer& participant : m_peers) {
            m_expectedLoaded |= static_cast<uint8_t>(1 << participant.slot);
            participant.postMatchReady = false;
        }
        m_startDeadline = now + cost + LoadWaitSeconds;
        m_phase = SessionState::PhaseLobby; // for the log line
        setPhase(SessionState::PhaseStarting);
        log("[lobby] %s on %s: waiting for slots mask %02X", modeNames[std::max(portMode(m_frozenMatch.mode), 0)], m_frozenMatch.roomKey.c_str(),
            m_expectedLoaded);
        return LobbyCommandResult::Ok;
    }
    case LobbyCommand::KickPlayer:
    case LobbyCommand::TransferOwner: {
        Peer* selected = nullptr;
        for (Peer& other : m_peers) {
            if (other.slot == command.targetSlot) {
                selected = &other;
            }
        }
        if (selected == nullptr || selected == &peer) {
            reason = "Choose another connected player.";
            return LobbyCommandResult::TargetNotFound;
        }
        if (command.type == LobbyCommand::TransferOwner) {
            m_ownerClientId = selected->clientId;
        } else {
            sendRefusal(selected->endpoint, Refused::ReasonKicked);
            remove(static_cast<size_t>(selected - m_peers.data()), "removed by lobby owner");
        }
        break;
    }
    default:
        break;
    }
    log("[lobby] slot %d: command %u", slot, command.type);
    touchSession();
    return LobbyCommandResult::Ok;
}

void Server::handleMatchLoaded(Peer& peer, std::span<const uint8_t> p, double now)
{
    if (p.size() != static_cast<size_t>(MatchLoadedSize) || readU16(p, 0) != m_matchId) {
        return;
    }
    const auto mask = static_cast<uint8_t>(1 << peer.slot);
    if (m_phase != SessionState::PhaseStarting || (m_expectedLoaded & mask) == 0 || (m_loaded & mask) != 0) {
        return;
    }
    m_loaded |= mask;
    log("[lobby] slot %d loaded match %u", peer.slot, m_matchId);
    touchSession();
    checkLoadBarrier(now);
}

void Server::checkLoadBarrier(double now)
{
    if (m_phase != SessionState::PhaseStarting) {
        return;
    }
    if ((m_loaded & m_expectedLoaded) != m_expectedLoaded && now < m_startDeadline) {
        return;
    }
    log(now >= m_startDeadline ? "[lobby] load timeout; late clients may join in progress" : "[lobby] all clients loaded");
    m_matchStarted = now;
    m_lastAdvance = -1;
    setPhase(SessionState::PhaseInMatch);
    broadcastMatchState(now);
}

void Server::returnToLobby(double now)
{
    // The next map of the rotation, in the last match's format and rules.
    SessionState next = definitionFor(m_config.rotation.advance());
    next.format = m_frozenMatch.format;
    next.customTeams = m_frozenMatch.customTeams;
    next.rules = m_frozenMatch.rules;
    if (!validateDefinition(next).empty()) {
        next.format = SessionState::FormatAuto;
    }
    m_lobbyMatch = next;
    m_matchEndedAt = -1;
    m_simStarted = false;
    m_lastSnapshot.clear();
    m_expectedLoaded = m_loaded = 0;
    closeBallot();
    broadcastMapChoices();
    invalidateLobbyReady();
    m_phase = SessionState::PhaseLobby;
    normalizeTeams();
    m_phase = SessionState::PhasePostMatch; // for the log line
    setPhase(SessionState::PhaseLobby);
    (void)now;
}

void Server::lobbyPeerRemoved(const Peer& peer, double now)
{
    m_expectedLoaded &= static_cast<uint8_t>(~(1 << peer.slot));
    m_loaded &= static_cast<uint8_t>(~(1 << peer.slot));
    if (m_ownerClientId == peer.clientId) {
        m_ownerClientId = m_peers.empty() ? 0 : m_peers.front().clientId;
    }
    if (m_phase == SessionState::PhaseStarting) {
        uint8_t code = 0;
        const std::string why = validateStart(m_frozenMatch, false, code);
        if (code != LobbyCommandResult::Ok) {
            log("[lobby] start cancelled: %s", why.c_str());
            m_simStarted = false;
            m_lastSnapshot.clear();
            invalidateLobbyReady();
            setPhase(SessionState::PhaseLobby);
        } else {
            checkLoadBarrier(now);
        }
    }
}

} // namespace fp::net
