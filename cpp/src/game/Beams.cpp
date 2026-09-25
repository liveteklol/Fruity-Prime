// BeamProjectileEntity and BeamEffectEntity: spawning a shot, moving it, what
// it hits and what that does. Ported from Entities/BeamProjectileEntity.cs for
// a multiplayer battle: no enemies, no halfturret, no beam-on-beam collisions
// (single player only), and the effects (muzzle flashes, impact sparks) wait
// for the particle system.
#include "World.h"
#include "Rng.h"

#include "formats/Metadata.h"

#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace fp {

namespace {

constexpr float kFrameTime = 1 / 60.0f;
constexpr float kDegToRad = std::numbers::pi_v<float> / 180.0f;
constexpr int kTerrainLava = 8;
constexpr int kTerrainAcid = 9;
constexpr uint16_t kCollisionReflectBeams = 0x200;

float fx(int32_t raw) { return raw / 4096.0f; }

Vec3 normalized(Vec3 v)
{
    const float len = std::sqrt(dot(v, v));
    return len > 0 ? v * (1.0f / len) : v;
}

Vec3 cross(const Vec3& a, const Vec3& b)
{
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}

float length(const Vec3& v) { return std::sqrt(dot(v, v)); }

Vec4 plane(const Vec3& normal, float w = 0) { return {normal[0], normal[1], normal[2], w}; }

// ContinuousWeaponPhase.Amount: a continuous weapon's ammo and damage are
// fractions of 32 paid out every other tick.
int continuousAmount(int amount, uint64_t phase, bool damage)
{
    if (phase % 2 != 0) {
        return 0;
    }
    const uint64_t bits = static_cast<uint64_t>(amount & 31);
    const uint64_t fraction = (bits * (phase / 2)) & 31;
    return amount / 32 + (bits != 0 && (damage ? fraction >= 32 - bits : fraction > 32 - bits) ? 1 : 0);
}

// BeamProjectileEntity.GetInterpolatedValue
float interpolated(int type, float value1, float value2, float ratio)
{
    if (type == 3) {
        return ratio > 1 ? value2 : value1; // binary
    }
    ratio = std::clamp(ratio, 0.0f, 1.0f);
    if (type == 0) {
        return value1 + (value2 - value1) * ratio;
    }
    if (type == 1) {
        return value1 + (value2 - value1) * ((std::sin((270 - 180 * ratio) * kDegToRad) + 1) / 2);
    }
    if (type == 2) {
        return value1 + (value2 - value1) * (std::sin((270 - 90 * ratio) * kDegToRad) + 1);
    }
    return 0;
}

// BeamProjectileEntity.GetCrossVector
Vec3 crossVector(const Vec3& up)
{
    if (up[2] <= fx(-3686) || up[2] >= fx(3686)) {
        return normalized(cross({1, 0, 0}, up));
    }
    return normalized(cross({0, 0, 1}, up));
}

// Rng.GetRandomInt2: the game's LCG, top bits scaled to [0, max).
bool debugBeams()
{
    static const bool debug = qEnvironmentVariableIsSet("FP_DEBUG_BEAMS");
    return debug;
}

uint32_t randomInt(uint32_t max)
{
    static uint32_t rng = 0x5E0B2C1D;
    rng = rng * 0x7FF8A3ED + 0x2AA01D31;
    return static_cast<uint32_t>((static_cast<uint64_t>(rng >> 16) * max) >> 16);
}

} // namespace

World::BeamTexture World::beamTexture(const Model* model, int materialId) const
{
    BeamTexture t;
    if (model == nullptr || materialId < 0 || materialId >= static_cast<int>(model->materials().size())) {
        return t;
    }
    const Material& material = model->materials()[materialId];
    if (material.textureId < 0 || material.textureId >= static_cast<int>(model->textureCount())) {
        return t;
    }
    const Image image = model->decodeTexture(material.textureId, material.paletteId);
    t.model = model;
    t.textureId = material.textureId;
    t.paletteId = material.paletteId;
    t.xRepeat = material.xRepeat;
    t.yRepeat = material.yRepeat;
    t.width = std::max(1, image.width);
    t.height = std::max(1, image.height);
    return t;
}

size_t World::activeBeams() const
{
    size_t count = 0;
    for (const PlayerSlot& slot : m_slots) {
        for (const BeamProjectile& beam : slot.beams) {
            count += beam.lifespan > 0 ? 1 : 0;
        }
    }
    return count;
}

size_t World::activeBeams(size_t slot) const
{
    size_t count = 0;
    for (const BeamProjectile& beam : m_slots.at(slot).beams) {
        count += beam.lifespan > 0 ? 1 : 0;
    }
    return count;
}

// ---- spawning ---------------------------------------------------------------

