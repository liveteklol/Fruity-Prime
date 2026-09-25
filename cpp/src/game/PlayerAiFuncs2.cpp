// PlayerAiData funcs_2: what a tree node does every tick it runs.
#include "PlayerAi.h"

#include "PlayerAiUtil.h"
#include "World.h"

#include <algorithm>
#include <cassert>
#include <optional>

#pragma GCC diagnostic ignored "-Wunused-parameter"

namespace fp {

using namespace ai;

namespace {
constexpr int kTerrainLava = 8;
}

void PlayerAi::Func2_213EA10(AiContext& context)
{
    if (!_player->m_altForm && _touchButtons.Morph.FramesUp > 10 * 2) {
        _touchButtons.Morph.IsDown = true;
    }
}

// the main process, counterpart to Func4_21462DC
void PlayerAi::Func2_213EA48(AiContext& context)
{
    Player* p = _player;
    if (context.FieldD == 28 && p->m_altForm && context.Field4 != 37) {
        CheckUnmorph();
        if (context.Field4 == 33) {
            Field118++;
        }
    } else if (context.FieldD == 29 && !p->m_altForm && context.Field4 != 37) {
        if (_touchButtons.Morph.FramesUp > 10 * 2) {
            _touchButtons.Morph.IsDown = true;
        }
    }
    Vec3 targetPos{};
    if (p->m_altForm || context.FieldA == 31) {
        if (p->v.AltFormStrafe != 0 && context.FieldA == 31) {
            if (context.FieldB == 4 && (Flags2 & AiFlags2::TargetPlayer)) {
                targetPos = TargetPosition(_targetPlayer);
                Func2145C14(targetPos);
            } else if (context.FieldB == 5 && (Flags2 & AiFlags2::TargetHalfturret)) {
                targetPos = _targetHalfturret->m_halfturret.position;
                Func2145C14(targetPos);
            }
        }
    } else if (context.FieldA == 32) {
        auto aimAt = [&](const Vec3& at) {
            _field1038 = at - p->m_cam.position;
            _field1038 = !isZero(_field1038) ? normalized(_field1038) : p->m_cam.facing;
        };
        if (context.FieldB == 4 && (Flags2 & AiFlags2::TargetPlayer)) {
            targetPos = TargetPosition(_targetPlayer);
            aimAt(targetPos);
        } else if (context.FieldB == 5 && (Flags2 & AiFlags2::TargetHalfturret)) {
            targetPos = _targetHalfturret->m_halfturret.position;
            aimAt(targetPos);
        } else if (context.FieldB == 27) {
            aimAt(_fieldB8);
        }
        Func21447E8();
    } else if (context.FieldC == 55) {
        Func21433A0(_fieldB8);
    } else if (context.FieldC == 56 || context.FieldC == 57 || context.FieldC == 59) {
        if (Flags2 & AiFlags2::TargetPlayer) {
            if (context.FieldC == 56) {
                Func21436D8();
            } else if (context.FieldC == 57) {
                Func2143658();
            } else {
                Func21433E4();
            }
        } else if (Flags4 & AiFlags4::Bit1) {
            Func214380C();
        }
    } else if (context.FieldC == 60) {
        if (Flags2 & AiFlags2::TargetHalfturret) {
            Func2143470();
        } else if (Flags4 & AiFlags4::Bit1) {
            Func214380C();
        }
    } else if (context.FieldC == 61) {
        assert(_targetDoor != nullptr);
        targetPos = _targetDoor->position;
        Func21433A0(targetPos);
    } else {
        Func2145BA0();
    }
    if (context.Field4 == 37) {
        if (context.FieldD == 29) {
            Func2140094(context);
            if (p->v.AltFormStrafe != 0 && _buttonAimX == 0 && _buttonAimY == 0) {
                if ((Flags2 & AiFlags2::TargetPlayer) && context.Field9 == 4) {
                    targetPos = TargetPosition(_targetPlayer);
                    Func2145C14(targetPos);
                } else {
                    assert(_node40 != nullptr);
                    Func2145C14(_node40->Position);
                }
            }
        } else if (context.Field5 != 58 || p->m_altForm) {
            Func2140094(context);
        } else {
            Func214003C(context);
        }
        if (context.Field6 == 52 && !p->m_altForm && !p->m_morphing && !p->m_usedJump && _buttons.L.FramesUp > 5 * 2) {
            AiPlayerAggro* aggro = AggroFunc214847C(4, 7, 1, nullptr, nullptr);
            if (aggro != nullptr && aggro->Staleness < 30 * 2) {
                assert(_node40 != nullptr);
                if (lengthSquared(_node40->Position - p->m_position) > 3 * 3) {
                    _buttons.L.IsDown = true;
                }
            }
        }
    } else if (context.Field4 == 33 && (context.Field9 != 4 || (Flags2 & AiFlags2::TargetPlayer))
        && (context.Field9 != 5 || (Flags2 & AiFlags2::TargetHalfturret))) {
        std::optional<Vec3> position;
        if (context.Field9 == 4) {
            position = _targetPlayer->m_position;
        } else if (context.Field9 == 5) {
            position = _targetHalfturret->m_halfturret.position;
        } else if (context.Field9 == 6 && (Flags2 & AiFlags2::TargetItem)) {
            assert(_itemC8 != nullptr);
            position = targetPos = addY(_itemC8->position, -0.5f);
        } else if (context.Field9 == 12 || context.Field9 == 13 || context.Field9 == 39) {
            assert(_node40 != nullptr);
            position = _node40->Position;
        } else if (context.Field9 == 14 && _octolithFlagCC != nullptr) {
            position = _octolithFlagCC->position;
        } else if (context.Field9 == 15 && _octolithFlagCC != nullptr) {
            position = _octolithFlagCC->basePosition;
        } else if (context.Field9 == 16 && _flagBaseD0 != nullptr) {
            position = _flagBaseD0->position;
        } else if (context.Field9 == 17 && _octolithFlagD4 != nullptr) {
            position = _octolithFlagD4->position;
        } else if (context.Field9 == 18 && _octolithFlagD4 != nullptr) {
            position = _octolithFlagD4->basePosition;
        } else if (context.Field9 == 19 && _flagBaseD8 != nullptr) {
            position = _flagBaseD8->position;
        } else if (context.Field9 == 20 && _octolithFlagDC != nullptr) {
            position = _octolithFlagDC->position;
        } else if (context.Field9 == 21 && _octolithFlagDC != nullptr) {
            position = _octolithFlagDC->basePosition;
        } else if (context.Field9 == 22 && _flagBaseE0 != nullptr) {
            position = _flagBaseE0->position;
        } else if (context.Field9 == 23 && (Flags2 & AiFlags2::TargetDefense)) {
            position = _targetDefense->position;
        } else if (context.Field9 == 35) {
            position = targetPos = addZ(p->m_position, 1);
        } else if (context.Field9 == 36) {
            position = targetPos = addZ(p->m_position, -1);
        }
        if (position) {
            if (p->m_altForm) {
                if (p->v.AltFormStrafe != 0) {
                    Func2145C14(targetPos);
                    Func2142AE8(*position);
                } else if (context.Field5 != 48 || p->m_hunter != Hunter::Samus) {
                    Func21418D8(*position);
                } else {
                    Func2141840(*position);
                }
            } else {
                Func2140B18(context, *position);
                if (context.FieldA != 0 || context.FieldC == 56 || context.FieldC == 57 || context.FieldC == 59 || context.FieldC == 60
                    || context.FieldC == 61 || context.FieldC == 55) {
                    Func2142AE8(*position);
                } else {
                    Func2142ABC(*position);
                }
                if (!p->m_morphing && !p->m_unmorphing) {
                    if (p->m_horizColTimer > 10 * 2 && p->m_grounded) {
                        PressL();
                    }
                    if (context.Field9 == 6 || context.Field9 == 14 || context.Field9 == 17 || context.Field9 == 20) {
                        Vec3 toPos = *position - p->m_position;
                        toPos = addY(toPos, -fx(p->v.MaxPickupHeight));
                        if (context.Field9 != 6) {
                            toPos = addY(toPos, -1.25f);
                        }
                        if (toPos[1] > -0.5f && lengthSquared(withY(toPos, 0)) < 0.5f * 0.5f) {
                            PressL();
                        }
                    }
                }
            }
        }
    } else if (context.Field4 == 34 && context.FieldD == 29) {
        if (!p->m_altForm) {
            PressButton(_touchButtons.Morph, 10);
        } else if (_fieldAC[0] != 0 || _fieldAC[2] != 0) {
            Func2141CD4(_fieldAC);
        }
    }
    if (context.FieldE == 38 && !p->m_altForm) {
        Func214380C();
    }
    const int abilities = p->abilities();
    constexpr int bombs = 0x4, spireAlt = 0x200, traceAlt = 0x400, weavelAlt = 0x1000;
    if (context.FieldC == 62 && p->m_altForm) {
        auto shootAndSetDelay = [&] {
            if (static_cast<uint32_t>(_buttons.L.FramesUp) > _field102E) {
                _buttons.L.IsDown = true;
                _field102E = _field102C + randomInt2(_field102C / 2);
            }
        };
        if (context.FieldF == 64) {
            if ((abilities & bombs) && p->m_bombAmmo > 0 && p->m_bombCooldown == 0) {
                shootAndSetDelay();
            }
        } else if (context.FieldF == 65) {
            if ((abilities & bombs) && p->m_bombCooldown == 0) {
                shootAndSetDelay();
            }
        } else if (context.FieldF == 66) {
            if ((abilities & spireAlt) && !p->m_altAttack) {
                shootAndSetDelay();
            }
        } else if (context.FieldF == 67) {
            if (p->m_altAttackTime > 0 || static_cast<uint32_t>(_buttons.L.FramesUp) > _field102E) {
                _buttons.L.IsDown = true;
                _field102E = _field102C + randomInt2(_field102C / 2);
            }
        } else if (context.FieldF == 68) {
            if ((abilities & traceAlt) && p->m_altAttackCooldown == 0 && (Flags2 & AiFlags2::Bit0)) {
                shootAndSetDelay();
            }
        } else if (context.FieldF == 69) {
            if (randomInt2(static_cast<uint32_t>(15 * p->m_syluxBombCount + 10)) == 0 && (abilities & bombs) && p->m_bombAmmo > 0
                && p->m_bombCooldown == 0) {
                shootAndSetDelay();
            }
        } else if (context.FieldF == 70) {
            if ((abilities & weavelAlt) && p->m_altAttackCooldown == 0 && (Flags2 & AiFlags2::Bit0)) {
                shootAndSetDelay();
            }
        }
    } else if (context.FieldC == 63) {
        if (context.FieldF == 65) {
            if ((abilities & bombs) && p->m_bombCooldown == 0 && _buttons.L.FramesUp > 60 * 2) {
                _buttons.L.IsDown = true;
            }
        } else if (context.FieldF == 69 && p->m_syluxBombCount < 2) {
            assert(_node3C != nullptr);
            if (IsNodeInRange(_node3C) && (abilities & bombs) && p->m_bombAmmo > 0 && p->m_bombCooldown == 0 && _buttons.L.FramesUp > 0) {
                _buttons.L.IsDown = true;
            }
        }
    }
    if (!p->m_altForm && !p->m_morphing && (Flags2 & AiFlags2::TargetPlayer)) {
        if (_targetPlayer->m_hunter == Hunter::Sylux && _targetPlayer->m_altForm && Func2139C60(_targetPlayer)) {
            PressL();
        }
    }
    if (context.Field6 == 51 && !p->m_altForm && !p->m_morphing && !p->m_usedJump) {
        PressButton(_buttons.L, 5);
    }
    if ((Flags2 & AiFlags2::Bit21) && randomInt2(10) == 0 && !p->m_altForm && !p->m_morphing && !p->m_usedJump) {
        PressButton(_buttons.L, 5);
    }
    if (context.Func24Id == 95 && !p->m_altForm && !p->m_morphing && !p->m_usedJump && slipSpeedFactors()[p->m_slipperiness] > 0
        && p->m_hSpeedMag > 0) {
        PressButton(_buttons.L, 5);
    }
}

void PlayerAi::Func2142DCC()
{
    if (Flags2 & AiFlags2::Bit12) {
        _buttons.A.IsDown = true;
    } else {
        _buttons.Y.IsDown = true;
    }
    const float lengthSqr = lengthSquared(ExecuteVectorFunc(0, true, false));
    if (lengthSqr < 2 * 2) {
        _buttons.B.IsDown = true;
    } else if (lengthSqr > 3 * 3) {
        _buttons.X.IsDown = true;
    }
}

bool PlayerAi::Func2142EB0(AiContext& context)
{
    // stuck in place for half a second: switch sides
    const Vec3 toSelf = withY(_player->m_position - context.Field34, 0);
    if (_player->m_morphing || _player->m_unmorphing || lengthSquared(toSelf) >= 0.5f * 0.5f) {
        context.Field40 = 0;
        context.Field34 = _player->m_position;
    } else {
        context.Field40++;
    }
    if (context.Field40 < 15 * 2) {
        return false;
    }
    context.Field40 = 0;
    context.Field34 = _player->m_position;
    Flags2 ^= AiFlags2::Bit12;
    return true;
}

Vec3 PlayerAi::SyluxBombPosition(int index) const
{
    const int bomb = _world.m_slots[_player->m_slot].syluxBombs[index];
    return bomb >= 0 ? _world.m_bombs[bomb].position : _player->m_position;
}

void PlayerAi::Func2_213DDCC(AiContext& context)
{
    // Sylux circling a target, laying Lockjaw bombs around it
    Player* p = _player;
    if (p->m_altForm) {
        if (Flags2 & AiFlags2::TargetPlayer) {
            Func2142DCC();
            Func2145C14(_targetPlayer->m_position);
            bool spawnBomb = false;
            if (p->m_syluxBombCount == 0) {
                spawnBomb = true;
                if (randomInt2(2) == 0) {
                    Flags2 &= ~AiFlags2::Bit12;
                } else {
                    Flags2 |= AiFlags2::Bit12;
                }
            } else if (p->m_syluxBombCount == 1 || p->m_syluxBombCount == 2) {
                Vec3 targetToBomb = withY(SyluxBombPosition(p->m_syluxBombCount - 1) - _targetPlayer->m_position, 0);
                targetToBomb = !isZero(targetToBomb) ? normalized(targetToBomb) : UnitX;
                Vec3 targetToSelf = withY(p->m_position - _targetPlayer->m_position, 0);
                targetToSelf = !isZero(targetToSelf) ? normalized(targetToSelf) : targetToBomb;
                if (dot(targetToBomb, targetToSelf) < -0.5f) {
                    if (p->m_syluxBombCount == 1) {
                        spawnBomb = true;
                    } else {
                        const Vec3 added = targetToBomb + targetToSelf;
                        if (isZero(added)) {
                            spawnBomb = true;
                        } else {
                            targetToBomb = withY(SyluxBombPosition(0) - _targetPlayer->m_position, 0);
                            spawnBomb = dot(added, targetToBomb) > 0;
                        }
                    }
                }
            }
            if (Func2142EB0(context)) {
                spawnBomb = true;
            }
            if (spawnBomb && (p->abilities() & 0x4) && p->m_bombAmmo > 0 && p->m_bombCooldown == 0
                && static_cast<uint32_t>(_buttons.L.FramesUp) > _field102E) {
                _buttons.L.IsDown = true;
                _field102E = _field102C + randomInt2(_field102C / 2);
            }
        }
    } else if (_touchButtons.Morph.FramesUp > 10 * 2) {
        _touchButtons.Morph.IsDown = true;
    }
}

void PlayerAi::Func2_213DA88(AiContext& context)
{
    Player* p = _player;
    if (p->m_altForm) {
        _buttons.A.IsDown = true;
        if (Flags2 & AiFlags2::TargetPlayer) {
            // the C#'s note: these "vectors" all run from the origin
            const float targetLengthSqr = lengthSquared(withY(_targetPlayer->m_position, 0));
            const float selfLengthSqr = lengthSquared(withY(p->m_position, 0));
            bool spawnBomb = false;
            if (p->m_syluxBombCount == 0) {
                spawnBomb = _buttons.L.FramesUp > 1 * 2;
            } else if (p->m_syluxBombCount == 1 || p->m_syluxBombCount == 2) {
                Vec3 bombPos = withY(SyluxBombPosition(p->m_syluxBombCount - 1), 0);
                bombPos = !isZero(bombPos) ? normalized(bombPos) : UnitX;
                Vec3 selfPos = withY(p->m_position, 0);
                selfPos = !isZero(selfPos) ? normalized(selfPos) : bombPos;
                spawnBomb = dot(bombPos, selfPos) < -0.5f;
            }
            if (selfLengthSqr < targetLengthSqr + 2 * 2) {
                _buttons.B.IsDown = true;
            } else if (selfLengthSqr > targetLengthSqr + 3 * 3) {
                _buttons.X.IsDown = true;
            }
            Func2145C14({});
            if (spawnBomb) {
                _buttons.L.IsDown = true;
            }
        }
    } else if (_touchButtons.Morph.FramesUp > 10 * 2) {
        _touchButtons.Morph.IsDown = true;
    }
}

void PlayerAi::Func2142FC0()
{
    if (_buttons.A.FramesDown < 180 * 2 && _buttons.Y.FramesUp != 0) {
        _buttons.A.IsDown = true;
    } else if (_buttons.Y.FramesDown < 180 * 2) {
        _buttons.Y.IsDown = true;
    }
    const float lengthSqr = lengthSquared(ExecuteVectorFunc(0, true, false));
    if (lengthSqr < 2 * 2) {
        _buttons.B.IsDown = true;
    } else if (lengthSqr > 3 * 3) {
        _buttons.X.IsDown = true;
    }
}

void PlayerAi::Func2_213E148(AiContext& context)
{
    if (!_player->m_altForm && _touchButtons.Morph.FramesUp > 10 * 2) {
        _touchButtons.Morph.IsDown = true;
    }
    if (Flags2 & AiFlags2::TargetPlayer) {
        Func2142FC0();
        Func2145C14(_targetPlayer->m_position);
    }
}

void PlayerAi::Func213FD94()
{
    // dodge the target's shot closest to coming at this bot
    if (!(Flags2 & AiFlags2::TargetPlayer)) {
        return;
    }
    Player* p = _player;
    float minLengthSqr = lengthSquared(_targetPlayer->m_position - p->m_position);
    const BeamProjectile* closestBeam = nullptr;
    for (const BeamProjectile& beam : _world.m_slots[_targetPlayer->m_slot].beams) {
        if (beam.lifespan > 0) {
            Vec3 toBeam = beam.position - p->m_position;
            const float toBeamLenSq = lengthSquared(toBeam);
            toBeam = toBeamLenSq > 0 ? normalized(toBeam) : beam.direction;
            Vec3 beamVelocity = beam.velocity;
            beamVelocity = !isZero(beamVelocity) ? normalized(beamVelocity) : toBeam;
            if (dot(toBeam, beamVelocity) <= fx(-2896) && toBeamLenSq <= minLengthSqr) {
                minLengthSqr = toBeamLenSq;
                closestBeam = &beam;
            }
        }
    }
    if (closestBeam != nullptr) {
        const Vec3 beamVelocityH = withY(closestBeam->velocity, 0);
        const Vec3 altVec = p->m_altForm ? Vec3{p->m_field80, 0, p->m_field84} : Vec3{p->m_field70, 0, p->m_field74};
        if (cross(altVec, beamVelocityH)[1] > 0) {
            _buttons.A.IsDown = true;
        } else {
            _buttons.Y.IsDown = true;
        }
        _buttons.L.IsDown = true;
    }
}

void PlayerAi::Func2_213E9C8(AiContext& context)
{
    CheckUnmorph();
    if (Flags2 & AiFlags2::TargetPlayer) {
        Func21436D8();
    }
    Func213FD94();
}

void PlayerAi::Func2_213E984(AiContext& context) { CheckUnmorph(); }

void PlayerAi::Func2_213E934(AiContext& context)
{
    Func2140094(context);
    if (_player->m_altForm && _buttons.L.FramesUp > _player->v.BombRefillTime) {
        _buttons.L.IsDown = true;
    }
}

void PlayerAi::Func21449DC()
{
    // aim near the target, a unit off either way
    Vec3 pos = _targetPlayer->m_position;
    pos[0] += randomInt2(8192) / 4096.0f - 1;
    pos[1] += _targetPlayer->m_altForm ? fx(_targetPlayer->v.AltColYPos) : 0.5f;
    pos[2] += randomInt2(8192) / 4096.0f - 1;
    _field1048 = pos;
    if (!Func213842C()) {
        _field1048 = addZ(_field1048, _field1048[2] < 0 ? -1.5f : 1.5f);
    }
    Func2145738(_field1048);
}

void PlayerAi::Func2143578()
{
    // the Imperialist, zoomed on the target
    Func21449DC();
    _buttonAimX = std::clamp(_buttonAimX, -0.75f, 0.75f);
    _buttonAimY = std::clamp(_buttonAimY, -0.75f, 0.75f);
    if (_player->m_currentWeapon != Beam::Imperialist) {
        _touchButtons.Imperialist.IsDown = true;
    } else if (_buttons.R.FramesUp > 10 * 2) {
        _buttons.R.IsDown = true;
    }
}

void PlayerAi::Func2_213E904(AiContext& context)
{
    if (Flags2 & AiFlags2::TargetPlayer) {
        Func2143578();
    }
}

void PlayerAi::Func2_213E684(AiContext& context)
{
    // Sylux setting a Lockjaw trap
    Player* p = _player;
    if (p->m_altForm) {
        float distSqr = 7 * 7;
        if (Flags2 & AiFlags2::TargetPlayer) {
            distSqr = distanceSquared(_targetPlayer->m_position, p->m_position);
        }
        context.Field2C = 0;
        if (p->m_syluxBombCount != 1 && (distSqr >= 10 * 10 || p->m_syluxBombCount != 0)) {
            Func2140094(context);
        }
        if ((p->abilities() & 0x4) && p->m_bombAmmo > 0 && p->m_bombCooldown == 0 && _buttons.L.FramesUp > 1 * 2) {
            if (distSqr >= 10 * 10 || p->m_syluxBombCount != 0) {
                if (p->m_syluxBombCount == 1) {
                    _buttons.A.IsDown = false;
                    _buttons.Y.IsDown = true;
                    _buttons.X.IsDown = false;
                    _buttons.B.IsDown = false;
                    if (_buttons.Y.FramesDown > 10 * 2 && _buttons.L.FramesUp != 0) {
                        _buttons.L.IsDown = true;
                    }
                } else if (p->m_syluxBombCount == 2 && _buttons.L.FramesUp > 150 * 2) {
                    _buttons.L.IsDown = true;
                }
            } else {
                _buttons.A.IsDown = true;
                _buttons.Y.IsDown = false;
                _buttons.X.IsDown = false;
                _buttons.B.IsDown = false;
                if (_buttons.A.FramesDown > 10 * 2 && _buttons.L.FramesUp != 0) {
                    _buttons.L.IsDown = true;
                }
            }
        }
    } else if (_touchButtons.Morph.FramesUp > 10 * 2) {
        _touchButtons.Morph.IsDown = true;
    }
}

void PlayerAi::Func2_213E3C4(AiContext& context)
{
    Player* p = _player;
    if (p->m_altForm) {
        CheckUnmorph();
    } else if (Flags2 & AiFlags2::TargetDefense) {
        // Func4_2145F78 has a pared down version of this
        const float radius = _targetDefense->volume.radius;
        if (radius > 0.5f) {
            const Vec3 toDefense = withY(_targetDefense->position - p->m_position, 0);
            if (radius * radius <= lengthSquared(toDefense)) {
                Field118 = 0;
                context.Field40 = 0;
                context.Field44 = 0;
                context.Field34 = p->m_position;
                _field78 = 0;
                Flags2 &= ~AiFlags2::Bit15;
                const float x = randomInt2(4096) / 4096.0f;
                _fieldA0 = Vec3{x * sign(toDefense[0]), 0, std::sqrt(1 - x * x) * sign(toDefense[2])} * radius + _targetDefense->position;
            }
            Func2142AE8(_fieldA0);
            Field118++;
            Func2140B18(context, _fieldA0);
        }
        Func21436D8();
    }
    if ((Flags2 & AiFlags2::Bit10) && randomInt2(10) == 0 && !p->m_altForm && !p->m_morphing && !p->m_usedJump
        && _buttons.L.FramesUp > 5 * 2) {
        _buttons.L.IsDown = true;
    }
}

void PlayerAi::Func2142D38()
{
    if (_buttons.Y.FramesUp > 30 * 2 || (_buttons.Y.FramesDown < 30 * 2 && _buttons.Y.FramesDown != 0)) {
        _buttons.Y.IsDown = true;
    } else {
        _buttons.A.IsDown = true;
    }
    if (_buttons.X.FramesUp > 15 * 2 || (_buttons.X.FramesDown < 15 * 2 && _buttons.X.FramesDown != 0)) {
        _buttons.X.IsDown = true;
    } else {
        _buttons.B.IsDown = true;
    }
}

void PlayerAi::JumpAtRandom()
{
    if (!_player->m_usedJump && static_cast<uint32_t>(_buttons.L.FramesUp) > _field1034) {
        _buttons.L.IsDown = true;
        _field1034 = randomInt2(75 * 2) + 15 * 2;
    }
}

void PlayerAi::Func2_213E31C(AiContext& context)
{
    CheckUnmorph();
    if (Flags2 & AiFlags2::TargetPlayer) {
        Func21433E4();
    }
    Func2142D38();
    JumpAtRandom();
}

void PlayerAi::Func21431B4()
{
    // circle the target, the way it faces
    Player* p = _player;
    const Vec3 selfAltVec = p->m_altForm ? Vec3{p->m_field80, 0, p->m_field84} : Vec3{p->m_field70, 0, p->m_field74};
    const Vec3 targetAltVec = _targetPlayer->m_altForm ? Vec3{_targetPlayer->m_field80, 0, _targetPlayer->m_field84}
                                                       : Vec3{_targetPlayer->m_field70, 0, _targetPlayer->m_field74};
    if (dot(selfAltVec, targetAltVec) <= fx(-3138)) {
        if (_buttons.A.FramesDown != 0) {
            _buttons.A.IsDown = true;
        } else {
            _buttons.Y.IsDown = true;
        }
    } else if (cross(selfAltVec, targetAltVec)[1] >= 0) {
        _buttons.A.IsDown = true;
    } else {
        _buttons.Y.IsDown = true;
    }
    const float lengthSqr = lengthSquared(ExecuteVectorFunc(0, true, false));
    if (lengthSqr < 2 * 2) {
        _buttons.B.IsDown = true;
    } else if (lengthSqr > 3 * 3) {
        _buttons.X.IsDown = true;
    }
}

void PlayerAi::Func2_213E274(AiContext& context)
{
    CheckUnmorph();
    if (Flags2 & AiFlags2::TargetPlayer) {
        Func21436D8();
        Func21431B4();
    }
    JumpAtRandom();
}

void PlayerAi::Func21430B4()
{
    if (_buttons.Y.FramesDown > 30 * 2 || (_buttons.A.FramesDown < 60 * 2 && _buttons.A.FramesDown != 0)) {
        _buttons.A.IsDown = true;
    } else {
        _buttons.Y.IsDown = true;
    }
    const float distSqr = distanceSquared(_targetHalfturret->m_halfturret.position, _player->m_position);
    if (distSqr < 2 * 2) {
        _buttons.B.IsDown = true;
    } else if (distSqr > 3 * 3) {
        _buttons.X.IsDown = true;
    }
}

void PlayerAi::Func2_213E1CC(AiContext& context)
{
    CheckUnmorph();
    if (Flags2 & AiFlags2::TargetHalfturret) {
        Func2143470();
        Func21430B4();
    }
    JumpAtRandom();
}

void PlayerAi::Func2_213D9B8(AiContext& context)
{
    Player* p = _player;
    if (p->m_altForm) {
        CheckUnmorph();
    } else {
        assert(_node40 != nullptr);
        Func2142ABC(_node40->Position);
        Field118++;
        if (!p->m_usedJump && _buttons.L.FramesUp > 5 * 2
            && (p->m_standTerrain == kTerrainLava || (p->m_horizColTimer > 10 * 2 && p->m_grounded))) {
            _buttons.L.IsDown = true;
        }
        Func2140B18(context, _node40->Position);
    }
}

void PlayerAi::Func2_213D96C(AiContext& context)
{
    CheckUnmorph();
    if (Flags2 & AiFlags2::TargetPlayer) {
        Func2145C14(_targetPlayer->m_position);
    }
}

} // namespace fp
