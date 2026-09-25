#pragma once

#include "AiData.h"
#include "Player.h"
#include "WorldEntities.h"

#include <array>
#include <cstdint>
#include <vector>

// The bots: a port of PlayerEntity.PlayerAiData (Entities/Players/PlayerAi.cs),
// itself a decompilation of the DS game's AI. The names are the C#'s, which are
// mostly the game's addresses; the behavior tree they run comes from
// aiPersonalityData.bin. Every tick the bot presses "buttons" (ProcessInput
// turns them into the player's input) and decides what to press next (Process).
namespace fp {

class World;

namespace AiFlags2 {
constexpr uint32_t Bit0 = 0x1, Bit1 = 0x2, TargetPlayer = 0x4, TargetHalfturret = 0x8, TargetItem = 0x10, TargetDefense = 0x20,
                   TargetDoor = 0x40, Bit7 = 0x80, Bit8 = 0x100, Bit9 = 0x200, Bit10 = 0x400, Bit11 = 0x800, Bit12 = 0x1000,
                   Bit13 = 0x2000, AiStart = 0x4000, Bit15 = 0x8000, Bit16 = 0x10000, Bit17 = 0x20000, Bit18 = 0x40000,
                   Bit19 = 0x80000, Bit20 = 0x100000, Bit21 = 0x200000;
} // namespace AiFlags2

namespace AiFlags3 {
constexpr uint32_t NoInput = 0x1, Bit1 = 0x2, Bit2 = 0x4, Despawned = 0x8, Invulnerable = 0x10, Bit5 = 0x20;
} // namespace AiFlags3

namespace AiFlags4 {
constexpr uint8_t Bit0 = 0x1, Bit1 = 0x2, Bit2 = 0x4, Bit3 = 0x8;
} // namespace AiFlags4

// AiQueuedEnt.None
constexpr int AiQueuedEntNone = 40;

// The match-wide part of PlayerAiData's statics, one per World.
struct AiGlobalState {
    struct Global {
        Player* player = nullptr;
        int field4 = 0;
        int nodeDataIndex = 0;
        const std::array<const NodeData3*, 11>* nodeData = nullptr; // the bot's path (_field4C)
    };
    int globalField0 = 0, globalField2 = 0;
    std::array<Global, 16> globalObjs{};
    std::array<std::array<bool, 16>, 16> playerVisibility{};
    int visIndex1 = 1, visIndex2 = 0;
};

class PlayerAi {
public:
    PlayerAi(Player& player, World& world);

    // AiPersonality.LoadAll: the behavior tree for the match's mode.
    void setPersonality(const AiPersonalityData1* personality) { Personality = personality; }
    void Reset();
    void InitializeAtLoad();
    void InitializeAtSpawn();
    // ProcessInput: this tick's buttons as the player's input.
    PlayerInput ProcessInput();
    // Process: what to press next tick.
    void Process();
    // OnTakeDamage: `victim` (this bot's player) was hurt; every bot hears of it.
    void OnTakeDamage(int damage, bool fromBeam, int beam, bool fromWeavelAlt, Player* attacker);
    static void InitializeGlobals(AiGlobalState& state);
    static void UpdateVisibilityAndGlobals(World& world);
    // The execution path through the tree, for debugging.
    std::string pathDescription() const;

    const AiPersonalityData1* Personality = nullptr;
    bool Flags1 = false;
    uint32_t Flags2 = 0;
    uint32_t Flags3 = 0; // frame flags, cleared every frame and set again by the funcs
    uint8_t Flags4 = 0;
    uint16_t HealthThreshold = 0;
    uint32_t DamageFromHalfturret = 0;
    int Field118 = 0;

