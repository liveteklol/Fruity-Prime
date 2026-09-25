// PlayerAiData: finding nodes, opponents, items, and the tree node's settings.
#include "PlayerAi.h"

#include "PlayerAiUtil.h"
#include "World.h"

#include <cassert>
#include <limits>

#pragma GCC diagnostic ignored "-Wunused-parameter"

namespace fp {

using namespace ai;

// ---- refs -------------------------------------------------------------------------

Player* PlayerAi::PlayerRef(int type) const { return _entityRefs.Players[type - 26]; }
ItemSpawn* PlayerAi::ItemSpawnRef(int type) const { return _entityRefs.ItemSpawns[type - 34]; }
ItemInstance* PlayerAi::ItemRef(int type) const { return _entityRefs.Items[type - 54]; }

std::vector<ItemInstance*> PlayerAi::ItemInstances() const
{
    // Scene.GetItemInstanceEntities: the spawners' items, then dropped ammo.
    std::vector<ItemInstance*> items;
    for (ItemSpawn& spawn : _world.m_items) {
        if (spawn.item != nullptr) {
            items.push_back(spawn.item);
        }
    }
    for (ItemInstance& item : _world.m_droppedItems) {
        if (item.active()) {
            items.push_back(&item);
        }
    }
    return items;
}

std::vector<const PlayerAi::NodeDefense*> PlayerAi::NodeDefenses() const
{
    std::vector<const NodeDefense*> defenses;
    for (const NodeDefense& defense : _world.m_nodeDefenses) {
        defenses.push_back(&defense);
    }
    return defenses;
}

// ---- the tree node's settings --------------------------------------------------------

void PlayerAi::Func214715C(AiContext& context)
{
    // What the funcs_2/4 id asks for: where to go (Field4, Field9), how to aim
    // (FieldA/B), shoot (FieldC), form (FieldD) and alt attacks (FieldF).
    context.Field4 = context.Field5 = context.Field6 = context.Field7 = context.Field8 = context.Field9 = 0;
    context.FieldA = context.FieldB = context.FieldC = 0;
    context.FieldD = 28;
    context.FieldE = context.FieldF = 0;
#include "PlayerAiContextSwitch.inc"
    if (_player->m_botLevel == 0 && context.Field5 == 47) {
        context.Field5 = 0;
    }
    if (context.Field4 != 37) {
        Flags2 &= ~AiFlags2::Bit7;
    }
}

// ---- small target helpers ---------------------------------------------------------------

void PlayerAi::Func2135510()
{
    FindEntityRef(26);
    Func21356C0(PlayerRef(26));
}

void PlayerAi::Func21354E0()
{
    FindEntityRef(28);
    Func21356C0(PlayerRef(28));
}

void PlayerAi::Func2135540()
{
    FindEntityRef(27);
    Func21356C0(PlayerRef(27));
}

void PlayerAi::Func21354B0()
{
    FindEntityRef(30);
    Func21356C0(PlayerRef(30));
}

void PlayerAi::Func2135380()
{
    FindEntityRef(31);
    Func21356C0(PlayerRef(31));
}

void PlayerAi::Func2135480()
{
    FindEntityRef(32);
    Func21356C0(PlayerRef(32));
}

void PlayerAi::Func2135320()
{
    FindEntityRef(74);
    Func2135624(_entityRefs.Defenses[0]);
}

void PlayerAi::Func21355D8()
{
    FindEntityRef(77);
    if (_entityRefs.Field77 != nullptr) {
        Func2135608(_entityRefs.Field77);
    }
}

void PlayerAi::UpdateTargetItem(ItemInstance* item)
{
    if (item != nullptr && item->despawnTimer != 0) {
        Flags2 |= AiFlags2::TargetItem;
    } else {
        Flags2 &= ~AiFlags2::TargetItem;
    }
    _itemC8 = item;
}

// ---- FindEntityRef ---------------------------------------------------------------------

const NodeData3* PlayerAi::ClosestNodeForPlayer(Player* player, bool nonHazard)
{
    // a player's closest node, from its feet, cached on the player for the tick
    if (player->closestNode != nullptr) {
        return player->closestNode;
    }
    const Vec3 position = addY(player->m_position, player->m_altForm ? -(fx(player->v.AltColRadius) - fx(player->v.AltColYPos)) : -0.5f);
    const NodeData3* node = nonHazard ? FindClosestNonHazardNodeToPosition(position) : FindClosestNodeToPosition(position);
    if (_nodeData->Simple()) {
        player->closestNode = node;
    }
    return node;
}

void PlayerAi::FindEntityRef(int type)
{
    if (type < 0 || type > 77 || _entityRefs.IsPopulated(type)) {
        return;
    }
    Player* p = _player;
    auto& nodes = _entityRefs.Nodes;
    auto fallback = [&](int field) {
        FindEntityRef(0);
        nodes[field] = nodes[0];
    };
    switch (type) {
    case 0: nodes[0] = ClosestNodeForPlayer(p, false); break;
    case 1:
        if (Flags2 & AiFlags2::Bit10) {
            fallback(1);
            return;
        }
        nodes[1] = Func2138D28(addY(p->m_position, p->m_altForm ? -(fx(p->v.AltColRadius) - fx(p->v.AltColYPos)) : -0.5f));
        break;
    case 2:
        if (!(Flags2 & AiFlags2::TargetPlayer) || _targetPlayer == nullptr) {
            fallback(2);
            return;
        }
        nodes[2] = ClosestNodeForPlayer(_targetPlayer, true);
        break;
    case 3:
        if (!(Flags2 & AiFlags2::TargetHalfturret) || _targetHalfturret == nullptr) {
            fallback(3);
            return;
        }
        if (_targetHalfturret->m_halfturret.closestNode != nullptr) {
            nodes[3] = _targetHalfturret->m_halfturret.closestNode;
        } else {
            nodes[3] = FindClosestNonHazardNodeToPosition(_targetHalfturret->m_halfturret.position);
            if (_nodeData->Simple()) {
                _targetHalfturret->m_halfturret.closestNode = nodes[3];
            }
        }
        break;
    case 4:
        if (_itemSpawnC4 == nullptr) {
            fallback(4);
            return;
        }
        // (the game's: item spawns never get a closest node in scene setup)
        nodes[4] = _nodeData->Simple() ? nullptr : FindClosestNonHazardNodeToPosition(_itemSpawnC4->position);
        break;
    case 5:
        if (!(Flags2 & AiFlags2::TargetItem) || _itemC8 == nullptr) {
            fallback(5);
            return;
        }
        if (_itemC8->closestNode != nullptr) {
            nodes[5] = _itemC8->closestNode;
        } else {
            nodes[5] = FindClosestNonHazardNodeToPosition(_itemC8->position);
            if (_nodeData->Simple()) {
                _itemC8->closestNode = nodes[5];
            }
        }
        break;
    // Capture and Bounty: the Octoliths, where they sit at home, and the bases.
    case 6:
        FindEntityRef(_octolithFlagCC == _octolithFlagD4 ? 9 : 12);
        nodes[6] = nodes[_octolithFlagCC == _octolithFlagD4 ? 9 : 12];
        break;
    case 7:
        FindEntityRef(_octolithFlagCC == _octolithFlagD4 ? 10 : 13);
        nodes[7] = nodes[_octolithFlagCC == _octolithFlagD4 ? 10 : 13];
        break;
    case 8:
        FindEntityRef(_flagBaseD0 == _flagBaseD8 ? 11 : 14);
        nodes[8] = nodes[_flagBaseD0 == _flagBaseD8 ? 11 : 14];
        break;
    case 9:
    case 12: {
        OctolithFlag* flag = type == 9 ? _octolithFlagD4 : _octolithFlagDC;
        if (flag == nullptr) {
            break;
        }
        if (flag->closestNode != nullptr) {
            nodes[type] = flag->closestNode;
        } else {
            nodes[type] = FindClosestNonHazardNodeToPosition(flag->position);
            if (_nodeData->Simple()) {
                flag->closestNode = nodes[type];
            }
        }
        break;
    }
    case 10:
    case 13: {
        const OctolithFlag* flag = type == 10 ? _octolithFlagD4 : _octolithFlagDC;
        if (flag != nullptr) {
            nodes[type] = _nodeData->Simple() ? flag->baseClosestNode : FindClosestNonHazardNodeToPosition(flag->basePosition);
        }
        break;
    }
    case 11:
    case 14: {
        const FlagBase* base = type == 11 ? _flagBaseD8 : _flagBaseE0;
        if (base != nullptr) {
            nodes[type] = _nodeData->Simple() ? base->closestNode : FindClosestNonHazardNodeToPosition(base->position);
        }
        break;
    }
    case 15:
        if (!(Flags2 & AiFlags2::TargetDefense) || _targetDefense == nullptr) {
            fallback(15);
            return;
        }
        nodes[15] = _nodeData->Simple() ? _targetDefense->closestNode : FindClosestNonHazardNodeToPosition(_targetDefense->position);
        break;
    case 16: nodes[16] = FindFarthestNodeFromPosition(p->m_position); break;
    case 17:
        if (_targetPlayer != nullptr) {
            nodes[17] = FindFarthestNodeFromPosition(_targetPlayer->m_position);
        }
        break;
    case 18:
        if (_targetPlayer != nullptr) {
            nodes[18] = Func21396A0(_targetPlayer->m_position);
        }
        break;
    case 19: nodes[19] = FindHighestNode(); break;
    case 20: nodes[20] = FindClosestVantageNodeToPosition(p->m_position); break;
    case 21: nodes[21] = FindClosestVantageNodeToPositionWithRange(p->m_position); break;
    case 22: nodes[22] = FindFarthestVantageNodeFromPosition(p->m_position); break;
    case 23: nodes[23] = GetRandomAerialNode(); break;
    case 24: nodes[24] = GetRandomNavigationNodeByField4(); break;
    case 25: nodes[25] = FindClosestNavigationNodeByField4(p->m_position); break;
    case 26: _entityRefs.Players[0] = FindClosestOpponentToPosition(p->m_position, false); break;
    case 27: _entityRefs.Players[1] = FindClosestOpponentToPosition(p->m_position, true); break;
    case 28: _entityRefs.Players[2] = Func2138038(p->m_position); break;
    case 29:
        // (for when the target is dead, else it finds the same one)
        if (_targetPlayer != nullptr) {
            _entityRefs.Players[3] = FindClosestOpponentToPosition(_targetPlayer->m_position, false);
        }
        break;
    case 30: _entityRefs.Players[4] = Func2137E8C(p->m_position); break;
    case 31: _entityRefs.Players[5] = Func21378C0(p->m_position); break;
    case 32:
        if (Player* result = Func2137AA4(p->m_position)) {
            _entityRefs.Players[6] = result;
        }
        break;
    case 33: _entityRefs.Field33 = Func2137D08(p->m_position); break;
    case 34: _entityRefs.ItemSpawns[0] = FindClosestPopulatedItemSpawnToPosition(p->m_position, true); break;
    case 35:
        _entityRefs.ItemSpawns[1]
            = FindClosestPopulatedItemSpawnOfTypeToPosition(p->m_position, ItemType::HealthMedium, ItemType::HealthSmall, ItemType::HealthBig);
        break;
    case 52: _entityRefs.ItemSpawns[18] = FindClosestPopulatedItemSpawnOfTypeToPosition(p->m_position, ItemType::DoubleDamage); break;
    case 53: _entityRefs.ItemSpawns[19] = FindClosestPopulatedItemSpawnOfTypeToPosition(p->m_position, ItemType::Cloak); break;
    case 54: _entityRefs.Items[0] = FindClosestItemToPosition(p->m_position, true); break;
    case 55:
        _entityRefs.Items[1] = FindClosestItemOfTypeToPosition(p->m_position, ItemType::HealthMedium, ItemType::HealthSmall, ItemType::HealthBig);
        break;
    case 72: _entityRefs.Items[18] = FindClosestItemOfTypeToPosition(p->m_position, ItemType::DoubleDamage); break;
    case 73: _entityRefs.Items[19] = FindClosestItemOfTypeToPosition(p->m_position, ItemType::Cloak); break;
    case 74: _entityRefs.Defenses[0] = FindClosestNodeDefense(p->m_position); break;
    case 75: _entityRefs.Defenses[1] = ChooseNodeDefenseToRetake(); break;
    case 76: _entityRefs.Defenses[2] = FindClosestFriendlyNodeDefense(); break;
    case 77: _entityRefs.Field77 = FindClosestDoor(p->m_position); break;
    default:
        if (type >= 36 && type <= 51) {
            // weapon (even) / its ammo (odd) spawns: 36/37 missiles, 38/39 Volt Driver...
            static constexpr ItemType weapons[8] = {ItemType::MissileSmall, ItemType::VoltDriver, ItemType::Battlehammer, ItemType::Imperialist,
                ItemType::Judicator, ItemType::Magmaul, ItemType::ShockCoil, ItemType::OmegaCannon};
            static constexpr int beams[8] = {Beam::Missile, Beam::VoltDriver, Beam::Battlehammer, Beam::Imperialist, Beam::Judicator,
                Beam::Magmaul, Beam::ShockCoil, Beam::OmegaCannon};
            const int i = (type - 36) / 2;
            ItemSpawn*& out = _entityRefs.ItemSpawns[type - 34];
            if (i == 0) {
                out = FindItemSpawnForMissiles();
            } else if ((type & 1) == 0 || i == 7) {
                out = FindItemSpawnForWeapon(weapons[i], beams[i]);
            } else {
                out = FindItemSpawnForUa();
            }
        } else if (type >= 56 && type <= 71) {
            static constexpr ItemType weapons[8] = {ItemType::MissileSmall, ItemType::VoltDriver, ItemType::Battlehammer, ItemType::Imperialist,
                ItemType::Judicator, ItemType::Magmaul, ItemType::ShockCoil, ItemType::OmegaCannon};
            static constexpr int beams[8] = {Beam::Missile, Beam::VoltDriver, Beam::Battlehammer, Beam::Imperialist, Beam::Judicator,
                Beam::Magmaul, Beam::ShockCoil, Beam::OmegaCannon};
            const int i = (type - 56) / 2;
            ItemInstance*& out = _entityRefs.Items[type - 54];
            if (i == 0) {
                out = FindItemForMissiles();
            } else if ((type & 1) == 0 || i == 7) {
                out = FindItemForWeapon(weapons[i], beams[i]);
            } else {
                out = FindItemForUa();
            }
        }
        break;
    }
}

void PlayerAi::FindQueuedEntityRef()
{
    const int action = _queuedFindEntityAction;
    auto node3CFrom = [&](int type) {
        FindEntityRef(type);
        _node3C = _entityRefs.Nodes[type];
    };
    auto itemOrWander = [&](ItemInstance* item) {
        UpdateTargetItem(item);
        if (Flags2 & AiFlags2::TargetItem) {
            node3CFrom(5);
        } else {
            node3CFrom(0);
        }
    };
    switch (action) {
    case 0: node3CFrom((Flags2 & AiFlags2::TargetPlayer) ? 2 : 0); break;
    case 1:
        Func2135510();
        node3CFrom(2);
        break;
    case 2:
        Func21354E0();
        node3CFrom(2);
        break;
    case 3:
        Func2135510();
        node3CFrom(17);
        break;
    case 4: node3CFrom((Flags2 & AiFlags2::TargetPlayer) ? 18 : 0); break;
    case 5:
        Func21354B0();
        node3CFrom(2);
        break;
    case 6:
        Func2135380();
        node3CFrom(2);
        break;
    case 7:
        Func21354B0();
        node3CFrom(17);
        break;
    case 8: node3CFrom((Flags2 & AiFlags2::TargetPlayer) ? 3 : 0); break;
    case 9: node3CFrom(19); break;
    case 10:
        FindEntityRef(54);
        UpdateTargetItem(ItemRef(54));
        if (Flags2 & AiFlags2::TargetItem) {
            node3CFrom(5);
        }
        break;
    case 11:
        FindEntityRef(34);
        if (_itemSpawnC4 != ItemSpawnRef(34)) {
            _itemSpawnC4 = ItemSpawnRef(34);
            if (_itemSpawnC4 != nullptr && _itemSpawnC4->item != nullptr) {
                node3CFrom(4);
            }
        }
        break;
    case 12:
        FindEntityRef(55);
        UpdateTargetItem(ItemRef(55));
        if (Flags2 & AiFlags2::TargetItem) {
            node3CFrom(5);
        } else if (_player->m_botLevel >= 3) {
            // Insane: head for where health will come back
            FindEntityRef(35);
            _itemSpawnC4 = ItemSpawnRef(35);
            node3CFrom(_itemSpawnC4 != nullptr ? 4 : 0);
        } else {
            node3CFrom(0);
        }
        break;
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
    case 18: {
        const int type = 56 + (action - 13);
        FindEntityRef(type);
        itemOrWander(ItemRef(type));
        break;
    }
    case 19:
    case 20:
    case 21:
    case 22:
    case 23:
    case 24:
    case 25:
    case 26: {
        // (never used by the game, which passes the spawn itself)
        const int type = 42 + (action - 19);
        FindEntityRef(type);
        itemOrWander(ItemSpawnRef(type) != nullptr ? ItemSpawnRef(type)->item : nullptr);
        break;
    }
    case 27:
    case 28: node3CFrom(6); break;
    case 29: node3CFrom(8); break;
    case 30:
    case 31: node3CFrom(9); break;
    case 32: node3CFrom(11); break;
    case 33:
    case 34: node3CFrom(12); break;
    case 35: node3CFrom(14); break;
    case 36: node3CFrom(15); break;
    case 37: node3CFrom(20); break;
    case 38: node3CFrom(21); break;
    case 39: node3CFrom(22); break;
    default: break;
    }
}

// ---- items -------------------------------------------------------------------------------

ItemSpawn* PlayerAi::FindItemSpawnForWeapon(ItemType itemType, int beam)
{
    const ItemType type2 = affinityWeapons()[static_cast<int>(_player->m_hunter)] == beam ? ItemType::AffinityWeapon : kItemNone;
    return FindClosestPopulatedItemSpawnOfTypeToPosition(_player->m_position, itemType, type2);
}

ItemInstance* PlayerAi::FindItemForWeapon(ItemType itemType, int beam)
{
    const ItemType type2 = affinityWeapons()[static_cast<int>(_player->m_hunter)] == beam ? ItemType::AffinityWeapon : kItemNone;
    return FindClosestItemOfTypeToPosition(_player->m_position, itemType, type2);
}

ItemSpawn* PlayerAi::FindItemSpawnForUa()
{
    return FindClosestPopulatedItemSpawnOfTypeToPosition(_player->m_position, ItemType::UASmall, ItemType::UABig);
}

ItemInstance* PlayerAi::FindItemForUa() { return FindClosestItemOfTypeToPosition(_player->m_position, ItemType::UASmall, ItemType::UABig); }

ItemSpawn* PlayerAi::FindItemSpawnForMissiles()
{
    const ItemType type3 = affinityWeapons()[static_cast<int>(_player->m_hunter)] == Beam::Missile ? ItemType::AffinityWeapon : kItemNone;
    return FindClosestPopulatedItemSpawnOfTypeToPosition(_player->m_position, ItemType::MissileSmall, ItemType::MissileBig, type3);
}

ItemInstance* PlayerAi::FindItemForMissiles()
{
    const ItemType type3 = affinityWeapons()[static_cast<int>(_player->m_hunter)] == Beam::Missile ? ItemType::AffinityWeapon : kItemNone;
    return FindClosestItemOfTypeToPosition(_player->m_position, ItemType::MissileSmall, ItemType::MissileBig, type3);
}

ItemSpawn* PlayerAi::FindClosestPopulatedItemSpawnToPosition(const Vec3& position, bool checkNeeded)
{
    // spawns whose item is there first, then the closest
    ItemSpawn* result = nullptr;
    float minDist = std::numeric_limits<float>::max();
    for (ItemSpawn& itemSpawn : _world.m_items) {
        if (result == nullptr) {
            result = &itemSpawn;
            if (Func2137860()) {
                return result;
            }
            continue;
        }
        if ((!checkNeeded || !IsItemNotNeeded(itemSpawn.type))
            && (_world.m_mode != GameMode::PrimeHunter || _world.m_primeHunter != _player->slot() || !IsHealth(itemSpawn.type))
            && (result->item == nullptr || itemSpawn.item != nullptr)
            && (itemSpawn.item == nullptr || !Func21377FC(itemSpawn.item))) {
            const float dist = distanceSquared(itemSpawn.position, position);
            if ((result->item == nullptr && itemSpawn.item != nullptr) || dist < minDist) {
                result = &itemSpawn;
                minDist = dist;
            }
        }
    }
    return result;
}

ItemSpawn* PlayerAi::FindClosestPopulatedItemSpawnOfTypeToPosition(const Vec3& position, ItemType type1, ItemType type2, ItemType type3)
{
    auto isType = [&](const ItemSpawn& candidate) {
        return candidate.type == type1 || (type2 != kItemNone && candidate.type == type2) || (type3 != kItemNone && candidate.type == type3);
    };
    ItemSpawn* result = nullptr;
    float minDist = std::numeric_limits<float>::max();
    for (ItemSpawn& itemSpawn : _world.m_items) {
        if (result == nullptr) {
            result = &itemSpawn;
            if (Func2137860()) {
                return result;
            }
            continue;
        }
        if (!isType(itemSpawn) || (itemSpawn.item != nullptr && Func21377FC(itemSpawn.item))) {
            continue;
        }
        const float dist = distanceSquared(itemSpawn.position, position);
        if ((result->item == nullptr && itemSpawn.item != nullptr) || !isType(*result) || dist < minDist) {
            result = &itemSpawn;
            minDist = dist;
        }
    }
    return result;
}

ItemInstance* PlayerAi::FindClosestItemToPosition(const Vec3& position, bool checkNeeded)
{
    ItemInstance* result = nullptr;
    float minDist = std::numeric_limits<float>::max();
    if (Func2137860()) {
        return result;
    }
    for (ItemInstance* item : ItemInstances()) {
        if ((!checkNeeded || !IsItemNotNeeded(item->type))
            && (_world.m_mode != GameMode::PrimeHunter || _world.m_primeHunter != _player->slot() || !IsHealth(item->type))
            && item->despawnTimer != 0 && !Func21377FC(item)) {
            const float dist = distanceSquared(item->position, position);
            if (dist < minDist) {
                result = item;
                minDist = dist;
            }
        }
    }
    return result;
}

ItemInstance* PlayerAi::FindClosestItemOfTypeToPosition(const Vec3& position, ItemType type1, ItemType type2, ItemType type3)
{
    auto isType = [&](const ItemInstance& candidate) {
        return candidate.type == type1 || (type2 != kItemNone && candidate.type == type2) || (type3 != kItemNone && candidate.type == type3);
    };
    ItemInstance* result = nullptr;
    if (Func2137860()) {
        return result;
    }
    float minDist = std::numeric_limits<float>::max();
    for (ItemInstance* item : ItemInstances()) {
        if (isType(*item) && item->despawnTimer != 0 && !Func21377FC(item)) {
            const float dist = distanceSquared(item->position, position);
            if (dist < minDist) {
                result = item;
                minDist = dist;
            }
        }
    }
    return result;
}

bool PlayerAi::Func2137860() const
{
    // MP1 SANCTORUS (Data Shrine), or MP6 HEADSHOT (Head Shot) carrying flag DC.
    const bool octolithMode = _world.octolithMode();
    const int room = _world.roomId();
    return octolithMode && (room == 93 || (room == 99 && _octolithFlagDC != nullptr && _octolithFlagDC->carrier == _player));
}

bool PlayerAi::IsHealth(ItemType type) const
{
    return type == ItemType::HealthMedium || type == ItemType::HealthSmall || type == ItemType::HealthBig;
}

bool PlayerAi::IsItemNotNeeded(ItemType itemType) const
{
    const Player* p = _player;
    switch (itemType) {
    case ItemType::HealthMedium:
    case ItemType::HealthSmall:
    case ItemType::HealthBig: return p->m_health == p->m_healthMax;
    case ItemType::UASmall:
    case ItemType::UABig: return p->m_ammo[0] == p->m_ammoMax[0];
    case ItemType::MissileSmall:
    case ItemType::MissileBig: return p->m_ammo[1] == p->m_ammoMax[1];
    case ItemType::AffinityWeapon:
        if (p->m_hunter == Hunter::Samus) {
            return p->m_ammo[1] == p->m_ammoMax[1];
        }
        return p->m_availableWeapons[affinityWeapons()[static_cast<int>(p->m_hunter)]];
    case ItemType::VoltDriver:
    case ItemType::Battlehammer:
    case ItemType::Imperialist:
    case ItemType::Judicator:
    case ItemType::Magmaul:
    case ItemType::ShockCoil:
    case ItemType::OmegaCannon: return p->m_availableWeapons[static_cast<int>(itemType) - 4];
    default: return false;
    }
}

bool PlayerAi::Func21377FC(const ItemInstance* item) const
{
    // UNIT 4 ARCTERRA BASE (Arcterra Gateway): one item spawn is off limits to the carrier.
    return _world.octolithMode() && _world.roomId() == 117 && _octolithFlagDC != nullptr && _octolithFlagDC->carrier == _player
        && item->owner != nullptr && item->owner->id == 53;
}

// ---- node defenses and doors -----------------------------------------------------------------

const PlayerAi::NodeDefense* PlayerAi::FindClosestNodeDefense(const Vec3& position) const
{
    const NodeDefense* result = nullptr;
    float minDist = std::numeric_limits<float>::max();
    for (const NodeDefense* defense : NodeDefenses()) {
        const float dist = distanceSquared(defense->position, position);
        if (dist < minDist) {
            result = defense;
            minDist = dist;
        }
    }
    return result;
}

const PlayerAi::NodeDefense* PlayerAi::ChooseNodeDefenseToRetake() const
{
    // With every node the team's, the first node. Otherwise one of the nodes
    // of the opponent holding the most (random among ties, the C# says --
    // though only one slot can match), else a random node nobody holds.
    const NodeDefense* firstResult = nullptr;
    std::array<int, 16> captureList{};
    int maxCaptureCount = 0;
    int maxCaptureSlotIndex = 0;
    for (const NodeDefense& defense : _world.m_nodeDefenses) {
        if (firstResult == nullptr) {
            firstResult = &defense;
        }
        if (defense.capturedPlayer != nullptr && defense.capturedPlayer->m_teamIndex != _player->m_teamIndex) {
            const int captureCount = ++captureList[defense.capturedPlayer->slot() & 15];
            if (captureCount > maxCaptureCount) {
                maxCaptureSlotIndex = defense.capturedPlayer->slot();
                maxCaptureCount = captureCount;
            }
        }
    }
    int resultCount = 0;
    std::array<const NodeDefense*, 10> resultList{};
    if (maxCaptureCount > 0) {
        int playerCount = 0;
        std::array<const Player*, 4> playerList{};
        for (const auto& slot : _world.m_slots) {
            const Player* player = slot.player.get();
            if (player->m_teamIndex != _player->m_teamIndex && player->slot() == maxCaptureSlotIndex && playerCount < 4) {
                playerList[playerCount++] = player;
            }
        }
        const Player* chosenPlayer = playerList[randomInt2(static_cast<uint32_t>(playerCount))];
        for (const NodeDefense& defense : _world.m_nodeDefenses) {
            if (resultCount < 10 && defense.capturedPlayer == chosenPlayer) {
                resultList[resultCount++] = &defense;
            }
        }
    } else {
        for (const NodeDefense& defense : _world.m_nodeDefenses) {
            if (resultCount < 10 && defense.capturedPlayer == nullptr) {
                resultList[resultCount++] = &defense;
            }
        }
    }
    if (resultCount > 0) {
        return resultList[randomInt2(static_cast<uint32_t>(resultCount))];
    }
    return firstResult;
}

const PlayerAi::NodeDefense* PlayerAi::FindClosestFriendlyNodeDefense() const
{
    // The closest node the team holds with somebody in it.
    const NodeDefense* result = nullptr;
    float minDist = std::numeric_limits<float>::max();
    for (const NodeDefense& defense : _world.m_nodeDefenses) {
        if (defense.capturedPlayer != nullptr && defense.capturedPlayer->m_teamIndex == _player->m_teamIndex && defense.isOccupied()) {
            const float dist = distanceSquared(defense.position, _player->m_position);
            if (dist < minDist) {
                result = &defense;
                minDist = dist;
            }
        }
    }
    return result;
}

Door* PlayerAi::FindClosestDoor(const Vec3& position) const
{
    Door* result = nullptr;
    float minDist = std::numeric_limits<float>::max();
    for (Door& door : _world.m_doors) {
        const float dist = distanceSquared(door.position, position);
        if (dist < minDist) {
            result = &door;
            minDist = dist;
        }
    }
    return result;
}

// ---- opponents ------------------------------------------------------------------------------------

Player* PlayerAi::FindClosestOpponentToPosition(const Vec3& position, bool botsOnly)
{
    Player* result = nullptr;
    float minDist = std::numeric_limits<float>::max();
    Flags2 |= AiFlags2::Bit9;
    for (auto& slot : _world.m_slots) {
        Player* player = slot.player.get();
        if (result == nullptr) {
            result = player;
            minDist = distanceSquared(player->m_position, position);
        }
        if (player != _player && player->m_teamIndex != _player->m_teamIndex && player->inPlay() && (!botsOnly || player->m_isBot)) {
            const float dist = distanceSquared(player->m_position, position);
            if (dist <= minDist || result->m_health == 0 || (botsOnly && !result->m_isBot)) {
                result = player;
                minDist = dist;
                Flags2 &= ~AiFlags2::Bit9;
            }
        }
    }
    return result;
}

Player* PlayerAi::Func2138038(const Vec3& position)
{
    // FindClosestOpponentToPosition among those in view
    Player* result = nullptr;
    float minDist = std::numeric_limits<float>::max();
    Flags2 |= AiFlags2::Bit9;
    for (auto& slot : _world.m_slots) {
        Player* player = slot.player.get();
        if (result == nullptr) {
            result = player;
            minDist = distanceSquared(player->m_position, position);
        }
        if (AggroFunc214857C(6, 1, 2, nullptr, player) && player != _player && player->m_teamIndex != _player->m_teamIndex
            && player->inPlay()) {
            const float dist = distanceSquared(player->m_position, position);
            if (dist <= minDist || result->m_health == 0) {
                result = player;
                minDist = dist;
                Flags2 &= ~AiFlags2::Bit9;
            }
        }
    }
    return result;
}

Player* PlayerAi::MostHatedOpponent(const Vec3& position, bool inViewOnly, float initialDist)
{
    // Func2137E8C (everybody) and Func21378C0 (those in view): the most
    // aggro, then the closest
    Player* result = nullptr;
    float minDist = std::numeric_limits<float>::max();
    float dist = initialDist;
    int maxValue = 0;
    Flags2 |= AiFlags2::Bit9;
    for (auto& slot : _world.m_slots) {
        Player* player = slot.player.get();
        if (result == nullptr) {
            result = player;
            minDist = distanceSquared(player->m_position, position);
        }
        if ((!inViewOnly || AggroFunc214857C(6, 1, 2, nullptr, player)) && player != _player && player->m_teamIndex != _player->m_teamIndex
            && player->inPlay()) {
            const int value = AggroFunc2148394(7, 2, 1, player, nullptr);
            if (value > maxValue) {
                result = player;
                minDist = dist; // (the game's: maybe undefined)
                maxValue = value;
                Flags2 &= ~AiFlags2::Bit9;
            } else if (value == maxValue) {
                dist = distanceSquared(player->m_position, position);
                if (dist <= minDist || result->m_health == 0) {
                    result = player;
                    minDist = dist;
                    Flags2 &= ~AiFlags2::Bit9;
                }
            }
        }
    }
    return result;
}

Player* PlayerAi::Func2137E8C(const Vec3& position) { return MostHatedOpponent(position, false, 0); }
Player* PlayerAi::Func21378C0(const Vec3& position) { return MostHatedOpponent(position, true, std::numeric_limits<float>::max()); }

Player* PlayerAi::Func2137AA4(const Vec3& position)
{
    // the opponent most worth attacking: aggro on it and on teammates, nearness, frozen
    Player* result = nullptr;
    bool v4 = false;
    int maxValue = -50000;
    Flags2 |= AiFlags2::Bit9;
    for (auto& slot : _world.m_slots) {
        Player* player = slot.player.get();
        const bool v14 = AggroFunc214857C(6, 1, 2, nullptr, player);
        if ((v14 || !v4) && player != _player && player->m_teamIndex != _player->m_teamIndex && player->inPlay()) {
            int value = AggroFunc2148394(7, 2, 1, player, nullptr);
            for (auto& otherSlot : _world.m_slots) {
                Player* other = otherSlot.player.get();
                if (other != _player && other->m_teamIndex == _player->m_teamIndex && other->inPlay()) {
                    value += AggroFunc2148394(7, 2, 2, player, other);
                }
            }
            const int dist = static_cast<int>(distanceSquared(player->m_position, position));
            if (dist < 400) {
                value += (400 - dist) / 4;
            }
            value += 2 * player->m_frozenTimer / 2;
            if (value > maxValue || (!v4 && v14)) {
                if (v14) {
                    v4 = true;
                }
                result = player;
                maxValue = value;
                Flags2 &= ~AiFlags2::Bit9;
            }
        }
    }
    return result;
}

Player* PlayerAi::Func2137D08(const Vec3& position)
{
    // an opponent's Halfturret to attack (returned by its owner)
    Player* result = nullptr;
    float minDist = 10000;
    float dist = minDist;
    int maxValue = 0;
    for (auto& slot : _world.m_slots) {
        Player* player = slot.player.get();
        if (player != _player && player->m_teamIndex != _player->m_teamIndex && player->inPlay() && player->m_hunter == Hunter::Weavel
            && player->m_halfturret.active && player->m_halfturret.health > 0) {
            int value = AggroFunc2148394(5, 2, 1, player, nullptr);
            if (value == 0) {
                if (player->m_halfturret.target != _player->m_slot) {
                    continue;
                }
                value = 1;
            }
            if (value > maxValue) {
                result = player;
                minDist = dist;
                maxValue = value;
            } else if (value == maxValue) {
                dist = distanceSquared(player->m_position, position);
                if (dist <= minDist) {
                    result = player;
                    minDist = dist;
                }
            }
        }
    }
    return result;
}

// ---- nodes -------------------------------------------------------------------------------------------

const NodeData3* PlayerAi::FindClosestNodeToPosition(const Vec3& position) const
{
    const auto& list = *_nodeList;
    const NodeData3* result = &list[0];
    float minDist = distanceSquared(result->Position, position);
    for (size_t i = 1; i < list.size(); i++) {
        const float dist = distanceSquared(list[i].Position, position);
        if (dist < minDist) {
            result = &list[i];
            minDist = dist;
        }
    }
    return result;
}

const NodeData3* PlayerAi::FindFarthestNodeFromPosition(const Vec3& position) const
{
    const auto& list = *_nodeList;
    const NodeData3* result = &list[0];
    float maxDist = distanceSquared(result->Position, position);
    for (size_t i = 1; i < list.size(); i++) {
        const float dist = distanceSquared(list[i].Position, position);
        if (dist > maxDist) {
            result = &list[i];
            maxDist = dist;
        }
    }
    return result;
}

const NodeData3* PlayerAi::Func2138D28(const Vec3& position)
{
    // The ten nearest nodes, skipping the ones found stuck on (_field7A),
    // closest first in _field4C[1..]; the World checks them one a tick for
    // the first one in sight.
    _field4C[1] = nullptr;
    int v4 = 0;
    float minDist = 100000;
    float distList[10] = {};
    const auto& list = *_nodeList;
    for (size_t i = 0; i < list.size(); i++) {
        int j = 0;
        for (; j < _field78; j++) {
            if (_field7A[j] == i) {
                break;
            }
        }
        if (j < _field78) {
            continue;
        }
        const NodeData3* node = &list[i];
        const float dist = distanceSquared(node->Position, position);
        if (dist < minDist) {
            int k = 0;
            for (; k < v4; k++) {
                if (dist < distList[k]) {
                    break;
                }
            }
            for (int l = v4; l > k; l--) {
                if (l + 1 < static_cast<int>(_field4C.size())) {
                    _field4C[l + 1] = _field4C[l];
                }
                distList[l] = distList[l - 1];
            }
            if (k < 10) {
                _field4C[k + 1] = node;
                distList[k] = dist;
            }
            minDist = distList[v4];
            if (v4 < 9) {
                v4++;
            }
        }
    }
    if (const NodeData3* result = _field4C[1]) {
        Flags2 |= AiFlags2::Bit10;
        Func214B810(v4);
        return result;
    }
    return FindClosestNonHazardNodeToPosition(position);
}

void PlayerAi::Func214B810(int v4)
{
    AiGlobalState& g = _world.m_aiGlobals;
    for (int i = 0; i < g.globalField2; i++) {
        AiGlobalState::Global& obj = g.globalObjs[i];
        if (obj.player == _player) {
            obj.field4 = v4;
            obj.nodeData = &_field4C;
            obj.nodeDataIndex = 1;
            return;
        }
    }
    if (g.globalField2 < static_cast<int>(g.globalObjs.size())) {
        AiGlobalState::Global& next = g.globalObjs[g.globalField2];
        next.player = _player;
        next.field4 = v4;
        next.nodeData = &_field4C;
        next.nodeDataIndex = 1;
        g.globalField2++;
    }
}

const NodeData3* PlayerAi::FindClosestNonHazardNodeToPosition(const Vec3& position) const
{
    const auto& list = *_nodeList;
    const NodeData3* result = &list[0];
    float minDist = distanceSquared(result->Position, position);
    for (size_t i = 1; i < list.size(); i++) {
        const NodeData3& node = list[i];
        if (node.NodeType != NodeType::Hazard) {
            const float dist = distanceSquared(node.Position, position);
            if (dist < minDist || result->NodeType == NodeType::Hazard) {
                result = &node;
                minDist = dist;
            }
        }
    }
    return result;
}

int PlayerAi::Func213A0A4(const NodeData3* head, const NodeData3** nodeList) const
{
    // a node's neighbors: Values at Index1 holds (run length, node index) pairs
    const auto& list = *_nodeList;
    if (list.empty() || head == nullptr) {
        return 0;
    }
    const auto& values = *head->Values;
    int count = 0;
    size_t i = 0;
    int index = 0;
    while (i < list.size()) {
        const size_t at = static_cast<size_t>(head->Index1 + index);
        if (at + 1 >= values.size() || values[at + 1] >= list.size()) {
            break;
        }
        const NodeData3* node = &list[values[at + 1]];
        int j = 0;
        for (; j < count; j++) {
            if (nodeList[j] == node) {
                break;
            }
        }
        if (j == count && node != head) {
            nodeList[count++] = node;
            if (count >= 19) {
                break;
            }
        }
        if (values[at] == 0) {
            break;
        }
        i += values[at];
        index += 2;
    }
    return count;
}

const NodeData3* PlayerAi::Func213A1A8() const
{
    // The next node from _node40 toward _node3C: the node's routing table, a
    // run-length list of (count, next node) by destination id.
    assert(_node3C != nullptr && _node40 != nullptr);
    const auto& values = *_node40->Values;
    const int base = _node40->Index1;
    int index = 0;
    const int maxId = _node3C->Id;
    int valueTotal = 0;
    if (values[base] <= maxId) {
        int value2;
        do {
            valueTotal += values[base + index];
            index += 2;
            value2 = valueTotal + values[base + index];
        } while (value2 <= maxId && base + index + 2 < static_cast<int>(values.size()));
    }
    const int next = values[base + index + 1];
    return next < static_cast<int>(_nodeList->size()) ? &(*_nodeList)[next] : _node40;
}

const NodeData3* PlayerAi::Func21396A0(const Vec3& position) const
{
    // the neighbor of _node40 that most moves away from `position` relative to this bot
    std::array<const NodeData3*, 20> nodeList{};
    const int nodeCount = Func213A0A4(_node40, nodeList.data());
    if (nodeCount == 0) {
        return _node40;
    }
    const NodeData3* result = nodeList[0];
    float maxDist = lengthSquared(position - result->Position) - lengthSquared(_player->m_position - result->Position);
    for (int i = 1; i < nodeCount; i++) {
        const NodeData3* node = nodeList[i];
        const float dist = lengthSquared(position - node->Position) - lengthSquared(_player->m_position - node->Position);
        if (dist > maxDist) {
            result = node;
            maxDist = dist;
        }
    }
    return result;
}

const NodeData3* PlayerAi::FindHighestNode() const
{
    // (the game's loop never finds anything but the first)
    return &(*_nodeList)[0];
}

const NodeData3* PlayerAi::FindClosestVantageNodeToPosition(const Vec3& position) const
{
    const auto& list = *_nodeList;
    const NodeData3* result = &list[0];
    float minDist = distanceSquared(result->Position, position);
    for (size_t i = 1; i < list.size(); i++) {
        const NodeData3& node = list[i];
        if (node.NodeType != NodeType::Vantage && result->NodeType == NodeType::Vantage) {
            continue;
        }
        const float dist = distanceSquared(node.Position, position);
        if (dist < minDist) {
            result = &node;
            minDist = dist;
        }
    }
    return result;
}

const NodeData3* PlayerAi::FindClosestVantageNodeToPositionWithRange(const Vec3& position) const
{
    const auto& list = *_nodeList;
    const NodeData3* result = &list[0];
    float minDist = distanceSquared(result->Position, position);
    bool resultInRange = IsNodeInRange(result);
    for (size_t i = 1; i < list.size(); i++) {
        const NodeData3& node = list[i];
        if (node.NodeType != NodeType::Vantage && result->NodeType == NodeType::Vantage) {
            continue;
        }
        const float dist = distanceSquared(node.Position, position);
        const bool nodeInRange = IsNodeInRange(&node);
        const bool vantage = node.NodeType == NodeType::Vantage, resultVantage = result->NodeType == NodeType::Vantage;
        if ((!resultVantage && vantage) || (dist < minDist && (!nodeInRange || (!resultVantage && !vantage)))
            || (resultVantage && vantage && resultInRange && !nodeInRange)) {
            result = &node;
            resultInRange = nodeInRange;
            minDist = dist;
        }
    }
    return result;
}

const NodeData3* PlayerAi::FindFarthestVantageNodeFromPosition(const Vec3& position) const
{
    const auto& list = *_nodeList;
    const NodeData3* result = &list[0];
    float maxDist = distanceSquared(result->Position, position);
    for (size_t i = 1; i < list.size(); i++) {
        const NodeData3& node = list[i];
        if (node.NodeType != NodeType::Vantage && result->NodeType == NodeType::Vantage) {
            continue;
        }
        const float dist = distanceSquared(node.Position, position);
        if (dist > maxDist) {
            result = &node;
            maxDist = dist;
        }
    }
    return result;
}

bool PlayerAi::IsNodeInRange(const NodeData3* node) const
{
    // standing within the node's radius (in the air only toward an aerial node)
    const Player* p = _player;
    if (p->m_timeSinceJumpPad > 5 * 2 && IsJumpPadNode(node)) {
        return false;
    }
    if (!p->m_grounded && (_node40 == nullptr || _node40->NodeType != NodeType::Aerial)) {
        return false;
    }
    const Vec3 between = addY(node->Position - p->m_position, p->m_altForm ? fx(p->v.AltColRadius) - fx(p->v.AltColYPos) : 0.5f);
    return lengthSquared(between) < node->MaxDistance * node->MaxDistance;
}

bool PlayerAi::IsJumpPadNode(const NodeData3* node) const
{
    for (const JumpPad& pad : _world.m_jumpPads) {
        if (pad.closestNode == node) {
            return true;
        }
    }
    return false;
}

const NodeData3* PlayerAi::GetRandomAerialNode() const
{
    const int aerialIndex = _nodeTypeIndex[static_cast<int>(NodeType::Aerial)];
    const int vantageIndex = _nodeTypeIndex[static_cast<int>(NodeType::Vantage)];
    if (aerialIndex == vantageIndex) {
        return GetRandomNavigationNode();
    }
    return &(*_nodeList)[aerialIndex + static_cast<int>(randomInt2(static_cast<uint32_t>(vantageIndex - aerialIndex)))];
}

const NodeData3* PlayerAi::GetRandomNavigationNode() const
{
    const int count = _nodeTypeIndex[static_cast<int>(NodeType::Navigation)];
    return &(*_nodeList)[static_cast<int>(randomInt2(static_cast<uint32_t>(count)))];
}

const NodeData3* PlayerAi::GetRandomNavigationNodeByField4() const
{
    const auto& list = *_nodeList;
    int navIndex = _nodeTypeIndex[static_cast<int>(NodeType::Navigation)];
    const int specIndex = _nodeTypeIndex[static_cast<int>(NodeType::Special)];
    while (navIndex < specIndex && static_cast<int>(list[navIndex].Field4) != _field30) {
        navIndex++;
    }
    int endIndex = navIndex;
    while (endIndex < specIndex && static_cast<int>(list[endIndex].Field4) == _field30) {
        endIndex++;
    }
    if (endIndex == navIndex) {
        return GetRandomNavigationNode();
    }
    return &list[navIndex + static_cast<int>(randomInt2(static_cast<uint32_t>(endIndex - navIndex)))];
}

const NodeData3* PlayerAi::FindClosestNavigationNodeByField4(const Vec3& position) const
{
    const auto& list = *_nodeList;
    const int navIndex = _nodeTypeIndex[static_cast<int>(NodeType::Navigation)];
    const int specIndex = _nodeTypeIndex[static_cast<int>(NodeType::Special)];
    if (navIndex == specIndex) {
        return FindClosestNonHazardNodeToPosition(position);
    }
    const NodeData3* result = &list[navIndex];
    float minDist = distanceSquared(result->Position, position);
    for (int i = navIndex + 1; i < specIndex; i++) {
        const NodeData3& node = list[i];
        const bool match = static_cast<int>(node.Field4) == _field30, resultMatch = static_cast<int>(result->Field4) == _field30;
        if (!match && resultMatch) {
            break;
        }
        const float dist = distanceSquared(node.Position, position);
        if (dist < minDist || (match && !resultMatch)) {
            result = &node;
            minDist = dist;
        }
    }
    return result;
}

void PlayerAi::UpdateNodeDataSetSelection()
{
    // the node sets switched on and off by the funcs; every bot follows the change
    auto& selector = _nodeData->SetSelector;
    const size_t count = std::min(_nodeData->SetIndices.size(), selector.size());
    for (size_t i = 0; i < count; i++) {
        if (selector[i] && (_nodeDataSelOff & (1 << i))) {
            selector[i] = false;
        } else if (!selector[i] && (_nodeDataSelOn & (1 << i))) {
            selector[i] = true;
        }
    }
    int newIndex = 0;
    for (size_t i = 0; i < count; i++) {
        newIndex += (selector[i] ? 1 : 0) << i;
    }
    if (newIndex >= static_cast<int>(_nodeData->Data.size())) {
        newIndex = 0;
    }
    for (auto& slot : _world.m_slots) {
        if (slot.ai != nullptr && slot.ai->_nodeDataSetIndex != newIndex) {
            slot.ai->_nodeDataSetIndex = newIndex;
            slot.ai->SetClosestNodeList(slot.player->m_position);
        }
    }
}

void PlayerAi::SetClosestNodeList(const Vec3& position)
{
    Flags2 &= ~AiFlags2::Bit7;
    const auto& data1 = _nodeData->Data[_nodeDataSetIndex];
    _nodeList = &data1[0];
    float minDist = GetClosestNodeInList(position, data1[0]);
    for (size_t i = 1; i < data1.size(); i++) {
        const float dist = GetClosestNodeInList(position, data1[i]);
        if (dist < minDist) {
            _nodeList = &data1[i];
            minDist = dist;
        }
    }
    SetNodeTypeFirstIndices();
}

float PlayerAi::GetClosestNodeInList(const Vec3& position, const std::vector<NodeData3>& list)
{
    float minDist = std::numeric_limits<float>::max();
    for (const NodeData3& node : list) {
        minDist = std::min(minDist, distanceSquared(node.Position, position));
    }
    return minDist;
}

void PlayerAi::SetNodeTypeFirstIndices()
{
    // Nodes come sorted by type; each entry ends up where the next type starts.
    int curType = 0;
    const auto& list = *_nodeList;
    for (size_t i = 0; i < list.size(); i++) {
        const int type = static_cast<int>(list[i].NodeType);
        while (curType < type && curType < 6) {
            _nodeTypeIndex[curType] = static_cast<int>(i);
            curType++;
        }
    }
    for (int i = curType; i < 6; i++) {
        _nodeTypeIndex[i] = static_cast<int>(list.size());
    }
}

} // namespace fp
