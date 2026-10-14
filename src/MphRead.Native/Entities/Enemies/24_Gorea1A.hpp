#pragma once

#include "../../Formats/Culling.hpp"
#include "../../Formats/Effects.hpp"
#include "../../Metadata/SoundMeta.hpp"
#include "../EnemyInstanceEntity.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace MphRead
{
    class Material;
    class ModelInstance;
    class Node;
}

namespace MphRead::Entities
{
    class EnemySpawnEntity;
}

namespace MphRead::Entities::Enemies
{
    class Enemy25Entity;
    class Enemy26Entity;
    class Enemy27Entity;
    class Enemy28Entity;

    enum class Gorea1AFlags : std::uint8_t
    {
        None = 0x0,
        Bit0 = 0x1,
        Bit1 = 0x2,
        Bit2 = 0x4,
        Bit3 = 0x8,
        Bit4 = 0x10,
        Unused5 = 0x20,
        Unused6 = 0x40,
        Unused7 = 0x80
    };

    [[nodiscard]] constexpr Gorea1AFlags operator|(Gorea1AFlags left, Gorea1AFlags right) noexcept
    {
        return static_cast<Gorea1AFlags>(
            static_cast<std::uint8_t>(left) | static_cast<std::uint8_t>(right));
    }

    [[nodiscard]] constexpr Gorea1AFlags operator&(Gorea1AFlags left, Gorea1AFlags right) noexcept
    {
        return static_cast<Gorea1AFlags>(
            static_cast<std::uint8_t>(left) & static_cast<std::uint8_t>(right));
    }

    [[nodiscard]] constexpr Gorea1AFlags operator^(Gorea1AFlags left, Gorea1AFlags right) noexcept
    {
        return static_cast<Gorea1AFlags>(
            static_cast<std::uint8_t>(left) ^ static_cast<std::uint8_t>(right));
    }

    [[nodiscard]] constexpr Gorea1AFlags operator~(Gorea1AFlags value) noexcept
    {
        return static_cast<Gorea1AFlags>(
            static_cast<std::uint8_t>(~static_cast<std::uint8_t>(value)));
    }

    constexpr Gorea1AFlags &operator|=(Gorea1AFlags &left, Gorea1AFlags right) noexcept
    {
        left = left | right;
        return left;
    }

    constexpr Gorea1AFlags &operator&=(Gorea1AFlags &left, Gorea1AFlags right) noexcept
    {
        left = left & right;
        return left;
    }

    constexpr Gorea1AFlags &operator^=(Gorea1AFlags &left, Gorea1AFlags right) noexcept
    {
        left = left ^ right;
        return left;
    }

    class GoreaEnemyEntityBase : public EnemyInstanceEntity
    {
    public:
        GoreaEnemyEntityBase(EnemyInstanceEntityData data,
                             Formats::Culling::NodeRef nodeRef, Scene *scene);

        GoreaEnemyEntityBase(const GoreaEnemyEntityBase &) = delete;
        GoreaEnemyEntityBase &operator=(const GoreaEnemyEntityBase &) = delete;
        GoreaEnemyEntityBase(GoreaEnemyEntityBase &&) = delete;
        GoreaEnemyEntityBase &operator=(GoreaEnemyEntityBase &&) = delete;

        void InitializeCommon(EnemySpawnEntity *spawner);

    protected:
        static constexpr SetFlags _animSetNoMat = SetFlags::Texture | SetFlags::Texcoord | SetFlags::Unused | SetFlags::Node;

        ModelInstance *_model = nullptr;
        std::optional<std::uint64_t> _lastNodeTransformUpdate{};
        bool _lightOverride = false;

        [[nodiscard]] bool AnimationEnded() const;
        void SpawnEffect(std::int32_t effectId, OpenTK::Mathematics::Vector3 position);
        void SpawnEffect(std::int32_t effectId, OpenTK::Mathematics::Vector3 position,
                         OpenTK::Mathematics::Vector3 facing, OpenTK::Mathematics::Vector3 up);
        [[nodiscard]] std::shared_ptr<Effects::EffectEntry> SpawnEffectGetEntry(
            std::int32_t effectId, OpenTK::Mathematics::Vector3 position, bool extensionFlag);
        [[nodiscard]] std::shared_ptr<Effects::EffectEntry> SpawnEffectGetEntry(
            std::int32_t effectId, OpenTK::Mathematics::Vector3 position,
            OpenTK::Mathematics::Vector3 facing, OpenTK::Mathematics::Vector3 up,
            bool extensionFlag);
        [[nodiscard]] OpenTK::Mathematics::Matrix4 GetNodeTransform(
            GoreaEnemyEntityBase *entity, Node *node);
        void TransformHurtVolumeToNode(Node *node, OpenTK::Mathematics::Vector3 offset);
        [[nodiscard]] bool SeekTargetFacing(OpenTK::Mathematics::Vector3 target, float angle);
        [[nodiscard]] OpenTK::Mathematics::Vector3 SeekTargetSetAnim(
            OpenTK::Mathematics::Vector3 target, std::int32_t index);
        [[nodiscard]] OpenTK::Mathematics::Vector3 SeekTargetSetAnim(
            OpenTK::Mathematics::Vector3 target, std::int32_t index,
            std::int32_t slot, SetFlags setFlags);
        [[nodiscard]] bool CheckFacingAngle(float minCos, OpenTK::Mathematics::Vector3 position);
        void EnsureAnimation(std::int32_t index, AnimFlags animFlags = AnimFlags::None);
        void EnsureAnimation(std::int32_t index, std::int32_t slot,
                             SetFlags setFlags, AnimFlags animFlags = AnimFlags::None);
        [[nodiscard]] bool IsAtEndFrame() const;
        void IncrementMaterialColors(Material *material, ColorRgb ambient, ColorRgb diffuse,
                                     std::int32_t frame, std::int32_t frameCount);
        [[nodiscard]] std::uint8_t InterpolateColor(
            std::int32_t frame, std::int32_t frameCount, std::int32_t color) const;
        [[nodiscard]] LightInfo GetLightInfo() override;

