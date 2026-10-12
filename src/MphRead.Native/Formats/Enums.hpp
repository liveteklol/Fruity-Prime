#pragma once

#include <cstdint>
#include <string>

namespace MphRead
{
    enum class EntityType : std::uint16_t
    {
        Platform = 0,
        Object = 1,
        PlayerSpawn = 2,
        Door = 3,
        ItemSpawn = 4,
        ItemInstance = 5,
        EnemySpawn = 6,
        TriggerVolume = 7,
        AreaVolume = 8,
        JumpPad = 9,
        PointModule = 10,
        MorphCamera = 11,
        OctolithFlag = 12,
        FlagBase = 13,
        Teleporter = 14,
        NodeDefense = 15,
        LightSource = 16,
        Artifact = 17,
        CameraSequence = 18,
        ForceField = 19,
        BeamEffect = 21,
        Bomb = 22,
        EnemyInstance = 23,
        Halfturret = 24,
        Player = 25,
        BeamProjectile = 26,
        ListHead = 27,
        FhUnknown0 = 100,
        FhPlayerSpawn = 101,
        FhUnknown2 = 102,
        FhDoor = 103,
        FhItemSpawn = 104,
        FhItemInstance = 105,
        FhEnemySpawn = 106,
        FhEffectInstance = 107,
        FhBomb = 108,
        FhTriggerVolume = 109,
        FhAreaVolume = 110,
        FhPlatform = 111,
        FhJumpPad = 112,
        FhPointModule = 113,
        FhMorphCamera = 114,
        FhEnemyInstance = 115,
        FhPlayer = 116,
        FhBeamProjectile = 117,
        Room = 200,
        Model = 201,
        // A translated Quake 3 level's mover (Entities::Q3MoverEntity): no
        // cartridge entity, only collision that moves.
        Q3Mover = 202,
        All = 255
    };

    enum class VolumeType : std::uint32_t
    {
        Box = 0,
        Cylinder = 1,
        Sphere = 2
    };

    enum class FhVolumeType : std::uint32_t
    {
        Sphere = 0,
        Box = 1,
        Cylinder = 2
    };

    enum class DoorType : std::uint32_t
    {
        Standard = 0,
        MorphBall = 1,
        Boss = 2,
        Thin = 3
    };

    enum class ItemType : std::int32_t
    {
        None = -1,
        HealthMedium = 0,
        HealthSmall = 1,
        HealthBig = 2,
        DoubleDamage = 3,
        EnergyTank = 4,
        VoltDriver = 5,
        MissileExpansion = 6,
        Battlehammer = 7,
        Imperialist = 8,
        Judicator = 9,
        Magmaul = 10,
        ShockCoil = 11,
        OmegaCannon = 12,
        UASmall = 13,
        UABig = 14,
        MissileSmall = 15,
        MissileBig = 16,
        Cloak = 17,
        UAExpansion = 18,
        ArtifactKey = 19,
        Deathalt = 20,
        AffinityWeapon = 21,
        PickWpnMissile = 22
    };

    enum class FhItemType : std::int32_t
    {
        None = -1,
        AmmoSmall = 0,
        AmmoBig = 1,
        HealthSmall = 2,
        HealthBig = 3,
        DoubleDamage = 4,
        PowerBeam = 5,
        ElectroLob = 6,
        Missile = 7
    };

    enum class BeamType : std::int8_t
    {
        None = -1,
        PowerBeam = 0,
        VoltDriver = 1,
        Missile = 2,
        Battlehammer = 3,
        Imperialist = 4,
        Judicator = 5,
        Magmaul = 6,
        ShockCoil = 7,
        OmegaCannon = 8,
        Platform = 9,
        Enemy = 10
    };

    enum class WeaponUnlockBits : std::uint16_t
    {
        PowerBeam = 0x0001,
        VoltDriver = 0x0002,
        Missile = 0x0004,
        Battlehammer = 0x0008,
        Imperialist = 0x0010,
        Judicator = 0x0020,
        Magmaul = 0x0040,
        ShockCoil = 0x0080,
        OmegaCannon = 0x0100
    };

    enum class BombType : std::uint8_t
    {
        MorphBall = 0,
        Stinglarva = 1,
        Lockjaw = 2
    };

    enum class Affliction : std::uint8_t
    {
        None = 0,
        Freeze = 1,
        Disrupt = 2,
        Burn = 4
    };

    enum class FadeType : std::uint8_t
    {
        None = 0,
        FadeInBlack = 1,
        FadeOutBlack = 2,
        FadeInWhite = 3,
        FadeOutWhite = 4,
        FadeOutInBlack = 5,
        FadeOutInWhite = 6
    };

    enum class Terrain : std::uint8_t
    {
        Metal = 0,
        OrangeHolo = 1,
        GreenHolo = 2,
        BlueHolo = 3,
        Ice = 4,
        Snow = 5,
        Sand = 6,
        Rock = 7,
        Lava = 8,
        Acid = 9,
        Gorea = 10,
        Unknown11 = 11,
        All = 12
    };