int World::spawnBeam(int owner, EquipInfo& equip, const WeaponInfo& weapon, const Vec3& position, const Vec3& direction,
    int spawnFlags, const BeamProjectile* parent)
{
    // BeamProjectileEntity.Spawn
    PlayerSlot& slot = m_slots.at(owner);
    Player& player = *slot.player;
    int result = BeamResult::Spawned;
    const uint32_t wflags = static_cast<uint32_t>(weapon.flags);
    bool charged = false;
    float chargePct = 0;
    if (wflags & WeaponFlags::CanCharge) {
        if (wflags & WeaponFlags::PartialCharge) {
            if (equip.chargeLevel >= weapon.minCharge * 2) {
                charged = true;
                chargePct = (equip.chargeLevel - weapon.minCharge * 2) / static_cast<float>(weapon.fullCharge * 2 - weapon.minCharge * 2);
            }
        } else if (equip.chargeLevel >= weapon.fullCharge * 2) {
            charged = true;
            chargePct = 1;
        }
    }
    const int c = charged ? 1 : 0;
    auto amount = [&](int uncharged, int minCharge, int fullCharge) {
        return chargePct <= 0 ? static_cast<float>(uncharged) : minCharge + (fullCharge - minCharge) * chargePct;
    };
    const uint64_t phase = static_cast<uint64_t>(m_ticks);
    int cost = static_cast<int>(amount(weapon.ammoCost, weapon.minChargeCost, weapon.chargeCost));
    if (wflags & WeaponFlags::Continuous) {
        cost = continuousAmount(cost, phase, false);
    }
    // A ricochet's equip has no ammo of its own, nor has the Halfturret's.
    const bool fromHalfturret = parent != nullptr ? parent->fromHalfturret : (spawnFlags & BeamSpawnFlags::FromHalfturret) != 0;
    if (parent == nullptr && !fromHalfturret) {
        const int ammo = player.ammo(weapon.ammoType);
        if (ammo >= 0 && cost > ammo) {
            return BeamResult::NoSpawn;
        }
        player.setAmmo(weapon.ammoType, ammo - cost);
    }
    if (!(spawnFlags & BeamSpawnFlags::NoMuzzle) && weapon.muzzleEffects[c] != 255 && weapon.muzzleEffects[c] >= 3) {
        m_effects->spawn(weapon.muzzleEffects[c] - 3, Mat4::fromVectors(crossVector(direction), direction, position));
    }
    const int projectiles = static_cast<int>(amount(weapon.projectiles, weapon.minChargeProjectiles, weapon.chargedProjectiles));
    if (projectiles <= 0) {
        return result;
    }
    const bool instantAoe = (charged && (wflags & WeaponFlags::AoeCharged)) || (!charged && (wflags & WeaponFlags::AoeUncharged));
    auto pick = [&](uint32_t unchargedFlag, uint32_t chargedFlag) { return (wflags & (charged ? chargedFlag : unchargedFlag)) != 0; };

    uint16_t flags = 0;
    const float speed = amount(weapon.unchargedSpeed, weapon.minChargeSpeed, weapon.chargedSpeed) / 4096.0f / 2;
    const float finalSpeed = amount(weapon.unchargedFinalSpeed, weapon.minChargeFinalSpeed, weapon.chargedFinalSpeed) / 4096.0f / 2;
    const float speedDecayTime = weapon.speedDecayTimes[c] * (1 / 30.0f);
    const float gravity = amount(weapon.unchargedGravity, weapon.minChargeGravity, weapon.chargedGravity) / 4096.0f;
    const Vec3 acceleration{0, gravity / 2, 0};
    const float homing = amount(weapon.unchargedHoming, weapon.minChargeHoming, weapon.chargedHoming) / 4096.0f / 2;
    if (homing > 0) {
        flags |= BeamFlags::Homing;
    }
    if (charged || (spawnFlags & BeamSpawnFlags::Charged)) {
        flags |= BeamFlags::Charged;
    }
    if (pick(WeaponFlags::RicochetUncharged, WeaponFlags::RicochetCharged)) {
        flags |= BeamFlags::Ricochet;
    }
    if (pick(WeaponFlags::SelfDamageUncharged, WeaponFlags::SelfDamageCharged)) {
        flags |= BeamFlags::SelfDamage;
    }
    if (pick(WeaponFlags::ForceEffectUncharged, WeaponFlags::ForceEffectCharged)) {
        flags |= BeamFlags::ForceEffect;
    }
    if (pick(WeaponFlags::DestroyableUncharged, WeaponFlags::DestroyableCharged)) {
        flags |= BeamFlags::Destroyable;
    }
    if (pick(WeaponFlags::LifeDrainUncharged, WeaponFlags::LifeDrainCharged)) {
        flags |= BeamFlags::LifeDrain;
    }
    if (wflags & WeaponFlags::Continuous) {
        flags |= BeamFlags::Continuous;
    }
    if (wflags & WeaponFlags::SurfaceCollision) {
        flags |= BeamFlags::SurfaceCollision;
    }
    const int colorValue = weapon.colors[c];
    const Vec3 color{(colorValue & 0x1F) / 31.0f, ((colorValue >> 5) & 0x1F) / 31.0f, ((colorValue >> 10) & 0x1F) / 31.0f};
    int damage = static_cast<int>(amount(weapon.unchargedDamage, weapon.minChargeDamage, weapon.chargedDamage));
    int hsDamage = static_cast<int>(amount(weapon.headshotDamage, weapon.minChargeHeadshotDamage, weapon.chargedHeadshotDamage));
    int splashDamage = static_cast<int>(amount(weapon.splashDamage, weapon.minChargeSplashDamage, weapon.chargedSplashDamage));
    if (spawnFlags & BeamSpawnFlags::DoubleDamage) {
        damage *= 2;
        hsDamage *= 2;
        splashDamage *= 2;
    } else if (spawnFlags & BeamSpawnFlags::PrimeHunter) {
        damage = 150 * damage / 100;
        hsDamage = 150 * hsDamage / 100;
        splashDamage = 150 * splashDamage / 100;
    }
    if (wflags & WeaponFlags::Continuous) {
        damage = continuousAmount(damage, phase, true);
    }
    const int maxSpread = static_cast<int>(amount(weapon.unchargedSpread, weapon.minChargeSpread, weapon.chargedSpread));
    const int ricochetWeapon = charged ? weapon.chargedRicochetWeapon : weapon.unchargedRicochetWeapon;
    const Vec3 dirVec = direction;
    const Vec3 rightVec = dirVec[0] != 0 || dirVec[2] != 0 ? normalized({dirVec[2], 0, -dirVec[0]}) : Vec3{1, 0, 0};
    const Vec3 upVec = normalized(cross(dirVec, rightVec));
    Vec3 velocity{};
    if (maxSpread <= 0) {
        velocity = direction * speed;
    }
    for (int i = 0; i < projectiles; i++) {
        // ChooseBeamSlot: a continuous beam replaces its own last one, else a
        // free slot, else the last.
        BeamProjectile* chosen = nullptr;
        auto reusable = [&](const BeamProjectile& b) {
            return (b.flags & BeamFlags::Continuous) && b.beamKind == weapon.beamKind && b.owner == owner
                && b.lifespan < weapon.unchargedLifespan;
        };
        if (wflags & WeaponFlags::Continuous) {
            for (BeamProjectile& b : slot.beams) {
                if (reusable(b)) {
                    chosen = &b;
                    break;
                }
            }
        }
        if (chosen == nullptr) {
            for (BeamProjectile& b : slot.beams) {
                if (b.lifespan <= 0 || reusable(b)) {
                    chosen = &b;
                    break;
                }
            }
        }
        if (chosen == nullptr) {
            chosen = &slot.beams.back();
        }
        BeamProjectile& beam = *chosen;
        if (beam.lifespan > 0 && !(beam.flags & BeamFlags::Collided)) {
            // The one being replaced ends where it is.
            CollisionResult end;
            end.position = beam.position;
            end.plane = plane(beam.direction * -1);
            beam.ricochetWeapon = -1;
            onBeamCollision(beam, end, -1);
        }
        if (beam.model) {
            m_scene.instances[*beam.model].visible = false;
        }
        endBeamEffect(beam);
        const std::optional<size_t> model = beam.model;
        beam = BeamProjectile{};
        beam.model = model;
        if (!charged) {
            equip.smokeLevel = std::min(equip.smokeLevel + weapon.smokeShotAmount, weapon.smokeStart);
        }
        beam.owner = owner;
        beam.fromHalfturret = fromHalfturret;
        beam.launchFrame = parent != nullptr ? parent->launchFrame : m_launchFrameFor ? m_launchFrameFor(owner) : 0;
        beam.beam = weapon.beam;
        beam.beamKind = weapon.beamKind;
        beam.flags = flags;
        beam.initialSpeed = beam.speed = speed;
        beam.finalSpeed = finalSpeed;
        beam.speedDecayTime = speedDecayTime;
        beam.speedInterpolation = weapon.speedInterpolations[c];
        beam.homing = homing;
        beam.drawFuncId = weapon.drawFuncIds[c];
        beam.color = color;
        beam.damageDirType = weapon.dmgDirTypes[c];
        beam.splashDamageType = weapon.splashDamageTypes[c];
        beam.damageDirMag = amount(weapon.unchargedDmgDirMag, weapon.minChargeDmgDirMag, weapon.chargedDmgDirMag) / 4096.0f;
        beam.spawnPosition = beam.backPosition = beam.position = position;
        beamSound(beam).update(position, 0);
        beam.pastPositions.fill(position);
        beam.direction = dirVec;
        beam.right = rightVec;
        beam.up = upVec;
        beam.damage = static_cast<float>(damage);
        beam.headshotDamage = static_cast<float>(hsDamage);
        beam.splashDamage = static_cast<float>(splashDamage);
        beam.splashRadius = amount(weapon.unchargedSplashRadius, weapon.minChargeSplashRadius, weapon.chargedSplashRadius) / 4096.0f;
        beam.damageInterpolation = weapon.damageInterpolations[c];
        beam.maxDistance = amount(weapon.unchargedDistance, weapon.minChargeDistance, weapon.chargedDistance) / 4096.0f;
        beam.afflictions = weapon.afflictions[c];
        beam.cylinderRadius = amount(weapon.unchargedCylRadius, weapon.minChargeCylRadius, weapon.chargedCylRadius) / 4096.0f;
        beam.lifespan = amount(weapon.unchargedLifespan, weapon.minChargeLifespan, weapon.chargedLifespan) * (1 / 30.0f);
        beam.ricochetLossH = amount(weapon.unchargedRicochetLossH, weapon.minChargeRicochetLossH, weapon.chargedRicochetLossH) / 4096.0f;
        beam.ricochetLossV = amount(weapon.unchargedRicochetLossV, weapon.minChargeRicochetLossV, weapon.chargedRicochetLossV) / 4096.0f;
        beam.ricochetWeapon = ricochetWeapon;
        beam.collisionEffect = weapon.collisionEffects[c];
        if (instantAoe) {
            // The Judicator's charged shot is the ice wave, and no projectile.
            spawnIceWave(beam, chargePct, weapon);
            beam.flags = BeamFlags::Collided;
            beam.lifespan = 0;
            return result;
        }
        if (maxSpread > 0) {
            const float angle1 = randomInt(static_cast<uint32_t>(maxSpread)) / 4096.0f * kDegToRad;
            const float angle2 = randomInt(0x168000) / 4096.0f * kDegToRad;
            const float s1 = std::sin(angle1), c1 = std::cos(angle1), s2 = std::sin(angle2), c2 = std::cos(angle2);
            for (int k = 0; k < 3; k++) {
                velocity[k] = direction[k] * c1 + (beam.up[k] * c2 + beam.right[k] * s2) * s1;
            }
            velocity = velocity * beam.speed;
        }
        beam.velocity = velocity;
        beam.acceleration = acceleration;
        if (debugBeams()) {
            qInfo("tick %lld: slot %d fires beam %d%s from (%.2f %.2f %.2f) dir (%.3f %.3f %.3f) speed %.3f damage %.0f", m_ticks, owner,
                weapon.beam, charged ? " charged" : "", position[0], position[1], position[2], direction[0], direction[1], direction[2],
                beam.speed, beam.damage);
        }
        if (beam.drawFuncId == 3) {
            // The Judicator's ice shard.
            beam.flags |= BeamFlags::HasModel;
            if (!beam.model) {
                if (const Model* shard = m_scene.model(m_root, "iceShard")) {
                    ModelInstance inst;
                    inst.model = shard;
                    inst.animation.set(shard->animations(), 0);
                    m_scene.instances.push_back(inst);
                    beam.model = m_scene.instances.size() - 1;
                }
            }
            if (beam.model) {
                ModelInstance& inst = m_scene.instances[*beam.model];
                inst.visible = true;
                inst.transform = Mat4::fromVectors(beam.direction, beam.up, beam.position);
            }
        } else if (beam.drawFuncId >= 0 && beam.drawFuncId < static_cast<int>(beamDrawEffects().size())) {
            // Metadata.BeamDrawEffects: the beams an effect draws.
            if (const int effectId = beamDrawEffects()[beam.drawFuncId]; effectId != 0) {
                beam.effect = m_effects->spawnEntry(effectId, Mat4::fromVectors(crossVector(beam.direction), beam.direction, beam.position));
                m_effects->setElementExtension(beam.effect, true);
            }
        }
        if (beam.flags & BeamFlags::Homing) {
            // CheckHomingTargets: the enemy (or door) nearest the line of flight,
            // within the weapon's cone and range.
            const float tolerance = fx(weapon.homingTolerance);
            const float range = fx(weapon.homingRange);
            const bool continuous = (wflags & WeaponFlags::Continuous) != 0;
            float curDiv = tolerance;
            auto consider = [&](const Vec3& targetPos, bool isPlayer, const Player* target) -> bool {
                const Vec3 between = targetPos - beam.position;
                const float distSqr = dot(between, between);
                if (!((continuous && beam.beamKind != Beam::Platform) || distSqr <= range * range) || distSqr <= 0 || length(beam.velocity) == 0) {
                    return false;
                }
                const float dist = std::sqrt(distSqr);
                const float div1 = dot(between, normalized(beam.velocity)) / dist;
                if (div1 < curDiv) {
                    return false;
                }
                if (continuous) {
                    const bool canTarget = !isPlayer || (!target->isAltForm() && !target->isMorphing()) || div1 >= fx(4006);
                    const float div2 = std::min(dist / range, 1.0f);
                    if (!canTarget || div1 < tolerance + div2 * (fx(4094) - tolerance)) {
                        return false;
                    }
                    result |= BeamResult::Homing;
                }
                curDiv = div1;
                return true;
            };
            for (const PlayerSlot& other : m_slots) {
                const Player& target = *other.player;
                if (target.slot() == owner || !target.inPlay() || (!beam.fromHalfturret && areAllies(target.teamIndex(), player.teamIndex()))) {
                    continue;
                }
                // PlayerEntity.GetPosition
                const Vec3 targetPos = target.position() + Vec3{0, target.isAltForm() ? 0.0f : 0.5f, 0};
                if (consider(targetPos, true, &target)) {
                    beam.targetPlayer = target.slot();
                    beam.targetDoor = -1;
                }
            }
            for (size_t d = 0; d < m_doors.size(); d++) {
                if (consider(m_doors[d].lockPosition, false, nullptr)) {
                    beam.targetDoor = static_cast<int>(d);
                    beam.targetPlayer = -1;
                }
            }
            if (weapon.beam == Beam::ShockCoil && player.shockCoilTarget() == beam.targetPlayer && phase % 2 == 0) {
                // Held on one target, the Shock Coil gains a point of damage every half second, up to 4.
                const int bonus = std::min(player.shockCoilTimer() / (30 * 2), 4);
                beam.damage += bonus;
                beam.splashDamage += bonus;
                beam.headshotDamage += bonus;
            }
        }
    }
    return result;
}

