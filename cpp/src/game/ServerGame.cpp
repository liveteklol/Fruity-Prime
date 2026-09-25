#include "ServerGame.h"

#include "Rng.h"
#include "World.h"
#include "formats/Rooms.h"

#include <QtGlobal>

#include <algorithm>
#include <cmath>

namespace fp {

namespace {

namespace B = net::Buttons;

bool sane(const Vec3& v)
{
    for (float c : v) {
        if (!std::isfinite(c) || std::fabs(c) >= 100000) {
            return false;
        }
    }
    return true;
}

bool nonZero(const Vec3& v) { return v[0] != 0 || v[1] != 0 || v[2] != 0; }

// A position measured in `alt`'s frame of reference, in the player's current form's.
Vec3 inForm(const Player& p, Vec3 position, bool alt)
{
    if (alt != p.isAltForm()) {
        position = alt ? position - p.formOffset() : position + p.formOffset();
    }
    return position;
}

bool debugNet() { return qEnvironmentVariableIsSet("FP_DEBUG_NET"); }

} // namespace

ServerGame::ServerGame(std::filesystem::path root)
    : m_root(std::move(root))
{
}

ServerGame::~ServerGame() = default;

Player* ServerGame::worldPlayer(int serverSlot)
{
    const int w = m_slots[serverSlot].worldSlot;
    return w >= 0 && m_room ? m_room->world->player(static_cast<size_t>(w)) : nullptr;
}

int ServerGame::serverSlotOf(const Player& player) const
{
    const int w = player.slot();
    return w >= 0 && w < static_cast<int>(m_serverSlotOfWorld.size()) ? m_serverSlotOfWorld[w] : -1;
}

bool ServerGame::start(const net::RotationEntry& entry, bool friendlyFire, int layerPlayers)
{
    const RoomMetadata* room = findRoom(entry.roomKey);
    if (room == nullptr) {
        qWarning("[sim] unknown room \"%s\"", entry.roomKey.c_str());
        return false;
    }
    const GameMode mode = static_cast<GameMode>(std::clamp(entry.mode, 0, 11));
    const bool reload = !m_room || m_roomKey != entry.roomKey || m_mode != entry.mode || m_layerPlayers != layerPlayers;
    if (reload) {
        m_room.reset();
        std::optional<MatchRoom> loaded = loadMatchRoom(m_root, *room, mode, layerPlayers, 0, false);
        if (!loaded || loaded->world->collision() == nullptr) {
            return false;
        }
        m_room = std::move(loaded);
        m_roomKey = entry.roomKey;
        m_mode = entry.mode;
        m_layerPlayers = layerPlayers;
        World& w = *m_room->world;
        // Slot 0, which the World made for a main player, waits for somebody like the rest.
        w.m_slots[0].player->despawn();
        w.m_slots[0].active = false;
        m_freeWorldSlots = {0};
        m_serverSlotOfWorld = {-1};
        for (Slot& s : m_slots) {
            s.worldSlot = -1;
        }
        w.setIntro(std::nullopt);
        World::Authority authority;
        authority.beforePlayer = [this](size_t worldSlot) {
            const int s = m_serverSlotOfWorld[worldSlot];
            if (s >= 0) {
                applyIntentState(s);
            }
        };
        authority.afterPlayer = [this](size_t worldSlot) {
            const int s = m_serverSlotOfWorld[worldSlot];
            if (s >= 0) {
                placeReported(s, false);
            }
        };
        authority.beginShot = [this](Player& shooter) { beginShot(shooter); };
        authority.endShot = [this](Player& shooter) { endShot(shooter); };
        authority.damage = [this](Player& victim, const DamageSource& source, bool, uint32_t flags) { noteDamage(victim, source, flags); };
        w.setAuthority(std::move(authority));
        w.setNetDamageHooks(this);
        w.setLaunchFrameSource([this](int worldSlot) {
            // NetUnlagged.LaunchFrameFor: the world the shooter was looking at.
            const int s = worldSlot >= 0 && worldSlot < static_cast<int>(m_serverSlotOfWorld.size()) ? m_serverSlotOfWorld[worldSlot] : -1;
            return s >= 0 ? fireFrameOf(s) : m_frame;
        });
        w.setOtherInput([this](size_t worldSlot, long long) { return worldSlot < m_input.size() ? m_input[worldSlot] : PlayerInput{}; });
        qInfo("[sim] loaded %s", room->name);
    }
    World& w = *m_room->world;
    teamRules().teamCount = std::clamp(entry.teamCount, 2, 4); // the format's teams (Auto: two)
    w.setFriendlyFire(friendlyFire);
    // MatchGoalRules: Defender and Prime Hunter keep their time target in the point goal.
    const bool timeTarget = mode == GameMode::Defender || mode == GameMode::DefenderTeams || mode == GameMode::PrimeHunter;
    w.setTimeGoal(timeTarget ? static_cast<float>(entry.pointGoal) : 0.0f);
    w.setRules(timeTarget ? 0 : entry.pointGoal, entry.timeLimit);
    for (Slot& s : m_slots) {
        // Lives start again with the match (the clients reset theirs on its new id).
        s.life = 0;
        s.hasIntent = false;
        s.pressSeen = s.reportSeen = s.mismatching = s.attempted = false;
        s.damage = {};
        if (Player* p = worldPlayer(static_cast<int>(&s - m_slots.data()))) {
            s.spawnCount = p->spawnCount();
        }
    }
    if (!reload) {
        w.restartMatch();
    }
    m_stamp = {};
    m_moved = {};
    m_reconciled = m_shotInProgress = false;
    m_frame = 0;
    resetClaims();
    return true;
}

void ServerGame::setOccupants(const std::array<net::Occupant, net::SlotCapacity>& occupants)
{
    for (int i = 0; i < net::SlotCapacity; i++) {
        Slot& s = m_slots[i];
        if (occupants[i].generation != s.occupant.generation || !occupants[i].occupied) {
            // Somebody else (or nobody): the previous occupant's player goes.
            if (s.worldSlot >= 0) {
                release(i);
            }
            s.life = 0;
            s.hasIntent = false;
            s.damage = {};
            forgetClaims(i);
        }
        s.occupant = occupants[i];
    }
}

void ServerGame::release(int serverSlot)
{
    Slot& s = m_slots[serverSlot];
    if (s.worldSlot < 0 || !m_room) {
        s.worldSlot = -1;
        return;
    }
    World& w = *m_room->world;
    World::PlayerSlot& ws = w.m_slots[s.worldSlot];
    ws.player->despawn();
    ws.active = false;
    ws.points = ws.kills = ws.deaths = 0;
    ws.time = 0;
    m_serverSlotOfWorld[s.worldSlot] = -1;
    m_freeWorldSlots.push_back(s.worldSlot);
    s.worldSlot = -1;
}

void ServerGame::bind(int serverSlot)
{
    // A world player for this slot: one left free with the same hunter, else a new one.
    Slot& s = m_slots[serverSlot];
    World& w = *m_room->world;
    const int hunter = std::clamp(s.occupant.hunter, 0, 6);
    int worldSlot = -1;
    for (size_t i = 0; i < m_freeWorldSlots.size(); i++) {
        if (static_cast<int>(w.player(m_freeWorldSlots[i])->hunter()) == hunter) {
            worldSlot = m_freeWorldSlots[i];
            m_freeWorldSlots.erase(m_freeWorldSlots.begin() + static_cast<long>(i));
            break;
        }
    }
    if (worldSlot < 0) {
        World::PlayerSlot& created = w.createSlot(hunter);
        worldSlot = created.player->slot();
    }
    if (m_serverSlotOfWorld.size() <= static_cast<size_t>(worldSlot)) {
        m_serverSlotOfWorld.resize(worldSlot + 1, -1);
    }
    m_serverSlotOfWorld[worldSlot] = serverSlot;
    s.worldSlot = worldSlot;
    World::PlayerSlot& ws = w.m_slots[worldSlot];
    ws.player->despawn(); // the next step spawns it at a spawn point
    ws.player->setBot(false, 0);
    ws.active = true;
    ws.points = ws.kills = ws.deaths = 0;
    ws.time = 0;
    s.spawnCount = ws.player->spawnCount();
    qInfo("[sim] slot %d (%s) plays hunter %d, world slot %d", serverSlot, s.occupant.name.c_str(), hunter, worldSlot);
}

void ServerGame::syncSlots()
{
    World& w = *m_room->world;
    for (int i = 0; i < net::SlotCapacity; i++) {
        Slot& s = m_slots[i];
        if (!s.occupant.occupied) {
            if (s.worldSlot >= 0) {
                release(i);
            }
            continue;
        }
        if (s.worldSlot >= 0 && static_cast<int>(w.player(s.worldSlot)->hunter()) != std::clamp(s.occupant.hunter, 0, 6)) {
            release(i); // another hunter: another player, from its next life
            s.life = 0;
            s.hasIntent = false;
        }
        if (s.worldSlot < 0) {
            bind(i);
        }
    }
    // Teams and suits, as the server dealt them.
    const bool teams = w.teams();
    for (Slot& s : m_slots) {
        if (s.worldSlot < 0) {
            continue;
        }
        World::PlayerSlot& ws = w.m_slots[s.worldSlot];
        ws.name = s.occupant.name;
        const int team = teams && s.occupant.team >= 0 ? s.occupant.team : s.worldSlot;
        if (ws.player->teamIndex() != team || ws.recolor != s.occupant.color) {
            ws.player->setTeamIndex(team);
            ws.recolor = s.occupant.color;
            w.applyTeamVisuals(ws);
        }
    }
}

void ServerGame::intent(int slot, const net::Intent& in)
{
    Slot& s = m_slots[slot];
    if (s.hasIntent && s.intent.lifeId == in.lifeId && !net::newer32(in.frame, s.intent.frame)) {
        return; // reordered
    }
    if (!s.hasIntent || s.intent.lifeId != in.lifeId) {
        s.pressSeen = false;
    }
    s.intent = in;
    s.hasIntent = true;
    s.intentFrame = m_frame;
}

bool ServerGame::intentCurrent(const Slot& s) const
{
    return s.hasIntent && s.life != 0 && s.intent.lifeId == s.life && m_frame - s.intentFrame <= 30;
}

PlayerInput ServerGame::puppetInput(int serverSlot)
{
    // NetPlayerBridge.ApplyIntent: held buttons from the levels, presses from
    // the history, each press taken once.
    PlayerInput in;
    Slot& s = m_slots[serverSlot];
    if (!intentCurrent(s)) {
        return in;
    }
    const net::Intent& intent = s.intent;
    uint32_t pressed = 0;
    if (!s.pressSeen) {
        s.pressSeen = true;
        s.lastPressFrame = intent.frame;
    } else {
        for (int i = net::Intent::PressHistory - 1; i >= 0; i--) {
            if (intent.frame < static_cast<uint32_t>(i)) {
                continue;
            }
            const uint32_t frame = intent.frame - static_cast<uint32_t>(i);
            if (frame <= s.lastPressFrame) {
                continue;
            }
            pressed |= intent.presses[i];
        }
        s.lastPressFrame = std::max(s.lastPressFrame, intent.frame);
    }
    if (!(intent.buttons & B::InPlayState)) {
        return in; // a dead player's fire is a respawn request, not a shot
    }
    const uint32_t held = intent.buttons | pressed;
    in.forward = held & B::MoveUp;
    in.back = held & B::MoveDown;
    in.left = held & B::MoveLeft;
    in.right = held & B::MoveRight;
    in.jumpPressed = pressed & B::Jump;
    in.morphPressed = pressed & B::Morph;
    in.boostHeld = held & B::Boost;
    in.altAttackPressed = pressed & B::AltAttack;
    in.altAttackHeld = held & B::AltAttack;
    in.shootPressed = pressed & B::Shoot;
    in.shootHeld = held & B::Shoot;
    in.hasInput = (intent.buttons & ~B::States) != 0 || pressed != 0;
    return in;
}

int ServerGame::reconcileForm(Slot& s, bool desiredAlt, const Player& p)
{
    // FormReconciliation, on the machine that answers the question for
    // everybody: 0 nothing, 1 start the switch, 2 force the form.
    const bool transitioning = p.isMorphing() || p.isUnmorphing();
    if (p.isAltForm() == desiredAlt && !transitioning) {
        s.mismatching = s.attempted = false;
        return 0;
    }
    if (!s.mismatching) {
        s.mismatching = true;
        s.mismatchSince = m_frame;
    }
    if (transitioning) {
        if (m_frame - s.mismatchSince >= 90) {
            s.mismatching = s.attempted = false;
            return 2;
        }
        return 0;
    }
    if (s.attempted) {
        if (m_frame - s.attemptSince < 12) {
            return 0;
        }
        s.mismatching = s.attempted = false;
        return 2;
    }
    if (m_frame - s.mismatchSince < 8) {
        return 0;
    }
    s.attempted = true;
    s.attemptSince = m_frame;
    return 1;
}

void ServerGame::applyIntentState(int serverSlot)
{
    // What the owner says about itself, before its step: where it aims, what
    // it holds, its form and what its next shot is worth; and where it is.
    Slot& s = m_slots[serverSlot];
    Player* p = worldPlayer(serverSlot);
    if (p != nullptr && s.hasIntent && s.intent.lifeId == s.life) {
        // The owner's own answer about whether it is still in the match.
        p->netSetSpectating((s.intent.buttons & B::SpectatingState) != 0);
    }
    if (p == nullptr || p->health() <= 0 || !intentCurrent(s) || !sane(s.intent.aim)) {
        return;
    }
    const net::Intent& intent = s.intent;
    if (!(intent.buttons & B::InPlayState)) {
        return;
    }
    // A new life keeps the spawn's aim until the owner has seen that life.
    if (s.aimHeld && (intent.ackFrame >= s.spawnFrame || m_frame - s.spawnFrame > 90)) {
        s.aimHeld = false;
    }
    if (!s.aimHeld && nonZero(intent.aim)) {
        p->netSetFacing(intent.aim);
    }
    if (intent.weaponSelect != 0xFF) {
        p->netSetWeapon(intent.weaponSelect);
    }
    p->setAmmo(0, intent.ammoUa);
    p->setAmmo(1, intent.ammoMissiles);
    p->netSetZoom((intent.buttons & B::ZoomedState) != 0);
    const bool alt = (intent.buttons & B::AltFormState) != 0;
    switch (reconcileForm(s, alt, *p)) {
    case 1:
        p->netStartFormSwitch();
        break;
    case 2:
        p->netForceForm(alt);
        break;
    default:
        break;
    }
    if (intent.hasState) {
        p->netSetShotState(intent.chargeLevel, intent.boostDamage, (intent.shotFlags & net::Intent::FlagDoubleDamage) != 0);
    }
    placeReported(serverSlot, true);
}

void ServerGame::placeReported(int serverSlot, bool noteVelocity)
{
    // NetPlayerBridge.ApplyReportedPosition, and RestoreReportedPosition after
    // the step: the server takes a player's position from its own report.
    Slot& s = m_slots[serverSlot];
    Player* p = worldPlayer(serverSlot);
    if (p == nullptr || p->health() <= 0 || !intentCurrent(s) || !(s.intent.buttons & B::InPlayState)) {
        return;
    }
    const Vec3& reported = s.intent.position;
    if (!sane(reported) || !nonZero(reported) || p->frozenTimer() > 0) {
        return;
    }
    const Vec3 position = inForm(*p, reported, (s.intent.buttons & B::AltFormState) != 0);
    if (noteVelocity) {
        // NoteReportedVelocity: the speed the snapshot publishes, from the reports themselves.
        if (s.reportSeen && net::newer32(s.intent.frame, s.lastReportFrame)) {
            const float frames = static_cast<float>(s.intent.frame - s.lastReportFrame);
            const Vec3 speed = (reported - s.lastReport) * (1 / frames);
            s.reportedSpeed = dot(speed, speed) < 4 * 4 ? speed : Vec3{};
        }
        if (!s.reportSeen || net::newer32(s.intent.frame, s.lastReportFrame)) {
            s.reportSeen = true;
            s.lastReport = reported;
            s.lastReportFrame = s.intent.frame;
        }
    } else if (s.reportSeen) {
        p->netSetSpeed(s.reportedSpeed);
    }
    p->netPlace(position);
}

void ServerGame::noteDamage(Player& victim, const DamageSource& source, uint32_t flags)
{
    // NetDamage.Note: every hit this machine resolves, for the victim's
    // snapshot history, which is what every client replays.
    const int slot = serverSlotOf(victim);
    if (slot < 0) {
        return;
    }
    Slot& s = m_slots[slot];
    s.damageSequence = net::nextSerial(s.damageSequence);
    net::DamageEvent e;
    e.eventId = s.damageSequence;
    const int attacker = source.attacker != nullptr ? serverSlotOf(*source.attacker) : -1;
    e.attackerSlot = attacker >= 0 ? static_cast<uint8_t>(attacker) : 0xFF;
    e.attackerGeneration = attacker >= 0 ? m_slots[attacker].occupant.generation : 0;
    e.damage = static_cast<uint16_t>(std::clamp(source.damage, 0, 0xFFFF));
    e.beam = source.beam >= 0 ? static_cast<uint8_t>(source.beam) : 0xFF;
    e.flags = static_cast<uint8_t>(flags & (DamageFlags::Headshot | DamageFlags::Deathalt | DamageFlags::Burn));
    // The knockback the engine applied, verbatim, within 1.5 (NetDamage.ClampImpulse).
    Vec3 impulse = source.impulse;
    const float length = std::sqrt(dot(impulse, impulse));
    if (!std::isfinite(length)) {
        impulse = {};
    } else if (length > 1.5f) {
        impulse = impulse * (1.5f / length);
    }
    e.direction = impulse;
    std::rotate(s.damage.begin(), s.damage.begin() + 1, s.damage.end());
    s.damage.back() = e;
    noteAuthorityHit(attacker, slot, source.launchFrame, source.damage);
    if (debugNet()) {
        qInfo("[sim] slot %d hit slot %d for %d (beam %d)%s", attacker, slot, source.damage, source.beam,
            victim.health() <= 0 ? ", lethal" : "");
    }
}

void ServerGame::numberLives()
{
    // NetPlayerLifecycle.OnSpawn: every spawn is a new life.
    for (int i = 0; i < net::SlotCapacity; i++) {
        Slot& s = m_slots[i];
        Player* p = worldPlayer(i);
        if (p == nullptr || p->spawnCount() == s.spawnCount) {
            continue;
        }
        s.spawnCount = p->spawnCount();
        if (p->health() <= 0) {
            continue;
        }
        s.life = net::nextSerial(s.life);
        s.spawnFrame = m_frame;
        s.aimHeld = true;
        s.hasIntent = false;
        s.pressSeen = s.reportSeen = s.mismatching = s.attempted = false;
        s.reportedSpeed = {};
        for (auto& cell : m_history[i]) {
            cell.inPlay = false;
        }
        if (debugNet()) {
            const Vec3& at = p->position();
            qInfo("[sim] slot %d life %u at (%.2f %.2f %.2f)", i, s.life, at[0], at[1], at[2]);
        }
    }
}

size_t ServerGame::composeSnapshot(uint32_t frame, uint16_t matchId, uint64_t epoch, std::span<uint8_t> out)
{
    // NetSession.BroadcastSnapshot
    World& w = *m_room->world;
    size_t offset = net::SnapshotHeader::Size;
    int count = 0;
    for (int i = 0; i < net::SlotCapacity; i++) {
        const Slot& s = m_slots[i];
        Player* p = worldPlayer(i);
        if (p == nullptr || !sane(p->position())) {
            continue;
        }
        const World::PlayerSlot& ws = w.m_slots[s.worldSlot];
        const bool inPlay = p->health() > 0;
        net::PlayerState state;
        state.slot = static_cast<uint8_t>(i);
        state.flags = static_cast<uint8_t>(net::PlayerState::FlagActive | (p->isAltForm() ? net::PlayerState::FlagAltForm : 0)
            | (inPlay ? net::PlayerState::FlagSpawned : 0) | (p->zoomed() ? net::PlayerState::FlagZoomed : 0)
            | (p->frozenTimer() > 0 ? net::PlayerState::FlagFrozen : 0) | (p->disruptedTimer() > 0 ? net::PlayerState::FlagDisrupted : 0)
            | (p->burnTimer() > 0 ? net::PlayerState::FlagBurning : 0) | (p->spectating() ? net::PlayerState::FlagSpectating : 0));
        state.position = p->position();
        state.speed = sane(p->speed()) ? p->speed() : Vec3{};
        state.facing = p->aimVector();
        state.health = static_cast<uint16_t>(std::clamp(p->health(), 0, 0xFFFF));
        state.currentWeapon = static_cast<uint8_t>(std::max(p->currentWeapon(), 0));
        state.team = static_cast<uint8_t>(w.teams() ? p->teamIndex() : 0);
        state.points = static_cast<int16_t>(std::clamp(ws.points, -32768, 32767));
        state.kills = static_cast<uint16_t>(std::clamp(ws.kills, 0, 0xFFFF));
        state.deaths = static_cast<uint16_t>(std::clamp(ws.deaths, 0, 0xFFFF));
        state.slotGeneration = s.occupant.generation;
        state.lifeId = s.life;
        state.damageEventId = s.damageSequence;
        state.damage = s.damage;
        if (state.health > 0 && state.lifeId == 0) {
            continue; // not numbered yet: a client would refuse it
        }
        state.write(out.subspan(offset));
        offset += net::PlayerState::Size;
        count++;
    }
    // NetMatchTimeSync
    for (int i = 0; i < net::SlotCapacity; i++) {
        const int ws = m_slots[i].worldSlot;
        const float time = ws >= 0 ? w.m_slots[ws].time : 0;
        net::writeF32(out, offset + i * 8, std::isfinite(time) ? std::max(time, -1.0f) : 0);
        net::writeF32(out, offset + i * 8 + 4, std::max(w.m_teamTime[i], -1.0f));
    }
    offset += net::MatchTimeSyncSize;
    // NetHealthSync: the health spawners, which only this machine picks up from.
    const size_t healthAt = offset;
    net::writeU16(out, offset, matchId);
    int spawns = 0;
    offset += net::HealthSyncHeaderSize;
    for (const ItemSpawn& spawn : w.m_items) {
        if (static_cast<int>(spawn.type) > 2 || spawn.id < 0 || spawns == net::HealthSyncMaxSpawns
            || offset + net::HealthSyncEntrySize > out.size()) {
            continue;
        }
        const bool available = spawn.item != nullptr && spawn.item->despawnTimer != 0;
        net::writeU16(out, offset, static_cast<uint16_t>(spawn.id));
        const int picker = spawn.lastPicker >= 0 && spawn.lastPicker < static_cast<int>(m_serverSlotOfWorld.size())
            ? m_serverSlotOfWorld[spawn.lastPicker] : -1;
        out[offset + 2] = static_cast<uint8_t>((available ? 1 : 0) | (spawn.active ? 2 : 0) | ((picker + 1) << 2)); // PickerSlot, +1
        net::writeU16(out, offset + 3, spawn.cooldown);
        net::writeU16(out, offset + 5, spawn.spawnCount);
        offset += net::HealthSyncEntrySize;
        spawns++;
    }
    out[healthAt + 2] = static_cast<uint8_t>(spawns);
    net::SnapshotHeader header;
    header.frame = frame;
    header.rng1 = rngState1();
    header.rng2 = rngState2();
    header.playerCount = static_cast<uint8_t>(count);
    header.matchId = matchId;
    header.authorityEpoch = epoch;
    header.write(out);
    return offset;
}

size_t ServerGame::step(uint32_t frame, uint16_t matchId, uint64_t epoch, std::span<uint8_t> out)
{
    if (!m_room) {
        return 0;
    }
    m_frame = frame;
    m_matchId = matchId;
    m_epoch = epoch;
    World& w = *m_room->world;
    syncSlots();
    m_input.assign(w.playerCount(), PlayerInput{});
    for (int i = 0; i < net::SlotCapacity; i++) {
        if (m_slots[i].worldSlot >= 0) {
            m_input[m_slots[i].worldSlot] = puppetInput(i);
        }
    }
    // World slot 0 is the World's "main player": its input is the tick's own.
    const bool slot0 = m_serverSlotOfWorld[0] >= 0;
    w.tick(slot0 ? m_input[0] : PlayerInput{}, slot0);
    restore();
    numberLives();
    tickClaims(); // what a claim rescues goes out in this step's snapshot
    const size_t length = composeSnapshot(frame, matchId, epoch, out);
    record(frame);
    return length;
}

bool ServerGame::over() const { return m_room && m_room->world->matchState() != MatchState::InProgress; }

float ServerGame::timeRemaining() const
{
    if (!m_room || m_room->world->matchState() != MatchState::InProgress) {
        return 0;
    }
    return m_room->world->matchTime();
}

std::string ServerGame::describe() const
{
    std::string text = m_roomKey + " (mode " + std::to_string(m_mode) + ")";
    if (m_shotsCompensated == 0) {
        text += ", lag compensation: nothing to compensate (history misses " + std::to_string(m_historyMisses) + ")";
    } else {
        char line[160];
        std::snprintf(line, sizeof line, ", lag compensation: %lld shots rewound, mean %.1f frames, worst %d, catch-up hits %lld, misses %lld",
            m_shotsCompensated, static_cast<double>(m_framesRewound) / m_shotsCompensated, m_worstRewind, m_catchUpHits, m_historyMisses);
        text += line;
    }
    if (m_claimsReceived > 0) {
        char line[400];
        std::snprintf(line, sizeof line,
            ", hit claims: %lld received, %lld applied (%lld damage, %lld kills, %lld headshots rescued), %lld already resolved, "
            "%lld from a shooter already dead, %lld on a victim already down, %lld refused, %lld too old, %lld repeats, %lld own shots refused",
            m_claimsReceived, m_claimsApplied, m_rescuedDamage, m_rescuedKills, m_rescuedHeadshots, m_claimsDuplicate, m_claimsDeadShooter,
            m_claimsDeadVictim, m_claimsRefused, m_claimsTooOld, m_claimsRepeats, m_suppressed);
        text += line;
    }
    return text;
}

// ---- NetUnlagged ---------------------------------------------------------------

void ServerGame::record(uint32_t frame)
{
    const size_t index = frame % HistoryFrames;
    m_stamp[index] = frame;
    for (int i = 0; i < net::SlotCapacity; i++) {
        HistoryCell& cell = m_history[i][index];
        Player* p = worldPlayer(i);
        cell.inPlay = p != nullptr && p->health() > 0;
        cell.life = m_slots[i].life;
        cell.generation = m_slots[i].occupant.generation;
        if (cell.inPlay) {
            cell.position = p->position();
            cell.alt = p->isAltForm();
        }
    }
}

bool ServerGame::reconcile(int exceptSlot, double targetFrame)
{
    // Everybody but the shooter where they were at `targetFrame`, between two
    // recorded frames when it falls between them.
    const auto frame = static_cast<uint32_t>(std::floor(targetFrame));
    const float fraction = static_cast<float>(targetFrame - frame);
    const size_t index = frame % HistoryFrames;
    if (frame == 0 || m_stamp[index] != frame) {
        return false;
    }
    const size_t next = (frame + 1) % HistoryFrames;
    const bool haveNext = fraction > 0.0001f && m_stamp[next] == frame + 1;
    restore();
    for (int i = 0; i < net::SlotCapacity; i++) {
        const HistoryCell& cell = m_history[i][index];
        Player* p = worldPlayer(i);
        if (i == exceptSlot || p == nullptr || p->health() <= 0 || !cell.inPlay || cell.life != m_slots[i].life
            || cell.generation != m_slots[i].occupant.generation) {
            continue;
        }
        Vec3 was = cell.position;
        const HistoryCell& then = m_history[i][next];
        if (haveNext && then.inPlay && then.alt == cell.alt && then.life == cell.life && then.generation == cell.generation) {
            const Vec3 travel = then.position - was;
            if (dot(travel, travel) <= 16) {
                was = was + travel * fraction;
            }
        }
        m_restore[i] = p->position();
        m_moved[i] = true;
        p->netPlace(inForm(*p, was, cell.alt));
    }
    m_reconciled = true;
    return true;
}

void ServerGame::restore()
{
    if (!m_reconciled) {
        return;
    }
    for (int i = 0; i < net::SlotCapacity; i++) {
        if (m_moved[i]) {
            m_moved[i] = false;
            if (Player* p = worldPlayer(i)) {
                p->netPlace(m_restore[i]);
            }
        }
    }
    m_reconciled = false;
}

void ServerGame::beginShot(Player& shooter)
{
    // NetUnlagged.BeginShot: the world the shooter saw, as far back as its ack
    // (and the fraction of a frame its playout clock was between two), within 45 frames.
    if (m_shotInProgress) {
        return;
    }
    m_shooter = -1;
    m_rewind = 0;
    const int slot = serverSlotOf(shooter);
    if (slot < 0 || !m_slots[slot].hasIntent) {
        return;
    }
    const net::Intent& intent = m_slots[slot].intent;
    if (intent.ackFrame == 0 || intent.ackFrame >= m_frame) {
        return;
    }
    double depth = static_cast<double>(m_frame - intent.ackFrame) - intent.ackSubFrame / 256.0;
    depth = std::min(depth, static_cast<double>(MaxRewindFrames));
    if (depth <= 0) {
        return;
    }
    if (!reconcile(slot, m_frame - depth)) {
        m_historyMisses++;
        return;
    }
    m_shooter = slot;
    m_rewind = static_cast<int>(std::ceil(depth));
    m_shotsCompensated++;
    m_framesRewound += std::lround(depth);
    m_worstRewind = std::max(m_worstRewind, static_cast<int>(std::lround(depth)));
    const auto& beams = m_room->world->m_slots[shooter.slot()].beams;
    for (size_t i = 0; i < beams.size(); i++) {
        m_beamsBefore[i] = beams[i].lifespan > 0;
    }
    m_shotInProgress = true;
}

void ServerGame::endShot(Player& shooter)
{
    // NetUnlagged.EndShot: the new shots catch up the frames they were fired
    // behind, each one against the world of its frame, then everybody goes back.
    const int slot = serverSlotOf(shooter);
    if (m_shooter < 0 || m_shooter != slot || m_rewind <= 0) {
        restore();
        m_shooter = -1;
        m_shotInProgress = false;
        return;
    }
    World& w = *m_room->world;
    auto& beams = w.m_slots[shooter.slot()].beams;
    std::array<bool, 16> fresh{};
    int count = 0;
    for (size_t i = 0; i < beams.size(); i++) {
        fresh[i] = !m_beamsBefore[i] && beams[i].lifespan > 0;
        count += fresh[i];
    }
    for (int step = 1; step <= m_rewind && count > 0; step++) {
        const uint32_t frame = m_frame - static_cast<uint32_t>(m_rewind - step);
        if (frame < m_frame) {
            if (!reconcile(slot, frame)) {
                m_historyMisses++;
                break;
            }
        } else {
            restore();
        }
        for (size_t i = 0; i < beams.size(); i++) {
            if (!fresh[i]) {
                continue;
            }
            BeamProjectile& beam = beams[i];
            const bool hit = beam.flags & BeamFlags::Collided;
            const bool alive = w.processBeam(beam);
            if (!alive) {
                beam.lifespan = 0;
            }
            if (!alive || (beam.flags & BeamFlags::Collided)) {
                if (!hit && (beam.flags & BeamFlags::Collided)) {
                    m_catchUpHits++;
                }
                fresh[i] = false;
                count--;
            }
        }
    }
    restore();
    m_shooter = -1;
    m_rewind = 0;
    m_shotInProgress = false;
}

} // namespace fp
