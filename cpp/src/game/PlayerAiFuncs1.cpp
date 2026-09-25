// PlayerAiData funcs_1: run when a tree node is entered (data3a) and every tick it runs (data3b).
#include "PlayerAi.h"

#include "PlayerAiUtil.h"
#include "World.h"

#include <cassert>

#pragma GCC diagnostic ignored "-Wunused-parameter"

namespace fp {

using namespace ai;

// helpers

bool PlayerAi::CheckBeam(int beam) const
{
    const WeaponInfo& info = weaponsMP()[beam];
    return _player->m_ammo[info.ammoType] >= info.ammoCost && _player->m_availableWeapons[beam];
}

bool PlayerAi::AvailableCharge(int beam) const
{
    // _availableCharges: every weapon picked up but the Omega Cannon.
    return _player->m_availableWeapons[beam] && beam != Beam::OmegaCannon;
}

bool PlayerAi::CheckCharge(int beam) const
{
    const WeaponInfo& info = weaponsMP()[beam];
    return (info.flags & WeaponFlags::CanCharge) && _player->m_ammo[info.ammoType] >= info.chargeCost && AvailableCharge(beam);
}

int PlayerAi::GetBeamType(int weapon)
{
    switch (weapon) {
    case 1: return Beam::Missile;
    case 2: return Beam::VoltDriver;
    default: return weapon;
    }
}

int PlayerAi::GetWeaponIndex(int beam)
{
    switch (beam) {
    case Beam::Missile: return 1;
    case Beam::VoltDriver: return 2;
    default: return beam;
    }
}

bool PlayerAi::CanChargeWeapon() const
{
    const WeaponInfo* weapon = _player->m_equip.weapon;
    if (weapon == nullptr) {
        return false;
    }
    const int ammo = _player->m_ammo[weapon->ammoType];
    return (weapon->flags & WeaponFlags::CanCharge) && AvailableCharge(_player->m_currentWeapon)
        && (ammo >= weapon->chargeCost || ammo == -1);
}

// funcs

void PlayerAi::Func1_214A39C()
{
    _weapon1 = 0;
    Flags4 &= ~AiFlags4::Bit1;
    if (CheckBeam(Beam::Magmaul)) {
        _weapon1 = 6;
    } else if (CheckBeam(Beam::Judicator)) {
        _weapon1 = 5;
    } else if (CheckBeam(Beam::Imperialist)) {
        _weapon1 = 4;
    } else if (CheckBeam(Beam::Battlehammer)) {
        _weapon1 = 3;
    } else if (CheckBeam(Beam::VoltDriver)) {
        _weapon1 = 2;
    } else if (CheckBeam(Beam::ShockCoil)) {
        _weapon1 = 7;
    } else if (CheckBeam(Beam::Missile)) {
        _weapon1 = 1;
    }
    if (CheckCharge(GetBeamType(_weapon1))) {
        Flags4 |= AiFlags4::Bit1;
    }
}

void PlayerAi::Func1_214A098()
{
    // the item spawns of weapons this bot is missing
    const int affinityWeapon = affinityWeapons()[static_cast<int>(_player->m_hunter)];
    const int affinityIndex = GetWeaponIndex(affinityWeapon);
    int seenBits = 1 | 2;
    bool sawAffinity = false;
    int seenCount = 0;
    int seenItems[9] = {};
    for (ItemSpawn& itemSpawn : _world.m_items) {
        if (seenBits == 255) {
            break;
        }
        ItemType type = itemSpawn.type;
        if (type == ItemType::AffinityWeapon) {
            // the game's bug, kept: the beam ID read as a pickup type
            type = static_cast<ItemType>(affinityWeapon);
        }
        int index = 0;
        switch (type) {
        case ItemType::VoltDriver: index = 2; break;
        case ItemType::Battlehammer: index = 3; break;
        case ItemType::Imperialist: index = 4; break;
        case ItemType::Judicator: index = 5; break;
        case ItemType::Magmaul: index = 6; break;
        case ItemType::ShockCoil: index = 7; break;
        case ItemType::OmegaCannon: index = 8; break;
        default: index = 0; break;
        }
        if ((seenBits & (1 << index)) == 0) {
            seenBits |= 1 << index;
            if (!_player->m_availableWeapons[GetBeamType(index)]) {
                seenItems[seenCount++] = index;
                if (index == affinityIndex) {
                    sawAffinity = true;
                }
            }
        }
    }
    const int randIdx = static_cast<int>(randomInt2(static_cast<uint32_t>(seenCount * (sawAffinity ? 2 : 1))));
    _findWeaponIndex = randIdx < seenCount ? seenItems[randIdx] : affinityIndex;
}

void PlayerAi::Func1_2149D3C()
{
    // a beam to switch to
    if (CheckBeam(Beam::OmegaCannon)) {
        _weapon2 = 8;
        return;
    }
    const int affinity = affinityWeapons()[static_cast<int>(_player->m_hunter)];
    int affinityIndex = 0;
    int candidateCount = 0;
    int candidates[9] = {};
    const bool includeMissile = affinity == Beam::Missile;
    for (int i = includeMissile ? 1 : 2; i < 9; i++) {
        const int beam = GetBeamType(i);
        if (CheckBeam(beam)) {
            candidates[candidateCount++] = i;
            if (affinity == beam) {
                affinityIndex = i;
            }
        }
    }
    if (candidateCount == 1) {
        candidates[0] = 1; // Missile
        candidates[1] = 0; // Power Beam
        candidateCount = 2;
    } else if (candidateCount == 0) {
        candidates[0] = 0; // Power Beam
        candidateCount = 1;
        if (CheckBeam(Beam::Missile)) {
            candidates[1] = 1;
            candidateCount = 2;
        }
    }
    // the affinity weapon, when there, gets half the chance
    const int randIdx = static_cast<int>(randomInt2(static_cast<uint32_t>(candidateCount * (affinityIndex != 0 ? 2 : 1))));
    int noChargeChanceOneIn;
    if (randIdx < candidateCount) {
        _weapon2 = candidates[randIdx];
        noChargeChanceOneIn = 2;
    } else {
        _weapon2 = affinityIndex;
        noChargeChanceOneIn = 4;
    }
    if (CheckCharge(GetBeamType(_weapon2)) && _player->m_botLevel > 0) {
        if (randomInt2(static_cast<uint32_t>(noChargeChanceOneIn)) == 0) {
            Flags4 &= ~AiFlags4::Bit1;
        } else {
            Flags4 |= AiFlags4::Bit1;
        }
    }
}

void PlayerAi::Func1_2149C98() { _weapon1 = CheckBeam(GetBeamType(_weapon2)) ? _weapon2 : 0; }

void PlayerAi::Func1_2149C80()
{
    _weapon1 = 0;
    Flags4 &= ~AiFlags4::Bit1;
}

void PlayerAi::Func1_2149C68()
{
    _weapon1 = 2;
    Flags4 &= ~AiFlags4::Bit1;
}

void PlayerAi::Func1_2149C50()
{
    _weapon1 = 2;
    Flags4 |= AiFlags4::Bit1;
}

void PlayerAi::Func1_2149C38()
{
    _weapon1 = 5;
    Flags4 &= ~AiFlags4::Bit1;
}

void PlayerAi::Func1_2149C20()
{
    _weapon1 = 5;
    Flags4 |= AiFlags4::Bit1;
}

void PlayerAi::Func1_2149C08()
{
    _weapon1 = 4;
    Flags4 &= ~AiFlags4::Bit1;
}

void PlayerAi::Func1_2149BF0()
{
    _weapon1 = 6;
    Flags4 &= ~AiFlags4::Bit1;
}

void PlayerAi::Func1_2149BD8()
{
    _weapon1 = 6;
    Flags4 |= AiFlags4::Bit1;
}

void PlayerAi::Func1_2149BC0()
{
    _weapon1 = 7;
    Flags4 &= ~AiFlags4::Bit1;
}

void PlayerAi::Func1_2149BA8()
{
    _weapon1 = 3;
    Flags4 &= ~AiFlags4::Bit1;
}

void PlayerAi::Func1_2149B98() { Flags4 |= AiFlags4::Bit1; }

void PlayerAi::Func1_2149AD8()
{
    // keep holding shoot to charge the weapon
    if (_buttons.R.FramesDown == 0 || _player->m_altForm) {
        return;
    }
    if (CanChargeWeapon()) {
        _buttons.R.IsDown = true;
    }
}

void PlayerAi::Func1_2149AC8() { Flags4 |= AiFlags4::Bit2; }
void PlayerAi::Func1_2149ABC() { _octolithFlagCC = _octolithFlagD4; }
void PlayerAi::Func1_2149AB0() { _octolithFlagCC = _octolithFlagDC; }
void PlayerAi::Func1_2149AA4() { _flagBaseD0 = _flagBaseD8; }
void PlayerAi::Func1_2149A98() { _flagBaseD0 = _flagBaseE0; }

void PlayerAi::Func1_2149A64()
{
    FindEntityRef(54);
    UpdateTargetItem(ItemRef(54));
}

// Types 56/57, 58/59, 60/61... per weapon index: the item (or spawn) for a
// weapon the bot has (ammo) or has not (the weapon).
namespace {
struct WeaponRefs {
    int have, missing;
};
// Func1_2149824 and Func3_213C0D0 (item instances); the spawns are these minus 20.
constexpr WeaponRefs kWeaponItemRefs[9] = {{-1, -1}, {57, 56}, {59, 58}, {60, 61}, {63, 62}, {65, 64}, {67, 66}, {69, 68}, {71, 70}};
} // namespace

void PlayerAi::Func1_2149824()
{
    if (_findWeaponIndex >= 1 && _findWeaponIndex <= 8) {
        const WeaponRefs refs = kWeaponItemRefs[_findWeaponIndex];
        const int type = _player->m_availableWeapons[GetBeamType(_findWeaponIndex)] ? refs.have : refs.missing;
        FindEntityRef(type);
        UpdateTargetItem(ItemRef(type));
    } else {
        UpdateTargetItem(nullptr);
    }
}

void PlayerAi::Func1_21497F0()
{
    FindEntityRef(55);
    UpdateTargetItem(ItemRef(55));
}

void PlayerAi::Func1_2149570()
{
    FindEntityRef(56);
    UpdateTargetItem(ItemRef(56));
}

void PlayerAi::Func1_21494FC()
{
    FindEntityRef(57);
    UpdateTargetItem(ItemRef(57));
}

void PlayerAi::Func1_2149488()
{
    FindEntityRef(58);
    UpdateTargetItem(ItemRef(58));
}

void PlayerAi::Func1_2149414()
{
    FindEntityRef(59);
    UpdateTargetItem(ItemRef(59));
}

void PlayerAi::Func1_21493A0()
{
    FindEntityRef(72);
    UpdateTargetItem(ItemRef(72));
}

void PlayerAi::Func1_214932C()
{
    FindEntityRef(73);
    UpdateTargetItem(ItemRef(73));
}

void PlayerAi::Func1_21495A4()
{
    // like Func1_2149824, but for the spawn
    if (_findWeaponIndex >= 1 && _findWeaponIndex <= 8) {
        const WeaponRefs refs = kWeaponItemRefs[_findWeaponIndex];
        // the game's bug, kept: the Omega Cannon checks the Shock Coil
        const int checkBeam = _findWeaponIndex == 8 ? Beam::ShockCoil : GetBeamType(_findWeaponIndex);
        // spawn refs: 37/36 missile, 39/38 volt, 41/40 battlehammer, 43/42...
        static constexpr WeaponRefs spawnRefs[9] = {{-1, -1}, {37, 36}, {39, 38}, {41, 40}, {43, 42}, {45, 44}, {47, 46}, {49, 48}, {51, 50}};
        (void)refs;
        const int type = _player->m_availableWeapons[checkBeam] ? spawnRefs[_findWeaponIndex].have : spawnRefs[_findWeaponIndex].missing;
        FindEntityRef(type);
        ItemSpawn* spawn = ItemSpawnRef(type);
        _itemSpawnC4 = spawn != nullptr && spawn->item != nullptr ? spawn : nullptr;
    } else {
        _itemSpawnC4 = nullptr;
    }
}

void PlayerAi::SetItemSpawnC4(int type)
{
    FindEntityRef(type);
    ItemSpawn* spawn = ItemSpawnRef(type);
    _itemSpawnC4 = spawn != nullptr && spawn->item != nullptr ? spawn : nullptr;
}

void PlayerAi::Func1_2149530() { SetItemSpawnC4(36); }
void PlayerAi::Func1_21494BC() { SetItemSpawnC4(37); }
void PlayerAi::Func1_2149448() { SetItemSpawnC4(38); }
void PlayerAi::Func1_21493D4() { SetItemSpawnC4(39); }
void PlayerAi::Func1_2149360() { SetItemSpawnC4(52); }
void PlayerAi::Func1_21492EC() { SetItemSpawnC4(53); }

void PlayerAi::Func1_21492DC()
{
    // in multiplayer, the first player object
    Player* first = _world.m_slots.empty() ? nullptr : _world.m_slots[0].player.get();
    if (first == nullptr) {
        Flags2 |= AiFlags2::Bit9;
    } else {
        Flags2 &= ~AiFlags2::Bit9;
        Func21356C0(first);
    }
}

void PlayerAi::Func1_21492CC()
{
    FindEntityRef(27);
    Func21356C0(PlayerRef(27));
}

void PlayerAi::Func1_21492BC() { Func2135510(); }
void PlayerAi::Func1_21492AC() { Func21354E0(); }
void PlayerAi::Func1_214929C() { Func2135510(); }
void PlayerAi::Func1_214928C() { Func21354B0(); }
void PlayerAi::Func1_214927C() { Func2135380(); }
void PlayerAi::Func1_214926C() { Func2135480(); }

void PlayerAi::Func1_214925C()
{
    // after the Prime Hunter, when there is one and it is someone else
    if (_world.m_primeHunter < 0 || _player->slot() == _world.m_primeHunter) {
        FindEntityRef(32);
        Func21356C0(PlayerRef(32));
    } else {
        Flags2 &= ~AiFlags2::Bit9;
        Func21356C0(_world.m_slots[_world.m_primeHunter].player.get());
    }
}

void PlayerAi::Func1_214924C() { Func21354B0(); }

void PlayerAi::Func1_214923C()
{
    if (_octolithFlagCC != nullptr && _octolithFlagCC->carrier != nullptr && _octolithFlagCC->carrier->m_health != 0) {
        Func21356C0(_octolithFlagCC->carrier);
        Flags2 &= ~AiFlags2::Bit9;
    } else {
        Flags2 |= AiFlags2::Bit9;
    }
}

void PlayerAi::Func1_214922C()
{
    FindEntityRef(33);
    if (_entityRefs.Field33 != nullptr) {
        Flags2 |= AiFlags2::TargetHalfturret;
    } else {
        Flags2 &= ~AiFlags2::TargetHalfturret;
    }
    if (_entityRefs.Field33 != _targetHalfturret) {
        _targetHalfturret = _entityRefs.Field33;
        _entityRefs.Nodes[3] = nullptr;
    }
}

void PlayerAi::Func1_214921C()
{
    FindEntityRef(75);
    Func2135624(_entityRefs.Defenses[1]);
}

void PlayerAi::Func1_214920C()
{
    FindEntityRef(76);
    Func2135624(_entityRefs.Defenses[2]);
}

void PlayerAi::Func1_21491FC() { Func21355D8(); }

void PlayerAi::QueueFindEntity(int type)
{
    _queuedFindEntityAction = type;
    Flags2 |= AiFlags2::Bit1;
}

void PlayerAi::Func1_21491E4() { QueueFindEntity(0); }
void PlayerAi::Func1_21491CC() { QueueFindEntity(1); }
void PlayerAi::Func1_21491B4() { QueueFindEntity(2); }
void PlayerAi::Func1_214919C() { QueueFindEntity(3); }
void PlayerAi::Func1_2149184() { QueueFindEntity(5); }
void PlayerAi::Func1_214916C() { QueueFindEntity(6); }
void PlayerAi::Func1_2149154() { QueueFindEntity(7); }
void PlayerAi::Func1_214913C() { QueueFindEntity(9); }
void PlayerAi::Func1_2149124() { QueueFindEntity(10); }
void PlayerAi::Func1_214910C() { QueueFindEntity(11); }
void PlayerAi::Func1_21490F4() { QueueFindEntity(12); }
void PlayerAi::Func1_21490DC() { QueueFindEntity(13); }
void PlayerAi::Func1_21490C4() { QueueFindEntity(14); }
void PlayerAi::Func1_21490AC() { QueueFindEntity(15); }
void PlayerAi::Func1_2149094() { QueueFindEntity(16); }

void PlayerAi::Func1_2149088() { _field30 = 1; }

void PlayerAi::Func1_2149034()
{
    const int navIndex = _nodeTypeIndex[static_cast<int>(NodeType::Navigation)];
    const int specIndex = _nodeTypeIndex[static_cast<int>(NodeType::Special)];
    if (navIndex == specIndex) {
        _field30 = 0;
    } else {
        const int offset = static_cast<int>(randomInt2(static_cast<uint16_t>(specIndex - navIndex)));
        _field30 = static_cast<int>((*_nodeList)[navIndex + offset].Field4);
    }
}

void PlayerAi::Func1_2148F10()
{
    const int navIndex = _nodeTypeIndex[static_cast<int>(NodeType::Navigation)];
    const int specIndex = _nodeTypeIndex[static_cast<int>(NodeType::Special)];
    if (navIndex == specIndex) {
        _field30 = 0;
        return;
    }
    int i = 0;
    int offsetCount = 0;
    int offsets[10] = {};
    while (offsetCount < 10 && i < specIndex - navIndex) {
        if (_world.m_mode == GameMode::Capture && _player->m_teamIndex == 1) {
            // Capture's second team has its own paths, numbered from 101.
            const uint32_t field4 = (*_nodeList)[navIndex + i].Field4;
            if (field4 > 100 && field4 <= 110) {
                offsets[offsetCount++] = i;
            }
        } else if ((*_nodeList)[navIndex + i].Field4 <= 10) {
            offsets[offsetCount++] = i;
        }
        i++;
    }
    const int offset = offsets[randomInt2(static_cast<uint32_t>(offsetCount))];
    _field30 = static_cast<int>((*_nodeList)[navIndex + offset].Field4);
}

void PlayerAi::Func1_2148EDC()
{
    const int navIndex = _nodeTypeIndex[static_cast<int>(NodeType::Navigation)];
    const int specIndex = _nodeTypeIndex[static_cast<int>(NodeType::Special)];
    _field30 = specIndex <= navIndex ? 0 : static_cast<int>((*_nodeList)[specIndex - 1].Field4);
}

void PlayerAi::Func1_2148ECC() { _field30++; }

void PlayerAi::Func1_2148EB8()
{
    if (_field30 != 0) {
        _field30--;
    }
}

void PlayerAi::Func1_2148EA8() { _field30 += 10; }
void PlayerAi::Func1_2148E98() { _nodeDataSelOn = 255; }
void PlayerAi::Func1_2148E88() { _nodeDataSelOff = 255; }
void PlayerAi::Func1_2148E74() { Flags3 |= AiFlags3::Bit5; }
void PlayerAi::Func1_2148E64() { Flags2 |= AiFlags2::Bit18; }
void PlayerAi::Func1_2148E54() { Flags2 |= AiFlags2::Bit19; }
void PlayerAi::Func1_2148DF8() {} // Trace's warning sound (no sound yet)
void PlayerAi::Func1_2148DE8() { Flags2 |= AiFlags2::Bit20; }
void PlayerAi::Func1_2148D50() {}                  // single player: a camera sequence
void PlayerAi::Func1_UnlockEchoHallForceField() {} // single player
void PlayerAi::Func1_SetInvulnerable() { Flags3 |= AiFlags3::Invulnerable; }

} // namespace fp