    enum class Button : std::int32_t
    {
        A = 0,
        B = 1,
        Select = 2,
        Start = 3,
        Right = 4,
        Left = 5,
        Up = 6,
        Down = 7,
        R = 8,
        L = 9,
        X = 10,
        Y = 11
    };

    enum class ButtonFlags : std::uint16_t
    {
        None = 0x0,
        A = 0x1,
        B = 0x2,
        Select = 0x4,
        Start = 0x8,
        Right = 0x10,
        Left = 0x20,
        Up = 0x40,
        Down = 0x80,
        R = 0x100,
        L = 0x200,
        X = 0x400,
        Y = 0x800
    };

    enum class PressFlags : std::uint16_t
    {
        None = 0x0,
        Touch = 0x1,
        Pressed = 0x4,
        Released = 0x8,
        Repeated = 0x10
    };

    enum class ModelType : std::int32_t
    {
        Generic = 0,
        Room = 1,
        Item = 2,
        Object = 3,
        Placeholder = 4,
        JumpPad = 5,
        JumpPadBeam = 6,
        Enemy = 7,
        Player = 8,
        Platform = 9
    };

    enum class Team : std::int32_t
    {
        None = 0,
        Orange = 1,
        Green = 2
    };

    enum class BillboardMode : std::uint8_t
    {
        None = 0,
        Sphere = 1,
        Cylinder = 2
    };

    enum class PolygonMode : std::uint32_t
    {
        Modulate = 0,
        Decal = 1,
        Toon = 2,
        Shadow = 3
    };

    enum class RepeatMode : std::uint8_t
    {
        Clamp = 0,
        Repeat = 1,
        Mirror = 2
    };

    enum class RenderMode : std::uint8_t
    {
        Normal = 0,
        Decal = 1,
        Translucent = 2,
        Unknown3 = 3,
        Unknown4 = 4
    };

    enum class TexgenMode : std::uint32_t
    {
        None = 0,
        Texcoord = 1,
        Normal = 2,
        Vertex = 3
    };

    enum class CullingMode : std::uint8_t
    {
        Neither = 0,
        Front = 1,
        Back = 2
    };

    enum class TextureFormat : std::uint8_t
    {
        Palette2Bit = 0,
        Palette4Bit = 1,
        Palette8Bit = 2,
        PaletteA5I3 = 4,
        DirectRgb = 5,
        PaletteA3I5 = 6
    };

    enum class TriggerType : std::uint32_t
    {
        Volume = 0,
        Threshold = 1,
        Relay = 2,
        Automatic = 3,
        StateBits = 4
    };

    enum class FhTriggerType : std::uint32_t
    {
        Sphere = 0,
        Box = 1,
        Cylinder = 2,
        Threshold = 3
    };

    enum class Message : std::uint32_t
    {
        None = 0,
        SetActive = 5,
        Destroyed = 6,
        Damage = 7,
        Trigger = 9,
        UpdateMusic = 12,
        Gravity = 15,
        Unlock = 16,
        Lock = 17,
        Activate = 18,
        Complete = 19,
        Impact = 20,
        Death = 21,
        Unused22 = 22,
        ShipHatch = 23,
        Unused24 = 24,
        Unused25 = 25,
        ShowPrompt = 26,
        ShowWarning = 27,
        ShowOverlay = 28,
        MoveItemSpawner = 29,
        SetCamSeqAi = 30,
        PlayerCollideWith = 31,
        BeamCollideWith = 32,
        UnlockConnectors = 33,
        LockConnectors = 34,
        PreventFormSwitch = 35,
        Gorea2Trigger = 36,
        SetTriggerState = 42,
        ClearTriggerState = 43,
        PlatformWakeup = 44,
        PlatformSleep = 45,
        DripMoatPlatform = 46,
        ActivateTurret = 48,
        DecreaseTurretLights = 49,
        IncreaseTurretLights = 50,
        DeactivateTurret = 51,
        SetBeamReflection = 52,
        SetPlatformIndex = 53,
        PlaySfxScript = 54,
        UnlockOubliette = 56,
        Checkpoint = 57,
        EscapeUpdate1 = 58,
        SetSeekPlayerY = 59,
        LoadOubliette = 60,
        EscapeUpdate2 = 61
    };

    enum class FhMessage : std::uint32_t
    {
        None = 0,
        Activate = 5,
        Destroyed = 6,
        Damage = 7,
        Trigger = 9,
        Gravity = 15,
        Unlock = 16,
        SetActive = 17,
        Complete = 18,
        Impact = 19,
        Death = 20,
        Unknown21 = 21
    };

