// PlayerAiData funcs_4: run once when a tree node is switched to.
#include "PlayerAi.h"

#include "PlayerAiUtil.h"
#include "World.h"

#include <cassert>
#include <optional>

#pragma GCC diagnostic ignored "-Wunused-parameter"

namespace fp {

using namespace ai;

// the main init, counterpart to Func2_213EA48
void PlayerAi::Func4_21462DC(AiContext& context)
{
    Player* p = _player;
    Func214715C(context);
    if (context.FieldA == 31) {
        Flags2 &= ~AiFlags2::Bit0;
    }
    Vec3 targetPos{};
    Vec3 halfturretPos{};
    if (context.Field9 == 4 || context.FieldB == 4 || context.Field9 == 41 || context.FieldC == 56 || context.FieldC == 57
        || context.FieldC == 59 || context.Field5 == 58) {
        if (context.Field7 == 1) {
            Func2135510();
        }
        if (context.Field7 == 2) {
            Func21354B0();
        }
        if ((Flags2 & AiFlags2::TargetPlayer) && _targetPlayer != nullptr) {
            targetPos = TargetPosition(_targetPlayer);
        }
    }
    if ((Flags2 & AiFlags2::TargetHalfturret) && _targetHalfturret != nullptr
        && (context.Field9 == 5 || context.FieldB == 5 || context.FieldC == 60)) {
        halfturretPos = _targetHalfturret->m_halfturret.position;
    }
    if (context.Field9 == 6 && context.Field8 >= 7 && context.Field8 <= 11) {
        const int type = 55 + (context.Field8 - 7); // Type55 to Type59
        FindEntityRef(type);
        UpdateTargetItem(ItemRef(type));
    }
    if (context.Field9 == 23 && !(Flags2 & AiFlags2::TargetDefense)) {
        Func2135320();
    }
    if (context.FieldC == 61 && !(Flags2 & AiFlags2::TargetDoor)) {
        Func21355D8();
    }
    if (context.FieldA == 32) {
        if (context.FieldB == 4 && (Flags2 & AiFlags2::TargetPlayer)) {
            _field1038 = targetPos - p->m_cam.position;
        } else if (context.FieldB == 3 && (Flags2 & AiFlags2::TargetPlayer)) {
            AiPlayerAggro* aggro = AggroFunc214847C(4, 7, 1, nullptr, nullptr);
            if (aggro != nullptr && aggro->Player1 != nullptr) {
                _field1038 = aggro->Player1->m_position - p->m_cam.position;
            } else {
                _field1038 = p->m_cam.facing;
            }
        } else if (context.FieldB == 5 && (Flags2 & AiFlags2::TargetHalfturret)) {
            _field1038 = halfturretPos - p->m_cam.position;
        } else if (context.FieldB == 25) {
            const float x = randomInt2(4096) / 4096.0f - 0.5f;
            const float y = randomInt2(4096) / 4096.0f - 0.5f;
            const float z = randomInt2(4096) / 4096.0f - 0.5f;
            _field1038 = {x, y, z};
        } else if (context.FieldB == 26) {
            const float x = randomInt2(4096) / 4096.0f - 0.5f;
            const float z = randomInt2(4096) / 4096.0f - 0.5f;
            const float len = std::sqrt(x * x + z * z);
            const float y = randomInt2(static_cast<uint32_t>(toFx(len))) / 4096.0f - len / 2;
            _field1038 = {x, y, z};
        } else if (context.FieldB == 27) {
            _field1038 = _fieldB8 - p->m_cam.position;
        }
        _field1038 = !isZero(_field1038) ? normalized(_field1038) : p->m_cam.facing;
    }
    if (context.Field4 != 0) {
        Field118 = 0;
        _field78 = 0;
        Flags2 &= ~AiFlags2::Bit15;
        context.Field40 = 0;
        context.Field44 = 0;
        context.Field34 = p->m_position;
    }
    const NodeData3* v25 = nullptr;
    auto setField18 = [&] {
        context.Field18 = _node40->NodeType != NodeType::AltForm || p->m_hunter == Hunter::Guardian ? 1 : 2;
    };
    if (context.Field4 == 37 || context.Field9 == 39) {
        bool node40Set = false;
        if ((Flags2 & AiFlags2::Bit7) && context.Field4 == 37) {
            FindEntityRef(0);
            _node40 = _entityRefs.Nodes[0];
            if (_node40 == _node48) {
                v25 = Func213A1A8();
                if (v25 == _field4C[0]) {
                    _node44 = _node40;
                    _node40 = v25;
                    node40Set = true;
                }
            } else if (!p->m_grounded) {
                _node40 = _field4C[0];
                node40Set = true;
            }
        }
        if (!node40Set) {
            FindEntityRef(1);
            _node40 = _entityRefs.Nodes[1];
        }
        if (_node40 != nullptr) {
            setField18();
        }
    }
    if (context.Field4 == 33) {
        if (context.Field9 == 12) {
            FindEntityRef(25);
            _node3C = _node40 = _entityRefs.Nodes[25];
        } else if (context.Field9 == 13) {
            FindEntityRef(24);
            _node3C = _node40 = _entityRefs.Nodes[24];
        } else if (context.Field9 == 39) {
            _node3C = _node40;
        } else if (context.Field9 != 4) {
            std::optional<Vec3> position;
            if (context.Field9 == 5) {
                position = _targetHalfturret->m_halfturret.position;
            } else if (context.Field9 == 6) {
                if (Flags2 & AiFlags2::TargetItem) {
                    position = addY(_itemC8->position, -0.5f);
                }
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
            }
            if (position) {
                const NodeData3* node = FindClosestNonHazardNodeToPosition(*position);
                if (node != nullptr && node->NodeType == NodeType::AltForm && p->m_hunter != Hunter::Guardian && context.FieldD != 29) {
                    context.FieldD = 29;
                    context.FieldE = 0;
                }
            }
        }
    } else if (context.Field4 == 34) {
        if (context.Field9 == 4 && (Flags2 & AiFlags2::TargetPlayer)) {
            _fieldAC = withY(_targetPlayer->m_position - p->m_position, 0);
        } else if (context.Field9 == 5 && (Flags2 & AiFlags2::TargetHalfturret)) {
            _fieldAC = withY(_targetHalfturret->m_halfturret.position - p->m_position, 0);
        }
        if (_fieldAC[0] != 0 || _fieldAC[2] != 0) {
            _fieldAC = normalized(_fieldAC);
        }
    } else if (context.Field4 == 37) {
        context.Field20 = 0;
        context.Field24 = 1;
        context.Field14 = 0;
        context.Field18 = 0;
        context.Field1C = 0;
        context.Field2C = context.Field5 == 47 ? 1 : context.Field5 == 48 ? 2 : 0;
        context.Field28 = context.FieldD == 29;
        context.Field30 = context.FieldA == 0 && context.FieldC != 55 && context.FieldC != 56 && context.FieldC != 57 && context.FieldC != 59
            && context.FieldC != 60 && context.FieldC != 61;
        // Field9 -> (entity ref, queued find), for the targets a path leads to.
        struct Target {
            int field9, ref, queued;
        };
        static constexpr Target targets[] = {
            {12, 25, AiQueuedEntNone}, {13, 24, AiQueuedEntNone}, {14, 6, 27}, {15, 7, 28}, {16, 8, 29}, {17, 9, 30}, {18, 10, 31},
            {19, 11, 32}, {20, 12, 33}, {21, 13, 34}, {22, 14, 35}, {23, 15, 36}, {44, 23, AiQueuedEntNone}, {45, 22, AiQueuedEntNone},
            {46, 21, AiQueuedEntNone},
        };
        const Target* found = nullptr;
        for (const Target& t : targets) {
            if (t.field9 == context.Field9) {
                found = &t;
            }
        }
        if (context.Field9 == 4 && (Flags2 & AiFlags2::TargetPlayer)) {
            FindEntityRef(2);
            _node3C = _entityRefs.Nodes[2];
            _queuedFindEntityAction = 0;
        } else if (context.Field9 == 5 && (Flags2 & AiFlags2::TargetHalfturret)) {
            FindEntityRef(3);
            _node3C = _entityRefs.Nodes[3];
            _queuedFindEntityAction = 8;
        } else if (context.Field9 == 6 && (Flags2 & AiFlags2::TargetItem)) {
            FindEntityRef(5);
            _node3C = _entityRefs.Nodes[5];
            _queuedFindEntityAction = context.Field8 >= 7 && context.Field8 <= 11 ? 12 + (context.Field8 - 7) : AiQueuedEntNone;
        } else if (found != nullptr) {
            FindEntityRef(found->ref);
            _node3C = _entityRefs.Nodes[found->ref];
            _queuedFindEntityAction = found->queued;
        } else if (context.Field9 == 24) {
            _node3C = GetRandomNavigationNode();
            _queuedFindEntityAction = AiQueuedEntNone;
        } else if (context.Field9 == 40) {
            _node3C = FindHighestNode();
            _queuedFindEntityAction = 9;
        } else if (context.Field9 == 41 && (Flags2 & AiFlags2::TargetPlayer)) {
            FindEntityRef(17);
            _node3C = _entityRefs.Nodes[17];
            _queuedFindEntityAction = 3;
        } else if (context.Field9 == 42 && (Flags2 & AiFlags2::TargetPlayer)) {
            FindEntityRef(18);
            _node3C = _entityRefs.Nodes[18];
            _queuedFindEntityAction = 4;
        } else {
            if (Flags2 & AiFlags2::Bit1) {
                FindQueuedEntityRef();
            } else {
                _node3C = _node40;
            }
            _queuedFindEntityAction = AiQueuedEntNone;
        }
        if (_node40 != nullptr && IsNodeInRange(_node40)) {
            if (v25 == nullptr) {
                v25 = Func213A1A8();
            }
            bool v33 = false;
            for (int i = 0; i < _node40->Count2; i++) {
                if ((*_node40->Values)[_node40->Index2 + i] == v25->Id) {
                    v33 = true;
                    break;
                }
            }
            if (!v33) {
                _node48 = _node44 = _node40;
                _field4C[0] = _node40 = v25;
                setField18();
                Flags2 |= AiFlags2::Bit7;
            }
        }
    }
    if (context.Field5 == 47) {
        _field90 = p->m_position;
        _field9C = 0.5f;
    }
    if (context.Field6 == 51 && !p->m_altForm && !p->m_morphing && !p->m_usedJump && _buttons.L.FramesUp > 5 * 2) {
        _buttons.L.IsDown = true;
    }
}

void PlayerAi::Func4_2145EB0(AiContext& context)
{
    context.Field40 = 0;
    context.Field34 = _player->m_position;
    if (randomInt2(2) == 0) {
        Flags2 &= ~AiFlags2::Bit12;
    } else {
        Flags2 |= AiFlags2::Bit12;
    }
}

void PlayerAi::Func4_21462AC(AiContext& context)
{
    if (_player->m_altForm) {
        _touchButtons.Unmorph.IsDown = true;
    }
}

void PlayerAi::Func4_2146284(AiContext& context) { Func4_21462AC(context); }

void PlayerAi::Func4_21461EC(AiContext& context)
{
    FindEntityRef(1);
    _node40 = _entityRefs.Nodes[1];
    if (Flags2 & AiFlags2::TargetPlayer) {
        FindEntityRef(17);
        _node3C = _entityRefs.Nodes[17];
    } else {
        _node3C = _node40;
    }
    Field118 = 0;
    _field78 = 0;
    Flags2 &= ~AiFlags2::Bit15;
    _queuedFindEntityAction = 3;
    context.Field20 = 0;
    context.Field24 = 1;
    context.Field14 = 0;
    context.Field18 = 0;
    context.Field1C = 0;
    context.Field2C = 0;
    context.Field28 = true;
    context.Field30 = false;
}

void PlayerAi::Func4_214612C(AiContext& context)
{
    Field118 = 0;
    _field78 = 0;
    Flags2 &= ~AiFlags2::Bit15;
    context.Field40 = 0;
    context.Field44 = 0;
    context.Field34 = _player->m_position;
    context.Field20 = 0;
    context.Field24 = 1;
    context.Field14 = 0;
    context.Field18 = 0;
    context.Field1C = 0;
    context.Field2C = 0;
    context.Field28 = true;
    context.Field30 = true;
    FindEntityRef(1);
    _node40 = _entityRefs.Nodes[1];
    FindEntityRef(25);
    _node3C = _entityRefs.Nodes[25];
    _queuedFindEntityAction = AiQueuedEntNone;
    if (_node40 != nullptr && IsNodeInRange(_node40)) {
        const NodeData3* node = Func213A1A8();
        _node44 = _node40;
        _node40 = node;
    }
}

void PlayerAi::Func4_2145F78(AiContext& context)
{
    if (!(Flags2 & AiFlags2::TargetDefense)) {
        Func2135320();
    }
    Field118 = 0;
    _field78 = 0;
    Flags2 &= ~AiFlags2::Bit15;
    context.Field40 = 0;
    context.Field44 = 0;
    context.Field34 = _player->m_position;
    if (Flags2 & AiFlags2::TargetDefense) {
        const float radius = _targetDefense->volume.radius;
        if (radius > 0.5f) {
            const Vec3 toDefense = withY(_targetDefense->position - _player->m_position, 0);
            const float x = randomInt2(4096) / 4096.0f;
            _fieldA0 = Vec3{x * sign(toDefense[0]), 0, std::sqrt(1 - x * x) * sign(toDefense[2])} * radius + _targetDefense->position;
        }
    }
}

void PlayerAi::Func4_2145F50(AiContext& context) { Func4_21462AC(context); }
void PlayerAi::Func4_2145F28(AiContext& context) { Func4_21462AC(context); }
void PlayerAi::Func4_2145F00(AiContext& context) { Func4_21462AC(context); }

void PlayerAi::Func4_2145E54(AiContext& context)
{
    FindEntityRef(0);
    _node40 = _entityRefs.Nodes[0];
    _node3C = _node40;
    Field118 = 0;
    context.Field40 = 0;
    context.Field44 = 0;
    context.Field34 = _player->m_position;
}

void PlayerAi::Func4_2145E40(AiContext& context) { Flags3 |= AiFlags3::Bit1; }
void PlayerAi::Func4_SetDespawned(AiContext& context) { Flags3 |= AiFlags3::Despawned; }

} // namespace fp