    private:
        [[nodiscard]] OpenTK::Mathematics::Vector3 SeekTargetSetAnim(
            OpenTK::Mathematics::Vector3 target, std::int32_t index,
            std::int32_t slot, SetFlags setFlags, bool useSlot);
    };

    class Enemy24Entity : public GoreaEnemyEntityBase
    {
    public:
        Enemy24Entity(EnemyInstanceEntityData data,
                      Formats::Culling::NodeRef nodeRef, Scene *scene);

        Enemy24Entity(const Enemy24Entity &) = delete;
        Enemy24Entity &operator=(const Enemy24Entity &) = delete;
        Enemy24Entity(Enemy24Entity &&) = delete;
        Enemy24Entity &operator=(Enemy24Entity &&) = delete;

        [[nodiscard]] EnemySpawnEntity *Spawner() const noexcept;
        [[nodiscard]] const std::vector<ColorRgb> *Colors() const noexcept;
        [[nodiscard]] std::int32_t WeaponIndex() const noexcept;
        // Mods: a check moves Gorea on to its next weapon the way its own
        // phase change does (Mods::Diagnostics::HomingSpawnCheck).
        void ModChangeWeapon() { ChangeWeapon(); }

        void Activate();

        [[nodiscard]] static bool Behavior00(Enemy24Entity *enemy);
        [[nodiscard]] static bool Behavior01(Enemy24Entity *enemy);
        [[nodiscard]] static bool Behavior02(Enemy24Entity *enemy);
        [[nodiscard]] static bool Behavior03(Enemy24Entity *enemy);
        [[nodiscard]] static bool Behavior04(Enemy24Entity *enemy);
        [[nodiscard]] static bool Behavior05(Enemy24Entity *enemy);
        [[nodiscard]] static bool Behavior06(Enemy24Entity *enemy);
        [[nodiscard]] static bool Behavior07(Enemy24Entity *enemy);
        [[nodiscard]] static bool Behavior08(Enemy24Entity *enemy);
        [[nodiscard]] static bool Behavior09(Enemy24Entity *enemy);
        [[nodiscard]] static bool Behavior10(Enemy24Entity *enemy);
        [[nodiscard]] static bool Behavior11(Enemy24Entity *enemy);
        [[nodiscard]] static bool Behavior12(Enemy24Entity *enemy);
        [[nodiscard]] static bool Behavior13(Enemy24Entity *enemy);
        [[nodiscard]] static bool Behavior14(Enemy24Entity *enemy);
        [[nodiscard]] static bool Behavior15(Enemy24Entity *enemy);
        [[nodiscard]] static bool Behavior16(Enemy24Entity *enemy);
        [[nodiscard]] static bool Behavior17(Enemy24Entity *enemy);
        [[nodiscard]] static bool Behavior18(Enemy24Entity *enemy);
        [[nodiscard]] static bool Behavior19(Enemy24Entity *enemy);
        [[nodiscard]] static bool Behavior20(Enemy24Entity *enemy);
        [[nodiscard]] static bool Behavior21(Enemy24Entity *enemy);
        [[nodiscard]] static bool Behavior22(Enemy24Entity *enemy);

    protected:
        void EnemyInitialize() override;
        void EnemyProcess() override;
        [[nodiscard]] bool BaseProcess() override;
        [[nodiscard]] bool EnemyTakeDamage(EntityBase *source) override;
        [[nodiscard]] bool EnemyGetDrawInfo() override;

    private:
        static const std::array<std::int32_t, 6> _chargeEffects;
        static const std::array<std::int32_t, 6> _shotEffects;
        static const std::array<BeamType, 6> _beamTypes;
        static const std::array<std::int32_t, 6> _weaponAnimIds;
        static const std::array<std::int32_t, 6> _chargeChances;
        static const std::array<float, 3> _speedFactors;

