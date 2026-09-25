// PlayerAiData: following the node graph, in biped and alt form.
#include "PlayerAi.h"

#include "PlayerAiUtil.h"
#include "World.h"

#include <cassert>
#include <cmath>

#pragma GCC diagnostic ignored "-Wunused-parameter"

namespace fp {

using namespace ai;

namespace {
int field18For(const NodeData3* node, const Player* p, Hunter hunter)
{
    return node->NodeType == NodeType::AltForm && hunter != Hunter::Guardian ? 2 : 0;
}
} // namespace

void PlayerAi::Func2140094(AiContext& context)
{
    // Walk (or roll) the path toward _node3C, one node at a time.
    Player* p = _player;
    auto setField118 = [&] {
        // MP13 ACCELERATOR (Fuel Stack): going up the middle lift counts as arrived
        if (_world.roomId() == 106 && p->m_position[0] > -2 && p->m_position[0] < 2 && p->m_position[2] > -2 && p->m_position[2] < 2
            && p->m_speed[1] > 0 && p->m_position[1] - _node40->Position[1] > 1) {
            Field118 = 151 * 2;
        } else {
            Field118++;
        }
    };
    if (_node40 == nullptr) {
        return;
    }
    for (int guard = 0; guard < 64 && Func2140584(context); guard++) {
    }
    if (_node40 == nullptr) {
        return;
    }
    context.Field18 = field18For(_node40, p, p->m_hunter);
    if (p->m_altForm) {
        if (p->m_deathaltTimer == 0 && (context.Field1C == 1 || (!context.Field28 && context.Field18 != 2))) {
            CheckUnmorph();
        }
        if (context.Field20 != 0 && (context.Field1C != 1 || p->m_deathaltTimer != 0)) {
            if (p->v.AltFormStrafe != 0) {
                if (context.Field30 && context.Field2C == 0) {
                    Func2142A80();
                } else if (!context.Field30 && context.Field2C == 0) {
                    Func2142AE8(_node40->Position);
                } else if (context.Field30 && context.Field2C == 1) {
                    Func2141EA8();
                } else if (!context.Field30 && context.Field2C == 1) {
                    Func214201C();
                }
            } else if (context.Field2C == 1) {
                Func2140D5C();
            } else if (context.Field2C != 2 || Field118 != 0) {
                Func21418D8(_node40->Position);
            } else {
                Func214182C();
            }
            setField118();
        }
    } else {
        if (context.Field1C != 1 && (context.Field28 || context.Field18 == 2)) {
            PressButton(_touchButtons.Morph, 10);
        }
        if (context.Field20 == 2) {
            PressL();
        }
        if (context.Field20 != 0) {
            if ((Flags2 & AiFlags2::Bit21) || p->m_grounded
                || lengthSquared(withY(p->m_position - _node40->Position, 0)) > _node40->MaxDistance * _node40->MaxDistance) {
                const bool plain = context.Field2C == 0 || context.Field2C == 2 || context.Field20 == 2;
                if (context.Field30 && plain) {
                    Func2142A80();
                } else if (!context.Field30 && plain) {
                    Func2142AE8(_node40->Position);
                } else if (context.Field30 && context.Field2C == 1) {
                    Func2141EA8();
                } else if (!context.Field30 && context.Field2C == 1) {
                    Func214201C();
                }
            }
            setField118();
            Func2140B18(context, _node40->Position);
        }
    }
}

bool PlayerAi::Func2140584(AiContext& context)
{
    // One step of the path's state: arrived at a node, stuck, or jumping.
    Player* p = _player;
    assert(_node40 != nullptr);
    if (context.Field24 == 0) {
        if (IsNodeInRange(_node40)) {
            Field118 = 0;
            _field78 = 0;
            Flags2 &= ~AiFlags2::Bit15;
            context.Field40 = 0;
            context.Field44 = 0;
            context.Field34 = p->m_position;
            FindQueuedEntityRef();
            if (_node40 == _node3C) {
                context.Field20 = 0;
                context.Field24 = 1;
                Flags2 &= ~AiFlags2::Bit7;
                return true;
            }
            const NodeData3* node2 = nullptr;
            const NodeData3* node1 = Func213A1A8();
            Vec3 bomb0Position, bomb1Position;
            if (Func2139E34(_node40, node1, bomb0Position, bomb1Position)) {
                // a Lockjaw tether across the way: around it
                node2 = Func2139F84(_node40, node1, bomb0Position, bomb1Position);
                if (node2 != node1) {
                    node1 = node2;
                }
            }
            context.Field14 = 0;
            context.Field1C = 0;
            auto jump = [&] {
                context.Field14 = 1;
                context.Field1C = 1;
                context.Field20 = 2;
                context.Field24 = 2;
            };
            if (node1 == node2) {
                jump();
            } else {
                for (int i = 0; i < _node40->Count2; i++) {
                    if ((*_node40->Values)[_node40->Index2 + i] == node1->Id) {
                        jump();
                        break;
                    }
                }
            }
            _node48 = _node44 = _node40;
            _field4C[0] = _node40 = node1;
            context.Field18 = field18For(_node40, p, p->m_hunter);
            Flags2 |= AiFlags2::Bit7;
            return context.Field24 != 0;
        }
        auto skipNode = [&] {
            _field7A[_field78] = _node40->Id;
            if (_field78 < 9) {
                _field78++;
            }
            FindEntityRef(1);
            _node40 = _entityRefs.Nodes[1];
            if (_node40 != nullptr) {
                context.Field18 = field18For(_node40, p, p->m_hunter);
            }
        };
        if (p->m_horizColTimer <= 10 * 2 || (p->m_grounded && p->m_horizColTimer <= 60 * 2) || p->m_altForm || p->m_morphing) {
            if (p->m_horizColTimer > 30 * 2 && p->m_grounded && p->m_altForm && !p->m_unmorphing && Field118 > 30 * 2) {
                skipNode();
            } else if ((Flags2 & AiFlags2::TargetPlayer) && _targetPlayer != nullptr && _targetPlayer->m_hunter == Hunter::Sylux
                && _targetPlayer->m_altForm) {
                if (Func2139C60(_targetPlayer)) {
                    context.Field14 = 1;
                    context.Field1C = 1;
                    context.Field20 = 2;
                    context.Field24 = 2;
                    Flags2 |= AiFlags2::Bit15;
                }
            } else if (_node44 == nullptr || !IsNodeInRange(_node44)) {
                if (lengthSquared(_node40->Position - p->m_position) < 0.5f * 0.5f && p->m_grounded && Field118 > 30 * 2) {
                    Field118 = 151 * 2;
                }
            }
        } else if (Flags2 & AiFlags2::Bit15) {
            skipNode();
        } else {
            // stuck against a wall: jump
            context.Field14 = 1;
            context.Field1C = 1;
            context.Field20 = 2;
            context.Field24 = 2;
            Flags2 |= AiFlags2::Bit15;
        }
        return false;
    }
    if (context.Field24 == 1) {
        Field118 = 0;
        _field78 = 0;
        Flags2 &= ~AiFlags2::Bit15;
        context.Field40 = 0;
        context.Field44 = 0;
        context.Field34 = p->m_position;
        FindQueuedEntityRef();
        if (_node3C != nullptr && IsNodeInRange(_node3C) && _node40 == _node3C) {
            return false;
        }
        context.Field14 = 0;
        context.Field18 = 0;
        context.Field1C = 0;
        context.Field20 = 1;
        context.Field24 = 0;
        if (IsNodeInRange(_node40) || (Flags2 & AiFlags2::Bit7)) {
            return true;
        }
        FindEntityRef(1);
        _node40 = _entityRefs.Nodes[1];
        return false;
    }
    if (context.Field24 == 2 && p->m_usedJump) {
        context.Field20 = 1;
        context.Field24 = 0;
        return true;
    }
    return false;
}

bool PlayerAi::Func2139E34(const NodeData3* node1, const NodeData3* node2, Vec3& bomb0Position, Vec3& bomb1Position)
{
    // A Sylux's two Lockjaw bombs whose tether crosses the way from node1 to node2.
    const float nodeMidpointY = (node1->Position[1] + node2->Position[1]) / 2;
    for (auto& slot : _world.m_slots) {
        Player* other = slot.player.get();
        if (other->m_hunter != Hunter::Sylux || other->m_syluxBombCount != 2 || slot.syluxBombs[0] < 0 || slot.syluxBombs[1] < 0) {
            continue;
        }
        const Vec3 b0 = _world.m_bombs[slot.syluxBombs[0]].position;
        const Vec3 b1 = _world.m_bombs[slot.syluxBombs[1]].position;
        const float yDiff = (b0[1] + b1[1]) / 2 - nodeMidpointY;
        if (yDiff >= -3 && yDiff <= 3 && Func204CF74(node1->Position, node2->Position, b0, b1)) {
            bomb0Position = b0;
            bomb1Position = b1;
            return true;
        }
    }
    return false;
}

bool PlayerAi::Func204CF74(const Vec3& a1, const Vec3& a2, const Vec3& a3, const Vec3& a4)
{
    // whether the segments a1-a2 and a3-a4 cross, seen from above
    const float v18 = (a1[0] - a3[0]) * (a4[0] - a3[0]) + (a1[2] - a3[2]) * (a4[2] - a3[2]);
    const float v19 = (a1[0] - a4[0]) * (a4[0] - a3[0]) + (a1[2] - a4[2]) * (a4[2] - a3[2]);
    const float v22 = (a1[0] - a3[0]) * (a2[0] - a1[0]) + (a1[2] - a3[2]) * (a2[2] - a1[2]);
    const float v23 = (a2[0] - a3[0]) * (a2[0] - a1[0]) + (a2[2] - a3[2]) * (a2[2] - a1[2]);
    return ((v18 <= 0 && v19 >= 0) || (v18 >= 0 && v19 <= 0)) && ((v22 <= 0 && v23 >= 0) || (v22 >= 0 && v23 <= 0));
}

const NodeData3* PlayerAi::Func2139F84(const NodeData3* node1, const NodeData3* node2, const Vec3& bomb0Position, const Vec3& bomb1Position)
{
    // a neighbor of both nodes whose way around does not cross the tether
    std::array<const NodeData3*, 20> nodeList1{}, nodeList2{}, nodeList3{};
    const int nodeCount1 = Func213A0A4(node1, nodeList1.data());
    const int nodeCount2 = Func213A0A4(node2, nodeList2.data());
    int nodeCount3 = 0;
    for (int i = 0; i < nodeCount1; i++) {
        for (int j = 0; j < nodeCount2; j++) {
            if (nodeList1[i] == nodeList2[j] && nodeCount3 < 20) {
                nodeList3[nodeCount3++] = nodeList1[i];
            }
        }
    }
    for (int i = 0; i < nodeCount3; i++) {
        const NodeData3* node = nodeList3[i];
        if (!Func204CF74(node1->Position, node->Position, bomb0Position, bomb1Position)
            && !Func204CF74(node->Position, node2->Position, bomb0Position, bomb1Position)) {
            return node;
        }
    }
    return node2;
}

bool PlayerAi::Func2139C60(Player* target)
{
    // whether this bot is inside the triangle of a Sylux's Lockjaw bombs
    const auto& slot = _world.m_slots[target->m_slot];
    if (target->m_syluxBombCount != 2 && target->m_syluxBombCount != 3) {
        return false;
    }
    const int count = target->m_syluxBombCount;
    for (int i = 0; i < count; i++) {
        if (slot.syluxBombs[i] < 0) {
            return false;
        }
    }
    const Vec3 bomb0 = _world.m_bombs[slot.syluxBombs[0]].position;
    const Vec3 bomb1 = _world.m_bombs[slot.syluxBombs[1]].position;
    const Vec3 bomb2 = count == 2 ? target->m_position : _world.m_bombs[slot.syluxBombs[2]].position;
    const Vec3 bomb01 = bomb1 - bomb0;
    const Vec3 bomb12 = bomb2 - bomb1;
    const Vec3 bomb20 = bomb0 - bomb2;
    const Vec3 c = normalized(cross(bomb12, bomb01));
    const Vec3 bomb0Player = _player->m_position - bomb0;
    const Vec3 bomb1Player = _player->m_position - bomb1;
    const Vec3 bomb2Player = _player->m_position - bomb2;
    const float d = dot(c, bomb0Player);
    return d > -0.75f && d < 0.75f && dot(cross(bomb0Player, bomb01), c) > 0 && dot(cross(bomb1Player, bomb12), c) > 0
        && dot(cross(bomb2Player, bomb20), c) > 0;
}

Vec3 PlayerAi::AltVec() const
{
    const Player* p = _player;
    return p->m_altForm ? Vec3{p->m_field80, 0, p->m_field84} : Vec3{p->m_field70, 0, p->m_field74};
}

Vec3 PlayerAi::RollVec() const { return {_player->m_altRollFbX, 0, _player->m_altRollFbZ}; }

void PlayerAi::Func2142A80()
{
    assert(_node40 != nullptr);
    Func2142AE8(_node40->Position);
    Func2145C14(_node40->Position);
}

void PlayerAi::Func2141EA8()
{
    // turn along the edge from the last node to this one
    assert(_node40 != nullptr);
    const Vec3 altVec = AltVec();
    Vec3 toNode = _node40->Position - (_node44 != nullptr ? _node44->Position : _field90);
    toNode = toNode[0] != 0 || toNode[2] != 0 ? normalized(withY(toNode, 0)) : altVec;
    const float d = dot(altVec, toNode);
    if (d < 1) {
        _buttonAimX = TurnToward(d, altVec, toNode);
    }
    Func214201C();
}

void PlayerAi::Func2142AE8(const Vec3& position)
{
    // move toward `position` with the biped's (or a strafing alt form's) X/B/Y/A
    const Vec3 altVec = AltVec();
    Vec3 toPos = position - _player->m_position;
    toPos = toPos[0] != 0 || toPos[2] != 0 ? normalized(withY(toPos, 0)) : altVec;
    Helper01(altVec, toPos, _buttons.X, _buttons.B, _buttons.Y, _buttons.A);
}

void PlayerAi::Func2142ABC(const Vec3& position)
{
    Func2142AE8(position);
    Func2145C14(position);
}

void PlayerAi::Func21418D8(const Vec3& position)
{
    // roll toward `position`
    if (CheckSpireClimbInput()) {
        return;
    }
    Vec3 toPos = position - _player->m_position;
    toPos = toPos[0] != 0 || toPos[2] != 0 ? normalized(withY(toPos, 0)) : RollVec();
    Func2141CD4(toPos);
}

void PlayerAi::Func2141CD4(const Vec3& toPos) { Helper01(RollVec(), toPos, _buttons.Up, _buttons.Down, _buttons.Left, _buttons.Right); }

void PlayerAi::Func214201C() { Helper02(AltVec(), _buttons.X, _buttons.B, _buttons.Y, _buttons.A); }

void PlayerAi::Func2140D5C()
{
    if (CheckSpireClimbInput()) {
        return;
    }
    Helper02(RollVec(), _buttons.Up, _buttons.Down, _buttons.Left, _buttons.Right);
}

bool PlayerAi::CheckSpireClimbInput()
{
    // Spire on a wall: let go of the stick now and then, or it keeps climbing
    if (_player->m_hunter == Hunter::Spire) {
        if (_field116 > 0) {
            _field116--;
        } else if (_player->m_spireClimbing) {
            if (_buttons.Up.FramesUp < 10 * 2 || _buttons.Down.FramesUp < 10 * 2 || _buttons.Left.FramesUp < 10 * 2
                || _buttons.Right.FramesUp < 10 * 2) {
                return true;
            }
            _field116 = 10 * 2;
        }
    }
    return false;
}

void PlayerAi::Helper01(const Vec3& altVec, const Vec3& toPos, AiButton& upButton, AiButton& downButton, AiButton& leftButton,
    AiButton& rightButton)
{
    // the direction buttons nearest the way to go, diagonals included
    const float d = dot(altVec, toPos);
    const bool left = cross(altVec, toPos)[1] >= 0;
    if (d > 0.866f) {
        upButton.IsDown = true;
    } else if (d > 0.5f) {
        upButton.IsDown = true;
        (left ? leftButton : rightButton).IsDown = true;
    } else if (d > -0.5f) {
        (left ? leftButton : rightButton).IsDown = true;
    } else if (d >= -0.866f) {
        downButton.IsDown = true;
    } else {
        downButton.IsDown = true;
        (left ? leftButton : rightButton).IsDown = true;
    }
}

void PlayerAi::Helper02(const Vec3& altVec, AiButton& upButton, AiButton& downButton, AiButton& leftButton, AiButton& rightButton)
{
    // Keep between the edges of the corridor from the last node to this one
    // (each node's MaxDistance wide), weaving back toward its middle.
    assert(_node40 != nullptr);
    const Player* p = _player;
    Vec3 toNode = _node40->Position - (_node44 != nullptr ? _node44->Position : _field90);
    toNode = toNode[0] != 0 || toNode[2] != 0 ? normalized(withY(toNode, 0)) : altVec;
    Vec3 playerToNode = _node40->Position - p->m_position;
    // (the C#'s: normalizes toNode here rather than playerToNode)
    playerToNode = playerToNode[0] != 0 || playerToNode[2] != 0 ? normalized(withY(toNode, 0)) : UnitX;
    if (dot(playerToNode, toNode) < 0) {
        _node44 = nullptr;
        _field90 = p->m_position;
        toNode = _node40->Position - _field90;
        toNode = toNode[0] != 0 || toNode[2] != 0 ? normalized(withY(toNode, 0)) : altVec;
    }
    const float d = dot(altVec, toNode);
    const Vec3 cross1 = cross(altVec, toNode);
    Vec3 cross2 = cross(toNode, UnitY);
    cross2 = cross2[0] != 0 || cross2[2] != 0 ? normalized(withY(cross2, 0)) : withY(toNode, 0);
    auto side = [&](float sign) {
        const Vec3 pos1Add = _node40->Position + cross2 * (sign * _node40->MaxDistance);
        const Vec3 pos2Add = _node44 != nullptr ? _node44->Position + cross2 * (sign * _node44->MaxDistance) : _field90 + cross2 * (sign * _field9C);
        const Vec3 pos3Add = p->m_position + cross2 * (sign / 2);
        const Vec3 pos21 = withY(pos1Add - pos2Add, 0);
        const Vec3 pos23 = withY(pos3Add - pos2Add, 0);
        if ((pos21[0] != 0 || pos21[2] != 0) && (pos23[0] != 0 || pos23[2] != 0)) {
            return cross(pos21, pos23);
        }
        return UnitY;
    };
    const Vec3 cross3 = side(1);
    const Vec3 cross4 = side(-1);
    if (cross4[1] > 0) {
        Flags2 |= AiFlags2::Bit11;
    } else if (cross3[1] < 0) {
        Flags2 &= ~AiFlags2::Bit11;
    }
    const bool bit11 = Flags2 & AiFlags2::Bit11;
    if (d > 0.866f) {
        upButton.IsDown = true;
        (bit11 ? rightButton : leftButton).IsDown = true;
    } else if (d > 0.5f) {
        if (cross1[1] >= 0) {
            (bit11 ? upButton : leftButton).IsDown = true;
        } else {
            (bit11 ? rightButton : upButton).IsDown = true;
        }
    } else if (d > -0.5f) {
        if (cross1[1] >= 0) {
            leftButton.IsDown = true;
            (bit11 ? upButton : downButton).IsDown = true;
        } else {
            rightButton.IsDown = true;
            (bit11 ? downButton : upButton).IsDown = true;
        }
    } else if (d >= -0.866f) {
        downButton.IsDown = true;
        (bit11 ? leftButton : rightButton).IsDown = true;
    } else if (cross1[1] >= 0) {
        (bit11 ? leftButton : downButton).IsDown = true;
    } else {
        (bit11 ? downButton : rightButton).IsDown = true;
    }
}

void PlayerAi::Func214182C()
{
    assert(_node40 != nullptr);
    Func2141840(_node40->Position);
}

void PlayerAi::Func2141840(const Vec3& position)
{
    Vec3 toPos = position - _player->m_position;
    toPos = toPos[0] != 0 || toPos[2] != 0 ? normalized(withY(toPos, 0)) : RollVec();
    Func2141A0C(toPos);
}

void PlayerAi::Func2141A0C(const Vec3& toPos)
{
    // Samus rolling, with a boost now and then (a touch-screen flick in the
    // game, which the C# records without acting on)
    const Vec3 altVec = RollVec();
    const float d = dot(altVec, toPos);
    const Vec3 c = cross(altVec, toPos);
    if ((_player->abilities() & 0x40) && !_player->m_boosting) {
        if (_framesWithTouch == 0 && static_cast<uint32_t>(_framesWithoutTouch) > static_cast<uint32_t>(_field1032)) {
            _field1032 = static_cast<int>(_field1030 + randomInt2(_field1030 / 2));
            _hasTouch = true;
            _touchAimX = static_cast<uint16_t>(static_cast<int>(50 * c[1]) + 128);
            _touchAimY = static_cast<uint16_t>(static_cast<int>(50 * d) + 100);
        } else if (_framesWithTouch == 1) {
            _hasTouch = true;
            _touchAimX = static_cast<uint16_t>(256 - _touchAimX);
            _touchAimY = static_cast<uint16_t>(200 - _touchAimY);
        }
    }
    Helper01(altVec, toPos, _buttons.Up, _buttons.Down, _buttons.Left, _buttons.Right);
}

void PlayerAi::Func2140B18(AiContext& context, const Vec3& position)
{
    // Stuck on a ledge in the air: stop, and jump for a quarter second.
    Player* p = _player;
    if (context.Field44 > 0) {
        context.Field44--;
        PressL();
        if (!p->m_altForm || p->v.AltFormStrafe != 0) {
            _buttons.X.IsDown = _buttons.B.IsDown = _buttons.Y.IsDown = _buttons.A.IsDown = false;
        } else {
            _buttons.Up.IsDown = _buttons.Down.IsDown = _buttons.Left.IsDown = _buttons.Right.IsDown = false;
        }
        return;
    }
    if (p->m_speed[1] >= 0.0625f / 2 || p->m_speed[1] < -0.0625f / 2 || p->m_morphing || p->m_unmorphing) {
        context.Field40 = 0;
        context.Field34 = p->m_position;
    } else if (p->m_collidingLateral && !p->m_grounded) {
        context.Field40++;
    }
    if (context.Field40 >= 12 * 2) {
        const Vec3 toPlayer = withY(p->m_position - context.Field34, 0);
        const Vec3 toPos = normalized(withY(position - p->m_position, 0));
        if (dot(toPlayer, toPos) < 1 / 256.0f) {
            context.Field44 = 15 * 2;
        }
        context.Field40 = 0;
        context.Field34 = p->m_position;
    }
}

void PlayerAi::Func214003C(AiContext& context)
{
    if (Flags2 & AiFlags2::TargetPlayer) {
        Func21436D8();
    } else if (Flags4 & AiFlags4::Bit1) {
        Func214380C();
    }
    Func2140094(context);
}

} // namespace fp
