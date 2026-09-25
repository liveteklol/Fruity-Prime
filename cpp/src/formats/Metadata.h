#pragma once

#include <array>
#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

// Tables exported from the C# metadata (see tools/gen_metadata.py). Paths are
// relative to the game's file system root, with forward slashes.
namespace fp {

struct RecolorMetadata {
    const char* name;
    const char* modelPath;
    const char* texturePath;
    const char* palettePath;
    const char* replacePath; // nullptr: palettePath
    std::vector<std::pair<int, std::vector<int>>> replaceIds;
};

struct ModelMetadata {
    const char* name;
    const char* modelPath;
    const char* animationPath;
    const char* animationShare;
    std::vector<RecolorMetadata> recolors;
};

struct DoorMetadata {
    const char* name;
    const char* lockName;
    float lockOffset;
    float radius;
};

struct ObjectMetadata {
    const char* name;
    bool lighting;
    int recolorId;
    std::array<int, 4> animationIds;
    std::array<float, 3> visPosOffset;
};

struct PlatformMetadata {
    const char* name; // nullptr: an invisible platform
    bool lighting;
    std::array<int, 4> animationIds;
};

const std::vector<ModelMetadata>& modelTable();
const std::vector<DoorMetadata>& doorTable();
const std::vector<int>& doorPaletteTable();
const std::vector<const char*>& jumpPadTable();
const std::vector<const char*>& itemTable();
const std::vector<ObjectMetadata>& objectTable();
const std::vector<PlatformMetadata>& platformTable();

// One per hunter; raw values, fixed point where the C# reads them through Fixed.ToFloat.
struct PlayerValues {
#include "PlayerValues.inc"
};

const std::vector<PlayerValues>& playerValuesTable();
const std::array<float, 4>& slipSpeedFactors();
const std::array<float, 4>& tractionFactors();

// Weapons.WeaponsMP: the nine multiplayer weapons, then their affinity versions.
// Raw table values: speeds, radii and distances are fixed point, times are 30 Hz frames.
struct WeaponInfo {
#include "WeaponInfo.inc"
};
const std::vector<WeaponInfo>& weaponsMP();
// Weapons.Ricochets, which WeaponInfo.unchargedRicochetWeapon/chargedRicochetWeapon index.
const std::vector<WeaponInfo>& ricochetWeapons();
// Metadata.BeamDrawEffects: the effect a projectile carries, per draw function.
const std::vector<int>& beamDrawEffects();

// Metadata.Effects: effect id -> file name, and the archive it is in (none: effects/).
struct EffectMetadata {
    const char* name;
    const char* archive;
};
const std::vector<EffectMetadata>& effectTable();
// Metadata.SyluxBombEffects: bombStartSylux by recolor.
const std::vector<int>& syluxBombEffects();

// WeaponFlags (bits 0-7 are the priority)
namespace WeaponFlags {
constexpr uint32_t PartialCharge = 0x100;
constexpr uint32_t CanCharge = 0x200;
constexpr uint32_t RepeatFire = 0x400;
constexpr uint32_t CanZoom = 0x800;
constexpr uint32_t RicochetUncharged = 0x1000;
constexpr uint32_t RicochetCharged = 0x2000;
constexpr uint32_t SelfDamageUncharged = 0x8000;
constexpr uint32_t SelfDamageCharged = 0x10000;
constexpr uint32_t ForceEffectUncharged = 0x20000;
constexpr uint32_t ForceEffectCharged = 0x40000;
constexpr uint32_t AoeUncharged = 0x80000;
constexpr uint32_t AoeCharged = 0x100000;
constexpr uint32_t Continuous = 0x200000;
constexpr uint32_t DestroyableUncharged = 0x400000;
constexpr uint32_t DestroyableCharged = 0x800000;
constexpr uint32_t LifeDrainUncharged = 0x10000000;
constexpr uint32_t LifeDrainCharged = 0x20000000;
constexpr uint32_t SurfaceCollision = 0x40000000;
} // namespace WeaponFlags

// BeamType
namespace Beam {
constexpr int PowerBeam = 0, VoltDriver = 1, Missile = 2, Battlehammer = 3, Imperialist = 4, Judicator = 5, Magmaul = 6,
              ShockCoil = 7, OmegaCannon = 8, Platform = 9, None = 0xFF;
} // namespace Beam

// Affliction
namespace Affliction {
constexpr int Freeze = 1, Disrupt = 2, Burn = 4;
} // namespace Affliction
const std::vector<int>& affinityWeapons(); // beam per hunter
// Metadata.GunAnimationIds[hunter][GunAnimation][0: all groups, 1 + beam: materials], -1 for none.
const std::vector<int>& gunAnimationIds();
inline int gunAnimationId(int hunter, int animation, int column) { return gunAnimationIds().at((hunter * 13 + animation) * 10 + column); }

// HUD layout per hunter (HudElements.HunterObjects and the meter tables).
#include "HudStructs.inc"
const std::vector<HudHunterObjects>& hudHunterObjects();
const std::vector<HudMeterInfo>& hudMainHealthbars();
const std::vector<HudMeterInfo>& hudSubHealthbars();
const std::vector<HudMeterInfo>& hudAmmoBars();
const char* hudBoostFile();
const char* hudBombsFile();

const ModelMetadata* findModel(std::string_view name);

} // namespace fp
