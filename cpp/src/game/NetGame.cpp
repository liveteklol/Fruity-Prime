#include "NetGame.h"

#include "World.h"
#include "audio/Sfx.h"
#include "audio/SoundIds.h"
#include "formats/Rooms.h"

#include <QStringList>
#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <ctime>

namespace fp {

namespace {

namespace B = net::Buttons;

bool debugNet() { return qEnvironmentVariableIsSet("FP_DEBUG_NET"); }

bool nonZero(const Vec3& v) { return v[0] != 0 || v[1] != 0 || v[2] != 0; }

} // namespace

NetGame::NetGame(std::unique_ptr<net::Client> client)
    : m_client(std::move(client))
    , m_prediction(std::make_unique<HitPrediction>(*m_client, [this](const Player* p) { return serverSlotOfPlayer(p); }))
{
}

int NetGame::serverSlotOfPlayer(const Player* player) const
{
    if (player == nullptr) {
        return -1;
    }
    if (player->slot() == 0) {
        return m_client->localSlot();
    }
    for (int slot = 0; slot < net::SlotCapacity; slot++) {
        if (slot != m_client->localSlot() && m_remotes[slot].worldSlot == player->slot()) {
            return slot;
        }
    }
    return -1;
}

NetGame::~NetGame() = default;

std::string NetGame::serverRoom() const { return m_client->match() ? m_client->match()->roomKey : std::string(); }

GameMode NetGame::serverMode() const
{
    const int mode = m_client->match() ? net::portMode(m_client->match()->mode) : -1;
    return mode < 0 ? GameMode::Battle : static_cast<GameMode>(mode);
}

bool NetGame::roomChanged() const
{
    const std::string room = serverRoom();
    return m_world != nullptr && !room.empty() && room != m_roomKey && m_client->shouldLoadMatch();
}

bool NetGame::waitingInLobby() const
{
    // NetSession.FreezeGameplay: a persistent lobby between matches, or a start this player is not in.
    return m_client->persistentLobby() && !m_client->shouldLoadMatch();
}

std::vector<std::string> NetGame::voteLines() const
{
    // MapVote.PromptLine and TallyLine.
    const auto& vote = m_client->vote();
    if (!vote || vote->state != net::VoteState::StateRunning) {
        return {};
    }
    std::string room = vote->roomKey;
    for (char& c : room) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    std::string tally = std::to_string(vote->yes) + "/" + std::to_string(vote->needed) + " OF " + std::to_string(vote->eligible) + "   "
        + std::to_string(vote->seconds) + "s";
    if (!m_client->voteAnswered()) {
        tally += "  F1 YES / F2 NO";
    }
    return {vote->proposer + " PROPOSES " + room, tally};
}

void NetGame::preloadModels(Scene& scene, const std::filesystem::path& root)
{
    // World::createSlot's models, for every hunter, and the parts of them that come and go.
    static constexpr const char* names[] = {"Samus_lod0", "SamusAlt_lod0", "SamusGun", "Kanden_lod0", "KandenAlt_lod0", "KandenGun",
        "KandenAlt_TailBomb", "Trace_lod0", "TraceAlt_lod0", "TraceGun", "Sylux_lod0", "SyluxAlt_lod0", "SyluxGun", "Nox_lod0",
        "NoxAlt_lod0", "NoxGun", "Spire_lod0", "SpireAlt_lod0", "SpireGun", "Weavel_lod0", "WeavelAlt_lod0", "WeavelGun",
        "WeavelAlt_Turret_lod0", "iceShard"};
    for (const char* name : names) {
        scene.model(root, name);
    }
}

void NetGame::attach(World& world, const std::string& roomKey)
{
    m_world = &world;
    m_roomKey = roomKey;
    m_remotes = {};
    m_freeWorldSlots.clear();
    m_lastMatchState = 0;
    world.setNetworked(true);
    world.setIntro(std::nullopt);
    // NetHitPrediction: this machine's own hits land here at once, and are claimed.
    m_prediction->forgetPending();
    world.setNetDamageHooks(m_prediction.get());
    world.setLaunchFrameSource([this](int) {
        // NetUnlagged.LaunchFrameFor: the point the playout clock is reading.
        uint32_t frame;
        uint8_t sub;
        if (m_client->smoothing().ackPoint(frame, sub)) {
            return frame;
        }
        return m_client->appliedSnapshotFrame() != 0 ? m_client->appliedSnapshotFrame() : m_client->frame();
    });
    // The local player waits for the server to spawn it.
    Player& local = *world.player();
    local.setNetReplica(true);
    local.despawn();
    world.setOtherInput([this](size_t slot, long long) { return slot < m_puppetInput.size() ? m_puppetInput[slot] : PlayerInput{}; });
    if (const auto& match = m_client->match()) {
        TeamRules& rules = teamRules();
        rules.friendlyFire = match->friendlyFire();
        world.m_pointGoal = match->pointGoal;
    }
    for (int slot = 0; slot < net::SlotCapacity; slot++) {
        m_client->takeSlotChanged(slot);
    }
    m_client->takeNewMatch();
    m_client->markMatchLoaded(); // a lobby's start waits for everybody's room
    qInfo("[net] playing %s online as slot %d", roomKey.c_str(), m_client->localSlot());
}

Player* NetGame::worldPlayer(int serverSlot)
{
    const int w = worldSlotOf(serverSlot);
    return w >= 0 && m_world != nullptr ? m_world->player(static_cast<size_t>(w)) : nullptr;
}

void NetGame::resetTrackers(int serverSlot)
{
    Remote& r = m_remotes[serverSlot];
    const int worldSlot = r.worldSlot, hunter = r.hunter;
    r = {};
    r.worldSlot = worldSlot;
    r.hunter = hunter;
}

void NetGame::releaseSlot(int serverSlot)
{
    m_prediction->forgetSlot(serverSlot);
    m_prediction->forgetClaims(serverSlot);
    Remote& r = m_remotes[serverSlot];
    if (r.worldSlot > 0) {
        m_world->player(r.worldSlot)->despawn();
        if (static_cast<size_t>(r.worldSlot) < m_puppetInput.size()) {
            m_puppetInput[r.worldSlot] = {};
        }
        m_freeWorldSlots.push_back(r.worldSlot);
        m_world->m_slots[r.worldSlot].active = false;
        if (debugNet()) {
            qInfo("[net] slot %d left (world slot %d)", serverSlot, r.worldSlot);
        }
    }
    r = {};
}

void NetGame::bindSlot(int serverSlot, int hunter)
{
    // A world slot for this server slot: one left by somebody who played the
    // same hunter, else a new one (its models were preloaded).
    int worldSlot = -1;
    for (size_t i = 0; i < m_freeWorldSlots.size(); i++) {
        if (static_cast<int>(m_world->player(m_freeWorldSlots[i])->hunter()) == hunter) {
            worldSlot = m_freeWorldSlots[i];
            m_freeWorldSlots.erase(m_freeWorldSlots.begin() + static_cast<long>(i));
            break;
        }
    }
    if (worldSlot < 0) {
        World::PlayerSlot& slot = m_world->createSlot(hunter);
        worldSlot = slot.player->slot();
        slot.player->despawn();
    }
    if (m_puppetInput.size() <= static_cast<size_t>(worldSlot)) {
        m_puppetInput.resize(worldSlot + 1);
    }
    Remote& r = m_remotes[serverSlot];
    r = {};
    r.worldSlot = worldSlot;
    r.hunter = hunter;
    m_world->m_slots[worldSlot].active = true;
    Player& p = *m_world->player(worldSlot);
    p.setNetReplica(true);
    p.setBot(false, 0);
    qInfo("[net] slot %d (%s) plays hunter %d, world slot %d", serverSlot, m_client->slotInfo()[serverSlot].name.c_str(), hunter,
        worldSlot);
}

void NetGame::syncSlots()
{
    // NetSlotManager.Sync: the scene's players follow the server's roster.
    const int local = m_client->localSlot();
    const auto& info = m_client->slotInfo();
    for (int slot = 0; slot < net::SlotCapacity; slot++) {
        const bool changed = m_client->takeSlotChanged(slot);
        if (slot == local) {
            if (changed) {
                resetTrackers(slot);
            }
            m_remotes[slot].worldSlot = 0;
            continue;
        }
        Remote& r = m_remotes[slot];
        if (r.worldSlot == 0) {
            r = {}; // was ours before a reconnection moved us
        }
        if (!info[slot].occupied) {
            if (r.worldSlot > 0) {
                releaseSlot(slot);
            }
            continue;
        }
        if (r.worldSlot > 0 && (changed || r.hunter != info[slot].hunter)) {
            releaseSlot(slot);
        }
        if (r.worldSlot < 0) {
            bindSlot(slot, std::clamp(info[slot].hunter, 0, 6));
        }
    }
    // Teams and suits, from the roster.
    const bool teams = m_world->teams();
    int teamCount = 2;
    for (int slot = 0; slot < net::SlotCapacity; slot++) {
        const int w = worldSlotOf(slot);
        if (w < 0 || !info[slot].occupied) {
            continue;
        }
        World::PlayerSlot& ws = m_world->m_slots[w];
        ws.name = info[slot].name;
        const int team = teams && info[slot].team >= 0 ? info[slot].team : w;
        teamCount = std::max(teamCount, info[slot].team + 1);
        if (ws.player->teamIndex() != team || ws.recolor != info[slot].color) {
            ws.player->setTeamIndex(team);
            ws.recolor = info[slot].color;
            m_world->applyTeamVisuals(ws);
        }
    }
    if (teams && m_world->m_mode != GameMode::Capture) {
        teamRules().teamCount = std::clamp(teamCount, 2, 4);
    }
}

void NetGame::netDie(Player& player, Player* attacker)
{
    // NetDamage.ReplayDeath: the engine's own death, which the authority has decided.
    Player::DamageReplay replay;
    DamageSource source;
    source.attacker = attacker;
    player.takeDamage(1, DamageFlags::Death | DamageFlags::NoDmgInvuln, nullptr, source);
}

void NetGame::beginLife(int serverSlot, const net::PlayerState& state)
{
    // NetPlayerBridge.BeginRemoteLife: a new life begins where the authority put it.
    Remote& r = m_remotes[serverSlot];
    r.lifeApplied = true;
    r.appliedLife = state.lifeId;
    r.pressSeen = false;
    r.mismatching = r.attempted = r.transitionSeen = r.transitionActive = false;
    r.damageSeen = true;
    r.damageLife = state.lifeId;
    r.damageGeneration = state.slotGeneration;
    r.damageLastSeen = state.damageEventId;
    m_prediction->noteRespawn(serverSlot);
    const int w = worldSlotOf(serverSlot);
    World::PlayerSlot& slot = m_world->m_slots[w];
    Player& p = *slot.player;
    if (state.lifeId != 0) {
        Vec3 facing = state.facing;
        if (!nonZero(facing)) {
            facing = {0, 0, -1};
        }
        // World::respawn's bookkeeping, then PlayerEntity.Spawn.
        slot.syluxBombs = {-1, -1, -1};
        slot.syluxBombCount = 0;
        p.spawn(state.position, facing, true);
        p.netSetFacing(facing);
        slot.lights = m_world->roomLights();
        if (w == 0) {
            m_world->m_introActive = false;
        }
        if (state.health == 0) {
            netDie(p, nullptr);
        }
        if (debugNet()) {
            qInfo("[net] slot %d life %u at (%.2f %.2f %.2f) health %u", serverSlot, state.lifeId, state.position[0], state.position[1],
                state.position[2], state.health);
        }
    }
    p.netSetHealth(state.health);
}

void NetGame::replayDamage(int serverSlot, const net::PlayerState& state)
{
    // NetDamage.Replay: every hit the authority resolved on this player that
    // has not been shown here, with its attacker, its knockback and its flags.
    Remote& r = m_remotes[serverSlot];
    Player* player = worldPlayer(serverSlot);
    if (player == nullptr) {
        return;
    }
    if (!r.damageSeen || r.damageLife != state.lifeId || r.damageGeneration != state.slotGeneration) {
        r.damageSeen = true;
        r.damageLife = state.lifeId;
        r.damageGeneration = state.slotGeneration;
        r.damageLastSeen = state.damageEventId;
        return;
    }
    for (const net::DamageEvent& hit : state.damage) {
        if (hit.eventId == 0 || (r.damageLastSeen != 0 && !net::newer16(hit.eventId, r.damageLastSeen))) {
            continue;
        }
        r.damageLastSeen = hit.eventId;
        const bool knownAttacker = hit.attackerSlot < net::SlotCapacity && hit.attackerGeneration != 0
            && m_client->life(hit.attackerSlot).generation() == hit.attackerGeneration;
        const int health = hit.eventId == state.damageEventId ? state.health : std::max(1, player->health() - hit.damage);
        const bool lethal = health == 0;
        // A hit this machine already showed is consumed, not shown twice
        // (before the "already down" test: a victim this machine killed is down here).
        const bool mine = knownAttacker && hit.attackerSlot == m_client->localSlot();
        const bool predicted = mine && m_prediction->confirm(serverSlot, 1, (hit.flags & DamageFlags::Headshot) != 0);
        if (player->health() <= 0) {
            continue; // already down here
        }
        if (predicted && !lethal) {
            continue;
        }
        Player* attacker = knownAttacker ? worldPlayer(hit.attackerSlot) : nullptr;
        int amount = std::max(1, player->health() - health);
        if (!lethal) {
            amount = std::min(amount, std::max(0, player->health() - 1));
        }
        uint32_t flags = hit.flags | DamageFlags::NoDmgInvuln;
        if (lethal) {
            flags |= DamageFlags::Death;
        }
        DamageSource source;
        source.attacker = attacker;
        source.beam = hit.beam == 0xFF ? -1 : hit.beam;
        Vec3 impulse = hit.direction;
        const float length = std::sqrt(dot(impulse, impulse));
        if (!std::isfinite(length)) {
            impulse = {};
        } else if (length > 1.5f) {
            impulse = impulse * (1.5f / length); // NetDamage.ClampImpulse
        }
        Player::DamageReplay replay;
        player->takeDamage(amount, flags, nonZero(impulse) ? &impulse : nullptr, source);
        if (debugNet()) {
            qInfo("[net] slot %d hit by %d for %d (%s) beam %d", serverSlot, hit.attackerSlot, amount, lethal ? "lethal" : "health left",
                source.beam);
        }
    }
}

int NetGame::reconcileForm(Remote& r, bool desiredAlt, const Player& p, int ping)
{
    // FormReconciliation.Step: 0 nothing, 1 start the switch, 2 force the form.
    const uint32_t frame = m_client->frame();
    const bool morphing = p.isMorphing(), unmorphing = p.isUnmorphing();
    const bool transitioning = morphing || unmorphing;
    if (transitioning) {
        if (!r.transitionActive || r.transitionTarget != morphing) {
            r.transitionSince = frame;
        }
        r.transitionSeen = true;
        r.transitionTarget = morphing;
        r.transitionLastSeen = frame;
    }
    r.transitionActive = transitioning;
    const bool actualAlt = p.isAltForm();
    auto reset = [&] { r.mismatching = r.attempted = r.transitionSeen = r.transitionActive = false; };
    if (actualAlt == desiredAlt && !transitioning) {
        r.mismatching = r.attempted = false;
        return 0;
    }
    if (transitioning) {
        if (frame - r.transitionSince >= 90) {
            reset();
            return 2;
        }
        return 0;
    }
    const uint32_t latencyGrace = static_cast<uint32_t>(std::clamp(ping * 60 / 1000 + 8, 8, 32));
    if (r.transitionSeen && !r.attempted && r.transitionTarget == actualAlt && frame - r.transitionLastSeen <= latencyGrace) {
        return 0;
    }
    if (r.attempted) {
        if (frame - r.attemptSince < 12) {
            return 0;
        }
        reset();
        return 2;
    }
    if (!r.mismatching) {
        r.mismatchSince = frame;
        r.mismatching = true;
    }
    if (frame - r.mismatchSince < 8) {
        return 0;
    }
    r.attempted = true;
    r.attemptSince = frame;
    return 1;
}

void NetGame::applyState(int serverSlot, const net::PlayerState& state)
{
    // NetPlayerBridge.ApplyState
    Remote& r = m_remotes[serverSlot];
    const int w = worldSlotOf(serverSlot);
    if (w < 0 || !m_client->lifeMatches(serverSlot, state.slotGeneration, state.lifeId)) {
        return;
    }
    const bool isLocal = serverSlot == m_client->localSlot();
    World::PlayerSlot& slot = m_world->m_slots[w];
    Player& p = *slot.player;
    const bool fresh = !r.lifeApplied || r.appliedLife != state.lifeId;
    if (fresh) {
        beginLife(serverSlot, state);
    }
    const bool spawned = (state.flags & net::PlayerState::FlagSpawned) && state.health > 0;
    if (m_world->m_matchState == MatchState::InProgress) {
        slot.points = state.points;
        slot.kills = state.kills;
        slot.deaths = state.deaths;
        const float time = m_client->slotTime()[serverSlot];
        slot.time = time;
    }
    replayDamage(serverSlot, state);
    if (!isLocal) {
        p.netSetSpectating((state.flags & net::PlayerState::FlagSpectating) != 0);
    }
    if (!spawned) {
        if (state.health == 0) {
            m_prediction->noteDeath(serverSlot);
            if (p.health() > 0) {
                netDie(p, nullptr);
            }
        }
        p.netSetHealth(state.health);
        return;
    }
    if (!fresh && p.health() <= 0) {
        return; // only a new life stands a player back up
    }
    if (!isLocal) {
        p.netSetSpeed(state.speed);
        p.netSetHealth(m_prediction->healthFor(serverSlot, state.health));
        p.netSetFacing(state.facing);
        p.netSetWeapon(state.currentWeapon);
        p.netSetZoom((state.flags & net::PlayerState::FlagZoomed) != 0);
        const bool alt = (state.flags & net::PlayerState::FlagAltForm) != 0;
        switch (reconcileForm(r, alt, p, m_client->slotInfo()[serverSlot].ping)) {
        case 1:
            p.netStartFormSwitch();
            break;
        case 2:
            p.netForceForm(alt);
            break;
        default:
            break;
        }
    } else {
        if (!fresh && diverged(p, state)) {
            const bool alt = (state.flags & net::PlayerState::FlagAltForm) != 0;
            const Vec3 position = alt != p.isAltForm() ? (alt ? state.position - p.formOffset() : state.position + p.formOffset()) : state.position;
            qInfo("[net] this player was %.1f units from where the server has it: moved there", std::sqrt(dot(position - p.position(), position - p.position())));
            p.netPlace(position);
            p.netSetSpeed(state.speed);
            m_divergedFrames = 0;
        }
        p.netSetHealth(m_prediction->localHealthFor(p, state.health));
    }
    p.netSetAfflictions((state.flags & net::PlayerState::FlagFrozen) != 0, (state.flags & net::PlayerState::FlagDisrupted) != 0,
        (state.flags & net::PlayerState::FlagBurning) != 0);
}

void NetGame::applyStates()
{
    for (int slot = 0; slot < net::SlotCapacity; slot++) {
        if (const net::PlayerState* state = m_client->state(slot)) {
            applyState(slot, *state);
        }
    }
    m_client->noteStatesApplied();
}

void NetGame::pinPuppets()
{
    // NetPlayerBridge.RestoreSnapshotPosition: every other player where the
    // playout clock reads it, before the step (so this tick's shots test
    // against it) and after (the engine's own movement step moved it).
    const int local = m_client->localSlot();
    for (int slot = 0; slot < net::SlotCapacity; slot++) {
        if (slot == local || m_remotes[slot].worldSlot <= 0) {
            continue;
        }
        Player& p = *m_world->player(m_remotes[slot].worldSlot);
        if (p.health() <= 0) {
            continue;
        }
        Vec3 position;
        bool alt = false;
        if (!m_client->smoothing().sample(slot, m_client->lives(), position, alt)) {
            const net::PlayerState* state = m_client->state(slot);
            if (state == nullptr || !nonZero(state->position) || !m_client->lifeMatches(slot, state->slotGeneration, state->lifeId)) {
                continue;
            }
            position = state->position;
            alt = (state->flags & net::PlayerState::FlagAltForm) != 0;
        }
        // A position measured in the other form's frame of reference.
        if (alt != p.isAltForm()) {
            position = alt ? position - p.formOffset() : position + p.formOffset();
        }
        p.netPlace(position);
    }
}

PlayerInput NetGame::puppetInput(int serverSlot)
{
    // NetPlayerBridge.ApplyIntent: held buttons from the levels, presses from
    // the history, each press taken once.
    PlayerInput in;
    const net::Intent* intent = m_client->intent(serverSlot);
    Remote& r = m_remotes[serverSlot];
    if (intent == nullptr || m_client->intentAge(serverSlot) > 30 || !m_client->lifeMatches(serverSlot, intent->slotGeneration, intent->lifeId)) {
        return in;
    }
    uint32_t pressed = 0;
    if (!r.pressSeen) {
        r.pressSeen = true;
        r.lastPressFrame = intent->frame;
    } else {
        for (int i = net::Intent::PressHistory - 1; i >= 0; i--) {
            if (intent->frame < static_cast<uint32_t>(i)) {
                continue;
            }
            const uint32_t frame = intent->frame - static_cast<uint32_t>(i);
            if (frame <= r.lastPressFrame) {
                continue;
            }
            pressed |= intent->presses[i];
        }
        r.lastPressFrame = std::max(r.lastPressFrame, intent->frame);
    }
    if (!(intent->buttons & B::InPlayState)) {
        return in; // a dead player's fire is a respawn request, not a shot
    }
    const uint32_t held = intent->buttons | pressed;
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
    in.hasInput = (intent->buttons & ~B::States) != 0 || pressed != 0;
    if (Player* p = worldPlayer(serverSlot)) {
        // The owner's own count: only it collects the pickups that refill it.
        p->setAmmo(0, intent->ammoUa);
        p->setAmmo(1, intent->ammoMissiles);
        if (intent->weaponSelect != 0xFF) {
            p->netSetWeapon(intent->weaponSelect);
        }
    }
    return in;
}

void NetGame::applyMatchClock()
{
    // NetMatchSync: the server's clock, and its word on when the match is over.
    const auto& match = m_client->match();
    if (!match) {
        return;
    }
    World& w = *m_world;
    if (m_client->matchStateCount() != m_lastMatchState) {
        m_lastMatchState = m_client->matchStateCount();
        w.m_pointGoal = match->pointGoal;
        teamRules().friendlyFire = match->friendlyFire();
        if (w.m_matchState == MatchState::InProgress && !match->ending()) {
            w.m_matchTime = match->timeRemaining > 0 ? match->timeRemaining : -1;
        }
    }
    if (w.m_matchState == MatchState::InProgress) {
        if (match->ending()) {
            w.m_matchTime = 0; // GAME OVER now
        } else if (w.m_matchTime >= 0 && w.m_matchTime < 2 / 60.0f) {
            w.m_matchTime = 2 / 60.0f; // the server says when it is over
        }
    } else if (w.m_matchState == MatchState::Ending && w.m_matchTime < 1 / 60.0f) {
        w.m_matchTime = 1 / 60.0f; // the results stay up until the next match starts
    }
    for (int team = 0; team < net::SlotCapacity; team++) {
        w.m_teamTime[team] = m_client->teamTime()[team];
    }
}

void NetGame::applyHealthSpawns()
{
    // NetHealthSync: the health pickups are where the server says.
    for (const net::HealthSpawnState& state : m_client->healthSpawns()) {
        for (ItemSpawn& spawn : m_world->m_items) {
            if (spawn.id != state.id || static_cast<int>(spawn.type) > 2) {
                continue;
            }
            spawn.cooldown = state.cooldown;
            spawn.spawnCount = state.spawnCount;
            if (state.available && spawn.item == nullptr) {
                spawn.instance.despawnTimer = -1;
                spawn.instance.owner = &spawn;
                spawn.item = &spawn.instance;
                m_world->m_scene.instances[spawn.instance.modelInstance].visible = true;
                spawn.instance.sound->update(spawn.instance.position, 7);
                spawn.instance.sound->playSfx(SfxId::ITEM_SPAWN1);
            } else if (!state.available && spawn.item != nullptr) {
                if (state.pickerSlot >= 0 && state.pickerSlot == m_client->localSlot()) {
                    // This player took it on the server: the pickup's sound, here.
                    Sfx::instance().playFreeSfx(spawn.type == ItemType::HealthSmall ? SfxId::POWER_UP1 : SfxId::POWER_UP2);
                }
                m_world->m_scene.instances[spawn.item->modelInstance].visible = false;
                spawn.item->sound->stopAllSfx(true);
                spawn.item = nullptr;
            }
        }
    }
}

void NetGame::recordLocalPresses(const PlayerInput& input)
{
    // NetPlayerBridge.RecordPresses: every tick's rising edges, and the charge
    // a release is about to spend.
    const Player& p = *m_world->player();
    uint32_t held = 0;
    if (input.forward) {
        held |= B::MoveUp;
    }
    if (input.back) {
        held |= B::MoveDown;
    }
    if (input.left) {
        held |= B::MoveLeft;
    }
    if (input.right) {
        held |= B::MoveRight;
    }
    uint32_t pressed = held & ~m_heldLast;
    m_heldLast = held;
    if (input.shootPressed) {
        pressed |= B::Shoot;
    }
    if (input.zoomPressed) {
        pressed |= B::Zoom;
    }
    if (input.jumpPressed) {
        pressed |= B::Jump;
    }
    if (input.morphPressed) {
        pressed |= B::Morph;
    }
    if (input.boostHeld && !m_boostHeldLast) {
        pressed |= B::Boost;
    }
    if (input.altAttackPressed) {
        pressed |= B::AltAttack;
    }
    if (p.health() <= 0) {
        m_pressHistory = {};
        m_hasLatch = false;
    } else {
        std::rotate(m_pressHistory.rbegin(), m_pressHistory.rbegin() + 1, m_pressHistory.rend());
        m_pressHistory[0] = pressed;
    }
    const bool shootReleased = m_shootHeldLast && !input.shootHeld;
    const bool boostReleased = m_boostHeldLast && !input.boostHeld;
    if (shootReleased || boostReleased || input.altAttackPressed) {
        m_latchedCharge = p.chargeLevel();
        m_latchedBoost = p.boostDamage();
        m_hasLatch = true;
    }
    m_shootHeldLast = input.shootHeld;
    m_boostHeldLast = input.boostHeld;
}

void NetGame::sendLocalIntent(const PlayerInput& input)
{
    // NetPlayerBridge.CaptureIntent
    const Player& p = *m_world->player();
    net::Intent intent;
    uint32_t buttons = m_heldLast;
    if (input.shootHeld || input.shootPressed) {
        buttons |= B::Shoot;
    }
    if (input.jumpPressed) {
        buttons |= B::Jump;
    }
    if (input.morphPressed) {
        buttons |= B::Morph;
    }
    if (input.boostHeld) {
        buttons |= B::Boost;
    }
    if (input.altAttackHeld || input.altAttackPressed) {
        buttons |= B::AltAttack;
    }
    if (p.zoomed()) {
        buttons |= B::ZoomedState;
    }
    if (p.isAltForm()) {
        buttons |= B::AltFormState;
    }
    if (p.health() > 0) {
        buttons |= B::InPlayState;
    }
    if (p.spectating()) {
        buttons |= B::SpectatingState; // watching: hidden, not solid, nobody's target
    }
    if (m_world->matchFinished()) {
        buttons |= B::ReadyState; // the results are over here: the server need not wait for this player
    }
    intent.buttons = buttons;
    intent.presses = m_pressHistory;
    intent.aim = p.aimVector();
    intent.position = p.position();
    intent.weaponSelect = static_cast<uint8_t>(p.currentWeapon());
    intent.ammoUa = static_cast<uint16_t>(std::clamp(p.ammo(0), 0, 0xFFFF));
    intent.ammoMissiles = static_cast<uint16_t>(std::clamp(p.ammo(1), 0, 0xFFFF));
    intent.chargeLevel = static_cast<uint8_t>(std::clamp(m_hasLatch ? m_latchedCharge : p.chargeLevel(), 0, 255));
    intent.boostDamage = static_cast<uint8_t>(std::clamp(m_hasLatch ? m_latchedBoost : p.boostDamage(), 0, 255));
    intent.shotFlags = static_cast<uint8_t>((p.doubleDamageTimer() > 0 ? net::Intent::FlagDoubleDamage : 0)
        | (p.primeHunter ? net::Intent::FlagPrimeHunter : 0));
    m_hasLatch = false;
    m_client->sendIntent(intent);
}

void NetGame::tick(const PlayerInput& input, bool walking)
{
    m_ticks++;
    m_client->update();
    for (const net::Chat& chat : m_client->takeChat()) {
        const std::string line = chat.kind == net::Chat::KindSystem || chat.name.empty() ? chat.text : chat.name + ": " + chat.text;
        qInfo("[chat] %s", line.c_str());
        m_chat.push_back({line, m_ticks});
        if (m_chat.size() > 6) {
            m_chat.erase(m_chat.begin());
        }
    }
    if (m_world == nullptr || (m_client->localSlot() < 0 && !m_client->playback()) || roomChanged() || waitingInLobby()) {
        return; // waiting to be let in, in the lobby, or for the caller to load the new room
    }
    if (m_client->persistentLobby() && m_client->session()->phase == net::SessionState::PhaseStarting) {
        m_client->markMatchLoaded(); // this room is the match's: ready when everybody is
        return;
    }
    World& w = *m_world;
    if (m_client->takeNewMatch()) {
        // A new round in the same room: everybody waits for the server to place them.
        w.restartMatch();
        m_prediction->forgetPending();
        for (int slot = 0; slot < net::SlotCapacity; slot++) {
            resetTrackers(slot);
        }
        m_lastMatchState = 0;
        qInfo("[net] new match %u on %s", m_client->matchId(), m_roomKey.c_str());
    }
    syncSlots();
    applyStates();
    const int local = m_client->localSlot();
    for (int slot = 0; slot < net::SlotCapacity; slot++) {
        const int ws = m_remotes[slot].worldSlot;
        if (slot != local && ws > 0) {
            m_puppetInput[ws] = puppetInput(slot);
        }
    }
    pinPuppets();
    applyMatchClock();
    applyHealthSpawns();
    if (m_client->playback()) {
        // A recording has no player of this machine's: everybody is somebody else.
        w.player()->netSetSpectating(true);
    }
    if (!m_placed && w.player()->health() > 0) {
        // FP_NET_PLACE=x,y,z,yaw (testing): stand somewhere after the first spawn.
        // The server takes a player's position from its own report.
        m_placed = true;
        if (const QStringList at = qEnvironmentVariable("FP_NET_PLACE").split(','); at.size() == 4) {
            w.player()->netPlace({at[0].toFloat(), at[1].toFloat(), at[2].toFloat()});
            w.player()->setAim(at[3].toFloat(), 0);
        }
    }
    recordLocalPresses(input);
    for (const std::vector<uint8_t>& verdicts : m_client->takeVerdicts()) {
        m_prediction->applyVerdicts(verdicts);
    }
    w.tick(input, walking);
    {
        const uint32_t frame = m_client->frame();
        m_ownFrames[frame % m_ownFrames.size()] = frame;
        m_ownPositions[frame % m_ownPositions.size()] = w.player()->position();
    }
    m_prediction->tick();
    m_client->smoothing().tick();
    applyStates();
    pinPuppets();
    if (m_client->playback()) {
        return;
    }
    sendLocalIntent(input);
    std::array<uint8_t, 1 + net::HitClaim::Size * net::HitClaim::MaxPerPacket> claims{};
    if (const size_t length = m_prediction->composeClaims(claims); length > 0) {
        m_client->sendHitClaims(std::span<const uint8_t>(claims.data(), length));
    }
    if (debugNet() && m_ticks % 300 == 0) {
        qInfo("%s", status().c_str());
    }
}

bool NetGame::diverged(const Player& player, const net::PlayerState& state)
{
    // Compared with where this player was when the server was looking -- a
    // ping back -- not with where it is now: a fall covers thirty units in the
    // half second a slow line takes to answer.
    constexpr float DesyncDistance = 30;
    const int slot = m_client->localSlot();
    Vec3 then = player.position();
    const int lagFrames = std::clamp(m_client->slotInfo()[slot].ping * 60 / 1000, 0, 100);
    const uint32_t frame = m_client->frame();
    if (lagFrames > 0 && frame > static_cast<uint32_t>(lagFrames)) {
        const uint32_t wanted = frame - static_cast<uint32_t>(lagFrames);
        const size_t at = wanted % m_ownFrames.size();
        if (m_ownFrames[at] == wanted) {
            then = m_ownPositions[at];
        }
    }
    const Vec3 gap = state.position - then;
    if (dot(gap, gap) <= DesyncDistance * DesyncDistance) {
        m_divergedFrames = 0;
        return false;
    }
    return ++m_divergedFrames >= 60;
}

bool NetGame::toggleRecording(const std::filesystem::path& directory)
{
    if (m_client->recording()) {
        m_client->stopRecording();
        m_chat.push_back({"DEMO SAVED", m_ticks});
        return false;
    }
    // DemoRecorder.Start: ROOM_date.fpdemo in the demos folder.
    std::string room = m_roomKey;
    for (char& c : room) {
        if (!std::isalnum(static_cast<unsigned char>(c))) {
            c = '_';
        }
    }
    const std::time_t t = std::time(nullptr);
    char stamp[32];
    std::strftime(stamp, sizeof stamp, "%Y-%m-%d_%H-%M-%S", std::localtime(&t));
    const std::filesystem::path path = directory / (room + "_" + stamp + net::DemoExtension);
    const bool ok = m_client->startRecording(path);
    m_chat.push_back({ok ? "RECORDING A DEMO (F9 TO STOP)" : "CANNOT RECORD A DEMO HERE", m_ticks});
    return ok;
}

std::vector<std::string> NetGame::recentChat() const
{
    std::vector<std::string> lines;
    for (const ChatLine& line : m_chat) {
        if (m_ticks - line.tick < 10 * 60) {
            lines.push_back(line.text);
        }
    }
    return lines;
}

void NetGame::say(const std::string& text)
{
    if (text.empty()) {
        return;
    }
    if (text.rfind("/vote ", 0) == 0) {
        // callvote map: proposed to the server, which asks everybody.
        m_client->sendVote(net::Vote::KindPropose, text.substr(6));
        return;
    }
    m_client->sendChat(text);
    const int slot = m_client->localSlot();
    const std::string name = slot >= 0 ? m_client->slotInfo()[slot].name : std::string();
    m_chat.push_back({name.empty() ? text : name + ": " + text, m_ticks});
    if (m_chat.size() > 6) {
        m_chat.erase(m_chat.begin());
    }
}

std::string NetGame::trouble() const
{
    if (!m_client->connected()) {
        return "DISCONNECTED";
    }
    if (waitingInLobby()) {
        return m_client->lobbyOwner() ? "LOBBY: YOU OWN IT - START THE MATCH" : "LOBBY: WAITING FOR THE OWNER TO START";
    }
    if (m_client->connectionLost()) {
        return "CONNECTION LOST - RETRYING";
    }
    if (m_client->localSlot() >= 0 && m_client->snapshotAge() > 60 && m_client->snapshotAge() != 0xFFFFFFFF) {
        return "WAITING FOR THE SERVER";
    }
    return {};
}

std::string NetGame::status() const
{
    const net::Client& c = *m_client;
    std::string text = "[net] " + c.serverName() + " slot " + std::to_string(c.localSlot());
    if (c.localSlot() >= 0) {
        text += " ping " + std::to_string(c.slotInfo()[c.localSlot()].ping) + " ms";
    }
    text += ", snapshots " + std::to_string(c.snapshotsReceived()) + " (age " + std::to_string(c.snapshotAge()) + "), delay "
        + std::to_string(c.smoothing().delay()) + ", starved " + std::to_string(c.smoothing().starved());
    if (c.connectionLost()) {
        text += ", CONNECTION LOST";
    }
    return text;
}

} // namespace fp