// ---- simulation -------------------------------------------------------------

SoundSource& World::beamSound(const BeamProjectile& beam)
{
    PlayerSlot& slot = m_slots.at(static_cast<size_t>(beam.owner));
    return *slot.beamSounds.at(static_cast<size_t>(&beam - slot.beams.data()));
}

void World::stopHomingSfx(BeamProjectile& beam)
{
    const int id = beamSfx(beam.beam, BeamSfx::Homing);
    if (id != -1) {
        beamSound(beam).stopSfx(id);
    }
}

void World::playBeamHitSfx(BeamProjectile& beam)
{
    stopHomingSfx(beam);
    const int id = beamSfx(beam.beam, (beam.flags & BeamFlags::Charged) ? BeamSfx::ChargeHit : BeamSfx::Hit);
    if (id != -1) {
        beamSound(beam).playSfx(id, false, true);
    }
}

void World::playRicochetSfx(BeamProjectile& beam)
{
    if ((beam.flags & BeamFlags::Charged) && beam.beam == Beam::Magmaul) {
        playBeamHitSfx(beam);
        return;
    }
    const int id = beamSfx(beam.beam, BeamSfx::Ricochet);
    if (id != -1) {
        const float amountA = beam.beam == Beam::Judicator ? static_cast<float>(randomInt1(0xFFFF)) : 0xFFFF * (beam.speed * 2) / fx(3300);
        beamSound(beam).playSfx(id, false, true, -1, false, false, amountA);
    }
}

