#pragma once

#include "../../Formats/Enums.hpp"
#include "../../Formats/Types.hpp"

#include <algorithm>
#include <any>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <vector>

namespace MphRead
{
    class Scene;
    class EquipInfo;
    class WeaponInfo;
    class Keybind;

    namespace Formats
    {
        class NodeData;
        class NodeData3;
        class AiPersonalityData1;
        class AiPersonalityData2;
        class AiPersonalityData4;
        class AiPersonalityData5;
    }
}

namespace MphRead::Entities
{
    class EntityBase;
    class HalfturretEntity;
    class ItemSpawnEntity;
    class ItemInstanceEntity;
    class OctolithFlagEntity;
    class FlagBaseEntity;
    class NodeDefenseEntity;
    class DoorEntity;
    class CamSeqEntity;
    class ForceFieldEntity;
    class BombEntity;
    class BeamProjectileEntity;
    class JumpPadEntity;

    enum class AiFlags2 : std::uint32_t
    {
        None = 0, Bit0 = 1, Bit1 = 2, TargetPlayer = 4, TargetHalfturret = 8,
        TargetItem = 0x10, TargetDefense = 0x20, TargetDoor = 0x40, Bit7 = 0x80,
        Bit8 = 0x100, Bit9 = 0x200, Bit10 = 0x400, Bit11 = 0x800, Bit12 = 0x1000,
        Bit13 = 0x2000, AiStart = 0x4000, Bit15 = 0x8000, Bit16 = 0x10000,
        Bit17 = 0x20000, Bit18 = 0x40000, Bit19 = 0x80000, Bit20 = 0x100000,
        Bit21 = 0x200000, Unused22 = 0x400000, Unused23 = 0x800000,
        Unused24 = 0x1000000, Unused25 = 0x2000000, Unused26 = 0x4000000,
        Unused27 = 0x8000000, Unused28 = 0x10000000, Unused29 = 0x20000000,
        Unused30 = 0x40000000, Unused31 = 0x80000000
    };
    enum class AiFlags3 : std::uint32_t
    {
        None = 0, NoInput = 1, Bit1 = 2, Bit2 = 4, Despawned = 8, Invulnerable = 0x10, Bit5 = 0x20
    };
    enum class AiFlags4 : std::uint8_t
    {
        None = 0, Bit0 = 1, Bit1 = 2, Bit2 = 4, Bit3 = 8
    };