    // The objects of Capture, Bounty, Nodes and Defender, as the World keeps them.
    using OctolithFlag = fp::OctolithFlag;
    using FlagBase = fp::FlagBase;
    using NodeDefense = fp::NodeDefense;

private:
    struct AiButton {
        bool IsDown = false;
        int FramesDown = 0;
        int FramesUp = 0;
        void Clear()
        {
            IsDown = false;
            FramesDown = 0;
            FramesUp = 0;
        }
    };
    struct AiButtons {
        AiButton Up, Down, Left, Right, A, B, X, Y, L, R, Start, Select;
    };
    struct AiTouchButtons {
        AiButton Morph, Unmorph, PowerBeam, Missile, VoltDriver, Battlehammer, Imperialist, Judicator, Magmaul, ShockCoil, OmegaCannon;
    };
    struct AiPlayerAggro {
        uint8_t VarA2 = 0, VarA9 = 0, VarA3 = 0, VarA4 = 0, VarA10 = 0;
        uint16_t VarA7 = 0, Staleness = 0, Expiration = 0;
        Player* Player1 = nullptr;
        Player* Player2 = nullptr;
        void Clear() { *this = AiPlayerAggro{}; }
    };
    struct AiContext {
        int Func24Id = 0;
        uint8_t Field4 = 0, Field5 = 0, Field6 = 0, Field7 = 0, Field8 = 0, Field9 = 0, FieldA = 0, FieldB = 0, FieldC = 0, FieldD = 0,
                FieldE = 0, FieldF = 0, Field10 = 0;
        int Field14 = 0, Field18 = 0, Field1C = 0, Field20 = 0, Field24 = 0;
        bool Field28 = false;
        int Field2C = 0;
        bool Field30 = false;
        Vec3 Field34{}; // stored player position
        int Field40 = 0; // timer/counter
        int Field44 = 0; // timer
        const AiPersonalityData1* Data1 = nullptr;
        int CallCount = 0;
        int Depth = 0;
        std::array<int, 21> Weights{};
        void Clear() { *this = AiContext{}; }
    };
    // AiEntityRefs: what FindEntityRef found this tick, by AiEntRefType.
    struct AiEntityRefs {
        std::array<const NodeData3*, 26> Nodes{};        // Field0-25
        std::array<Player*, 7> Players{};                // Field26-32
        Player* Field33 = nullptr;                       // a Halfturret, by its owner
        std::array<ItemSpawn*, 20> ItemSpawns{};         // Field34-53
        std::array<ItemInstance*, 20> Items{};           // Field54-73
        std::array<const NodeDefense*, 3> Defenses{};    // Field74-76
        Door* Field77 = nullptr;
        bool IsPopulated(int index) const;
        void Clear() { *this = AiEntityRefs{}; }
    };

    static constexpr int _maxContextDepth = 20;
    static constexpr ItemType kItemNone = static_cast<ItemType>(-1);

#include "PlayerAiMethods.inc"

    Player* _player;
    World& _world;
    const NodeData* _nodeData = nullptr;
    bool _forceDisable = false;

    std::array<int, 16> _slotHits{};
    std::array<int, 16> _slotDamage{};

    const NodeData3* _node3C = nullptr;
    const NodeData3* _node40 = nullptr;
    const NodeData3* _node44 = nullptr;
    const NodeData3* _node48 = nullptr;
    std::array<const NodeData3*, 11> _field4C{};
    uint16_t _field78 = 0;
    std::array<uint16_t, 10> _field7A{};

    Player* _targetPlayer = nullptr;
    Player* _targetHalfturret = nullptr; // the Halfturret's owner
    ItemSpawn* _itemSpawnC4 = nullptr;
    ItemInstance* _itemC8 = nullptr;
    OctolithFlag* _octolithFlagCC = nullptr;
    FlagBase* _flagBaseD0 = nullptr;
    OctolithFlag* _octolithFlagD4 = nullptr;
    FlagBase* _flagBaseD8 = nullptr;
    OctolithFlag* _octolithFlagDC = nullptr;
    FlagBase* _flagBaseE0 = nullptr;
    const NodeDefense* _targetDefense = nullptr;
    Door* _targetDoor = nullptr;

    int _nodeDataSetIndex = 0;
    uint8_t _nodeDataSelOff = 0;
    uint8_t _nodeDataSelOn = 0;
    const std::vector<NodeData3>* _nodeList = nullptr;
    std::array<int, 6> _nodeTypeIndex{};

    uint32_t _field102C = 0; // random value bounds based on bot level
    uint32_t _field102E = 0; // timer set from field102C -- bomb delay
    uint32_t _field1030 = 0;
    uint32_t _field1034 = 0; // timer set for jumping
    int _field116 = 0;
    int _field1020 = 0; // initial aim deviation
    int _field1032 = 0;
    Vec3 _fieldA0{}, _fieldAC{}, _fieldB8{}, _field1038{}, _field1048{}, _field1054{};
    int _field30 = 0;
    Vec3 _field90{};
    float _field9C = 0;

    int _weapon1 = 0;
    int _weapon2 = 0;
    int _findWeaponIndex = 0;
    int _shotDelay = 0;

    AiButtons _buttons;
    AiTouchButtons _touchButtons;
    bool _hasTouch = false;
    uint16_t _touchAimX = 0, _touchAimY = 0;
    std::array<bool, 18> _controlsDown{}; // the controls held last tick, for "pressed"
    int _framesWithTouch = 0, _framesWithoutTouch = 0;
    float _buttonAimX = 0, _buttonAimY = 0;

    int _playerAggroCount = 0;
    std::array<AiPlayerAggro, 25> _playerAggro{};
    std::array<AiContext, _maxContextDepth> _executionTree{};

    int _queuedFindEntityAction = AiQueuedEntNone;
    AiEntityRefs _entityRefs;
};

} // namespace fp