void World::processBeams()
{
    for (PlayerSlot& slot : m_slots) {
        for (size_t i = 0; i < slot.beams.size(); i++) {
            BeamProjectile& beam = slot.beams[i];
            const bool alive = beam.lifespan > 0;
            if (beam.lifespan > 0 && !processBeam(beam)) {
                beam.lifespan = 0;
            }
            if (alive && beam.lifespan <= 0) {
                slot.beamSounds[i]->stopAllSfx(); // Destroy
            }
            if (beam.lifespan <= 0) {
                if (beam.model) {
                    m_scene.instances[*beam.model].visible = false;
                }
                endBeamEffect(beam); // Destroy
            }
        }
    }
}

bool World::processBeam(BeamProjectile& beam)
{
    // BeamProjectileEntity.Process
    beam.lifespan -= kFrameTime;
    if (beam.flags & BeamFlags::Collided) {
        return true;
    }
    const bool firstFrame = beam.age == 0;
    if ((beam.flags & BeamFlags::Continuous) && beam.age > 0) {
        // Continuous beams go as soon as they are no longer being replaced;
        // their owner's shot sound goes with the last one.
        stopHomingSfx(beam);
        m_slots[beam.owner].player->stopContinuousBeamSfx(beam.beam);
        return false;
    }
    beam.age += kFrameTime;
    beam.backPosition = beam.position;
    // Ten past positions, every other tick: the trail.
    if (m_ticks % 2 == 0) {
        for (size_t i = beam.pastPositions.size() - 1; i > 0; i--) {
            beam.pastPositions[i] = beam.pastPositions[i - 1];
        }
        beam.pastPositions[0] = beam.position;
    }
    if (beam.targetPlayer >= 0 && m_slots[beam.targetPlayer].player->health() == 0) {
        beam.targetPlayer = -1; // Message.Destroyed
    }
    auto targetPosition = [&](Vec3& out) {
        if (beam.targetPlayer >= 0) {
            const Player& target = *m_slots[beam.targetPlayer].player;
            out = target.position() + Vec3{0, target.isAltForm() ? 0.0f : 0.5f, 0};
            return true;
        }
        if (beam.targetDoor >= 0) {
            out = m_doors[beam.targetDoor].lockPosition;
            return true;
        }
        return false;
    };
    if ((beam.flags & BeamFlags::Homing) && (beam.flags & BeamFlags::Continuous)) {
        Vec3 target;
        if (targetPosition(target)) {
            beam.position = target;
        } else {
            beam.velocity = beam.velocity * 0.25f;
        }
    } else {
        beam.position = beam.position + beam.velocity;
        beam.velocity = beam.velocity + beam.acceleration * 0.5f;
        if (beam.speedDecayTime > 0 && beam.age <= beam.speedDecayTime) {
            const float magnitude = length(beam.velocity);
            if (magnitude > 0) {
                beam.speed = interpolated(beam.speedInterpolation, beam.initialSpeed, beam.finalSpeed, beam.age / beam.speedDecayTime);
                beam.velocity = beam.velocity * (beam.speed / magnitude);
            }
        }
    }
    beamSound(beam).update(beam.position, beam.beam == Beam::Missile ? 3 : 2);
    if (!(beam.flags & BeamFlags::Continuous) || firstFrame) {
        checkBeamCollision(beam);
    }
    Vec3 target;
    if ((beam.flags & BeamFlags::Homing) && !(beam.flags & BeamFlags::Continuous) && !(beam.flags & BeamFlags::Collided)
        && targetPosition(target)) {
        Vec3 accel = target - beam.position;
        accel = dot(accel, accel) > 0 ? normalized(accel) : Vec3{1, 0, 0};
        accel = accel * beam.speed;
        if (dot(accel, beam.velocity) >= 0) {
            accel = accel - beam.velocity;
            const float mag = length(accel);
            if (mag > beam.homing) {
                accel = accel * (beam.homing / mag);
            }
            beam.velocity = beam.velocity + accel;
        } else {
            beam.targetPlayer = beam.targetDoor = -1;
        }
    }
    if ((beam.flags & BeamFlags::Charged) && (beam.beam != Beam::Missile || (beam.flags & BeamFlags::Homing))) {
        // The affinity missile's beeping, in practice.
        const int id = beamSfx(beam.beam, BeamSfx::Homing);
        if (id != -1 && !(beam.flags & BeamFlags::Collided)) {
            beamSound(beam).playSfx(id, true);
        }
    }
    if (beam.model) {
        ModelInstance& inst = m_scene.instances[*beam.model];
        inst.visible = !(beam.flags & BeamFlags::Collided);
        inst.transform = Mat4::fromVectors(beam.direction, beam.up, beam.position);
    }
    if (beam.effect >= 0) {
        m_effects->setTransform(beam.effect, beam.position, Mat4::fromVectors(beam.direction, beam.up, {0, 0, 0}));
    }
    if (beam.lifespan <= 0 && !(beam.flags & BeamFlags::Collided)) {
        CollisionResult end;
        end.plane = plane(beam.direction * -1);
        end.position = beam.position;
        spawnCollisionEffect(beam, end, true);
        onBeamCollision(beam, end, -1);
        if (!(beam.flags & BeamFlags::Continuous)) {
            playBeamHitSfx(beam); // a continuous one's goes with its last beam
        }
    }
    return true;
}