    constexpr AiFlags2 operator|(AiFlags2 a, AiFlags2 b) noexcept
    {
        return static_cast<AiFlags2>(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
    }
    constexpr AiFlags2 operator&(AiFlags2 a, AiFlags2 b) noexcept
    {
        return static_cast<AiFlags2>(static_cast<std::uint32_t>(a) & static_cast<std::uint32_t>(b));
    }
    constexpr AiFlags2 operator~(AiFlags2 a) noexcept
    {
        return static_cast<AiFlags2>(~static_cast<std::uint32_t>(a));
    }
    constexpr AiFlags2& operator|=(AiFlags2& a, AiFlags2 b) noexcept { return a = a | b; }
    constexpr AiFlags2& operator&=(AiFlags2& a, AiFlags2 b) noexcept { return a = a & b; }
    constexpr AiFlags2 operator^(AiFlags2 a, AiFlags2 b) noexcept
    {
        return static_cast<AiFlags2>(static_cast<std::uint32_t>(a) ^ static_cast<std::uint32_t>(b));
    }
    constexpr AiFlags2& operator^=(AiFlags2& a, AiFlags2 b) noexcept { return a = a ^ b; }

    constexpr AiFlags3 operator|(AiFlags3 a, AiFlags3 b) noexcept
    {
        return static_cast<AiFlags3>(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
    }
    constexpr AiFlags3 operator&(AiFlags3 a, AiFlags3 b) noexcept
    {
        return static_cast<AiFlags3>(static_cast<std::uint32_t>(a) & static_cast<std::uint32_t>(b));
    }
    constexpr AiFlags3 operator~(AiFlags3 a) noexcept
    {
        return static_cast<AiFlags3>(~static_cast<std::uint32_t>(a));
    }
    constexpr AiFlags3& operator|=(AiFlags3& a, AiFlags3 b) noexcept { return a = a | b; }
    constexpr AiFlags3& operator&=(AiFlags3& a, AiFlags3 b) noexcept { return a = a & b; }

    constexpr AiFlags4 operator|(AiFlags4 a, AiFlags4 b) noexcept
    {
        return static_cast<AiFlags4>(static_cast<std::uint8_t>(a) | static_cast<std::uint8_t>(b));
    }
    constexpr AiFlags4 operator&(AiFlags4 a, AiFlags4 b) noexcept
    {
        return static_cast<AiFlags4>(static_cast<std::uint8_t>(a) & static_cast<std::uint8_t>(b));
    }
    constexpr AiFlags4 operator~(AiFlags4 a) noexcept
    {
        return static_cast<AiFlags4>(~static_cast<std::uint8_t>(a));
    }
    constexpr AiFlags4& operator|=(AiFlags4& a, AiFlags4 b) noexcept { return a = a | b; }
    constexpr AiFlags4& operator&=(AiFlags4& a, AiFlags4 b) noexcept { return a = a & b; }

#ifndef MPHREAD_PLAYER_AI_MEMBERS
#define MPHREAD_PLAYER_AI_MEMBERS \
public: \
    class PlayerAiData; \
    std::shared_ptr<PlayerAiData> AiData{}; \
    std::shared_ptr<::MphRead::Formats::NodeData3> ClosestNode{}; \
    [[nodiscard]] std::int32_t BotLevel() const noexcept { return _botLevel; } \
    void SetBotLevel(std::int32_t value) noexcept { _botLevel = value; } \
private: \
    std::int32_t _botLevel = 0;
#endif

}

#ifndef MPHREAD_PLAYER_ENTITY_CANONICAL_HEADER
#include "PlayerEntity.hpp"
#include "HalfturretEntity.hpp"
#include "../BeamProjectileEntity.hpp"
#include "../BombEntity.hpp"
#include "../CamSeq/CamSeqEntity.hpp"
#include "../DoorEntity.hpp"
#include "../FlagBaseEntity.hpp"
#include "../ForceFieldEntity.hpp"
#include "../ItemInstanceEntity.hpp"
#include "../ItemSpawnEntity.hpp"
#include "../JumpPadEntity.hpp"
#include "../NodeDefenseEntity.hpp"
#include "../OctolithFlagEntity.hpp"
#include "../../Formats/AiPersonality.hpp"
#include "../../Formats/NodeData.hpp"
#include "../../GameState.hpp"
#include "../../Metadata/Metadata.hpp"
#include "../../Metadata/Player.hpp"
#include "../../Scene.hpp"
#include "../../SceneSetup.hpp"
#include "../../Utility/Rng.hpp"

namespace MphRead::Entities
{
    class PlayerEntity::PlayerAiData
    {
    public:
        using Vector3 = ::OpenTK::Mathematics::Vector3;
        using NodeType = ::MphRead::Formats::NodeType;

        explicit PlayerAiData(const std::shared_ptr<PlayerEntity>& player)
            : _player(player), _scene(*player->_scene)
        {
        }

        std::shared_ptr<Formats::AiPersonalityData1> Personality{};
        bool Flags1 = false;
        AiFlags2 Flags2{};
        AiFlags3 Flags3{};
        AiFlags4 Flags4{};
        std::uint16_t HealthThreshold = 0;
        std::uint32_t DamageFromHalfturret = 0;
        std::int32_t Field118 = 0;

    private:
        std::shared_ptr<PlayerEntity> _player;
        Scene& _scene;
        std::shared_ptr<Formats::NodeData> _nodeData{};
        bool _forceDisable = false;

        [[nodiscard]] static constexpr Vector3 WithY(Vector3 value, float y) noexcept
        {
            value.Y = y;
            return value;
        }
        [[nodiscard]] static constexpr Vector3 AddY(Vector3 value, float y) noexcept
        {
            value.Y += y;
            return value;
        }
        [[nodiscard]] static constexpr Vector3 AddZ(Vector3 value, float z) noexcept
        {
            value.Z += z;
            return value;
        }
        [[nodiscard]] static constexpr float LengthSquared(Vector3 value) noexcept
        {
            return value.X * value.X + value.Y * value.Y + value.Z * value.Z;
        }
        [[nodiscard]] static float Length(Vector3 value)
        {
            return std::sqrt(LengthSquared(value));
        }
        [[nodiscard]] static constexpr float DistanceSquared(Vector3 left, Vector3 right) noexcept
        {
            return LengthSquared(left - right);
        }
        [[nodiscard]] static constexpr bool VectorEqual(Vector3 left, Vector3 right) noexcept
        {
            return left.X == right.X && left.Y == right.Y && left.Z == right.Z;
        }
        [[nodiscard]] static constexpr Vector3 ScaleVector(Vector3 value, float scale) noexcept
        {
            return Vector3(value.X * scale, value.Y * scale, value.Z * scale);
        }
        [[nodiscard]] static constexpr Vector3 DivideVector(Vector3 value, float divisor) noexcept
        {
            return Vector3(value.X / divisor, value.Y / divisor, value.Z / divisor);
        }
        [[nodiscard]] static constexpr Vector3 UnitX() noexcept { return Vector3(1.0F, 0.0F, 0.0F); }
        [[nodiscard]] static constexpr Vector3 UnitY() noexcept { return Vector3(0.0F, 1.0F, 0.0F); }
        [[nodiscard]] static constexpr Vector3 UnitZ() noexcept { return Vector3(0.0F, 0.0F, 1.0F); }

        [[nodiscard]] static constexpr float DegreesToRadians(float degrees) noexcept
        {
            return ::OpenTK::Mathematics::MathHelper::DegreesToRadians(degrees);
        }
        [[nodiscard]] static constexpr float RadiansToDegrees(float radians) noexcept
        {
            return ::OpenTK::Mathematics::MathHelper::RadiansToDegrees(radians);
        }

        template <typename T>
        [[nodiscard]] static constexpr bool HasFlag(T value, T flag) noexcept
        {
            using U = std::underlying_type_t<T>;
            return (static_cast<U>(value) & static_cast<U>(flag)) != 0;
        }

        std::vector<std::int32_t> _slotHits = std::vector<std::int32_t>(PlayerEntity::SlotCapacity);
        std::vector<std::int32_t> _slotDamage = std::vector<std::int32_t>(PlayerEntity::SlotCapacity);

        std::shared_ptr<Formats::NodeData3> _node3C{};
        std::shared_ptr<Formats::NodeData3> _node40{};
        std::shared_ptr<Formats::NodeData3> _node44{};
        std::shared_ptr<Formats::NodeData3> _node48{};
        std::array<std::shared_ptr<Formats::NodeData3>, 11> _field4C{};
        std::uint16_t _field78 = 0;
        std::array<std::uint16_t, 10> _field7A{};

        std::shared_ptr<PlayerEntity> _targetPlayer{};
        std::shared_ptr<HalfturretEntity> _targetHalfturret{};
        std::shared_ptr<ItemSpawnEntity> _itemSpawnC4{};
        std::shared_ptr<ItemInstanceEntity> _itemC8{};
        std::shared_ptr<OctolithFlagEntity> _octolithFlagCC{};
        std::shared_ptr<FlagBaseEntity> _flagBaseD0{};
        std::shared_ptr<OctolithFlagEntity> _octolithFlagD4{};
        std::shared_ptr<FlagBaseEntity> _flagBaseD8{};
        std::shared_ptr<OctolithFlagEntity> _octolithFlagDC{};
        std::shared_ptr<FlagBaseEntity> _flagBaseE0{};
        std::shared_ptr<NodeDefenseEntity> _targetDefense{};
        std::shared_ptr<DoorEntity> _targetDoor{};

        std::int32_t _nodeDataSetIndex = 0;
        std::uint8_t _nodeDataSelOff = 0;
        std::uint8_t _nodeDataSelOn = 0;
        const std::vector<std::shared_ptr<Formats::NodeData3>>* _nodeList = nullptr;
        std::array<std::int32_t, 6> _nodeTypeIndex{};

        std::uint32_t _field102C = 0;
        std::uint32_t _field102E = 0;
        std::uint32_t _field1030 = 0;
        std::uint32_t _field1034 = 0;
        std::int32_t _field116 = 0;
        std::int32_t _field1020 = 0;
        std::int32_t _field1032 = 0;
        Vector3 _fieldA0{};
        Vector3 _fieldAC{};
        Vector3 _fieldB8{};
        Vector3 _field1038{};
        Vector3 _field1048{};
        Vector3 _field1054{};
        std::int32_t _field30 = 0;
        Vector3 _field90{};
        float _field9C = 0.0F;

        std::int32_t _weapon1 = 0;
        std::int32_t _weapon2 = 0;
        std::int32_t _findWeaponIndex = 0;
        std::int32_t _shotDelay = 0;

    public:
        void Reset();
        void InitializeAtLoad();
        void InitializeAtSpawn();
        static void InitializeGlobals();
        static void UpdateVisibilityAndGlobals(Scene& scene);
        void ProcessInput();
        void Process();
        void OnTakeDamage(std::int32_t damage, EntityBase& source,
            const std::shared_ptr<PlayerEntity>& attacker);

        enum class AiEntRefType : std::int32_t
        {
            Type0=0,Type1=1,Type2=2,Type3=3,Type4=4,Type5=5,Type6=6,Type7=7,Type8=8,Type9=9,
            Type10=10,Type11=11,Type12=12,Type13=13,Type14=14,Type15=15,Type16=16,Type17=17,Type18=18,Type19=19,
            Type20=20,Type21=21,Type22=22,Type23=23,Type24=24,Type25=25,Type26=26,Type27=27,Type28=28,Type29=29,
            Type30=30,Type31=31,Type32=32,Type33=33,Type34=34,Type35=35,Type36=36,Type37=37,Type38=38,Type39=39,
            Type40=40,Type41=41,Type42=42,Type43=43,Type44=44,Type45=45,Type46=46,Type47=47,Type48=48,Type49=49,
            Type50=50,Type51=51,Type52=52,Type53=53,Type54=54,Type55=55,Type56=56,Type57=57,Type58=58,Type59=59,
            Type60=60,Type61=61,Type62=62,Type63=63,Type64=64,Type65=65,Type66=66,Type67=67,Type68=68,Type69=69,
            Type70=70,Type71=71,Type72=72,Type73=73,Type74=74,Type75=75,Type76=76,Type77=77
        };
        enum class AiQueuedEnt : std::int32_t
        {
            Type0=0,Type1=1,Type2=2,Type3=3,Type4=4,Type5=5,Type6=6,Type7=7,Type8=8,Type9=9,
            Type10=10,Type11=11,Type12=12,Type13=13,Type14=14,Type15=15,Type16=16,Type17=17,Type18=18,Type19=19,
            Type20=20,Type21=21,Type22=22,Type23=23,Type24=24,Type25=25,Type26=26,Type27=27,Type28=28,Type29=29,
            Type30=30,Type31=31,Type32=32,Type33=33,Type34=34,Type35=35,Type36=36,Type37=37,Type38=38,Type39=39,None=40
        };

        void GetOuptut(std::string& sb);
        static std::vector<std::string> GetFuncs1Names(const std::vector<std::int32_t>& ids);
        static std::vector<std::string> GetFuncs3Names(const std::vector<std::int32_t>& ids);
        static std::string GetFuncs3Name(std::int32_t id);
        static std::string GetFuncs4Name(std::int32_t id);
        static std::string GetFuncs2Name(std::int32_t id);

    private:
        static std::string GetFuncs1Name(std::int32_t id);

        class AiButton
        {
        public:
            bool IsDown = false;
            std::int32_t FramesDown = 0;
            std::int32_t FramesUp = 0;

            void Clear() noexcept
            {
                IsDown = false;
                FramesDown = 0;
                FramesUp = 0;
            }
        };

        class AiButtons
        {
        public:
            AiButton Up;
            AiButton Down;
            AiButton Left;
            AiButton Right;
            AiButton A;
            AiButton B;
            AiButton X;
            AiButton Y;
            AiButton L;
            AiButton R;
            AiButton Start;
            AiButton Select;
            std::array<AiButton*, 12> AllButtons;

            AiButtons()
                : AllButtons{&Up, &Down, &Left, &Right, &A, &B, &X, &Y, &L, &R, &Start, &Select}
            {
            }
        };

        class AiTouchButtons
        {
        public:
            AiButton Morph;
            AiButton Unmorph;
            AiButton PowerBeam;
            AiButton Missile;
            AiButton VoltDriver;
            AiButton Battlehammer;
            AiButton Imperialist;
            AiButton Judicator;
            AiButton Magmaul;
            AiButton ShockCoil;
            AiButton OmegaCannon;
            std::array<AiButton*, 11> AllButtons;

            AiTouchButtons()
                : AllButtons{&Morph, &Unmorph, &PowerBeam, &Missile, &VoltDriver,
                      &Battlehammer, &Imperialist, &Judicator, &Magmaul, &ShockCoil, &OmegaCannon}
            {
            }
        };

        AiButtons _buttons{};
        AiTouchButtons _touchButtons{};
        std::uint16_t _touchAimX = 0;
        std::uint16_t _touchAimY = 0;
        bool _hasTouch = false;
        std::uint16_t _framesWithTouch = 0;
        std::uint16_t _framesWithoutTouch = 0;
        float _buttonAimX = 0.0F;
        float _buttonAimY = 0.0F;

    public:
        class AiGlobals
        {
        public:
            std::shared_ptr<PlayerEntity> Player{};
            std::int32_t Field4 = 0;
            std::int32_t NodeDataIndex = 0;
            const std::array<std::shared_ptr<Formats::NodeData3>, 11>* NodeData = nullptr;
        };

    private:
        static std::int32_t _globalField0;
        static std::int32_t _globalField2;
        static std::vector<AiGlobals> _globalObjs;
        static std::vector<std::vector<bool>> _playerVisibility;
        static std::uint8_t _visIndex1;
        static std::uint8_t _visIndex2;

        static const std::array<std::array<std::uint32_t, 8>, 4> _botLevelRandomValues1;
        static const std::array<std::uint32_t, 4> _botLevelRandomValues2;

        void ClearInput();
        void InitializeSub();
        void InitializeMain();
        static void UpdateVisibility(Scene& scene);
        static void UpdateGlobals(Scene& scene);

        class AiPlayerAggro
        {
        public:
            std::uint8_t VarA2 = 0;
            std::uint8_t VarA9 = 0;
            std::uint8_t VarA3 = 0;
            std::uint8_t VarA4 = 0;
            std::uint8_t VarA10 = 0;
            std::uint16_t VarA7 = 0;
            std::uint16_t Staleness = 0;
            std::uint16_t Expiration = 0;
            std::shared_ptr<PlayerEntity> Player1{};
            std::shared_ptr<PlayerEntity> Player2{};

            void Clear() noexcept
            {
                VarA2 = 0;
                VarA9 = 0;
                VarA3 = 0;
                VarA4 = 0;
                VarA10 = 0;
                VarA7 = 0;
                Staleness = 0;
                Expiration = 0;
                Player1.reset();
                Player2.reset();
            }
        };

        std::int32_t _playerAggroCount = 0;
        std::array<AiPlayerAggro, 25> _playerAggro{};

        void Func2134594();
        void UpdateAggro();
        [[nodiscard]] bool IsPlayerVisible(const PlayerEntity& player, const PlayerEntity& other) const;

        [[nodiscard]] std::int32_t AggroFunc2148394(std::int32_t a2, std::int32_t a3, std::int32_t a4,
            const std::shared_ptr<PlayerEntity>& player1, const std::shared_ptr<PlayerEntity>& player2)
        {
            std::int32_t result = 0;
            for (std::int32_t i = 0; i < _playerAggroCount; ++i)
            {
                const AiPlayerAggro& aggro = _playerAggro[static_cast<std::size_t>(i)];
                if ((a2 == aggro.VarA2 || a2 == 7)
                    && (a3 == aggro.VarA3 || a3 == 7)
                    && (a4 == aggro.VarA4 || a4 == 7)
                    && (player1 == aggro.Player1 || a3 != 2)
                    && (player2 == aggro.Player2 || a4 != 2))
                {
                    result += aggro.VarA7;
                }
            }
            return result;
        }

        [[nodiscard]] AiPlayerAggro* AggroFunc214847C(std::int32_t a2, std::int32_t a3, std::int32_t a4,
            const std::shared_ptr<PlayerEntity>& player1, const std::shared_ptr<PlayerEntity>& player2)
        {
            AiPlayerAggro* result = nullptr;
            std::int32_t maxField4 = -1;
            for (std::int32_t i = 0; i < _playerAggroCount; ++i)
            {
                AiPlayerAggro& aggro = _playerAggro[static_cast<std::size_t>(i)];
                if ((a2 == aggro.VarA2 || a2 == 7)
                    && (a3 == aggro.VarA3 || a3 == 7)
                    && (a4 == aggro.VarA4 || a4 == 7)
                    && (player1 == aggro.Player1 || a3 != 2)
                    && (player2 == aggro.Player2 || a4 != 2)
                    && aggro.Staleness > maxField4)
                {
                    result = &aggro;
                    maxField4 = aggro.Staleness;
                }
            }
            return result;
        }

        [[nodiscard]] bool AggroFunc214857C(std::int32_t a2, std::int32_t a3, std::int32_t a4,
            const std::shared_ptr<PlayerEntity>& player1, const std::shared_ptr<PlayerEntity>& player2) const
        {
            for (std::int32_t i = 0; i < _playerAggroCount; ++i)
            {
                const AiPlayerAggro& aggro = _playerAggro[static_cast<std::size_t>(i)];
                if ((a2 == aggro.VarA2 || a2 == 7)
                    && (a3 == aggro.VarA3 || a3 == 7)
                    && (a4 == aggro.VarA4 || a4 == 7)
                    && (player1 == aggro.Player1 || a3 != 2)
                    && (player2 == aggro.Player2 || a4 != 2))
                {
                    return true;
                }
            }
            return false;
        }

        [[nodiscard]] AiPlayerAggro* AggroFunc21489B4(std::int32_t a2, std::int32_t a3, std::int32_t a4,
            const std::shared_ptr<PlayerEntity>& player1, const std::shared_ptr<PlayerEntity>& player2)
        {
            for (std::int32_t i = 0; i < _playerAggroCount; ++i)
            {
                AiPlayerAggro& aggro = _playerAggro[static_cast<std::size_t>(i)];
                if (aggro.VarA10 != 1
                    && (a2 == aggro.VarA2 || a2 == 7)
                    && (a3 == aggro.VarA3 || a3 == 7)
                    && (a4 == aggro.VarA4 || a4 == 7)
                    && (player1 == aggro.Player1 || a3 != 2)
                    && (player2 == aggro.Player2 || a4 != 2))
                {
                    return &aggro;
                }
            }
            return nullptr;
        }

        void AggroFunc214864C(std::int32_t a2, std::int32_t a3, std::int32_t a4,
            const std::shared_ptr<PlayerEntity>& player1, const std::shared_ptr<PlayerEntity>& player2,
            std::int32_t a7, std::int32_t a8, std::int32_t a9, std::int32_t a10)
        {
            if (a10 == 2)
            {
                AiPlayerAggro* aggro = AggroFunc21489B4(a2, a3, a4, player1, player2);
                if (aggro != nullptr)
                {
                    aggro->Expiration = static_cast<std::uint16_t>(aggro->Expiration + static_cast<std::uint16_t>(a8));
                    if (aggro->Expiration > 54000)
                    {
                        aggro->Expiration = 54000;
                    }
                    if (a9 > aggro->VarA9)
                    {
                        aggro->VarA9 = static_cast<std::uint8_t>(a9 & 0xF);
                    }
                    aggro->VarA7 = static_cast<std::uint16_t>(aggro->VarA7 + static_cast<std::uint16_t>(a7));
                    if (aggro->VarA7 > 4000)
                    {
                        aggro->VarA7 = 4000;
                    }
                    return;
                }
            }
            else if (a10 == 3)
            {
                AiPlayerAggro* aggro = AggroFunc21489B4(a2, a3, a4, player1, player2);
                if (aggro != nullptr)
                {
                    aggro->VarA2 = static_cast<std::uint8_t>(a2 & 0xF);
                    aggro->VarA9 = static_cast<std::uint8_t>(a9 & 0xF);
                    aggro->VarA3 = static_cast<std::uint8_t>(a3 & 0xF);
                    aggro->VarA4 = static_cast<std::uint8_t>(a4 & 0xF);
                    aggro->VarA10 = 3;
                    aggro->VarA7 = static_cast<std::uint8_t>(a7 & 0xF);
                    aggro->Staleness = 0;
                    aggro->Expiration = static_cast<std::uint16_t>(a8);
                    aggro->Player1 = player1;
                    aggro->Player2 = player2;
                    return;
                }
            }
            std::int32_t index = _playerAggroCount;
            if (index >= static_cast<std::int32_t>(_playerAggro.size()))
            {
                std::int32_t min = a9;
                for (std::int32_t i = 0; i < static_cast<std::int32_t>(_playerAggro.size()); ++i)
                {
                    AiPlayerAggro& aggro = _playerAggro[static_cast<std::size_t>(i)];
                    if (aggro.VarA9 < min)
                    {
                        index = i;
                        min = aggro.VarA9;
                    }
                }
            }
            else
            {
                ++_playerAggroCount;
            }
            if (index < static_cast<std::int32_t>(_playerAggro.size()))
            {
                AiPlayerAggro& aggro = _playerAggro[static_cast<std::size_t>(index)];
                aggro.VarA2 = static_cast<std::uint8_t>(a2 & 0xF);
                aggro.VarA9 = static_cast<std::uint8_t>(a9 & 0xF);
                aggro.VarA3 = static_cast<std::uint8_t>(a3 & 0xF);
                aggro.VarA4 = static_cast<std::uint8_t>(a4 & 0xF);
                aggro.VarA10 = static_cast<std::uint8_t>(a10 & 0xF);
                aggro.VarA7 = static_cast<std::uint8_t>(a7 & 0xF);
                aggro.Staleness = 0;
                aggro.Expiration = static_cast<std::uint16_t>(a8);
                aggro.Player1 = player1;
                aggro.Player2 = player2;
            }
        }

        void UpdateAggroExpiration()
        {
            std::int32_t i = 0;
            while (i < _playerAggroCount)
            {
                AiPlayerAggro& aggro = _playerAggro[static_cast<std::size_t>(i)];
                ++aggro.Staleness;
                if (aggro.Staleness > aggro.Expiration * 2)
                {
                    AiPlayerAggro& last = _playerAggro[static_cast<std::size_t>(_playerAggroCount - 1)];
                    aggro.VarA2 = last.VarA2;
                    aggro.VarA9 = last.VarA9;
                    aggro.VarA3 = last.VarA3;
                    aggro.VarA4 = last.VarA4;
                    aggro.Staleness = last.Staleness;
                    aggro.Expiration = last.Expiration;
                    aggro.Player1 = last.Player1;
                    aggro.Player2 = last.Player2;
                    --_playerAggroCount;
                }
                else
                {
                    ++i;
                }
            }
        }

        void Func2148ABC();

        class AiContext
        {
        public:
            std::int32_t Func24Id = 0;
            std::uint8_t Field4 = 0;
            std::uint8_t Field5 = 0;
            std::uint8_t Field6 = 0;
            std::uint8_t Field7 = 0;
            std::uint8_t Field8 = 0;
            std::uint8_t Field9 = 0;
            std::uint8_t FieldA = 0;
            std::uint8_t FieldB = 0;
            std::uint8_t FieldC = 0;
            std::uint8_t FieldD = 0;
            std::uint8_t FieldE = 0;
            std::uint8_t FieldF = 0;
            std::uint8_t Field10 = 0;
            std::int32_t Field14 = 0;
            std::int32_t Field18 = 0;
            std::int32_t Field1C = 0;
            std::int32_t Field20 = 0;
            std::int32_t Field24 = 0;
            bool Field28 = false;
            std::int32_t Field2C = 0;
            bool Field30 = false;
            Vector3 Field34{};
            std::int32_t Field40 = 0;
            std::int32_t Field44 = 0;
            std::shared_ptr<Formats::AiPersonalityData1> Data1{};
            std::int32_t CallCount = 0;
            std::int32_t Depth = 0;
            std::array<std::int32_t, 21> Weights{};

            void Clear() noexcept
            {
                Func24Id = 0;
                Field4 = Field5 = Field6 = Field7 = Field8 = Field9 = 0;
                FieldA = FieldB = FieldC = FieldD = FieldE = FieldF = Field10 = 0;
                Field14 = Field18 = Field1C = Field20 = Field24 = 0;
                Field28 = false;
                Field2C = 0;
                Field30 = false;
                Field34 = Vector3{};
                Field40 = 0;
                Field44 = 0;
                Data1.reset();
                CallCount = 0;
                Depth = 0;
                Weights.fill(0);
            }
        };

        static constexpr std::int32_t _maxContextDepth = 20;
        std::array<std::shared_ptr<AiContext>, _maxContextDepth> _executionTree{};
        static const std::array<std::int32_t, 3> _func4Ids;

        void UpdateExecutionPath(const std::shared_ptr<Formats::AiPersonalityData1>& data1, std::int32_t depth);
        void Execute(AiContext& context);
        [[nodiscard]] std::int32_t UpdatePathWeights(AiContext& context);

        [[nodiscard]] Vector3 ExecuteVectorFunc(std::int32_t index, bool clearY, bool normalize)
        {
            Vector3 result{};
            switch (index)
            {
            case 0: result = Func213A470(); break;
            case 1: result = Func213A458(); break;
            case 2: result = Func213A3DC(); break;
            case 3: result = Func213A3C0(); break;
            case 4: result = Func213A3A8(); break;
            case 5: result = Func213A37C(); break;
            case 6: result = Func213A35C(); break;
            case 7: result = Func213A31C(); break;
            default: throw std::runtime_error("Invalid AI vector func.");
            }
            if (clearY)
            {
                result = WithY(result, 0.0F);
            }
            if (normalize)
            {
                result = !VectorEqual(result, Vector3{}) ? result.Normalized() : Vector3(1.0F, 0.0F, 0.0F);
            }
            return result;
        }

        [[nodiscard]] Vector3 Func213A470()
        {
            assert(_targetPlayer != nullptr);
            return static_cast<Vector3>(_targetPlayer->Position) - static_cast<Vector3>(_player->Position);
        }

        [[nodiscard]] Vector3 Func213A458() const
        {
            return _player->_facingVector;
        }

        [[nodiscard]] Vector3 Func213A3DC()
        {
            assert(_targetPlayer != nullptr);
            Vector3 targetPos{};
            _targetPlayer->GetPosition(targetPos);
            targetPos = AddY(targetPos, _targetPlayer->IsAltForm()
                ? Fixed::ToFloat(_targetPlayer->Values().AltColYPos) : 0.5F);
            return targetPos - _player->_muzzlePos;
        }

        [[nodiscard]] Vector3 Func213A3C0() const
        {
            return _player->_aimPosition - _player->_muzzlePos;
        }

        [[nodiscard]] Vector3 Func213A3A8()
        {
            assert(_targetPlayer != nullptr);
            return _targetPlayer->_facingVector;
        }

        [[nodiscard]] Vector3 Func213A37C()
        {
            assert(_node40 != nullptr);
            return static_cast<Vector3>(_node40->Position) - static_cast<Vector3>(_player->Position);
        }

        [[nodiscard]] Vector3 Func213A35C() const
        {
            return _player->CameraInfo()->Facing;
        }

        [[nodiscard]] Vector3 Func213A31C()
        {
            FindEntityRef(AiEntRefType::Type26);
            assert(_entityRefs.Field26 != nullptr);
            return static_cast<Vector3>(_entityRefs.Field26->Position) - static_cast<Vector3>(_player->Position);
        }

        void ExecuteFuncs1(const std::vector<std::int32_t>& funcsIds);
        void ExecuteFuncs2(AiContext& context);
        [[nodiscard]] std::int32_t ExecuteFuncs3(AiContext& context, std::int32_t funcId,
            const Formats::AiPersonalityData5& param);
        void ExecuteFuncs4(AiContext& context);

        [[nodiscard]] bool CheckBeam(BeamType beam)
        {
            const WeaponInfo& info = *(*Weapons::Current)[static_cast<std::size_t>(beam)];
            return _player->_ammo[info.AmmoType] >= info.AmmoCost && _player->_availableWeapons[beam];
        }

        [[nodiscard]] bool CheckCharge(BeamType beam)
        {
            const WeaponInfo& info = *(*Weapons::Current)[static_cast<std::size_t>(beam)];
            return HasFlag(info.Flags, WeaponFlags::CanCharge)
                && _player->_ammo[info.AmmoType] >= info.ChargeCost && _player->_availableCharges[beam];
        }

        void Func1_214A39C()
        {
            _weapon1 = 0;
            Flags4 &= ~AiFlags4::Bit1;
            if (CheckBeam(BeamType::Magmaul)) _weapon1 = 6;
            else if (CheckBeam(BeamType::Judicator)) _weapon1 = 5;
            else if (CheckBeam(BeamType::Imperialist)) _weapon1 = 4;
            else if (CheckBeam(BeamType::Battlehammer)) _weapon1 = 3;
            else if (CheckBeam(BeamType::VoltDriver)) _weapon1 = 2;
            else if (CheckBeam(BeamType::ShockCoil)) _weapon1 = 7;
            else if (CheckBeam(BeamType::Missile)) _weapon1 = 1;
            if (CheckCharge(GetBeamType(_weapon1))) Flags4 |= AiFlags4::Bit1;
        }

        void Func1_214A098()
        {
            BeamType affinityWeapon = Weapons::AffinityWeapons[static_cast<std::size_t>(_player->Hunter())];
            std::int32_t affinityIndex = GetWeaponIndex(affinityWeapon);
            std::int32_t seenBits = 1 | 2;
            bool sawAffinity = false;
            std::int32_t seenCount = 0;
            std::array<std::int32_t, 7> seenItems{};
            for (auto _enumerator0 = _scene.GetItemSpawnEntities().GetEnumerator(); _enumerator0.MoveNext(); )
            if (const auto itemSpawn = _enumerator0.Current(); true)
            {
                if (seenBits == 255) break;
                ItemType type = itemSpawn->Data().ItemType;
                if (type == ItemType::AffinityWeapon) type = static_cast<ItemType>(affinityWeapon);
                std::int32_t index = 0;
                switch (type)
                {
                case ItemType::VoltDriver: index = 2; break;
                case ItemType::Battlehammer: index = 3; break;
                case ItemType::Imperialist: index = 4; break;
                case ItemType::Judicator: index = 5; break;
                case ItemType::Magmaul: index = 6; break;
                case ItemType::ShockCoil: index = 7; break;
                case ItemType::OmegaCannon: index = 8; break;
                default: index = 0; break;
                }
                if ((seenBits & (1 << index)) == 0)
                {
                    seenBits |= 1 << index;
                    if (!_player->AvailableWeapons()[GetBeamType(index)])
                    {
                        seenItems[static_cast<std::size_t>(seenCount++)] = index;
                        if (index == affinityIndex) sawAffinity = true;
                    }
                }
            }
            std::int32_t randIdx = static_cast<std::int32_t>(Rng::GetRandomInt2(seenCount * (sawAffinity ? 2 : 1)));
            _findWeaponIndex = randIdx < seenCount ? seenItems[static_cast<std::size_t>(randIdx)] : affinityIndex;
        }

        void Func1_2149D3C()
        {
            if (CheckBeam(BeamType::OmegaCannon))
            {
                _weapon2 = 8;
                return;
            }
            std::int32_t affinityIndex = 0;
            std::int32_t candidateCount = 0;
            std::array<std::int32_t, 8> candidates{};
            bool includeMissile = Weapons::AffinityWeapons[static_cast<std::size_t>(_player->Hunter())] == BeamType::Missile;
            for (std::int32_t i = includeMissile ? 1 : 2; i < 9; ++i)
            {
                BeamType beam = GetBeamType(i);
                if (CheckBeam(beam))
                {
                    candidates[static_cast<std::size_t>(candidateCount++)] = i;
                    if (Weapons::AffinityWeapons[static_cast<std::size_t>(_player->Hunter())] == beam) affinityIndex = i;
                }
            }
            if (candidateCount == 1)
            {
                candidates[0] = 1;
                candidates[1] = 0;
                candidateCount = 2;
            }
            else if (candidateCount == 0)
            {
                candidates[0] = 0;
                candidateCount = 1;
                if (CheckBeam(BeamType::Missile))
                {
                    candidates[1] = 1;
                    candidateCount = 2;
                }
            }
            std::int32_t randIdx = static_cast<std::int32_t>(Rng::GetRandomInt2(candidateCount * (affinityIndex != 0 ? 2 : 1)));
            std::int32_t noChargeChanceOneIn;
            if (randIdx < candidateCount)
            {
                _weapon2 = candidates[static_cast<std::size_t>(randIdx)];
                noChargeChanceOneIn = 2;
            }
            else
            {
                _weapon2 = affinityIndex;
                noChargeChanceOneIn = 4;
            }
            if (CheckCharge(GetBeamType(_weapon2)) && _player->BotLevel() > 0)
            {
                if (Rng::GetRandomInt2(noChargeChanceOneIn) == 0) Flags4 &= ~AiFlags4::Bit1;
                else Flags4 |= AiFlags4::Bit1;
            }
        }

        [[nodiscard]] static BeamType GetBeamType(std::int32_t weapon)
        {
            if (weapon == 1) return BeamType::Missile;
            if (weapon == 2) return BeamType::VoltDriver;
            return static_cast<BeamType>(weapon);
        }

        [[nodiscard]] static std::int32_t GetWeaponIndex(BeamType beam)
        {
            if (beam == BeamType::Missile) return 1;
            if (beam == BeamType::VoltDriver) return 2;
            return static_cast<std::int32_t>(beam);
        }

        void Func1_2149C98() { _weapon1 = CheckBeam(GetBeamType(_weapon2)) ? _weapon2 : 0; }
        void Func1_2149C80() { _weapon1 = 0; Flags4 &= ~AiFlags4::Bit1; }
        void Func1_2149C68() { _weapon1 = 2; Flags4 &= ~AiFlags4::Bit1; }
        void Func1_2149C50() { _weapon1 = 2; Flags4 |= AiFlags4::Bit1; }
        void Func1_2149C38() { _weapon1 = 5; Flags4 &= ~AiFlags4::Bit1; }
        void Func1_2149C20() { _weapon1 = 5; Flags4 |= AiFlags4::Bit1; }
        void Func1_2149C08() { _weapon1 = 4; Flags4 &= ~AiFlags4::Bit1; }
        void Func1_2149BF0() { _weapon1 = 6; Flags4 &= ~AiFlags4::Bit1; }
        void Func1_2149BD8() { _weapon1 = 6; Flags4 |= AiFlags4::Bit1; }
        void Func1_2149BC0() { _weapon1 = 7; Flags4 &= ~AiFlags4::Bit1; }
        void Func1_2149BA8() { _weapon1 = 3; Flags4 &= ~AiFlags4::Bit1; }
        void Func1_2149B98() { Flags4 |= AiFlags4::Bit1; }

        [[nodiscard]] bool CanChargeWeapon()
        {
            const WeaponInfo& weapon = _player->EquipWeapon();
            return HasFlag(weapon.Flags, WeaponFlags::CanCharge) && _player->_availableCharges[_player->CurrentWeapon()]
                && (_player->_ammo[weapon.AmmoType] >= weapon.ChargeCost || _player->_ammo[weapon.AmmoType] == -1);
        }

        void Func1_2149AD8()
        {
            if (_buttons.R.FramesDown == 0 || _player->IsAltForm()) return;
            if (CanChargeWeapon()) _buttons.R.IsDown = true;
        }
        void Func1_2149AC8() { Flags4 |= AiFlags4::Bit2; }
        void Func1_2149ABC() { _octolithFlagCC = _octolithFlagD4; }
        void Func1_2149AB0() { _octolithFlagCC = _octolithFlagDC; }
        void Func1_2149AA4() { _flagBaseD0 = _flagBaseD8; }
        void Func1_2149A98() { _flagBaseD0 = _flagBaseE0; }

        void Func1_2149A64()
        {
            FindEntityRef(AiEntRefType::Type54);
            UpdateTargetItem(_entityRefs.Field54);
        }

        void Func1_2149824()
        {
            switch (_findWeaponIndex)
            {
            case 1:
                if (_player->_availableWeapons[BeamType::Missile]) { FindEntityRef(AiEntRefType::Type57); UpdateTargetItem(_entityRefs.Field57); }
                else { FindEntityRef(AiEntRefType::Type56); UpdateTargetItem(_entityRefs.Field56); }
                break;
            case 2:
                if (_player->_availableWeapons[BeamType::VoltDriver]) { FindEntityRef(AiEntRefType::Type59); UpdateTargetItem(_entityRefs.Field59); }
                else { FindEntityRef(AiEntRefType::Type58); UpdateTargetItem(_entityRefs.Field58); }
                break;
            case 3:
                if (_player->_availableWeapons[BeamType::Battlehammer]) { FindEntityRef(AiEntRefType::Type60); UpdateTargetItem(_entityRefs.Field60); }
                else { FindEntityRef(AiEntRefType::Type61); UpdateTargetItem(_entityRefs.Field61); }
                break;
            case 4:
                if (_player->_availableWeapons[BeamType::Imperialist]) { FindEntityRef(AiEntRefType::Type63); UpdateTargetItem(_entityRefs.Field63); }
                else { FindEntityRef(AiEntRefType::Type62); UpdateTargetItem(_entityRefs.Field62); }
                break;
            case 5:
                if (_player->_availableWeapons[BeamType::Judicator]) { FindEntityRef(AiEntRefType::Type65); UpdateTargetItem(_entityRefs.Field65); }
                else { FindEntityRef(AiEntRefType::Type64); UpdateTargetItem(_entityRefs.Field64); }
                break;
            case 6:
                if (_player->_availableWeapons[BeamType::Magmaul]) { FindEntityRef(AiEntRefType::Type67); UpdateTargetItem(_entityRefs.Field67); }
                else { FindEntityRef(AiEntRefType::Type66); UpdateTargetItem(_entityRefs.Field66); }
                break;
            case 7:
                if (_player->_availableWeapons[BeamType::ShockCoil]) { FindEntityRef(AiEntRefType::Type69); UpdateTargetItem(_entityRefs.Field69); }
                else { FindEntityRef(AiEntRefType::Type68); UpdateTargetItem(_entityRefs.Field68); }
                break;
            case 8:
                if (_player->_availableWeapons[BeamType::OmegaCannon]) { FindEntityRef(AiEntRefType::Type71); UpdateTargetItem(_entityRefs.Field71); }
                else { FindEntityRef(AiEntRefType::Type70); UpdateTargetItem(_entityRefs.Field70); }
                break;
            default: UpdateTargetItem(nullptr); break;
            }
        }

        void Func1_21497F0() { FindEntityRef(AiEntRefType::Type55); UpdateTargetItem(_entityRefs.Field55); }
        void Func1_2149570() { FindEntityRef(AiEntRefType::Type56); UpdateTargetItem(_entityRefs.Field56); }
        void Func1_21494FC() { FindEntityRef(AiEntRefType::Type57); UpdateTargetItem(_entityRefs.Field57); }
        void Func1_2149488() { FindEntityRef(AiEntRefType::Type58); UpdateTargetItem(_entityRefs.Field58); }
        void Func1_2149414() { FindEntityRef(AiEntRefType::Type59); UpdateTargetItem(_entityRefs.Field59); }
        void Func1_21493A0() { FindEntityRef(AiEntRefType::Type72); UpdateTargetItem(_entityRefs.Field72); }
        void Func1_214932C() { FindEntityRef(AiEntRefType::Type73); UpdateTargetItem(_entityRefs.Field73); }

        void Func1_21495A4()
        {
            auto setEntity = [this](const std::shared_ptr<ItemSpawnEntity>& entity)
            {
                _itemSpawnC4 = entity != nullptr && entity->Item() != nullptr ? entity : nullptr;
            };
            switch (_findWeaponIndex)
            {
            case 1:
                if (_player->_availableWeapons[BeamType::Missile]) { FindEntityRef(AiEntRefType::Type37); setEntity(_entityRefs.Field37); }
                else { FindEntityRef(AiEntRefType::Type36); setEntity(_entityRefs.Field36); }
                break;
            case 2:
                if (_player->_availableWeapons[BeamType::VoltDriver]) { FindEntityRef(AiEntRefType::Type39); setEntity(_entityRefs.Field39); }
                else { FindEntityRef(AiEntRefType::Type38); setEntity(_entityRefs.Field38); }
                break;
            case 3:
                if (_player->_availableWeapons[BeamType::Battlehammer]) { FindEntityRef(AiEntRefType::Type41); setEntity(_entityRefs.Field41); }
                else { FindEntityRef(AiEntRefType::Type40); setEntity(_entityRefs.Field40); }
                break;
            case 4:
                if (_player->_availableWeapons[BeamType::Imperialist]) { FindEntityRef(AiEntRefType::Type43); setEntity(_entityRefs.Field43); }
                else { FindEntityRef(AiEntRefType::Type42); setEntity(_entityRefs.Field42); }
                break;
            case 5:
                if (_player->_availableWeapons[BeamType::Judicator]) { FindEntityRef(AiEntRefType::Type45); setEntity(_entityRefs.Field45); }
                else { FindEntityRef(AiEntRefType::Type44); setEntity(_entityRefs.Field44); }
                break;
            case 6:
                if (_player->_availableWeapons[BeamType::Magmaul]) { FindEntityRef(AiEntRefType::Type47); setEntity(_entityRefs.Field47); }
                else { FindEntityRef(AiEntRefType::Type46); setEntity(_entityRefs.Field46); }
                break;
            case 7:
                if (_player->_availableWeapons[BeamType::ShockCoil]) { FindEntityRef(AiEntRefType::Type49); setEntity(_entityRefs.Field49); }
                else { FindEntityRef(AiEntRefType::Type48); setEntity(_entityRefs.Field48); }
                break;
            case 8:
                if (_player->_availableWeapons[BeamType::ShockCoil]) { FindEntityRef(AiEntRefType::Type51); setEntity(_entityRefs.Field51); }
                else { FindEntityRef(AiEntRefType::Type50); setEntity(_entityRefs.Field50); }
                break;
            default: _itemSpawnC4.reset(); break;
            }
        }

        void Func1_2149530() { FindEntityRef(AiEntRefType::Type36); _itemSpawnC4 = _entityRefs.Field36 && _entityRefs.Field36->Item() ? _entityRefs.Field36 : nullptr; }
        void Func1_21494BC() { FindEntityRef(AiEntRefType::Type37); _itemSpawnC4 = _entityRefs.Field37 && _entityRefs.Field37->Item() ? _entityRefs.Field37 : nullptr; }
        void Func1_2149448() { FindEntityRef(AiEntRefType::Type38); _itemSpawnC4 = _entityRefs.Field38 && _entityRefs.Field38->Item() ? _entityRefs.Field38 : nullptr; }
        void Func1_21493D4() { FindEntityRef(AiEntRefType::Type39); _itemSpawnC4 = _entityRefs.Field39 && _entityRefs.Field39->Item() ? _entityRefs.Field39 : nullptr; }
        void Func1_2149360() { FindEntityRef(AiEntRefType::Type52); _itemSpawnC4 = _entityRefs.Field52 && _entityRefs.Field52->Item() ? _entityRefs.Field52 : nullptr; }
        void Func1_21492EC() { FindEntityRef(AiEntRefType::Type53); _itemSpawnC4 = _entityRefs.Field53 && _entityRefs.Field53->Item() ? _entityRefs.Field53 : nullptr; }

        void Func1_21492DC()
        {
            if (PlayerEntity::Main() == nullptr) Flags2 |= AiFlags2::Bit9;
            else { Flags2 &= ~AiFlags2::Bit9; Func21356C0(PlayerEntity::Main()); }
        }
        void Func1_21492CC() { FindEntityRef(AiEntRefType::Type27); Func21356C0(_entityRefs.Field27); }
        void Func1_21492BC() { Func2135510(); }
        void Func1_21492AC() { Func21354E0(); }
        void Func1_214929C() { Func2135510(); }
        void Func1_214928C() { Func21354B0(); }
        void Func1_214927C() { Func2135380(); }
        void Func1_214926C() { Func2135480(); }
        void Func1_214925C()
        {
            if (GameState::PrimeHunter() == -1 || _player->SlotIndex() == GameState::PrimeHunter())
            {
                FindEntityRef(AiEntRefType::Type32);
                Func21356C0(_entityRefs.Field32);
            }
            else
            {
                Flags2 &= ~AiFlags2::Bit9;
                Func21356C0(PlayerEntity::Players().at(static_cast<std::size_t>(GameState::PrimeHunter())));
            }
        }
        void Func1_214924C() { Func21354B0(); }
        void Func1_214923C()
        {
            assert(_octolithFlagCC != nullptr);
            if (_octolithFlagCC->Carrier() != nullptr && _octolithFlagCC->Carrier()->Health() != 0)
            {
                Func21356C0(_octolithFlagCC->Carrier());
                Flags2 &= ~AiFlags2::Bit9;
            }
            else Flags2 |= AiFlags2::Bit9;
        }
        void Func1_214922C()
        {
            FindEntityRef(AiEntRefType::Type33);
            if (_entityRefs.Field33 != nullptr) Flags2 |= AiFlags2::TargetHalfturret;
            else Flags2 &= ~AiFlags2::TargetHalfturret;
            if (_entityRefs.Field33 != _targetHalfturret)
            {
                _targetHalfturret = _entityRefs.Field33;
                _entityRefs.Field3.reset();
            }
        }
        void Func1_214921C() { FindEntityRef(AiEntRefType::Type75); Func2135624(_entityRefs.Field75); }
        void Func1_214920C() { FindEntityRef(AiEntRefType::Type76); Func2135624(_entityRefs.Field76); }
        void Func1_21491FC() { Func21355D8(); }

        void Func1_21491E4() { _queuedFindEntityAction = AiQueuedEnt::Type0; Flags2 |= AiFlags2::Bit1; }
        void Func1_21491CC() { _queuedFindEntityAction = AiQueuedEnt::Type1; Flags2 |= AiFlags2::Bit1; }
        void Func1_21491B4() { _queuedFindEntityAction = AiQueuedEnt::Type2; Flags2 |= AiFlags2::Bit1; }
        void Func1_214919C() { _queuedFindEntityAction = AiQueuedEnt::Type3; Flags2 |= AiFlags2::Bit1; }
        void Func1_2149184() { _queuedFindEntityAction = AiQueuedEnt::Type5; Flags2 |= AiFlags2::Bit1; }
        void Func1_214916C() { _queuedFindEntityAction = AiQueuedEnt::Type6; Flags2 |= AiFlags2::Bit1; }
        void Func1_2149154() { _queuedFindEntityAction = AiQueuedEnt::Type7; Flags2 |= AiFlags2::Bit1; }
        void Func1_214913C() { _queuedFindEntityAction = AiQueuedEnt::Type9; Flags2 |= AiFlags2::Bit1; }
        void Func1_2149124() { _queuedFindEntityAction = AiQueuedEnt::Type10; Flags2 |= AiFlags2::Bit1; }
        void Func1_214910C() { _queuedFindEntityAction = AiQueuedEnt::Type11; Flags2 |= AiFlags2::Bit1; }
        void Func1_21490F4() { _queuedFindEntityAction = AiQueuedEnt::Type12; Flags2 |= AiFlags2::Bit1; }
        void Func1_21490DC() { _queuedFindEntityAction = AiQueuedEnt::Type13; Flags2 |= AiFlags2::Bit1; }
        void Func1_21490C4() { _queuedFindEntityAction = AiQueuedEnt::Type14; Flags2 |= AiFlags2::Bit1; }
        void Func1_21490AC() { _queuedFindEntityAction = AiQueuedEnt::Type15; Flags2 |= AiFlags2::Bit1; }
        void Func1_2149094() { _queuedFindEntityAction = AiQueuedEnt::Type16; Flags2 |= AiFlags2::Bit1; }
        void Func1_2149088() { _field30 = 1; }
        void Func1_2149034()
        {
            const std::int32_t navIndex = _nodeTypeIndex[static_cast<std::size_t>(NodeType::Navigation)];
            const std::int32_t specIndex = _nodeTypeIndex[static_cast<std::size_t>(NodeType::Special)];
            if (navIndex == specIndex) _field30 = 0;
            else
            {
                std::int32_t offset = static_cast<std::int32_t>(Rng::GetRandomInt2(static_cast<std::uint16_t>(specIndex - navIndex)));
                _field30 = static_cast<std::int32_t>((*_nodeList)[static_cast<std::size_t>(navIndex + offset)]->Field4);
            }
        }
        void Func1_2148F10()
        {
            const std::int32_t navIndex = _nodeTypeIndex[static_cast<std::size_t>(NodeType::Navigation)];
            const std::int32_t specIndex = _nodeTypeIndex[static_cast<std::size_t>(NodeType::Special)];
            if (navIndex == specIndex) { _field30 = 0; return; }
            std::int32_t i = 0;
            std::int32_t offsetCount = 0;
            std::array<std::int32_t, 10> offsets{};
            while (offsetCount < 10 && i < specIndex - navIndex)
            {
                if (GameState::Mode() == GameMode::Capture && _player->TeamIndex() == 1)
                {
                    std::uint32_t field4 = (*_nodeList)[static_cast<std::size_t>(navIndex + i)]->Field4;
                    if (field4 > 100 && field4 <= 110) offsets[static_cast<std::size_t>(offsetCount++)] = i;
                }
                else if ((*_nodeList)[static_cast<std::size_t>(navIndex + i)]->Field4 <= 10)
                {
                    offsets[static_cast<std::size_t>(offsetCount++)] = i;
                }
                ++i;
            }
            std::int32_t offset = offsets[static_cast<std::size_t>(Rng::GetRandomInt2(offsetCount))];
            _field30 = static_cast<std::int32_t>((*_nodeList)[static_cast<std::size_t>(navIndex + offset)]->Field4);
        }
        void Func1_2148EDC()
        {
            std::int32_t navIndex = _nodeTypeIndex[static_cast<std::size_t>(NodeType::Navigation)];
            std::int32_t specIndex = _nodeTypeIndex[static_cast<std::size_t>(NodeType::Special)];
            _field30 = specIndex <= navIndex ? 0 : static_cast<std::int32_t>((*_nodeList)[static_cast<std::size_t>(specIndex - 1)]->Field4);
        }
        void Func1_2148ECC() { ++_field30; }
        void Func1_2148EB8() { if (_field30 != 0) --_field30; }
        void Func1_2148EA8() { _field30 += 10; }
        void Func1_2148E98() { _nodeDataSelOn = 255; }
        void Func1_2148E88() { _nodeDataSelOff = 255; }
        void Func1_2148E74() { Flags3 |= AiFlags3::Bit5; }
        void Func1_2148E64() { Flags2 |= AiFlags2::Bit18; }
        void Func1_2148E54() { Flags2 |= AiFlags2::Bit19; }
        void Func1_2148DF8()
        {
            float volume = _player->_soundSource.Volume;
            _player->_soundSource.Volume = 1.0F;
            _player->_soundSource.PlaySfx(SfxId::TRACE_WARNING_SCR, true);
            _player->_soundSource.Volume = volume;
        }
        void Func1_2148DE8() { Flags2 |= AiFlags2::Bit20; }
        void Func1_2148D50()
        {
            for (auto _enumerator1 = _scene.GetCamSeqEntities().GetEnumerator(); _enumerator1.MoveNext(); )
            if (const auto camSeq = _enumerator1.Current(); true)
            {
                if (camSeq->Id == 56) { _scene.SendMessage(Message::Activate, _player.get(), camSeq.get(), std::make_shared<const std::any>(std::int32_t{0}), std::make_shared<const std::any>(std::int32_t{0})); break; }
            }
        }
        void Func1_UnlockEchoHallForceField()
        {
            for (auto _enumerator2 = _scene.GetForceFieldEntities().GetEnumerator(); _enumerator2.MoveNext(); )
            if (const auto forceField = _enumerator2.Current(); true)
            {
                if (forceField->Id == 19) { _scene.SendMessage(Message::Unlock, _player.get(), forceField.get(), std::make_shared<const std::any>(std::int32_t{0}), std::make_shared<const std::any>(std::int32_t{0})); break; }
            }
        }
        void Func1_SetInvulnerable() { Flags3 |= AiFlags3::Invulnerable; }

        void Func2_213EA10(AiContext& context)
        {
            if (!_player->IsAltForm() && _touchButtons.Morph.FramesUp > 10 * 2)
                _touchButtons.Morph.IsDown = true;
        }

        void Func2_213EA48(AiContext& context)
        {
            if (context.FieldD == 28 && _player->IsAltForm() && context.Field4 != 37)
            {
                CheckUnmorph();
                if (context.Field4 == 33) ++Field118;
            }
            else if (context.FieldD == 29 && !_player->IsAltForm() && context.Field4 != 37)
            {
                if (_touchButtons.Morph.FramesUp > 10 * 2) _touchButtons.Morph.IsDown = true;
            }
            Vector3 targetPos{};
            if (_player->IsAltForm() || context.FieldA == 31)
            {
                if (_player->Values().AltFormStrafe != 0 && context.FieldA == 31)
                {
                    if (context.FieldB == 4 && (Flags2 & AiFlags2::TargetPlayer) != AiFlags2::None)
                    {
                        assert(_targetPlayer != nullptr);
                        _targetPlayer->GetPosition(targetPos);
                        targetPos = AddY(targetPos, _targetPlayer->IsAltForm()
                            ? Fixed::ToFloat(_targetPlayer->Values().AltColYPos) : 0.5F);
                        Func2145C14(targetPos);
                    }
                    else if (context.FieldB == 5 && (Flags2 & AiFlags2::TargetHalfturret) != AiFlags2::None)
                    {
                        assert(_targetHalfturret != nullptr);
                        _targetHalfturret->GetPosition(targetPos);
                        Func2145C14(targetPos);
                    }
                }
            }
            else if (context.FieldA == 32)
            {
                if (context.FieldB == 4 && (Flags2 & AiFlags2::TargetPlayer) != AiFlags2::None)
                {
                    assert(_targetPlayer != nullptr);
                    _targetPlayer->GetPosition(targetPos);
                    targetPos = AddY(targetPos, _targetPlayer->IsAltForm()
                        ? Fixed::ToFloat(_targetPlayer->Values().AltColYPos) : 0.5F);
                    _field1038 = targetPos - _player->CameraInfo()->Position;
                    _field1038 = !VectorEqual(_field1038, Vector3{}) ? _field1038.Normalized() : _player->CameraInfo()->Facing;
                }
                else if (context.FieldB == 5 && (Flags2 & AiFlags2::TargetHalfturret) != AiFlags2::None)
                {
                    assert(_targetHalfturret != nullptr);
                    _targetHalfturret->GetPosition(targetPos);
                    _field1038 = targetPos - _player->CameraInfo()->Position;
                    _field1038 = !VectorEqual(_field1038, Vector3{}) ? _field1038.Normalized() : _player->CameraInfo()->Facing;
                }
                else if (context.FieldB == 27)
                {
                    _field1038 = _fieldB8 - _player->CameraInfo()->Position;
                    _field1038 = !VectorEqual(_field1038, Vector3{}) ? _field1038.Normalized() : _player->CameraInfo()->Facing;
                }
                Func21447E8();
            }
            else if (context.FieldC == 55) Func21433A0(_fieldB8);
            else if (context.FieldC == 56)
            {
                if ((Flags2 & AiFlags2::TargetPlayer) != AiFlags2::None) Func21436D8();
                else if ((Flags4 & AiFlags4::Bit1) != AiFlags4::None) Func214380C();
            }
            else if (context.FieldC == 57)
            {
                if ((Flags2 & AiFlags2::TargetPlayer) != AiFlags2::None) Func2143658();
                else if ((Flags4 & AiFlags4::Bit1) != AiFlags4::None) Func214380C();
            }
            else if (context.FieldC == 59)
            {
                if ((Flags2 & AiFlags2::TargetPlayer) != AiFlags2::None) Func21433E4();
                else if ((Flags4 & AiFlags4::Bit1) != AiFlags4::None) Func214380C();
            }
            else if (context.FieldC == 60)
            {
                if ((Flags2 & AiFlags2::TargetHalfturret) != AiFlags2::None) Func2143470();
                else if ((Flags4 & AiFlags4::Bit1) != AiFlags4::None) Func214380C();
            }
            else if (context.FieldC == 61)
            {
                assert(_targetDoor != nullptr);
                _targetDoor->GetPosition(targetPos);
                Func21433A0(targetPos);
            }
            else Func2145BA0();

            if (context.Field4 == 37)
            {
                if (context.FieldD == 29)
                {
                    Func2140094(context);
                    if (_player->Values().AltFormStrafe != 0 && _buttonAimX == 0 && _buttonAimY == 0)
                    {
                        if ((Flags2 & AiFlags2::TargetPlayer) != AiFlags2::None && context.Field9 == 4)
                        {
                            assert(_targetPlayer != nullptr);
                            _targetPlayer->GetPosition(targetPos);
                            targetPos = AddY(targetPos, _targetPlayer->IsAltForm()
                                ? Fixed::ToFloat(_targetPlayer->Values().AltColYPos) : 0.5F);
                            Func2145C14(targetPos);
                        }
                        else
                        {
                            assert(_node40 != nullptr);
                            Func2145C14(_node40->Position);
                        }
                    }
                }
                else if (context.Field5 != 58 || _player->IsAltForm()) Func2140094(context);
                else Func214003C(context);
                if (context.Field6 == 52 && !_player->IsAltForm() && !_player->IsMorphing()
                    && !HasFlag(_player->Flags1(), PlayerFlags1::UsedJump) && _buttons.L.FramesUp > 5 * 2)
                {
                    AiPlayerAggro* aggro = AggroFunc214847C(4, 7, 1, nullptr, nullptr);
                    if (aggro != nullptr && aggro->Staleness < 30 * 2)
                    {
                        assert(_node40 != nullptr);
                        Vector3 toNode = static_cast<Vector3>(_node40->Position) - static_cast<Vector3>(_player->Position);
                        if (LengthSquared(toNode) > 3 * 3) _buttons.L.IsDown = true;
                    }
                }
            }
            else if (context.Field4 == 33
                && (context.Field9 != 4 || (Flags2 & AiFlags2::TargetPlayer) != AiFlags2::None)
                && (context.Field9 != 5 || (Flags2 & AiFlags2::TargetHalfturret) != AiFlags2::None))
            {
                std::optional<Vector3> position;
                if (context.Field9 == 4) { assert(_targetPlayer); position = _targetPlayer->Position; }
                else if (context.Field9 == 5) { assert(_targetHalfturret); position = _targetHalfturret->Position; }
                else if (context.Field9 == 6 && (Flags2 & AiFlags2::TargetItem) != AiFlags2::None)
                { assert(_itemC8); position = targetPos = AddY(_itemC8->Position, -0.5F); }
                else if (context.Field9 == 12 || context.Field9 == 13 || context.Field9 == 39)
                { assert(_node40); position = _node40->Position; }
                else if (context.Field9 == 14) { assert(_octolithFlagCC); position = _octolithFlagCC->Position; }
                else if (context.Field9 == 15) { assert(_octolithFlagCC); position = _octolithFlagCC->BasePosition(); }
                else if (context.Field9 == 16) { assert(_flagBaseD0); position = _flagBaseD0->Position; }
                else if (context.Field9 == 17) { assert(_octolithFlagD4); position = _octolithFlagD4->Position; }
                else if (context.Field9 == 18) { assert(_octolithFlagD4); position = _octolithFlagD4->BasePosition(); }
                else if (context.Field9 == 19) { assert(_flagBaseD8); position = _flagBaseD8->Position; }
                else if (context.Field9 == 20) { assert(_octolithFlagDC); position = _octolithFlagDC->Position; }
                else if (context.Field9 == 21) { assert(_octolithFlagDC); position = _octolithFlagDC->BasePosition(); }
                else if (context.Field9 == 22) { assert(_flagBaseE0); position = _flagBaseE0->Position; }
                else if (context.Field9 == 23 && (Flags2 & AiFlags2::TargetDefense) != AiFlags2::None)
                { assert(_targetDefense); position = _targetDefense->Position; }
                else if (context.Field9 == 35) position = targetPos = AddZ(_player->Position, 1.0F);
                else if (context.Field9 == 36) position = targetPos = AddZ(_player->Position, -1.0F);
                if (position)
                {
                    if (_player->IsAltForm())
                    {
                        if (_player->Values().AltFormStrafe != 0) { Func2145C14(targetPos); Func2142AE8(*position); }
                        else if (context.Field5 != 48 || _player->Hunter() != Hunter::Samus) Func21418D8(*position);
                        else Func2141840(*position);
                    }
                    else
                    {
                        Func2140B18(context, *position);
                        if (context.FieldA != 0 || context.FieldC == 56 || context.FieldC == 57 || context.FieldC == 59
                            || context.FieldC == 60 || context.FieldC == 61 || context.FieldC == 55) Func2142AE8(*position);
                        else Func2142ABC(*position);
                        if (!_player->IsMorphing() && !_player->IsUnmorphing())
                        {
                            if (_player->_horizColTimer > 10 * 2 && HasFlag(_player->Flags1(), PlayerFlags1::Grounded)) PressL();
                            if (context.Field9 == 6 || context.Field9 == 14 || context.Field9 == 17 || context.Field9 == 20)
                            {
                                Vector3 toPos = *position - _player->Position;
                                toPos = AddY(toPos, -Fixed::ToFloat(_player->Values().MaxPickupHeight));
                                if (context.Field9 != 6) toPos = AddY(toPos, -1.25F);
                                if (toPos.Y > -0.5F && LengthSquared(WithY(toPos, 0)) < 0.5F * 0.5F) PressL();
                            }
                        }
                    }
                }
            }
            else if (context.Field4 == 34 && context.FieldD == 29)
            {
                if (!_player->IsAltForm()) PressButton(_touchButtons.Morph, 10);
                else if (_fieldAC.X != 0 || _fieldAC.Z != 0) Func2141CD4(_fieldAC);
            }
            if (context.FieldE == 38 && !_player->IsAltForm()) Func214380C();
            if (context.FieldC == 62 && _player->IsAltForm())
            {
                auto shootAndSetDelay = [this]()
                {
                    if (_buttons.L.FramesUp > _field102E)
                    {
                        _buttons.L.IsDown = true;
                        _field102E = _field102C + Rng::GetRandomInt2(_field102C / 2);
                    }
                };
                if (context.FieldF == 64)
                {
                    if (HasFlag(_player->_abilities, AbilityFlags::Bombs) && _player->_bombAmmo > 0 && _player->_bombCooldown == 0) shootAndSetDelay();
                }
                else if (context.FieldF == 65)
                {
                    if (HasFlag(_player->_abilities, AbilityFlags::Bombs) && _player->_bombCooldown == 0) shootAndSetDelay();
                }
                else if (context.FieldF == 66)
                {
                    if (HasFlag(_player->_abilities, AbilityFlags::SpireAltAttack) && !HasFlag(_player->Flags2(), PlayerFlags2::AltAttack)) shootAndSetDelay();
                }
                else if (context.FieldF == 67)
                {
                    if (_player->_altAttackTime > 0 || _buttons.L.FramesUp > _field102E)
                    { _buttons.L.IsDown = true; _field102E = _field102C + Rng::GetRandomInt2(_field102C / 2); }
                }
                else if (context.FieldF == 68)
                {
                    if (HasFlag(_player->_abilities, AbilityFlags::TraceAltAttack) && _player->_altAttackCooldown == 0
                        && (Flags2 & AiFlags2::Bit0) != AiFlags2::None) shootAndSetDelay();
                }
                else if (context.FieldF == 69)
                {
                    if (Rng::GetRandomInt2(15 * _player->SyluxBombCount() + 10) == 0
                        && HasFlag(_player->_abilities, AbilityFlags::Bombs) && _player->_bombAmmo > 0 && _player->_bombCooldown == 0) shootAndSetDelay();
                }
                else if (context.FieldF == 70)
                {
                    if (HasFlag(_player->_abilities, AbilityFlags::WeavelAltAttack) && _player->_altAttackCooldown == 0
                        && (Flags2 & AiFlags2::Bit0) != AiFlags2::None) shootAndSetDelay();
                }
            }
            else if (context.FieldC == 63)
            {
                if (context.FieldF == 65)
                {
                    if (HasFlag(_player->_abilities, AbilityFlags::Bombs) && _player->_bombCooldown == 0 && _buttons.L.FramesUp > 60 * 2)
                        _buttons.L.IsDown = true;
                }
                else if (context.FieldF == 69 && _player->SyluxBombCount() < 2)
                {
                    assert(_node3C);
                    if (IsNodeInRange(*_node3C) && HasFlag(_player->_abilities, AbilityFlags::Bombs)
                        && _player->_bombAmmo > 0 && _player->_bombCooldown == 0 && _buttons.L.FramesUp > 0) _buttons.L.IsDown = true;
                }
            }
            if (!_player->IsAltForm() && !_player->IsMorphing() && (Flags2 & AiFlags2::TargetPlayer) != AiFlags2::None)
            {
                assert(_targetPlayer);
                if (_targetPlayer->Hunter() == Hunter::Sylux && _targetPlayer->IsAltForm() && Func2139C60(*_targetPlayer)) PressL();
            }
            if (context.Field6 == 51 && !_player->IsAltForm() && !_player->IsMorphing() && !HasFlag(_player->Flags1(), PlayerFlags1::UsedJump)) PressButton(_buttons.L, 5);
            if ((Flags2 & AiFlags2::Bit21) != AiFlags2::None && Rng::GetRandomInt2(10) == 0
                && !_player->IsAltForm() && !_player->IsMorphing() && !HasFlag(_player->Flags1(), PlayerFlags1::UsedJump)) PressButton(_buttons.L, 5);
            if (context.Func24Id == 95 && !_player->IsAltForm() && !_player->IsMorphing() && !HasFlag(_player->Flags1(), PlayerFlags1::UsedJump)
                && Metadata::SlipSpeedFactors[_player->_slipperiness] > 0 && _player->_hSpeedMag > 0) PressButton(_buttons.L, 5);
        }

        void Func2142DCC()
        {
            if ((Flags2 & AiFlags2::Bit12) != AiFlags2::None) _buttons.A.IsDown = true;
            else _buttons.Y.IsDown = true;
            auto vec = ExecuteVectorFunc(0, true, false);
            float lengthSqr = LengthSquared(vec);
            if (lengthSqr < 4.0F) _buttons.B.IsDown = true;
            else if (lengthSqr > 9.0F) _buttons.X.IsDown = true;
        }

        bool Func2142EB0(AiContext& context)
        {
            auto toSelf = WithY((_player->Position - context.Field34), 0);
            if (_player->IsMorphing() || _player->IsUnmorphing() || LengthSquared(toSelf) >= 0.25F)
            {
                context.Field40 = 0;
                context.Field34 = _player->Position;
            }
            else context.Field40++;
            if (context.Field40 < 30) return false;
            context.Field40 = 0;
            context.Field34 = _player->Position;
            Flags2 ^= AiFlags2::Bit12;
            return true;
        }

        void Func2_213DDCC(AiContext& context)
        {
            if (_player->IsAltForm())
            {
                if ((Flags2 & AiFlags2::TargetPlayer) != AiFlags2::None)
                {
                    Func2142DCC();
                    assert(_targetPlayer);
                    Func2145C14(_targetPlayer->Position);
                    bool spawnBomb = false;
                    if (_player->SyluxBombCount() == 0)
                    {
                        spawnBomb = true;
                        if (Rng::GetRandomInt2(2) == 0) Flags2 &= ~AiFlags2::Bit12;
                        else Flags2 |= AiFlags2::Bit12;
                    }
                    else if (_player->SyluxBombCount() == 1 || _player->SyluxBombCount() == 2)
                    {
                        auto firstBomb = _player->SyluxBombs()[_player->SyluxBombCount() - 1];
                        assert(firstBomb);
                        auto targetToBomb = WithY((static_cast<Vector3>(firstBomb->Position) - static_cast<Vector3>(_targetPlayer->Position)), 0);
                        if (!VectorEqual(targetToBomb, Vector3::Zero)) targetToBomb = targetToBomb.Normalized();
                        else targetToBomb = UnitX();
                        auto targetToSelf = WithY((static_cast<Vector3>(_player->Position) - static_cast<Vector3>(_targetPlayer->Position)), 0);
                        if (!VectorEqual(targetToSelf, Vector3::Zero)) targetToSelf = targetToSelf.Normalized();
                        else targetToSelf = targetToBomb;
                        if (::OpenTK::Mathematics::Vector3::Dot(targetToBomb, targetToSelf) < -0.5F)
                        {
                            if (_player->SyluxBombCount() == 1) spawnBomb = true;
                            else
                            {
                                auto added = targetToBomb + targetToSelf;
                                if (VectorEqual(added, Vector3::Zero)) spawnBomb = true;
                                else
                                {
                                    auto secondBomb = _player->SyluxBombs()[0];
                                    assert(secondBomb);
                                    targetToBomb = WithY((static_cast<Vector3>(secondBomb->Position) - static_cast<Vector3>(_targetPlayer->Position)), 0);
                                    spawnBomb = ::OpenTK::Mathematics::Vector3::Dot(added, targetToBomb) > 0;
                                }
                            }
                        }
                    }
                    if (Func2142EB0(context)) spawnBomb = true;
                    if (spawnBomb && HasFlag(_player->_abilities, AbilityFlags::Bombs) && _player->_bombAmmo > 0
                        && _player->_bombCooldown == 0 && _buttons.L.FramesUp > _field102E)
                    {
                        _buttons.L.IsDown = true;
                        _field102E = _field102C + Rng::GetRandomInt2(_field102C / 2);
                    }
                }
            }
            else if (_touchButtons.Morph.FramesUp > 20) _touchButtons.Morph.IsDown = true;
        }

        void Func2_213DA88(AiContext& context)
        {
            if (_player->IsAltForm())
            {
                _buttons.A.IsDown = true;
                if ((Flags2 & AiFlags2::TargetPlayer) != AiFlags2::None)
                {
                    assert(_targetPlayer);
                    auto zero = ::OpenTK::Mathematics::Vector3::Zero;
                    float targetLengthSqr = LengthSquared(WithY(_targetPlayer->Position, 0));
                    float selfLengthSqr = LengthSquared(WithY(_player->Position, 0));
                    bool spawnBomb = false;
                    if (_player->SyluxBombCount() == 0) spawnBomb = _buttons.L.FramesUp > 2;
                    else if (_player->SyluxBombCount() == 1 || _player->SyluxBombCount() == 2)
                    {
                        auto bomb = _player->SyluxBombs()[_player->SyluxBombCount() - 1];
                        assert(bomb);
                        auto bombPos = WithY(bomb->Position, 0);
                        if (!VectorEqual(bombPos, Vector3::Zero)) bombPos = bombPos.Normalized();
                        else bombPos = UnitX();
                        auto selfPos = WithY(_player->Position, 0);
                        if (!VectorEqual(selfPos, Vector3::Zero)) selfPos = selfPos.Normalized();
                        else selfPos = bombPos;
                        spawnBomb = ::OpenTK::Mathematics::Vector3::Dot(bombPos, selfPos) < -0.5F;
                    }
                    if (selfLengthSqr < targetLengthSqr + 4) _buttons.B.IsDown = true;
                    else if (selfLengthSqr > targetLengthSqr + 9) _buttons.X.IsDown = true;
                    Func2145C14(zero);
                    if (spawnBomb) _buttons.L.IsDown = true;
                }
            }
            else if (_touchButtons.Morph.FramesUp > 20) _touchButtons.Morph.IsDown = true;
        }

        void Func2142FC0()
        {
            if (_buttons.A.FramesDown < 360 && _buttons.Y.FramesUp != 0) _buttons.A.IsDown = true;
            else if (_buttons.Y.FramesDown < 360) _buttons.Y.IsDown = true;
            auto vec = ExecuteVectorFunc(0, true, false);
            if (LengthSquared(vec) < 4) _buttons.B.IsDown = true;
            else if (LengthSquared(vec) > 9) _buttons.X.IsDown = true;
        }

        void Func2_213E148(AiContext& context)
        {
            if (!_player->IsAltForm() && _touchButtons.Morph.FramesUp > 20) _touchButtons.Morph.IsDown = true;
            if ((Flags2 & AiFlags2::TargetPlayer) != AiFlags2::None)
            {
                Func2142FC0(); assert(_targetPlayer); Func2145C14(_targetPlayer->Position);
            }
        }

        void Func213FD94()
        {
            if ((Flags2 & AiFlags2::TargetPlayer) == AiFlags2::None) return;
            assert(_targetPlayer);
            auto toTarget = static_cast<Vector3>(_targetPlayer->Position) - static_cast<Vector3>(_player->Position);
            float minLengthSqr = LengthSquared(toTarget);
            std::shared_ptr<BeamProjectileEntity> closestBeam{};
            for (std::int32_t beamIndex = 0; beamIndex < _targetPlayer->EquipInfo()->Beams->Length(); ++beamIndex)
            if (const auto& beam = (*_targetPlayer->EquipInfo()->Beams)[beamIndex]; true)
            {
                if (beam->Lifespan() > 0)
                {
                    auto toBeam = static_cast<Vector3>(beam->Position) - static_cast<Vector3>(_player->Position);
                    float toBeamLenSq = LengthSquared(toBeam);
                    if (toBeamLenSq > 0) toBeam = toBeam.Normalized(); else toBeam = beam->Direction();
                    auto beamVelocity = beam->Velocity();
                    if (!VectorEqual(beamVelocity, Vector3::Zero)) beamVelocity = beamVelocity.Normalized(); else beamVelocity = toBeam;
                    if (::OpenTK::Mathematics::Vector3::Dot(toBeam, beamVelocity) <= Fixed::ToFloat(-2896) && toBeamLenSq <= minLengthSqr)
                    { minLengthSqr = toBeamLenSq; closestBeam = beam; }
                }
            }
            if (closestBeam)
            {
                auto beamVelocityH = WithY(closestBeam->Velocity(), 0);
                auto altVec = _player->IsAltForm()
                    ? ::OpenTK::Mathematics::Vector3(_player->_field80, 0, _player->_field84)
                    : ::OpenTK::Mathematics::Vector3(_player->_field70, 0, _player->_field74);
                auto cross = ::OpenTK::Mathematics::Vector3::Cross(altVec, beamVelocityH);
                if (cross.Y > 0) _buttons.A.IsDown = true; else _buttons.Y.IsDown = true;
                _buttons.L.IsDown = true;
            }
        }

        void Func2_213E9C8(AiContext&) { CheckUnmorph(); if ((Flags2 & AiFlags2::TargetPlayer) != AiFlags2::None) Func21436D8(); Func213FD94(); }
        void Func2_213E984(AiContext&) { CheckUnmorph(); }
        void Func2_213E934(AiContext& context) { Func2140094(context); if (_player->IsAltForm() && _buttons.L.FramesUp > _player->Values().BombRefillTime) _buttons.L.IsDown = true; }

        void Func21449DC()
        {
            assert(_targetPlayer);
            _targetPlayer->GetPosition(_field1048);
            // The two draws are named so they land in X and Z in the order
            // C# draws them: C++ leaves argument evaluation order open, and
            // the transposed pair aims the bot somewhere else.
            const float spreadX = Rng::GetRandomInt2(8192) / 4096.0F - 1;
            const float spreadZ = Rng::GetRandomInt2(8192) / 4096.0F - 1;
            _field1048 = ::OpenTK::Mathematics::Vector3(
                _field1048.X + spreadX,
                _field1048.Y + (_targetPlayer->IsAltForm() ? _targetPlayer->Values().AltColYPos : 0.5F),
                _field1048.Z + spreadZ);
            if (!Func213842C()) _field1048 = AddZ(_field1048, _field1048.Z < 0 ? -1.5F : 1.5F);
            Func2145738(_field1048);
        }
        void Func2143578()
        {
            Func21449DC();
            _buttonAimX = std::clamp(_buttonAimX, -0.75F, 0.75F);
            _buttonAimY = std::clamp(_buttonAimY, -0.75F, 0.75F);
            if (_player->WeaponSelection() != BeamType::Imperialist) _touchButtons.Imperialist.IsDown = true;
            else if (_buttons.R.FramesUp > 20) _buttons.R.IsDown = true;
        }
        void Func2_213E904(AiContext&) { if ((Flags2 & AiFlags2::TargetPlayer) != AiFlags2::None) Func2143578(); }

        void Func2_213E684(AiContext& context)
        {
            if (_player->IsAltForm())
            {
                float distSqr = 49;
                if ((Flags2 & AiFlags2::TargetPlayer) != AiFlags2::None)
                { assert(_targetPlayer); distSqr = DistanceSquared(_targetPlayer->Position, _player->Position); }
                context.Field2C = 0;
                if (_player->SyluxBombCount() != 1 && (distSqr >= 100 || _player->SyluxBombCount() != 0)) Func2140094(context);
                if (HasFlag(_player->_abilities, AbilityFlags::Bombs) && _player->_bombAmmo > 0 && _player->_bombCooldown == 0 && _buttons.L.FramesUp > 2)
                {
                    if (distSqr >= 100 || _player->SyluxBombCount() != 0)
                    {
                        if (_player->SyluxBombCount() == 1)
                        {
                            _buttons.A.IsDown = false; _buttons.Y.IsDown = true; _buttons.X.IsDown = false; _buttons.B.IsDown = false;
                            if (_buttons.Y.FramesDown > 20 && _buttons.L.FramesUp != 0) _buttons.L.IsDown = true;
                        }
                        else if (_player->SyluxBombCount() == 2 && _buttons.L.FramesUp > 300) _buttons.L.IsDown = true;
                    }
                    else
                    {
                        _buttons.A.IsDown = true; _buttons.Y.IsDown = false; _buttons.X.IsDown = false; _buttons.B.IsDown = false;
                        if (_buttons.A.FramesDown > 20 && _buttons.L.FramesUp != 0) _buttons.L.IsDown = true;
                    }
                }
            }
            else if (_touchButtons.Morph.FramesUp > 20) _touchButtons.Morph.IsDown = true;
        }

        void Func2_213E3C4(AiContext& context)
        {
            if (_player->IsAltForm()) CheckUnmorph();
            else if ((Flags2 & AiFlags2::TargetDefense) != AiFlags2::None)
            {
                assert(_targetDefense);
                float radius = 0;
                if (_targetDefense->Volume().Type == VolumeType::Cylinder) radius = _targetDefense->Volume().CylinderRadius;
                else if (_targetDefense->Volume().Type == VolumeType::Sphere) radius = _targetDefense->Volume().SphereRadius;
                if (radius > 0.5F)
                {
                    auto toDefense = WithY((static_cast<Vector3>(_targetDefense->Position) - static_cast<Vector3>(_player->Position)), 0);
                    if (radius * radius <= LengthSquared(toDefense))
                    {
                        Field118 = 0; context.Field40 = 0; context.Field44 = 0; context.Field34 = _player->Position;
                        _field78 = 0; Flags2 &= ~AiFlags2::Bit15;
                        float x = Rng::GetRandomInt2(4096) / 4096.0F;
                        _fieldA0 = ::OpenTK::Mathematics::Vector3(x * std::copysign(1.0F, toDefense.X), 0,
                            std::sqrt(1 - x * x) * std::copysign(1.0F, toDefense.Z));
                        _fieldA0 = ScaleVector(_fieldA0, radius); _fieldA0 = _fieldA0 + static_cast<Vector3>(_targetDefense->Position);
                    }
                    Func2142AE8(_fieldA0); Field118++; Func2140B18(context, _fieldA0);
                }
                Func21436D8();
            }
            if ((Flags2 & AiFlags2::Bit10) != AiFlags2::None && Rng::GetRandomInt2(10) == 0 && !_player->IsAltForm() && !_player->IsMorphing()
                && !HasFlag(_player->Flags1(), PlayerFlags1::UsedJump) && _buttons.L.FramesUp > 10) _buttons.L.IsDown = true;
        }

        void Func2142D38()
        {
            if (_buttons.Y.FramesUp > 60 || (_buttons.Y.FramesDown < 60 && _buttons.Y.FramesDown != 0)) _buttons.Y.IsDown = true;
            else _buttons.A.IsDown = true;
            if (_buttons.X.FramesUp > 30 || (_buttons.X.FramesDown < 30 && _buttons.X.FramesDown != 0)) _buttons.X.IsDown = true;
            else _buttons.B.IsDown = true;
        }
        void Func2_213E31C(AiContext&)
        {
            CheckUnmorph(); if ((Flags2 & AiFlags2::TargetPlayer) != AiFlags2::None) Func21433E4(); Func2142D38();
            if (!HasFlag(_player->Flags1(), PlayerFlags1::UsedJump) && _buttons.L.FramesUp > _field1034)
            { _buttons.L.IsDown = true; _field1034 = Rng::GetRandomInt2(150) + 30; }
        }
        void Func21431B4()
        {
            assert(_targetPlayer);
            auto selfAltVec = _player->IsAltForm() ? ::OpenTK::Mathematics::Vector3(_player->_field80,0,_player->_field84)
                                               : ::OpenTK::Mathematics::Vector3(_player->_field70,0,_player->_field74);
            auto targetAltVec = _targetPlayer->IsAltForm() ? ::OpenTK::Mathematics::Vector3(_targetPlayer->_field80,0,_targetPlayer->_field84)
                                                         : ::OpenTK::Mathematics::Vector3(_targetPlayer->_field70,0,_targetPlayer->_field74);
            if (::OpenTK::Mathematics::Vector3::Dot(selfAltVec, targetAltVec) <= Fixed::ToFloat(-3138))
            { if (_buttons.A.FramesDown != 0) _buttons.A.IsDown = true; else _buttons.Y.IsDown = true; }
            else { auto cross = ::OpenTK::Mathematics::Vector3::Cross(selfAltVec,targetAltVec); if (cross.Y >= 0) _buttons.A.IsDown = true; else _buttons.Y.IsDown = true; }
            auto vec = ExecuteVectorFunc(0,true,false); if (LengthSquared(vec) < 4) _buttons.B.IsDown = true; else if (LengthSquared(vec) > 9) _buttons.X.IsDown = true;
        }
        void Func2_213E274(AiContext&)
        {
            CheckUnmorph(); if ((Flags2 & AiFlags2::TargetPlayer) != AiFlags2::None) { Func21436D8(); Func21431B4(); }
            if (!HasFlag(_player->Flags1(), PlayerFlags1::UsedJump) && _buttons.L.FramesUp > _field1034)
            { _buttons.L.IsDown = true; _field1034 = Rng::GetRandomInt2(150) + 30; }
        }
        void Func21430B4()
        {
            assert(_targetHalfturret);
            if (_buttons.Y.FramesDown > 60 || (_buttons.A.FramesDown < 120 && _buttons.A.FramesDown != 0)) _buttons.A.IsDown = true; else _buttons.Y.IsDown = true;
            float distSqr = DistanceSquared(_targetHalfturret->Position, _player->Position);
            if (distSqr < 4) _buttons.B.IsDown = true; else if (distSqr > 9) _buttons.X.IsDown = true;
        }
        void Func2_213E1CC(AiContext&)
        {
            CheckUnmorph(); if ((Flags2 & AiFlags2::TargetHalfturret) != AiFlags2::None) { Func2143470(); Func21430B4(); }
            if (!HasFlag(_player->Flags1(), PlayerFlags1::UsedJump) && _buttons.L.FramesUp > _field1034)
            { _buttons.L.IsDown = true; _field1034 = Rng::GetRandomInt2(150) + 30; }
        }
        void Func2_213D9B8(AiContext& context)
        {
            if (_player->IsAltForm()) CheckUnmorph();
            else
            {
                assert(_node40); Func2142ABC(_node40->Position); Field118++;
                if (!HasFlag(_player->Flags1(), PlayerFlags1::UsedJump) && _buttons.L.FramesUp > 10
                    && (_player->_standTerrain == Terrain::Lava || (_player->_horizColTimer > 20 && HasFlag(_player->Flags1(), PlayerFlags1::Grounded)))) _buttons.L.IsDown = true;
                Func2140B18(context, _node40->Position);
            }
        }
        void Func2_213D96C(AiContext&)
        { CheckUnmorph(); if ((Flags2 & AiFlags2::TargetPlayer) != AiFlags2::None) { assert(_targetPlayer); Func2145C14(_targetPlayer->Position); } }

        std::int32_t Func3_213D87C(AiContext&, const Formats::AiPersonalityData5&) { return 1; }
        std::int32_t Func3_213D83C(AiContext&, const Formats::AiPersonalityData5&)
        { assert(_node3C); return IsNodeInRange(*_node3C) && _node40 == _node3C ? 1 : 0; }
        std::int32_t Func3_213D814(AiContext&, const Formats::AiPersonalityData5& param)
        { assert(_node40); return _node40->Position.Y - _player->Position.Y > param.Param1 / 4096.0F ? 1 : 0; }
        std::int32_t Func3_213D7F0(AiContext&, const Formats::AiPersonalityData5&) { return (Flags2 & AiFlags2::Bit0) != AiFlags2::None ? 1 : 0; }
        std::int32_t Func3_213D800(AiContext&, const Formats::AiPersonalityData5&) { return (Flags4 & AiFlags4::Bit0) != AiFlags4::None ? 1 : 0; }
        std::int32_t Func3_213D7E8(AiContext&, const Formats::AiPersonalityData5&) { return 0; }
        std::int32_t Func3_213D7D0(AiContext&, const Formats::AiPersonalityData5&) { return (Flags2 & AiFlags2::TargetItem) != AiFlags2::None ? 0 : 1; }
        std::int32_t Func3_213D7B8(AiContext& c, const Formats::AiPersonalityData5& p) { return Func3_213D7D0(c,p); }
        std::int32_t Func3_213D7A0(AiContext& c, const Formats::AiPersonalityData5& p) { return Func3_213D7D0(c,p); }
        std::int32_t Func3_213D77C(AiContext&, const Formats::AiPersonalityData5&)
        { if ((Flags2 & AiFlags2::TargetDoor) != AiFlags2::None) { assert(_targetDoor); return HasFlag(_targetDoor->Flags(), DoorFlags::ShotOpen) ? 1 : 0; } return 0; }
        std::int32_t Func3_213D758(AiContext& c, const Formats::AiPersonalityData5& p) { return Func3_213D77C(c,p) ^ 1; }
        std::int32_t Func3_213D734(AiContext&, const Formats::AiPersonalityData5&)
        { if ((Flags2 & AiFlags2::TargetDoor) != AiFlags2::None) { assert(_targetDoor); return HasFlag(_targetDoor->Flags(), DoorFlags::Locked) ? 1 : 0; } return 0; }
        std::int32_t Func3_213D710(AiContext& c, const Formats::AiPersonalityData5& p) { return Func3_213D734(c,p) ^ 1; }
        std::int32_t Func3_213D6D0(AiContext&, const Formats::AiPersonalityData5&)
        { FindEntityRef(AiEntRefType::Type0); assert(_entityRefs.Field0); return IsNodeInRange(*_entityRefs.Field0) ? 1 : 0; }
        std::int32_t Func3_213D6AC(AiContext& c, const Formats::AiPersonalityData5& p) { return Func3_213D6D0(c,p) ^ 1; }
        std::int32_t Func3_213D624(AiContext&, const Formats::AiPersonalityData5&)
        {
            if ((Flags2 & AiFlags2::TargetPlayer) == AiFlags2::None) return 0;
            FindEntityRef(AiEntRefType::Type0); auto node0 = _entityRefs.Field0; assert(node0);
            FindEntityRef(AiEntRefType::Type2); auto node2 = _entityRefs.Field2; assert(node2);
            return node0 == node2 || IsNodeInRange(*node2) ? 1 : 0;
        }
        std::int32_t Func3_213D608(AiContext& c, const Formats::AiPersonalityData5& p) { return Func3_213D624(c,p) ^ 1; }
        std::int32_t Func3_213D564(AiContext&, const Formats::AiPersonalityData5& p)
        { if ((Flags2&AiFlags2::TargetPlayer)==AiFlags2::None) return 0; assert(_targetPlayer); float d=p.Param1/4096.0F; return DistanceSquared(_targetPlayer->Position,_player->Position)<d*d?1:0; }
        std::int32_t Func3_213D540(AiContext& c,const Formats::AiPersonalityData5& p){return Func3_213D564(c,p)^1;}
        std::int32_t Func3_213D530(AiContext&,const Formats::AiPersonalityData5&){return Func213842C()?1:0;}
        std::int32_t Func3_213D514(AiContext& c,const Formats::AiPersonalityData5& p){return Func3_213D530(c,p)^1;}
        std::int32_t Func3_213D4C0(AiContext&,const Formats::AiPersonalityData5&)
        { if((Flags2&AiFlags2::TargetPlayer)==AiFlags2::None)return 0; assert(_targetPlayer); for (std::int32_t beamIndex = 0; beamIndex < _targetPlayer->EquipInfo()->Beams->Length(); ++beamIndex)
            if (const auto& b = (*_targetPlayer->EquipInfo()->Beams)[beamIndex]; true) if(b->Lifespan()!=0)return 1; return 0; }
        std::int32_t Func3_213D49C(AiContext& c,const Formats::AiPersonalityData5& p){return Func3_213D4C0(c,p)^1;}
        std::int32_t Func3_213D43C(AiContext&,const Formats::AiPersonalityData5&)
        { for (auto _enumerator3 = _scene.GetBeamProjectileEntities().GetEnumerator(); _enumerator3.MoveNext(); )
            if (const auto b = _enumerator3.Current(); true) if(b->Lifespan()!=0&&b->Beam()==BeamType::OmegaCannon)return 1; return 0; }
        std::int32_t Func3_213D418(AiContext& c,const Formats::AiPersonalityData5& p){return Func3_213D43C(c,p)^1;}
        std::int32_t Func3_213D388(AiContext&,const Formats::AiPersonalityData5&)
        {
            std::shared_ptr<PlayerEntity> player{};
            if(GameState::Mode()==GameMode::SinglePlayer) player=PlayerEntity::Main();
            else { for (auto _enumerator4 = _scene.GetPlayerEntities().GetEnumerator(); _enumerator4.MoveNext(); )
            if (const auto e = _enumerator4.Current(); true){player=e;break;} }
            if(!player)return 0; return AggroFunc214857C(6,1,2,{},player)?1:0;
        }
        std::int32_t Func3_213D36C(AiContext& c,const Formats::AiPersonalityData5& p){return Func3_213D388(c,p)^1;}
        std::int32_t Func3_213D2C0(AiContext&,const Formats::AiPersonalityData5&)
        { for (auto _enumerator5 = _scene.GetPlayerEntities().GetEnumerator(); _enumerator5.MoveNext(); )
            if (const auto p = _enumerator5.Current(); true) if(p!=_player&&p->IsBot()&&AggroFunc214857C(6,1,2,{},p))return 1; return 0; }
        std::int32_t Func3_213D2A4(AiContext& c,const Formats::AiPersonalityData5& p){return Func3_213D2C0(c,p)^1;}
        std::int32_t Func3_213D234(AiContext&,const Formats::AiPersonalityData5&)
        {if((Flags2&AiFlags2::TargetPlayer)==AiFlags2::None)return 0; return AggroFunc214857C(6,2,1,_targetPlayer,{})?1:0;}
        std::int32_t Func3_213D218(AiContext& c,const Formats::AiPersonalityData5& p){return Func3_213D234(c,p)^1;}
        std::int32_t Func3_213D178(AiContext&,const Formats::AiPersonalityData5&)
        {for (auto _enumerator6 = _scene.GetPlayerEntities().GetEnumerator(); _enumerator6.MoveNext(); )
            if (const auto p = _enumerator6.Current(); true)if(p!=_player&&AggroFunc214857C(6,1,2,{},p))return 1;return 0;}
        std::int32_t Func3_213D15C(AiContext& c,const Formats::AiPersonalityData5& p){return Func3_213D178(c,p)^1;}
        bool Func213995C(float angleCos,float maxDistSqr)
        {auto v1=ExecuteVectorFunc(7,false,false);float l=LengthSquared(v1);if(maxDistSqr>0&&l>=maxDistSqr)return false;auto v2=ExecuteVectorFunc(1,false,false);float d=::OpenTK::Mathematics::Vector3::Dot(v2,v1);return d>angleCos||(d>0&&l<1);}
        std::int32_t Func3_213D128(AiContext&,const Formats::AiPersonalityData5&){return Func213995C(Fixed::ToFloat(3849),16)?1:0;}
        std::int32_t Func3_213D0F4(AiContext&,const Formats::AiPersonalityData5&){return Func213995C(Fixed::ToFloat(3849),25)?0:1;}
        std::int32_t Func3_213D0C4(AiContext&,const Formats::AiPersonalityData5&){if((Flags2&AiFlags2::TargetPlayer)==AiFlags2::None)return 0;assert(_targetPlayer);return _targetPlayer->IsAltForm()?1:0;}
        std::int32_t Func3_213D0A8(AiContext& c,const Formats::AiPersonalityData5& p){return Func3_213D0C4(c,p)^1;}
        std::int32_t Func3_213D078(AiContext&,const Formats::AiPersonalityData5&){if((Flags2&AiFlags2::TargetPlayer)==AiFlags2::None)return 0;assert(_targetPlayer);return _targetPlayer->_frozenTimer!=0?1:0;}
        std::int32_t Func3_213D05C(AiContext& c,const Formats::AiPersonalityData5& p){return Func3_213D078(c,p)^1;}
        std::int32_t Func3_213D044(AiContext&,const Formats::AiPersonalityData5&){return(Flags2&AiFlags2::TargetPlayer)!=AiFlags2::None?1:0;}
        std::int32_t Func3_213D028(AiContext& c,const Formats::AiPersonalityData5& p){return Func3_213D044(c,p)^1;}
        std::int32_t Func3_213D010(AiContext&,const Formats::AiPersonalityData5&){return(Flags2&AiFlags2::TargetHalfturret)!=AiFlags2::None?1:0;}
        std::int32_t Func3_213CFF4(AiContext& c,const Formats::AiPersonalityData5& p){return Func3_213D010(c,p)^1;}
        std::int32_t Func3_213CFDC(AiContext&,const Formats::AiPersonalityData5&){return(Flags2&AiFlags2::TargetItem)!=AiFlags2::None?1:0;}
        std::int32_t Func3_213CFC0(AiContext& c,const Formats::AiPersonalityData5& p){return Func3_213CFDC(c,p)^1;}
        std::int32_t Func3_213CFA4(AiContext&,const Formats::AiPersonalityData5&){return _slotHits[_player->SlotIndex()]!=0?1:0;}
        std::int32_t Func3_213CF0C(AiContext&,const Formats::AiPersonalityData5&){if((Flags2&AiFlags2::TargetPlayer)==AiFlags2::None)return 0;assert(_targetPlayer);return AggroFunc214857C(4,2,1,_targetPlayer,{})?1:0;}
        std::int32_t Func3_213CEE8(AiContext& c,const Formats::AiPersonalityData5& p){return Func3_213CF0C(c,p)^1;}
        std::int32_t Func3_213CDB8(AiContext&,const Formats::AiPersonalityData5&)
        {
            for (auto _enumerator7 = _scene.GetPlayerEntities().GetEnumerator(); _enumerator7.MoveNext(); )
            if (const auto o = _enumerator7.Current(); true)
            {
                float d=DistanceSquared(o->Position,_player->Position);bool a=false;if(d>=49)a=AggroFunc214857C(6,1,2,{},o);
                if(o!=_player&&o->TeamIndex()==_player->TeamIndex()&&o->Health()!=0&&(d<49||a)&&_slotHits[o->SlotIndex()]!=0)return 1;
            }return 0;
        }
        std::int32_t Func3_213CF94(AiContext&,const Formats::AiPersonalityData5&){return(Flags2&AiFlags2::Bit21)!=AiFlags2::None?1:0;}
        std::int32_t Func3_213CF7C(AiContext&,const Formats::AiPersonalityData5&){return(Flags2&AiFlags2::Bit21)!=AiFlags2::None?0:1;}
        std::int32_t Func3_213CDA4(AiContext&,const Formats::AiPersonalityData5&){return DamageFromHalfturret!=0?1:0;}
        std::int32_t Func3_213CD74(AiContext&,const Formats::AiPersonalityData5&){return _player->IsAltForm()&&_player->Halfturret()->Health()==0?1:0;}
        std::int32_t Func3_213CD58(AiContext&,const Formats::AiPersonalityData5&){return _player->_health<_player->_healthMax/4?1:0;}
        std::int32_t Func3_213CD34(AiContext&,const Formats::AiPersonalityData5&){return _player->_health<_player->_healthMax/4?0:1;}
        std::int32_t Func3_213CD18(AiContext&,const Formats::AiPersonalityData5&){return _player->_health==_player->_healthMax?1:0;}
        std::int32_t Func3_213CCF4(AiContext&,const Formats::AiPersonalityData5&){return _player->_health==_player->_healthMax?0:1;}
        std::int32_t Func3_213CCD8(AiContext&,const Formats::AiPersonalityData5& p){return _player->_health<p.Param1?1:0;}
        std::int32_t Func3_213CCBC(AiContext&,const Formats::AiPersonalityData5& p){return _player->_health>p.Param1?1:0;}
        std::int32_t Func3_213CCB0(AiContext&,const Formats::AiPersonalityData5&){return _player->_health;}
        std::int32_t Func3_213CC94(AiContext&,const Formats::AiPersonalityData5&){if(_player->_healthMax<=_player->_health)return 0;return _player->_healthMax-_player->_health;}
        std::int32_t Func3_213CBE4(AiContext&,const Formats::AiPersonalityData5& p){FindEntityRef(AiEntRefType::Type54);if(!_entityRefs.Field54)return 0;float d=p.Param1/4096.0F;return DistanceSquared(_entityRefs.Field54->Position,_player->Position)<d*d?1:0;}
        std::int32_t Func3_213CBC0(AiContext& c,const Formats::AiPersonalityData5& p){return Func3_213CBE4(c,p)^1;}
        std::int32_t Func3_213CBB0(AiContext&,const Formats::AiPersonalityData5&){if(Func2137860())return 0;for (auto _enumerator8 = _scene.GetItemInstanceEntities().GetEnumerator(); _enumerator8.MoveNext(); )
            if (const auto i = _enumerator8.Current(); true)if(!IsItemNotNeeded(i->ItemType())&&!Func21377FC(*i))return 1;return 0;}
        std::int32_t Func3_213CB8C(AiContext& c,const Formats::AiPersonalityData5& p){return Func3_213CBB0(c,p)^1;}
        std::int32_t Func3_213CADC(AiContext&,const Formats::AiPersonalityData5& p){FindEntityRef(AiEntRefType::Type55);if(!_entityRefs.Field55)return 0;float d=p.Param1/4096.0F;return DistanceSquared(_entityRefs.Field55->Position,_player->Position)<d*d?1:0;}
        std::int32_t Func3_213CAA8(AiContext&,const Formats::AiPersonalityData5&){FindEntityRef(AiEntRefType::Type55);return _entityRefs.Field55?1:0;}
        std::int32_t Func3_213CA84(AiContext& c,const Formats::AiPersonalityData5& p){return Func3_213CAA8(c,p)^1;}
        std::int32_t Func3_213CA70(AiContext&,const Formats::AiPersonalityData5&){return Field118>=302?1:0;}
        std::int32_t Func3_213CA58(AiContext&,const Formats::AiPersonalityData5& p){return Field118>p.Param1*2?1:0;}
        std::int32_t Func3_213CA2C(AiContext& c,const Formats::AiPersonalityData5& p){return _executionTree[c.Depth+1]->CallCount>(p.Param1*2)?1:0;}
        std::int32_t Func3_213CA00(AiContext& c,const Formats::AiPersonalityData5& p){return Func3_213CA2C(c,p);}
        std::int32_t Func3_213C9D4(AiContext& c,const Formats::AiPersonalityData5& p){return Func3_213CA2C(c,p);}
        std::int32_t Func3_213C9C4(AiContext&,const Formats::AiPersonalityData5&){return _slotHits[_player->SlotIndex()];}
        std::int32_t Func3_213C89C(AiContext&,const Formats::AiPersonalityData5&)
        {int hits=0;for (auto _enumerator9 = _scene.GetPlayerEntities().GetEnumerator(); _enumerator9.MoveNext(); )
            if (const auto o = _enumerator9.Current(); true){float d=DistanceSquared(o->Position,_player->Position);bool a=false;if(d>=49)a=AggroFunc214857C(6,1,2,{},o);if(o!=_player&&o->TeamIndex()==_player->TeamIndex()&&o->Health()!=0&&(d<49||a))hits+=_slotHits[o->SlotIndex()];}return hits;}
        std::int32_t Func3_213C88C(AiContext&,const Formats::AiPersonalityData5&){return _slotDamage[_player->SlotIndex()];}
        std::int32_t Func3_213C764(AiContext& c,const Formats::AiPersonalityData5& p){return Func3_213C89C(c,p);}
        std::int32_t Func3_213C75C(AiContext&,const Formats::AiPersonalityData5&){return static_cast<std::int32_t>(DamageFromHalfturret);}
        std::int32_t Func3_213C698(AiContext&,const Formats::AiPersonalityData5&)
        {if(_player->Hunter()==Hunter::Spire)return _player->_health<(GameState::EncounterState()[_player->SlotIndex()]==4?270:500)?1:0;if(_player->Hunter()==Hunter::Weavel)return _player->_health<110?1:0;if(_player->Hunter()==Hunter::Sylux)return _player->_health<700?1:0;if(_player->Hunter()==Hunter::Trace)return _player->_health<590?1:0;return 0;}
        std::int32_t Func3_213C64C(AiContext&,const Formats::AiPersonalityData5&){if(_player->Hunter()==Hunter::Sylux)return 100000*_slotDamage[_player->SlotIndex()]/25;return 0;}
        std::int32_t Func3_213C600(AiContext& c,const Formats::AiPersonalityData5&){return _player->Hunter()==Hunter::Sylux&&_executionTree[c.Depth+1]->CallCount>240?1:0;}
        std::int32_t Func3_213C52C(AiContext&,const Formats::AiPersonalityData5&){for (auto _enumerator10 = _scene.GetPlayerEntities().GetEnumerator(); _enumerator10.MoveNext(); )
            if (const auto o = _enumerator10.Current(); true)if(o!=_player&&o->TeamIndex()!=_player->TeamIndex()&&o->Health()!=0&&o->Hunter()==Hunter::Weavel&&o->Halfturret()->Health()!=0)return AggroFunc2148394(5,7,1,{},{})>0?1:0;return 0;}
        bool Func3_2139A1C(AiContext&,::OpenTK::Mathematics::Vector3 targetPos,float angleCos,float maxDistSqr)
        {auto t=targetPos-_player->Position;float d=LengthSquared(t);if(maxDistSqr>0&&d>=maxDistSqr)return false;t=t.Normalized();float dot=::OpenTK::Mathematics::Vector3::Dot(_player->FacingVector(),t);return dot>angleCos||(dot>0&&d<1);}
        std::int32_t Func3_213C48C(AiContext& c,const Formats::AiPersonalityData5&)
        {if((Flags2&AiFlags2::TargetPlayer)!=AiFlags2::None){assert(_targetPlayer);::OpenTK::Mathematics::Vector3 p;_targetPlayer->GetPosition(p);p=AddY(p, _targetPlayer->IsAltForm()?_targetPlayer->Values().AltColYPos:0.5F);if(Func3_2139A1C(c,p,0.5F,225))return 1;}return 0;}
        std::int32_t Func3_213C470(AiContext& c,const Formats::AiPersonalityData5& p){return Func3_213C48C(c,p)^1;}
        std::int32_t Func3_213C334(AiContext&,const Formats::AiPersonalityData5&){if(_findWeaponIndex>=0&&_findWeaponIndex<=8)return _player->_availableWeapons[GetBeamType(_findWeaponIndex)]?1:0;return 0;}
        std::int32_t Func3_213C310(AiContext& c,const Formats::AiPersonalityData5& p){return Func3_213C334(c,p)^1;}

        std::int32_t Func3_213C0D0(AiContext&,const Formats::AiPersonalityData5&)
        {
            if(_findWeaponIndex==1){if(_player->_availableWeapons[BeamType::Missile]){FindEntityRef(AiEntRefType::Type57);return _entityRefs.Field57?1:0;}FindEntityRef(AiEntRefType::Type56);return _entityRefs.Field56?1:0;}
            if(_findWeaponIndex==2){if(_player->_availableWeapons[BeamType::VoltDriver]){FindEntityRef(AiEntRefType::Type59);return _entityRefs.Field59?1:0;}FindEntityRef(AiEntRefType::Type58);return _entityRefs.Field58?1:0;}
            if(_findWeaponIndex==3){if(_player->_availableWeapons[BeamType::Battlehammer]){FindEntityRef(AiEntRefType::Type60);return _entityRefs.Field60?1:0;}FindEntityRef(AiEntRefType::Type61);return _entityRefs.Field61?1:0;}
            if(_findWeaponIndex==4){if(_player->_availableWeapons[BeamType::Imperialist]){FindEntityRef(AiEntRefType::Type63);return _entityRefs.Field63?1:0;}FindEntityRef(AiEntRefType::Type62);return _entityRefs.Field62?1:0;}
            if(_findWeaponIndex==5){if(_player->_availableWeapons[BeamType::Judicator]){FindEntityRef(AiEntRefType::Type65);return _entityRefs.Field65?1:0;}FindEntityRef(AiEntRefType::Type64);return _entityRefs.Field64?1:0;}
            if(_findWeaponIndex==6){if(_player->_availableWeapons[BeamType::Magmaul]){FindEntityRef(AiEntRefType::Type67);return _entityRefs.Field67?1:0;}FindEntityRef(AiEntRefType::Type66);return _entityRefs.Field66?1:0;}
            if(_findWeaponIndex==7){if(_player->_availableWeapons[BeamType::ShockCoil]){FindEntityRef(AiEntRefType::Type69);return _entityRefs.Field69?1:0;}FindEntityRef(AiEntRefType::Type68);return _entityRefs.Field68?1:0;}
            if(_findWeaponIndex==8){if(_player->_availableWeapons[BeamType::OmegaCannon]){FindEntityRef(AiEntRefType::Type71);return _entityRefs.Field71?1:0;}FindEntityRef(AiEntRefType::Type70);return _entityRefs.Field70?1:0;}
            return 0;
        }
        std::int32_t Func3_213C078(AiContext&,const Formats::AiPersonalityData5&){const WeaponInfo& info=*(*Weapons::Current)[static_cast<int>(GetBeamType(_weapon1))];return _player->_ammo[info.AmmoType]<info.AmmoCost?1:0;}
        std::int32_t Func3_213C054(AiContext&,const Formats::AiPersonalityData5&){const WeaponInfo& info=*(*Weapons::Current)[static_cast<int>(GetBeamType(_weapon1))];return _player->_ammo[info.AmmoType]>=info.AmmoCost?1:0;}
        std::int32_t Func3_213BFFC(AiContext&,const Formats::AiPersonalityData5&){const WeaponInfo& info=*(*Weapons::Current)[static_cast<int>(GetBeamType(_weapon2))];return _player->_ammo[info.AmmoType]<info.AmmoCost?1:0;}
        std::int32_t Func3_213BFD8(AiContext&,const Formats::AiPersonalityData5&){const WeaponInfo& info=*(*Weapons::Current)[static_cast<int>(GetBeamType(_weapon2))];return _player->_ammo[info.AmmoType]>=info.AmmoCost?1:0;}
        std::int32_t Func3_213BED8(AiContext&,const Formats::AiPersonalityData5&)
        {
            if(_weapon1==1){FindEntityRef(AiEntRefType::Type57);return _entityRefs.Field57?1:0;}
            if(_weapon1==2){FindEntityRef(AiEntRefType::Type59);return _entityRefs.Field59?1:0;}
            if(_weapon1==3){FindEntityRef(AiEntRefType::Type61);return _entityRefs.Field61?1:0;}
            if(_weapon1==4){FindEntityRef(AiEntRefType::Type63);return _entityRefs.Field63?1:0;}
            if(_weapon1==5){FindEntityRef(AiEntRefType::Type65);return _entityRefs.Field65?1:0;}
            if(_weapon1==6){FindEntityRef(AiEntRefType::Type67);return _entityRefs.Field67?1:0;}
            if(_weapon1==7){FindEntityRef(AiEntRefType::Type69);return _entityRefs.Field69?1:0;}
            if(_weapon1==8){FindEntityRef(AiEntRefType::Type71);return _entityRefs.Field71?1:0;}
            return 0;
        }
        std::int32_t Func3_213BEBC(AiContext&,const Formats::AiPersonalityData5&){return _player->_availableWeapons[BeamType::Missile]?1:0;}
        std::int32_t Func3_213BEA0(AiContext&,const Formats::AiPersonalityData5&){return _player->_availableWeapons[BeamType::Missile]?0:1;}
        std::int32_t Func3_213BE48(AiContext& c,const Formats::AiPersonalityData5& p){FindEntityRef(AiEntRefType::Type56);return _entityRefs.Field56&&Func3_213BEA0(c,p)==1?1:0;}
        std::int32_t Func3_213BE10(AiContext&,const Formats::AiPersonalityData5&){const WeaponInfo& info=*(*Weapons::Current)[static_cast<int>(BeamType::Missile)];return _player->_ammo[info.AmmoType]<info.AmmoCost?1:0;}
        std::int32_t Func3_213BDF4(AiContext&,const Formats::AiPersonalityData5&){const WeaponInfo& info=*(*Weapons::Current)[static_cast<int>(BeamType::Missile)];return _player->_ammo[info.AmmoType]>=info.AmmoCost?1:0;}
        std::int32_t Func3_213BD7C(AiContext& c,const Formats::AiPersonalityData5& p){FindEntityRef(AiEntRefType::Type57);return _entityRefs.Field57&&Func3_213BE10(c,p)==1?1:0;}
        std::int32_t Func3_213BCE8(AiContext&,const Formats::AiPersonalityData5&){return CanChargeWeapon()?1:0;}
        std::int32_t Func3_213BCC4(AiContext& c,const Formats::AiPersonalityData5& p){return Func3_213BCE8(c,p)^1;}
        std::int32_t Func3_213BCB0(AiContext&,const Formats::AiPersonalityData5&){return HasFlag(_player->Flags2(), PlayerFlags2::AltAttack)?1:0;}
        std::int32_t Func3_213BC8C(AiContext& c,const Formats::AiPersonalityData5& p){return Func3_213BCB0(c,p)^1;}
        std::int32_t Func3_213BC70(AiContext&,const Formats::AiPersonalityData5&){assert(_octolithFlagDC);return _octolithFlagDC->Carrier()==_player?1:0;}
        std::int32_t Func3_213BC4C(AiContext& c,const Formats::AiPersonalityData5& p){return Func3_213BC70(c,p)^1;}
        std::int32_t Func3_213BC0C(AiContext&,const Formats::AiPersonalityData5&){assert(_octolithFlagD4);auto c=_octolithFlagD4->Carrier();return c&&c->Health()!=0&&c->TeamIndex()!=_player->TeamIndex()?1:0;}
        std::int32_t Func3_213BBE8(AiContext& c,const Formats::AiPersonalityData5& p){return Func3_213BC0C(c,p)^1;}
        std::int32_t Func3_213BBA0(AiContext&,const Formats::AiPersonalityData5&){assert(_octolithFlagDC);auto c=_octolithFlagDC->Carrier();return c&&c->Health()!=0&&c->TeamIndex()==_player->TeamIndex()&&c!=_player?1:0;}
        std::int32_t Func3_213BB7C(AiContext& c,const Formats::AiPersonalityData5& p){return Func3_213BBA0(c,p)^1;}
        std::int32_t Func3_213BAF4(AiContext&,const Formats::AiPersonalityData5& p){assert(_octolithFlagCC);float d=p.Param1/4096.0F;return DistanceSquared(_octolithFlagCC->Position,_player->Position)<d*d?1:0;}
        std::int32_t Func3_213BAD0(AiContext& c,const Formats::AiPersonalityData5& p){return Func3_213BAF4(c,p)^1;}
        std::int32_t Func3_213BA68(AiContext&,const Formats::AiPersonalityData5&){assert(_octolithFlagDC);return DistanceSquared(_octolithFlagDC->Position,_player->Position)<9?1:0;}
        std::int32_t Func3_213BA44(AiContext& c,const Formats::AiPersonalityData5& p){return Func3_213BA68(c,p)^1;}
        std::int32_t Func3_213BA28(AiContext&,const Formats::AiPersonalityData5&){assert(_octolithFlagD4);return _octolithFlagD4->AtBase()?1:0;}
        std::int32_t Func3_213BA04(AiContext& c,const Formats::AiPersonalityData5& p){return Func3_213BA28(c,p)^1;}
        std::int32_t Func3_213B99C(AiContext&,const Formats::AiPersonalityData5&){assert(_flagBaseD8);return DistanceSquared(_flagBaseD8->Position,_player->Position)<9?1:0;}
        std::int32_t Func3_213B978(AiContext& c,const Formats::AiPersonalityData5& p){return Func3_213B99C(c,p)^1;}
        std::int32_t Func3_213B8B0(AiContext&,const Formats::AiPersonalityData5&){assert(_flagBaseD8);for (auto _enumerator11 = _scene.GetPlayerEntities().GetEnumerator(); _enumerator11.MoveNext(); )
            if (const auto o = _enumerator11.Current(); true)if(o!=_player&&o->TeamIndex()==_player->TeamIndex()&&DistanceSquared(_flagBaseD8->Position,o->Position)<9)return 1;return 0;}
        std::int32_t Func3_213B88C(AiContext& c,const Formats::AiPersonalityData5& p){return Func3_213B8B0(c,p)^1;}
        std::int32_t Func3_213B7A0(AiContext&,const Formats::AiPersonalityData5&){assert(_octolithFlagD4);assert(_flagBaseD8);if(_octolithFlagD4->Carrier())for (auto _enumerator12 = _scene.GetPlayerEntities().GetEnumerator(); _enumerator12.MoveNext(); )
            if (const auto o = _enumerator12.Current(); true)if(o!=_player&&o->TeamIndex()!=_octolithFlagD4->Carrier()->TeamIndex()&&DistanceSquared(_flagBaseD8->Position,o->Position)<9)return 1;return 0;}
        std::int32_t Func3_213B77C(AiContext& c,const Formats::AiPersonalityData5& p){return Func3_213B7A0(c,p)^1;}
        std::int32_t Func3_213B690(AiContext&,const Formats::AiPersonalityData5&){if(GameState::Mode()==GameMode::Capture)return 0;assert(_octolithFlagD4);assert(_octolithFlagDC);if(!_octolithFlagD4->Carrier())return 0;return DistanceSquared(_octolithFlagDC->BasePosition(),_player->Position)<DistanceSquared(_octolithFlagD4->Carrier()->Position,_player->Position)?1:0;}
        std::int32_t Func3_213B5DC(AiContext&,const Formats::AiPersonalityData5&){assert(_octolithFlagDC);assert(_flagBaseD8);return DistanceSquared(_octolithFlagDC->Position,_player->Position)<DistanceSquared(_flagBaseD8->Position,_player->Position)?1:0;}
        std::int32_t Func3_213B528(AiContext&,const Formats::AiPersonalityData5&){assert(_flagBaseD8);assert(_octolithFlagDC);return DistanceSquared(_flagBaseD8->Position,_player->Position)<DistanceSquared(_octolithFlagDC->Position,_player->Position)?1:0;}
        std::int32_t Func3_213B4E4(AiContext&,const Formats::AiPersonalityData5&){if((Flags2&AiFlags2::TargetDefense)==AiFlags2::None)return 0;assert(_targetDefense);return _targetDefense->Volume().TestPoint(_player->Volume().SpherePosition)?1:0;}
        std::int32_t Func3_213B4A0(AiContext& c,const Formats::AiPersonalityData5& p){return (Flags2&AiFlags2::TargetDefense)!=AiFlags2::None&&Func3_213B4E4(c,p)==0?1:0;}
        std::int32_t Func3_213B45C(AiContext&,const Formats::AiPersonalityData5&){if((Flags2&AiFlags2::TargetDefense)==AiFlags2::None)return 0;assert(_targetDefense);return _targetDefense->CapturedPlayer()&&_targetDefense->CapturedPlayer()->TeamIndex()==_player->TeamIndex()?1:0;}
        std::int32_t Func3_213B3F0(AiContext&,const Formats::AiPersonalityData5&){for (auto _enumerator13 = _scene.GetNodeDefenseEntities().GetEnumerator(); _enumerator13.MoveNext(); )
            if (const auto d = _enumerator13.Current(); true)if(d->CapturedPlayer()&&d->CapturedPlayer()->TeamIndex()==_player->TeamIndex()&&d->OccupiedBy()!=nullptr)return 1;return 0;}
        std::int32_t Func3_213B3A0(AiContext&,const Formats::AiPersonalityData5&){if((Flags2&AiFlags2::TargetDefense)==AiFlags2::None)return 0;assert(_targetDefense);return _targetDefense->CapturedPlayer()&&_targetDefense->CapturedPlayer()->TeamIndex()==_player->TeamIndex()&&_targetDefense->OccupiedBy()!=nullptr?1:0;}
        std::int32_t Func3_213B37C(AiContext& c,const Formats::AiPersonalityData5& p){return Func3_213B3A0(c,p)^1;}
        std::int32_t Func3_213B34C(AiContext&,const Formats::AiPersonalityData5&){if((Flags2&AiFlags2::TargetDefense)!=AiFlags2::None){assert(_targetDefense);return _targetDefense->Contested()?1:0;}return 0;}
        std::int32_t Func3_213B328(AiContext& c,const Formats::AiPersonalityData5& p){return Func3_213B34C(c,p)^1;}
        std::int32_t Func3_213B284(AiContext&,const Formats::AiPersonalityData5& p){if((Flags2&AiFlags2::TargetDefense)==AiFlags2::None)return 0;assert(_targetDefense);float d=p.Param1/4096.0F;return DistanceSquared(_targetDefense->Position,_player->Position)<d*d?1:0;}
        std::int32_t Func3_213B260(AiContext& c,const Formats::AiPersonalityData5& p){return Func3_213B284(c,p)^1;}
        std::int32_t Func3_213B1F0(AiContext&,const Formats::AiPersonalityData5&){for (auto _enumerator14 = _scene.GetNodeDefenseEntities().GetEnumerator(); _enumerator14.MoveNext(); )
            if (const auto d = _enumerator14.Current(); true)if(!d->CapturedPlayer()||d->CapturedPlayer()->TeamIndex()!=_player->TeamIndex()||d->OccupiedBy()!=nullptr)return 0;return 1;}
        std::int32_t Func3_213B1D8(AiContext&,const Formats::AiPersonalityData5&){return _weapon2==0?1:0;}
        std::int32_t Func3_213B1C0(AiContext&,const Formats::AiPersonalityData5&){return _weapon2!=0?1:0;}
        std::int32_t Func3_213B1A8(AiContext&,const Formats::AiPersonalityData5&){return _weapon2==1?1:0;}
        std::int32_t Func3_213B190(AiContext&,const Formats::AiPersonalityData5&){return _weapon2!=1?1:0;}
        std::int32_t Func3_213B178(AiContext&,const Formats::AiPersonalityData5&){return _weapon2==2?1:0;}
        std::int32_t Func3_213B160(AiContext&,const Formats::AiPersonalityData5&){return _weapon2!=2?1:0;}
        std::int32_t Func3_213B148(AiContext&,const Formats::AiPersonalityData5&){return _weapon2==3?1:0;}
        std::int32_t Func3_213B130(AiContext&,const Formats::AiPersonalityData5&){return _weapon2!=3?1:0;}
        std::int32_t Func3_213B118(AiContext&,const Formats::AiPersonalityData5&){return _weapon2==4?1:0;}
        std::int32_t Func3_213B100(AiContext&,const Formats::AiPersonalityData5&){return _weapon2!=4?1:0;}
        std::int32_t Func3_213B0E8(AiContext&,const Formats::AiPersonalityData5&){return _weapon2==5?1:0;}
        std::int32_t Func3_213B0D0(AiContext&,const Formats::AiPersonalityData5&){return _weapon2!=5?1:0;}
        std::int32_t Func3_213B0B8(AiContext&,const Formats::AiPersonalityData5&){return _weapon2==6?1:0;}
        std::int32_t Func3_213B0A0(AiContext&,const Formats::AiPersonalityData5&){return _weapon2!=6?1:0;}
        std::int32_t Func3_213B088(AiContext&,const Formats::AiPersonalityData5&){return _weapon2==7?1:0;}
        std::int32_t Func3_213B070(AiContext&,const Formats::AiPersonalityData5&){return _weapon2!=7?1:0;}
        std::int32_t Func3_213B058(AiContext&,const Formats::AiPersonalityData5&){return _weapon2==8?1:0;}
        std::int32_t Func3_213B040(AiContext&,const Formats::AiPersonalityData5&){return _weapon2!=8?1:0;}
        std::int32_t Func3_213B020(AiContext&,const Formats::AiPersonalityData5&){return _player->BotLevel()==0?1:0;}
        std::int32_t Func3_213B000(AiContext&,const Formats::AiPersonalityData5&){return _player->BotLevel()!=0?1:0;}
        std::int32_t Func3_213AFE0(AiContext&,const Formats::AiPersonalityData5&){return _player->BotLevel()==1?1:0;}
        std::int32_t Func3_213AFC0(AiContext&,const Formats::AiPersonalityData5&){return _player->BotLevel()!=1?1:0;}
        std::int32_t Func3_213AFA0(AiContext&,const Formats::AiPersonalityData5&){return _player->BotLevel()>=2?1:0;}
        std::int32_t Func3_213AF80(AiContext&,const Formats::AiPersonalityData5&){return _player->BotLevel()<2?1:0;}
        std::int32_t Func3_213AF68(AiContext&,const Formats::AiPersonalityData5&){return _player->Hunter()==Hunter::Samus?1:0;}
        std::int32_t Func3_213AF50(AiContext&,const Formats::AiPersonalityData5&){return _player->Hunter()!=Hunter::Samus?1:0;}
        std::int32_t Func3_213AF38(AiContext&,const Formats::AiPersonalityData5&){return _player->Hunter()==Hunter::Kanden?1:0;}
        std::int32_t Func3_213AF20(AiContext&,const Formats::AiPersonalityData5&){return _player->Hunter()!=Hunter::Kanden?1:0;}
        std::int32_t Func3_213AF08(AiContext&,const Formats::AiPersonalityData5&){return _player->Hunter()==Hunter::Spire?1:0;}
        std::int32_t Func3_213AEF0(AiContext&,const Formats::AiPersonalityData5&){return _player->Hunter()!=Hunter::Spire?1:0;}
        std::int32_t Func3_213AED8(AiContext&,const Formats::AiPersonalityData5&){return _player->Hunter()==Hunter::Noxus?1:0;}
        std::int32_t Func3_213AEC0(AiContext&,const Formats::AiPersonalityData5&){return _player->Hunter()!=Hunter::Noxus?1:0;}
        std::int32_t Func3_213AEA8(AiContext&,const Formats::AiPersonalityData5&){return _player->Hunter()==Hunter::Trace?1:0;}
        std::int32_t Func3_213AE90(AiContext&,const Formats::AiPersonalityData5&){return _player->Hunter()!=Hunter::Trace?1:0;}
        std::int32_t Func3_213AE78(AiContext&,const Formats::AiPersonalityData5&){return _player->Hunter()==Hunter::Sylux?1:0;}
        std::int32_t Func3_213AE60(AiContext&,const Formats::AiPersonalityData5&){return _player->Hunter()!=Hunter::Sylux?1:0;}
        std::int32_t Func3_213AE48(AiContext&,const Formats::AiPersonalityData5&){return _player->Hunter()==Hunter::Weavel?1:0;}
        std::int32_t Func3_213AE30(AiContext&,const Formats::AiPersonalityData5&){return _player->Hunter()!=Hunter::Weavel?1:0;}
        std::int32_t Func3_213AE14(AiContext&,const Formats::AiPersonalityData5&){return GameState::Mode()==GameMode::Capture?1:0;}
        std::int32_t Func3_213ADF8(AiContext&,const Formats::AiPersonalityData5&){return GameState::Mode()!=GameMode::Capture?1:0;}
        std::int32_t Func3_213ADC4(AiContext&,const Formats::AiPersonalityData5&){return GameState::Mode()==GameMode::PrimeHunter&&_player->IsPrimeHunter()?1:0;}
        std::int32_t Func3_213ADA0(AiContext& c,const Formats::AiPersonalityData5& p){return Func3_213ADC4(c,p)^1;}
        std::int32_t Func3_213AD88(AiContext&,const Formats::AiPersonalityData5& p){return _field30==p.Param1?1:0;}
        std::int32_t Func3_213AD64(AiContext& c,const Formats::AiPersonalityData5& p){return Func3_213AD88(c,p)^1;}
        std::int32_t Func3_213ACE8(AiContext&,const Formats::AiPersonalityData5&)
        {
            if(GameState::Mode()==GameMode::Capture&&_player->TeamIndex()==0&&_field30>100)return 1;
            int navIndex=_nodeTypeIndex[static_cast<int>(NodeType::Navigation)],specIndex=_nodeTypeIndex[static_cast<int>(NodeType::Special)];
            if(navIndex>=specIndex)return 1;
            while(navIndex<specIndex){auto node=(*_nodeList)[navIndex];if(node->Field4==_field30)return 0;navIndex++;}return 1;
        }
        std::int32_t Func3_213ACCC(AiContext&,const Formats::AiPersonalityData5& p){return _player->Position.Y<p.Param1/4096.0F?1:0;}
        std::int32_t Func3_213ACA8(AiContext&,const Formats::AiPersonalityData5& p){return _player->Position.Y>=p.Param1/4096.0F?1:0;}
        std::int32_t Func3_213AC8C(AiContext&,const Formats::AiPersonalityData5& p){return _player->Position.X>p.Param1/4096.0F?1:0;}
        std::int32_t Func3_213AC70(AiContext&,const Formats::AiPersonalityData5& p){return _player->Position.X<p.Param1/4096.0F?1:0;}
        std::int32_t Func3_213AC54(AiContext&,const Formats::AiPersonalityData5& p){return _player->Position.Z>p.Param1/4096.0F?1:0;}
        std::int32_t Func3_213AC38(AiContext&,const Formats::AiPersonalityData5& p){return _player->Position.Z<p.Param1/4096.0F?1:0;}
        std::int32_t Func3_213AC04(AiContext&,const Formats::AiPersonalityData5& p){return (Flags2&AiFlags2::TargetPlayer)!=AiFlags2::None&&_targetPlayer&&_targetPlayer->Position.Y<p.Param1/4096.0F?1:0;}
        std::int32_t Func3_213ABC0(AiContext&,const Formats::AiPersonalityData5& p){return (Flags2&AiFlags2::TargetPlayer)!=AiFlags2::None&&_targetPlayer&&_targetPlayer->Position.Y>=p.Param1/4096.0F?1:0;}
        std::int32_t Func3_213AB8C(AiContext&,const Formats::AiPersonalityData5& p){return (Flags2&AiFlags2::TargetPlayer)!=AiFlags2::None&&_targetPlayer&&_targetPlayer->Position.X>p.Param1/4096.0F?1:0;}
        std::int32_t Func3_213AB58(AiContext&,const Formats::AiPersonalityData5& p){return (Flags2&AiFlags2::TargetPlayer)!=AiFlags2::None&&_targetPlayer&&_targetPlayer->Position.X<p.Param1/4096.0F?1:0;}
        std::int32_t Func3_213AB24(AiContext&,const Formats::AiPersonalityData5& p){return (Flags2&AiFlags2::TargetPlayer)!=AiFlags2::None&&_targetPlayer&&_targetPlayer->Position.Z>p.Param1/4096.0F?1:0;}
        std::int32_t Func3_213AAF0(AiContext&,const Formats::AiPersonalityData5& p){return (Flags2&AiFlags2::TargetPlayer)!=AiFlags2::None&&_targetPlayer&&_targetPlayer->Position.Z<p.Param1/4096.0F?1:0;}
        std::int32_t Func3_213AA64(AiContext&,const Formats::AiPersonalityData5& p){if((Flags2&AiFlags2::TargetPlayer)==AiFlags2::None)return 0;assert(_targetPlayer);float d=p.Param1/4096.0F;return LengthSquared(_targetPlayer->Position)<d*d?1:0;}
        std::int32_t Func3_213AA20(AiContext& c,const Formats::AiPersonalityData5& p){return (Flags2&AiFlags2::TargetPlayer)!=AiFlags2::None&&Func3_213AA64(c,p)==0?1:0;}
        std::int32_t Func3_213A9B8(AiContext&,const Formats::AiPersonalityData5&){if((Flags2&AiFlags2::TargetPlayer)==AiFlags2::None)return 0;if(GameState::RadarPlayers())return 31;assert(_targetPlayer);if(HasFlag(_targetPlayer->Flags2(), PlayerFlags2::RadarReveal)||_targetPlayer->OctolithFlag()||_targetPlayer->IsPrimeHunter())return 31;return static_cast<int>(_targetPlayer->CurAlpha()*31);}
        std::int32_t Func3_213A94C(AiContext&,const Formats::AiPersonalityData5&){if((Flags2&AiFlags2::TargetPlayer)==AiFlags2::None)return 31;if(GameState::RadarPlayers())return 0;assert(_targetPlayer);if(HasFlag(_targetPlayer->Flags2(), PlayerFlags2::RadarReveal)||_targetPlayer->OctolithFlag()||_targetPlayer->IsPrimeHunter())return 0;return 31-static_cast<int>(_targetPlayer->CurAlpha()*31);}
        std::int32_t Func3_213A938(AiContext&,const Formats::AiPersonalityData5&){return (Flags4&AiFlags4::Bit2)!=AiFlags4::None?1:0;}
        std::int32_t Func3_213A91C(AiContext&,const Formats::AiPersonalityData5&){return (Flags4&AiFlags4::Bit2)!=AiFlags4::None?0:1;}
        std::int32_t Func3_213A900(AiContext&,const Formats::AiPersonalityData5&){return _player->_deathaltTimer!=0?1:0;}
        std::int32_t Func3_213A8DC(AiContext& c,const Formats::AiPersonalityData5& p){return Func3_213A900(c,p)^1;}
        std::int32_t Func3_213A8A8(AiContext&,const Formats::AiPersonalityData5&){if((Flags2&AiFlags2::TargetPlayer)==AiFlags2::None)return 0;assert(_targetPlayer);return _targetPlayer->_deathaltTimer!=0?1:0;}
        std::int32_t Func3_213A884(AiContext& c,const Formats::AiPersonalityData5& p){return Func3_213A8A8(c,p)^1;}
        std::int32_t Func3_213A868(AiContext&,const Formats::AiPersonalityData5&){return _player->_timeSinceGrounded>60?1:0;}
        std::int32_t Func3_213A844(AiContext& c,const Formats::AiPersonalityData5& p){return Func3_213A868(c,p)^1;}
        std::int32_t Func3_213A828(AiContext&,const Formats::AiPersonalityData5&){return HasFlag(_player->Flags1(), PlayerFlags1::UsedJump)?0:1;}
        std::int32_t Func3_213A804(AiContext& c,const Formats::AiPersonalityData5& p){return Func3_213A828(c,p)^1;}
        std::int32_t Func3_213A798(AiContext&,const Formats::AiPersonalityData5&){for (auto _enumerator15 = _scene.GetPlayerEntities().GetEnumerator(); _enumerator15.MoveNext(); )
            if (const auto o = _enumerator15.Current(); true)if(o!=_player&&o->IsBot()&&(o->AiData->Flags2&AiFlags2::Bit18)!=AiFlags2::None)return 1;return 0;}
        std::int32_t Func3_213A72C(AiContext&,const Formats::AiPersonalityData5&){for (auto _enumerator16 = _scene.GetPlayerEntities().GetEnumerator(); _enumerator16.MoveNext(); )
            if (const auto o = _enumerator16.Current(); true)if(o!=_player&&o->IsBot()&&(o->AiData->Flags2&AiFlags2::Bit19)!=AiFlags2::None)return 1;return 0;}
        std::int32_t Func3_213A714(AiContext&,const Formats::AiPersonalityData5&){return _player->_horizColTimer>20?1:0;}
        std::int32_t Func3_213A698(AiContext&,const Formats::AiPersonalityData5&){if((_scene.RoomId()==114||_scene.RoomId()==112)&&!VectorEqual(_player->Speed(), Vector3::Zero)&&!_player->IsAltForm())return 0;return _player->_standTerrain==Terrain::Lava?1:0;}
        std::int32_t Func3_213A688(AiContext&,const Formats::AiPersonalityData5&){return (Flags2&AiFlags2::AiStart)!=AiFlags2::None?1:0;}
        std::int32_t Func3_213A660(AiContext&,const Formats::AiPersonalityData5& p){return p.Param1+static_cast<int>(Rng::GetRandomInt2(p.Param2-p.Param1));}
        std::int32_t Func3_213A650(AiContext&,const Formats::AiPersonalityData5&){return (Flags2&AiFlags2::Bit13)!=AiFlags2::None?1:0;}

        void Func4_21462DC(AiContext& context)
        {
            Func214715C(context);
            if (context.FieldA == 31) Flags2 &= ~AiFlags2::Bit0;
            Vector3 targetPos{};
            Vector3 halfturretPos{};
            if (context.Field9 == 4 || context.FieldB == 4 || context.Field9 == 41 || context.FieldC == 56
                || context.FieldC == 57 || context.FieldC == 59 || context.Field5 == 58)
            {
                if (context.Field7 == 1) Func2135510();
                if (context.Field7 == 2) Func21354B0();
                if ((Flags2 & AiFlags2::TargetPlayer) != AiFlags2::None && _targetPlayer)
                {
                    _targetPlayer->GetPosition(targetPos);
                    targetPos = AddY(targetPos, _targetPlayer->IsAltForm() ? Fixed::ToFloat(_targetPlayer->Values().AltColYPos) : 0.5F);
                }
            }
            if ((Flags2 & AiFlags2::TargetHalfturret) != AiFlags2::None && _targetHalfturret
                && (context.Field9 == 5 || context.FieldB == 5 || context.FieldC == 60))
            {
                _targetHalfturret->GetPosition(halfturretPos);
            }
            if (context.Field9 == 6)
            {
                if (context.Field8 == 7) { FindEntityRef(AiEntRefType::Type55); UpdateTargetItem(_entityRefs.Field55); }
                else if (context.Field8 == 8) { FindEntityRef(AiEntRefType::Type56); UpdateTargetItem(_entityRefs.Field56); }
                else if (context.Field8 == 9) { FindEntityRef(AiEntRefType::Type57); UpdateTargetItem(_entityRefs.Field57); }
                else if (context.Field8 == 10) { FindEntityRef(AiEntRefType::Type58); UpdateTargetItem(_entityRefs.Field58); }
                else if (context.Field8 == 11) { FindEntityRef(AiEntRefType::Type59); UpdateTargetItem(_entityRefs.Field59); }
            }
            if (context.Field9 == 23 && (Flags2 & AiFlags2::TargetDefense) == AiFlags2::None) Func2135320();
            if (context.FieldC == 61 && (Flags2 & AiFlags2::TargetDoor) == AiFlags2::None) Func21355D8();
            if (context.FieldA == 32)
            {
                if (context.FieldB == 4 && (Flags2 & AiFlags2::TargetPlayer) != AiFlags2::None)
                    _field1038 = targetPos - _player->CameraInfo()->Position;
                else if (context.FieldB == 3 && (Flags2 & AiFlags2::TargetPlayer) != AiFlags2::None)
                {
                    AiPlayerAggro* aggro = AggroFunc214847C(4,7,1,{},{});
                    if (aggro && aggro->Player1) _field1038 = aggro->Player1->Position - _player->CameraInfo()->Position;
                    else _field1038 = _player->CameraInfo()->Facing;
                }
                else if (context.FieldB == 5 && (Flags2 & AiFlags2::TargetHalfturret) != AiFlags2::None)
                    _field1038 = halfturretPos - _player->CameraInfo()->Position;
                else if (context.FieldB == 25)
                {
                    float x=Rng::GetRandomInt2(4096)/4096.0F-0.5F;
                    float y=Rng::GetRandomInt2(4096)/4096.0F-0.5F;
                    float z=Rng::GetRandomInt2(4096)/4096.0F-0.5F;
                    _field1038=Vector3(x,y,z);
                }
                else if (context.FieldB == 26)
                {
                    float x=Rng::GetRandomInt2(4096)/4096.0F-0.5F;
                    float z=Rng::GetRandomInt2(4096)/4096.0F-0.5F;
                    float length=std::sqrt(x*x+z*z);
                    float y=Rng::GetRandomInt2(Fixed::ToInt(length))/4096.0F-length/2;
                    _field1038=Vector3(x,y,z);
                }
                else if (context.FieldB == 27) _field1038 = _fieldB8 - _player->CameraInfo()->Position;
                if (!VectorEqual(_field1038, Vector3::Zero)) _field1038 = _field1038.Normalized(); else _field1038 = _player->CameraInfo()->Facing;
            }
            if (context.Field4 != 0)
            {
                Field118=0; _field78=0; Flags2&=~AiFlags2::Bit15; context.Field40=0; context.Field44=0; context.Field34=_player->Position;
            }
            std::shared_ptr<Formats::NodeData3> v25{};
            if (context.Field4 == 37 || context.Field9 == 39)
            {
                bool node40Set=false;
                if ((Flags2&AiFlags2::Bit7)!=AiFlags2::None && context.Field4==37)
                {
                    FindEntityRef(AiEntRefType::Type0); _node40=_entityRefs.Field0;
                    if(_node40==_node48)
                    {
                        v25=Func213A1A8();
                        if(v25==_field4C[0]){_node44=_node40;_node40=v25;node40Set=true;}
                    }
                    else if(!HasFlag(_player->Flags1(), PlayerFlags1::Grounded)){_node40=_field4C[0];node40Set=true;}
                }
                if(!node40Set){FindEntityRef(AiEntRefType::Type1);_node40=_entityRefs.Field1;}
                assert(_node40);
                context.Field18=(_node40->NodeType!=NodeType::AltForm||_player->Hunter()==Hunter::Guardian)?1:2;
            }
            if(context.Field4==33)
            {
                if(context.Field9==12){FindEntityRef(AiEntRefType::Type25);_node3C=_node40=_entityRefs.Field25;}
                else if(context.Field9==13){FindEntityRef(AiEntRefType::Type24);_node3C=_node40=_entityRefs.Field24;}
                else if(context.Field9==39)_node3C=_node40;
                else if(context.Field9!=4)
                {
                    std::optional<Vector3> position{};
                    if(context.Field9==5){assert(_targetHalfturret);position=_targetHalfturret->Position;}
                    else if(context.Field9==6){if((Flags2&AiFlags2::TargetItem)!=AiFlags2::None){assert(_itemC8);position=AddY(_itemC8->Position, -0.5F);}}
                    else if(context.Field9==14){assert(_octolithFlagCC);position=_octolithFlagCC->Position;}
                    else if(context.Field9==15){assert(_octolithFlagCC);position=_octolithFlagCC->BasePosition();}
                    else if(context.Field9==16){assert(_flagBaseD0);position=_flagBaseD0->Position;}
                    else if(context.Field9==17){assert(_octolithFlagD4);position=_octolithFlagD4->Position;}
                    else if(context.Field9==18){assert(_octolithFlagD4);position=_octolithFlagD4->BasePosition();}
                    else if(context.Field9==19){assert(_flagBaseD8);position=_flagBaseD8->Position;}
                    else if(context.Field9==20){assert(_octolithFlagDC);position=_octolithFlagDC->Position;}
                    else if(context.Field9==21){assert(_octolithFlagDC);position=_octolithFlagDC->BasePosition();}
                    else if(context.Field9==22){assert(_flagBaseE0);position=_flagBaseE0->Position;}
                    else if(context.Field9==23&&(Flags2&AiFlags2::TargetDefense)!=AiFlags2::None){assert(_targetDefense);position=_targetDefense->Position;}
                    if(position)
                    {
                        auto node=FindClosestNonHazardNodeToPosition(*position);
                        if(node->NodeType==NodeType::AltForm&&_player->Hunter()!=Hunter::Guardian&&context.FieldD!=29){context.FieldD=29;context.FieldE=0;}
                    }
                }
            }
            else if(context.Field4==34)
            {
                if(context.Field9==4&&(Flags2&AiFlags2::TargetPlayer)!=AiFlags2::None){assert(_targetPlayer);_fieldAC=WithY((static_cast<Vector3>(_targetPlayer->Position) - static_cast<Vector3>(_player->Position)), 0);}
                else if(context.Field9==5&&(Flags2&AiFlags2::TargetHalfturret)!=AiFlags2::None){assert(_targetHalfturret);_fieldAC=WithY((static_cast<Vector3>(_targetHalfturret->Position) - static_cast<Vector3>(_player->Position)), 0);}
                if(_fieldAC.X!=0||_fieldAC.Z!=0)_fieldAC=_fieldAC.Normalized();
            }
            else if(context.Field4==37)
            {
                context.Field20=0;context.Field24=1;context.Field14=0;context.Field18=0;context.Field1C=0;
                if(context.Field5==47)context.Field2C=1;else if(context.Field5==48)context.Field2C=2;else context.Field2C=0;
                context.Field28=context.FieldD==29;
                context.Field30=context.FieldA==0&&context.FieldC!=55&&context.FieldC!=56&&context.FieldC!=57&&context.FieldC!=59&&context.FieldC!=60&&context.FieldC!=61;
                if(context.Field9==4&&(Flags2&AiFlags2::TargetPlayer)!=AiFlags2::None){FindEntityRef(AiEntRefType::Type2);_node3C=_entityRefs.Field2;_queuedFindEntityAction=AiQueuedEnt::Type0;}
                else if(context.Field9==5&&(Flags2&AiFlags2::TargetHalfturret)!=AiFlags2::None){FindEntityRef(AiEntRefType::Type3);_node3C=_entityRefs.Field3;_queuedFindEntityAction=AiQueuedEnt::Type8;}
                else if(context.Field9==6&&(Flags2&AiFlags2::TargetItem)!=AiFlags2::None)
                {
                    FindEntityRef(AiEntRefType::Type5);_node3C=_entityRefs.Field5;
                    if(context.Field8==7)_queuedFindEntityAction=AiQueuedEnt::Type12;else if(context.Field8==8)_queuedFindEntityAction=AiQueuedEnt::Type13;
                    else if(context.Field8==9)_queuedFindEntityAction=AiQueuedEnt::Type14;else if(context.Field8==10)_queuedFindEntityAction=AiQueuedEnt::Type15;
                    else if(context.Field8==11)_queuedFindEntityAction=AiQueuedEnt::Type16;else _queuedFindEntityAction=AiQueuedEnt::None;
                }
                else if(context.Field9==12){FindEntityRef(AiEntRefType::Type25);_node3C=_entityRefs.Field25;_queuedFindEntityAction=AiQueuedEnt::None;}
                else if(context.Field9==13){FindEntityRef(AiEntRefType::Type24);_node3C=_entityRefs.Field24;_queuedFindEntityAction=AiQueuedEnt::None;}
                else if(context.Field9==14){FindEntityRef(AiEntRefType::Type6);_node3C=_entityRefs.Field6;_queuedFindEntityAction=AiQueuedEnt::Type27;}
                else if(context.Field9==15){FindEntityRef(AiEntRefType::Type7);_node3C=_entityRefs.Field7;_queuedFindEntityAction=AiQueuedEnt::Type28;}
                else if(context.Field9==16){FindEntityRef(AiEntRefType::Type8);_node3C=_entityRefs.Field8;_queuedFindEntityAction=AiQueuedEnt::Type29;}
                else if(context.Field9==17){FindEntityRef(AiEntRefType::Type9);_node3C=_entityRefs.Field9;_queuedFindEntityAction=AiQueuedEnt::Type30;}
                else if(context.Field9==18){FindEntityRef(AiEntRefType::Type10);_node3C=_entityRefs.Field10;_queuedFindEntityAction=AiQueuedEnt::Type31;}
                else if(context.Field9==19){FindEntityRef(AiEntRefType::Type11);_node3C=_entityRefs.Field11;_queuedFindEntityAction=AiQueuedEnt::Type32;}
                else if(context.Field9==20){FindEntityRef(AiEntRefType::Type12);_node3C=_entityRefs.Field12;_queuedFindEntityAction=AiQueuedEnt::Type33;}
                else if(context.Field9==21){FindEntityRef(AiEntRefType::Type13);_node3C=_entityRefs.Field13;_queuedFindEntityAction=AiQueuedEnt::Type34;}
                else if(context.Field9==22){FindEntityRef(AiEntRefType::Type14);_node3C=_entityRefs.Field14;_queuedFindEntityAction=AiQueuedEnt::Type35;}
                else if(context.Field9==23){FindEntityRef(AiEntRefType::Type15);_node3C=_entityRefs.Field15;_queuedFindEntityAction=AiQueuedEnt::Type36;}
                else if(context.Field9==24){_node3C=GetRandomNavigationNode();_queuedFindEntityAction=AiQueuedEnt::None;}
                else if(context.Field9==40){_node3C=FindHighestNode();_queuedFindEntityAction=AiQueuedEnt::Type9;}
                else if(context.Field9==41&&(Flags2&AiFlags2::TargetPlayer)!=AiFlags2::None){FindEntityRef(AiEntRefType::Type17);_node3C=_entityRefs.Field17;_queuedFindEntityAction=AiQueuedEnt::Type3;}
                else if(context.Field9==42&&(Flags2&AiFlags2::TargetPlayer)!=AiFlags2::None){FindEntityRef(AiEntRefType::Type18);_node3C=_entityRefs.Field18;_queuedFindEntityAction=AiQueuedEnt::Type4;}
                else if(context.Field9==44){FindEntityRef(AiEntRefType::Type23);_node3C=_entityRefs.Field23;_queuedFindEntityAction=AiQueuedEnt::None;}
                else if(context.Field9==45){FindEntityRef(AiEntRefType::Type22);_node3C=_entityRefs.Field22;_queuedFindEntityAction=AiQueuedEnt::None;}
                else if(context.Field9==46){FindEntityRef(AiEntRefType::Type21);_node3C=_entityRefs.Field21;_queuedFindEntityAction=AiQueuedEnt::None;}
                else {if((Flags2&AiFlags2::Bit1)!=AiFlags2::None)FindQueuedEntityRef();else _node3C=_node40;_queuedFindEntityAction=AiQueuedEnt::None;}
                if(_node40&&IsNodeInRange(*_node40))
                {
                    if(!v25)v25=Func213A1A8();bool v33=false;
                    for(int i=0;i<_node40->Count2;i++)if((*_node40->Values)[_node40->Index2+i]==v25->Id){v33=true;break;}
                    if(!v33)
                    {
                        _node48=_node44=_node40;_field4C[0]=_node40=v25;
                        context.Field18=(_node40->NodeType!=NodeType::AltForm||_player->Hunter()==Hunter::Guardian)?1:2;Flags2|=AiFlags2::Bit7;
                    }
                }
            }
            if(context.Field5==47){_field90=_player->Position;_field9C=0.5F;}
            if(context.Field6==51&&!_player->IsAltForm()&&!_player->IsMorphing()&&!HasFlag(_player->Flags1(), PlayerFlags1::UsedJump)&&_buttons.L.FramesUp>10)_buttons.L.IsDown=true;
        }

        void Func4_2145EB0(AiContext& context){context.Field40=0;context.Field34=_player->Position;if(Rng::GetRandomInt2(2)==0)Flags2&=~AiFlags2::Bit12;else Flags2|=AiFlags2::Bit12;}
        void Func4_21462AC(AiContext&){if(_player->IsAltForm())_touchButtons.Unmorph.IsDown=true;}
        void Func4_2146284(AiContext& c){Func4_21462AC(c);}
        void Func4_21461EC(AiContext& c)
        {FindEntityRef(AiEntRefType::Type1);_node40=_entityRefs.Field1;if((Flags2&AiFlags2::TargetPlayer)!=AiFlags2::None){FindEntityRef(AiEntRefType::Type17);_node3C=_entityRefs.Field17;}else _node3C=_node40;Field118=0;_field78=0;Flags2&=~AiFlags2::Bit15;_queuedFindEntityAction=AiQueuedEnt::Type3;c.Field20=0;c.Field24=1;c.Field14=0;c.Field18=0;c.Field1C=0;c.Field2C=0;c.Field28=true;c.Field30=false;}
        void Func4_214612C(AiContext& c)
        {Field118=0;_field78=0;Flags2&=~AiFlags2::Bit15;c.Field40=0;c.Field44=0;c.Field34=_player->Position;c.Field20=0;c.Field24=1;c.Field14=0;c.Field18=0;c.Field1C=0;c.Field2C=0;c.Field28=true;c.Field30=true;FindEntityRef(AiEntRefType::Type1);_node40=_entityRefs.Field1;FindEntityRef(AiEntRefType::Type25);_node3C=_entityRefs.Field25;_queuedFindEntityAction=AiQueuedEnt::None;assert(_node40);if(IsNodeInRange(*_node40)){auto node=Func213A1A8();_node44=_node40;_node40=node;}}
        void Func4_2145F78(AiContext& c)
        {if((Flags2&AiFlags2::TargetDefense)==AiFlags2::None)Func2135320();Field118=0;_field78=0;Flags2&=~AiFlags2::Bit15;c.Field40=0;c.Field44=0;c.Field34=_player->Position;if((Flags2&AiFlags2::TargetDefense)!=AiFlags2::None){assert(_targetDefense);float radius=0;if(_targetDefense->Volume().Type==VolumeType::Cylinder)radius=_targetDefense->Volume().CylinderRadius;else if(_targetDefense->Volume().Type==VolumeType::Sphere)radius=_targetDefense->Volume().SphereRadius;if(radius>0.5F){auto t=WithY((static_cast<Vector3>(_targetDefense->Position) - static_cast<Vector3>(_player->Position)), 0);float x=Rng::GetRandomInt2(4096)/4096.0F;_fieldA0=Vector3(x*std::copysign(1.0F,t.X),0,std::sqrt(1-x*x)*std::copysign(1.0F,t.Z));_fieldA0 = ScaleVector(_fieldA0, radius);_fieldA0 = _fieldA0 + static_cast<Vector3>(_targetDefense->Position);}}}
        void Func4_2145F50(AiContext& c){Func4_21462AC(c);} void Func4_2145F28(AiContext& c){Func4_21462AC(c);} void Func4_2145F00(AiContext& c){Func4_21462AC(c);}
        void Func4_2145E54(AiContext& c){FindEntityRef(AiEntRefType::Type0);_node40=_entityRefs.Field0;_node3C=_node40;Field118=0;c.Field40=0;c.Field44=0;c.Field34=_player->Position;}
        void Func4_2145E40(AiContext&){Flags3|=AiFlags3::Bit1;} void Func4_SetDespawned(AiContext&){Flags3|=AiFlags3::Despawned;}

        void Func21356C0(const std::shared_ptr<PlayerEntity>& player)
        {
            if ((Flags2 & AiFlags2::Bit9) != AiFlags2::None) Flags2 &= ~AiFlags2::TargetPlayer;
            else Flags2 |= AiFlags2::TargetPlayer;
            if (player != _targetPlayer)
            {
                _targetPlayer = player;
                _entityRefs.Field2.reset(); _entityRefs.Field17.reset(); _entityRefs.Field18.reset(); _entityRefs.Field29.reset();
            }
        }
        void Func2135624(const std::shared_ptr<NodeDefenseEntity>& defense)
        {
            if (defense)
            {
                Flags2 |= AiFlags2::TargetDefense;
                if (_targetDefense != defense) { _targetDefense = defense; _entityRefs.Field15.reset(); }
            }
        }
        void Func2135608(const std::shared_ptr<DoorEntity>& door)
        { Flags2 |= AiFlags2::TargetDoor; if (_targetDoor != door) _targetDoor = door; }
        void CheckUnmorph(){if(_player->IsAltForm()&&_touchButtons.Unmorph.FramesUp>20)_touchButtons.Unmorph.IsDown=true;}

        static const std::array<float,4> _dotValues;
        static const std::array<float,4> _aimValues;

        void Func2145C14(Vector3 position)
        {
            Vector3 vec1=_player->IsAltForm()?Vector3(_player->_field80,0,_player->_field84):Vector3(_player->_field70,0,_player->_field74);
            Vector3 vec2=WithY((position-_player->Position), 0);
            if(vec2.X!=0||vec2.Z!=0)vec2=vec2.Normalized();else vec2=vec1;
            float dot=Vector3::Dot(vec1,vec2);
            if(dot>255/256.0F)Flags2|=AiFlags2::Bit0;else Flags2&=~AiFlags2::Bit0;
            if(dot<1)
            {
                if(dot>_dotValues[_player->BotLevel()])_buttonAimX=RadiansToDegrees(std::acos(dot));else _buttonAimX=_aimValues[_player->BotLevel()];
                if(Vector3::Cross(vec1,vec2).Y<0)_buttonAimX*=-1;
            }
        }
        void Func21447E8()
        {
            Vector3 vec1(_player->CameraInfo()->Field48,0,_player->CameraInfo()->Field4C);
            Vector3 vec2=(_field1038.X!=0||_field1038.Z!=0)?WithY(_field1038, 0).Normalized():vec1;
            float dot=Vector3::Dot(vec1,vec2);float value=_aimValues[_player->BotLevel()];
            if(dot<1){if(dot>_dotValues[_player->BotLevel()])_buttonAimX=RadiansToDegrees(std::acos(dot));else _buttonAimX=value;if(Vector3::Cross(vec1,vec2).Y<0)_buttonAimX*=-1;}
            float angle1=90-RadiansToDegrees(std::acos(_player->CameraInfo()->Facing.Y));
            float angle2=90-RadiansToDegrees(std::acos(_field1038.Y));
            _buttonAimY=std::clamp(angle2-angle1,-value,value);
        }
        void Func21436D8()
        {
            if((Flags2&AiFlags2::TargetPlayer)!=AiFlags2::None)
            {
                assert(_targetPlayer);Func2144B88();auto vec=ExecuteVectorFunc(0,false,false);
                if(((Flags2&AiFlags2::Bit8)!=AiFlags2::None&&Func213842C()&&(IsPlayerVisible(*_player,*_targetPlayer)||_weapon1==7))||LengthSquared(vec)<10)Func2143A40();
                else if((Flags4&AiFlags4::Bit1)!=AiFlags4::None||_weapon1<=1)Func214380C();
            }
            else if((Flags4&AiFlags4::Bit1)!=AiFlags4::None)Func214380C();
        }

        void Func2144B88()
        {
            if((Flags2&AiFlags2::TargetPlayer)==AiFlags2::None)return;
            assert(_targetPlayer);
            Vector3 toTarget=static_cast<Vector3>(_targetPlayer->Position) - static_cast<Vector3>(_player->Position);float targetDist=Length(toTarget);
            if(_field1020==0||Vector3::Dot(toTarget,_player->_facingVector)<0)
            {
                Vector3 targetPos;_targetPlayer->GetPosition(targetPos);int prevField1020=_field1020;
                if((Flags2&AiFlags2::Bit21)!=AiFlags2::None)_field1020=0;
                if(_player->BotLevel()==0)_field1020=30;else if(_player->BotLevel()==1)_field1020=14;else _field1020=6;
                if(_field1020<_player->_disruptedTimer)_field1020+=static_cast<int>(Rng::GetRandomInt2(_player->_disruptedTimer-_field1020));
                int field1020Diff=_field1020-prevField1020;
                if((Flags4&AiFlags4::Bit3)!=AiFlags4::None&&field1020Diff>0&&_player->BotLevel()>0)
                {
                    const auto& equip = *_player->EquipInfo(); const WeaponInfo& weapon = *equip.Weapon;bool isCharged=false;float chargePct=0;
                    if(HasFlag(weapon.Flags, WeaponFlags::PartialCharge))
                    {
                        if(HasFlag(weapon.Flags, WeaponFlags::CanCharge)&&equip.ChargeLevel>=weapon.MinCharge*2)
                        {isCharged=true;chargePct=(equip.ChargeLevel-weapon.MinCharge*2)/static_cast<float>(weapon.FullCharge*2-weapon.MinCharge*2);}
                    }
                    else if(equip.ChargeLevel>=weapon.FullCharge*2){isCharged=true;chargePct=1;}
                    Vector3 vec = DivideVector(targetPos - _field1054, field1020Diff / 2.0F);float homing;float speed;
                    if(isCharged)
                    {
                        homing=(weapon.MinChargeHoming+((weapon.ChargedHoming-weapon.MinChargeHoming)*chargePct))/4096.0F/2;
                        speed=(weapon.MinChargeSpeed+((weapon.ChargedSpeed-weapon.MinChargeSpeed)*chargePct))/4096.0F/2;
                    }
                    else {homing=weapon.UnchargedHoming/4096.0F/2;speed=weapon.UnchargedSpeed/4096.0F/2;}
                    if(homing>0||speed<=0)vec = ScaleVector(vec, (_field1020 / 2.0F) / 2.0F);
                    else
                    {
                        Vector3 muzzleTarget=targetPos-_player->_muzzlePos;float muzzleDist=Length(muzzleTarget);vec = ScaleVector(vec, muzzleDist);
                        std::uint16_t decay=(*weapon.SpeedDecayTimes)[isCharged ? 1 : 0];float finalSpeed;
                        if(decay==0)finalSpeed=speed;
                        else if(isCharged)finalSpeed=(weapon.MinChargeFinalSpeed+((weapon.ChargedFinalSpeed-weapon.MinChargeFinalSpeed)*chargePct))/4096.0F/2;
                        else finalSpeed=weapon.UnchargedFinalSpeed/4096.0F/2;
                        vec = DivideVector(vec, finalSpeed);
                        if (_player->BotLevel() >= 3 && finalSpeed > 0.0F)
                        {
                            // Player speed is stored per 30 Hz game frame; projectiles move per 60 Hz tick.
                            const Vector3 realTargetVel = ScaleVector(_targetPlayer->Speed(), 0.5F);
                            Vector3 lead = ScaleVector(realTargetVel, muzzleDist / finalSpeed);
                            for (std::int32_t i = 0; i < 4; ++i)
                            {
                                const float distance = Length(targetPos + lead - _player->_muzzlePos);
                                lead = ScaleVector(realTargetVel, distance / finalSpeed);
                            }
                            vec = lead;
                        }
                    }
                    _field1048=targetPos+vec;
                }
                else _field1048=targetPos;
                _field1054=targetPos;Flags4|=AiFlags4::Bit3;
                _field1048=AddY(_field1048, _targetPlayer->IsAltForm()?Fixed::ToFloat(_targetPlayer->Values().AltColYPos):0.5F);
                Vector3 speedDiff=_player->Speed()-_targetPlayer->Speed();Vector3 camVec(_player->CameraInfo()->Field50,0,_player->CameraInfo()->Field54);
                float dot1=std::abs(Vector3::Dot(speedDiff,camVec));float dot2=std::abs(Vector3::Dot(speedDiff,_player->CameraInfo()->UpVector));float v52;float v66;
                if(_player->BotLevel()==0){v52=dot1*5+0.25F;v66=dot2*5+0.25F;if((Flags4&AiFlags4::Bit2)==AiFlags4::None){v52+=targetDist/2;v66+=targetDist/2;}}
                else if(_player->BotLevel()==1){v52=dot1*2+0.1F;v66=dot2*2+0.1F;if((Flags4&AiFlags4::Bit2)==AiFlags4::None){v52+=targetDist/9;v66+=targetDist/9;}}
                else if(_player->BotLevel()==2){v52=dot1*0.2F+0.01F;v66=dot2*0.2F+0.01F;if((Flags4&AiFlags4::Bit2)==AiFlags4::None){v52+=targetDist/50;v66+=targetDist/50;}}
                else {v52=0.0F;v66=0.0F;}
                if(_player->_disruptedTimer>0){v52*=2;v66*=2;}if((Flags2&AiFlags2::Bit21)!=AiFlags2::None){v52/=2;v66/=2;}if(_player->ShockCoilTimer()>20){v52/=2;v66/=2;}
                int v61=static_cast<int>(v52*4096);int v62=static_cast<int>(v66*4096);
                float rand1=(Rng::GetRandomInt2(v61*2)-v61)/4096.0F;float rand2=(Rng::GetRandomInt2(v62*2)-v62)/4096.0F;
                if(_player->_disruptedTimer==0)
                {
                    if(rand1>6)rand1=Rng::GetRandomInt2(8192)/4096.0F+4;else if(rand1<-6)rand1=-4-Rng::GetRandomInt2(9182)/4096.0F;
                    if(rand2>6)rand2=Rng::GetRandomInt2(8192)/4096.0F+4;else if(rand2<-6)rand2=-4-Rng::GetRandomInt2(9182)/4096.0F;
                }
                _field1048 = _field1048 + ScaleVector(camVec, rand1) + ScaleVector(_player->CameraInfo()->UpVector, rand2);
            }
            Func2145738(_field1048);
            if((Flags4&AiFlags4::Bit2)!=AiFlags4::None){float aimValue=_aimValues[_player->BotLevel()]/2;_buttonAimX=std::clamp(_buttonAimX,-aimValue,aimValue);_buttonAimY=std::clamp(_buttonAimY,-aimValue,aimValue);}
        }

        void Func2145738(Vector3 position)
        {
            Vector3 toTarget=_player->_aimPosition-_player->_muzzlePos;float toTargetX=toTarget.X,toTargetY=toTarget.Y,toTargetZ=toTarget.Z;
            toTarget=toTarget.Normalized();float toTargetYNrm=toTarget.Y;toTarget=WithY(toTarget, 0);toTarget=!VectorEqual(toTarget, Vector3::Zero)?toTarget.Normalized():UnitX();
            Vector3 toPos=position-_player->_muzzlePos;float distToPosH=Length(WithY(toPos, 0));float posY=toPos.Y;toPos=toPos.Normalized();float posYNrm=toPos.Y;toPos=WithY(toPos, 0);toPos=!VectorEqual(toPos, Vector3::Zero)?toPos.Normalized():toTarget;
            float dot=Vector3::Dot(toTarget,toPos);float value=_aimValues[_player->BotLevel()];
            if(dot<1){if(dot>_dotValues[_player->BotLevel()])_buttonAimX=RadiansToDegrees(std::acos(dot));else _buttonAimX=value;if(Vector3::Cross(toTarget,toPos).Y<0)_buttonAimX*=-1;}
            const auto& equip = *_player->EquipInfo(); const WeaponInfo& weapon = *equip.Weapon;float chargePct=0;
            if(HasFlag(weapon.Flags, WeaponFlags::CanCharge)&&equip.ChargeLevel>=weapon.MinCharge*2)chargePct=(equip.ChargeLevel-weapon.MinCharge*2)/static_cast<float>(weapon.FullCharge*2-weapon.MinCharge*2);
            float speed=(weapon.UnchargedSpeed+((weapon.MinChargeSpeed-weapon.UnchargedSpeed)*chargePct))/4096.0F/2;
            float gravity=(weapon.UnchargedGravity+((weapon.MinChargeGravity-weapon.UnchargedGravity)*chargePct))/4096.0F/2;
            float div=distToPosH*distToPosH*gravity/(speed*speed);float angle1;float angle2;
            if(div!=0)
            {
                float div2=0;if(toTargetX!=0||toTargetZ!=0){float toTargetDistH=std::sqrt(toTargetX*toTargetX+toTargetZ*toTargetZ);div2=toTargetY/toTargetDistH;}
                float v28=distToPosH*distToPosH-4*(div/2*(div/2-posY));if(v28<0)return;float sqrt=std::sqrt(v28);float div3=(sqrt-distToPosH)/div;
                angle1=RadiansToDegrees(std::atan(div2));angle2=RadiansToDegrees(std::atan(div3));
            }
            else {angle1=90-RadiansToDegrees(std::acos(toTargetYNrm));angle2=90-RadiansToDegrees(std::acos(posYNrm));}
            float angleDiff=angle2-angle1;_buttonAimY=std::clamp(angleDiff,-value,value);Flags2&=~AiFlags2::Bit8;if(dot>=255/256.0F&&angleDiff>-5&&angleDiff<5)Flags2|=AiFlags2::Bit8;
        }

        bool Func213842C()
        {
            if((Flags2&AiFlags2::Bit16)==AiFlags2::None)
            {Flags2|=AiFlags2::Bit16;Flags2&=~AiFlags2::Bit17;if((Flags2&AiFlags2::TargetPlayer)!=AiFlags2::None&&AggroFunc214857C(6,1,2,{},_targetPlayer))Flags2|=AiFlags2::Bit17;}
            return (Flags2&AiFlags2::Bit17)!=AiFlags2::None;
        }

        void Func2143A40()
        {
            const auto& equip = *_player->EquipInfo(); const WeaponInfo& weapon = _player->EquipWeapon();int shotDelay;
            if(_player->BotLevel()==0)shotDelay=60;else if(_player->BotLevel()==1)shotDelay=15;else if(_player->BotLevel()==2)shotDelay=5;else shotDelay=0;
            if((Flags2&AiFlags2::Bit21)!=AiFlags2::None)shotDelay/=2;shotDelay*=2;
            BeamType beam=GetBeamType(_weapon1);if(beam!=BeamType::ShockCoil&&!_player->AvailableWeapons()[beam])return;
            auto setRandomDelay=[&](){_shotDelay=weapon.ShotCooldown*2+static_cast<int>(Rng::GetRandomInt2(shotDelay));};
            if(beam==BeamType::PowerBeam)
            {
                if(_player->CurrentWeapon()!=beam)_touchButtons.PowerBeam.IsDown=true;
                else if((Flags4&AiFlags4::Bit1)!=AiFlags4::None&&CanChargeWeapon())
                {if((!HasFlag(_player->Flags2(), PlayerFlags2::Shooting)&&_buttons.R.FramesUp==0)||equip.ChargeLevel>=weapon.FullCharge*2)setRandomDelay();else _buttons.R.IsDown=true;}
                else if(_buttons.R.FramesDown>0&&_buttons.R.FramesDown<_shotDelay)_buttons.R.IsDown=true;
                else if(_buttons.R.FramesUp<=_shotDelay)setRandomDelay();
                else{_buttons.R.IsDown=true;_shotDelay=static_cast<int>(Rng::GetRandomInt2(weapon.FullCharge*2));}
            }
            else if(beam==BeamType::Missile)
            {
                if(_player->CurrentWeapon()!=beam)_touchButtons.Missile.IsDown=true;
                else if((Flags4&AiFlags4::Bit1)!=AiFlags4::None&&CanChargeWeapon())
                {if((!HasFlag(_player->Flags2(), PlayerFlags2::Shooting)&&_buttons.R.FramesUp<=_shotDelay)||equip.ChargeLevel>=weapon.FullCharge*2)setRandomDelay();else _buttons.R.IsDown=true;}
                else if(HasFlag(_player->Flags1(), PlayerFlags1::GunOpenAnimation)&&_buttons.R.FramesUp>_shotDelay){_buttons.R.IsDown=true;setRandomDelay();}
            }
            else if(beam==BeamType::VoltDriver)
            {
                if(_player->CurrentWeapon()!=beam)_touchButtons.VoltDriver.IsDown=true;
                else if((Flags4&AiFlags4::Bit1)!=AiFlags4::None&&CanChargeWeapon())
                {if((!HasFlag(_player->Flags2(), PlayerFlags2::Shooting)&&_buttons.R.FramesUp<=_shotDelay)||equip.ChargeLevel>=weapon.FullCharge*2)setRandomDelay();else _buttons.R.IsDown=true;}
                else if(_buttons.R.FramesUp>_shotDelay){_buttons.R.IsDown=true;setRandomDelay();}
            }
            else if(beam==BeamType::Battlehammer)
            {
                if(_player->CurrentWeapon()!=beam)_touchButtons.Battlehammer.IsDown=true;
                else if(HasFlag(_player->Flags1(), PlayerFlags1::ShotUncharged)||_buttons.R.FramesUp>0)_buttons.R.IsDown=true;
            }
            else if(beam==BeamType::Imperialist)
            {if(_player->CurrentWeapon()!=beam)_touchButtons.Imperialist.IsDown=true;else if(_buttons.R.FramesUp>_shotDelay)_buttons.R.IsDown=true;}
            else if(beam==BeamType::Judicator)
            {
                if(_player->CurrentWeapon()!=beam)_touchButtons.Judicator.IsDown=true;
                else if((Flags4&AiFlags4::Bit1)!=AiFlags4::None&&CanChargeWeapon())
                {
                    if((!HasFlag(_player->Flags2(), PlayerFlags2::Shooting)&&_buttons.R.FramesUp<=_shotDelay)||equip.ChargeLevel>=weapon.FullCharge*2)
                    {
                        if((Flags2&AiFlags2::TargetPlayer)!=AiFlags2::None)
                        {
                            assert(_targetPlayer);Vector3 toTarget=static_cast<Vector3>(_targetPlayer->Position) - static_cast<Vector3>(_player->Position);float distSqr=LengthSquared(toTarget);
                            if((distSqr>9&&_player->BotLevel()==0)||(distSqr>11&&_player->BotLevel()==1)
                                ||(distSqr>13&&_player->BotLevel()==2)
                                ||(distSqr>400&&_player->BotLevel()>=3))setRandomDelay();else _buttons.R.IsDown=true;
                        }
                        else setRandomDelay();
                    }
                    else _buttons.R.IsDown=true;
                }
                else if(_buttons.R.FramesUp>_shotDelay){_buttons.R.IsDown=true;setRandomDelay();}
            }
            else if(beam==BeamType::Magmaul)
            {
                if(_player->CurrentWeapon()!=beam)_touchButtons.Magmaul.IsDown=true;
                else if((Flags4&AiFlags4::Bit1)!=AiFlags4::None&&CanChargeWeapon())
                {if((!HasFlag(_player->Flags2(), PlayerFlags2::Shooting)&&_buttons.R.FramesUp<=_shotDelay)||equip.ChargeLevel>=weapon.FullCharge*2)setRandomDelay();else _buttons.R.IsDown=true;}
                else if(_buttons.R.FramesUp>_shotDelay){setRandomDelay();_buttons.R.IsDown=true;}
            }
            else if(beam==BeamType::ShockCoil)
            {
                assert(_targetPlayer);Vector3 toTarget=static_cast<Vector3>(_targetPlayer->Position) - static_cast<Vector3>(_player->Position);float distSqr=LengthSquared(toTarget);
                if(GameState::SinglePlayer()||distSqr<=225||!_player->AvailableWeapons()[BeamType::PowerBeam])
                {
                    if(!_player->AvailableWeapons()[beam])return;
                    if(_player->CurrentWeapon()!=beam)_touchButtons.ShockCoil.IsDown=true;
                    else if((HasFlag(_player->Flags1(), PlayerFlags1::ShotUncharged)||_buttons.R.FramesUp>0)
                        &&(distSqr<225||(distSqr<256&&HasFlag(_player->Flags1(), PlayerFlags1::ShotUncharged))))_buttons.R.IsDown=true;
                }
                else if(_player->CurrentWeapon()!=BeamType::PowerBeam)_touchButtons.PowerBeam.IsDown=true;
                else if(_buttons.R.FramesDown>0&&_buttons.R.FramesDown<_shotDelay)_buttons.R.IsDown=true;
                else if(_buttons.R.FramesUp<=_shotDelay)setRandomDelay();
                else{_buttons.R.IsDown=true;_shotDelay=static_cast<int>(Rng::GetRandomInt2(weapon.FullCharge*2));}
            }
            else if(beam==BeamType::OmegaCannon)
            {
                if(_player->CurrentWeapon()!=beam)_touchButtons.OmegaCannon.IsDown=true;
                else if((Flags2&AiFlags2::TargetPlayer)!=AiFlags2::None&&_buttons.R.FramesUp>0)
                {assert(_targetPlayer);Vector3 toTarget=static_cast<Vector3>(_targetPlayer->Position) - static_cast<Vector3>(_player->Position);if(LengthSquared(toTarget)>100)_buttons.R.IsDown=true;}
            }
        }

        void Func214380C()
        {
            const auto& equip = *_player->EquipInfo(); const WeaponInfo& weapon = _player->EquipWeapon();if(!HasFlag(weapon.Flags, WeaponFlags::CanCharge))return;BeamType beam=GetBeamType(_weapon1);
            if(_player->CurrentWeapon()!=beam&&_player->_availableWeapons[beam])
            {
                if(beam==BeamType::PowerBeam)_touchButtons.PowerBeam.IsDown=true;else if(beam==BeamType::Missile)_touchButtons.Missile.IsDown=true;
                else if(beam==BeamType::VoltDriver)_touchButtons.VoltDriver.IsDown=true;else if(beam==BeamType::Judicator)_touchButtons.Judicator.IsDown=true;
                else if(beam==BeamType::Magmaul)_touchButtons.Magmaul.IsDown=true;return;
            }
            if(CanChargeWeapon()&&(_buttons.R.FramesDown==0||equip.ChargeLevel>0))_buttons.R.IsDown=true;
        }
        void Func2143658(){if((Flags2&AiFlags2::TargetPlayer)!=AiFlags2::None){Func2144AE4();if((Flags2&AiFlags2::Bit8)!=AiFlags2::None)Func2143A40();}else if((Flags4&AiFlags4::Bit1)!=AiFlags4::None)Func214380C();}
        void Func2144AE4(){if((Flags2&AiFlags2::TargetPlayer)!=AiFlags2::None){assert(_targetPlayer);Vector3 p;_targetPlayer->GetPosition(p);_field1048=AddY(p, _targetPlayer->IsAltForm()?Fixed::ToFloat(_targetPlayer->Values().AltColYPos):0.5F);Func2145738(_field1048);}}
        void Func21433E4(){if((Flags2&AiFlags2::TargetPlayer)!=AiFlags2::None){Func2144B88();auto v=ExecuteVectorFunc(0,false,false);if(Vector3::Dot(_player->_facingVector,v)>0.866F)Func2143A40();}}
        void Func2143470(){if((Flags2&AiFlags2::TargetHalfturret)!=AiFlags2::None){assert(_targetHalfturret);Func2144964();auto t=static_cast<Vector3>(_targetHalfturret->Position) - static_cast<Vector3>(_player->Position);if((Flags2&AiFlags2::Bit8)!=AiFlags2::None||LengthSquared(t)<10)Func2143A40();else if((Flags4&AiFlags4::Bit1)!=AiFlags4::None||_weapon1==0||_weapon1==1)Func214380C();}else if((Flags4&AiFlags4::Bit1)!=AiFlags4::None)Func214380C();}
        void Func2144964(){if((Flags2&AiFlags2::TargetHalfturret)!=AiFlags2::None){assert(_targetHalfturret);Vector3 p;_targetHalfturret->GetPosition(p);_field1048=AddY(p, 0.5F);Func2145738(_field1048);}}
        void Func21433A0(Vector3 position){Func2145738(position);if((Flags2&AiFlags2::Bit8)!=AiFlags2::None)Func2143A40();}
        void Func2145BA0(){float facingY=_player->_facingVector.Y;if(facingY!=0){float aimY=RadiansToDegrees(std::acos(facingY))-90;float value=_aimValues[_player->BotLevel()];_buttonAimY=std::clamp(aimY,-value,value);}}
        void PressButton(AiButton& button,int frames=0){if(button.FramesUp>frames*2)button.IsDown=true;}
        void PressL(int frames=0){if(_touchButtons.Magmaul.FramesUp>frames*2)_buttons.L.IsDown=true;}

        void Func2140094(AiContext& context)
        {
            auto setField118=[&]()
            {
                if(_scene.RoomId()==106&&_player->Position.X>-2&&_player->Position.X<2&&_player->Position.Z>-2&&_player->Position.Z<2
                    &&_player->Speed().Y>0&&_player->Position.Y-_node40->Position.Y>1)Field118=302;else Field118++;
            };
            while(Func2140584(context)){}
            assert(_node40);context.Field18=_node40->NodeType==NodeType::AltForm&&_player->Hunter()!=Hunter::Guardian?2:0;
            if(_player->IsAltForm())
            {
                if(_player->_deathaltTimer==0&&(context.Field1C==1||(!context.Field28&&context.Field18!=2)))CheckUnmorph();
                if(context.Field20!=0&&(context.Field1C!=1||_player->_deathaltTimer!=0))
                {
                    if(_player->Values().AltFormStrafe!=0)
                    {
                        if(context.Field30&&context.Field2C==0)Func2142A80();
                        else if(!context.Field30&&context.Field2C==0)Func2142AE8(_node40->Position);
                        else if(context.Field30&&context.Field2C==1)Func2141EA8();
                        else if(!context.Field30&&context.Field2C==1)Func214201C();
                    }
                    else if(context.Field2C==1)Func2140D5C();
                    else if(context.Field2C!=2||Field118!=0)Func21418D8(_node40->Position);
                    else Func214182C();
                    setField118();
                }
            }
            else
            {
                if(context.Field1C!=1&&(context.Field28||context.Field18==2))PressButton(_touchButtons.Morph,10);
                if(context.Field20==2)PressL();
                if(context.Field20!=0)
                {
                    if((Flags2&AiFlags2::Bit21)!=AiFlags2::None||HasFlag(_player->Flags1(), PlayerFlags1::Grounded)
                        ||LengthSquared(WithY((static_cast<Vector3>(_player->Position) - static_cast<Vector3>(_node40->Position)), 0))>_node40->MaxDistance*_node40->MaxDistance)
                    {
                        if(context.Field30&&(context.Field2C==0||context.Field2C==2||context.Field20==2))Func2142A80();
                        else if(!context.Field30&&(context.Field2C==0||context.Field2C==2||context.Field20==2))Func2142AE8(_node40->Position);
                        else if(context.Field30&&context.Field2C==1)Func2141EA8();
                        else if(!context.Field30&&context.Field2C==1)Func214201C();
                    }
                    setField118();Func2140B18(context,_node40->Position);
                }
            }
        }

        bool Func2140584(AiContext& context)
        {
            assert(_node40);
            if(context.Field24==0)
            {
                if(IsNodeInRange(*_node40))
                {
                    Field118=0;_field78=0;Flags2&=~AiFlags2::Bit15;context.Field40=0;context.Field44=0;context.Field34=_player->Position;FindQueuedEntityRef();
                    if(_node40==_node3C){context.Field20=0;context.Field24=1;Flags2&=~AiFlags2::Bit7;return true;}
                    std::shared_ptr<Formats::NodeData3> node2{};auto node1=Func213A1A8();auto bomb=Func2139E34(_node40,node1);
                    if(std::get<0>(bomb)){node2=Func2139F84(_node40,node1,std::get<1>(bomb),std::get<2>(bomb));if(node2!=node1)node1=node2;}
                    context.Field14=0;context.Field1C=0;
                    if(node1==node2){context.Field14=1;context.Field1C=1;context.Field20=2;context.Field24=2;}
                    else for(int i=0;i<_node40->Count2;i++)if((*_node40->Values)[_node40->Index2+i]==node1->Id){context.Field14=1;context.Field1C=1;context.Field20=2;context.Field24=2;break;}
                    _node48=_node44=_node40;_field4C[0]=_node40=node1;context.Field18=_node40->NodeType==NodeType::AltForm&&_player->Hunter()!=Hunter::Guardian?2:0;Flags2|=AiFlags2::Bit7;
                    return context.Field24!=0;
                }
                if(_player->_horizColTimer<=20||(HasFlag(_player->Flags1(), PlayerFlags1::Grounded)&&_player->_horizColTimer<=120)||_player->IsAltForm()||_player->IsMorphing())
                {
                    if(_player->_horizColTimer>60&&HasFlag(_player->Flags1(), PlayerFlags1::Grounded)&&_player->IsAltForm()&&!_player->IsUnmorphing()&&Field118>60)
                    {
                        _field7A[_field78]=_node40->Id;if(_field78<9)_field78++;FindEntityRef(AiEntRefType::Type1);_node40=_entityRefs.Field1;assert(_node40);
                        context.Field18=_node40->NodeType==NodeType::AltForm&&_player->Hunter()!=Hunter::Guardian?2:0;
                    }
                    else if((Flags2&AiFlags2::TargetPlayer)!=AiFlags2::None&&_targetPlayer&&_targetPlayer->Hunter()==Hunter::Sylux&&_targetPlayer->IsAltForm())
                    {if(Func2139C60(*_targetPlayer)){context.Field14=1;context.Field1C=1;context.Field20=2;context.Field24=2;Flags2|=AiFlags2::Bit15;}}
                    else if(!_node44||!IsNodeInRange(*_node44))
                    {auto toNode=static_cast<Vector3>(_node40->Position) - static_cast<Vector3>(_player->Position);if(LengthSquared(toNode)<0.25F&&HasFlag(_player->Flags1(), PlayerFlags1::Grounded)&&Field118>60)Field118=302;}
                }
                else if((Flags2&AiFlags2::Bit15)!=AiFlags2::None)
                {
                    _field7A[_field78]=_node40->Id;if(_field78<9)_field78++;FindEntityRef(AiEntRefType::Type1);_node40=_entityRefs.Field1;assert(_node40);
                    context.Field18=_node40->NodeType==NodeType::AltForm&&_player->Hunter()!=Hunter::Guardian?2:0;
                }
                else {context.Field14=1;context.Field1C=1;context.Field20=2;context.Field24=2;Flags2|=AiFlags2::Bit15;}
                return false;
            }
            if(context.Field24==1)
            {
                Field118=0;_field78=0;Flags2&=~AiFlags2::Bit15;context.Field40=0;context.Field44=0;context.Field34=_player->Position;FindQueuedEntityRef();assert(_node3C);
                if(IsNodeInRange(*_node3C)&&_node40==_node3C)return false;
                context.Field14=0;context.Field18=0;context.Field1C=0;context.Field20=1;context.Field24=0;
                if(IsNodeInRange(*_node40)||(Flags2&AiFlags2::Bit7)!=AiFlags2::None)return true;
                FindEntityRef(AiEntRefType::Type1);_node40=_entityRefs.Field1;return false;
            }
            if(context.Field24==2&&HasFlag(_player->Flags1(), PlayerFlags1::UsedJump)){context.Field20=1;context.Field24=0;return true;}
            return false;
        }

        std::tuple<bool,Vector3,Vector3> Func2139E34(const std::shared_ptr<Formats::NodeData3>& node1,const std::shared_ptr<Formats::NodeData3>& node2)
        {
            float nodeMidpointY=(node1->Position.Y+node2->Position.Y)/2;
            for (auto _enumerator17 = _scene.GetPlayerEntities().GetEnumerator(); _enumerator17.MoveNext(); )
            if (const auto other = _enumerator17.Current(); true)
            {
                if(other->Hunter()!=Hunter::Sylux||other->SyluxBombCount()!=2)continue;auto bomb0=other->SyluxBombs()[0],bomb1=other->SyluxBombs()[1];assert(bomb0);assert(bomb1);
                float bombMidpointY=(bomb0->Position.Y+bomb1->Position.Y)/2;float yDiff=bombMidpointY-nodeMidpointY;
                if(yDiff>=-3&&yDiff<=3&&Func204CF74(node1->Position,node2->Position,bomb0->Position,bomb1->Position))return {true,bomb0->Position,bomb1->Position};
            }return {false,Vector3::Zero,Vector3::Zero};
        }
        bool Func204CF74(Vector3 a1,Vector3 a2,Vector3 a3,Vector3 a4)
        {float v18=(a1.X-a3.X)*(a4.X-a3.X)+(a1.Z-a3.Z)*(a4.Z-a3.Z);float v19=(a1.X-a4.X)*(a4.X-a3.X)+(a1.Z-a4.Z)*(a4.Z-a3.Z);float v22=(a1.X-a3.X)*(a2.X-a1.X)+(a1.Z-a3.Z)*(a2.Z-a1.Z);float v23=(a2.X-a3.X)*(a2.X-a1.X)+(a2.Z-a3.Z)*(a2.Z-a1.Z);return((v18<=0&&v19>=0)||(v18>=0&&v19<=0))&&((v22<=0&&v23>=0)||(v22>=0&&v23<=0));}
        std::shared_ptr<Formats::NodeData3> Func2139F84(const std::shared_ptr<Formats::NodeData3>& node1,const std::shared_ptr<Formats::NodeData3>& node2,Vector3 bomb0Position,Vector3 bomb1Position)
        {
            std::array<std::shared_ptr<Formats::NodeData3>,20> nodeList1{},nodeList2{},nodeList3{};int n1=Func213A0A4(node1,nodeList1);int n2=Func213A0A4(node2,nodeList2);int n3=0;
            for(int i=0;i<n1;i++)for(int j=0;j<n2;j++)if(nodeList1[i]==nodeList2[j])nodeList3[n3++]=nodeList1[i];
            for(int i=0;i<n3;i++){auto n=nodeList3[i];if(!Func204CF74(node1->Position,n->Position,bomb0Position,bomb1Position)&&!Func204CF74(n->Position,node2->Position,bomb0Position,bomb1Position))return n;}
            return node2;
        }

        bool Func2139C60(PlayerEntity& target)
        {
            assert(target.Hunter()==Hunter::Sylux);if(target.SyluxBombCount()!=2&&target.SyluxBombCount()!=3)return false;Vector3 bomb2Position;
            if(target.SyluxBombCount()==2)bomb2Position=target.Position;else{auto bomb2=target.SyluxBombs()[2];assert(bomb2);bomb2Position=bomb2->Position;}
            auto bomb0=target.SyluxBombs()[0],bomb1=target.SyluxBombs()[1];assert(bomb0);assert(bomb1);Vector3 bomb01=static_cast<Vector3>(bomb1->Position) - static_cast<Vector3>(bomb0->Position);Vector3 bomb12=bomb2Position-bomb1->Position;Vector3 bomb20=bomb0->Position-bomb2Position;
            Vector3 cross=Vector3::Cross(bomb12,bomb01).Normalized();Vector3 bomb0Player=static_cast<Vector3>(_player->Position) - static_cast<Vector3>(bomb0->Position);Vector3 bomb1Player=static_cast<Vector3>(_player->Position) - static_cast<Vector3>(bomb1->Position);Vector3 bomb2Player=_player->Position-bomb2Position;float dot=Vector3::Dot(cross,bomb0Player);
            return dot>-0.75F&&dot<0.75F&&Vector3::Dot(Vector3::Cross(bomb0Player,bomb01),cross)>0&&Vector3::Dot(Vector3::Cross(bomb1Player,bomb12),cross)>0&&Vector3::Dot(Vector3::Cross(bomb2Player,bomb20),cross)>0;
        }
        void Func2142A80(){assert(_node40);Func2142AE8(_node40->Position);Func2145C14(_node40->Position);}
        void Func2141EA8()
        {
            assert(_node40);Vector3 altVec=_player->IsAltForm()?Vector3(_player->_field80,0,_player->_field84):Vector3(_player->_field70,0,_player->_field74);
            Vector3 toNode=_node40->Position-(_node44?_node44->Position:_field90);toNode=(toNode.X!=0||toNode.Z!=0)?WithY(toNode, 0).Normalized():altVec;float dot=Vector3::Dot(altVec,toNode);
            if(dot<1){if(dot>_dotValues[_player->BotLevel()])_buttonAimX=RadiansToDegrees(std::acos(dot));else _buttonAimX=_aimValues[_player->BotLevel()];if(Vector3::Cross(altVec,toNode).Y<0)_buttonAimX*=-1;}Func214201C();
        }
        void Func2142AE8(Vector3 position){Vector3 altVec=_player->IsAltForm()?Vector3(_player->_field80,0,_player->_field84):Vector3(_player->_field70,0,_player->_field74);Vector3 toPos=position-_player->Position;toPos=(toPos.X!=0||toPos.Z!=0)?WithY(toPos, 0).Normalized():altVec;Helper01(altVec,toPos,_buttons.X,_buttons.B,_buttons.Y,_buttons.A);}
        void Func2142ABC(Vector3 position){Func2142AE8(position);Func2145C14(position);}
        void Func21418D8(Vector3 position){if(CheckSpireClimbInput())return;Vector3 toPos=position-_player->Position;toPos=(toPos.X!=0||toPos.Z!=0)?WithY(toPos, 0).Normalized():Vector3(_player->_altRollFbX,0,_player->_altRollFbZ);Func2141CD4(toPos);}
        void Func2141CD4(Vector3 toPos){Vector3 altVec(_player->_altRollFbX,0,_player->_altRollFbZ);Helper01(altVec,toPos,_buttons.Up,_buttons.Down,_buttons.Left,_buttons.Right);}
        void Func214201C(){Vector3 altVec=_player->IsAltForm()?Vector3(_player->_field80,0,_player->_field84):Vector3(_player->_field70,0,_player->_field74);Helper02(altVec,_buttons.X,_buttons.B,_buttons.Y,_buttons.A);}
        void Func2140D5C(){if(CheckSpireClimbInput())return;Vector3 altVec(_player->_altRollFbX,0,_player->_altRollFbZ);Helper02(altVec,_buttons.Up,_buttons.Down,_buttons.Left,_buttons.Right);}
        bool CheckSpireClimbInput()
        {if(_player->Hunter()==Hunter::Spire){if(_field116>0)_field116--;else if(HasFlag(_player->Flags2(), PlayerFlags2::SpireClimbing)){if(_buttons.Up.FramesUp<20||_buttons.Down.FramesUp<20||_buttons.Left.FramesUp<20||_buttons.Right.FramesUp<20)return true;_field116=20;}}return false;}
        void Helper01(Vector3 altVec,Vector3 toPos,AiButton& upButton,AiButton& downButton,AiButton& leftButton,AiButton& rightButton)
        {
            float dot=Vector3::Dot(altVec,toPos);auto cross=Vector3::Cross(altVec,toPos);
            if(dot>0.866F)upButton.IsDown=true;
            else if(dot>0.5F){upButton.IsDown=true;if(cross.Y>=0)leftButton.IsDown=true;else rightButton.IsDown=true;}
            else if(dot>-0.5F){if(cross.Y>=0)leftButton.IsDown=true;else rightButton.IsDown=true;}
            else if(dot>=-0.866F)downButton.IsDown=true;
            else{downButton.IsDown=true;if(cross.Y>=0)leftButton.IsDown=true;else rightButton.IsDown=true;}
        }
        void Helper02(Vector3 altVec,AiButton& upButton,AiButton& downButton,AiButton& leftButton,AiButton& rightButton)
        {
            assert(_node40);Vector3 toNode=_node40->Position-(_node44?_node44->Position:_field90);toNode=(toNode.X!=0||toNode.Z!=0)?WithY(toNode, 0).Normalized():altVec;
            Vector3 playerToNode=static_cast<Vector3>(_node40->Position) - static_cast<Vector3>(_player->Position);playerToNode=(playerToNode.X!=0||playerToNode.Z!=0)?WithY(toNode, 0).Normalized():UnitX();
            if(Vector3::Dot(playerToNode,toNode)<0){_node44.reset();_field90=_player->Position;toNode=_node40->Position-_field90;toNode=(toNode.X!=0||toNode.Z!=0)?WithY(toNode, 0).Normalized():altVec;}
            float dot=Vector3::Dot(altVec,toNode);auto cross1=Vector3::Cross(altVec,toNode);auto cross2=Vector3::Cross(toNode,UnitY());cross2 = (cross2.X != 0 || cross2.Z != 0) ? WithY(cross2, 0).Normalized() : WithY(toNode, 0);
            Vector3 pos1Add=_node40->Position+ScaleVector(cross2, _node40->MaxDistance);Vector3 pos2Add=_node44?_node44->Position+ScaleVector(cross2, _node44->MaxDistance):_field90 + ScaleVector(cross2, _field9C);Vector3 pos3Add=_player->Position+DivideVector(cross2, 2.0F);Vector3 cross3=UnitY();
            Vector3 pos21=WithY((pos1Add-pos2Add), 0),pos23=WithY((pos3Add-pos2Add), 0);if((pos21.X!=0||pos21.Z!=0)&&(pos23.X!=0||pos23.Z!=0))cross3=Vector3::Cross(pos21,pos23);
            pos1Add=_node40->Position+ScaleVector(cross2, -_node40->MaxDistance);pos2Add=_node44?_node44->Position+ScaleVector(cross2, -_node44->MaxDistance):_field90 + ScaleVector(cross2, -_field9C);pos3Add=_player->Position+DivideVector(cross2, -2.0F);Vector3 cross4=UnitY();
            pos21=WithY((pos1Add-pos2Add), 0);pos23=WithY((pos3Add-pos2Add), 0);if((pos21.X!=0||pos21.Z!=0)&&(pos23.X!=0||pos23.Z!=0))cross4=Vector3::Cross(pos21,pos23);
            if(cross4.Y>0)Flags2|=AiFlags2::Bit11;else if(cross3.Y<0)Flags2&=~AiFlags2::Bit11;bool bit11=(Flags2&AiFlags2::Bit11)!=AiFlags2::None;
            if(dot>0.866F){upButton.IsDown=true;if(bit11)rightButton.IsDown=true;else leftButton.IsDown=true;}
            else if(dot>0.5F){if(cross1.Y>=0){if(bit11)upButton.IsDown=true;else leftButton.IsDown=true;}else if(bit11)rightButton.IsDown=true;else upButton.IsDown=true;}
            else if(dot>-0.5F){if(cross1.Y>=0){leftButton.IsDown=true;if(bit11)upButton.IsDown=true;else downButton.IsDown=true;}else{rightButton.IsDown=true;if(bit11)downButton.IsDown=true;else upButton.IsDown=true;}}
            else if(dot>=-0.866F){downButton.IsDown=true;if(bit11)leftButton.IsDown=true;else rightButton.IsDown=true;}
            else if(cross1.Y>=0){if(bit11)leftButton.IsDown=true;else downButton.IsDown=true;}else if(bit11)downButton.IsDown=true;else rightButton.IsDown=true;
        }
        void Func214182C(){assert(_node40);Func2141840(_node40->Position);}
        void Func2141840(Vector3 position){assert(_node40);Vector3 toPos=position-_player->Position;toPos=(toPos.X!=0||toPos.Z!=0)?WithY(toPos, 0).Normalized():Vector3(_player->_altRollFbX,0,_player->_altRollFbZ);Func2141A0C(toPos);}
        void Func2141A0C(Vector3 toPos)
        {assert(_node40);Vector3 altVec(_player->_altRollFbX,0,_player->_altRollFbZ);float dot=Vector3::Dot(altVec,toPos);auto cross=Vector3::Cross(altVec,toPos);if(HasFlag(_player->_abilities, AbilityFlags::Boost)&&!HasFlag(_player->Flags1(), PlayerFlags1::Boosting)){if(_framesWithTouch==0&&_framesWithoutTouch>_field1032){_field1032=static_cast<int>(_field1030+Rng::GetRandomInt2(_field1030/2));_hasTouch=true;_touchAimX=static_cast<std::uint16_t>(static_cast<int>(50*cross.Y)+128);_touchAimY=static_cast<std::uint16_t>(static_cast<int>(50*dot)+100);}else if(_framesWithTouch==1){_hasTouch=true;_touchAimX=static_cast<std::uint16_t>(256-_touchAimX);_touchAimY=static_cast<std::uint16_t>(200-_touchAimY);}}Helper01(altVec,toPos,_buttons.Up,_buttons.Down,_buttons.Left,_buttons.Right);}
        void Func2140B18(AiContext& context,Vector3 position)
        {
            if(context.Field44>0){context.Field44--;PressL();if(!_player->IsAltForm()||_player->Values().AltFormStrafe!=0){_buttons.X.IsDown=false;_buttons.B.IsDown=false;_buttons.Y.IsDown=false;_buttons.A.IsDown=false;}else{_buttons.Up.IsDown=false;_buttons.Down.IsDown=false;_buttons.Left.IsDown=false;_buttons.Right.IsDown=false;}}
            else{if(_player->Speed().Y>=0.0625F/2||_player->Speed().Y<-0.0625F/2||_player->IsMorphing()||_player->IsUnmorphing()){context.Field40=0;context.Field34=_player->Position;}else if(HasFlag(_player->Flags1(), PlayerFlags1::CollidingLateral)&&!HasFlag(_player->Flags1(), PlayerFlags1::Grounded))context.Field40++;if(context.Field40>=24){Vector3 toPlayer=WithY((_player->Position-context.Field34), 0);Vector3 toPos=WithY((position-_player->Position), 0).Normalized();if(Vector3::Dot(toPlayer,toPos)<1/256.0F)context.Field44=30;context.Field40=0;context.Field34=_player->Position;}}
        }
        void Func214003C(AiContext& context){if((Flags2&AiFlags2::TargetPlayer)!=AiFlags2::None)Func21436D8();else if((Flags4&AiFlags4::Bit1)!=AiFlags4::None)Func214380C();Func2140094(context);}

        void Func214715C(AiContext& c)
        {
            c.Field4=0;c.Field5=0;c.Field6=0;c.Field7=0;c.Field8=0;c.Field9=0;c.FieldA=0;c.FieldB=0;c.FieldC=0;c.FieldD=28;c.FieldE=0;c.FieldF=0;
            switch(c.Func24Id)
            {
            case 2:c.FieldA=31;c.FieldB=4;break;
            case 3:c.FieldA=31;c.FieldB=4;c.FieldD=29;break;
            case 4:c.FieldA=32;c.FieldB=4;break;
            case 5:c.FieldA=32;c.FieldB=4;c.FieldE=38;break;
            case 6:c.FieldA=32;c.FieldB=3;break;
            case 7:c.FieldA=32;c.FieldB=25;break;
            case 8:c.FieldA=32;c.FieldB=26;break;
            case 9:c.Field4=37;break;
            case 10:c.Field4=37;c.FieldE=38;break;
            case 11:c.Field4=33;c.Field9=39;break;
            case 12:c.Field4=33;c.Field9=39;c.FieldD=29;break;
            case 13:c.Field4=33;c.Field9=39;c.Field6=51;break;
            case 14:c.Field4=33;c.Field9=4;break;
            case 15:c.Field4=33;c.Field9=4;c.FieldD=29;break;
            case 16:c.Field4=37;c.Field9=4;break;
            case 17:c.Field4=33;c.Field9=4;c.FieldE=38;c.FieldA=32;c.FieldB=4;break;
            case 18:c.Field4=37;c.Field9=4;c.FieldE=38;break;
            case 19:c.FieldA=32;c.FieldB=4;c.Field4=37;c.Field9=4;break;
            case 20:c.Field4=37;c.Field9=4;c.Field5=47;break;
            case 21:c.Field4=37;c.Field9=4;c.FieldD=29;break;
            case 22:c.Field4=37;c.Field9=4;c.FieldD=29;if(_player->BotLevel()>0)c.Field5=48;break;
            case 23:c.Field4=37;c.Field9=24;c.FieldD=29;break;
            case 24:c.Field4=37;c.Field9=24;c.FieldD=29;c.FieldC=63;c.FieldF=69;break;
            case 25:c.Field4=37;c.Field9=12;c.FieldD=29;break;
            case 26:c.Field4=37;c.Field9=12;c.Field5=47;c.FieldD=29;break;
            case 27:c.Field4=37;c.Field9=13;c.FieldD=29;break;
            case 28:c.Field4=37;c.Field9=12;c.FieldD=29;c.FieldC=62;c.FieldF=66;break;
            case 29:c.Field4=37;c.Field9=4;c.Field5=47;c.FieldD=29;break;
            case 30:c.Field4=37;c.Field9=4;c.FieldD=29;c.FieldC=62;c.FieldF=64;break;
            case 31:c.Field4=33;c.Field9=4;c.FieldD=29;c.FieldC=62;c.FieldF=64;break;
            case 32:c.Field4=33;c.Field9=4;c.FieldD=29;if(_player->BotLevel()>0)c.Field5=48;c.FieldF=64;break;
            case 33:c.Field4=37;c.Field9=4;c.FieldD=29;c.FieldC=62;c.FieldF=65;break;
            case 34:c.Field4=37;c.Field9=4;c.FieldD=29;c.FieldC=63;c.FieldF=65;break;
            case 35:c.Field4=33;c.Field9=4;c.FieldD=29;c.FieldC=62;c.FieldF=65;break;
            case 36:c.Field4=33;c.Field9=4;c.FieldD=29;c.FieldC=63;c.FieldF=65;break;
            case 37:c.Field4=37;c.Field9=4;c.FieldD=29;c.FieldC=62;c.FieldF=66;break;
            case 38:c.Field4=33;c.Field9=4;c.FieldD=29;c.FieldC=62;c.FieldF=66;break;
            case 39:c.Field4=37;c.Field9=4;c.FieldD=29;c.FieldC=62;c.FieldF=67;break;
            case 40:c.Field4=33;c.Field9=4;c.FieldD=29;c.FieldC=62;c.FieldF=67;break;
            case 41:c.Field4=34;c.Field9=4;c.FieldD=29;c.FieldC=62;c.FieldF=67;break;
            case 42:c.Field4=37;c.Field5=47;c.Field9=24;c.FieldD=29;c.FieldC=62;c.FieldF=65;break;
            case 43:c.Field4=37;c.Field5=47;c.Field9=24;c.FieldD=29;c.FieldC=63;c.FieldF=65;break;
            case 44:c.FieldA=31;c.FieldB=4;c.FieldD=29;c.FieldC=62;c.FieldF=68;break;
            case 45:c.Field4=33;c.Field9=4;c.FieldA=31;c.FieldB=4;c.FieldD=29;c.FieldC=62;c.FieldF=69;break;
            case 48:c.FieldA=31;c.FieldB=4;c.FieldD=29;c.FieldC=62;c.FieldF=70;break;
            case 50:c.Field4=37;c.Field9=40;break;
            case 51:c.Field4=37;c.Field9=24;break;
            case 52:c.Field4=37;c.Field9=24;c.FieldC=56;break;
            case 53:c.Field4=37;c.Field9=44;break;
            case 54:c.Field4=37;c.Field9=44;c.FieldC=56;break;
            case 55:c.FieldA=32;c.FieldB=27;c.Field4=37;c.Field9=44;_fieldB8=Vector3(29,15,0);break;
            case 56:c.Field4=37;c.Field9=45;c.FieldD=29;c.FieldC=62;c.FieldF=65;break;
            case 57:c.FieldA=32;c.FieldB=4;c.Field4=37;c.Field9=46;break;
            case 59:c.Field4=37;c.Field9=6;break;
            case 60:c.Field4=37;c.Field9=6;c.FieldC=56;break;
            case 61:c.Field4=37;c.Field9=6;c.Field8=7;break;
            case 62:c.Field4=37;c.Field9=6;c.FieldC=56;c.Field8=7;break;
            case 63:c.Field4=37;c.Field9=6;c.Field8=7;c.FieldD=29;c.FieldC=62;c.FieldF=65;break;
            case 64:c.Field4=37;c.Field9=6;c.Field8=8;break;
            case 65:c.Field4=37;c.Field9=6;c.Field8=9;break;
            case 66:c.Field4=37;c.Field9=6;c.Field8=10;break;
            case 67:c.Field4=37;c.Field9=6;c.Field8=11;break;
            case 68:c.Field4=37;c.Field9=14;break;
            case 69:c.Field4=37;c.Field9=14;c.FieldC=56;break;
            case 70:c.Field4=37;c.Field9=15;break;
            case 71:c.Field4=37;c.Field9=15;c.FieldC=56;break;
            case 72:c.Field4=37;c.Field9=16;break;
            case 73:c.Field4=37;c.Field9=16;c.FieldC=56;break;
            case 74:c.Field4=37;c.Field9=16;c.FieldC=56;c.Field6=52;break;
            case 75:c.Field4=37;c.Field9=16;c.FieldA=32;c.FieldB=4;break;
            case 76:c.Field4=37;c.Field9=23;break;
            case 77:c.Field4=37;c.Field9=23;c.FieldC=56;break;
            case 78:c.Field4=33;c.Field9=23;break;
            case 82:c.Field4=33;c.Field9=6;break;
            case 83:c.Field4=33;c.Field9=6;c.FieldC=56;break;
            case 84:c.Field4=33;c.Field9=6;c.Field8=7;break;
            case 85:c.Field4=33;c.Field9=6;c.Field8=7;c.FieldD=29;c.FieldC=62;c.FieldF=65;break;
            case 86:c.Field4=33;c.Field9=6;c.Field8=8;break;
            case 87:c.Field4=33;c.Field9=6;c.Field8=9;break;
            case 88:c.Field4=33;c.Field9=6;c.Field8=10;break;
            case 89:c.Field4=33;c.Field9=6;c.Field8=11;break;
            case 90:c.Field4=33;c.Field9=20;break;
            case 91:c.Field4=33;c.Field9=17;break;
            case 92:c.Field4=33;c.Field9=15;break;
            case 93:c.Field4=33;c.Field9=15;c.FieldC=56;break;
            case 94:c.Field4=33;c.Field9=19;break;
            case 95:c.FieldA=32;c.FieldB=26;break;
            case 96:c.FieldC=55;_fieldB8=Vector3(29,15,0);break;
            case 97:c.FieldC=56;break;
            case 98:c.FieldC=57;break;
            case 101:c.FieldC=59;break;
            case 103:c.FieldC=61;break;
            case 106:c.Field4=33;c.Field9=4;c.FieldC=56;break;
            case 108:c.Field4=37;c.Field9=4;c.FieldC=56;break;
            case 109:c.Field4=37;c.Field9=42;c.FieldC=56;break;
            case 110:c.Field4=37;c.Field9=42;c.FieldC=56;c.Field5=47;c.Field6=51;break;
            case 112:c.Field4=37;c.Field5=47;c.Field9=4;c.FieldC=56;break;
            case 113:c.Field4=37;c.Field5=47;c.Field9=5;c.FieldC=60;break;
            case 115:c.Field4=37;c.Field9=12;break;
            case 116:c.Field4=37;c.Field9=12;c.FieldA=32;c.FieldB=4;break;
            case 117:c.Field4=37;c.Field9=12;c.FieldC=56;break;
            case 118:c.Field4=37;c.Field9=12;c.FieldC=56;c.Field6=52;break;
            case 119:c.Field4=37;c.Field9=6;c.FieldD=29;break;
            case 120:c.Field4=33;c.Field9=6;c.FieldD=29;break;
            case 121:c.Field4=33;c.Field9=35;break;
            case 122:c.Field4=33;c.Field9=36;break;
            default:break;
            }
            if(_player->BotLevel()==0&&c.Field5==47)c.Field5=0;if(c.Field4!=37)Flags2&=~AiFlags2::Bit7;
        }

        void Func2135510(){FindEntityRef(AiEntRefType::Type26);Func21356C0(_entityRefs.Field26);} void Func21354E0(){FindEntityRef(AiEntRefType::Type28);Func21356C0(_entityRefs.Field28);}
        void Func2135540(){FindEntityRef(AiEntRefType::Type27);Func21356C0(_entityRefs.Field27);} void Func21354B0(){FindEntityRef(AiEntRefType::Type30);Func21356C0(_entityRefs.Field30);}
        void Func2135380(){FindEntityRef(AiEntRefType::Type31);Func21356C0(_entityRefs.Field31);} void Func2135480(){FindEntityRef(AiEntRefType::Type32);Func21356C0(_entityRefs.Field32);}
        void Func2135320(){FindEntityRef(AiEntRefType::Type74);Func2135624(_entityRefs.Field74);} void Func21355D8(){FindEntityRef(AiEntRefType::Type77);Func2135608(_entityRefs.Field77);}
        void UpdateTargetItem(const std::shared_ptr<ItemInstanceEntity>& item){if(item&&item->DespawnTimer()!=0)Flags2|=AiFlags2::TargetItem;else Flags2&=~AiFlags2::TargetItem;if(item!=_itemC8)_itemC8=item;}

        void FindEntityRef(AiEntRefType type)
        {
            if (_entityRefs.IsPopulated(static_cast<std::int32_t>(type))) return;
            if (type == AiEntRefType::Type0)
            {
                if (_player->ClosestNode) _entityRefs.Field0 = _player->ClosestNode;
                else
                {
                    Vector3 position = _player->Position;
                    position = AddY(position, _player->IsAltForm()
                        ? -(Fixed::ToFloat(_player->Values().AltColRadius) - Fixed::ToFloat(_player->Values().AltColYPos)) : -0.5F);
                    _entityRefs.Field0 = FindClosestNodeToPosition(position);
                    if (_nodeData->Simple()) _player->ClosestNode = _entityRefs.Field0;
                }
            }
            else if (type == AiEntRefType::Type1)
            {
                if ((Flags2 & AiFlags2::Bit10) != AiFlags2::None)
                {
                    FindEntityRef(AiEntRefType::Type0); _entityRefs.Field1 = _entityRefs.Field0; return;
                }
                Vector3 position = _player->Position;
                position = AddY(position, _player->IsAltForm()
                    ? -(Fixed::ToFloat(_player->Values().AltColRadius) - Fixed::ToFloat(_player->Values().AltColYPos)) : -0.5F);
                _entityRefs.Field1 = Func2138D28(position);
            }
            else if (type == AiEntRefType::Type2)
            {
                if ((Flags2 & AiFlags2::TargetPlayer) == AiFlags2::None)
                {
                    FindEntityRef(AiEntRefType::Type0); _entityRefs.Field2 = _entityRefs.Field0; return;
                }
                assert(_targetPlayer);
                if (_targetPlayer->ClosestNode) _entityRefs.Field2 = _targetPlayer->ClosestNode;
                else
                {
                    Vector3 position = _targetPlayer->Position;
                    position = AddY(position, _targetPlayer->IsAltForm()
                        ? -(Fixed::ToFloat(_targetPlayer->Values().AltColRadius) - Fixed::ToFloat(_targetPlayer->Values().AltColYPos)) : -0.5F);
                    _entityRefs.Field2 = FindClosestNonHazardNodeToPosition(position);
                    if (_nodeData->Simple()) _targetPlayer->ClosestNode = _entityRefs.Field2;
                }
            }
            else if (type == AiEntRefType::Type3)
            {
                if ((Flags2 & AiFlags2::TargetHalfturret) == AiFlags2::None)
                {
                    FindEntityRef(AiEntRefType::Type0); _entityRefs.Field3 = _entityRefs.Field0; return;
                }
                assert(_targetHalfturret);
                if (_targetHalfturret->ClosestNode()) _entityRefs.Field3 = _targetHalfturret->ClosestNode();
                else
                {
                    _entityRefs.Field3 = FindClosestNonHazardNodeToPosition(_targetHalfturret->Position);
                    if (_nodeData->Simple()) _targetHalfturret->ClosestNode() = _entityRefs.Field3;
                }
            }
            else if (type == AiEntRefType::Type4)
            {
                if (!_itemSpawnC4) { FindEntityRef(AiEntRefType::Type0); _entityRefs.Field4 = _entityRefs.Field0; return; }
                if (_nodeData->Simple()) _entityRefs.Field4 = _itemSpawnC4->ClosestNode();
                else _entityRefs.Field4 = FindClosestNonHazardNodeToPosition(_itemSpawnC4->Position);
            }
            else if (type == AiEntRefType::Type5)
            {
                if ((Flags2 & AiFlags2::TargetItem) == AiFlags2::None)
                { FindEntityRef(AiEntRefType::Type0); _entityRefs.Field5 = _entityRefs.Field0; return; }
                assert(_itemC8);
                if (_itemC8->ClosestNode()) _entityRefs.Field5 = _itemC8->ClosestNode();
                else
                {
                    _entityRefs.Field5 = FindClosestNonHazardNodeToPosition(_itemC8->Position);
                    if (_nodeData->Simple()) _itemC8->ClosestNode() = _entityRefs.Field5;
                }
            }
            else if (type == AiEntRefType::Type6)
            {
                if (_octolithFlagCC == _octolithFlagD4) { FindEntityRef(AiEntRefType::Type9); _entityRefs.Field6 = _entityRefs.Field9; }
                else { FindEntityRef(AiEntRefType::Type12); _entityRefs.Field6 = _entityRefs.Field12; }
            }
            else if (type == AiEntRefType::Type7)
            {
                if (_octolithFlagCC == _octolithFlagD4) { FindEntityRef(AiEntRefType::Type10); _entityRefs.Field7 = _entityRefs.Field10; }
                else { FindEntityRef(AiEntRefType::Type13); _entityRefs.Field7 = _entityRefs.Field13; }
            }
            else if (type == AiEntRefType::Type8)
            {
                if (_flagBaseD0 == _flagBaseD8) { FindEntityRef(AiEntRefType::Type11); _entityRefs.Field8 = _entityRefs.Field11; }
                else { FindEntityRef(AiEntRefType::Type14); _entityRefs.Field8 = _entityRefs.Field14; }
            }
            else if (type == AiEntRefType::Type9)
            {
                assert(_octolithFlagD4);
                if (_octolithFlagD4->ClosestNode()) _entityRefs.Field9 = _octolithFlagD4->ClosestNode();
                else { _entityRefs.Field9 = FindClosestNonHazardNodeToPosition(_octolithFlagD4->Position); if (_nodeData->Simple()) _octolithFlagD4->ClosestNode() = _entityRefs.Field9; }
            }
            else if (type == AiEntRefType::Type10)
            { assert(_octolithFlagD4); _entityRefs.Field10 = _nodeData->Simple() ? _octolithFlagD4->BaseClosestNode() : FindClosestNonHazardNodeToPosition(_octolithFlagD4->BasePosition()); }
            else if (type == AiEntRefType::Type11)
            { assert(_flagBaseD8); _entityRefs.Field11 = _nodeData->Simple() ? _flagBaseD8->ClosestNode() : FindClosestNonHazardNodeToPosition(_flagBaseD8->Position); }
            else if (type == AiEntRefType::Type12)
            {
                assert(_octolithFlagDC);
                if (_octolithFlagDC->ClosestNode()) _entityRefs.Field12 = _octolithFlagDC->ClosestNode();
                else { _entityRefs.Field12 = FindClosestNonHazardNodeToPosition(_octolithFlagDC->Position); if (_nodeData->Simple()) _octolithFlagDC->ClosestNode() = _entityRefs.Field12; }
            }
            else if (type == AiEntRefType::Type13)
            { assert(_octolithFlagDC); _entityRefs.Field13 = _nodeData->Simple() ? _octolithFlagDC->BaseClosestNode() : FindClosestNonHazardNodeToPosition(_octolithFlagDC->BasePosition()); }
            else if (type == AiEntRefType::Type14)
            { assert(_flagBaseE0); _entityRefs.Field14 = _nodeData->Simple() ? _flagBaseE0->ClosestNode() : FindClosestNonHazardNodeToPosition(_flagBaseE0->Position); }
            else if (type == AiEntRefType::Type15)
            {
                if ((Flags2 & AiFlags2::TargetDefense) == AiFlags2::None)
                { FindEntityRef(AiEntRefType::Type0); _entityRefs.Field15 = _entityRefs.Field0; return; }
                assert(_targetDefense);
                _entityRefs.Field15 = _nodeData->Simple() ? _targetDefense->ClosestNode() : FindClosestNonHazardNodeToPosition(_targetDefense->Position);
            }
            else if (type == AiEntRefType::Type16) _entityRefs.Field16 = FindFarthestNodeFromPosition(_player->Position);
            else if (type == AiEntRefType::Type17) { assert(_targetPlayer); _entityRefs.Field17 = FindFarthestNodeFromPosition(_targetPlayer->Position); }
            else if (type == AiEntRefType::Type18) { assert(_targetPlayer); _entityRefs.Field18 = Func21396A0(_targetPlayer->Position); }
            else if (type == AiEntRefType::Type19) _entityRefs.Field19 = FindHighestNode();
            else if (type == AiEntRefType::Type20) _entityRefs.Field20 = FindClosestVantageNodeToPosition(_player->Position);
            else if (type == AiEntRefType::Type21) _entityRefs.Field21 = FindClosestVantageNodeToPositionWithRange(_player->Position);
            else if (type == AiEntRefType::Type22) _entityRefs.Field22 = FindFarthestVantageNodeFromPosition(_player->Position);
            else if (type == AiEntRefType::Type23) _entityRefs.Field23 = GetRandomAerialNode();
            else if (type == AiEntRefType::Type24) _entityRefs.Field24 = GetRandomNavigationNodeByField4();
            else if (type == AiEntRefType::Type25) _entityRefs.Field25 = FindClosestNavigationNodeByField4(_player->Position);
            else if (type == AiEntRefType::Type26) _entityRefs.Field26 = FindClosestOpponentToPosition(_player->Position);
            else if (type == AiEntRefType::Type27) _entityRefs.Field27 = FindClosestOpponentToPosition(_player->Position, true);
            else if (type == AiEntRefType::Type28) _entityRefs.Field28 = Func2138038(_player->Position);
            else if (type == AiEntRefType::Type29) { assert(_targetPlayer); _entityRefs.Field29 = FindClosestOpponentToPosition(_targetPlayer->Position); }
            else if (type == AiEntRefType::Type30) _entityRefs.Field30 = Func2137E8C(_player->Position);
            else if (type == AiEntRefType::Type31) _entityRefs.Field31 = Func21378C0(_player->Position);
            else if (type == AiEntRefType::Type32) { auto result = Func2137AA4(_player->Position); if (result) _entityRefs.Field32 = result; }
            else if (type == AiEntRefType::Type33) _entityRefs.Field33 = Func2137D08(_player->Position);
            else if (type == AiEntRefType::Type34) _entityRefs.Field34 = FindClosestPopulatedItemSpawnToPosition(_player->Position);
            else if (type == AiEntRefType::Type35) _entityRefs.Field35 = FindClosestPopulatedItemSpawnOfTypeToPosition(_player->Position, ItemType::HealthMedium, ItemType::HealthSmall, ItemType::HealthBig);
            else if (type == AiEntRefType::Type36) _entityRefs.Field36 = FindItemSpawnForMissiles();
            else if (type == AiEntRefType::Type37) _entityRefs.Field37 = FindItemSpawnForMissiles();
            else if (type == AiEntRefType::Type38) _entityRefs.Field38 = FindItemSpawnForWeapon(ItemType::VoltDriver, BeamType::VoltDriver);
            else if (type == AiEntRefType::Type39) _entityRefs.Field39 = FindItemSpawnForUa();
            else if (type == AiEntRefType::Type40) _entityRefs.Field40 = FindItemSpawnForWeapon(ItemType::Battlehammer, BeamType::Battlehammer);
            else if (type == AiEntRefType::Type41) _entityRefs.Field41 = FindItemSpawnForUa();
            else if (type == AiEntRefType::Type42) _entityRefs.Field42 = FindItemSpawnForWeapon(ItemType::Imperialist, BeamType::Imperialist);
            else if (type == AiEntRefType::Type43) _entityRefs.Field43 = FindItemSpawnForUa();
            else if (type == AiEntRefType::Type44) _entityRefs.Field44 = FindItemSpawnForWeapon(ItemType::Judicator, BeamType::Judicator);
            else if (type == AiEntRefType::Type45) _entityRefs.Field45 = FindItemSpawnForUa();
            else if (type == AiEntRefType::Type46) _entityRefs.Field46 = FindItemSpawnForWeapon(ItemType::Magmaul, BeamType::Magmaul);
            else if (type == AiEntRefType::Type47) _entityRefs.Field47 = FindItemSpawnForUa();
            else if (type == AiEntRefType::Type48) _entityRefs.Field48 = FindItemSpawnForWeapon(ItemType::ShockCoil, BeamType::ShockCoil);
            else if (type == AiEntRefType::Type49) _entityRefs.Field49 = FindItemSpawnForUa();
            else if (type == AiEntRefType::Type50) _entityRefs.Field50 = FindItemSpawnForWeapon(ItemType::OmegaCannon, BeamType::OmegaCannon);
            else if (type == AiEntRefType::Type51) _entityRefs.Field51 = FindItemSpawnForWeapon(ItemType::OmegaCannon, BeamType::OmegaCannon);
            else if (type == AiEntRefType::Type52) _entityRefs.Field52 = FindClosestPopulatedItemSpawnOfTypeToPosition(_player->Position, ItemType::DoubleDamage);
            else if (type == AiEntRefType::Type53) _entityRefs.Field53 = FindClosestPopulatedItemSpawnOfTypeToPosition(_player->Position, ItemType::Cloak);
            else if (type == AiEntRefType::Type54) _entityRefs.Field54 = FindClosestItemToPosition(_player->Position);
            else if (type == AiEntRefType::Type55) _entityRefs.Field55 = FindClosestItemOfTypeToPosition(_player->Position, ItemType::HealthMedium, ItemType::HealthSmall, ItemType::HealthBig);
            else if (type == AiEntRefType::Type56) _entityRefs.Field56 = FindItemForMissiles();
            else if (type == AiEntRefType::Type57) _entityRefs.Field57 = FindItemForMissiles();
            else if (type == AiEntRefType::Type58) _entityRefs.Field58 = FindItemForWeapon(ItemType::VoltDriver, BeamType::VoltDriver);
            else if (type == AiEntRefType::Type59) _entityRefs.Field59 = FindItemForUa();
            else if (type == AiEntRefType::Type60) _entityRefs.Field60 = FindItemForWeapon(ItemType::Battlehammer, BeamType::Battlehammer);
            else if (type == AiEntRefType::Type61) _entityRefs.Field61 = FindItemForUa();
            else if (type == AiEntRefType::Type62) _entityRefs.Field62 = FindItemForWeapon(ItemType::Imperialist, BeamType::Imperialist);
            else if (type == AiEntRefType::Type63) _entityRefs.Field63 = FindItemForUa();
            else if (type == AiEntRefType::Type64) _entityRefs.Field64 = FindItemForWeapon(ItemType::Judicator, BeamType::Judicator);
            else if (type == AiEntRefType::Type65) _entityRefs.Field65 = FindItemForUa();
            else if (type == AiEntRefType::Type66) _entityRefs.Field66 = FindItemForWeapon(ItemType::Magmaul, BeamType::Magmaul);
            else if (type == AiEntRefType::Type67) _entityRefs.Field67 = FindItemForUa();
            else if (type == AiEntRefType::Type68) _entityRefs.Field68 = FindItemForWeapon(ItemType::ShockCoil, BeamType::ShockCoil);
            else if (type == AiEntRefType::Type69) _entityRefs.Field69 = FindItemForUa();
            else if (type == AiEntRefType::Type70) _entityRefs.Field70 = FindItemForWeapon(ItemType::OmegaCannon, BeamType::OmegaCannon);
            else if (type == AiEntRefType::Type71) _entityRefs.Field71 = FindItemForWeapon(ItemType::OmegaCannon, BeamType::OmegaCannon);
            else if (type == AiEntRefType::Type72) _entityRefs.Field72 = FindClosestItemOfTypeToPosition(_player->Position, ItemType::DoubleDamage);
            else if (type == AiEntRefType::Type73) _entityRefs.Field73 = FindClosestItemOfTypeToPosition(_player->Position, ItemType::Cloak);
            else if (type == AiEntRefType::Type74) _entityRefs.Field74 = FindClosestNodeDefense(_player->Position);
            else if (type == AiEntRefType::Type75) _entityRefs.Field75 = ChooseNodeDefenseToRetake();
            else if (type == AiEntRefType::Type76) _entityRefs.Field76 = FindClosestFriendlyNodeDefense();
            else if (type == AiEntRefType::Type77) _entityRefs.Field77 = FindClosestDoor(_player->Position);
        }

        void FindQueuedEntityRef()
        {
            if (_queuedFindEntityAction == AiQueuedEnt::Type0)
            {
                if ((Flags2 & AiFlags2::TargetPlayer) == AiFlags2::None)
                {
                    FindEntityRef(AiEntRefType::Type0);
                    _node3C = _entityRefs.Field0;
                }
                else
                {
                    FindEntityRef(AiEntRefType::Type2);
                    _node3C = _entityRefs.Field2;
                }
            }
            else if (_queuedFindEntityAction == AiQueuedEnt::Type1)
            {
                Func2135510();
                FindEntityRef(AiEntRefType::Type2);
                _node3C = _entityRefs.Field2;
            }
            else if (_queuedFindEntityAction == AiQueuedEnt::Type2)
            {
                Func21354E0();
                FindEntityRef(AiEntRefType::Type2);
                _node3C = _entityRefs.Field2;
            }
            else if (_queuedFindEntityAction == AiQueuedEnt::Type3)
            {
                Func2135510();
                FindEntityRef(AiEntRefType::Type17);
                _node3C = _entityRefs.Field17;
            }
            else if (_queuedFindEntityAction == AiQueuedEnt::Type4)
            {
                if ((Flags2 & AiFlags2::TargetPlayer) == AiFlags2::None)
                {
                    FindEntityRef(AiEntRefType::Type0);
                    _node3C = _entityRefs.Field0;
                }
                else
                {
                    FindEntityRef(AiEntRefType::Type18);
                    _node3C = _entityRefs.Field18;
                }
            }
            else if (_queuedFindEntityAction == AiQueuedEnt::Type5)
            {
                Func21354B0();
                FindEntityRef(AiEntRefType::Type2);
                _node3C = _entityRefs.Field2;
            }
            else if (_queuedFindEntityAction == AiQueuedEnt::Type6)
            {
                Func2135380();
                FindEntityRef(AiEntRefType::Type2);
                _node3C = _entityRefs.Field2;
            }
            else if (_queuedFindEntityAction == AiQueuedEnt::Type7)
            {
                Func21354B0();
                FindEntityRef(AiEntRefType::Type17);
                _node3C = _entityRefs.Field17;
            }
            else if (_queuedFindEntityAction == AiQueuedEnt::Type8)
            {
                if ((Flags2 & AiFlags2::TargetPlayer) == AiFlags2::None)
                {
                    FindEntityRef(AiEntRefType::Type0);
                    _node3C = _entityRefs.Field0;
                }
                else
                {
                    FindEntityRef(AiEntRefType::Type3);
                    _node3C = _entityRefs.Field3;
                }
            }
            else if (_queuedFindEntityAction == AiQueuedEnt::Type9)
            {
                FindEntityRef(AiEntRefType::Type19);
                _node3C = _entityRefs.Field19;
            }
            else if (_queuedFindEntityAction == AiQueuedEnt::Type10)
            {
                FindEntityRef(AiEntRefType::Type54);
                UpdateTargetItem(_entityRefs.Field54);
                if ((Flags2 & AiFlags2::TargetItem) != AiFlags2::None)
                {
                    FindEntityRef(AiEntRefType::Type5);
                    _node3C = _entityRefs.Field5;
                }
            }
            else if (_queuedFindEntityAction == AiQueuedEnt::Type11)
            {
                FindEntityRef(AiEntRefType::Type34);
                if (_itemSpawnC4 != _entityRefs.Field34)
                {
                    _itemSpawnC4 = _entityRefs.Field34;
                    if (_itemSpawnC4 && _itemSpawnC4->Item())
                    {
                        FindEntityRef(AiEntRefType::Type4);
                        _node3C = _entityRefs.Field4;
                    }
                }
            }
            else if (_queuedFindEntityAction == AiQueuedEnt::Type12)
            {
                FindEntityRef(AiEntRefType::Type55);
                UpdateTargetItem(_entityRefs.Field55);
                if ((Flags2 & AiFlags2::TargetItem) != AiFlags2::None)
                {
                    FindEntityRef(AiEntRefType::Type5);
                    _node3C = _entityRefs.Field5;
                }
                else if (_player->BotLevel() >= 3)
                {
                    FindEntityRef(AiEntRefType::Type35);
                    _itemSpawnC4 = _entityRefs.Field35;
                    if (_itemSpawnC4)
                    {
                        FindEntityRef(AiEntRefType::Type4);
                        _node3C = _entityRefs.Field4;
                    }
                    else
                    {
                        FindEntityRef(AiEntRefType::Type0);
                        _node3C = _entityRefs.Field0;
                    }
                }
                else
                {
                    FindEntityRef(AiEntRefType::Type0);
                    _node3C = _entityRefs.Field0;
                }
            }
            else if (_queuedFindEntityAction == AiQueuedEnt::Type13)
            {
                FindEntityRef(AiEntRefType::Type56);
                UpdateTargetItem(_entityRefs.Field56);
                if ((Flags2 & AiFlags2::TargetItem) != AiFlags2::None)
                {
                    FindEntityRef(AiEntRefType::Type5);
                    _node3C = _entityRefs.Field5;
                }
                else
                {
                    FindEntityRef(AiEntRefType::Type0);
                    _node3C = _entityRefs.Field0;
                }
            }
            else if (_queuedFindEntityAction == AiQueuedEnt::Type14)
            {
                FindEntityRef(AiEntRefType::Type57);
                UpdateTargetItem(_entityRefs.Field57);
                if ((Flags2 & AiFlags2::TargetItem) != AiFlags2::None)
                {
                    FindEntityRef(AiEntRefType::Type5);
                    _node3C = _entityRefs.Field5;
                }
                else
                {
                    FindEntityRef(AiEntRefType::Type0);
                    _node3C = _entityRefs.Field0;
                }
            }
            else if (_queuedFindEntityAction == AiQueuedEnt::Type15)
            {
                FindEntityRef(AiEntRefType::Type58);
                UpdateTargetItem(_entityRefs.Field58);
                if ((Flags2 & AiFlags2::TargetItem) != AiFlags2::None)
                {
                    FindEntityRef(AiEntRefType::Type5);
                    _node3C = _entityRefs.Field5;
                }
                else
                {
                    FindEntityRef(AiEntRefType::Type0);
                    _node3C = _entityRefs.Field0;
                }
            }
            else if (_queuedFindEntityAction == AiQueuedEnt::Type16)
            {
                FindEntityRef(AiEntRefType::Type59);
                UpdateTargetItem(_entityRefs.Field59);
                if ((Flags2 & AiFlags2::TargetItem) != AiFlags2::None)
                {
                    FindEntityRef(AiEntRefType::Type5);
                    _node3C = _entityRefs.Field5;
                }
                else
                {
                    FindEntityRef(AiEntRefType::Type0);
                    _node3C = _entityRefs.Field0;
                }
            }
            else if (_queuedFindEntityAction == AiQueuedEnt::Type17)
            {
                FindEntityRef(AiEntRefType::Type60);
                UpdateTargetItem(_entityRefs.Field60);
                if ((Flags2 & AiFlags2::TargetItem) != AiFlags2::None)
                {
                    FindEntityRef(AiEntRefType::Type5);
                    _node3C = _entityRefs.Field5;
                }
                else
                {
                    FindEntityRef(AiEntRefType::Type0);
                    _node3C = _entityRefs.Field0;
                }
            }
            else if (_queuedFindEntityAction == AiQueuedEnt::Type18)
            {
                FindEntityRef(AiEntRefType::Type61);
                UpdateTargetItem(_entityRefs.Field61);
                if ((Flags2 & AiFlags2::TargetItem) != AiFlags2::None)
                {
                    FindEntityRef(AiEntRefType::Type5);
                    _node3C = _entityRefs.Field5;
                }
                else
                {
                    FindEntityRef(AiEntRefType::Type0);
                    _node3C = _entityRefs.Field0;
                }
            }
            else if (_queuedFindEntityAction == AiQueuedEnt::Type19)
            {
                FindEntityRef(AiEntRefType::Type42);
                UpdateTargetItem(_entityRefs.Field42 ? _entityRefs.Field42->Item() : nullptr);
                if ((Flags2 & AiFlags2::TargetItem) != AiFlags2::None)
                {
                    FindEntityRef(AiEntRefType::Type5);
                    _node3C = _entityRefs.Field5;
                }
                else
                {
                    FindEntityRef(AiEntRefType::Type0);
                    _node3C = _entityRefs.Field0;
                }
            }
            else if (_queuedFindEntityAction == AiQueuedEnt::Type20)
            {
                FindEntityRef(AiEntRefType::Type43);
                UpdateTargetItem(_entityRefs.Field43 ? _entityRefs.Field43->Item() : nullptr);
                if ((Flags2 & AiFlags2::TargetItem) != AiFlags2::None)
                {
                    FindEntityRef(AiEntRefType::Type5);
                    _node3C = _entityRefs.Field5;
                }
                else
                {
                    FindEntityRef(AiEntRefType::Type0);
                    _node3C = _entityRefs.Field0;
                }
            }
            else if (_queuedFindEntityAction == AiQueuedEnt::Type21)
            {
                FindEntityRef(AiEntRefType::Type44);
                UpdateTargetItem(_entityRefs.Field44 ? _entityRefs.Field44->Item() : nullptr);
                if ((Flags2 & AiFlags2::TargetItem) != AiFlags2::None)
                {
                    FindEntityRef(AiEntRefType::Type5);
                    _node3C = _entityRefs.Field5;
                }
                else
                {
                    FindEntityRef(AiEntRefType::Type0);
                    _node3C = _entityRefs.Field0;
                }
            }
            else if (_queuedFindEntityAction == AiQueuedEnt::Type22)
            {
                FindEntityRef(AiEntRefType::Type45);
                UpdateTargetItem(_entityRefs.Field45 ? _entityRefs.Field45->Item() : nullptr);
                if ((Flags2 & AiFlags2::TargetItem) != AiFlags2::None)
                {
                    FindEntityRef(AiEntRefType::Type5);
                    _node3C = _entityRefs.Field5;
                }
                else
                {
                    FindEntityRef(AiEntRefType::Type0);
                    _node3C = _entityRefs.Field0;
                }
            }
            else if (_queuedFindEntityAction == AiQueuedEnt::Type23)
            {
                FindEntityRef(AiEntRefType::Type46);
                UpdateTargetItem(_entityRefs.Field46 ? _entityRefs.Field46->Item() : nullptr);
                if ((Flags2 & AiFlags2::TargetItem) != AiFlags2::None)
                {
                    FindEntityRef(AiEntRefType::Type5);
                    _node3C = _entityRefs.Field5;
                }
                else
                {
                    FindEntityRef(AiEntRefType::Type0);
                    _node3C = _entityRefs.Field0;
                }
            }
            else if (_queuedFindEntityAction == AiQueuedEnt::Type24)
            {
                FindEntityRef(AiEntRefType::Type47);
                UpdateTargetItem(_entityRefs.Field47 ? _entityRefs.Field47->Item() : nullptr);
                if ((Flags2 & AiFlags2::TargetItem) != AiFlags2::None)
                {
                    FindEntityRef(AiEntRefType::Type5);
                    _node3C = _entityRefs.Field5;
                }
                else
                {
                    FindEntityRef(AiEntRefType::Type0);
                    _node3C = _entityRefs.Field0;
                }
            }
            else if (_queuedFindEntityAction == AiQueuedEnt::Type25)
            {
                FindEntityRef(AiEntRefType::Type48);
                UpdateTargetItem(_entityRefs.Field48 ? _entityRefs.Field48->Item() : nullptr);
                if ((Flags2 & AiFlags2::TargetItem) != AiFlags2::None)
                {
                    FindEntityRef(AiEntRefType::Type5);
                    _node3C = _entityRefs.Field5;
                }
                else
                {
                    FindEntityRef(AiEntRefType::Type0);
                    _node3C = _entityRefs.Field0;
                }
            }
            else if (_queuedFindEntityAction == AiQueuedEnt::Type26)
            {
                FindEntityRef(AiEntRefType::Type49);
                UpdateTargetItem(_entityRefs.Field49 ? _entityRefs.Field49->Item() : nullptr);
                if ((Flags2 & AiFlags2::TargetItem) != AiFlags2::None)
                {
                    FindEntityRef(AiEntRefType::Type5);
                    _node3C = _entityRefs.Field5;
                }
                else
                {
                    FindEntityRef(AiEntRefType::Type0);
                    _node3C = _entityRefs.Field0;
                }
            }
            else if (_queuedFindEntityAction == AiQueuedEnt::Type27 || _queuedFindEntityAction == AiQueuedEnt::Type28)
            {
                FindEntityRef(AiEntRefType::Type6);
                _node3C = _entityRefs.Field6;
            }
            else if (_queuedFindEntityAction == AiQueuedEnt::Type29)
            {
                FindEntityRef(AiEntRefType::Type8);
                _node3C = _entityRefs.Field8;
            }
            else if (_queuedFindEntityAction == AiQueuedEnt::Type30 || _queuedFindEntityAction == AiQueuedEnt::Type31)
            {
                FindEntityRef(AiEntRefType::Type9);
                _node3C = _entityRefs.Field9;
            }
            else if (_queuedFindEntityAction == AiQueuedEnt::Type32)
            {
                FindEntityRef(AiEntRefType::Type11);
                _node3C = _entityRefs.Field11;
            }
            else if (_queuedFindEntityAction == AiQueuedEnt::Type33 || _queuedFindEntityAction == AiQueuedEnt::Type34)
            {
                FindEntityRef(AiEntRefType::Type12);
                _node3C = _entityRefs.Field12;
            }
            else if (_queuedFindEntityAction == AiQueuedEnt::Type35)
            {
                FindEntityRef(AiEntRefType::Type14);
                _node3C = _entityRefs.Field14;
            }
            else if (_queuedFindEntityAction == AiQueuedEnt::Type36)
            {
                FindEntityRef(AiEntRefType::Type15);
                _node3C = _entityRefs.Field15;
            }
            else if (_queuedFindEntityAction == AiQueuedEnt::Type37)
            {
                FindEntityRef(AiEntRefType::Type20);
                _node3C = _entityRefs.Field20;
            }
            else if (_queuedFindEntityAction == AiQueuedEnt::Type38)
            {
                FindEntityRef(AiEntRefType::Type21);
                _node3C = _entityRefs.Field21;
            }
            else if (_queuedFindEntityAction == AiQueuedEnt::Type39)
            {
                FindEntityRef(AiEntRefType::Type22);
                _node3C = _entityRefs.Field22;
            }
        }

        std::shared_ptr<ItemSpawnEntity> FindItemSpawnForWeapon(ItemType itemType, BeamType beamType)
        {
            ItemType type2 = ItemType::None;
            if (Weapons::AffinityWeapons[static_cast<std::int32_t>(_player->Hunter())] == beamType) type2 = ItemType::AffinityWeapon;
            return FindClosestPopulatedItemSpawnOfTypeToPosition(_player->Position, itemType, type2);
        }
        std::shared_ptr<ItemInstanceEntity> FindItemForWeapon(ItemType itemType, BeamType beamType)
        {
            ItemType type2 = ItemType::None;
            if (Weapons::AffinityWeapons[static_cast<std::int32_t>(_player->Hunter())] == beamType) type2 = ItemType::AffinityWeapon;
            return FindClosestItemOfTypeToPosition(_player->Position, itemType, type2);
        }
        std::shared_ptr<ItemSpawnEntity> FindItemSpawnForUa() { return FindClosestPopulatedItemSpawnOfTypeToPosition(_player->Position, ItemType::UASmall, ItemType::UABig); }
        std::shared_ptr<ItemInstanceEntity> FindItemForUa() { return FindClosestItemOfTypeToPosition(_player->Position, ItemType::UASmall, ItemType::UABig); }
        std::shared_ptr<ItemSpawnEntity> FindItemSpawnForMissiles()
        {
            ItemType type3 = ItemType::None;
            if (Weapons::AffinityWeapons[static_cast<std::int32_t>(_player->Hunter())] == BeamType::Missile) type3 = ItemType::AffinityWeapon;
            return FindClosestPopulatedItemSpawnOfTypeToPosition(_player->Position, ItemType::MissileSmall, ItemType::MissileBig, type3);
        }
        std::shared_ptr<ItemInstanceEntity> FindItemForMissiles()
        {
            ItemType type3 = ItemType::None;
            if (Weapons::AffinityWeapons[static_cast<std::int32_t>(_player->Hunter())] == BeamType::Missile) type3 = ItemType::AffinityWeapon;
            return FindClosestItemOfTypeToPosition(_player->Position, ItemType::MissileSmall, ItemType::MissileBig, type3);
        }

        std::shared_ptr<Formats::NodeData3> FindClosestNodeToPosition(Vector3 position)
        {
            assert(_nodeList && !_nodeList->empty()); auto result=(*_nodeList)[0]; float minDist=DistanceSquared(result->Position,position);
            for(std::size_t i=1;i<_nodeList->size();i++){auto node=(*_nodeList)[i];float dist=DistanceSquared(node->Position,position);if(dist<minDist){result=node;minDist=dist;}} return result;
        }
        std::shared_ptr<Formats::NodeData3> FindFarthestNodeFromPosition(Vector3 position)
        {
            assert(_nodeList && !_nodeList->empty()); auto result=(*_nodeList)[0]; float maxDist=DistanceSquared(result->Position,position);
            for(std::size_t i=1;i<_nodeList->size();i++){auto node=(*_nodeList)[i];float dist=DistanceSquared(node->Position,position);if(dist>maxDist){result=node;maxDist=dist;}} return result;
        }
        std::shared_ptr<Formats::NodeData3> Func2138D28(Vector3 position)
        {
            _field4C[1].reset(); std::int32_t v4=0; float minDist=100000; std::array<float,10> distList{};
            for(std::size_t i=0;i<_nodeList->size();i++)
            {
                std::int32_t j=0; for(;j<_field78;j++) if(_field7A[j]==i) break;
                if(j>=_field78)
                {
                    auto node=(*_nodeList)[i]; float dist=DistanceSquared(node->Position,position);
                    if(dist<minDist)
                    {
                        std::int32_t k=0;for(;k<v4;k++)if(dist<distList[k])break;
                        for(std::int32_t l=v4;l>k;l--){_field4C[l+1]=_field4C[l];distList[l]=distList[l-1];}
                        if(k<10){_field4C[k+1]=node;distList[k]=dist;}
                        minDist=distList[v4]; if(v4<9)v4++;
                    }
                }
            }
            auto result=_field4C[1]; if(result){Flags2|=AiFlags2::Bit10;Func214B810(v4);return result;} return FindClosestNonHazardNodeToPosition(position);
        }
        void Func214B810(std::int32_t v4)
        {
            for (std::int32_t i = 0; i < _globalField2; ++i)
            {
                AiGlobals& obj = _globalObjs[static_cast<std::size_t>(i)];
                if (obj.Player == _player)
                {
                    obj.Field4 = v4;
                    obj.NodeData = &_field4C;
                    obj.NodeDataIndex = 1;
                    return;
                }
            }
            assert(_globalField2 < static_cast<std::int32_t>(_globalObjs.size()));
            AiGlobals& nextObj = _globalObjs[static_cast<std::size_t>(_globalField2)];
            nextObj.Player = _player;
            nextObj.Field4 = v4;
            nextObj.NodeData = &_field4C;
            nextObj.NodeDataIndex = 1;
            ++_globalField2;
        }
        std::shared_ptr<Formats::NodeData3> FindClosestNonHazardNodeToPosition(Vector3 position)
        {
            assert(_nodeList&&!_nodeList->empty());auto result=(*_nodeList)[0];float minDist=DistanceSquared(result->Position,position);
            for(std::size_t i=1;i<_nodeList->size();i++){auto node=(*_nodeList)[i];if(node->NodeType!=NodeType::Hazard){float dist=DistanceSquared(node->Position,position);if(dist<minDist||result->NodeType==NodeType::Hazard){result=node;minDist=dist;}}}return result;
        }
        std::int32_t Func213A0A4(const std::shared_ptr<Formats::NodeData3>& head, std::array<std::shared_ptr<Formats::NodeData3>,20>& nodeList)
        {
            if(!_nodeList||_nodeList->empty()||!head)return 0;std::int32_t count=0,i=0,index=0;
            while(i<static_cast<std::int32_t>(_nodeList->size())){std::int32_t j=0;for(;j<count;j++)if(nodeList[j]==(*_nodeList)[(*head->Values)[head->Index1+index+1]])break;if(j==count){auto node=(*_nodeList)[(*head->Values)[head->Index1+index+1]];if(node!=head){nodeList[count]=node;count++;if(count>=19)break;}}i+=(*head->Values)[head->Index1+index];index+=2;}return count;
        }
        std::shared_ptr<Formats::NodeData3> Func213A1A8()
        {
            assert(_node3C&&_node40);std::int32_t index=0;std::int32_t maxId=_node3C->Id;std::int32_t valueTotal=0;
            if((*_node40->Values)[_node40->Index1]<=maxId){std::int32_t value2;do{std::int32_t value=(*_node40->Values)[_node40->Index1+index];valueTotal+=value;index+=2;value2=valueTotal+(*_node40->Values)[_node40->Index1+index];}while(value2<=maxId);}return (*_nodeList)[(*_node40->Values)[_node40->Index1+index+1]];
        }
        std::shared_ptr<Formats::NodeData3> Func21396A0(Vector3 position)
        {
            std::array<std::shared_ptr<Formats::NodeData3>,20> nodeList{};std::int32_t count=Func213A0A4(_node40,nodeList);if(count==0)return _node40;auto result=nodeList[0];float maxDist=LengthSquared((position-result->Position))-LengthSquared((static_cast<Vector3>(_player->Position) - static_cast<Vector3>(result->Position)));for(std::int32_t i=1;i<count;i++){auto node=nodeList[i];float dist=LengthSquared((position-node->Position))-LengthSquared((static_cast<Vector3>(_player->Position) - static_cast<Vector3>(node->Position)));if(dist>maxDist){result=node;maxDist=dist;}}return result;
        }
        std::shared_ptr<Formats::NodeData3> FindHighestNode() { return (*_nodeList)[0]; }
        std::shared_ptr<Formats::NodeData3> FindClosestVantageNodeToPosition(Vector3 position)
        {
            auto result=(*_nodeList)[0];float minDist=DistanceSquared(result->Position,position);for(std::size_t i=1;i<_nodeList->size();i++){auto node=(*_nodeList)[i];if(node->NodeType!=NodeType::Vantage&&result->NodeType==NodeType::Vantage)continue;float dist=DistanceSquared(node->Position,position);if(dist<minDist){result=node;minDist=dist;}}return result;
        }
        std::shared_ptr<Formats::NodeData3> FindClosestVantageNodeToPositionWithRange(Vector3 position)
        {
            auto result=(*_nodeList)[0];float minDist=DistanceSquared(result->Position,position);bool resultInRange=IsNodeInRange(*result);
            for(std::size_t i=1;i<_nodeList->size();i++){auto node=(*_nodeList)[i];if(node->NodeType!=NodeType::Vantage&&result->NodeType==NodeType::Vantage)continue;float dist=DistanceSquared(node->Position,position);bool nodeInRange=IsNodeInRange(*node);if((result->NodeType!=NodeType::Vantage&&node->NodeType==NodeType::Vantage)||(dist<minDist&&(!nodeInRange||(result->NodeType!=NodeType::Vantage&&node->NodeType!=NodeType::Vantage)))||(result->NodeType==NodeType::Vantage&&node->NodeType==NodeType::Vantage&&resultInRange&&!nodeInRange)){result=node;resultInRange=nodeInRange;minDist=dist;}}return result;
        }
        std::shared_ptr<Formats::NodeData3> FindFarthestVantageNodeFromPosition(Vector3 position)
        {
            auto result=(*_nodeList)[0];float maxDist=DistanceSquared(result->Position,position);for(std::size_t i=1;i<_nodeList->size();i++){auto node=(*_nodeList)[i];if(node->NodeType!=NodeType::Vantage&&result->NodeType==NodeType::Vantage)continue;float dist=DistanceSquared(node->Position,position);if(dist>maxDist){result=node;maxDist=dist;}}return result;
        }
        bool IsNodeInRange(const Formats::NodeData3& node)
        {
            if(_player->_timeSinceJumpPad>10&&IsJumpPadNode(node))return false;if(!HasFlag(_player->Flags1(), PlayerFlags1::Grounded)&&(!_node40||_node40->NodeType!=NodeType::Aerial))return false;Vector3 between=node.Position-_player->Position;between=AddY(between, _player->IsAltForm()?Fixed::ToFloat(_player->Values().AltColRadius)-Fixed::ToFloat(_player->Values().AltColYPos):0.5F);return LengthSquared(between)<node.MaxDistance*node.MaxDistance;
        }
        bool IsJumpPadNode(const Formats::NodeData3& node)
        { for (auto _enumerator18 = _scene.GetJumpPadEntities().GetEnumerator(); _enumerator18.MoveNext(); )
            if (const auto jumpPad = _enumerator18.Current(); true)if(reinterpret_cast<const void*>(jumpPad->ClosestNode().get()) == reinterpret_cast<const void*>(&node))return true;return false; }
        std::shared_ptr<Formats::NodeData3> GetRandomAerialNode()
        {std::int32_t a=_nodeTypeIndex[static_cast<int>(NodeType::Aerial)],v=_nodeTypeIndex[static_cast<int>(NodeType::Vantage)];if(a==v)return GetRandomNavigationNode();return (*_nodeList)[a+static_cast<int>(Rng::GetRandomInt2(v-a))];}
        std::shared_ptr<Formats::NodeData3> GetRandomNavigationNode()
        {return (*_nodeList)[static_cast<int>(Rng::GetRandomInt2(_nodeTypeIndex[static_cast<int>(NodeType::Navigation)]))];}
        std::shared_ptr<Formats::NodeData3> GetRandomNavigationNodeByField4()
        {std::int32_t n=_nodeTypeIndex[static_cast<int>(NodeType::Navigation)],s=_nodeTypeIndex[static_cast<int>(NodeType::Special)];while(n<s){if((*_nodeList)[n]->Field4==_field30)break;n++;}std::int32_t e=n;while(n<s){auto node=(*_nodeList)[e];if(node->Field4!=_field30)break;e++;}if(e==n)return GetRandomNavigationNode();return (*_nodeList)[n+static_cast<int>(Rng::GetRandomInt2(e-n))];}
        std::shared_ptr<Formats::NodeData3> FindClosestNavigationNodeByField4(Vector3 position)
        {std::int32_t n=_nodeTypeIndex[static_cast<int>(NodeType::Navigation)],s=_nodeTypeIndex[static_cast<int>(NodeType::Special)];if(n==s)return FindClosestNonHazardNodeToPosition(position);auto result=(*_nodeList)[n];float min=DistanceSquared(result->Position,position);for(std::int32_t i=n+1;i<s;i++){auto node=(*_nodeList)[i];if(node->Field4!=_field30&&result->Field4==_field30)break;float d=DistanceSquared(node->Position,position);if(d<min||(node->Field4==_field30&&result->Field4!=_field30)){result=node;min=d;}}return result;}

        std::shared_ptr<PlayerEntity> FindClosestOpponentToPosition(Vector3 position, bool botsOnly = false)
        {
            std::shared_ptr<PlayerEntity> result{};
            float minDist = std::numeric_limits<float>::max();
            Flags2 |= AiFlags2::Bit9;
            for (auto _enumerator19 = _scene.GetPlayerEntities().GetEnumerator(); _enumerator19.MoveNext(); )
            if (const auto player = _enumerator19.Current(); true)
            {
                if (!result)
                {
                    result = player;
                    minDist = DistanceSquared(player->Position, position);
                }
                if (player != _player && player->TeamIndex() != _player->TeamIndex() && player->ModInPlay()
                    && (!botsOnly || player->IsBot()))
                {
                    float dist = DistanceSquared(player->Position, position);
                    if (dist <= minDist || result->Health() == 0 || botsOnly && !result->IsBot())
                    {
                        result = player;
                        minDist = dist;
                        Flags2 &= ~AiFlags2::Bit9;
                    }
                }
            }
            assert(result);
            return result;
        }

        std::shared_ptr<PlayerEntity> Func2138038(Vector3 position)
        {
            std::shared_ptr<PlayerEntity> result{};
            float minDist = std::numeric_limits<float>::max();
            Flags2 |= AiFlags2::Bit9;
            for (auto _enumerator20 = _scene.GetPlayerEntities().GetEnumerator(); _enumerator20.MoveNext(); )
            if (const auto player = _enumerator20.Current(); true)
            {
                if (!result)
                {
                    result = player;
                    minDist = DistanceSquared(player->Position, position);
                }
                if (AggroFunc214857C(6, 1, 2, nullptr, player)
                    && player != _player && player->TeamIndex() != _player->TeamIndex() && player->ModInPlay())
                {
                    float dist = DistanceSquared(player->Position, position);
                    if (dist <= minDist || result->Health() == 0)
                    {
                        result = player;
                        minDist = dist;
                        Flags2 &= ~AiFlags2::Bit9;
                    }
                }
            }
            assert(result);
            return result;
        }

        std::shared_ptr<PlayerEntity> Func2137E8C(Vector3 position)
        {
            std::shared_ptr<PlayerEntity> result{};
            float minDist = std::numeric_limits<float>::max();
            float dist = 0.0F;
            std::int32_t maxValue = 0;
            Flags2 |= AiFlags2::Bit9;
            for (auto _enumerator21 = _scene.GetPlayerEntities().GetEnumerator(); _enumerator21.MoveNext(); )
            if (const auto player = _enumerator21.Current(); true)
            {
                if (!result)
                {
                    result = player;
                    minDist = DistanceSquared(player->Position, position);
                }
                if (player != _player && player->TeamIndex() != _player->TeamIndex() && player->ModInPlay())
                {
                    std::int32_t value = AggroFunc2148394(7, 2, 1, player, nullptr);
                    if (value > maxValue)
                    {
                        result = player;
                        minDist = dist;
                        maxValue = value;
                        Flags2 &= ~AiFlags2::Bit9;
                    }
                    else if (value == maxValue)
                    {
                        dist = DistanceSquared(player->Position, position);
                        if (dist <= minDist || result->Health() == 0)
                        {
                            result = player;
                            minDist = dist;
                            Flags2 &= ~AiFlags2::Bit9;
                        }
                    }
                }
            }
            assert(result);
            return result;
        }

        std::shared_ptr<PlayerEntity> Func21378C0(Vector3 position)
        {
            std::shared_ptr<PlayerEntity> result{};
            float minDist = std::numeric_limits<float>::max();
            float dist = minDist;
            std::int32_t maxValue = 0;
            Flags2 |= AiFlags2::Bit9;
            for (auto _enumerator22 = _scene.GetPlayerEntities().GetEnumerator(); _enumerator22.MoveNext(); )
            if (const auto player = _enumerator22.Current(); true)
            {
                if (!result)
                {
                    result = player;
                    minDist = DistanceSquared(player->Position, position);
                }
                if (AggroFunc214857C(6, 1, 2, nullptr, player)
                    && player != _player && player->TeamIndex() != _player->TeamIndex() && player->ModInPlay())
                {
                    std::int32_t value = AggroFunc2148394(7, 2, 1, player, nullptr);
                    if (value > maxValue)
                    {
                        result = player;
                        minDist = dist;
                        maxValue = value;
                        Flags2 &= ~AiFlags2::Bit9;
                    }
                    else if (value == maxValue)
                    {
                        dist = DistanceSquared(player->Position, position);
                        if (dist <= minDist || result->Health() == 0)
                        {
                            result = player;
                            minDist = dist;
                            Flags2 &= ~AiFlags2::Bit9;
                        }
                    }
                }
            }
            assert(result);
            return result;
        }

        std::shared_ptr<PlayerEntity> Func2137AA4(Vector3 position)
        {
            std::shared_ptr<PlayerEntity> result{};
            bool v4 = false;
            std::int32_t maxValue = -50000;
            Flags2 |= AiFlags2::Bit9;
            for (auto _enumerator23 = _scene.GetPlayerEntities().GetEnumerator(); _enumerator23.MoveNext(); )
            if (const auto player = _enumerator23.Current(); true)
            {
                bool v14 = AggroFunc214857C(6, 1, 2, nullptr, player);
                if ((v14 || !v4) && player != _player && player->TeamIndex() != _player->TeamIndex() && player->ModInPlay())
                {
                    std::int32_t value = AggroFunc2148394(7, 2, 1, player, nullptr);
                    for (auto _enumerator24 = _scene.GetPlayerEntities().GetEnumerator(); _enumerator24.MoveNext(); )
            if (const auto other = _enumerator24.Current(); true)
                    {
                        if (other != _player && other->TeamIndex() == _player->TeamIndex() && other->ModInPlay())
                        {
                            value += AggroFunc2148394(7, 2, 2, player, other);
                        }
                    }
                    std::int32_t dist = static_cast<std::int32_t>(DistanceSquared(player->Position, position));
                    if (dist < 400)
                    {
                        value += (400 - dist) / 4;
                    }
                    value += 2 * player->_frozenTimer / 2;
                    if (value > maxValue || !v4 && v14)
                    {
                        if (v14)
                        {
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

        std::shared_ptr<HalfturretEntity> Func2137D08(Vector3 position)
        {
            std::shared_ptr<HalfturretEntity> result{};
            float minDist = 10000.0F;
            float dist = minDist;
            std::int32_t maxValue = 0;
            for (auto _enumerator25 = _scene.GetPlayerEntities().GetEnumerator(); _enumerator25.MoveNext(); )
            if (const auto player = _enumerator25.Current(); true)
            {
                if (player != _player && player->TeamIndex() != _player->TeamIndex() && player->ModInPlay()
                    && player->Hunter() == Hunter::Weavel && player->Halfturret()->Health() > 0)
                {
                    std::int32_t value = AggroFunc2148394(5, 2, 1, player, nullptr);
                    if (value == 0)
                    {
                        if (player->Halfturret()->Target() != _player)
                        {
                            continue;
                        }
                        value = 1;
                    }
                    if (value > maxValue)
                    {
                        result = player->Halfturret();
                        minDist = dist;
                        maxValue = value;
                    }
                    else if (value == maxValue)
                    {
                        dist = DistanceSquared(player->Position, position);
                        if (dist <= minDist)
                        {
                            result = player->Halfturret();
                            minDist = dist;
                        }
                    }
                }
            }
            return result;
        }

        std::shared_ptr<ItemSpawnEntity> FindClosestPopulatedItemSpawnToPosition(Vector3 position, bool checkNeeded = true)
        {
            std::shared_ptr<ItemSpawnEntity> result{};
            float minDist = std::numeric_limits<float>::max();
            for (auto _enumerator26 = _scene.GetItemSpawnEntities().GetEnumerator(); _enumerator26.MoveNext(); )
            if (const auto itemSpawn = _enumerator26.Current(); true)
            {
                if (!result)
                {
                    result = itemSpawn;
                    if (Func2137860())
                    {
                        return result;
                    }
                    continue;
                }
                if ((!checkNeeded || !IsItemNotNeeded(itemSpawn->Data().ItemType))
                    && (GameState::Mode() != GameMode::PrimeHunter || GameState::PrimeHunter() != _player->SlotIndex() || !IsHealth(*itemSpawn))
                    && (!result || !result->Item() || itemSpawn->Item())
                    && (!itemSpawn->Item() || !Func21377FC(*itemSpawn->Item())))
                {
                    float dist = DistanceSquared(itemSpawn->Position, position);
                    if (!result || !result->Item() && itemSpawn->Item() || dist < minDist)
                    {
                        result = itemSpawn;
                        minDist = dist;
                    }
                }
            }
            return result;
        }

        std::shared_ptr<ItemSpawnEntity> FindClosestPopulatedItemSpawnOfTypeToPosition(Vector3 position,
            ItemType type1, ItemType type2 = ItemType::None, ItemType type3 = ItemType::None)
        {
            auto isType = [type1, type2, type3](const ItemSpawnEntity& candidate)
            {
                return candidate.Data().ItemType == type1
                    || type2 != ItemType::None && candidate.Data().ItemType == type2
                    || type3 != ItemType::None && candidate.Data().ItemType == type3;
            };
            std::shared_ptr<ItemSpawnEntity> result{};
            float minDist = std::numeric_limits<float>::max();
            for (auto _enumerator27 = _scene.GetItemSpawnEntities().GetEnumerator(); _enumerator27.MoveNext(); )
            if (const auto itemSpawn = _enumerator27.Current(); true)
            {
                if (!result)
                {
                    result = itemSpawn;
                    if (Func2137860())
                    {
                        return result;
                    }
                    continue;
                }
                if (!isType(*itemSpawn) || itemSpawn->Item() && Func21377FC(*itemSpawn->Item()))
                {
                    continue;
                }
                float dist = DistanceSquared(itemSpawn->Position, position);
                if (!result->Item() && itemSpawn->Item() || !isType(*result) || dist < minDist)
                {
                    result = itemSpawn;
                    minDist = dist;
                }
            }
            return result;
        }

        std::shared_ptr<ItemInstanceEntity> FindClosestItemToPosition(Vector3 position, bool checkNeeded = true)
        {
            std::shared_ptr<ItemInstanceEntity> result{};
            float minDist = std::numeric_limits<float>::max();
            if (Func2137860())
            {
                return result;
            }
            for (auto _enumerator28 = _scene.GetItemInstanceEntities().GetEnumerator(); _enumerator28.MoveNext(); )
            if (const auto item = _enumerator28.Current(); true)
            {
                if ((!checkNeeded || !IsItemNotNeeded(item->ItemType()))
                    && (GameState::Mode() != GameMode::PrimeHunter || GameState::PrimeHunter() != _player->SlotIndex() || !IsHealth(*item))
                    && item->DespawnTimer() != 0 && !Func21377FC(*item))
                {
                    float dist = DistanceSquared(item->Position, position);
                    if (dist < minDist)
                    {
                        result = item;
                        minDist = dist;
                    }
                }
            }
            return result;
        }

        std::shared_ptr<ItemInstanceEntity> FindClosestItemOfTypeToPosition(Vector3 position,
            ItemType type1, ItemType type2 = ItemType::None, ItemType type3 = ItemType::None)
        {
            auto isType = [type1, type2, type3](const ItemInstanceEntity& candidate)
            {
                return candidate.ItemType() == type1
                    || type2 != ItemType::None && candidate.ItemType() == type2
                    || type3 != ItemType::None && candidate.ItemType() == type3;
            };
            std::shared_ptr<ItemInstanceEntity> result{};
            if (Func2137860())
            {
                return result;
            }
            float minDist = std::numeric_limits<float>::max();
            for (auto _enumerator29 = _scene.GetItemInstanceEntities().GetEnumerator(); _enumerator29.MoveNext(); )
            if (const auto item = _enumerator29.Current(); true)
            {
                if (isType(*item) && item->DespawnTimer() != 0 && !Func21377FC(*item))
                {
                    float dist = DistanceSquared(item->Position, position);
                    if (dist < minDist)
                    {
                        result = item;
                        minDist = dist;
                    }
                }
            }
            return result;
        }

        bool Func2137860() const
        {
            return GameState::IsOctolithMode()
                && (_scene.RoomId() == 93 || _scene.RoomId() == 99 && _octolithFlagDC && _octolithFlagDC->Carrier() == _player);
        }

        bool IsItemNotNeeded(ItemType itemType) const
        {
            if (itemType == ItemType::HealthMedium || itemType == ItemType::HealthSmall || itemType == ItemType::HealthBig)
            {
                return _player->Health() == _player->HealthMax();
            }
            if (itemType == ItemType::UASmall || itemType == ItemType::UABig)
            {
                return _player->_ammo[0] == _player->_ammoMax[0];
            }
            if (itemType == ItemType::MissileSmall || itemType == ItemType::MissileBig
                || itemType == ItemType::AffinityWeapon && _player->Hunter() == Hunter::Samus)
            {
                return _player->_ammo[1] == _player->_ammoMax[1];
            }
            if (itemType == ItemType::VoltDriver || itemType == ItemType::Battlehammer || itemType == ItemType::Imperialist
                || itemType == ItemType::Judicator || itemType == ItemType::Magmaul || itemType == ItemType::ShockCoil
                || itemType == ItemType::OmegaCannon || itemType == ItemType::AffinityWeapon)
            {
                std::int32_t weapon = static_cast<std::int32_t>(itemType) - 4;
                if (itemType == ItemType::AffinityWeapon)
                {
                    weapon = static_cast<std::int32_t>(Weapons::AffinityWeapons[static_cast<std::int32_t>(_player->Hunter())]);
                }
                return _player->AvailableWeapons()[weapon];
            }
            return false;
        }

        bool IsHealth(const ItemSpawnEntity& itemSpawn) const
        {
            return itemSpawn.Data().ItemType == ItemType::HealthMedium
                || itemSpawn.Data().ItemType == ItemType::HealthSmall
                || itemSpawn.Data().ItemType == ItemType::HealthBig;
        }

        bool IsHealth(const ItemInstanceEntity& item) const
        {
            return item.ItemType() == ItemType::HealthMedium
                || item.ItemType() == ItemType::HealthSmall
                || item.ItemType() == ItemType::HealthBig;
        }

        bool Func21377FC(const ItemInstanceEntity& item) const
        {
            return GameState::IsOctolithMode() && _scene.RoomId() == 117
                && _octolithFlagDC && _octolithFlagDC->Carrier() == _player && item.Owner() && item.Owner()->Id == 53;
        }

        std::shared_ptr<NodeDefenseEntity> FindClosestNodeDefense(Vector3 position)
        {
            std::shared_ptr<NodeDefenseEntity> result{};
            float minDist = std::numeric_limits<float>::max();
            for (auto _enumerator30 = _scene.GetNodeDefenseEntities().GetEnumerator(); _enumerator30.MoveNext(); )
            if (const auto defense = _enumerator30.Current(); true)
            {
                float dist = DistanceSquared(defense->Position, position);
                if (dist < minDist)
                {
                    result = defense;
                    minDist = dist;
                }
            }
            return result;
        }

        std::shared_ptr<NodeDefenseEntity> ChooseNodeDefenseToRetake()
        {
            std::shared_ptr<NodeDefenseEntity> firstResult{};
            std::vector<std::int32_t> captureList(static_cast<std::size_t>(PlayerEntity::SlotCapacity));
            std::int32_t maxCaptureCount = 0;
            std::int32_t maxCaptureSlotIndex = 0;
            for (auto _enumerator31 = _scene.GetNodeDefenseEntities().GetEnumerator(); _enumerator31.MoveNext(); )
            if (const auto defense = _enumerator31.Current(); true)
            {
                if (!firstResult)
                {
                    firstResult = defense;
                }
                if (defense->CapturedPlayer() && defense->CapturedPlayer()->TeamIndex() != _player->TeamIndex())
                {
                    std::int32_t captureCount = ++captureList[static_cast<std::size_t>(defense->CapturedPlayer()->SlotIndex())];
                    if (captureCount > maxCaptureCount)
                    {
                        maxCaptureSlotIndex = defense->CapturedPlayer()->SlotIndex();
                        maxCaptureCount = captureCount;
                    }
                }
            }
            std::int32_t resultCount = 0;
            std::array<std::shared_ptr<NodeDefenseEntity>, 10> resultList{};
            if (maxCaptureCount > 0)
            {
                std::int32_t playerCount = 0;
                std::array<std::shared_ptr<PlayerEntity>, 4> playerList{};
                for (auto _enumerator32 = _scene.GetPlayerEntities().GetEnumerator(); _enumerator32.MoveNext(); )
            if (const auto player = _enumerator32.Current(); true)
                {
                    if (player->TeamIndex() != _player->TeamIndex() && player->SlotIndex() == maxCaptureSlotIndex)
                    {
                        playerList[static_cast<std::size_t>(playerCount++)] = player;
                    }
                }
                std::shared_ptr<PlayerEntity> chosenPlayer = playerList[static_cast<std::size_t>(Rng::GetRandomInt2(playerCount))];
                for (auto _enumerator33 = _scene.GetNodeDefenseEntities().GetEnumerator(); _enumerator33.MoveNext(); )
            if (const auto defense = _enumerator33.Current(); true)
                {
                    if (resultCount < 10 && defense->CapturedPlayer() == chosenPlayer)
                    {
                        resultList[static_cast<std::size_t>(resultCount++)] = defense;
                    }
                }
            }
            else
            {
                for (auto _enumerator34 = _scene.GetNodeDefenseEntities().GetEnumerator(); _enumerator34.MoveNext(); )
            if (const auto defense = _enumerator34.Current(); true)
                {
                    if (resultCount < 10 && !defense->CapturedPlayer())
                    {
                        resultList[static_cast<std::size_t>(resultCount++)] = defense;
                    }
                }
            }
            if (resultCount > 0)
            {
                return resultList[static_cast<std::size_t>(Rng::GetRandomInt2(resultCount))];
            }
            return firstResult;
        }

        std::shared_ptr<NodeDefenseEntity> FindClosestFriendlyNodeDefense()
        {
            std::shared_ptr<NodeDefenseEntity> result{};
            float minDist = std::numeric_limits<float>::max();
            for (auto _enumerator35 = _scene.GetNodeDefenseEntities().GetEnumerator(); _enumerator35.MoveNext(); )
            if (const auto defense = _enumerator35.Current(); true)
            {
                if (defense->CapturedPlayer() && defense->CapturedPlayer()->TeamIndex() == _player->TeamIndex() && defense->IsOccupied())
                {
                    float dist = DistanceSquared(defense->Position, _player->Position);
                    if (dist < minDist)
                    {
                        result = defense;
                        minDist = dist;
                    }
                }
            }
            return result;
        }

        std::shared_ptr<DoorEntity> FindClosestDoor(Vector3 position)
        {
            std::shared_ptr<DoorEntity> result{};
            float minDist = std::numeric_limits<float>::max();
            for (auto _enumerator36 = _scene.GetDoorEntities().GetEnumerator(); _enumerator36.MoveNext(); )
            if (const auto door = _enumerator36.Current(); true)
            {
                float dist = DistanceSquared(door->Position, position);
                if (dist < minDist)
                {
                    result = door;
                    minDist = dist;
                }
            }
            return result;
        }

        void UpdateNodeDataSetSelection()
        {
            for (std::size_t i = 0; i < _nodeData->SetIndices->size(); ++i)
            {
                const std::int32_t index = static_cast<std::int32_t>(i);
                const bool selected = NativeRuntime::ManagedBoolArrayGet(_nodeData->SetSelector, index);
                if (selected && (_nodeDataSelOff & (1 << i)) != 0)
                {
                    NativeRuntime::ManagedBoolArraySet(_nodeData->SetSelector, index, false);
                }
                else if (!selected && (_nodeDataSelOn & (1 << i)) != 0)
                {
                    NativeRuntime::ManagedBoolArraySet(_nodeData->SetSelector, index, true);
                }
            }
            std::int32_t newIndex = 0;
            for (std::size_t i = 0; i < _nodeData->SetIndices->size(); ++i)
            {
                newIndex += (NativeRuntime::ManagedBoolArrayGet(
                    _nodeData->SetSelector, static_cast<std::int32_t>(i)) ? 1 : 0) << i;
            }
            for (auto _enumerator37 = _scene.GetPlayerEntities().GetEnumerator(); _enumerator37.MoveNext(); )
            if (const auto player = _enumerator37.Current(); true)
            {
                if (player->IsBot() && player->AiData->_nodeDataSetIndex != newIndex)
                {
                    player->AiData->_nodeDataSetIndex = newIndex;
                    player->AiData->SetClosestNodeList(player->Position);
                }
            }
        }

        void SetClosestNodeList(Vector3 position)
        {
            Flags2 &= ~AiFlags2::Bit7;
            const auto& data1 = *(*_nodeData->Data)[static_cast<std::size_t>(_nodeDataSetIndex)];
            _nodeList = data1[0].get();
            if (data1.size() > 1)
            {
                float minDist = GetClosestNodeInList(position, *data1[0]);
                for (std::size_t i = 1; i < data1.size(); ++i)
                {
                    float dist = GetClosestNodeInList(position, *data1[i]);
                    if (dist < minDist)
                    {
                        _nodeList = data1[i].get();
                        minDist = dist;
                    }
                }
            }
            SetNodeTypeFirstIndices();
        }

        float GetClosestNodeInList(Vector3 position, const std::vector<std::shared_ptr<Formats::NodeData3>>& data2) const
        {
            float minDist = DistanceSquared(data2[0]->Position, position);
            for (std::size_t i = 1; i < data2.size(); ++i)
            {
                float dist = DistanceSquared(data2[i]->Position, position);
                if (dist < minDist)
                {
                    minDist = dist;
                }
            }
            return minDist;
        }

        void SetNodeTypeFirstIndices()
        {
            std::int32_t curType = 0;
            for (std::size_t i = 0; i < _nodeList->size(); ++i)
            {
                std::uint16_t type = static_cast<std::uint16_t>((*_nodeList)[i]->NodeType);
                while (curType < type)
                {
                    _nodeTypeIndex[static_cast<std::size_t>(curType)] = static_cast<std::int32_t>(i);
                    ++curType;
                }
            }
            for (std::int32_t i = curType; i < 6; ++i)
            {
                _nodeTypeIndex[static_cast<std::size_t>(i)] = static_cast<std::int32_t>(_nodeList->size());
            }
        }

        static void RemovePlayerFromGlobals(const std::shared_ptr<PlayerEntity>& player)
        {
            if (_globalField2 == 0)
            {
                return;
            }
            std::int32_t i = 0;
            for (; i < _globalField2; ++i)
            {
                if (_globalObjs[static_cast<std::size_t>(i)].Player == player)
                {
                    break;
                }
            }
            if (i == _globalField2)
            {
                return;
            }
            for (; i < _globalField2 - 1; ++i)
            {
                AiGlobals& current = _globalObjs[static_cast<std::size_t>(i)];
                AiGlobals& next = _globalObjs[static_cast<std::size_t>(i + 1)];
                current.Player = next.Player;
                current.Field4 = next.Field4;
                current.NodeDataIndex = next.NodeDataIndex;
                current.NodeData = next.NodeData;
            }
            --_globalField2;
        }

        class AiEntityRefs
        {
        public:
            std::shared_ptr<Formats::NodeData3> Field0{};
            std::shared_ptr<Formats::NodeData3> Field1{};
            std::shared_ptr<Formats::NodeData3> Field2{};
            std::shared_ptr<Formats::NodeData3> Field3{};
            std::shared_ptr<Formats::NodeData3> Field4{};
            std::shared_ptr<Formats::NodeData3> Field5{};
            std::shared_ptr<Formats::NodeData3> Field6{};
            std::shared_ptr<Formats::NodeData3> Field7{};
            std::shared_ptr<Formats::NodeData3> Field8{};
            std::shared_ptr<Formats::NodeData3> Field9{};
            std::shared_ptr<Formats::NodeData3> Field10{};
            std::shared_ptr<Formats::NodeData3> Field11{};
            std::shared_ptr<Formats::NodeData3> Field12{};
            std::shared_ptr<Formats::NodeData3> Field13{};
            std::shared_ptr<Formats::NodeData3> Field14{};
            std::shared_ptr<Formats::NodeData3> Field15{};
            std::shared_ptr<Formats::NodeData3> Field16{};
            std::shared_ptr<Formats::NodeData3> Field17{};
            std::shared_ptr<Formats::NodeData3> Field18{};
            std::shared_ptr<Formats::NodeData3> Field19{};
            std::shared_ptr<Formats::NodeData3> Field20{};
            std::shared_ptr<Formats::NodeData3> Field21{};
            std::shared_ptr<Formats::NodeData3> Field22{};
            std::shared_ptr<Formats::NodeData3> Field23{};
            std::shared_ptr<Formats::NodeData3> Field24{};
            std::shared_ptr<Formats::NodeData3> Field25{};
            std::shared_ptr<PlayerEntity> Field26{};
            std::shared_ptr<PlayerEntity> Field27{};
            std::shared_ptr<PlayerEntity> Field28{};
            std::shared_ptr<PlayerEntity> Field29{};
            std::shared_ptr<PlayerEntity> Field30{};
            std::shared_ptr<PlayerEntity> Field31{};
            std::shared_ptr<PlayerEntity> Field32{};
            std::shared_ptr<HalfturretEntity> Field33{};
            std::shared_ptr<ItemSpawnEntity> Field34{};
            std::shared_ptr<ItemSpawnEntity> Field35{};
            std::shared_ptr<ItemSpawnEntity> Field36{};
            std::shared_ptr<ItemSpawnEntity> Field37{};
            std::shared_ptr<ItemSpawnEntity> Field38{};
            std::shared_ptr<ItemSpawnEntity> Field39{};
            std::shared_ptr<ItemSpawnEntity> Field40{};
            std::shared_ptr<ItemSpawnEntity> Field41{};
            std::shared_ptr<ItemSpawnEntity> Field42{};
            std::shared_ptr<ItemSpawnEntity> Field43{};
            std::shared_ptr<ItemSpawnEntity> Field44{};
            std::shared_ptr<ItemSpawnEntity> Field45{};
            std::shared_ptr<ItemSpawnEntity> Field46{};
            std::shared_ptr<ItemSpawnEntity> Field47{};
            std::shared_ptr<ItemSpawnEntity> Field48{};
            std::shared_ptr<ItemSpawnEntity> Field49{};
            std::shared_ptr<ItemSpawnEntity> Field50{};
            std::shared_ptr<ItemSpawnEntity> Field51{};
            std::shared_ptr<ItemSpawnEntity> Field52{};
            std::shared_ptr<ItemSpawnEntity> Field53{};
            std::shared_ptr<ItemInstanceEntity> Field54{};
            std::shared_ptr<ItemInstanceEntity> Field55{};
            std::shared_ptr<ItemInstanceEntity> Field56{};
            std::shared_ptr<ItemInstanceEntity> Field57{};
            std::shared_ptr<ItemInstanceEntity> Field58{};
            std::shared_ptr<ItemInstanceEntity> Field59{};
            std::shared_ptr<ItemInstanceEntity> Field60{};
            std::shared_ptr<ItemInstanceEntity> Field61{};
            std::shared_ptr<ItemInstanceEntity> Field62{};
            std::shared_ptr<ItemInstanceEntity> Field63{};
            std::shared_ptr<ItemInstanceEntity> Field64{};
            std::shared_ptr<ItemInstanceEntity> Field65{};
            std::shared_ptr<ItemInstanceEntity> Field66{};
            std::shared_ptr<ItemInstanceEntity> Field67{};
            std::shared_ptr<ItemInstanceEntity> Field68{};
            std::shared_ptr<ItemInstanceEntity> Field69{};
            std::shared_ptr<ItemInstanceEntity> Field70{};
            std::shared_ptr<ItemInstanceEntity> Field71{};
            std::shared_ptr<ItemInstanceEntity> Field72{};
            std::shared_ptr<ItemInstanceEntity> Field73{};
            std::shared_ptr<NodeDefenseEntity> Field74{};
            std::shared_ptr<NodeDefenseEntity> Field75{};
            std::shared_ptr<NodeDefenseEntity> Field76{};
            std::shared_ptr<DoorEntity> Field77{};

            bool IsPopulated(std::int32_t index) const
            {
                switch (index)
                {
                case 0: return Field0 != nullptr;
                case 1: return Field1 != nullptr;
                case 2: return Field2 != nullptr;
                case 3: return Field3 != nullptr;
                case 4: return Field4 != nullptr;
                case 5: return Field5 != nullptr;
                case 6: return Field6 != nullptr;
                case 7: return Field7 != nullptr;
                case 8: return Field8 != nullptr;
                case 9: return Field9 != nullptr;
                case 10: return Field10 != nullptr;
                case 11: return Field11 != nullptr;
                case 12: return Field12 != nullptr;
                case 13: return Field13 != nullptr;
                case 14: return Field14 != nullptr;
                case 15: return Field15 != nullptr;
                case 16: return Field16 != nullptr;
                case 17: return Field17 != nullptr;
                case 18: return Field18 != nullptr;
                case 19: return Field19 != nullptr;
                case 20: return Field20 != nullptr;
                case 21: return Field21 != nullptr;
                case 22: return Field22 != nullptr;
                case 23: return Field23 != nullptr;
                case 24: return Field24 != nullptr;
                case 25: return Field25 != nullptr;
                case 26: return Field26 != nullptr;
                case 27: return Field27 != nullptr;
                case 28: return Field28 != nullptr;
                case 29: return Field29 != nullptr;
                case 30: return Field30 != nullptr;
                case 31: return Field31 != nullptr;
                case 32: return Field32 != nullptr;
                case 33: return Field33 != nullptr;
                case 34: return Field34 != nullptr;
                case 35: return Field35 != nullptr;
                case 36: return Field36 != nullptr;
                case 37: return Field37 != nullptr;
                case 38: return Field38 != nullptr;
                case 39: return Field39 != nullptr;
                case 40: return Field40 != nullptr;
                case 41: return Field41 != nullptr;
                case 42: return Field42 != nullptr;
                case 43: return Field43 != nullptr;
                case 44: return Field44 != nullptr;
                case 45: return Field45 != nullptr;
                case 46: return Field46 != nullptr;
                case 47: return Field47 != nullptr;
                case 48: return Field48 != nullptr;
                case 49: return Field49 != nullptr;
                case 50: return Field50 != nullptr;
                case 52: return Field52 != nullptr;
                case 53: return Field53 != nullptr;
                case 54: return Field54 != nullptr;
                case 55: return Field55 != nullptr;
                case 56: return Field56 != nullptr;
                case 57: return Field57 != nullptr;
                case 58: return Field58 != nullptr;
                case 59: return Field59 != nullptr;
                case 60: return Field60 != nullptr;
                case 61: return Field61 != nullptr;
                case 62: return Field62 != nullptr;
                case 63: return Field63 != nullptr;
                case 64: return Field64 != nullptr;
                case 65: return Field65 != nullptr;
                case 66: return Field66 != nullptr;
                case 67: return Field67 != nullptr;
                case 68: return Field68 != nullptr;
                case 69: return Field69 != nullptr;
                case 70: return Field70 != nullptr;
                case 71: return Field71 != nullptr;
                case 72: return Field72 != nullptr;
                case 73: return Field73 != nullptr;
                case 74: return Field74 != nullptr;
                case 75: return Field75 != nullptr;
                case 76: return Field76 != nullptr;
                case 77: return Field77 != nullptr;
                default: throw std::runtime_error("Invalid AI entity index.");
                }
            }

            void Clear()
            {
                Field0.reset();
                Field1.reset();
                Field2.reset();
                Field3.reset();
                Field4.reset();
                Field5.reset();
                Field6.reset();
                Field7.reset();
                Field8.reset();
                Field9.reset();
                Field10.reset();
                Field11.reset();
                Field12.reset();
                Field13.reset();
                Field14.reset();
                Field15.reset();
                Field16.reset();
                Field17.reset();
                Field18.reset();
                Field19.reset();
                Field20.reset();
                Field21.reset();
                Field22.reset();
                Field23.reset();
                Field24.reset();
                Field25.reset();
                Field26.reset();
                Field27.reset();
                Field28.reset();
                Field29.reset();
                Field30.reset();
                Field31.reset();
                Field32.reset();
                Field33.reset();
                Field34.reset();
                Field35.reset();
                Field36.reset();
                Field37.reset();
                Field38.reset();
                Field39.reset();
                Field40.reset();
                Field41.reset();
                Field42.reset();
                Field43.reset();
                Field44.reset();
                Field45.reset();
                Field46.reset();
                Field47.reset();
                Field48.reset();
                Field49.reset();
                Field50.reset();
                Field52.reset();
                Field53.reset();
                Field54.reset();
                Field55.reset();
                Field56.reset();
                Field57.reset();
                Field58.reset();
                Field59.reset();
                Field60.reset();
                Field61.reset();
                Field62.reset();
                Field63.reset();
                Field64.reset();
                Field65.reset();
                Field66.reset();
                Field67.reset();
                Field68.reset();
                Field69.reset();
                Field70.reset();
                Field71.reset();
                Field72.reset();
                Field73.reset();
                Field74.reset();
                Field75.reset();
                Field76.reset();
                Field77.reset();
            }
        };

        AiQueuedEnt _queuedFindEntityAction = AiQueuedEnt::None;
        AiEntityRefs _entityRefs{};
    };
}

#endif
