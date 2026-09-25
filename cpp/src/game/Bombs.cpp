// BombEntity: Samus's morph ball bombs, Kanden's stinglarva and Sylux's
// Lockjaw, laid in alt form and processed one 60 Hz tick at a time.

#include "World.h"

#include <QtGlobal>

#include <algorithm>
#include <cmath>

namespace fp {

namespace {

Vec3 normalized(Vec3 v)
{
    const float len = std::sqrt(dot(v, v));
    return len > 0 ? v * (1.0f / len) : v;
}

Vec3 cross(const Vec3& a, const Vec3& b)
{
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}

float fx(int32_t raw) { return raw / 4096.0f; }

// Mods/Render/LockjawTrailNoise: the arcs' jitter, a hash of what the arc is.
float lockjawTrailNoise(unsigned long long tick, int ownerSlot, int sourceBomb, int targetBomb, int segment, int axis)
{
    uint32_t value = 0x9E3779B9;
    value = (value ^ static_cast<uint32_t>(tick)) * 0x01000193;
    value = (value ^ static_cast<uint32_t>(tick >> 32)) * 0x01000193;
    value = (value ^ static_cast<uint32_t>(ownerSlot)) * 0x01000193;
    value = (value ^ static_cast<uint32_t>(sourceBomb)) * 0x01000193;
    value = (value ^ static_cast<uint32_t>(targetBomb)) * 0x01000193;
    value = (value ^ static_cast<uint32_t>(segment)) * 0x01000193;
    value = (value ^ static_cast<uint32_t>(axis)) * 0x01000193;
    value ^= value >> 16;
    value *= 0x7FEB352D;
    value ^= value >> 15;
    value *= 0x846CA68B;
    value ^= value >> 16;
    return (value & 0xFFFF) / 65536.0f * 0.5f - 0.25f;
}

} // namespace

bool World::spawnBomb(Player& owner, const Mat4& transform)
{
    // PlayerInput.SpawnBomb and BombEntity.Spawn/Initialize.
    PlayerSlot& slot = m_slots[owner.slot()];
    int type = BombType::MorphBall;
    if (owner.hunter() == Hunter::Kanden) {
        type = BombType::Stinglarva;
    } else if (owner.hunter() == Hunter::Sylux) {
        type = BombType::Lockjaw;
        if (slot.syluxBombCount >= 3) {
            // Detonate what is out, newest first.
            bool detonated = false;
            for (int i = 2; i >= 0; i--) {
                if (slot.syluxBombs[i] >= 0) {
                    m_bombs[slot.syluxBombs[i]].countdown = 0;
                    detonated = true;
                }
            }
            if (detonated) {
                return false;
            }
            slot.syluxBombCount = 0;
        }
    }
    size_t index = 0;
    while (index < m_bombs.size() && m_bombs[index].active) {
        index++;
    }
    if (index == m_bombs.size()) {
        m_bombs.emplace_back();
    }
    Bomb& bomb = m_bombs[index];
    const std::optional<size_t> instance = bomb.instance;
    bomb = Bomb{};
    bomb.instance = instance;
    bomb.active = true;
    bomb.type = type;
    bomb.owner = owner.slot();
    bomb.transform = transform;
    bomb.position = transform.translationPart();
    const PlayerValues& v = owner.values();
    bomb.radius = fx(v.BombRadius);
    bomb.selfRadius = fx(v.BombSelfRadius);
    bomb.damage = v.BombDamage;
    if (owner.doubleDamageTimer() > 0) {
        bomb.damage *= 2;
    }
    if (type == BombType::Stinglarva) {
        bomb.countdown = 43 * 2;
        if (!bomb.instance) {
            if (const Model* model = m_scene.model(m_root, "KandenAlt_TailBomb")) {
                ModelInstance inst;
                inst.model = model;
                inst.animation.set(model->animations(), 0);
                m_scene.instances.push_back(inst);
                bomb.instance = m_scene.instances.size() - 1;
            }
        }
        if (bomb.instance) {
            ModelInstance& inst = m_scene.instances[*bomb.instance];
            inst.recolor = owner.slot() < static_cast<int>(inst.model->recolorCount()) ? owner.slot() : 0;
            inst.transform = transform;
            inst.visible = true;
        }
    } else if (type == BombType::Lockjaw) {
        bomb.countdown = 900 * 2;
        bomb.index = slot.syluxBombCount;
        slot.syluxBombs[slot.syluxBombCount++] = static_cast<int>(index);
        owner.setSyluxBombCount(slot.syluxBombCount);
        if (bomb.index == 1) {
            // Too far from the first, or a wall between: both go off.
            Bomb& first = m_bombs[slot.syluxBombs[0]];
            const Vec3 between = first.position - bomb.position;
            CollisionResult result;
            if (dot(between, between) >= 100
                || (m_collision && m_collision->checkBetweenPoints(first.position, bomb.position, TestFlags::Players, result))) {
                bomb.countdown = 1;
                first.countdown = 1;
            }
        }
    } else {
        bomb.countdown = 43 * 2;
    }
    int effectId = 0;
    if (type == BombType::Lockjaw) {
        // bombStartSylux in the owner's color
        const auto& effects = syluxBombEffects();
        effectId = effects[std::min<size_t>(static_cast<size_t>(owner.slot()), effects.size() - 1)];
    } else if (type == BombType::MorphBall) {
        effectId = m_slots.size() > 2 ? 119 : 9; // bombStartMP or bombStart
    }
    if (effectId != 0) {
        bomb.effect = m_effects->spawnEntry(effectId, transform);
        m_effects->setElementExtension(bomb.effect, true);
    }
    // BombEntity.PlaySpawnSfx
    bomb.sound->update(bomb.position, 5);
    bomb.sound->playSfx(type == BombType::Stinglarva ? SfxId::KANDEN_ALT_ATTACK : SfxId::MORPH_BALL_BOMB_PLACE);
    return true;
}

void World::processBombs()
{
    for (size_t i = 0; i < m_bombs.size(); i++) {
        if (m_bombs[i].active && !processBomb(i)) {
            destroyBomb(i);
        }
    }
    for (PlayerSlot& slot : m_slots) {
        slot.player->setSyluxBombCount(slot.syluxBombCount);
    }
}

bool World::processBomb(size_t index)
{
    // BombEntity.Process
    Bomb& bomb = m_bombs[index];
    Player& owner = *m_slots[bomb.owner].player;
    PlayerSlot& ownerSlot = m_slots[bomb.owner];
    if (bomb.type == BombType::Lockjaw) {
        bomb.visualTick++;
    }
    bomb.sound->update(bomb.position, 5);
    int hitPlayer = -1;
    bool hitTurret = false;
    if (bomb.countdown > 0) {
        bomb.countdown--;
    }
    if (bomb.countdown == 0) {
        bomb.exploding = true;
    }
    if (!bomb.exploded) {
        for (PlayerSlot& slot : m_slots) {
            Player& player = *slot.player;
            if (&player == &owner || player.health() == 0 || areAllies(player.teamIndex(), owner.teamIndex())) {
                continue;
            }
            BombHit hit{&owner, bomb.position, bomb.radius, bomb.damage, bomb.type, bomb.exploding || bomb.exploded};
            if (player.checkHitByBomb(hit)) {
                hitPlayer = player.slot();
                hitTurret = false;
                bomb.exploding = true;
            }
            if (player.halfturret().active && player.checkHalfturretHitByBomb(hit)) {
                hitPlayer = player.slot();
                hitTurret = true;
                bomb.exploding = true;
            }
            if (bomb.target >= 0) {
                continue;
            }
            const Vec3 facing{bomb.transform.m[2][0], bomb.transform.m[2][1], bomb.transform.m[2][2]};
            if (bomb.type == BombType::Lockjaw) {
                lockjawCheckTargeting(bomb, player, hitPlayer, hitTurret);
            } else if (bomb.type == BombType::Stinglarva) {
                const Vec3 between = player.position() - bomb.position;
                if (dot(between, between) < 5 * 5) {
                    bomb.target = player.slot();
                    bomb.targetTurret = false;
                    bomb.speed = facing * 0.3f;
                }
            }
        }
        if (bomb.type == BombType::Stinglarva && bomb.target < 0) {
            for (PlayerSlot& slot : m_slots) {
                const Halfturret& turret = slot.player->halfturret();
                const Vec3 between = turret.position - bomb.position;
                if (turret.active && dot(between, between) < 5 * 5) {
                    bomb.target = slot.player->slot();
                    bomb.targetTurret = true;
                    const Vec3 facing{bomb.transform.m[2][0], bomb.transform.m[2][1], bomb.transform.m[2][2]};
                    bomb.speed = facing * 0.3f;
                }
            }
        }
        if (owner.isAltForm()) {
            // The bomb jump, once it goes off.
            owner.checkHitByBomb({&owner, bomb.position, bomb.radius, bomb.damage, bomb.type, bomb.exploding || bomb.exploded});
        }
        if (bomb.exploding) {
            // Doors a bomb blast touches open like shot ones.
            for (Door& door : m_doors) {
                Vec3 between = bomb.position - door.lockPosition;
                const float d = dot(door.facing, between);
                const float radius = bomb.selfRadius + 0.4f;
                if (d < radius && d > -radius) {
                    between = between - door.facing * d;
                    if (dot(between, between) <= door.radiusSquared) {
                        door.shotOpen = true;
                    }
                }
            }
        }
        if (bomb.type == BombType::Lockjaw && bomb.index == 0 && ownerSlot.syluxBombCount == 3 && bomb.target < 0 && hitPlayer < 0) {
            // The snare closed on nobody: all three go off.
            for (int i = 0; i < 3; i++) {
                Bomb& other = m_bombs[ownerSlot.syluxBombs[i]];
                other.countdown = 1;
                other.target = bomb.owner;
                other.targetTurret = false;
            }
        }
    }
    if (bomb.target >= 0) {
        const Player& target = *m_slots[bomb.target].player;
        if (target.health() > 0 && (!bomb.targetTurret || target.halfturret().active)) {
            processBombTargeting(bomb);
        } else {
            bomb.target = -1;
        }
    }
    if (bomb.type == BombType::Lockjaw) {
        if (owner.health() == 0) {
            bomb.exploding = true;
            bomb.countdown = 0;
        }
        if (hitPlayer >= 0) {
            for (int i = 0; i < ownerSlot.syluxBombCount; i++) {
                Bomb& other = m_bombs[ownerSlot.syluxBombs[i]];
                other.target = hitPlayer;
                other.targetTurret = hitTurret;
                other.countdown = std::min(other.countdown, 22 * 2);
            }
        }
    }
    if (bomb.exploding) {
        bomb.exploding = false;
        if (bomb.exploded) {
            return false;
        }
        bomb.exploded = true;
        bomb.target = -1;
        if (bomb.instance) {
            m_scene.instances[*bomb.instance].visible = false;
        }
        m_effects->unlink(bomb.effect);
        bomb.effect = -1;
        // bombKanden, bombSylux, bombBlue
        m_effects->spawn(bomb.type == BombType::Stinglarva ? 128 : bomb.type == BombType::Lockjaw ? 146 : 145, bomb.transform);
        bomb.countdown = 0;
        bomb.sound->stopSfx(SfxId::KANDEN_ALT_ATTACK);
        bomb.sound->stopSfx(SfxId::MORPH_BALL_BOMB_PLACE);
        bomb.sound->playSfx(SfxId::MORPH_BALL_BOMB);
    }
    if (bomb.effect >= 0) {
        m_effects->setTransform(bomb.effect, bomb.position, bomb.transform);
    }
    if (bomb.instance) {
        ModelInstance& inst = m_scene.instances[*bomb.instance];
        inst.transform = bomb.transform;
    }
    return true;
}

void World::lockjawCheckTargeting(Bomb& bomb, Player& player, int& hitPlayer, bool& hitTurret)
{
    // BombEntity.LockjawCheckTargeting: the arcs between the bombs hurt, and
    // the three of them make a snare.
    PlayerSlot& ownerSlot = m_slots[bomb.owner];
    const PlayerValues& v = player.values();
    const Vec3 targetPos = player.isAltForm() ? player.volumeCenter()
                                              : Vec3{player.position()[0], player.position()[1] + fx(v.MinPickupHeight), player.position()[2]};
    const float cylHeight = fx(v.MaxPickupHeight) - fx(v.MinPickupHeight);
    CollisionResult discard;
    auto crosses = [&](const Vec3& other) {
        if (player.isAltForm()) {
            return checkCylinderOverlapSphere(bomb.position, other, targetPos, player.volumeRadius(), discard);
        }
        return checkCylindersOverlap(bomb.position, other, targetPos, {0, 1, 0}, cylHeight, player.volumeRadius(), discard);
    };
    for (int i = 0; i < std::min(ownerSlot.syluxBombCount, 3); i++) {
        if (ownerSlot.syluxBombs[i] < 0) {
            return;
        }
    }
    bool lineHit = false;
    if (bomb.index == 1) {
        lineHit = crosses(m_bombs[ownerSlot.syluxBombs[0]].position);
    } else if (bomb.index == 2) {
        lineHit = crosses(m_bombs[ownerSlot.syluxBombs[0]].position);
        lineHit = crosses(m_bombs[ownerSlot.syluxBombs[1]].position) || lineHit;
    } else if (ownerSlot.syluxBombCount == 3) {
        const bool inSnare = lockjawCheckSnare(bomb, player.position());
        const bool turretInSnare = !inSnare && player.halfturret().active && lockjawCheckSnare(bomb, player.halfturret().position);
        if (inSnare || turretInSnare) {
            hitPlayer = player.slot();
            hitTurret = turretInSnare;
            for (int i = 0; i < ownerSlot.syluxBombCount; i++) {
                m_bombs[ownerSlot.syluxBombs[i]].damage = 60;
            }
        }
        return;
    }
    bool turretHit = false;
    if (!lineHit && player.halfturret().active && bomb.index > 0) {
        auto crossesTurret = [&](int i) {
            return checkCylinderOverlapSphere(bomb.position, m_bombs[ownerSlot.syluxBombs[i]].position, player.halfturret().position, 0.45f, discard);
        };
        turretHit = crossesTurret(0) || (bomb.index == 2 && crossesTurret(1));
    }
    if (lineHit || turretHit) {
        hitPlayer = player.slot();
        hitTurret = turretHit;
        DamageSource source;
        source.attacker = m_slots[bomb.owner].player.get();
        source.bomb = bomb.type;
        player.takeDamage(20, DamageFlags::NoDmgInvuln | (turretHit ? DamageFlags::Halfturret : 0), nullptr, source);
    }
}

bool World::lockjawCheckSnare(const Bomb& bomb, const Vec3& position) const
{
    // BombEntity.LockjawCheckSnare: inside the triangle, within 0.75 of its plane.
    const PlayerSlot& ownerSlot = m_slots[bomb.owner];
    const Vec3& p0 = m_bombs[ownerSlot.syluxBombs[0]].position;
    const Vec3& p1 = m_bombs[ownerSlot.syluxBombs[1]].position;
    const Vec3& p2 = m_bombs[ownerSlot.syluxBombs[2]].position;
    const Vec3 zeroToOne = p1 - p0;
    const Vec3 oneToTwo = p2 - p1;
    const Vec3 normal = normalized(cross(oneToTwo, zeroToOne));
    const Vec3 zeroToPosition = position - p0;
    const float d = dot(normal, zeroToPosition);
    if (d <= -0.75f || d >= 0.75f) {
        return false;
    }
    return dot(cross(zeroToPosition, zeroToOne), normal) > 0 && dot(cross(position - p1, oneToTwo), normal) > 0
        && dot(cross(position - p2, p0 - p2), normal) > 0;
}

void World::processBombTargeting(Bomb& bomb)
{
    // BombEntity.ProcessTargeting: Lockjaw bombs home in; the stinglarva
    // steers along the ground and falls.
    const Player& target = *m_slots[bomb.target].player;
    const Vec3 targetPos = bomb.targetTurret ? target.halfturret().position : target.position();
    const Vec3 prevPos = bomb.position;
    Vec3 newSpeed;
    if (bomb.type == BombType::Lockjaw) {
        const Vec3 between = normalized(targetPos - bomb.position);
        newSpeed = bomb.speed + (between - bomb.speed) * 0.15f;
    } else {
        Vec3 between = targetPos - bomb.position;
        between[1] = 0;
        between = normalized(between);
        newSpeed = {bomb.speed[0] + (between[0] - bomb.speed[0]) * 0.05f, bomb.speed[1] - 0.05f,
            bomb.speed[2] + (between[2] - bomb.speed[2]) * 0.05f};
    }
    bomb.speed = bomb.speed + (newSpeed - bomb.speed) * 0.5f;
    bomb.position = bomb.position + bomb.speed * 0.5f;
    if (m_collision) {
        const float margin = 0.4f;
        const Vec3 lo{std::min(prevPos[0], bomb.position[0]) - margin, std::min(prevPos[1], bomb.position[1]) - margin,
            std::min(prevPos[2], bomb.position[2]) - margin};
        const Vec3 hi{std::max(prevPos[0], bomb.position[0]) + margin, std::max(prevPos[1], bomb.position[1]) + margin,
            std::max(prevPos[2], bomb.position[2]) + margin};
        CollisionResult results[8];
        const int count = m_collision->checkSphereBetweenPoints(m_collision->candidates(lo, hi), prevPos, bomb.position, 0.4f, 8, false, 0, results);
        for (int i = 0; i < count; i++) {
            const Vec4& plane = results[i].plane;
            const Vec3 normal{plane[0], plane[1], plane[2]};
            const float dotw = plane[3] - dot(bomb.position, normal) + 0.4f;
            if (dotw > 0) {
                bomb.position = bomb.position + normal * dotw;
                const float d = dot(bomb.speed * 0.5f, normal);
                if (d < 0) {
                    bomb.speed = bomb.speed + normal * -d;
                }
            }
        }
    }
    Mat4& t = bomb.transform;
    if (bomb.type != BombType::Lockjaw && (bomb.speed[0] != 0 || bomb.speed[2] != 0)) {
        t = Mat4::fromVectors(normalized(bomb.speed), {t.m[1][0], t.m[1][1], t.m[1][2]}, bomb.position);
    } else {
        t.m[3][0] = bomb.position[0];
        t.m[3][1] = bomb.position[1];
        t.m[3][2] = bomb.position[2];
    }
}

void World::destroyBomb(size_t index)
{
    // BombEntity.Destroy: a Lockjaw bomb leaves its owner's list, the later ones moving down.
    Bomb& bomb = m_bombs[index];
    bomb.sound->stopAllSfx();
    PlayerSlot& ownerSlot = m_slots[bomb.owner];
    const int owned = std::min(ownerSlot.syluxBombCount, 3);
    if (bomb.type == BombType::Lockjaw && bomb.index >= 0 && bomb.index < owned
        && ownerSlot.syluxBombs[bomb.index] == static_cast<int>(index)) {
        for (int i = bomb.index; i < owned - 1; i++) {
            ownerSlot.syluxBombs[i] = ownerSlot.syluxBombs[i + 1];
            if (ownerSlot.syluxBombs[i] >= 0) {
                m_bombs[ownerSlot.syluxBombs[i]].index = i;
            }
        }
        ownerSlot.syluxBombs[owned - 1] = -1;
        ownerSlot.syluxBombCount--;
    }
    if (bomb.instance) {
        m_scene.instances[*bomb.instance].visible = false;
    }
    m_effects->unlink(bomb.effect);
    bomb.effect = -1;
    bomb.active = false;
}

void World::drawBombs()
{
    // BombEntity.GetDrawInfo: the arcs between Lockjaw bombs (DrawLockjawTrail).
    std::vector<Vertex>& vertices = m_scene.dynamicVertices;
    for (const Bomb& bomb : m_bombs) {
        if (!bomb.active || bomb.exploded || bomb.type != BombType::Lockjaw || bomb.index == 0) {
            continue;
        }
        const PlayerSlot& ownerSlot = m_slots[bomb.owner];
        const int recolor = std::max(0, bomb.owner - 1);
        const BeamTexture& tex = bomb.owner == 0 ? m_arcWelder : m_arcWelder1;
        if (tex.model == nullptr) {
            continue;
        }
        const float height = fx(614);
        const int segments = 10;
        for (int target = bomb.index - 1; target >= 0; target--) {
            const int other = ownerSlot.syluxBombs[target];
            if (other < 0) {
                continue;
            }
            const Vec3 vec = m_bombs[other].position - bomb.position;
            const float uvT = (tex.height - 1 / 16.0f) / tex.height;
            DynamicDraw draw;
            draw.model = tex.model;
            draw.textureId = tex.textureId;
            draw.paletteId = tex.paletteId;
            draw.recolor = recolor;
            draw.xRepeat = tex.xRepeat;
            draw.yRepeat = tex.yRepeat;
            draw.transform = Mat4::translation(bomb.position[0], bomb.position[1], bomb.position[2]);
            draw.firstVertex = static_cast<uint32_t>(vertices.size());
            std::vector<Vertex> strip;
            for (int i = 0; i < segments; i++) {
                const float uvS = i > 0 ? (tex.width / static_cast<float>(segments - 1) * i - 1 / 16.0f) / tex.width : 0;
                const float pct = i * (1.0f / (segments - 1));
                Vec3 p = vec * pct;
                if (i > 0 && i < segments - 1) {
                    for (int axis = 0; axis < 3; axis++) {
                        p[axis] += lockjawTrailNoise(bomb.visualTick, bomb.owner, bomb.index, target, i, axis);
                    }
                }
                for (int side = 0; side < 2; side++) {
                    Vertex out{};
                    out.pos[0] = p[0];
                    out.pos[1] = p[1] + (side == 0 ? -height : height);
                    out.pos[2] = p[2];
                    out.normal[2] = 1;
                    out.color[0] = out.color[1] = out.color[2] = out.color[3] = 1;
                    out.uv[0] = uvS;
                    out.uv[1] = side == 0 ? 0 : uvT;
                    strip.push_back(out);
                }
            }
            for (size_t i = 0; i + 3 < strip.size(); i += 2) {
                for (size_t k : {i, i + 1, i + 2, i + 1, i + 3, i + 2}) {
                    vertices.push_back(strip[k]);
                }
            }
            draw.vertexCount = static_cast<uint32_t>(vertices.size()) - draw.firstVertex;
            m_scene.dynamicDraws.push_back(draw);
        }
    }
}

} // namespace fp