        EnemySpawnEntity *const _spawner;
        Gorea1AFlags _goreaFlags = Gorea1AFlags::None;
        ModelInstance *_regenModel = nullptr;
        std::shared_ptr<Node> _spineNode{};
        CollisionVolume _volume{};
        std::shared_ptr<Enemy28Entity> _gorea1B{};
        std::shared_ptr<Enemy25Entity> _head{};
        std::int32_t _armBits = 0;
        std::array<std::shared_ptr<Enemy26Entity>, 2> _arms{};
        std::array<std::shared_ptr<Enemy27Entity>, 3> _legs{};
        const std::vector<ColorRgb> *_colors = nullptr;
        std::int32_t _weaponIndex = 5;
        float _speedFactor = 0.0F;
        OpenTK::Mathematics::Vector3 _targetFacing{};
        std::int32_t _field23C = 0;
        std::int32_t _field23E = 0;
        std::int32_t _field240 = 0;
        std::int32_t _field244 = 0;
        std::uint8_t _nextState = 0;
        std::int32_t _field242 = 0;

        const std::array<std::string_view, 12> _armMatNames{
            "L_Bisep", "L_GunArm", "L_GunTipBottom", "L_GunTipSide", "L_GunTipTop", "L_ShoulderTarget",
            "R_Bisep", "R_GunArm", "R_GunTipBottom", "R_GunTipSide", "R_GunTipTop", "R_ShoulderTarget"};
        const std::array<std::string_view, 10> _bodyMatNames1{
            "ChestMembrane", "Eye", "Head1", "L_GunArm", "L_Bisep",
            "R_GunArm", "R_Bisep", "Legs", "Torso", "Shoulder"};
        const std::array<std::string_view, 2> _bodyMatNames2{
            "ChestCore", "HeadFullLit"};
        const std::array<MusicId, 6> _musicTracks{
            MusicId::SEQ_GOREA_1_M22,
            MusicId::SEQ_GOREA_1_M26,
            MusicId::SEQ_GOREA_1_M27,
            MusicId::SEQ_GOREA_1_M24,
            MusicId::SEQ_GOREA_1_M23,
            MusicId::SEQ_GOREA_1_M25};

        void SpawnHead();
        void SpawnArms();
        void SpawnLegs();
        void SpawnGorea1B();
        void ChangeWeapon();
        void IncrementAllMaterialColors();
        void CheckPlayerCollision();
        void UpdateArmMaterialAlpha();
        void SetArmMaterialAlpha(std::int32_t index, std::uint8_t alpha);
        [[nodiscard]] bool UpdateTargetFacing();
        [[nodiscard]] bool CheckTargeting(Enemy26Entity *arm);
        void CreateShotEffectLoose(Enemy26Entity *arm, std::int32_t effectId);
        void GetArmAim(Enemy26Entity *arm,
                       OpenTK::Mathematics::Vector3 &position, OpenTK::Mathematics::Vector3 &direction);
        [[nodiscard]] std::int32_t GetBeamChargeSfx(BeamType beam) const;
        void PlayBeamChargeSfx(BeamType beam);
        void StopBeamChargeSfx(BeamType beam);
        void PlayBeamShotSfx(BeamType beam, bool charged);
        [[nodiscard]] bool GetHorizontalToPlayer(float maxDistance,
                                                 OpenTK::Mathematics::Vector3 &between, float &distance) const;
        void StopShots(std::int32_t index, bool detach);
        [[nodiscard]] bool CheckOffsetOutsideVolume(OpenTK::Mathematics::Vector3 offset) const;
        void StopAndSetUp();
        void StartShots();
        void CreateChargeEffect(std::int32_t index);
        void SetShotAnimation();
        void SetSwingAnimation(OpenTK::Mathematics::Vector3 between, float distance);
        [[nodiscard]] bool TrySprintingRoomInVolume();
        void RegenerateArms();
        void UpdateSpeed();
        void DrawArmRegen();
        void UpdateArmMaterials();
        void ResetMaterialColors();

        void State00();
        void State01();
        void State02();
        void State03();
        void State04();
        void State05();
        void State06();
        void State07();
        void State08();
        void State09();
        void State10();
        void State11();
        void State12();
        void State13();
        void State14();

        [[nodiscard]] bool Behavior00();
        [[nodiscard]] bool Behavior01();
        [[nodiscard]] bool Behavior02();
        [[nodiscard]] bool Behavior03();
        [[nodiscard]] bool Behavior04();
        [[nodiscard]] bool Behavior05();
        [[nodiscard]] bool Behavior06();
        [[nodiscard]] bool Behavior07();
        [[nodiscard]] bool Behavior08();
        [[nodiscard]] bool Behavior09();
        [[nodiscard]] bool Behavior10();
        [[nodiscard]] bool Behavior11();
        [[nodiscard]] bool Behavior12();
        [[nodiscard]] bool Behavior13();
        [[nodiscard]] bool Behavior14();
        [[nodiscard]] bool Behavior15();
        [[nodiscard]] bool Behavior16();
        [[nodiscard]] bool Behavior17();
        [[nodiscard]] bool Behavior18();
        [[nodiscard]] bool Behavior19();
        [[nodiscard]] bool Behavior20();
        [[nodiscard]] bool Behavior21();
        [[nodiscard]] bool Behavior22();
    };
}