    enum class EnemyType : std::uint8_t
    {
        WarWasp = 0,
        Zoomer = 1,
        Temroid = 2,
        Petrasyl1 = 3,
        Petrasyl2 = 4,
        Petrasyl3 = 5,
        Petrasyl4 = 6,
        Unknown7 = 7,
        Unknown8 = 8,
        Unknown9 = 9,
        BarbedWarWasp = 10,
        Shriekbat = 11,
        Geemer = 12,
        Unknown13 = 13,
        Unknown14 = 14,
        Unknown15 = 15,
        Blastcap = 16,
        Unknown17 = 17,
        AlimbicTurret = 18,
        Cretaphid = 19,
        CretaphidEye = 20,
        CretaphidCrystal = 21,
        Unknown22 = 22,
        PsychoBit1 = 23,
        Gorea1A = 24,
        GoreaHead = 25,
        GoreaArm = 26,
        GoreaLeg = 27,
        Gorea1B = 28,
        GoreaSealSphere1 = 29,
        Trocra = 30,
        Gorea2 = 31,
        GoreaSealSphere2 = 32,
        GoreaMeteor = 33,
        PsychoBit2 = 34,
        Voldrum2 = 35,
        Voldrum1 = 36,
        Quadtroid = 37,
        CrashPillar = 38,
        FireSpawn = 39,
        Spawner = 40,
        Slench = 41,
        SlenchShield = 42,
        SlenchNest = 43,
        SlenchSynapse = 44,
        SlenchTurret = 45,
        LesserIthrak = 46,
        GreaterIthrak = 47,
        Hunter = 48,
        ForceFieldLock = 49,
        HitZone = 50,
        CarnivorousPlant = 51
    };

    enum class FhEnemyType : std::uint32_t
    {
        WarWasp = 0,
        Zoomer = 1,
        Metroid = 2,
        Mochtroid1 = 3,
        Mochtroid2 = 4,
        Mochtroid3 = 5,
        Mochtroid4 = 6
    };

    enum class Hunter : std::uint8_t
    {
        Samus = 0,
        Kanden = 1,
        Trace = 2,
        Sylux = 3,
        Noxus = 4,
        Spire = 5,
        Weavel = 6,
        Guardian = 7,
        Random = 8
    };

    // Hunter.ToString().
    [[nodiscard]] std::string ToString(Hunter value);
    // EntityType.ToString().
    [[nodiscard]] std::string ToString(EntityType value);
    // Message.ToString().
    [[nodiscard]] std::string ToString(Message value);
    // DoorType.ToString().
    [[nodiscard]] std::string ToString(DoorType value);
    // FhItemType.ToString().
    [[nodiscard]] std::string ToString(FhItemType value);
    // FhMessage.ToString().
    [[nodiscard]] std::string ToString(FhMessage value);
    // FhTriggerType.ToString().
    [[nodiscard]] std::string ToString(FhTriggerType value);
    // TriggerType.ToString().
    [[nodiscard]] std::string ToString(TriggerType value);
    // BeamType.ToString() / Enum.TryParse(text, ignoreCase, out BeamType).
    [[nodiscard]] std::string ToString(BeamType value);
    [[nodiscard]] bool TryParse(std::string_view text, bool ignoreCase, BeamType& value);
    // Terrain.ToString() / Enum.TryParse(text, ignoreCase, out Terrain).
    [[nodiscard]] std::string ToString(Terrain value);
    [[nodiscard]] bool TryParse(std::string_view text, bool ignoreCase, Terrain& value);
    // Enum.TryParse(text, ignoreCase, out ItemType).
    [[nodiscard]] bool TryParse(std::string_view text, bool ignoreCase, ItemType& value);
    // The model and texture enums' ToString().
    [[nodiscard]] std::string ToString(BillboardMode value);
    [[nodiscard]] std::string ToString(PolygonMode value);
    [[nodiscard]] std::string ToString(RepeatMode value);
    [[nodiscard]] std::string ToString(RenderMode value);
    [[nodiscard]] std::string ToString(TexgenMode value);
    [[nodiscard]] std::string ToString(CullingMode value);
    [[nodiscard]] std::string ToString(TextureFormat value);
    // Enum.TryParse(text, ignoreCase, out Hunter).
    [[nodiscard]] bool TryParse(std::string_view text, bool ignoreCase, Hunter& value);
    // ItemType.ToString().
    [[nodiscard]] std::string ToString(ItemType value);
    // EnemyType.ToString().
    [[nodiscard]] std::string ToString(EnemyType value);
    // FhEnemyType.ToString().
    [[nodiscard]] std::string ToString(FhEnemyType value);

    enum class Language : std::int32_t
    {
        English = 0,
        Japanese = 1,
        French = 2,
        Spanish = 3,
        German = 4,
        Italian = 5
    };

    enum class WaveFormat : std::int32_t
    {
        None = -1,
        PCM8 = 0,
        PCM16 = 1,
        ADPCM = 2
    };

    enum class SingleType : std::int32_t
    {
        Death = 0,
        Fuzzball = 1,
        Lore = 2,
        LoreDim = 3,
        Enemy = 4,
        EnemyDim = 5,
        Object = 6,
        ObjectDim = 7,
        Equipment = 8,
        EquipmentDim = 9,
        Red = 10,
        RedDim = 11
    };

    enum class SaveWhen : std::int32_t
    {
        Never = 0,
        Always = 1,
        Prompt = 2
    };
}
