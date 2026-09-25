#include "NetProtocol.h"

#include <algorithm>
#include <cmath>

namespace fp::net {

void writeText(std::span<uint8_t> dest, const std::string& value)
{
    std::fill(dest.begin(), dest.end(), 0);
    const size_t count = std::min(value.size(), dest.size());
    for (size_t i = 0; i < count; i++) {
        const unsigned char c = static_cast<unsigned char>(value[i]);
        dest[i] = c < 32 || c > 126 ? '?' : c;
    }
}

std::string readText(std::span<const uint8_t> src)
{
    std::string text;
    for (uint8_t b : src) {
        if (b == 0) {
            break;
        }
        text.push_back(b < 32 || b > 126 ? '?' : static_cast<char>(b));
    }
    return text;
}

Refused Refused::read(std::span<const uint8_t> src)
{
    Refused r;
    r.reason = src.size() > 0 ? src[0] : 0;
    r.players = src.size() > 1 ? src[1] : 0;
    r.maxPlayers = src.size() > 2 ? src[2] : 0;
    return r;
}

void Refused::write(std::span<uint8_t> dest) const
{
    dest[0] = reason;
    dest[1] = players;
    dest[2] = maxPlayers;
}

std::string Refused::describe(const std::string& where) const
{
    switch (reason) {
    case ReasonKicked:
        return "You were removed by the lobby owner.";
    case ReasonInMatch:
        return "This server does not allow joining a match in progress.";
    case ReasonFull:
        return where + " is full (" + std::to_string(players) + "/" + std::to_string(maxPlayers)
            + " players). Try again when somebody leaves.";
    case ReasonProtocol:
        return where + " is running a different version of the game. One of you needs updating.";
    default:
        return where + " would not admit this client.";
    }
}

MatchState MatchState::read(std::span<const uint8_t> src)
{
    MatchState m;
    m.authorityEpoch = readU64(src, 95);
    m.mode = src[0];
    m.timeRemaining = readF32(src, 1);
    m.timeElapsed = readF32(src, 5);
    m.playerCount = src[9];
    m.flags = src[10];
    m.pointGoal = readU16(src, 11);
    m.matchId = readU16(src, 13);
    m.roomKey = readText(src.subspan(15, MaxNameBytes));
    m.nextRoomKey = readText(src.subspan(15 + MaxNameBytes, MaxNameBytes));
    return m;
}

void MatchState::write(std::span<uint8_t> dest) const
{
    std::fill(dest.begin(), dest.begin() + Size, 0);
    writeU64(dest, 95, authorityEpoch);
    dest[0] = mode;
    writeF32(dest, 1, timeRemaining);
    writeF32(dest, 5, timeElapsed);
    dest[9] = playerCount;
    dest[10] = flags;
    writeU16(dest, 11, pointGoal);
    writeU16(dest, 13, matchId);
    writeText(dest.subspan(15, MaxNameBytes), roomKey);
    writeText(dest.subspan(15 + MaxNameBytes, MaxNameBytes), nextRoomKey);
}

void Roster::write(std::span<uint8_t> dest) const
{
    std::fill(dest.begin(), dest.begin() + Size, 0);
    dest[0] = count;
    writeU16(dest, 1, matchId);
    writeU64(dest, 3, authorityEpoch);
    writeU32(dest, 11, revision);
    writeU16(dest, 15, sessionRevision);
    for (int i = 0; i < count && i < SlotCapacity; i++) {
        const size_t at = HeaderSize + i * EntrySize;
        const Entry& e = entries[i];
        dest[at] = e.slot;
        dest[at + 1] = e.hunter;
        dest[at + 2] = e.color;
        writeU16(dest, at + 3, e.ping);
        writeText(dest.subspan(at + 5, MaxNameBytes), e.name);
        dest[at + 7 + MaxNameBytes] = static_cast<uint8_t>(e.team);
        dest[at + 8 + MaxNameBytes] = e.lobbyReady ? 1 : 0;
        writeU16(dest, at + 21, e.generation);
    }
}

bool Roster::tryRead(std::span<const uint8_t> src, Roster& roster)
{
    if (src.size() != static_cast<size_t>(Size) || src[0] > SlotCapacity) {
        return false;
    }
    int seen = 0;
    for (int i = 0; i < src[0]; i++) {
        const size_t at = HeaderSize + i * EntrySize;
        const int slot = src[at];
        const int team = static_cast<int8_t>(src[at + 7 + MaxNameBytes]);
        if (slot >= SlotCapacity || (seen & (1 << slot)) != 0 || src[at + 1] >= 7 || src[at + 2] > 3 || team < -1 || team > 3
            || src[at + 8 + MaxNameBytes] > 1) {
            return false;
        }
        seen |= 1 << slot;
    }
    roster = {};
    roster.count = src[0];
    roster.matchId = readU16(src, 1);
    roster.authorityEpoch = readU64(src, 3);
    roster.revision = readU32(src, 11);
    roster.sessionRevision = readU16(src, 15);
    for (int i = 0; i < roster.count; i++) {
        const size_t at = HeaderSize + i * EntrySize;
        Entry& e = roster.entries[i];
        e.slot = src[at];
        e.hunter = src[at + 1];
        e.color = src[at + 2];
        e.ping = readU16(src, at + 3);
        e.name = readText(src.subspan(at + 5, MaxNameBytes));
        e.team = static_cast<int8_t>(src[at + 7 + MaxNameBytes]);
        e.lobbyReady = src[at + 8 + MaxNameBytes] != 0;
        e.generation = readU16(src, at + 21);
    }
    return true;
}

void Chat::write(std::span<uint8_t> dest) const
{
    std::fill(dest.begin(), dest.begin() + Size, 0);
    dest[0] = slot;
    dest[1] = kind;
    writeText(dest.subspan(2, MaxNameBytes), name);
    writeText(dest.subspan(2 + MaxNameBytes, MaxTextBytes), text);
}

Chat Chat::read(std::span<const uint8_t> src)
{
    Chat c;
    c.slot = src[0];
    c.kind = src[1];
    c.name = readText(src.subspan(2, MaxNameBytes));
    c.text = readText(src.subspan(2 + MaxNameBytes, MaxTextBytes));
    return c;
}

void Intent::write(std::span<uint8_t> dest) const
{
    writeU32(dest, 0, frame);
    writeU32(dest, 4, buttons);
    writeVec(dest, 8, aim);
    dest[20] = weaponSelect;
    for (int i = 0; i < PressHistory; i++) {
        writeU32(dest, 21 + i * 4, presses[i]);
    }
    const size_t at = 21 + PressHistory * 4;
    writeVec(dest, at, position);
    writeU16(dest, at + 12, ammoUa);
    writeU16(dest, at + 14, ammoMissiles);
    writeU32(dest, at + 16, ackFrame);
    dest[at + 20] = ackSubFrame;
    writeU16(dest, 74, matchId);
    writeU64(dest, 76, authorityEpoch);
    writeU16(dest, 84, slotGeneration);
    writeU16(dest, 86, lifeId);
    dest[Size] = chargeLevel;
    dest[Size + 1] = boostDamage;
    dest[Size + 2] = shotFlags;
    dest[Size + 3] = 0;
}

Intent Intent::read(std::span<const uint8_t> src)
{
    Intent in;
    in.frame = readU32(src, 0);
    in.buttons = readU32(src, 4);
    in.aim = readVec(src, 8);
    in.weaponSelect = src[20];
    for (int i = 0; i < PressHistory; i++) {
        in.presses[i] = readU32(src, 21 + i * 4);
    }
    const size_t at = 21 + PressHistory * 4;
    in.position = readVec(src, at);
    in.ammoUa = readU16(src, at + 12);
    in.ammoMissiles = readU16(src, at + 14);
    in.ackFrame = readU32(src, at + 16);
    in.ackSubFrame = src[at + 20];
    in.matchId = readU16(src, 74);
    in.authorityEpoch = readU64(src, 76);
    in.slotGeneration = readU16(src, 84);
    in.lifeId = readU16(src, 86);
    in.hasState = src.size() >= static_cast<size_t>(FullSize);
    if (in.hasState) {
        in.chargeLevel = src[Size];
        in.boostDamage = src[Size + 1];
        in.shotFlags = src[Size + 2];
    }
    return in;
}

void DamageEvent::write(std::span<uint8_t> dest) const
{
    writeU16(dest, 0, eventId);
    writeU16(dest, 2, attackerGeneration);
    writeU16(dest, 4, damage);
    dest[6] = attackerSlot;
    dest[7] = beam;
    dest[8] = flags;
    for (int i = 0; i < 3; i++) {
        const int packed = std::clamp(static_cast<int>(std::lround(direction[i] * 16384.0f)), -32768, 32767);
        writeU16(dest, 9 + i * 2, static_cast<uint16_t>(static_cast<int16_t>(packed)));
    }
}

DamageEvent DamageEvent::read(std::span<const uint8_t> src)
{
    DamageEvent e;
    e.eventId = readU16(src, 0);
    e.attackerGeneration = readU16(src, 2);
    e.damage = readU16(src, 4);
    e.attackerSlot = src[6];
    e.beam = src[7];
    e.flags = src[8];
    // Knockback in signed 16-bit fixed point, 1/16384.
    e.direction = {readI16(src, 9) / 16384.0f, readI16(src, 11) / 16384.0f, readI16(src, 13) / 16384.0f};
    return e;
}

void PlayerState::write(std::span<uint8_t> dest) const
{
    dest[0] = slot;
    dest[1] = flags;
    writeVec(dest, 2, position);
    writeVec(dest, 14, speed);
    writeVec(dest, 26, facing);
    writeU16(dest, 38, health);
    dest[40] = currentWeapon;
    dest[41] = team;
    writeU16(dest, 42, static_cast<uint16_t>(points));
    writeU16(dest, 44, kills);
    writeU16(dest, 46, deaths);
    writeU16(dest, 48, slotGeneration);
    writeU16(dest, 50, lifeId);
    writeU16(dest, 52, damageEventId);
    for (int i = 0; i < DamageHistory; i++) {
        damage[i].write(dest.subspan(54 + i * DamageEvent::Size));
    }
}

PlayerState PlayerState::read(std::span<const uint8_t> src)
{
    PlayerState s;
    s.slot = src[0];
    s.flags = src[1];
    s.position = readVec(src, 2);
    s.speed = readVec(src, 14);
    s.facing = readVec(src, 26);
    s.health = readU16(src, 38);
    s.currentWeapon = src[40];
    s.team = src[41];
    s.points = readI16(src, 42);
    s.kills = readU16(src, 44);
    s.deaths = readU16(src, 46);
    s.slotGeneration = readU16(src, 48);
    s.lifeId = readU16(src, 50);
    s.damageEventId = readU16(src, 52);
    for (int i = 0; i < DamageHistory; i++) {
        s.damage[i] = DamageEvent::read(src.subspan(54 + i * DamageEvent::Size));
    }
    return s;
}

void SnapshotHeader::write(std::span<uint8_t> dest) const
{
    writeU32(dest, 0, frame);
    writeU32(dest, 4, rng1);
    writeU32(dest, 8, rng2);
    dest[12] = playerCount;
    writeU16(dest, 13, matchId);
    writeU64(dest, 15, authorityEpoch);
}

SnapshotHeader SnapshotHeader::read(std::span<const uint8_t> src)
{
    SnapshotHeader h;
    h.frame = readU32(src, 0);
    h.rng1 = readU32(src, 4);
    h.rng2 = readU32(src, 8);
    h.playerCount = src[12];
    h.matchId = readU16(src, 13);
    h.authorityEpoch = readU64(src, 15);
    return h;
}

void SessionState::write(std::span<uint8_t> dest) const
{
    std::fill(dest.begin(), dest.begin() + Size, 0);
    dest[0] = phase;
    dest[1] = policy;
    writeU16(dest, 2, revision);
    writeU16(dest, 4, matchId);
    dest[6] = ownerSlot;
    dest[7] = maxPlayers;
    dest[8] = format;
    dest[9] = mode;
    writeU16(dest, 10, timeLimit);
    writeU16(dest, 12, pointGoal);
    writeU16(dest, 14, rules);
    dest[16] = expectedParticipants;
    dest[17] = loadedParticipants;
    for (int i = 0; i < 5; i++) {
        dest[20 + i] = customTeams[i];
    }
    dest[25] = entityLayerPlayers;
    dest[26] = resources;
    writeText(dest.subspan(27, 40), roomKey);
    writeU64(dest, Size - 8, authorityEpoch);
}

bool SessionState::tryRead(std::span<const uint8_t> src, SessionState& state)
{
    if (src.size() != static_cast<size_t>(Size) || src[0] > 3 || src[1] > 1 || src[7] < 1 || src[7] > 8
        || (src[6] != 0xFF && src[6] >= src[7]) || (src[16] & ~((1 << src[7]) - 1)) != 0 || (src[17] & ~src[16]) != 0 || src[8] > 7
        || portMode(src[9]) < 0 || (readU16(src, 14) & ~127) != 0) {
        return false;
    }
    state.phase = src[0];
    state.policy = src[1];
    state.revision = readU16(src, 2);
    state.matchId = readU16(src, 4);
    state.ownerSlot = src[6];
    state.maxPlayers = src[7];
    state.format = src[8];
    state.mode = src[9];
    state.timeLimit = readU16(src, 10);
    state.pointGoal = readU16(src, 12);
    state.rules = readU16(src, 14);
    state.expectedParticipants = src[16];
    state.loadedParticipants = src[17];
    for (int i = 0; i < 5; i++) {
        state.customTeams[i] = src[20 + i];
    }
    state.entityLayerPlayers = src[25];
    state.resources = src[26];
    state.roomKey = readText(src.subspan(27, 40));
    state.authorityEpoch = readU64(src, Size - 8);
    return validateDefinition(state).empty();
}

bool TeamLayout::valid() const
{
    if (teamCount < 2 || teamCount > 4 || totalPlayers() < 2 || totalPlayers() > 8) {
        return false;
    }
    for (int team = 0; team < 4; team++) {
        if (team < teamCount ? capacity[team] == 0 : capacity[team] != 0) {
            return false;
        }
    }
    return true;
}

std::string TeamLayout::describe() const
{
    std::string text;
    for (int i = 0; i < teamCount; i++) {
        text += (i > 0 ? "v" : "") + std::to_string(capacity[i]);
    }
    return teamCount >= 2 ? text : "FFA";
}

int chooseTeam(const TeamLayout& layout, const std::array<int, 4>& counts)
{
    int best = -1;
    for (int team = 0; team < layout.teamCount; team++) {
        const int capacity = layout.capacity[team];
        if (capacity == 0 || counts[team] >= capacity) {
            continue;
        }
        if (best < 0 || counts[team] * layout.capacity[best] < counts[best] * capacity
            || (counts[team] * layout.capacity[best] == counts[best] * capacity && counts[team] < counts[best])) {
            best = team;
        }
    }
    return best;
}

bool isTeamModeWire(int wireMode)
{
    const int mode = portMode(wireMode);
    return mode == 1 || mode == 3 || mode == 4 || mode == 6 || mode == 8 || mode == 10;
}

TeamLayout resolveTeamLayout(int wireMode, uint8_t format, const std::array<uint8_t, 5>& custom)
{
    if (!isTeamModeWire(wireMode)) {
        return {};
    }
    switch (format) {
    case SessionState::FormatOneVsOne: return {2, {1, 1, 0, 0}};
    case SessionState::FormatTwoVsTwo: return {2, {2, 2, 0, 0}};
    case SessionState::FormatThreeVsThree: return {2, {3, 3, 0, 0}};
    case SessionState::FormatTwoVsTwoVsTwoVsTwo: return {4, {2, 2, 2, 2}};
    case SessionState::FormatCustom: return {custom[0], {custom[1], custom[2], custom[3], custom[4]}};
    default: return {2, {4, 4, 0, 0}};
    }
}

std::string validateDefinition(const SessionState& d)
{
    const int mode = portMode(d.mode);
    if (d.roomKey.empty() || d.roomKey.size() > 40 || d.format > 7 || mode < 0) {
        return "Choose a multiplayer map and mode.";
    }
    const bool teams = isTeamModeWire(d.mode);
    if (d.format != SessionState::FormatAuto && teams != (d.format != SessionState::FormatFreeForAll)) {
        return "Choose a team mode for a team format, or a free-for-all mode for FFA.";
    }
    const TeamLayout layout = resolveTeamLayout(d.mode, d.format, d.customTeams);
    if (teams && !layout.valid()) {
        return "Use 2 to 4 nonempty teams, zero inactive capacities, and at most 8 players.";
    }
    if (mode == 4 && layout.teamCount != 2) {
        return "Capture requires exactly two teams because maps have two bases.";
    }
    return {};
}

void Vote::write(std::span<uint8_t> dest) const
{
    std::fill(dest.begin(), dest.begin() + Size, 0);
    dest[0] = kind;
    writeText(dest.subspan(1, MatchState::MaxNameBytes), roomKey);
}

Vote Vote::read(std::span<const uint8_t> src)
{
    return {src[0], readText(src.subspan(1, MatchState::MaxNameBytes))};
}

void VoteState::write(std::span<uint8_t> dest) const
{
    std::fill(dest.begin(), dest.begin() + Size, 0);
    dest[0] = state;
    writeText(dest.subspan(1, MatchState::MaxNameBytes), roomKey);
    writeText(dest.subspan(1 + MatchState::MaxNameBytes, Chat::MaxNameBytes), proposer);
    const size_t at = 1 + MatchState::MaxNameBytes + Chat::MaxNameBytes;
    dest[at] = yes;
    dest[at + 1] = no;
    dest[at + 2] = eligible;
    dest[at + 3] = needed;
    writeU16(dest, at + 4, seconds);
}

VoteState VoteState::read(std::span<const uint8_t> src)
{
    VoteState v;
    const size_t at = 1 + MatchState::MaxNameBytes + Chat::MaxNameBytes;
    v.state = src[0];
    v.roomKey = readText(src.subspan(1, MatchState::MaxNameBytes));
    v.proposer = readText(src.subspan(1 + MatchState::MaxNameBytes, Chat::MaxNameBytes));
    v.yes = src[at];
    v.no = src[at + 1];
    v.eligible = src[at + 2];
    v.needed = src[at + 3];
    v.seconds = readU16(src, at + 4);
    return v;
}

void MapChoices::write(std::span<uint8_t> dest) const
{
    std::fill(dest.begin(), dest.begin() + Size, 0);
    const int count = std::min<int>(static_cast<int>(choices.size()), MaxChoices);
    dest[0] = static_cast<uint8_t>(count);
    dest[1] = eligible;
    dest[2] = open ? 1 : 0;
    for (int i = 0; i < count; i++) {
        const size_t at = 4 + static_cast<size_t>(i) * (MatchState::MaxNameBytes + 1);
        writeText(dest.subspan(at, MatchState::MaxNameBytes), choices[i].first);
        dest[at + MatchState::MaxNameBytes] = choices[i].second;
    }
}

MapChoices MapChoices::read(std::span<const uint8_t> src)
{
    MapChoices m;
    const int count = std::min<int>(src[0], MaxChoices);
    m.eligible = src[1];
    m.open = src[2] != 0;
    for (int i = 0; i < count; i++) {
        const size_t at = 4 + static_cast<size_t>(i) * (MatchState::MaxNameBytes + 1);
        m.choices.emplace_back(readText(src.subspan(at, MatchState::MaxNameBytes)), src[at + MatchState::MaxNameBytes]);
    }
    return m;
}

void LobbyCommand::write(std::span<uint8_t> dest) const
{
    std::fill(dest.begin(), dest.begin() + Size, 0);
    writeU32(dest, 0, commandId);
    writeU16(dest, 4, expectedRevision);
    dest[6] = type;
    dest[7] = targetSlot;
    dest[8] = static_cast<uint8_t>(teamIndex);
    dest[9] = ready ? 1 : 0;
    configuration.write(dest.subspan(10, SessionState::Size));
}

bool LobbyCommand::tryRead(std::span<const uint8_t> src, LobbyCommand& command)
{
    if (src.size() != static_cast<size_t>(Size) || src[6] > TransferOwner || src[9] > 1) {
        return false;
    }
    SessionState config;
    if (src[6] == UpdateMatch && !SessionState::tryRead(src.subspan(10, SessionState::Size), config)) {
        return false;
    }
    command.commandId = readU32(src, 0);
    command.expectedRevision = readU16(src, 4);
    command.type = src[6];
    command.targetSlot = src[7];
    command.teamIndex = static_cast<int8_t>(src[8]);
    command.ready = src[9] != 0;
    command.configuration = config;
    return command.commandId != 0;
}

void LobbyCommandResult::write(std::span<uint8_t> dest) const
{
    std::fill(dest.begin(), dest.begin() + Size, 0);
    writeU32(dest, 0, commandId);
    dest[4] = result;
    writeU16(dest, 5, currentRevision);
    writeText(dest.subspan(7, 96), reason);
}

bool LobbyCommandResult::tryRead(std::span<const uint8_t> src, LobbyCommandResult& r)
{
    if (src.size() != static_cast<size_t>(Size) || src[4] > MapUnavailable) {
        return false;
    }
    r.commandId = readU32(src, 0);
    r.result = src[4];
    r.currentRevision = readU16(src, 5);
    r.reason = readText(src.subspan(7, 96));
    return true;
}

void ServerStatus::write(std::span<uint8_t> dest) const
{
    match.write(dest);
    dest[MatchState::Size] = maxPlayers;
    dest[MatchState::Size + 1] = protocol;
    writeText(dest.subspan(MatchState::Size + 2, MaxNameBytes), name);
    dest[Size] = flags;
    dest[Size + 1] = phase;
    dest[Size + 2] = format;
    dest[Size + 3] = lobbyEnabled ? 1 : 0;
    dest[Size + 4] = allowJoinInProgress ? 1 : 0;
}

ServerStatus ServerStatus::read(std::span<const uint8_t> src)
{
    ServerStatus status;
    status.match = MatchState::read(src);
    status.maxPlayers = src[MatchState::Size];
    status.protocol = src[MatchState::Size + 1];
    status.name = readText(src.subspan(MatchState::Size + 2, MaxNameBytes));
    status.flags = src.size() > static_cast<size_t>(Size) ? src[Size] : 0;
    if (src.size() >= static_cast<size_t>(SizeWithFlags)) {
        status.phase = src[Size + 1];
        status.format = src[Size + 2];
        status.lobbyEnabled = src[Size + 3] != 0;
        status.allowJoinInProgress = src[Size + 4] != 0;
    }
    return status;
}

void HitClaim::write(std::span<uint8_t> dest) const
{
    writeU16(dest, 0, claimId);
    writeU32(dest, 2, frame);
    writeU32(dest, 6, ackFrame);
    writeU32(dest, 10, launchFrame);
    dest[14] = victimSlot;
    dest[15] = beam;
    writeU16(dest, 16, damage);
    dest[18] = flags;
    writeVec(dest, 19, hitPoint);
    writeU16(dest, 31, matchId);
    writeU64(dest, 33, authorityEpoch);
    writeU16(dest, 41, shooterGeneration);
    writeU16(dest, 43, shooterLifeId);
    writeU16(dest, 45, victimGeneration);
    writeU16(dest, 47, victimLifeId);
}

HitClaim HitClaim::read(std::span<const uint8_t> src)
{
    HitClaim c;
    c.claimId = readU16(src, 0);
    c.frame = readU32(src, 2);
    c.ackFrame = readU32(src, 6);
    c.launchFrame = readU32(src, 10);
    c.victimSlot = src[14];
    c.beam = src[15];
    c.damage = readU16(src, 16);
    c.flags = src[18];
    c.hitPoint = readVec(src, 19);
    c.matchId = readU16(src, 31);
    c.authorityEpoch = readU64(src, 33);
    c.shooterGeneration = readU16(src, 41);
    c.shooterLifeId = readU16(src, 43);
    c.victimGeneration = readU16(src, 45);
    c.victimLifeId = readU16(src, 47);
    return c;
}

const char* HitVerdict::describe(uint8_t result)
{
    switch (result) {
    case WrongLife: return "wrong lifecycle";
    case Geometry: return "hit point outside reconciliation radius";
    case DamageLimit: return "damage exceeds weapon limit";
    case InvalidLaunch: return "launch frame follows hit frame";
    case NoDamage: return "authority damage rules prevented the hit";
    case Applied: return "applied";
    case Duplicate: return "already resolved";
    case DeadShooter: return "shooter was already dead when it fired";
    case DeadVictim: return "victim was already down";
    case Refused: return "refused";
    case TooOld: return "older than the history";
    default: return "unknown";
    }
}

bool parseGuid(const std::string& text, std::array<uint8_t, 16>& bytes)
{
    std::string hex;
    for (char c : text) {
        if (c != '-' && c != '{' && c != '}') {
            hex.push_back(c);
        }
    }
    if (hex.size() != 32) {
        return false;
    }
    std::array<uint8_t, 16> raw{};
    for (int i = 0; i < 16; i++) {
        int value = 0;
        for (int j = 0; j < 2; j++) {
            const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(hex[i * 2 + j])));
            const int digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
            if (digit < 0) {
                return false;
            }
            value = value * 16 + digit;
        }
        raw[i] = static_cast<uint8_t>(value);
    }
    // Data1 (4 bytes), Data2 and Data3 (2 each) are stored little-endian.
    bytes = raw;
    std::reverse(bytes.begin(), bytes.begin() + 4);
    std::reverse(bytes.begin() + 4, bytes.begin() + 6);
    std::reverse(bytes.begin() + 6, bytes.begin() + 8);
    return true;
}

int wireMode(int portMode) { return portMode + 3; }

int portMode(int wireMode) { return wireMode >= 3 && wireMode <= 14 ? wireMode - 3 : -1; }

} // namespace fp::net
