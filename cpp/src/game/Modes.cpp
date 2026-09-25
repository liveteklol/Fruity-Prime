// The objects of the objective modes: OctolithFlagEntity and FlagBaseEntity
// (Capture, Bounty), NodeDefenseEntity (Nodes, Defender).

#include "World.h"
#include "audio/Music.h"

#include "formats/Enums.h"

#include <QtGlobal>

#include <algorithm>
#include <cmath>

namespace fp {

namespace {

constexpr float kFrameTime = 1 / 60.0f;

namespace Offsets {
constexpr size_t OctolithTeamId = 40;
constexpr size_t FlagBaseTeamId = 40, FlagBaseVolume = 44;
constexpr size_t NodeDefenseVolume = 40;
constexpr size_t VolumeCylinderRadius = 28; // RawCollisionVolume.CylinderRadius
} // namespace Offsets

// EntityBase.ConstantAcceleration: the new velocity and how far it went this tick.
std::pair<float, float> constantAcceleration(float step, float velocity, float minVelocity = -1e30f, float maxVelocity = 1e30f)
{
    const float newVelocity = std::clamp(velocity + step * 30 * 31 * kFrameTime, minVelocity, maxVelocity);
    const float displacement = velocity * kFrameTime + (newVelocity - velocity) / 2 * kFrameTime;
    return {newVelocity, displacement};
}

// CollisionDetection.CheckCylinderBetweenPoints: whether the segment point1-point2
// passes within `radii` of the upright cylinder hanging `cylHeight` down from cylPos.
bool cylinderBetweenPoints(const Vec3& point1, const Vec3& point2, const Vec3& cylPos, float cylHeight, float radii)
{
    Vec3 travel = point2 - point1;
    const float length = std::sqrt(dot(travel, travel));
    if (!(length > 0)) {
        return false; // the C# divides by zero here, and every test after fails on the NaN
    }
    travel = travel * (1 / length);
    const Vec3 vec1 = cylPos - point1;
    const float d = dot(travel, vec1);
    if (d < -radii || d > length + radii) {
        return false;
    }
    const Vec3 vec2 = vec1 - travel * d;
    if (vec2[1] > 0 || vec2[1] < -cylHeight) {
        return false;
    }
    return vec2[0] * vec2[0] + vec2[2] * vec2[2] <= radii * radii;
}

// Fixed.ToInt
int toFixed(float value) { return static_cast<int>(value * 4096); }

} // namespace

void World::mainMessage(int messageId, float duration, int category, float y, bool red)
{
    HudEvent event{HudEvent::Message};
    event.messageId = messageId;
    event.duration = duration;
    event.category = category;
    event.y = y;
    event.red = red;
    m_hudEvents.push_back(event);
}

void World::setUpModeEntities()
{
    // The entity constructors: each object has its models only in the modes
    // that use it (Sic Transit's layers, for one, have Octoliths in Defender).
    m_octolithFlags.clear();
    m_flagBases.clear();
    m_nodeDefenses.clear();
    auto add = [&](const char* name, const Mat4& transform, int recolor) -> std::optional<size_t> {
        const Model* model = m_scene.model(m_root, name);
        if (model == nullptr) {
            return std::nullopt;
        }
        ModelInstance inst;
        inst.model = model;
        inst.transform = transform;
        inst.recolor = recolor >= 0 && recolor < static_cast<int>(model->recolorCount()) ? recolor : 0;
        inst.animation.set(model->animations(), 0);
        m_scene.instances.push_back(inst);
        return m_scene.instances.size() - 1;
    };
    const bool capture = m_mode == GameMode::Capture;
    for (const Entity& e : m_modeEntities) {
        const Mat4 transform = Mat4::fromVectors(e.facing, e.up, e.position);
        if (e.type == EntityType::OctolithFlag && octolithMode()) {
            OctolithFlag flag;
            flag.teamId = e.u8(Offsets::OctolithTeamId);
            flag.basePosition = e.position;
            // Recolor: the team's in Capture, the Bounty one otherwise.
            auto inst = add("octolith_ctf", transform, capture ? flag.teamId : 2);
            if (!inst) {
                continue;
            }
            flag.instance = *inst;
            flag.baseInstance = add(capture ? "flagbase_ctf" : "flagbase_bounty", transform, capture ? flag.teamId : 0);
            octolithAtBase(flag);
            m_octolithFlags.push_back(flag);
        } else if (e.type == EntityType::FlagBase && octolithMode()) {
            FlagBase base;
            base.teamId = static_cast<int>(e.u32(Offsets::FlagBaseTeamId));
            base.position = e.position;
            base.volume = TriggerVolume::fromRaw(e, Offsets::FlagBaseVolume, e.position);
            if (!capture) {
                add("flagbase_cap", transform, 0); // invisible in Capture
            }
            m_flagBases.push_back(base);
        } else if (e.type == EntityType::NodeDefense && (nodesMode() || defenderMode())) {
            NodeDefense defense;
            defense.position = e.position;
            defense.volume = TriggerVolume::fromRaw(e, Offsets::NodeDefenseVolume, e.position);
            defense.circleScale = fxToFloat(e.i32(Offsets::NodeDefenseVolume + Offsets::VolumeCylinderRadius));
            // Yes, these names are the right way round.
            auto terminal = add("koth_data_flow", transform, 0);
            auto ring = add("koth_terminal", transform, 0);
            if (!terminal || !ring) {
                continue;
            }
            defense.terminalInstance = *terminal;
            defense.ringInstance = *ring;
            m_nodeDefenses.push_back(defense);
        }
    }
    if (m_nodeData != nullptr && m_nodeData->Simple()) {
        // SceneSetup.LoadNodeData
        for (OctolithFlag& flag : m_octolithFlags) {
            flag.closestNode = m_nodeData->findClosestNode(flag.position);
            flag.baseClosestNode = m_nodeData->findClosestNode(flag.basePosition);
        }
        for (FlagBase& base : m_flagBases) {
            base.closestNode = m_nodeData->findClosestNode(base.position);
        }
        for (NodeDefense& defense : m_nodeDefenses) {
            defense.closestNode = m_nodeData->findClosestNode(defense.position);
        }
    }
    updateModeModels();
}

void World::resetModeEntities()
{
    // A new match: the Octoliths home, the rings nobody's.
    for (OctolithFlag& flag : m_octolithFlags) {
        octolithAtBase(flag);
    }
    for (NodeDefense& defense : m_nodeDefenses) {
        const TriggerVolume volume = defense.volume;
        NodeDefense fresh;
        fresh.position = defense.position;
        fresh.volume = volume;
        fresh.circleScale = defense.circleScale;
        fresh.terminalInstance = defense.terminalInstance;
        fresh.ringInstance = defense.ringInstance;
        fresh.closestNode = defense.closestNode;
        defense = fresh;
    }
    m_teamTime.fill(0);
    updateModeModels();
}

void World::processModeEntities()
{
    for (OctolithFlag& flag : m_octolithFlags) {
        processOctolithFlag(flag);
    }
    for (FlagBase& base : m_flagBases) {
        processFlagBase(base);
    }
    for (NodeDefense& defense : m_nodeDefenses) {
        if (defenderMode()) {
            processDefender(defense);
        } else {
            processNodes(defense);
        }
    }
    updateModeModels();
}

// ---- OctolithFlagEntity ---------------------------------------------------------------------

void World::octolithAtBase(OctolithFlag& flag)
{
    // SetAtBase
    flag.position = flag.basePosition + Vec3{0, 1.25f, 0};
    flag.atBase = true;
    flag.grounded = true;
    flag.resetTimer = 0;
    flag.gravity = 0;
    if (flag.carrier != nullptr) {
        flag.carrier->octolithFlag = nullptr;
        flag.carrier = nullptr;
    }
    flag.lastCarrier = nullptr;
    flag.closestNode = flag.baseClosestNode;
}

void World::processOctolithFlag(OctolithFlag& flag)
{
    const bool bounty = m_mode != GameMode::Capture;
    const Player& main = *m_slots[0].player;
    bool pickedUp = false;
    if (flag.carrier == nullptr) {
        for (PlayerSlot& slot : m_slots) {
            Player& player = *slot.player;
            if (player.health() == 0 || player.isAltForm() || player.isMorphing()
                || (!bounty && player.teamIndex() == flag.teamId && flag.atBase)) {
                continue;
            }
            const float max = player.values().MaxPickupHeight / 4096.0f;
            const float min = player.values().MinPickupHeight / 4096.0f;
            const float radius = player.values().BipedColRadius / 4096.0f;
            const Vec3 cylPos{flag.position[0], flag.position[1] - max - 1.25f, flag.position[2]};
            const float cylHeight = max - min + 0.5f;
            const float radii = radius + 0.5f;
            if (cylinderBetweenPoints(player.prevPosition(), player.position(), cylPos, cylHeight, radii)) {
                pickedUp = octolithTouched(flag, player);
                break;
            }
        }
        if (!flag.atBase && flag.carrier == nullptr) {
            flag.resetTimer += kFrameTime;
            if (flag.resetTimer >= 20) {
                octolithReset(flag);
            }
        }
    }
    if (flag.carrier != nullptr) {
        Player& carrier = *flag.carrier;
        flag.atBase = false;
        flag.grounded = true;
        flag.resetTimer = 0;
        flag.closestNode = carrier.closestNode;
        flag.position = {carrier.position()[0] + -0.35f * carrier.facingX(), carrier.position()[1] + 1.05f,
            carrier.position()[2] + -0.35f * carrier.facingZ()};
        Sfx& sfx = Sfx::instance();
        if (carrier.health() <= 0 || carrier.isAltForm() || carrier.isMorphing()) {
            const bool reset = carrier.health() == 0 && m_octolithReset;
            octolithDropped(flag, reset);
        } else if (pickedUp && !bounty) {
            if (main.teamIndex() == flag.teamId) {
                sfx.queueStream(VoiceId::VOICE_OCTO_PICKUP, 1, 2);
                m_slots[0].player->startFlagCarrySfx();
                Music::instance().playRoomMusic(roomId(), 1);
            } else {
                sfx.playFreeSfx(SfxId::FLAG_ACQUIRED);
            }
        } else if (pickedUp && bounty) {
            Music::instance().playRoomMusic(roomId(), 1);
            sfx.queueStream(VoiceId::VOICE_OCTO_PICKUP, 1, 2);
            if (flag.carrier == &main) {
                sfx.playFreeSfx(SfxId::FLAG_ACQUIRED);
                mainMessage(202, 90 / 30.0f, 1); // return to base
            } else {
                m_slots[0].player->startFlagCarrySfx();
            }
        }
    }
    if (flag.grounded) {
        flag.gravity = 0;
    } else {
        // Falling: onto a floor, or home when it falls out of the room.
        const Vec3 prevPos = flag.position;
        const auto [gravity, displacement] = constantAcceleration(-0.02f, flag.gravity);
        flag.position[1] += displacement;
        flag.gravity = gravity;
        flag.closestNode = nullptr;
        if (m_collision != nullptr) {
            CollisionResult results[16];
            const float margin = 1.25f;
            const Vec3 limitMin{std::min(prevPos[0], flag.position[0]) - margin, std::min(prevPos[1], flag.position[1]) - margin,
                std::min(prevPos[2], flag.position[2]) - margin};
            const Vec3 limitMax{std::max(prevPos[0], flag.position[0]) + margin, std::max(prevPos[1], flag.position[1]) + margin,
                std::max(prevPos[2], flag.position[2]) + margin};
            const int count = m_collision->checkSphereBetweenPoints(m_collision->candidates(limitMin, limitMax), prevPos,
                flag.position, 1.25f, 16, false, 0, results);
            for (int i = 0; i < count; i++) {
                const CollisionResult& result = results[i];
                if (result.plane[1] > 1401 / 4096.0f) {
                    const Vec3 pos = flag.position + Vec3{0, -1.25f, 0};
                    const float dist = result.plane[3] - dot(pos, xyz(result.plane));
                    flag.position = flag.position + xyz(result.plane) * dist;
                    flag.grounded = true;
                }
            }
        }
        if (m_scene.room != nullptr && flag.position[1] < m_scene.room->killHeight) {
            octolithReset(flag);
        }
    }
}

bool World::octolithTouched(OctolithFlag& flag, Player& player)
{
    // OnTouched: its own team sends it home; anybody else picks it up.
    const bool bounty = m_mode != GameMode::Capture;
    const Player& main = *m_slots[0].player;
    if (!bounty && player.teamIndex() == flag.teamId) {
        if (!flag.atBase) {
            Sfx::instance().playFreeSfx(SfxId::FLAG_RESET2);
            // your octolith reset! / enemy octolith reset!
            mainMessage(main.teamIndex() == flag.teamId ? 201 : 207, 60 / 30.0f, 1);
        }
        octolithAtBase(flag);
        return false;
    }
    if (flag.carrier != nullptr) {
        flag.carrier->octolithFlag = nullptr;
    }
    player.octolithFlag = &flag;
    flag.carrier = &player;
    flag.lastCarrier = &player;
    flag.atBase = false;
    flag.grounded = true;
    flag.resetTimer = 0;
    if (qEnvironmentVariableIsSet("FP_DEBUG_WORLD")) {
        qInfo("tick %lld: slot %d picks up the octolith of team %d", m_ticks, player.slot(), flag.teamId);
    }
    return true;
}

void World::octolithReset(OctolithFlag& flag)
{
    // Reset: 20 seconds on the ground, or out of the room.
    octolithAtBase(flag);
    int messageId = 257; // octolith reset!
    SfxId sound = SfxId::FLAG_RESET2;
    if (m_mode == GameMode::Capture) {
        // your octolith reset! / enemy octolith reset!
        const bool ours = m_slots[0].player->teamIndex() == flag.teamId;
        messageId = ours ? 201 : 207;
        sound = ours ? SfxId::FLAG_RESET2 : SfxId::FLAG_RESET1;
    }
    Sfx::instance().playFreeSfx(sound);
    mainMessage(messageId, 60 / 30.0f, 1);
}

void World::octolithDropped(OctolithFlag& flag, bool reset)
{
    // OnDropped: the carrier died or rolled up into its alt form.
    int messageId;
    Sfx& sfx = Sfx::instance();
    if (m_mode == GameMode::Capture) {
        if (m_slots[0].player->teamIndex() == flag.teamId) {
            messageId = reset ? 201 : 230; // your octolith reset! / the enemy dropped your octolith!
            if (reset) {
                sfx.playFreeSfx(SfxId::FLAG_RESET2);
            } else {
                sfx.queueStream(VoiceId::VOICE_OCTO_RESET, 1, 2);
            }
        } else {
            messageId = reset ? 207 : 231; // enemy octolith reset! / your team dropped the octolith!
            sfx.playFreeSfx(reset ? SfxId::FLAG_RESET1 : SfxId::FLAG_DROPPED);
        }
    } else {
        messageId = reset ? 257 : 229; // octolith reset! / the octolith has been dropped!
        if (reset) {
            sfx.playFreeSfx(SfxId::FLAG_RESET2);
        } else {
            sfx.queueStream(VoiceId::VOICE_OCTO_RESET, 1, 2);
            sfx.playFreeSfx(SfxId::FLAG_DROPPED);
        }
    }
    mainMessage(messageId, 60 / 30.0f, 1);
    m_slots[0].player->stopFlagCarrySfx();
    Music::instance().playRoomMusic(roomId(), 0);
    if (qEnvironmentVariableIsSet("FP_DEBUG_WORLD")) {
        qInfo("tick %lld: slot %d drops the octolith of team %d%s", m_ticks, flag.carrier->slot(), flag.teamId, reset ? " (reset)" : "");
    }
    if (reset) {
        octolithAtBase(flag);
    } else {
        flag.grounded = false;
        flag.atBase = false;
        if (flag.carrier != nullptr) {
            flag.carrier->octolithFlag = nullptr;
            flag.carrier = nullptr;
        }
        flag.resetTimer = 0;
    }
}

void World::octolithCaptured(OctolithFlag& flag)
{
    // OnCaptured: a point for the carrier.
    Sfx& sfx = Sfx::instance();
    const int mainTeam = m_slots[0].player->teamIndex();
    const bool ours = m_mode == GameMode::Capture ? mainTeam == flag.teamId : flag.carrier->teamIndex() == mainTeam;
    if (ours) {
        sfx.queueStream(m_mode == GameMode::Capture ? VoiceId::VOICE_OCTO_SCORE : VoiceId::VOICE_BOUNTY, 40 / 30.0f);
        sfx.playFreeSfx(SfxId::SCORE);
    } else {
        sfx.playFreeSfx(SfxId::SCORED_ON);
    }
    if (m_mode != GameMode::Capture) {
        mainMessage(203, 90 / 30.0f, 1); // bounty received
    }
    m_slots[0].player->stopFlagCarrySfx();
    Music::instance().playRoomMusic(roomId(), 0);
    PlayerSlot& slot = m_slots[flag.carrier->slot()];
    slot.points++;
    if (qEnvironmentVariableIsSet("FP_DEBUG_WORLD")) {
        qInfo("tick %lld: slot %d scores with the octolith of team %d (%d points)", m_ticks, flag.carrier->slot(), flag.teamId,
            slot.points);
    }
    octolithAtBase(flag);
}

// ---- FlagBaseEntity --------------------------------------------------------------------------

void World::processFlagBase(FlagBase& base)
{
    const bool capture = m_mode == GameMode::Capture;
    for (PlayerSlot& slot : m_slots) {
        Player& player = *slot.player;
        if (player.octolithFlag == nullptr || (capture && player.teamIndex() != base.teamId)) {
            continue;
        }
        if (base.volume.testPoint(player.position())) {
            if (capture) {
                // CheckOwnOctolith: a team scores only with its own Octolith at home.
                bool home = true;
                for (const OctolithFlag& flag : m_octolithFlags) {
                    if (flag.teamId == player.teamIndex() && !flag.atBase) {
                        home = false;
                    }
                }
                if (!home) {
                    if (&player == m_slots[0].player.get()) {
                        mainMessage(232, 1 / 1000.0f, 0, 50); // your octolith is missing!
                    }
                    continue;
                }
            }
            octolithCaptured(*player.octolithFlag);
        }
    }
}

// ---- NodeDefenseEntity -------------------------------------------------------------------------

void World::processDefender(NodeDefense& defense)
{
    // ProcessDefender: the ring is a team's while only that team stands in
    // it; its time counts toward the goal.
    int team = NodeDefense::NoTeam;
    defense.contested = false;
    for (PlayerSlot& slot : m_slots) {
        const Player& player = *slot.player;
        if (player.health() > 0 && player.teamIndex() >= 0 && player.teamIndex() < 16 && defense.volume.testPoint(player.volumeCenter())) {
            if (team == NodeDefense::NoTeam) {
                team = player.teamIndex();
            } else if (team != player.teamIndex()) {
                defense.contested = true;
            }
        }
    }
    if (defense.contested) {
        team = NodeDefense::NoTeam;
    }
    float speed, rotation;
    if (team == NodeDefense::NoTeam) {
        std::tie(speed, rotation) = constantAcceleration(-0.25f, defense.spinSpeed, 0);
    } else {
        std::tie(speed, rotation) = constantAcceleration(0.25f, defense.spinSpeed, -1e30f, 8 * 30.0f);
        m_teamTime[team] += kFrameTime;
    }
    defense.spinSpeed = speed;
    defense.curRotation += rotation;
    if (defense.curRotation >= 360) {
        defense.curRotation -= 360;
    }
    defense.currentTeam = team;
}

void World::processNodes(NodeDefense& defense)
{
    // ProcessNodes: standing in a node for ten seconds uncontested takes it;
    // a held node scores for whoever took it every five seconds, faster the
    // more nodes the team holds.
    const Player& main = *m_slots[0].player;
    const bool mainWasIn = defense.occupiedBy[main.slot() & 15];
    int value1 = 0, value2 = 0; // what the capture sounds are to say, as the C# has them
    defense.occupiedBy.fill(false);
    defense.contested = false;
    bool occupiedByAny = false;
    for (PlayerSlot& slot : m_slots) {
        const Player& player = *slot.player;
        if (player.health() > 0 && player.teamIndex() >= 0 && player.teamIndex() < 16 && defense.volume.testPoint(player.volumeCenter())) {
            if (defense.occupyingTeam == player.teamIndex()) {
                defense.occupiedBy[player.slot()] = true;
                occupiedByAny = true;
            } else if (defense.occupyingTeam == NodeDefense::NoTeam && defense.currentTeam != player.teamIndex()) {
                defense.occupiedBy[player.slot()] = true;
                occupiedByAny = true;
                defense.occupyingTeam = player.teamIndex();
                defense.progress = 0;
                defense.inProgress = false;
            } else if (defense.occupyingTeam != player.teamIndex()) {
                defense.contested = true;
            }
        }
    }
    float rotation = 0;
    Sfx& sfx = Sfx::instance();
    if (occupiedByAny) {
        if (defense.contested) {
            if (defense.occupiedBy[main.slot()]) {
                sfx.setPausedFreeSfxScripts(true);
            }
        } else if (defense.currentTeam != defense.occupyingTeam) {
            if (defense.occupiedBy[main.slot()]) {
                if (!defense.inProgress && defense.progress >= 10 / 30.0f) {
                    Music::instance().playRoomMusic(roomId(), 2);
                    value1 = 1;
                    defense.inProgress = true;
                }
                sfx.setPausedFreeSfxScripts(false);
            }
            defense.progress += kFrameTime;
            const float spinSpeed = defense.progress / (300 / 30.0f) * (15 * 30.0f);
            rotation = defense.spinSpeed * kFrameTime + (spinSpeed - defense.spinSpeed) / 2 * kFrameTime;
            defense.spinSpeed = spinSpeed;
            if (defense.progress >= 300 / 30.0f) {
                completeNode(defense, value1, value2);
                occupiedByAny = false;
            }
        }
    } else {
        if (mainWasIn) {
            Music::instance().playRoomMusic(roomId(), 0);
            if (value1 != 2) {
                value1 = 3;
            }
        }
        defense.occupyingTeam = NodeDefense::NoTeam;
        defense.progress = 0;
        defense.inProgress = false;
        std::tie(defense.spinSpeed, rotation) = constantAcceleration(-0.15f, defense.spinSpeed, 0);
    }
    int nodeCount = 0;
    int team = defense.currentTeam;
    float scoreThreshold = 150 / 30.0f;
    if (team == NodeDefense::NoTeam) {
        team = defense.occupyingTeam;
    }
    if (team != NodeDefense::NoTeam) {
        for (const NodeDefense& node : m_nodeDefenses) {
            if (node.currentTeam == team && node.occupyingTeam == NodeDefense::NoTeam) {
                nodeCount++;
                if (nodeCount > 1) {
                    scoreThreshold -= 45 / 30.0f;
                }
            }
        }
    }
    if (defense.currentTeam != NodeDefense::NoTeam && !occupiedByAny) {
        defense.scoreTimer += kFrameTime;
        if (defense.scoreTimer >= scoreThreshold && defense.capturedPlayer != nullptr) {
            m_slots[defense.capturedPlayer->slot()].points++;
            defense.scoreTimer = 0;
        }
    }
    if (value2 != 0 && nodeCount >= 2) {
        value1 = 5;
    }
    if (value1 == 1) {
        sfx.stopFreeSfxScripts();
        sfx.playFreeSfx(nodeCount == 0 ? SfxId::CAPTURE_RING_SCRIPT1 : nodeCount == 1 ? SfxId::CAPTURE_RING_SCRIPT2
                                                                                   : SfxId::CAPTURE_RING_SCRIPT3);
    } else if (value1 == 2) {
        if (nodeCount >= 2) {
            sfx.queueStream(VoiceId::VOICE_MULTI_NODE, 1, 35 / 30.0f);
        }
    } else if (value1 == 3) {
        sfx.stopFreeSfxScripts();
        sfx.playFreeSfx(SfxId::CAPTURE_RING_FAIL);
    }
    const float prevRotation = defense.curRotation;
    defense.curRotation += rotation;
    if (defense.curRotation >= 360) {
        defense.curRotation -= 360;
    }
    if (!occupiedByAny) {
        defense.blinkTimer = 0;
    } else if (toFixed(defense.curRotation) / 61440 != toFixed(prevRotation) / 61440) {
        defense.blinkTimer = 1 / 30.0f; // every 15 degrees the ring turns
    } else if (defense.blinkTimer > 0) {
        defense.blinkTimer -= kFrameTime;
    }
}

void World::completeNode(NodeDefense& defense, int& value1, int& value2)
{
    // Complete: the node changes hands.
    const Player& main = *m_slots[0].player;
    if (defense.currentTeam == main.teamIndex()) {
        value1 = 4;
        mainMessage(211, 90 / 30.0f, 17, 133, true); // node stolen
    }
    for (size_t i = 0; i < m_slots.size() && i < defense.occupiedBy.size(); i++) {
        if (defense.occupiedBy[i]) {
            defense.capturedPlayer = m_slots[i].player.get();
        }
        defense.occupiedBy[i] = false;
    }
    if (defense.capturedPlayer == &main) {
        mainMessage(206, 90 / 30.0f, 1); // complete
    }
    if (qEnvironmentVariableIsSet("FP_DEBUG_WORLD")) {
        qInfo("tick %lld: node at (%.1f %.1f %.1f) taken by team %d (slot %d)", m_ticks, defense.position[0], defense.position[1],
            defense.position[2], defense.occupyingTeam, defense.capturedPlayer != nullptr ? defense.capturedPlayer->slot() : -1);
    }
    defense.currentTeam = defense.occupyingTeam;
    defense.progress = 0;
    defense.inProgress = false;
    defense.occupyingTeam = NodeDefense::NoTeam;
    defense.scoreTimer = 150 / 30.0f;
    if (defense.currentTeam == main.teamIndex()) {
        Music::instance().playRoomMusic(roomId(), 0);
        value1 = 2;
    } else {
        value2 = 1;
    }
    defense.blinkTimer = 0;
}

// ---- drawing ------------------------------------------------------------------------------------

void World::updateModeModels()
{
    // The Octolith where it is, its base where it stays; the node rings
    // turning, both parts in the node's color.
    for (const OctolithFlag& flag : m_octolithFlags) {
        ModelInstance& inst = m_scene.instances[flag.instance];
        inst.transform.m[3][0] = flag.position[0];
        inst.transform.m[3][1] = flag.position[1];
        inst.transform.m[3][2] = flag.position[2];
    }
    if (m_nodeDefenses.empty()) {
        return;
    }
    for (NodeDefense& defense : m_nodeDefenses) {
        const std::array<float, 3> color = nodeColor(defense);
        auto setColor = [&](size_t index, const char* material) {
            ModelInstance& inst = m_scene.instances[index];
            inst.diffuseOverrides.clear();
            const auto& materials = inst.model->materials();
            for (size_t i = 0; i < materials.size(); i++) {
                if (materials[i].name == material) {
                    inst.diffuseOverrides.push_back({static_cast<int>(i), color});
                    break;
                }
            }
        };
        setColor(defense.terminalInstance, "lambert4");
        setColor(defense.ringInstance, "lambert2");
        // GetModelTransform: the ring scaled to the volume and turned, 0.7 above the node.
        ModelInstance& ring = m_scene.instances[defense.ringInstance];
        const Mat4& base = m_scene.instances[defense.terminalInstance].transform;
        const float radians = defense.curRotation * 3.14159265f / 180;
        Mat4 transform = Mat4::scale(defense.circleScale, defense.circleScale, defense.circleScale) * Mat4::rotationY(radians) * base;
        transform.m[3][0] = defense.position[0];
        transform.m[3][1] = defense.position[1] + 0.7f;
        transform.m[3][2] = defense.position[2];
        ring.transform = transform;
    }
}

std::array<float, 3> World::teamColor(int team)
{
    static constexpr std::array<std::array<float, 3>, 4> colors{{
        {1.0f, 19 / 31.0f, 0}, {0, 1.0f, 0}, {5 / 31.0f, 19 / 31.0f, 1.0f}, {27 / 31.0f, 8 / 31.0f, 1.0f}}};
    return team >= 0 && team < 4 ? colors[team] : std::array<float, 3>{1, 1, 1};
}

std::array<float, 3> World::nodeColor(const NodeDefense& defense) const
{
    // GetDrawInfo: white while nobody holds it; the holder's team color, or
    // without teams blue for the main player and red for anybody else --
    // blinking to the occupier's while it is being taken.
    static constexpr std::array<float, 3> neutral{1, 1, 1}, self{15 / 31.0f, 15 / 31.0f, 1}, enemy{1, 0, 0};
    const int mainTeam = m_slots[0].player->teamIndex();
    const bool blinking = defense.blinking();
    if (defense.currentTeam == NodeDefense::NoTeam) {
        if (!blinking) {
            return neutral;
        }
        return teams() ? teamColor(defense.occupyingTeam) : defense.occupyingTeam == mainTeam ? self : enemy;
    }
    if (teams()) {
        return teamColor(blinking ? defense.occupyingTeam : defense.currentTeam);
    }
    if (defense.currentTeam == mainTeam) {
        return !blinking || defense.occupyingTeam == mainTeam ? self : enemy;
    }
    return blinking && defense.occupyingTeam == mainTeam ? self : enemy;
}

void World::addModeLocators(HudContext& context) const
{
    static constexpr std::array<float, 3> good{15 / 31.0f, 15 / 31.0f, 1}, red{1, 0, 0};
    const Player& main = *m_slots[0].player;
    // The carrier's color, every other 8 ticks.
    auto carrierColor = [&](const OctolithFlag& flag, const std::array<float, 3>& otherwise) {
        if (flag.carrier != nullptr && (m_ticks & (4 * 2)) != 0) {
            return teams() ? teamColor(flag.carrier->teamIndex()) : flag.carrier == &main ? good : red;
        }
        return otherwise;
    };
    if (m_mode == GameMode::Bounty || m_mode == GameMode::BountyTeams) {
        // ProcessHudBounty: the bases for the carrier, else the Octolith.
        if (main.octolithFlag != nullptr) {
            for (const FlagBase& base : m_flagBases) {
                context.locators.push_back({base.position, good, 1, HudContext::Locator::Node});
            }
        } else {
            for (const OctolithFlag& flag : m_octolithFlags) {
                context.locators.push_back({flag.position, carrierColor(flag, {1, 1, 1}), 1, HudContext::Locator::Octolith});
            }
        }
    } else if (m_mode == GameMode::Capture) {
        // ProcessHudCapture: both Octoliths in their team's color (but the one
        // carried); carrying, the way home to the team's own base.
        for (const OctolithFlag& flag : m_octolithFlags) {
            if (flag.carrier == &main) {
                continue;
            }
            context.locators.push_back({flag.position, carrierColor(flag, teamColor(flag.teamId)), 1, HudContext::Locator::Octolith});
            if (main.octolithFlag != nullptr && flag.teamId == main.teamIndex()) {
                context.locators.push_back({flag.basePosition, good, 1, HudContext::Locator::Node});
            }
        }
    } else if (defenderMode() || nodesMode()) {
        // ProcessHudDefender / ProcessHudNodes: the rings, in their colors.
        for (const NodeDefense& defense : m_nodeDefenses) {
            context.locators.push_back({defense.position, nodeColor(defense), 1, HudContext::Locator::Node});
        }
    }
}

} // namespace fp
