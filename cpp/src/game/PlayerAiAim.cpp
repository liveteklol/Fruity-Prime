// PlayerAiData: targets, aiming and shooting.
#include "PlayerAi.h"

#include "PlayerAiUtil.h"
#include "World.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <numbers>

#pragma GCC diagnostic ignored "-Wunused-parameter"

namespace fp {

using namespace ai;

namespace {

constexpr float kRadToDeg = 180.0f / std::numbers::pi_v<float>;

// dword_214C75C, dword_214C750, by bot level: past this dot the turn is exact,
// short of it a fixed swing. Insane (3) always turns exactly onto the target.
constexpr float kDotValues[4] = {255 / 256.0f, 3956 / 4096.0f, 3849 / 4096.0f, -1.0f};
constexpr float kAimValues[4] = {5, 15, 20, 180};

float acosDegrees(float value) { return std::acos(std::clamp(value, -1.0f, 1.0f)) * kRadToDeg; }

} // namespace

float PlayerAi::AimValue() const { return kAimValues[std::clamp(_player->m_botLevel, 0, 3)]; }
float PlayerAi::DotValue() const { return kDotValues[std::clamp(_player->m_botLevel, 0, 3)]; }

void PlayerAi::Func21356C0(Player* player)
{
    if (Flags2 & AiFlags2::Bit9) {
        Flags2 &= ~AiFlags2::TargetPlayer;
    } else {
        Flags2 |= AiFlags2::TargetPlayer;
    }
    if (player != _targetPlayer) {
        _targetPlayer = player;
        _entityRefs.Nodes[2] = nullptr;
        _entityRefs.Nodes[17] = nullptr;
        _entityRefs.Nodes[18] = nullptr;
        _entityRefs.Players[29 - 26] = nullptr;
    }
    if (_targetPlayer == nullptr) {
        Flags2 &= ~AiFlags2::TargetPlayer;
    }
}

void PlayerAi::Func2135624(const NodeDefense* defense)
{
    if (defense != nullptr) {
        Flags2 |= AiFlags2::TargetDefense;
        if (_targetDefense != defense) {
            _targetDefense = defense;
            _entityRefs.Nodes[15] = nullptr;
        }
    }
}

void PlayerAi::Func2135608(Door* door)
{
    Flags2 |= AiFlags2::TargetDoor;
    _targetDoor = door;
}

void PlayerAi::CheckUnmorph()
{
    if (_player->m_altForm && _touchButtons.Unmorph.FramesUp > 10 * 2) {
        _touchButtons.Unmorph.IsDown = true;
    }
}

float PlayerAi::TurnToward(float dotValue, const Vec3& from, const Vec3& to)
{
    // The horizontal turn: exact when close, else the level's fixed swing.
    float aim = dotValue > DotValue() ? acosDegrees(dotValue) : AimValue();
    if (cross(from, to)[1] < 0) {
        aim *= -1;
    }
    return aim;
}

// Func2145C14 turns (X); Func21447E8 turns and looks up or down (X and Y).
void PlayerAi::Func2145C14(const Vec3& position)
{
    Player* p = _player;
    const Vec3 vec1 = p->m_altForm ? Vec3{p->m_field80, 0, p->m_field84} : Vec3{p->m_field70, 0, p->m_field74};
    Vec3 vec2 = withY(position - p->m_position, 0);
    vec2 = vec2[0] != 0 || vec2[2] != 0 ? normalized(vec2) : vec1;
    const float d = dot(vec1, vec2);
    if (d > 255 / 256.0f) {
        Flags2 |= AiFlags2::Bit0;
    } else {
        Flags2 &= ~AiFlags2::Bit0;
    }
    if (d < 1) {
        _buttonAimX = TurnToward(d, vec1, vec2);
    }
}

void PlayerAi::Func21447E8()
{
    Player* p = _player;
    const Vec3 vec1{p->m_cam.field48, 0, p->m_cam.field4C};
    const Vec3 vec2 = _field1038[0] != 0 || _field1038[2] != 0 ? normalized(withY(_field1038, 0)) : vec1;
    const float d = dot(vec1, vec2);
    const float value = AimValue();
    if (d < 1) {
        _buttonAimX = TurnToward(d, vec1, vec2);
    }
    const float angle1 = 90 - acosDegrees(p->m_cam.facing[1]);
    const float angle2 = 90 - acosDegrees(_field1038[1]);
    _buttonAimY = std::clamp(angle2 - angle1, -value, value);
}

void PlayerAi::Func21436D8()
{
    if (Flags2 & AiFlags2::TargetPlayer) {
        Func2144B88();
        const Vec3 vec = ExecuteVectorFunc(0, false, false);
        if (((Flags2 & AiFlags2::Bit8) && Func213842C() && (IsPlayerVisible(_player, _targetPlayer) || _weapon1 == 7))
            || lengthSquared(vec) < 10) {
            Func2143A40();
        } else if ((Flags4 & AiFlags4::Bit1) || _weapon1 <= 1) {
            Func214380C();
        }
    } else if (Flags4 & AiFlags4::Bit1) {
        Func214380C();
    }
}

void PlayerAi::Func2144B88()
{
    // Aim at the target: leading its movement by the shot's travel time, and
    // off by an error that grows with distance and shrinks with bot level.
    if (!(Flags2 & AiFlags2::TargetPlayer)) {
        return;
    }
    Player* p = _player;
    const Vec3 toTarget = _targetPlayer->m_position - p->m_position;
    const float targetDist = length(toTarget);
    // _field1020: the aim's deviation, refreshed when unset or when facing away
    if (_field1020 == 0 || dot(toTarget, p->facingVector()) < 0) {
        const Vec3 targetPos = _targetPlayer->m_position;
        const int prevField1020 = _field1020;
        if (p->m_botLevel == 0) {
            _field1020 = 15 * 2;
        } else if (p->m_botLevel == 1) {
            _field1020 = 7 * 2;
        } else {
            // Insane keeps Hard's interval: it is also the velocity estimate's time base
            _field1020 = 3 * 2;
        }
        if (_field1020 < p->m_disruptedTimer) {
            _field1020 += static_cast<int>(randomInt2(static_cast<uint32_t>(p->m_disruptedTimer - _field1020)));
        }
        const int field1020Diff = _field1020 - prevField1020;
        if ((Flags4 & AiFlags4::Bit3) && field1020Diff > 0 && p->m_botLevel > 0 && p->m_equip.weapon != nullptr) {
            const EquipInfo& equip = p->m_equip;
            const WeaponInfo& weapon = *equip.weapon;
            bool isCharged = false;
            float chargePct = 0;
            if (weapon.flags & WeaponFlags::PartialCharge) {
                if ((weapon.flags & WeaponFlags::CanCharge) && equip.chargeLevel >= weapon.minCharge * 2) {
                    isCharged = true;
                    chargePct = (equip.chargeLevel - weapon.minCharge * 2) / static_cast<float>(weapon.fullCharge * 2 - weapon.minCharge * 2);
                }
            } else if (equip.chargeLevel >= weapon.fullCharge * 2) {
                isCharged = true;
                chargePct = 1;
            }
            Vec3 vec = (targetPos - _field1054) / (field1020Diff / 2.0f);
            float homing, speed;
            if (isCharged) {
                homing = (weapon.minChargeHoming + (weapon.chargedHoming - weapon.minChargeHoming) * chargePct) / 4096.0f / 2;
                speed = (weapon.minChargeSpeed + (weapon.chargedSpeed - weapon.minChargeSpeed) * chargePct) / 4096.0f / 2;
            } else {
                homing = weapon.unchargedHoming / 4096.0f / 2;
                speed = weapon.unchargedSpeed / 4096.0f / 2;
            }
            if (homing > 0 || speed <= 0) {
                vec = vec * ((_field1020 / 2.0f) / 2.0f);
            } else {
                const float muzzleDist = length(targetPos - p->m_muzzlePos);
                vec = vec * muzzleDist;
                const int decay = weapon.speedDecayTimes[isCharged ? 1 : 0];
                float finalSpeed;
                if (decay == 0) {
                    finalSpeed = speed;
                } else if (isCharged) {
                    finalSpeed = (weapon.minChargeFinalSpeed + (weapon.chargedFinalSpeed - weapon.minChargeFinalSpeed) * chargePct) / 4096.0f / 2;
                } else {
                    finalSpeed = weapon.unchargedFinalSpeed / 4096.0f / 2;
                }
                vec = vec / finalSpeed;
                if (p->m_botLevel >= 3 && finalSpeed > 0) {
                    // Insane: the target's real speed (per 60 Hz tick), and the
                    // flight time solved for where the shot meets it.
                    const Vec3 realTargetVel = _targetPlayer->m_speed * 0.5f;
                    Vec3 lead = realTargetVel * (muzzleDist / finalSpeed);
                    for (int i = 0; i < 4; i++) {
                        const float dist = length(targetPos + lead - p->m_muzzlePos);
                        lead = realTargetVel * (dist / finalSpeed);
                    }
                    vec = lead;
                }
            }
            _field1048 = targetPos + vec;
        } else {
            _field1048 = targetPos;
        }
        _field1054 = targetPos;
        Flags4 |= AiFlags4::Bit3;
        _field1048 = addY(_field1048, _targetPlayer->m_altForm ? fx(_targetPlayer->v.AltColYPos) : 0.5f);
        const Vec3 speedDiff = p->m_speed - _targetPlayer->m_speed;
        const Vec3 camVec{p->m_cam.field50, 0, p->m_cam.field54};
        const float dot1 = std::fabs(dot(speedDiff, camVec));
        const float dot2 = std::fabs(dot(speedDiff, p->m_cam.up));
        float v52, v66;
        const bool zoomed = Flags4 & AiFlags4::Bit2;
        if (p->m_botLevel == 0) {
            v52 = dot1 * 5 + 0.25f;
            v66 = dot2 * 5 + 0.25f;
            if (!zoomed) {
                v52 += targetDist / 2;
                v66 += targetDist / 2;
            }
        } else if (p->m_botLevel == 1) {
            v52 = dot1 * 2 + 0.1f;
            v66 = dot2 * 2 + 0.1f;
            if (!zoomed) {
                v52 += targetDist / 9;
                v66 += targetDist / 9;
            }
        } else if (p->m_botLevel == 2) {
            v52 = dot1 * 0.2f + 0.01f;
            v66 = dot2 * 0.2f + 0.01f;
            if (!zoomed) {
                v52 += targetDist / 50;
                v66 += targetDist / 50;
            }
        } else {
            v52 = v66 = 0; // Insane: dead on
        }
        if (p->m_disruptedTimer > 0) {
            v52 *= 2;
            v66 *= 2;
        }
        if (Flags2 & AiFlags2::Bit21) {
            v52 /= 2;
            v66 /= 2;
        }
        if (p->m_shockCoilTimer > 10 * 2) {
            v52 /= 2;
            v66 /= 2;
        }
        const int v61 = static_cast<int>(v52 * 4096);
        const int v62 = static_cast<int>(v66 * 4096);
        float rand1 = (static_cast<int>(randomInt2(static_cast<uint32_t>(v61 * 2))) - v61) / 4096.0f;
        float rand2 = (static_cast<int>(randomInt2(static_cast<uint32_t>(v62 * 2))) - v62) / 4096.0f;
        if (p->m_disruptedTimer == 0) {
            if (rand1 > 6) {
                rand1 = randomInt2(8192) / 4096.0f + 4;
            } else if (rand1 < -6) {
                rand1 = -4 - randomInt2(9182) / 4096.0f;
            }
            if (rand2 > 6) {
                rand2 = randomInt2(8192) / 4096.0f + 4;
            } else if (rand2 < -6) {
                rand2 = -4 - randomInt2(9182) / 4096.0f;
            }
        }
        _field1048 = _field1048 + camVec * rand1 + p->m_cam.up * rand2;
    }
    Func2145738(_field1048);
    if (Flags4 & AiFlags4::Bit2) {
        const float aimValue = AimValue() / 2;
        _buttonAimX = std::clamp(_buttonAimX, -aimValue, aimValue);
        _buttonAimY = std::clamp(_buttonAimY, -aimValue, aimValue);
    }
}

void PlayerAi::Func2145738(const Vec3& position)
{
    // Turn the gun toward `position`, raising it for the shot's drop.
    Player* p = _player;
    Vec3 toTarget = p->m_aimPosition - p->m_muzzlePos;
    const float toTargetX = toTarget[0], toTargetY = toTarget[1], toTargetZ = toTarget[2];
    toTarget = normalized(toTarget);
    const float toTargetYNrm = toTarget[1];
    toTarget = withY(toTarget, 0);
    toTarget = !isZero(toTarget) ? normalized(toTarget) : UnitX;
    Vec3 toPos = position - p->m_muzzlePos;
    const float distToPosH = length(withY(toPos, 0));
    const float posY = toPos[1];
    toPos = normalized(toPos);
    const float posYNrm = toPos[1];
    toPos = withY(toPos, 0);
    toPos = !isZero(toPos) ? normalized(toPos) : toTarget;
    const float d = dot(toTarget, toPos);
    const float value = AimValue();
    if (d < 1) {
        _buttonAimX = TurnToward(d, toTarget, toPos);
    }
    float chargePct = 0;
    float speed = 0, gravity = 0;
    if (p->m_equip.weapon != nullptr) {
        const EquipInfo& equip = p->m_equip;
        const WeaponInfo& weapon = *equip.weapon;
        if ((weapon.flags & WeaponFlags::CanCharge) && equip.chargeLevel >= weapon.minCharge * 2) {
            chargePct = (equip.chargeLevel - weapon.minCharge * 2) / static_cast<float>(weapon.fullCharge * 2 - weapon.minCharge * 2);
        }
        // (the game's: between uncharged and min charge)
        speed = (weapon.unchargedSpeed + (weapon.minChargeSpeed - weapon.unchargedSpeed) * chargePct) / 4096.0f / 2;
        gravity = (weapon.unchargedGravity + (weapon.minChargeGravity - weapon.unchargedGravity) * chargePct) / 4096.0f / 2;
    }
    const float div = speed != 0 ? distToPosH * distToPosH * gravity / (speed * speed) : 0;
    float angle1, angle2;
    if (div != 0) {
        float div2 = 0;
        if (toTargetX != 0 || toTargetZ != 0) {
            div2 = toTargetY / std::sqrt(toTargetX * toTargetX + toTargetZ * toTargetZ);
        }
        const float v28 = distToPosH * distToPosH - 4 * (div / 2 * (div / 2 - posY));
        if (v28 < 0) {
            return;
        }
        const float div3 = (std::sqrt(v28) - distToPosH) / div;
        angle1 = std::atan(div2) * kRadToDeg;
        angle2 = std::atan(div3) * kRadToDeg;
    } else {
        angle1 = 90 - acosDegrees(toTargetYNrm);
        angle2 = 90 - acosDegrees(posYNrm);
    }
    const float angleDiff = angle2 - angle1;
    _buttonAimY = std::clamp(angleDiff, -value, value);
    Flags2 &= ~AiFlags2::Bit8;
    if (d >= 255 / 256.0f && angleDiff > -5 && angleDiff < 5) {
        Flags2 |= AiFlags2::Bit8; // on target
    }
}

bool PlayerAi::Func213842C()
{
    // whether the target is in view, once per tick
    if (!(Flags2 & AiFlags2::Bit16)) {
        Flags2 |= AiFlags2::Bit16;
        Flags2 &= ~AiFlags2::Bit17;
        if ((Flags2 & AiFlags2::TargetPlayer) && AggroFunc214857C(6, 1, 2, nullptr, _targetPlayer)) {
            Flags2 |= AiFlags2::Bit17;
        }
    }
    return Flags2 & AiFlags2::Bit17;
}

void PlayerAi::Func2143A40()
{
    // Fire _weapon1, switching to it first, with a reaction delay by bot level.
    Player* p = _player;
    if (p->m_equip.weapon == nullptr) {
        return;
    }
    const EquipInfo& equip = p->m_equip;
    const WeaponInfo& weapon = *p->m_equip.weapon;
    int shotDelay = p->m_botLevel == 0 ? 60 : p->m_botLevel == 1 ? 15 : p->m_botLevel == 2 ? 5 : 0;
    if (Flags2 & AiFlags2::Bit21) {
        shotDelay /= 2;
    }
    shotDelay *= 2;
    const int beam = GetBeamType(_weapon1);
    if (beam != Beam::ShockCoil && !p->m_availableWeapons[beam]) {
        return;
    }
    auto setRandomDelay = [&] { _shotDelay = weapon.shotCooldown * 2 + static_cast<int>(randomInt2(static_cast<uint32_t>(shotDelay))); };
    const bool shooting = p->m_shooting;
    const bool fullCharge = equip.chargeLevel >= weapon.fullCharge * 2;
    auto chargedShot = [&](AiButton& select) {
        // the charged weapons: charge while held, let go when full
        if (p->m_currentWeapon != beam) {
            select.IsDown = true;
        } else if ((Flags4 & AiFlags4::Bit1) && CanChargeWeapon()) {
            if ((!shooting && _buttons.R.FramesUp <= _shotDelay) || fullCharge) {
                setRandomDelay();
            } else {
                _buttons.R.IsDown = true;
            }
        } else if (_buttons.R.FramesUp > _shotDelay) {
            _buttons.R.IsDown = true;
            setRandomDelay();
        }
    };
    auto powerBeam = [&] {
        if (_buttons.R.FramesDown > 0 && _buttons.R.FramesDown < _shotDelay) {
            _buttons.R.IsDown = true;
        } else if (_buttons.R.FramesUp <= _shotDelay) {
            setRandomDelay();
        } else {
            _buttons.R.IsDown = true;
            _shotDelay = static_cast<int>(randomInt2(static_cast<uint32_t>(weapon.fullCharge * 2)));
        }
    };
    switch (beam) {
    case Beam::PowerBeam:
        if (p->m_currentWeapon != beam) {
            _touchButtons.PowerBeam.IsDown = true;
        } else if ((Flags4 & AiFlags4::Bit1) && CanChargeWeapon()) {
            if ((!shooting && _buttons.R.FramesUp == 0) || fullCharge) {
                setRandomDelay();
            } else {
                _buttons.R.IsDown = true;
            }
        } else {
            powerBeam();
        }
        break;
    case Beam::Missile:
        if (p->m_currentWeapon != beam) {
            _touchButtons.Missile.IsDown = true;
        } else if ((Flags4 & AiFlags4::Bit1) && CanChargeWeapon()) {
            if ((!shooting && _buttons.R.FramesUp <= _shotDelay) || fullCharge) {
                setRandomDelay();
            } else {
                _buttons.R.IsDown = true;
            }
        } else if (p->m_gunOpenAnimation && _buttons.R.FramesUp > _shotDelay) {
            _buttons.R.IsDown = true;
            setRandomDelay();
        }
        break;
    case Beam::VoltDriver: chargedShot(_touchButtons.VoltDriver); break;
    case Beam::Battlehammer:
        if (p->m_currentWeapon != beam) {
            _touchButtons.Battlehammer.IsDown = true;
        } else if (p->m_shotUncharged || _buttons.R.FramesUp > 0) {
            _buttons.R.IsDown = true;
        }
        break;
    case Beam::Imperialist:
        if (p->m_currentWeapon != beam) {
            _touchButtons.Imperialist.IsDown = true;
        } else if (_buttons.R.FramesUp > _shotDelay) {
            _buttons.R.IsDown = true;
        }
        break;
    case Beam::Judicator:
        if (p->m_currentWeapon != beam) {
            _touchButtons.Judicator.IsDown = true;
        } else if ((Flags4 & AiFlags4::Bit1) && CanChargeWeapon()) {
            if ((!shooting && _buttons.R.FramesUp <= _shotDelay) || fullCharge) {
                if (Flags2 & AiFlags2::TargetPlayer) {
                    // the charged ice wave only close up
                    const float distSqr = lengthSquared(_targetPlayer->m_position - p->m_position);
                    const int level = p->m_botLevel;
                    if ((distSqr > 3 * 3 && level == 0) || (distSqr > 11 && level == 1) || (distSqr > 13 && level == 2)
                        || (distSqr > 400 && level >= 3)) {
                        setRandomDelay();
                    } else {
                        _buttons.R.IsDown = true;
                    }
                } else {
                    setRandomDelay();
                }
            } else {
                _buttons.R.IsDown = true;
            }
        } else if (_buttons.R.FramesUp > _shotDelay) {
            _buttons.R.IsDown = true;
            setRandomDelay();
        }
        break;
    case Beam::Magmaul:
        if (p->m_currentWeapon != beam) {
            _touchButtons.Magmaul.IsDown = true;
        } else if ((Flags4 & AiFlags4::Bit1) && CanChargeWeapon()) {
            if ((!shooting && _buttons.R.FramesUp <= _shotDelay) || fullCharge) {
                setRandomDelay();
            } else {
                _buttons.R.IsDown = true;
            }
        } else if (_buttons.R.FramesUp > _shotDelay) {
            setRandomDelay();
            _buttons.R.IsDown = true;
        }
        break;
    case Beam::ShockCoil: {
        if (_targetPlayer == nullptr) {
            break;
        }
        const float distSqr = lengthSquared(_targetPlayer->m_position - p->m_position);
        if (distSqr <= 15 * 15 || !p->m_availableWeapons[Beam::PowerBeam]) {
            if (!p->m_availableWeapons[beam]) {
                return;
            }
            if (p->m_currentWeapon != beam) {
                _touchButtons.ShockCoil.IsDown = true;
            } else if ((p->m_shotUncharged || _buttons.R.FramesUp > 0) && (distSqr < 15 * 15 || (distSqr < 16 * 16 && p->m_shotUncharged))) {
                _buttons.R.IsDown = true;
            }
        } else if (p->m_currentWeapon != Beam::PowerBeam) {
            _touchButtons.PowerBeam.IsDown = true;
        } else {
            powerBeam();
        }
        break;
    }
    case Beam::OmegaCannon:
        if (p->m_currentWeapon != beam) {
            _touchButtons.OmegaCannon.IsDown = true;
        } else if ((Flags2 & AiFlags2::TargetPlayer) && _buttons.R.FramesUp > 0) {
            if (lengthSquared(_targetPlayer->m_position - p->m_position) > 10 * 10) {
                _buttons.R.IsDown = true;
            }
        }
        break;
    default: break;
    }
}

void PlayerAi::Func214380C()
{
    // Switch to _weapon1 when possible; otherwise hold fire to charge what is equipped.
    Player* p = _player;
    if (p->m_equip.weapon == nullptr || !(p->m_equip.weapon->flags & WeaponFlags::CanCharge)) {
        return;
    }
    const int beam = GetBeamType(_weapon1);
    if (p->m_currentWeapon != beam && p->m_availableWeapons[beam]) {
        switch (beam) {
        case Beam::PowerBeam: _touchButtons.PowerBeam.IsDown = true; break;
        case Beam::Missile: _touchButtons.Missile.IsDown = true; break;
        case Beam::VoltDriver: _touchButtons.VoltDriver.IsDown = true; break;
        case Beam::Judicator: _touchButtons.Judicator.IsDown = true; break;
        case Beam::Magmaul: _touchButtons.Magmaul.IsDown = true; break;
        default: break;
        }
        return;
    }
    if (CanChargeWeapon() && (_buttons.R.FramesDown == 0 || p->m_equip.chargeLevel > 0)) {
        _buttons.R.IsDown = true;
    }
}

void PlayerAi::Func2143658()
{
    if (Flags2 & AiFlags2::TargetPlayer) {
        Func2144AE4();
        if (Flags2 & AiFlags2::Bit8) {
            Func2143A40();
        }
    } else if (Flags4 & AiFlags4::Bit1) {
        Func214380C();
    }
}

void PlayerAi::Func2144AE4()
{
    if (Flags2 & AiFlags2::TargetPlayer) {
        _field1048 = TargetPosition(_targetPlayer);
        Func2145738(_field1048);
    }
}

void PlayerAi::Func21433E4()
{
    if (Flags2 & AiFlags2::TargetPlayer) {
        Func2144B88();
        const Vec3 vec = ExecuteVectorFunc(0, false, false);
        if (dot(_player->facingVector(), vec) > 0.866f) {
            Func2143A40();
        }
    }
}

void PlayerAi::Func2143470()
{
    if (Flags2 & AiFlags2::TargetHalfturret) {
        Func2144964();
        const Vec3 toHalfturret = _targetHalfturret->m_halfturret.position - _player->m_position;
        if ((Flags2 & AiFlags2::Bit8) || lengthSquared(toHalfturret) < 10) {
            Func2143A40();
        } else if ((Flags4 & AiFlags4::Bit1) || _weapon1 == 0 || _weapon1 == 1) {
            Func214380C();
        }
    } else if (Flags4 & AiFlags4::Bit1) {
        Func214380C();
    }
}

void PlayerAi::Func2144964()
{
    if (Flags2 & AiFlags2::TargetHalfturret) {
        _field1048 = addY(_targetHalfturret->m_halfturret.position, 0.5f);
        Func2145738(_field1048);
    }
}

void PlayerAi::Func21433A0(const Vec3& position)
{
    Func2145738(position);
    if (Flags2 & AiFlags2::Bit8) {
        Func2143A40();
    }
}

void PlayerAi::Func2145BA0()
{
    // level the aim
    const float facingY = _player->facingVector()[1];
    if (facingY != 0) {
        const float aimY = acosDegrees(facingY) - 90;
        const float value = AimValue();
        _buttonAimY = std::clamp(aimY, -value, value);
    }
}

void PlayerAi::PressButton(AiButton& button, int frames)
{
    if (button.FramesUp > frames * 2) {
        button.IsDown = true;
    }
}

void PlayerAi::PressL(int frames)
{
    // (the game's: checks the Magmaul's touch button instead of L)
    if (_touchButtons.Magmaul.FramesUp > frames * 2) {
        _buttons.L.IsDown = true;
    }
}

} // namespace fp
