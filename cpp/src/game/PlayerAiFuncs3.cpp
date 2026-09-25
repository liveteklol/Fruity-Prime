// PlayerAiData funcs_3: the checks that weigh switching the tree's path. Most
// return 0 or 1; the ones in pairs are each other's inverse.
#include "PlayerAi.h"

#include "PlayerAiUtil.h"
#include "World.h"

#include <cassert>

#pragma GCC diagnostic ignored "-Wunused-parameter"

namespace fp {

using namespace ai;

namespace {
constexpr int kTerrainLava = 8;
}

int PlayerAi::ExecuteFuncs3(AiContext& context, int funcId, const AiPersonalityData5& param)
{
    using Func3 = int (PlayerAi::*)(AiContext&, const AiPersonalityData5&);
#include "PlayerAiFuncs3Table.inc"
    if (funcId < 0 || funcId >= 212) {
        return 0;
    }
    return (this->*kFuncs3[funcId])(context, param);
}

#define FUNC3(name) int PlayerAi::name(AiContext& context, const AiPersonalityData5& param)

FUNC3(Func3_213D87C) { return 1; }

FUNC3(Func3_213D83C)
{
    return _node3C != nullptr && IsNodeInRange(_node3C) && _node40 == _node3C ? 1 : 0;
}

FUNC3(Func3_213D814)
{
    assert(_node40 != nullptr);
    return _node40->Position[1] - _player->m_position[1] > param.Param1 / 4096.0f ? 1 : 0;
}

FUNC3(Func3_213D7F0) { return (Flags2 & AiFlags2::Bit0) ? 1 : 0; }
FUNC3(Func3_213D800) { return (Flags4 & AiFlags4::Bit0) ? 1 : 0; }
FUNC3(Func3_213D7E8) { return 0; }
FUNC3(Func3_213D7D0) { return (Flags2 & AiFlags2::TargetItem) ? 0 : 1; }
FUNC3(Func3_213D7B8) { return Func3_213D7D0(context, param); }
FUNC3(Func3_213D7A0) { return Func3_213D7D0(context, param); }

FUNC3(Func3_213D77C)
{
    if (Flags2 & AiFlags2::TargetDoor) {
        return _targetDoor->shotOpen ? 1 : 0;
    }
    return 0;
}

FUNC3(Func3_213D758) { return Func3_213D77C(context, param) ^ 1; }

FUNC3(Func3_213D734)
{
    if (Flags2 & AiFlags2::TargetDoor) {
        return _targetDoor->locked ? 1 : 0;
    }
    return 0;
}

FUNC3(Func3_213D710) { return Func3_213D734(context, param) ^ 1; }

FUNC3(Func3_213D6D0)
{
    FindEntityRef(0);
    return _entityRefs.Nodes[0] != nullptr && IsNodeInRange(_entityRefs.Nodes[0]) ? 1 : 0;
}

FUNC3(Func3_213D6AC) { return Func3_213D6D0(context, param) ^ 1; }

FUNC3(Func3_213D624)
{
    if (!(Flags2 & AiFlags2::TargetPlayer)) {
        return 0;
    }
    FindEntityRef(0);
    const NodeData3* node0 = _entityRefs.Nodes[0];
    FindEntityRef(2);
    const NodeData3* node2 = _entityRefs.Nodes[2];
    return node0 == node2 || (node2 != nullptr && IsNodeInRange(node2)) ? 1 : 0;
}

FUNC3(Func3_213D608) { return Func3_213D624(context, param) ^ 1; }

FUNC3(Func3_213D564)
{
    if (!(Flags2 & AiFlags2::TargetPlayer)) {
        return 0;
    }
    const float dist = param.Param1 / 4096.0f;
    return distanceSquared(_targetPlayer->m_position, _player->m_position) < dist * dist ? 1 : 0;
}

FUNC3(Func3_213D540) { return Func3_213D564(context, param) ^ 1; }
FUNC3(Func3_213D530) { return Func213842C() ? 1 : 0; }
FUNC3(Func3_213D514) { return Func3_213D530(context, param) ^ 1; }

FUNC3(Func3_213D4C0)
{
    if (!(Flags2 & AiFlags2::TargetPlayer)) {
        return 0;
    }
    for (const BeamProjectile& beam : _world.m_slots[_targetPlayer->m_slot].beams) {
        if (beam.lifespan != 0) {
            return 1;
        }
    }
    return 0;
}

FUNC3(Func3_213D49C) { return Func3_213D4C0(context, param) ^ 1; }

FUNC3(Func3_213D43C)
{
    for (const auto& slot : _world.m_slots) {
        for (const BeamProjectile& beam : slot.beams) {
            if (beam.lifespan != 0 && beam.beam == Beam::OmegaCannon) {
                return 1;
            }
        }
    }
    return 0;
}

FUNC3(Func3_213D418) { return Func3_213D43C(context, param) ^ 1; }

FUNC3(Func3_213D388)
{
    // the first player object, in multiplayer
    Player* player = _world.m_slots.empty() ? nullptr : _world.m_slots[0].player.get();
    if (player == nullptr) {
        return 0;
    }
    return AggroFunc214857C(6, 1, 2, nullptr, player) ? 1 : 0;
}

FUNC3(Func3_213D36C) { return Func3_213D388(context, param) ^ 1; }

FUNC3(Func3_213D2C0)
{
    for (const auto& slot : _world.m_slots) {
        Player* player = slot.player.get();
        if (player != _player && player->m_isBot && AggroFunc214857C(6, 1, 2, nullptr, player)) {
            return 1;
        }
    }
    return 0;
}

FUNC3(Func3_213D2A4) { return Func3_213D2C0(context, param) ^ 1; }

FUNC3(Func3_213D234)
{
    if (!(Flags2 & AiFlags2::TargetPlayer)) {
        return 0;
    }
    return AggroFunc214857C(6, 2, 1, _targetPlayer, nullptr) ? 1 : 0;
}

FUNC3(Func3_213D218) { return Func3_213D234(context, param) ^ 1; }

FUNC3(Func3_213D178)
{
    for (const auto& slot : _world.m_slots) {
        Player* player = slot.player.get();
        if (player != _player && AggroFunc214857C(6, 1, 2, nullptr, player)) {
            return 1;
        }
    }
    return 0;
}

FUNC3(Func3_213D15C) { return Func3_213D178(context, param) ^ 1; }

bool PlayerAi::Func213995C(float angleCos, float maxDistSqr)
{
    const Vec3 vec1 = ExecuteVectorFunc(7, false, false);
    const float lengthSqr = lengthSquared(vec1);
    if (maxDistSqr > 0 && lengthSqr >= maxDistSqr) {
        return false;
    }
    const Vec3 vec2 = ExecuteVectorFunc(1, false, false);
    const float d = dot(vec2, vec1);
    return d > angleCos || (d > 0 && lengthSqr < 1);
}

FUNC3(Func3_213D128) { return Func213995C(fx(3849), 4 * 4) ? 1 : 0; }
FUNC3(Func3_213D0F4) { return Func213995C(fx(3849), 5 * 5) ? 0 : 1; }

FUNC3(Func3_213D0C4)
{
    if (!(Flags2 & AiFlags2::TargetPlayer)) {
        return 0;
    }
    return _targetPlayer->m_altForm ? 1 : 0;
}

FUNC3(Func3_213D0A8) { return Func3_213D0C4(context, param) ^ 1; }

FUNC3(Func3_213D078)
{
    if (!(Flags2 & AiFlags2::TargetPlayer)) {
        return 0;
    }
    return _targetPlayer->m_frozenTimer != 0 ? 1 : 0;
}

FUNC3(Func3_213D05C) { return Func3_213D078(context, param) ^ 1; }
FUNC3(Func3_213D044) { return (Flags2 & AiFlags2::TargetPlayer) ? 1 : 0; }
FUNC3(Func3_213D028) { return Func3_213D044(context, param) ^ 1; }
FUNC3(Func3_213D010) { return (Flags2 & AiFlags2::TargetHalfturret) ? 1 : 0; }
FUNC3(Func3_213CFF4) { return Func3_213D010(context, param) ^ 1; }
FUNC3(Func3_213CFDC) { return (Flags2 & AiFlags2::TargetItem) ? 1 : 0; }
FUNC3(Func3_213CFC0) { return Func3_213CFDC(context, param) ^ 1; }
FUNC3(Func3_213CFA4) { return _slotHits[_player->m_slot] != 0 ? 1 : 0; }

FUNC3(Func3_213CF0C)
{
    if (!(Flags2 & AiFlags2::TargetPlayer)) {
        return 0;
    }
    return AggroFunc214857C(4, 2, 1, _targetPlayer, nullptr) ? 1 : 0;
}

FUNC3(Func3_213CEE8) { return Func3_213CF0C(context, param) ^ 1; }

int PlayerAi::TeammateHits(bool any)
{
    // Func3_213CDB8 (any) and Func3_213C89C (how many): hits on teammates nearby or in view.
    int hits = 0;
    for (const auto& slot : _world.m_slots) {
        Player* other = slot.player.get();
        const float distSqr = distanceSquared(other->m_position, _player->m_position);
        bool hasAggro = false;
        if (distSqr >= 7 * 7) {
            hasAggro = AggroFunc214857C(6, 1, 2, nullptr, other);
        }
        if (other != _player && other->m_teamIndex == _player->m_teamIndex && other->m_health != 0 && (distSqr < 7 * 7 || hasAggro)) {
            if (any && _slotHits[other->m_slot] != 0) {
                return 1;
            }
            hits += _slotHits[other->m_slot];
        }
    }
    return any ? 0 : hits;
}

FUNC3(Func3_213CDB8) { return TeammateHits(true); }
FUNC3(Func3_213CF94) { return (Flags2 & AiFlags2::Bit21) ? 1 : 0; }
FUNC3(Func3_213CF7C) { return (Flags2 & AiFlags2::Bit21) ? 0 : 1; }
FUNC3(Func3_213CDA4) { return DamageFromHalfturret != 0 ? 1 : 0; }
FUNC3(Func3_213CD74) { return _player->m_altForm && _player->m_halfturret.health == 0 ? 1 : 0; }
FUNC3(Func3_213CD58) { return _player->m_health < _player->m_healthMax / 4 ? 1 : 0; }
FUNC3(Func3_213CD34) { return _player->m_health < _player->m_healthMax / 4 ? 0 : 1; }
FUNC3(Func3_213CD18) { return _player->m_health == _player->m_healthMax ? 1 : 0; }
FUNC3(Func3_213CCF4) { return _player->m_health == _player->m_healthMax ? 0 : 1; }
FUNC3(Func3_213CCD8) { return _player->m_health < param.Param1 ? 1 : 0; }
FUNC3(Func3_213CCBC) { return _player->m_health > param.Param1 ? 1 : 0; }
FUNC3(Func3_213CCB0) { return _player->m_health; }

FUNC3(Func3_213CC94)
{
    if (_player->m_healthMax <= _player->m_health) {
        return 0;
    }
    return _player->m_healthMax - _player->m_health;
}

FUNC3(Func3_213CBE4)
{
    FindEntityRef(54);
    ItemInstance* item = ItemRef(54);
    if (item == nullptr) {
        return 0;
    }
    const float dist = param.Param1 / 4096.0f;
    return distanceSquared(item->position, _player->m_position) < dist * dist ? 1 : 0;
}

FUNC3(Func3_213CBC0) { return Func3_213CBE4(context, param) ^ 1; }

FUNC3(Func3_213CBB0)
{
    if (Func2137860()) {
        return 0;
    }
    for (ItemInstance* item : ItemInstances()) {
        if (!IsItemNotNeeded(item->type) && !Func21377FC(item)) {
            return 1;
        }
    }
    return 0;
}

FUNC3(Func3_213CB8C) { return Func3_213CBB0(context, param) ^ 1; }

FUNC3(Func3_213CADC)
{
    FindEntityRef(55);
    ItemInstance* item = ItemRef(55);
    if (item == nullptr) {
        return 0;
    }
    const float dist = param.Param1 / 4096.0f;
    return distanceSquared(item->position, _player->m_position) < dist * dist ? 1 : 0;
}

FUNC3(Func3_213CAA8)
{
    FindEntityRef(55);
    return ItemRef(55) != nullptr ? 1 : 0;
}

FUNC3(Func3_213CA84) { return Func3_213CAA8(context, param) ^ 1; }
FUNC3(Func3_213CA70) { return Field118 >= 151 * 2 ? 1 : 0; }
FUNC3(Func3_213CA58) { return Field118 > param.Param1 * 2 ? 1 : 0; }
FUNC3(Func3_213CA2C) { return _executionTree[context.Depth + 1].CallCount > param.Param1 * 2 ? 1 : 0; }
FUNC3(Func3_213CA00) { return Func3_213CA2C(context, param); }
FUNC3(Func3_213C9D4) { return Func3_213CA2C(context, param); }
FUNC3(Func3_213C9C4) { return _slotHits[_player->m_slot]; }
FUNC3(Func3_213C89C) { return TeammateHits(false); }
FUNC3(Func3_213C88C) { return _slotDamage[_player->m_slot]; }
FUNC3(Func3_213C764) { return Func3_213C89C(context, param); }
FUNC3(Func3_213C75C) { return static_cast<int>(DamageFromHalfturret); }

FUNC3(Func3_213C698)
{
    // single player encounter thresholds
    switch (_player->m_hunter) {
    case Hunter::Spire: return _player->m_health < 500 ? 1 : 0;
    case Hunter::Weavel: return _player->m_health < 110 ? 1 : 0;
    case Hunter::Sylux: return _player->m_health < 700 ? 1 : 0;
    case Hunter::Trace: return _player->m_health < 590 ? 1 : 0;
    default: return 0;
    }
}

FUNC3(Func3_213C64C)
{
    if (_player->m_hunter == Hunter::Sylux) {
        return 100000 * _slotDamage[_player->m_slot] / 25;
    }
    return 0;
}

FUNC3(Func3_213C600)
{
    return _player->m_hunter == Hunter::Sylux && _executionTree[context.Depth + 1].CallCount > 120 * 2 ? 1 : 0;
}

FUNC3(Func3_213C52C)
{
    for (const auto& slot : _world.m_slots) {
        Player* other = slot.player.get();
        if (other != _player && other->m_teamIndex != _player->m_teamIndex && other->m_health != 0 && other->m_hunter == Hunter::Weavel
            && other->m_halfturret.health != 0) {
            return AggroFunc2148394(5, 7, 1, nullptr, nullptr) > 0 ? 1 : 0;
        }
    }
    return 0;
}

bool PlayerAi::Func3_2139A1C(const Vec3& targetPos, float angleCos, float maxDistSqr)
{
    Vec3 toTarget = targetPos - _player->m_position;
    const float distSqr = lengthSquared(toTarget);
    if (maxDistSqr > 0 && distSqr >= maxDistSqr) {
        return false;
    }
    toTarget = normalized(toTarget);
    const float d = dot(_player->facingVector(), toTarget);
    return d > angleCos || (d > 0 && distSqr < 1);
}

FUNC3(Func3_213C48C)
{
    if (Flags2 & AiFlags2::TargetPlayer) {
        // (the C# adds AltColYPos unconverted from fixed point here)
        if (Func3_2139A1C(TargetPosition(_targetPlayer), 0.5f, 15 * 15)) {
            return 1;
        }
    }
    return 0;
}

FUNC3(Func3_213C470) { return Func3_213C48C(context, param) ^ 1; }

FUNC3(Func3_213C334)
{
    if (_findWeaponIndex >= 0 && _findWeaponIndex <= 8) {
        return _player->m_availableWeapons[GetBeamType(_findWeaponIndex)] ? 1 : 0;
    }
    return 0;
}

FUNC3(Func3_213C310) { return Func3_213C334(context, param) ^ 1; }

FUNC3(Func3_213C0D0)
{
    // like Func1_2149824, but whether there is one
    static constexpr int have[9] = {-1, 57, 59, 60, 63, 65, 67, 69, 71};
    static constexpr int missing[9] = {-1, 56, 58, 61, 62, 64, 66, 68, 70};
    if (_findWeaponIndex < 1 || _findWeaponIndex > 8) {
        return 0;
    }
    const int type = _player->m_availableWeapons[GetBeamType(_findWeaponIndex)] ? have[_findWeaponIndex] : missing[_findWeaponIndex];
    FindEntityRef(type);
    return ItemRef(type) != nullptr ? 1 : 0;
}

int PlayerAi::AmmoBelowCost(int weapon) const
{
    const WeaponInfo& info = weaponsMP()[GetBeamType(weapon)];
    return _player->m_ammo[info.ammoType] < info.ammoCost ? 1 : 0;
}

FUNC3(Func3_213C078) { return AmmoBelowCost(_weapon1); }
FUNC3(Func3_213C054) { return AmmoBelowCost(_weapon1) ^ 1; }
FUNC3(Func3_213BFFC) { return AmmoBelowCost(_weapon2); }
FUNC3(Func3_213BFD8) { return AmmoBelowCost(_weapon2) ^ 1; }

FUNC3(Func3_213BED8)
{
    static constexpr int types[9] = {-1, 57, 59, 61, 63, 65, 67, 69, 71};
    if (_weapon1 < 1 || _weapon1 > 8) {
        return 0;
    }
    FindEntityRef(types[_weapon1]);
    return ItemRef(types[_weapon1]) != nullptr ? 1 : 0;
}

FUNC3(Func3_213BEBC) { return _player->m_availableWeapons[Beam::Missile] ? 1 : 0; }
FUNC3(Func3_213BEA0) { return _player->m_availableWeapons[Beam::Missile] ? 0 : 1; }

FUNC3(Func3_213BE48)
{
    FindEntityRef(56);
    return ItemRef(56) != nullptr && Func3_213BEA0(context, param) == 1 ? 1 : 0;
}

FUNC3(Func3_213BE10) { return AmmoBelowCost(1); }
FUNC3(Func3_213BDF4) { return AmmoBelowCost(1) ^ 1; }

FUNC3(Func3_213BD7C)
{
    FindEntityRef(57);
    return ItemRef(57) != nullptr && Func3_213BE10(context, param) == 1 ? 1 : 0;
}

FUNC3(Func3_213BCE8) { return CanChargeWeapon() ? 1 : 0; }
FUNC3(Func3_213BCC4) { return Func3_213BCE8(context, param) ^ 1; }
FUNC3(Func3_213BCB0) { return _player->m_altAttack ? 1 : 0; }
FUNC3(Func3_213BC8C) { return Func3_213BCB0(context, param) ^ 1; }

// Capture and Bounty: the Octoliths and bases InitializeMain picked.

FUNC3(Func3_213BC70) { return _octolithFlagDC != nullptr && _octolithFlagDC->carrier == _player ? 1 : 0; }
FUNC3(Func3_213BC4C) { return Func3_213BC70(context, param) ^ 1; }

FUNC3(Func3_213BC0C)
{
    Player* carrier = _octolithFlagD4 != nullptr ? _octolithFlagD4->carrier : nullptr;
    return carrier != nullptr && carrier->m_health != 0 && carrier->m_teamIndex != _player->m_teamIndex ? 1 : 0;
}

FUNC3(Func3_213BBE8) { return Func3_213BC0C(context, param) ^ 1; }

FUNC3(Func3_213BBA0)
{
    Player* carrier = _octolithFlagDC != nullptr ? _octolithFlagDC->carrier : nullptr;
    return carrier != nullptr && carrier->m_health != 0 && carrier->m_teamIndex == _player->m_teamIndex && carrier != _player ? 1 : 0;
}

FUNC3(Func3_213BB7C) { return Func3_213BBA0(context, param) ^ 1; }

FUNC3(Func3_213BAF4)
{
    if (_octolithFlagCC == nullptr) {
        return 0;
    }
    const float dist = param.Param1 / 4096.0f;
    return distanceSquared(_octolithFlagCC->position, _player->m_position) < dist * dist ? 1 : 0;
}

FUNC3(Func3_213BAD0) { return Func3_213BAF4(context, param) ^ 1; }

FUNC3(Func3_213BA68)
{
    return _octolithFlagDC != nullptr && distanceSquared(_octolithFlagDC->position, _player->m_position) < 3 * 3 ? 1 : 0;
}

FUNC3(Func3_213BA44) { return Func3_213BA68(context, param) ^ 1; }
FUNC3(Func3_213BA28) { return _octolithFlagD4 != nullptr && _octolithFlagD4->atBase ? 1 : 0; }
FUNC3(Func3_213BA04) { return Func3_213BA28(context, param) ^ 1; }

FUNC3(Func3_213B99C)
{
    return _flagBaseD8 != nullptr && distanceSquared(_flagBaseD8->position, _player->m_position) < 3 * 3 ? 1 : 0;
}

FUNC3(Func3_213B978) { return Func3_213B99C(context, param) ^ 1; }

FUNC3(Func3_213B8B0)
{
    if (_flagBaseD8 == nullptr) {
        return 0;
    }
    for (const auto& slot : _world.m_slots) {
        Player* other = slot.player.get();
        if (other != _player && other->m_teamIndex == _player->m_teamIndex
            && distanceSquared(_flagBaseD8->position, other->m_position) < 3 * 3) {
            return 1;
        }
    }
    return 0;
}

FUNC3(Func3_213B88C) { return Func3_213B8B0(context, param) ^ 1; }

FUNC3(Func3_213B7A0)
{
    if (_octolithFlagD4 == nullptr || _flagBaseD8 == nullptr || _octolithFlagD4->carrier == nullptr) {
        return 0;
    }
    for (const auto& slot : _world.m_slots) {
        Player* other = slot.player.get();
        if (other != _player && other->m_teamIndex != _octolithFlagD4->carrier->m_teamIndex
            && distanceSquared(_flagBaseD8->position, other->m_position) < 3 * 3) {
            return 1;
        }
    }
    return 0;
}

FUNC3(Func3_213B77C) { return Func3_213B7A0(context, param) ^ 1; }

FUNC3(Func3_213B690)
{
    if (_world.m_mode == GameMode::Capture) {
        return 0;
    }
    if (_octolithFlagD4 == nullptr || _octolithFlagDC == nullptr || _octolithFlagD4->carrier == nullptr) {
        return 0;
    }
    return distanceSquared(_octolithFlagDC->basePosition, _player->m_position)
            < distanceSquared(_octolithFlagD4->carrier->m_position, _player->m_position)
        ? 1
        : 0;
}

FUNC3(Func3_213B5DC)
{
    if (_octolithFlagDC == nullptr || _flagBaseD8 == nullptr) {
        return 0;
    }
    return distanceSquared(_octolithFlagDC->position, _player->m_position) < distanceSquared(_flagBaseD8->position, _player->m_position)
        ? 1
        : 0;
}

FUNC3(Func3_213B528)
{
    if (_octolithFlagDC == nullptr || _flagBaseD8 == nullptr) {
        return 0;
    }
    return distanceSquared(_flagBaseD8->position, _player->m_position) < distanceSquared(_octolithFlagDC->position, _player->m_position)
        ? 1
        : 0;
}

// Nodes and Defender. Where the C# tests "defense.OccupiedBy != null" --
// always true, OccupiedBy being an array -- the port asks whether anybody
// is in the node, which is what the game's field held (see
// FindClosestFriendlyNodeDefense, which the C# writes with IsOccupied).

bool PlayerAi::DefenseContains(const NodeDefense* defense, const Vec3& point) const { return defense->volume.testPoint(point); }

FUNC3(Func3_213B4E4)
{
    if (!(Flags2 & AiFlags2::TargetDefense)) {
        return 0;
    }
    return DefenseContains(_targetDefense, _player->volumeCenter()) ? 1 : 0;
}

FUNC3(Func3_213B4A0) { return (Flags2 & AiFlags2::TargetDefense) && Func3_213B4E4(context, param) == 0 ? 1 : 0; }

FUNC3(Func3_213B45C)
{
    if (!(Flags2 & AiFlags2::TargetDefense)) {
        return 0;
    }
    return _targetDefense->capturedPlayer != nullptr && _targetDefense->capturedPlayer->m_teamIndex == _player->m_teamIndex ? 1 : 0;
}

FUNC3(Func3_213B3F0)
{
    for (const NodeDefense* defense : NodeDefenses()) {
        if (defense->capturedPlayer != nullptr && defense->capturedPlayer->m_teamIndex == _player->m_teamIndex
            && defense->isOccupied()) {
            return 1;
        }
    }
    return 0;
}

FUNC3(Func3_213B3A0)
{
    if (!(Flags2 & AiFlags2::TargetDefense)) {
        return 0;
    }
    return _targetDefense->capturedPlayer != nullptr && _targetDefense->capturedPlayer->m_teamIndex == _player->m_teamIndex
            && _targetDefense->isOccupied()
        ? 1
        : 0;
}

FUNC3(Func3_213B37C) { return Func3_213B3A0(context, param) ^ 1; }

FUNC3(Func3_213B34C)
{
    if (Flags2 & AiFlags2::TargetDefense) {
        return _targetDefense->contested ? 1 : 0;
    }
    return 0;
}

FUNC3(Func3_213B328) { return Func3_213B34C(context, param) ^ 1; }

FUNC3(Func3_213B284)
{
    if (!(Flags2 & AiFlags2::TargetDefense)) {
        return 0;
    }
    const float dist = param.Param1 / 4096.0f;
    return distanceSquared(_targetDefense->position, _player->m_position) < dist * dist ? 1 : 0;
}

FUNC3(Func3_213B260) { return Func3_213B284(context, param) ^ 1; }

FUNC3(Func3_213B1F0)
{
    for (const NodeDefense* defense : NodeDefenses()) {
        if (defense->capturedPlayer == nullptr || defense->capturedPlayer->m_teamIndex != _player->m_teamIndex
            || defense->isOccupied()) {
            return 0;
        }
    }
    return 1;
}

FUNC3(Func3_213B1D8) { return _weapon2 == 0 ? 1 : 0; }
FUNC3(Func3_213B1C0) { return _weapon2 != 0 ? 1 : 0; }
FUNC3(Func3_213B1A8) { return _weapon2 == 1 ? 1 : 0; }
FUNC3(Func3_213B190) { return _weapon2 != 1 ? 1 : 0; }
FUNC3(Func3_213B178) { return _weapon2 == 2 ? 1 : 0; }
FUNC3(Func3_213B160) { return _weapon2 != 2 ? 1 : 0; }
FUNC3(Func3_213B148) { return _weapon2 == 3 ? 1 : 0; }
FUNC3(Func3_213B130) { return _weapon2 != 3 ? 1 : 0; }
FUNC3(Func3_213B118) { return _weapon2 == 4 ? 1 : 0; }
FUNC3(Func3_213B100) { return _weapon2 != 4 ? 1 : 0; }
FUNC3(Func3_213B0E8) { return _weapon2 == 5 ? 1 : 0; }
FUNC3(Func3_213B0D0) { return _weapon2 != 5 ? 1 : 0; }
FUNC3(Func3_213B0B8) { return _weapon2 == 6 ? 1 : 0; }
FUNC3(Func3_213B0A0) { return _weapon2 != 6 ? 1 : 0; }
FUNC3(Func3_213B088) { return _weapon2 == 7 ? 1 : 0; }
FUNC3(Func3_213B070) { return _weapon2 != 7 ? 1 : 0; }
FUNC3(Func3_213B058) { return _weapon2 == 8 ? 1 : 0; }
FUNC3(Func3_213B040) { return _weapon2 != 8 ? 1 : 0; }
FUNC3(Func3_213B020) { return _player->m_botLevel == 0 ? 1 : 0; }
FUNC3(Func3_213B000) { return _player->m_botLevel != 0 ? 1 : 0; }
FUNC3(Func3_213AFE0) { return _player->m_botLevel == 1 ? 1 : 0; }
FUNC3(Func3_213AFC0) { return _player->m_botLevel != 1 ? 1 : 0; }
FUNC3(Func3_213AFA0) { return _player->m_botLevel >= 2 ? 1 : 0; } // Insane (3) takes Hard's branch
FUNC3(Func3_213AF80) { return _player->m_botLevel < 2 ? 1 : 0; }
FUNC3(Func3_213AF68) { return _player->m_hunter == Hunter::Samus ? 1 : 0; }
FUNC3(Func3_213AF50) { return _player->m_hunter != Hunter::Samus ? 1 : 0; }
FUNC3(Func3_213AF38) { return _player->m_hunter == Hunter::Kanden ? 1 : 0; }
FUNC3(Func3_213AF20) { return _player->m_hunter != Hunter::Kanden ? 1 : 0; }
FUNC3(Func3_213AF08) { return _player->m_hunter == Hunter::Spire ? 1 : 0; }
FUNC3(Func3_213AEF0) { return _player->m_hunter != Hunter::Spire ? 1 : 0; }
FUNC3(Func3_213AED8) { return _player->m_hunter == Hunter::Noxus ? 1 : 0; }
FUNC3(Func3_213AEC0) { return _player->m_hunter != Hunter::Noxus ? 1 : 0; }
FUNC3(Func3_213AEA8) { return _player->m_hunter == Hunter::Trace ? 1 : 0; }
FUNC3(Func3_213AE90) { return _player->m_hunter != Hunter::Trace ? 1 : 0; }
FUNC3(Func3_213AE78) { return _player->m_hunter == Hunter::Sylux ? 1 : 0; }
FUNC3(Func3_213AE60) { return _player->m_hunter != Hunter::Sylux ? 1 : 0; }
FUNC3(Func3_213AE48) { return _player->m_hunter == Hunter::Weavel ? 1 : 0; }
FUNC3(Func3_213AE30) { return _player->m_hunter != Hunter::Weavel ? 1 : 0; }
FUNC3(Func3_213AE14) { return _world.m_mode == GameMode::Capture ? 1 : 0; }
FUNC3(Func3_213ADF8) { return _world.m_mode != GameMode::Capture ? 1 : 0; }
FUNC3(Func3_213ADC4) { return _world.m_mode == GameMode::PrimeHunter && _player->primeHunter ? 1 : 0; }
FUNC3(Func3_213ADA0) { return Func3_213ADC4(context, param) ^ 1; }
FUNC3(Func3_213AD88) { return _field30 == param.Param1 ? 1 : 0; }
FUNC3(Func3_213AD64) { return Func3_213AD88(context, param) ^ 1; }

FUNC3(Func3_213ACE8)
{
    if (_world.m_mode == GameMode::Capture && _player->m_teamIndex == 0 && _field30 > 100) {
        return 1;
    }
    int navIndex = _nodeTypeIndex[static_cast<int>(NodeType::Navigation)];
    const int specIndex = _nodeTypeIndex[static_cast<int>(NodeType::Special)];
    if (navIndex >= specIndex) {
        return 1;
    }
    for (; navIndex < specIndex; navIndex++) {
        if (static_cast<int>((*_nodeList)[navIndex].Field4) == _field30) {
            return 0;
        }
    }
    return 1;
}

FUNC3(Func3_213ACCC) { return _player->m_position[1] < param.Param1 / 4096.0f ? 1 : 0; }
FUNC3(Func3_213ACA8) { return _player->m_position[1] >= param.Param1 / 4096.0f ? 1 : 0; }
FUNC3(Func3_213AC8C) { return _player->m_position[0] > param.Param1 / 4096.0f ? 1 : 0; }
FUNC3(Func3_213AC70) { return _player->m_position[0] < param.Param1 / 4096.0f ? 1 : 0; }
FUNC3(Func3_213AC54) { return _player->m_position[2] > param.Param1 / 4096.0f ? 1 : 0; }
FUNC3(Func3_213AC38) { return _player->m_position[2] < param.Param1 / 4096.0f ? 1 : 0; }

bool PlayerAi::HasTargetPlayer() const { return (Flags2 & AiFlags2::TargetPlayer) && _targetPlayer != nullptr; }

FUNC3(Func3_213AC04) { return HasTargetPlayer() && _targetPlayer->m_position[1] < param.Param1 / 4096.0f ? 1 : 0; }
FUNC3(Func3_213ABC0) { return HasTargetPlayer() && _targetPlayer->m_position[1] >= param.Param1 / 4096.0f ? 1 : 0; }
FUNC3(Func3_213AB8C) { return HasTargetPlayer() && _targetPlayer->m_position[0] > param.Param1 / 4096.0f ? 1 : 0; }
FUNC3(Func3_213AB58) { return HasTargetPlayer() && _targetPlayer->m_position[0] < param.Param1 / 4096.0f ? 1 : 0; }
FUNC3(Func3_213AB24) { return HasTargetPlayer() && _targetPlayer->m_position[2] > param.Param1 / 4096.0f ? 1 : 0; }
FUNC3(Func3_213AAF0) { return HasTargetPlayer() && _targetPlayer->m_position[2] < param.Param1 / 4096.0f ? 1 : 0; }

FUNC3(Func3_213AA64)
{
    if (!(Flags2 & AiFlags2::TargetPlayer)) {
        return 0;
    }
    const float dist = param.Param1 / 4096.0f;
    // (the game's: the distance to the origin)
    return lengthSquared(_targetPlayer->m_position) < dist * dist ? 1 : 0;
}

FUNC3(Func3_213AA20) { return (Flags2 & AiFlags2::TargetPlayer) && Func3_213AA64(context, param) == 0 ? 1 : 0; }

FUNC3(Func3_213A9B8)
{
    if (!(Flags2 & AiFlags2::TargetPlayer)) {
        return 0;
    }
    if (_world.m_radarPlayers || _targetPlayer->radarReveal || _targetPlayer->octolithFlag != nullptr || _targetPlayer->primeHunter) {
        return 31;
    }
    return static_cast<int>(_targetPlayer->m_curAlpha * 31);
}

FUNC3(Func3_213A94C)
{
    if (!(Flags2 & AiFlags2::TargetPlayer)) {
        return 31;
    }
    if (_world.m_radarPlayers || _targetPlayer->radarReveal || _targetPlayer->octolithFlag != nullptr || _targetPlayer->primeHunter) {
        return 0;
    }
    return 31 - static_cast<int>(_targetPlayer->m_curAlpha * 31);
}

FUNC3(Func3_213A938) { return (Flags4 & AiFlags4::Bit2) ? 1 : 0; }
FUNC3(Func3_213A91C) { return (Flags4 & AiFlags4::Bit2) ? 0 : 1; }
FUNC3(Func3_213A900) { return _player->m_deathaltTimer != 0 ? 1 : 0; }
FUNC3(Func3_213A8DC) { return Func3_213A900(context, param) ^ 1; }

FUNC3(Func3_213A8A8)
{
    if (!(Flags2 & AiFlags2::TargetPlayer)) {
        return 0;
    }
    return _targetPlayer->m_deathaltTimer != 0 ? 1 : 0;
}

FUNC3(Func3_213A884) { return Func3_213A8A8(context, param) ^ 1; }
FUNC3(Func3_213A868) { return _player->m_timeSinceGrounded > 30 * 2 ? 1 : 0; }
FUNC3(Func3_213A844) { return Func3_213A868(context, param) ^ 1; }
FUNC3(Func3_213A828) { return _player->m_usedJump ? 0 : 1; }
FUNC3(Func3_213A804) { return Func3_213A828(context, param) ^ 1; }

FUNC3(Func3_213A798)
{
    for (const auto& slot : _world.m_slots) {
        if (slot.player.get() != _player && slot.ai != nullptr && (slot.ai->Flags2 & AiFlags2::Bit18)) {
            return 1;
        }
    }
    return 0;
}

FUNC3(Func3_213A72C)
{
    for (const auto& slot : _world.m_slots) {
        if (slot.player.get() != _player && slot.ai != nullptr && (slot.ai->Flags2 & AiFlags2::Bit19)) {
            return 1;
        }
    }
    return 0;
}

FUNC3(Func3_213A714) { return _player->m_horizColTimer > 10 * 2 ? 1 : 0; }

FUNC3(Func3_213A698)
{
    // Alinos Gateway and Council Chamber: moving on their lava is fine
    const int room = _world.roomId();
    if ((room == 114 || room == 112) && !isZero(_player->m_speed) && !_player->m_altForm) {
        return 0;
    }
    return _player->m_standTerrain == kTerrainLava ? 1 : 0;
}

FUNC3(Func3_213A688) { return (Flags2 & AiFlags2::AiStart) ? 1 : 0; }

FUNC3(Func3_213A660)
{
    return param.Param1 + static_cast<int>(randomInt2(static_cast<uint32_t>(param.Param2 - param.Param1)));
}

FUNC3(Func3_213A650) { return (Flags2 & AiFlags2::Bit13) ? 1 : 0; }

#undef FUNC3

} // namespace fp