void World::checkBeamCollision(BeamProjectile& beam)
{
    // BeamProjectileEntity.CheckCollision: the nearest of the range limit, the
    // room, doors, force fields and players along this tick's travel.
    CollisionResult anyRes;
    int hitPlayer = -1;
    bool hitHalfturret = false;
    int hitDoor = -1;
    bool hitForceField = false;
    bool noColEff = false; // the range ran out: no effect unless a door, force field or player is nearer
    float minDist = 2;
    if (beam.maxDistance > 0) {
        const Vec3 frontTravel = beam.position - beam.spawnPosition;
        const float dist = length(frontTravel);
        if (dist >= beam.maxDistance) {
            const Vec3 backTravel = beam.backPosition - beam.spawnPosition;
            const float d = dot(normalized(frontTravel), backTravel);
            float pct = 1;
            if (static_cast<int>(dist * 4096) != static_cast<int>(d * 4096)) {
                pct = (beam.maxDistance - d) / (dist - d);
            }
            if (pct < 2) {
                minDist = pct;
                anyRes.position = beam.backPosition + (beam.position - beam.backPosition) * pct;
                anyRes.plane = beam.drawFuncId == 4 ? Vec4{0, 1, 0, 0} : plane(beam.direction * -1);
                noColEff = true;
            }
        }
    }
    if ((beam.flags & BeamFlags::SurfaceCollision) && m_collision) {
        CollisionResult colRes;
        if (m_collision->checkBetweenPoints(beam.backPosition, beam.position, TestFlags::Beams, colRes) && colRes.distance < minDist) {
            if (dot(beam.backPosition, xyz(colRes.plane)) - colRes.plane[3] >= 0) {
                minDist = colRes.distance;
                anyRes = colRes;
            }
        }
        for (size_t d = 0; d < m_doors.size(); d++) {
            const Door& door = m_doors[d];
            if (door.open) {
                continue;
            }
            Vec4 doorPlane = plane(door.facing);
            if (dot(beam.backPosition - door.lockPosition, door.facing) < 0) {
                doorPlane = plane(door.facing * -1);
            }
            const Vec3 normal = xyz(doorPlane);
            const Vec3 wvec = door.lockPosition + normal * 0.4f;
            doorPlane[3] = normal[0] * wvec[0] + normal[1] * wvec[1] + normal[2] * wvec[2];
            CollisionResult res;
            if (checkCylinderIntersectPlane(beam.backPosition, beam.position, doorPlane, res) && res.distance < minDist) {
                const Vec3 between = res.position - door.lockPosition;
                if (dot(between, between) < door.radiusSquared) {
                    minDist = res.distance;
                    anyRes = res;
                    anyRes.field0 = 0;
                    anyRes.plane = doorPlane;
                    anyRes.flags = 0;
                    hitDoor = static_cast<int>(d);
                    hitForceField = false;
                    noColEff = false;
                }
            }
        }
        for (const ForceField& ff : m_forceFields) {
            CollisionResult res;
            if (ff.active && checkCylinderIntersectPlane(beam.backPosition, beam.position, ff.obstacle.plane, res) && res.distance < minDist) {
                const Vec3 between = res.position - ff.obstacle.position;
                const float up = dot(between, ff.obstacle.up);
                const float right = dot(between, ff.obstacle.right);
                if (std::fabs(up) <= ff.obstacle.height && std::fabs(right) <= ff.obstacle.width) {
                    minDist = res.distance;
                    anyRes = res;
                    anyRes.field0 = 0;
                    anyRes.plane = ff.obstacle.plane;
                    hitForceField = true;
                    hitDoor = -1;
                    noColEff = false;
                }
            }
        }
    }
    for (const PlayerSlot& slot : m_slots) {
        const Player& player = *slot.player;
        if (!player.inPlay()) {
            continue;
        }
        if (player.slot() == beam.owner && (!(beam.flags & BeamFlags::SelfDamage) || beam.age < 1 / 30.0f * 4)) {
            continue;
        }
        bool hit = false;
        CollisionResult playerRes;
        const float radii = player.volumeRadius() + beam.cylinderRadius;
        if (player.isAltForm()) {
            if (player.hunter() == Hunter::Kanden) {
                const auto& seg = player.kandenSegPositions();
                if (checkCylinderOverlapSphere(beam.backPosition, beam.position, seg[2], 1.6f, playerRes)) {
                    hit = checkCylinderOverlapSphere(beam.backPosition, beam.position, player.volumeCenter(), radii, playerRes)
                        || checkCylinderOverlapSphere(beam.backPosition, beam.position, seg[1], radii, playerRes)
                        || checkCylinderOverlapSphere(beam.backPosition, beam.position, seg[2], radii, playerRes)
                        || checkCylinderOverlapSphere(beam.backPosition, beam.position, seg[3], radii, playerRes);
                }
            } else {
                hit = checkCylinderOverlapSphere(beam.backPosition, beam.position, player.volumeCenter(), radii, playerRes);
            }
        } else {
            // The biped is an upright cylinder from its min to its max pickup height.
            const float minY = fx(player.values().MinPickupHeight);
            const Vec3 bottom = player.position() + Vec3{0, minY, 0};
            const float height = fx(player.values().MaxPickupHeight) - minY;
            hit = checkCylindersOverlap(beam.backPosition, beam.position, bottom, {0, 1, 0}, height, radii, playerRes);
        }
        if (hit && playerRes.distance < minDist) {
            minDist = playerRes.distance;
            anyRes = playerRes;
            hitPlayer = player.slot();
            hitHalfturret = false;
            hitDoor = -1;
            hitForceField = false;
            noColEff = false;
        }
        const Halfturret& turret = player.halfturret();
        if (turret.active && !(beam.fromHalfturret && beam.owner == player.slot())) {
            CollisionResult turretRes;
            if (checkCylinderOverlapSphere(beam.backPosition, beam.position, turret.position, beam.cylinderRadius + 0.45f, turretRes)
                && turretRes.distance < minDist) {
                minDist = turretRes.distance;
                anyRes = turretRes;
                hitPlayer = player.slot();
                hitHalfturret = true;
                hitDoor = -1;
                hitForceField = false;
                noColEff = false;
            }
        }
    }
    if (minDist < 0 || minDist > 1) {
        return;
    }
    beam.position = anyRes.position + xyz(anyRes.plane) * fx(204);
    bool ricochet = true;
    if (debugBeams()) {
        qInfo("tick %lld: beam %d of slot %d hits %s at (%.2f %.2f %.2f) after %.2f s", m_ticks, beam.beam, beam.owner,
            hitPlayer >= 0 ? "a player" : hitDoor >= 0 ? "a door" : hitForceField ? "a force field" : "the room", anyRes.position[0],
            anyRes.position[1], anyRes.position[2], beam.age);
    }
    if (hitPlayer >= 0) {
        Player& victim = *m_slots[hitPlayer].player;
        const Vec3 direction = damageDirection(beam, anyRes.position, victim.position());
        uint32_t damageFlags = DamageFlags::NoDmgInvuln | (hitHalfturret ? DamageFlags::Halfturret : 0);
        float damage = beam.damage;
        // A hit in the top 0.3 of the biped is a headshot: always for the
        // Imperialist, within 15 units for everything else but the Shock Coil.
        bool headshot = false;
        if (!victim.isAltForm() && beam.beam != Beam::ShockCoil
            && anyRes.position[1] - victim.position()[1] >= fx(victim.values().MaxPickupHeight) - 0.3f) {
            const Vec3 travel = beam.position - beam.spawnPosition;
            headshot = beam.beam == Beam::Imperialist || dot(travel, travel) <= 15 * 15;
        }
        if (headshot) {
            damage = beam.headshotDamage;
            if (std::fabs(beam.damage - beam.headshotDamage) > 1 / 4096.0f) {
                damageFlags |= DamageFlags::Headshot;
            }
        }
        if (beam.maxDistance > 0) {
            const float pct = length(beam.position - beam.spawnPosition) / beam.maxDistance;
            damage = interpolated(beam.damageInterpolation, damage, 0, pct);
        }
        const int whole = static_cast<int>(std::clamp(damage, 0.0f, 2147483647.0f));
        if (whole != 0) {
            damagePlayer(victim, beam, whole, damageFlags, direction);
        }
        m_slots[beam.owner].player->onImpact(hitPlayer, beam.beam == Beam::ShockCoil); // Message.Impact
        if ((beam.flags & BeamFlags::LifeDrain) && beam.owner != hitPlayer && !m_slots[beam.owner].player->primeHunter
            && !areAllies(m_slots[beam.owner].player->teamIndex(), victim.teamIndex())) {
            Player& healer = *m_slots[beam.owner].player;
            const int before = healer.health();
            healer.gainHealth(whole);
            if (m_netHooks != nullptr) {
                m_netHooks->noteDrain(healer, healer.health() - before); // NetHitPrediction.NoteDrain
            }
        }
        if (hitPlayer != 0 || victim.isAltForm() || victim.isMorphing()) {
            spawnCollisionEffect(beam, anyRes, true);
        }
        onBeamCollision(beam, anyRes, hitPlayer);
        playBeamHitSfx(beam);
        ricochet = false;
    } else if (hitDoor >= 0) {
        spawnCollisionEffect(beam, anyRes, true);
        onBeamCollision(beam, anyRes, -1);
        playBeamHitSfx(beam);
        if (beam.owner == 0) {
            m_doors[hitDoor].shotOpen = true; // DoorFlags.ShotOpen
        }
        ricochet = false;
    } else if (hitForceField) {
        if (!(beam.flags & BeamFlags::Ricochet)) {
            spawnCollisionEffect(beam, anyRes, true);
            onBeamCollision(beam, anyRes, -1);
            playBeamHitSfx(beam);
            ricochet = false;
        }
    } else {
        // The room.
        const bool reflected = (anyRes.flags & kCollisionReflectBeams) != 0;
        if ((!(beam.flags & BeamFlags::Ricochet) && !reflected) || beam.drawFuncId == 8 || anyRes.terrain() >= kTerrainAcid) {
            if (!noColEff || (beam.flags & BeamFlags::ForceEffect)) {
                spawnCollisionEffect(beam, anyRes, anyRes.terrain() == kTerrainLava);
            }
            if (beam.ricochetWeapon >= 0) {
                playRicochetSfx(beam);
            } else if (anyRes.terrain() <= kTerrainLava) {
                playBeamHitSfx(beam);
            } else {
                beamSound(beam).playSfx(SfxId::GENERIC_HIT, false, true);
            }
            onBeamCollision(beam, anyRes, -1);
            ricochet = false;
        }
    }
    if (ricochet) {
        processRicochet(beam, anyRes);
    }
    if (beam.drawFuncId == 8) {
        // SpawnSniperBeam: the Imperialist's line, from the muzzle to the hit.
        const Vec3 spawnPos = beam.pastPositions[8];
        Vec3 up = beam.position - spawnPos;
        const float magnitude = length(up);
        if (magnitude > 0) {
            up = up * (1 / magnitude);
            const Mat4 transform = Mat4::scale(1, magnitude, 1) * Mat4::fromVectors(crossVector(up), up, spawnPos);
            spawnBeamEffect("sniperBeam", transform);
        }
    }
}

