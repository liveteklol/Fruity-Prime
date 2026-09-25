// HalfturretEntity: Weavel's legs, left standing as a turret when he goes
// into alt form. It takes half his health, shoots the Battlehammer at the
// nearest enemy, and gives back what is left when he unmorphs.

#include "Player.h"

#include <algorithm>
#include <cmath>

namespace fp {

namespace {

float fx(int32_t raw) { return raw / 4096.0f; }

Vec3 normalized(Vec3 v)
{
    const float len = std::sqrt(dot(v, v));
    return len > 0 ? v * (1.0f / len) : v;
}

// HalfturretEntity.UpdateAim: aim above the target to make up for the shot's drop.
Vec3 updateAim(const Vec3& muzzlePos, const Vec3& targetPos, const EquipInfo& equip)
{
    const WeaponInfo& weapon = *equip.weapon;
    float chargePct = 0;
    if ((static_cast<uint32_t>(weapon.flags) & WeaponFlags::CanCharge) && equip.chargeLevel >= weapon.minCharge * 2) {
        chargePct = static_cast<float>(equip.chargeLevel - weapon.minCharge * 2) / (weapon.fullCharge * 2 - weapon.minCharge * 2);
    }
    Vec3 aim = targetPos - muzzlePos;
    const float hMagSqr = aim[0] * aim[0] + aim[2] * aim[2];
    const float hMag = std::sqrt(hMagSqr);
    const float uncSpeed = fx(weapon.unchargedSpeed);
    const float speed = (fx(weapon.minChargeSpeed) - uncSpeed) * chargePct;
    const float uncGravity = fx(weapon.unchargedGravity);
    const float gravity = (fx(weapon.minChargeGravity) - uncGravity) * chargePct;
    float v23 = hMagSqr * (uncGravity + gravity) / ((uncSpeed + speed) * (uncSpeed + speed));
    const float v24 = v23 / 2;
    if (v24 >= 1 / 4096.0f || v24 <= -1 / 4096.0f) {
        if (static_cast<int>(v23 * 4096) & 1) {
            v23 -= 1 / 4096.0f;
        }
        const float v26 = hMagSqr - 4 * v24 * (v24 - aim[1]);
        if (v26 > 0) {
            aim[1] = (std::sqrt(v26) - hMag) / v23 * hMag;
        } else if (v26 > -1 / 4096.0f) {
            aim[1] = -hMag / v23 * hMag;
        } else {
            aim[1] = hMag;
        }
    }
    return aim[0] != 0 || aim[1] != 0 || aim[2] != 0 ? normalized(aim) : Vec3{1, 0, 0};
}

} // namespace

void Player::createHalfturret()
{
    // EnterAltForm and HalfturretEntity.Initialize: the legs stay where
    // Weavel stood, with half his health.
    Halfturret& t = m_halfturret;
    t = Halfturret{};
    t.active = true;
    t.position = {m_position[0], m_position[1] + fx(v.MinPickupHeight) + 0.45f, m_position[2]};
    t.facing = {m_field70, 0, m_field74};
    t.aimVector = t.facing;
    if (m_health > 1) {
        t.health = m_health / 2;
        m_health -= t.health;
    } else {
        t.health = 1;
    }
    t.grounded = m_standing;
    t.equip.weapon = &weaponsMP().at(Beam::Battlehammer); // the non-affinity one
    t.animation = 1;
}

void Player::halfturretDie()
{
    // HalfturretEntity.Die
    m_halfturret.active = false; // OnHalfturretDied
    if (m_halfturret.health > 0) {
        m_halfturret.health = 0;
        spawnEffect(216, {1, 0, 0}, {0, 1, 0}, m_halfturret.position, false); // deathAlt
    }
}

void Player::halfturretOnTakeDamage(Player* attacker, int damage)
{
    // HalfturretEntity.OnTakeDamage: it turns on whoever hurt its owner, and fires faster.
    m_halfturret.target = attacker->slot();
    m_halfturret.targetTimer = 30 * 2;
    m_halfturret.cooldownFactor = std::max(m_halfturret.cooldownFactor - 61.0f * damage, 0.7f);
}

void Player::processHalfturret(float killHeight)
{
    // HalfturretEntity.Process
    Halfturret& t = m_halfturret;
    if (!t.active || t.health == 0) {
        t.active = false;
        return;
    }
    if (t.burnTimer > 0) {
        t.burnTimer--;
        if (t.burnTimer % (8 * 2) == 0) {
            DamageSource source;
            source.attacker = m_burnedBy;
            takeDamage(1, DamageFlags::NoSfx | DamageFlags::Burn | DamageFlags::NoDmgInvuln | DamageFlags::Halfturret, nullptr, source);
            if (!t.active) {
                return;
            }
        }
    }
    if (t.freezeTimer == 0) {
        if (t.targetTimer > 0) {
            t.targetTimer--;
        } else {
            t.target = -1;
        }
        if (t.cooldownFactor < 1.5f) {
            t.cooldownFactor = std::min(t.cooldownFactor + 0.015f / 2, 1.5f);
        } else if (t.cooldownFactor > 1.5f) {
            t.cooldownFactor = std::max(t.cooldownFactor - 0.015f / 2, 1.5f);
        }
        if (t.target < 0 && m_players != nullptr) {
            // The nearest enemy within 15 units.
            float minDistSqr = 15 * 15;
            for (Player* player : *m_players) {
                if (player == this || player->m_health == 0 || areAllies(player->m_teamIndex, m_teamIndex) || player->m_curAlpha < 6 / 31.0f) {
                    continue;
                }
                const Vec3 between = player->m_position - t.position;
                const float distSqr = dot(between, between);
                if (distSqr < minDistSqr) {
                    minDistSqr = distSqr;
                    t.target = player->m_slot;
                }
            }
        }
        const Player* target = nullptr;
        if (t.target >= 0 && m_players != nullptr) {
            for (Player* player : *m_players) {
                if (player->m_slot == t.target) {
                    target = player;
                }
            }
        }
        if (target != nullptr) {
            const Vec3 muzzlePos{t.position[0], t.position[1] + 0.4f, t.position[2]};
            t.cooldownTimer = 1;
            t.aimVector = updateAim(muzzlePos, target->m_position, t.equip);
            float cooldown = t.equip.weapon->shotCooldown * t.cooldownFactor;
            if (cooldown < 7.5f) {
                cooldown = 7;
            }
            if (m_timeSinceShot >= cooldown * 2 && t.cooldownTimer < 60 * 2 && m_fire) {
                int flags = BeamSpawnFlags::FromHalfturret;
                if (m_doubleDmgTimer > 0) {
                    flags |= BeamSpawnFlags::DoubleDamage;
                }
                if (m_fire(*this, t.equip, muzzlePos, t.aimVector, flags) != BeamResult::NoSpawn) {
                    t.animation = 0;
                    m_timeSinceShot = 0;
                }
            }
        }
    } else {
        t.freezeTimer--;
        t.timeSinceFrozen = 0;
    }
    if (t.timeSinceFrozen < 0xFFFF) {
        t.timeSinceFrozen++;
    }
    if (t.timeSinceDamage < 0xFFFF) {
        t.timeSinceDamage++;
    }
    if (!t.grounded && m_collision != nullptr) {
        // It falls until it lands, and stays there.
        const Vec3 prevPos = t.position;
        t.ySpeed -= 0.02f / 2;
        t.position[1] += t.ySpeed / 2;
        const float margin = 0.45f;
        const Vec3 lo{std::min(prevPos[0], t.position[0]) - margin, std::min(prevPos[1], t.position[1]) - margin,
            std::min(prevPos[2], t.position[2]) - margin};
        const Vec3 hi{std::max(prevPos[0], t.position[0]) + margin, std::max(prevPos[1], t.position[1]) + margin,
            std::max(prevPos[2], t.position[2]) + margin};
        CollisionResult results[1];
        if (m_collision->checkSphereBetweenPoints(m_collision->candidates(lo, hi), prevPos, t.position, 0.45f, 1, false, 0, results) > 0) {
            const CollisionResult& result = results[0];
            const Vec3 normal = xyz(result.plane);
            const float d = 0.45f - (dot(t.position, normal) - result.plane[3]);
            t.position = result.position + normal * d;
            t.ySpeed = 0;
            t.grounded = true;
        }
    }
    if (t.position[1] < killHeight) {
        halfturretDie();
    }
}

bool Player::checkHalfturretHitByBomb(const BombHit& bomb)
{
    // CheckHitByBomb(halfturret: true)
    if (!m_halfturret.active || bomb.owner == this) {
        return false;
    }
    const Vec3 between = m_halfturret.position - bomb.position;
    if (dot(between, between) <= bomb.radius * bomb.radius) {
        DamageSource source;
        source.attacker = bomb.owner;
        source.bomb = bomb.type;
        takeDamage(bomb.damage, DamageFlags::NoDmgInvuln | DamageFlags::Halfturret, nullptr, source);
        return true;
    }
    return false;
}

void Player::checkHalfturretCollision(Player& other)
{
    // CheckPlayerCollision: the turret is a 0.45 sphere players bump into,
    // and a boost, Deathalt or an alt attack hurts it.
    Halfturret& t = m_halfturret;
    Vec3 toTurret = other.sphereCenter() - t.position;
    const float radius = other.volumeRadius() + 0.45f + 0.1f;
    if (dot(toTurret, toTurret) <= radius * radius) {
        if (toTurret[1] < 0) {
            toTurret[1] = 0;
        }
        toTurret = dot(toTurret, toTurret) > 0 ? normalized(toTurret) : Vec3{1, 0, 0};
        CollisionResult res;
        res.field0 = 0;
        const Vec3 onSurface = t.position + toTurret * 0.45f;
        res.plane = {toTurret[0], toTurret[1], toTurret[2], dot(onSurface, toTurret)};
        other.handleCollision(res);
        if (&other != this) {
            DamageSource source;
            source.attacker = &other;
            const Vec3 speed = other.m_speed;
            if (other.m_boosting) {
                takeDamage(other.m_boostDamage, DamageFlags::NoDmgInvuln | DamageFlags::Halfturret, &speed, source);
                other.endAltAttack();
            }
            if (other.m_deathaltTimer > 0) {
                takeDamage(200, DamageFlags::Deathalt | DamageFlags::NoDmgInvuln | DamageFlags::Halfturret, &speed, source);
            }
            if (t.active) {
                checkAltAttackHit2(other, *this, true);
            }
        }
    }
    if (t.active) {
        checkAltAttackHit1(other, *this, true);
    }
}

} // namespace fp