void World::processRicochet(BeamProjectile& beam, const CollisionResult& res)
{
    // BeamProjectileEntity.ProcessRicochet: reflect off the plane, losing speed.
    const Vec3 normal = xyz(res.plane);
    const float d1 = dot(beam.velocity, normal);
    beam.velocity = {(beam.velocity[0] - 2 * normal[0] * d1) * beam.ricochetLossH, (beam.velocity[1] - 2 * normal[1] * d1) * beam.ricochetLossV,
        (beam.velocity[2] - 2 * normal[2] * d1) * beam.ricochetLossH};
    beam.speed = length(beam.velocity);
    const float d2 = dot(beam.direction, normal);
    beam.direction = beam.direction - normal * (2 * d2);
    const float factor = 0.01f - (dot(res.position, normal) - res.plane[3]);
    beam.backPosition = beam.position = res.position + normal * factor;
    auto shift = [&] {
        for (size_t i = beam.pastPositions.size() - 1; i > 0; i--) {
            beam.pastPositions[i] = beam.pastPositions[i - 1];
        }
        beam.pastPositions[0] = beam.position;
    };
    shift();
    if (m_ticks % 2 == 0) {
        shift();
    }
    playRicochetSfx(beam);
}

void World::onBeamCollision(BeamProjectile& beam, const CollisionResult& res, int hitPlayer)
{
    // BeamProjectileEntity.OnCollision: splash damage, a ricochet weapon's
    // children, and the beam stops.
    endBeamEffect(beam);
    if (beam.splashDamage > 0 && res.terrain() <= kTerrainLava) {
        checkSplashDamage(beam, hitPlayer);
        if (beam.ricochetWeapon >= 0 && hitPlayer < 0 && beam.ricochetWeapon < static_cast<int>(ricochetWeapons().size())) {
            const Vec3 normal = xyz(res.plane);
            const Vec3 factor = beam.velocity * 7;
            const float d = dot(normal, factor);
            const Vec3 spawnDir = normalized(normal + factor - normal * (2 * d));
            EquipInfo equip;
            equip.weapon = &ricochetWeapons()[beam.ricochetWeapon];
            const int flags = (beam.flags & BeamFlags::Charged) ? BeamSpawnFlags::Charged : 0;
            // The children draw from the same pool as their parent; copy what is needed first.
            const BeamProjectile parent = beam;
            spawnBeam(parent.owner, equip, *equip.weapon, res.position, spawnDir, flags, &parent);
        }
    }
    if (!(beam.flags & BeamFlags::Continuous)) {
        beam.flags |= BeamFlags::Collided;
        beam.lifespan = 4 * (1 / 30.0f);
        beam.velocity = {};
    }
}

void World::checkSplashDamage(BeamProjectile& beam, int hitPlayer)
{
    // BeamProjectileEntity.CheckSplashDamage: everybody in the radius with a
    // clear line to the blast, the shooter included.
    for (PlayerSlot& slot : m_slots) {
        Player& player = *slot.player;
        if (player.slot() == hitPlayer || !player.inPlay()) {
            continue;
        }
        if (beam.fromHalfturret && beam.owner == player.slot()) {
            continue; // its own turret's blast spares Weavel
        }
        const float dist = length(player.position() - beam.position);
        CollisionResult discard;
        if (dist >= beam.splashRadius
            || (m_collision && m_collision->checkBetweenPoints(beam.position, player.position(), TestFlags::Beams, discard))) {
            continue;
        }
        const Vec3 direction = damageDirection(beam, beam.position, player.position());
        const int damage = static_cast<int>(interpolated(beam.splashDamageType, beam.splashDamage, 0, dist / beam.splashRadius));
        damagePlayer(player, beam, damage, DamageFlags::NoDmgInvuln, direction);
    }
}

void World::spawnIceWave(BeamProjectile& beam, float chargePct, const WeaponInfo& weapon)
{
    // BeamProjectileEntity.SpawnIceWave: a 60-degree wedge in front of the
    // shooter. The cartridge flattens the wedge into a column of infinite
    // height (GameState.ShadowFreeze, on by default), and so does this.
    float angle = chargePct <= 0 ? weapon.unchargedSpread : weapon.minChargeSpread + (weapon.chargedSpread - weapon.minChargeSpread) * chargePct;
    angle /= 4096.0f;
    const float angleCos = std::cos(angle * kDegToRad);
    for (PlayerSlot& slot : m_slots) {
        Player& player = *slot.player;
        if (player.slot() == beam.owner || !player.inPlay()) {
            continue;
        }
        auto check = [&](const Vec3& position, uint32_t flags) {
            Vec3 between = position - beam.position;
            between = between - beam.up * dot(between, beam.up);
            const float mag = length(between);
            if (mag < beam.maxDistance && mag > 0 && dot(between * (1 / mag), beam.direction) > angleCos) {
                damagePlayer(player, beam, static_cast<int>(beam.damage), DamageFlags::NoDmgInvuln | flags,
                    damageDirection(beam, beam.position, player.position()));
            }
        };
        check(player.position(), 0);
        if (player.halfturret().active) {
            check(player.halfturret().position, DamageFlags::Halfturret);
        }
    }
    const Vec3 up = beam.direction;
    const Vec3 facing = up[0] != 0 || up[2] != 0 ? normalized(cross(up, cross({0, 1, 0}, up))) : normalized(cross(up, cross({1, 0, 0}, up)));
    spawnBeamEffect("iceWave", Mat4::scale(beam.maxDistance) * Mat4::fromVectors(facing, up, beam.position));
    m_effects->spawn(78, Mat4::fromVectors(facing, up, beam.position)); // iceWave's particles
}

void World::endBeamEffect(BeamProjectile& beam)
{
    // OnCollision and Destroy: the effect's particles in flight live out their time.
    if (beam.effect >= 0) {
        m_effects->detach(beam.effect, true);
        beam.effect = -1;
    }
}

void World::spawnCollisionEffect(const BeamProjectile& beam, const CollisionResult& res, bool noSplat)
{
    // BeamProjectileEntity.SpawnCollisionEffect and BeamEffectEntity.Create.
    int type = beam.collisionEffect;
    if (type == 255) {
        return;
    }
    if (m_slots.size() > 2 && type == 4) {
        noSplat = true; // powerBeam
    }
    const Vec3 normal = xyz(res.plane);
    const Vec3 spawnPos = res.position + normal * (1 / 8.0f);
    const Vec3 up = beam.beam == Beam::Imperialist ? beam.direction * -1 : normal;
    const Mat4 transform = Mat4::fromVectors(crossVector(up), up, spawnPos);
    if (type < 3) {
        // Beam effect models: the ice wave, the Imperialist's line (drawn on their own already).
        return;
    }
    int effectId = type - 3;
    if (noSplat) {
        if (effectId == 1) {
            effectId = 2; // powerBeamNoSplat
        } else if (effectId == 92) {
            effectId = 98;
        }
    }
    m_effects->spawn(effectId, transform);
}

void World::spawnBeamEffect(const char* name, const Mat4& transform)
{
    // BeamEffectEntity: the model plays its one animation once.
    const Model* model = m_scene.model(m_root, name);
    if (model == nullptr) {
        return;
    }
    BeamEffect* effect = nullptr;
    for (BeamEffect& e : m_beamEffects) {
        if (e.lifespan <= 0 && m_scene.instances[e.instance].model == model) {
            effect = &e;
            break;
        }
    }
    if (effect == nullptr) {
        ModelInstance inst;
        inst.model = model;
        m_scene.instances.push_back(inst);
        m_beamEffects.push_back({m_scene.instances.size() - 1, 0});
        effect = &m_beamEffects.back();
    }
    ModelInstance& inst = m_scene.instances[effect->instance];
    inst.transform = transform;
    inst.visible = true;
    inst.animation.set(model->animations(), 0, AnimFlags::NoLoop);
    const AnimationSet& anims = model->animations();
    effect->lifespan = 0;
    if (!anims.node.empty() && anims.node[0].frameCount > 0) {
        effect->lifespan = (anims.node[0].frameCount - 1) * 2;
    } else if (!anims.material.empty()) {
        effect->lifespan = (anims.material[0].frameCount - 1) * 2;
    }
}

void World::processBeamEffects()
{
    for (BeamEffect& effect : m_beamEffects) {
        if (effect.lifespan > 0 && --effect.lifespan <= 0) {
            m_scene.instances[effect.instance].visible = false;
        }
    }
}

Vec3 World::damageDirection(const BeamProjectile& beam, const Vec3& beamPos, const Vec3& targetPos) const
{
    // BeamProjectileEntity.GetDamageDirection: the knockback.
    switch (beam.damageDirType) {
    case 1: {
        const Vec3 dir = length(beam.velocity) > 0 ? normalized(beam.velocity) : Vec3{0, 1, 0};
        return dir * beam.damageDirMag;
    }
    case 2: {
        Vec3 dir = targetPos - beamPos;
        if (dot(dir, dir) > 0) {
            dir = normalized(dir);
            dir[1] = std::max(dir[1] / 2, 0.03f);
        } else {
            dir = {0, 0.03f, 0};
        }
        return dir * beam.damageDirMag;
    }
    case 3: {
        Vec3 dir = targetPos - beamPos;
        dir[1] = 0;
        return dot(dir, dir) > 0 ? normalized(dir) * beam.damageDirMag : dir;
    }
    case 4:
        return {0, beam.damageDirMag, 0};
    default:
        return {};
    }
}

void World::damagePlayer(Player& victim, BeamProjectile& beam, int damage, uint32_t flags, const Vec3& direction)
{
    if (victim.slot() != 0 && victim.health() > 0 && beam.beam != Beam::OmegaCannon) { // 162 is not the Omega Cannon's
        // SpawnDamageEffect: mpEffectivePB and so on, where a hit lands on somebody else.
        m_effects->spawn(beam.beam + 154, Mat4::fromVectors({1, 0, 0}, {0, 1, 0}, beam.position));
    }
    DamageSource source;
    source.attacker = m_slots[beam.owner].player.get();
    source.beam = beam.beam;
    source.afflictions = beam.afflictions;
    source.beamVelocity = beam.velocity;
    source.fromHalfturret = beam.fromHalfturret;
    source.launchFrame = beam.launchFrame;
    source.flight = beam.age;
    victim.takeDamage(damage, flags, &direction, source); // World::onDamage hears of it
}

// ---- drawing ----------------------------------------------------------------

void World::drawBeams()
{
    // BeamProjectileEntity.GetDrawInfo: trails and the fuzzball, as the C#
    // renderer's TrailSingle, TrailMulti and Particle items.
    std::vector<Vertex>& vertices = m_scene.dynamicVertices;
    std::vector<DynamicDraw>& draws = m_scene.dynamicDraws;
    vertices.clear();
    draws.clear();
    auto vertex = [](const Vec3& pos, float u, float v, const Vec3& color) {
        Vertex out{};
        out.pos[0] = pos[0];
        out.pos[1] = pos[1];
        out.pos[2] = pos[2];
        out.normal[2] = 1;
        out.color[0] = color[0];
        out.color[1] = color[1];
        out.color[2] = color[2];
        out.color[3] = 1;
        out.uv[0] = u;
        out.uv[1] = v;
        return out;
    };
    // A quad strip of (uv, vertex) pairs, as triangles, relative to `origin`.
    auto strip = [&](const BeamTexture& tex, const Vec3& origin, const std::vector<std::pair<std::array<float, 2>, Vec3>>& points,
                     const Vec3& color, float alpha) {
        if (tex.model == nullptr || points.size() < 4) {
            return;
        }
        DynamicDraw draw;
        draw.model = tex.model;
        draw.textureId = tex.textureId;
        draw.paletteId = tex.paletteId;
        draw.xRepeat = tex.xRepeat;
        draw.yRepeat = tex.yRepeat;
        draw.transform = Mat4::translation(origin[0], origin[1], origin[2]);
        draw.alpha = alpha;
        draw.firstVertex = static_cast<uint32_t>(vertices.size());
        for (size_t i = 0; i + 3 < points.size(); i += 2) {
            const auto& a = points[i];
            const auto& b = points[i + 1];
            const auto& c = points[i + 2];
            const auto& d = points[i + 3];
            for (const auto* p : {&a, &b, &c, &b, &d, &c}) {
                vertices.push_back(vertex(p->second, p->first[0], p->first[1], color));
            }
        }
        draw.vertexCount = static_cast<uint32_t>(vertices.size()) - draw.firstVertex;
        draws.push_back(draw);
    };
    auto fuzzball = [&](const Vec3& position, const Vec3& color) {
        // SingleParticle: a camera-facing quad, a quarter unit across each way.
        const BeamTexture& tex = m_fuzzball;
        if (tex.model == nullptr) {
            return;
        }
        const float scale = 0.25f;
        DynamicDraw draw;
        draw.model = tex.model;
        draw.textureId = tex.textureId;
        draw.paletteId = tex.paletteId;
        draw.xRepeat = tex.xRepeat;
        draw.yRepeat = tex.yRepeat;
        draw.transform = Mat4::translation(position[0], position[1], position[2]);
        draw.billboard = true;
        draw.firstVertex = static_cast<uint32_t>(vertices.size());
        const Vertex v0 = vertex({-scale, scale, 0}, 0, 0, color);
        const Vertex v1 = vertex({scale, scale, 0}, 1, 0, color);
        const Vertex v2 = vertex({scale, -scale, 0}, 1, 1, color);
        const Vertex v3 = vertex({-scale, -scale, 0}, 0, 1, color);
        for (const Vertex* v : {&v0, &v1, &v2, &v0, &v2, &v3}) {
            vertices.push_back(*v);
        }
        draw.vertexCount = 6;
        draws.push_back(draw);
    };
    // DrawTrail1: one quad from the last position to this one.
    auto trail1 = [&](const BeamProjectile& beam, const BeamTexture& tex, float height, float alpha) {
        const float uvS = (tex.width - 1 / 16.0f) / tex.width;
        const float uvT = (tex.height - 1 / 16.0f) / tex.height;
        const Vec3 d = beam.position - beam.backPosition;
        strip(tex, beam.backPosition,
            {{{0, 0}, {d[0], d[1] - height, d[2]}}, {{0, uvT}, {d[0], d[1] + height, d[2]}}, {{uvS, 0}, {0, -height, 0}},
                {{uvS, uvT}, {0, height, 0}}},
            beam.color, alpha);
    };
    // DrawTrail2: segments through every other past position.
    auto trail2 = [&](const BeamProjectile& beam, const BeamTexture& tex, float height, int segments, float alpha) {
        segments = std::min(segments, static_cast<int>(beam.pastPositions.size() / 2));
        if (segments < 2) {
            return;
        }
        const float uvT = (tex.height - 1 / 16.0f) / tex.height;
        std::vector<std::pair<std::array<float, 2>, Vec3>> points;
        for (int i = 0; i < segments; i++) {
            const float uvS = i > 0 ? (tex.width / static_cast<float>(segments - 1) * i - 1 / 16.0f) / tex.width : 0;
            const Vec3 d = beam.pastPositions[i * 2] - beam.pastPositions[0];
            points.push_back({{uvS, 0}, {d[0], d[1] - height, d[2]}});
            points.push_back({{uvS, uvT}, {d[0], d[1] + height, d[2]}});
        }
        strip(tex, beam.pastPositions[0], points, beam.color, alpha);
    };
    // DrawTrail4: the Shock Coil's jittering arc.
    auto trail4 = [&](const BeamProjectile& beam, const BeamTexture& tex, float height, float range, int segments) {
        const int frames = static_cast<int>(m_ticks / 2);
        uint32_t rng = static_cast<uint32_t>(frames + static_cast<int>(beam.position[0] * 4096));
        auto callRng = [&](uint32_t max) {
            rng = rng * 0x7FF8A3ED + 0x2AA01D31;
            return static_cast<uint32_t>((static_cast<uint64_t>(rng >> 16) * max) >> 16);
        };
        const int index = frames & 15;
        const Vec3 d = beam.position - beam.pastPositions[8];
        const float uvT = (tex.height - 1 / 16.0f) / tex.height;
        std::vector<std::pair<std::array<float, 2>, Vec3>> points;
        for (int i = 0; i < segments; i++) {
            const int factor = index + i;
            const float uvS = factor > 0 ? (2 * tex.width / static_cast<float>(segments - 1) * factor - 1 / 16.0f) / tex.width : 0;
            const float pct = static_cast<float>(i) / (segments - 1);
            Vec3 p = d * pct + beam.velocity * (0.25f * pct * (1 - pct));
            if (i > 0 && i < segments - 1) {
                for (float& c : p) {
                    c += callRng(static_cast<uint32_t>(range * 4096)) / 4096.0f - range / 2;
                }
            }
            points.push_back({{uvS, 0}, {p[0], p[1] - height, p[2]}});
            points.push_back({{uvS, uvT}, {p[0], p[1] + height, p[2]}});
        }
        strip(tex, beam.pastPositions[8], points, beam.color, 1);
    };
    for (const PlayerSlot& slot : m_slots) {
        for (const BeamProjectile& beam : slot.beams) {
            if (beam.lifespan <= 0) {
                continue;
            }
            const bool collided = beam.flags & BeamFlags::Collided;
            const float alpha = std::clamp(beam.lifespan * 30 * 8, 0.0f, 31.0f) / 31;
            switch (beam.drawFuncId) {
            case 0: // Power Beam
                if (!collided) {
                    fuzzball(beam.position, beam.color);
                }
                trail1(beam, m_trail, fx(122), alpha);
                break;
            case 1: // uncharged Volt Driver
                trail1(beam, m_electroTrail, fx(614), alpha);
                break;
            case 2: // charged Volt Driver
                trail2(beam, m_electroTrail, fx(1024), 5, alpha);
                break;
            case 3: // Judicator (the shard is a model)
                trail2(beam, m_trail, fx(204), 5, alpha);
                break;
            case 7: // Missile
                if (!collided) {
                    fuzzball(beam.position, {1, 1, 1});
                }
                trail2(beam, m_trail, fx(204), 5, alpha);
                break;
            case 9: // Shock Coil
                if (!collided) {
                    if (beam.targetPlayer >= 0 || beam.targetDoor >= 0) {
                        trail4(beam, m_arcWelder, 0.15f, 0.5f, 10);
                    } else if (beam.owner == 0) {
                        trail4(beam, m_arcWelder, 0.025f, 0.35f, 5);
                    }
                }
                break;
            case 10: // Battlehammer
                trail2(beam, m_trail, fx(81), 2, alpha);
                break;
            default:
                // Magmaul (4, 5), Imperialist (8) and Omega Cannon (11) are
                // drawn by effects, which are not ported yet.
                break;
            }
        }
    }
}

} // namespace fp
