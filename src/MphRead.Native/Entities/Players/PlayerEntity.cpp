#include "PlayerEntity.hpp"

#include "../../Mods/Input/AimAssist/AimAssistTelemetry.hpp"
#include "../../NativeRuntime/System/Enum.hpp"

#include "HalfturretEntity.hpp"
#include "../ArtifactEntity.hpp"
#include "../BeamProjectileEntity.hpp"
#include "../BombEntity.hpp"
#include "../DoorEntity.hpp"
#include "../EnemySpawnEntity.hpp"
#include "../ItemSpawnEntity.hpp"
#include "../JumpPadEntity.hpp"
#include "../MorphCameraEntity.hpp"
#include "../RoomEntity.hpp"
#include "../CamSeq/CameraSequence.hpp"
#include "../OctolithFlagEntity.hpp"
#include "../../Features.hpp"
#include "../../GameState.hpp"
#include "../../Read.hpp"
#include "../../Scene.hpp"
#include "../../Mods/DebugLog.hpp"
#include "../../SceneSetup.hpp"
#include "../../Strings.hpp"
#include "../../Metadata/Enemies.hpp"
#include "../../Metadata/Metadata.hpp"
#include "../../Metadata/Player.hpp"
#include "../../Metadata/Weapons.hpp"
#include "../../Mods/Network/NetDamage.hpp"
#include "../../Mods/Network/NetHitPrediction.hpp"
#include "../../Mods/Network/NetLog.hpp"
#include "../../Mods/Network/NetPlayerBridge.hpp"
#include "../../Mods/Network/NetPlayerLifecycle.hpp"
#include "../../Mods/Network/NetSession.hpp"
#include "../../Mods/Multiplayer/TeamLayout.hpp"
#include "../../Mods/RespawnChoice.hpp"
#include "../../Sound/Music.hpp"
#include "../../NativeRuntime/System/Managed.hpp"
#include "../../NativeRuntime/OpenTK/Mathematics.hpp"
#include "../../Formats/Types.hpp"

#include <algorithm>
#include <any>
#include <cassert>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

using ::MphRead::NativeRuntime::ManagedAt;
using ::MphRead::NativeRuntime::RequireReference;
using ::MphRead::NativeRuntime::UncheckedAdd;
using ::MphRead::NativeRuntime::UncheckedSubtract;
using ::MphRead::TestAny;
using ::MphRead::TestFlag;
using ::OpenTK::Mathematics::AddY;
using ::OpenTK::Mathematics::IdentityMatrix;
using ::OpenTK::Mathematics::LengthSquared;
using ::OpenTK::Mathematics::Negate;
using ::OpenTK::Mathematics::WithY;

namespace
{
    using MphRead::CollisionVolume;
    using MphRead::Fixed;
    using MphRead::ManagedArray;
    using MphRead::ModelInstance;
    using MphRead::Node;
    using MphRead::Scene;
    using MphRead::Entities::PlayerEntity;
    using MphRead::Hud::Align;
    using MphRead::Hud::HudElements;
    using MphRead::Text::Strings;
    using MphRead::Text::StringTables;
    using OpenTK::Mathematics::Matrix4;
    using OpenTK::Mathematics::Vector3;
    using OpenTK::Mathematics::Vector4;

    [[nodiscard]] MphRead::MessageObject BoxEntity(MphRead::Entities::EntityBase* value)
    {
        return std::make_shared<const std::any>(value);
    }

    [[nodiscard]] bool VectorEquals(Vector3 left, Vector3 right) noexcept
    {
        return left.X == right.X && left.Y == right.Y && left.Z == right.Z;
    }

    [[nodiscard]] std::size_t CheckedArrayIndex(std::int32_t index, std::size_t length)
    {
        if (index < 0 || static_cast<std::size_t>(index) >= length)
        {
            throw MphRead::SceneDetail::IndexOutOfRangeException();
        }
        return static_cast<std::size_t>(index);
    }

    template <typename T>
    [[nodiscard]] const T& ManagedReadOnlyListAt(const std::vector<T>& values, std::int32_t index)
    {
        if (index < 0 || static_cast<std::size_t>(index) >= values.size())
        {
            throw System::ArgumentOutOfRangeException();
        }
        return values[static_cast<std::size_t>(index)];
    }

    template <typename T, std::size_t N>
    [[nodiscard]] const T& ManagedReadOnlyListAt(const std::array<T, N>& values, std::int32_t index)
    {
        if (index < 0 || static_cast<std::size_t>(index) >= N)
        {
            throw System::ArgumentOutOfRangeException();
        }
        return values[static_cast<std::size_t>(index)];
    }

    [[nodiscard]] std::shared_ptr<MphRead::WeaponInfo> CurrentWeaponPtrAt(std::int32_t index)
    {
        const auto& current = RequireReference(MphRead::Weapons::Current);
        return ManagedReadOnlyListAt(current, index);
    }

    [[nodiscard]] MphRead::WeaponInfo& CurrentWeaponAt(std::int32_t index)
    {
        return RequireReference(CurrentWeaponPtrAt(index));
    }

    [[nodiscard]] constexpr bool ManagedInt32LessThanOrEqualUInt32(
        std::int32_t left, std::uint32_t right) noexcept
    {
        return static_cast<std::int64_t>(left) <= static_cast<std::int64_t>(right);
    }

    [[nodiscard]] constexpr std::uint32_t ManagedShiftLeftInt32(
        std::int32_t value, std::int32_t count) noexcept
    {
        return std::bit_cast<std::uint32_t>(value)
            << (static_cast<std::uint32_t>(count) & 0x1FU);
    }

    [[nodiscard]] float Distance(Vector3 left, Vector3 right) noexcept
    {
        return std::sqrt(LengthSquared(left - right));
    }

    [[nodiscard]] std::string ReplaceToken(std::string value, const std::string& token,
        const std::string& replacement)
    {
        std::size_t position = 0;
        while ((position = value.find(token, position)) != std::string::npos)
        {
            value.replace(position, token.size(), replacement);
            position += replacement.size();
        }
        return value;
    }
}

namespace MphRead::Entities
{
    void AvailableArray::ClearAll() noexcept
    {
        for (std::size_t i = 0; i < _array.size(); ++i)
        {
            _array[i] = false;
        }
    }

    void AvailableArray::SetAll() noexcept
    {
        for (std::size_t i = 0; i < _array.size(); ++i)
        {
            _array[i] = true;
        }
    }

    void AvailableArray::Set(std::uint16_t value) noexcept
    {
        for (std::int32_t i = 0; i < 8; ++i)
        {
            _array[static_cast<std::size_t>(i)] = (value & (1 << i)) != 0;
        }
    }

    void AvailableArray::CopyFrom(const AvailableArray& source) noexcept
    {
        for (std::size_t i = 0; i < _array.size(); ++i)
        {
            _array[i] = source._array[i];
        }
    }

    bool& AvailableArray::operator[](std::int32_t key)
    {
        return _array[CheckedArrayIndex(key, _array.size())];
    }

    const bool& AvailableArray::operator[](std::int32_t key) const
    {
        return _array[CheckedArrayIndex(key, _array.size())];
    }

    bool& AvailableArray::operator[](MphRead::BeamType key)
    {
        return operator[](static_cast<std::int32_t>(key));
    }

    const bool& AvailableArray::operator[](MphRead::BeamType key) const
    {
        return operator[](static_cast<std::int32_t>(key));
    }

    void AvailableArray::Set(std::int32_t key, bool value)
    {
        operator[](key) = value;
    }

    void AvailableArray::Set(MphRead::BeamType key, bool value)
    {
        operator[](key) = value;
    }

    PlayerValues::PlayerValues(MphRead::Hunter hunter, std::int32_t walkBipedTraction,
        std::int32_t strafeBipedTraction, std::int32_t walkSpeedCap,
        std::int32_t strafeSpeedCap, std::int32_t altMinHSpeed,
        std::int32_t boostSpeedCap, std::int32_t bipedGravity,
        std::int32_t altAirGravity, std::int32_t altGroundGravity,
        std::int32_t jumpSpeed, std::int32_t walkSpeedFactor,
        std::int32_t altGroundSpeedFactor, std::int32_t strafeSpeedFactor,
        std::int32_t airSpeedFactor, std::int32_t standSpeedFactor,
        std::int32_t rollAltTraction, std::int32_t altColRadius,
        std::int32_t altColYPos, std::uint16_t boostChargeMin,
        std::uint16_t boostChargeMax, std::int32_t boostSpeedMin,
        std::int32_t boostSpeedMax, std::int32_t altHSpeedCapIncrement,
        std::int32_t field58, std::int32_t field5C, std::int32_t walkBobMax,
        std::int32_t aimDistance, std::uint16_t camSwitchTime,
        std::uint16_t padding6A, std::int32_t normalFov,
        std::int32_t zoomSensitivityFactor, std::int32_t aimYOffset,
        std::int32_t field78, std::int32_t field7C, std::int32_t field80,
        std::int32_t field84, std::int32_t field88, std::int32_t field8C,
        std::int32_t field90, std::int32_t minPickupHeight,
        std::int32_t maxPickupHeight, std::int32_t bipedColRadius,
        std::int32_t lockOnTolerance, std::int32_t lockOnMinDistance,
        std::int32_t lockOnMaxDistance, std::int16_t damageInvuln,
        std::uint16_t damageFlashTime, std::int32_t fieldB0,
        std::int32_t fieldB4, std::int32_t fieldB8, std::int32_t muzzleOffset,
        std::int32_t bombCooldown, std::int32_t bombSelfRadius,
        std::int32_t bombSelfRadiusSquared, std::int32_t bombRadius,
        std::int32_t bombRadiusSquared, std::int32_t bombJumpSpeed,
        std::int32_t bombRefillTime, std::int16_t bombDamage,
        std::int16_t bombEnemyDamage, std::int16_t lockOnSnapTime,
        std::int16_t spawnInvulnerability, std::uint16_t aimMinTouchTime,
        std::uint16_t paddingE6, std::int32_t autoAimFindTolerance,
        std::int32_t autoAimHoldTolerance, std::int32_t swayStartTime,
        std::int32_t swayIncrement, std::int32_t swayLimit,
        std::int32_t gunIdleTime, std::int16_t mpAmmoCap,
        std::uint8_t ammoRecharge, std::uint8_t padding103,
        std::uint16_t energyTank, std::int16_t ammoTank,
        std::uint8_t altFormStrafe, std::uint8_t padding109,
        std::uint16_t padding10A, std::int32_t fallDamageSpeed,
        std::int32_t fallDamageMax, std::int32_t viewTiltIncrement,
        std::int32_t viewTiltFactor, std::int32_t jumpPadSlideFactor,
        std::int32_t altTiltAngleCap, std::int32_t altMinWobble,
        std::int32_t altMaxWobble, std::int32_t altMinSpinAccel,
        std::int32_t altMaxSpinAccel, std::int32_t altMinSpinSpeed,
        std::int32_t altMaxSpinSpeed, std::int32_t altTiltAngleMax,
        std::int32_t altBounceWobble, std::int32_t altBounceTilt,
        std::int32_t altBounceSpin, std::int32_t altAttackKnockbackAccel,
        std::int16_t altAttackKnockbackTime, std::uint16_t altAttackStartup,
        std::int32_t field154, std::int32_t field158, std::int32_t lungeHSpeed,
        std::int32_t lungeVSpeed, std::uint16_t altAttackDamage,
        std::int16_t altAttackCooldown) noexcept
        : Hunter(hunter), WalkBipedTraction(walkBipedTraction),
          StrafeBipedTraction(strafeBipedTraction), WalkSpeedCap(walkSpeedCap),
          StrafeSpeedCap(strafeSpeedCap), AltMinHSpeed(altMinHSpeed),
          BoostSpeedCap(boostSpeedCap), BipedGravity(bipedGravity),
          AltAirGravity(altAirGravity), AltGroundGravity(altGroundGravity),
          JumpSpeed(jumpSpeed), WalkSpeedFactor(walkSpeedFactor),
          AltGroundSpeedFactor(altGroundSpeedFactor), StrafeSpeedFactor(strafeSpeedFactor),
          AirSpeedFactor(airSpeedFactor), StandSpeedFactor(standSpeedFactor),
          RollAltTraction(rollAltTraction), AltColRadius(altColRadius),
          AltColYPos(altColYPos), BoostChargeMin(boostChargeMin),
          BoostChargeMax(boostChargeMax), BoostSpeedMin(boostSpeedMin),
          BoostSpeedMax(boostSpeedMax), AltHSpeedCapIncrement(altHSpeedCapIncrement),
          Field58(field58), Field5C(field5C), WalkBobMax(walkBobMax),
          AimDistance(aimDistance), CamSwitchTime(camSwitchTime), Padding6A(padding6A),
          NormalFov(normalFov), ZoomSensitivityFactor(zoomSensitivityFactor),
          AimYOffset(aimYOffset), Field78(field78), Field7C(field7C),
          Field80(field80), Field84(field84), Field88(field88), Field8C(field8C),
          Field90(field90), MinPickupHeight(minPickupHeight), MaxPickupHeight(maxPickupHeight),
          BipedColRadius(bipedColRadius), LockOnTolerance(lockOnTolerance),
          LockOnMinDistance(lockOnMinDistance), LockOnMaxDistance(lockOnMaxDistance),
          DamageInvuln(damageInvuln), DamageFlashTime(damageFlashTime), FieldB0(fieldB0),
          FieldB4(fieldB4), FieldB8(fieldB8), MuzzleOffset(muzzleOffset),
          BombCooldown(bombCooldown), BombSelfRadius(bombSelfRadius),
          BombSelfRadiusSquared(bombSelfRadiusSquared), BombRadius(bombRadius),
          BombRadiusSquared(bombRadiusSquared), BombJumpSpeed(bombJumpSpeed),
          BombRefillTime(bombRefillTime), BombDamage(bombDamage),
          BombEnemyDamage(bombEnemyDamage), LockOnSnapTime(lockOnSnapTime),
          SpawnInvulnerability(spawnInvulnerability), AimMinTouchTime(aimMinTouchTime),
          PaddingE6(paddingE6), AutoAimFindTolerance(autoAimFindTolerance),
          AutoAimHoldTolerance(autoAimHoldTolerance), SwayStartTime(swayStartTime),
          SwayIncrement(swayIncrement), SwayLimit(swayLimit), GunIdleTime(gunIdleTime),
          MpAmmoCap(mpAmmoCap), AmmoRecharge(ammoRecharge), Padding103(padding103),
          EnergyTank(energyTank), AmmoTank(ammoTank), AltFormStrafe(altFormStrafe),
          Padding109(padding109), Padding10A(padding10A), FallDamageSpeed(fallDamageSpeed),
          FallDamageMax(fallDamageMax), ViewTiltIncrement(viewTiltIncrement),
          ViewTiltFactor(viewTiltFactor), JumpPadSlideFactor(jumpPadSlideFactor),
          AltTiltAngleCap(altTiltAngleCap), AltMinWobble(altMinWobble),
          AltMaxWobble(altMaxWobble), AltMinSpinAccel(altMinSpinAccel),
          AltMaxSpinAccel(altMaxSpinAccel), AltMinSpinSpeed(altMinSpinSpeed),
          AltMaxSpinSpeed(altMaxSpinSpeed), AltTiltAngleMax(altTiltAngleMax),
          AltBounceWobble(altBounceWobble), AltBounceTilt(altBounceTilt),
          AltBounceSpin(altBounceSpin), AltAttackKnockbackAccel(altAttackKnockbackAccel),
          AltAttackKnockbackTime(altAttackKnockbackTime), AltAttackStartup(altAttackStartup),
          Field154(field154), Field158(field158), LungeHSpeed(lungeHSpeed),
          LungeVSpeed(lungeVSpeed), AltAttackDamage(altAttackDamage),
          AltAttackCooldown(altAttackCooldown)
    {
    }

    PlayerEntity::PlayerEntity(std::int32_t slotIndex, Scene* scene)
        : DynamicLightEntityBase(MphRead::EntityType::Player, scene),
          _equipInfo(std::make_shared<MphRead::EquipInfo>())
    {
        _slotIndex = slotIndex;
        _beams = SceneSetup::CreateBeamList(16, scene);
        // A non-owning shared reference preserves the C# back-reference identity during construction;
        // AiData is a subobject lifetime-wise and does not extend PlayerEntity lifetime.
        std::shared_ptr<PlayerEntity> self(this, [](PlayerEntity*) noexcept {});
        AiData = std::make_shared<PlayerAiData>(self);
    }

    std::shared_ptr<PlayerEntity> PlayerEntity::Main()
    {
        return ManagedAt(_players, _mainPlayerIndex);
    }

    bool PlayerEntity::IsMainPlayer() const
    {
        return this == Main().get() && RequireReference(_scene).CameraMode() == MphRead::CameraMode::Player;
    }

    bool PlayerEntity::IsPrimeHunter() const
    {
        return _slotIndex == GameState::PrimeHunter();
    }

    bool PlayerEntity::IsAltForm() const noexcept
    {
        return TestFlag(_flags1, PlayerFlags1::AltForm);
    }

    bool PlayerEntity::IsMorphing() const noexcept
    {
        return TestFlag(_flags1, PlayerFlags1::Morphing);
    }

    bool PlayerEntity::IsUnmorphing() const noexcept
    {
        return TestFlag(_flags1, PlayerFlags1::Unmorphing);
    }

    MphRead::WeaponInfo& PlayerEntity::EquipWeapon() const
    {
        return RequireReference(RequireReference(_equipInfo).Weapon);
    }

    PlayerAnimation PlayerEntity::Biped1Anim() const
    {
        return static_cast<PlayerAnimation>(ManagedAt(RequireReference(_bipedModel1).AnimInfo->Index, 0));
    }

    PlayerAnimation PlayerEntity::Biped2Anim() const
    {
        return static_cast<PlayerAnimation>(ManagedAt(RequireReference(_bipedModel2).AnimInfo->Index, 0));
    }

    std::int32_t PlayerEntity::Biped1Frame() const
    {
        return ManagedAt(RequireReference(_bipedModel1).AnimInfo->Frame, 0);
    }

    std::int32_t PlayerEntity::Biped2Frame() const
    {
        return ManagedAt(RequireReference(_bipedModel2).AnimInfo->Frame, 0);
    }

    std::int32_t PlayerEntity::Biped1FrameCount() const
    {
        // PlayerEntity.cs intentionally reads Frame[0], not FrameCount[0].
        return ManagedAt(RequireReference(_bipedModel1).AnimInfo->Frame, 0);
    }

    std::int32_t PlayerEntity::Biped2FrameCount() const
    {
        // PlayerEntity.cs intentionally reads Frame[0], not FrameCount[0].
        return ManagedAt(RequireReference(_bipedModel2).AnimInfo->Frame, 0);
    }

    MphRead::AnimFlags PlayerEntity::Biped1Flags() const
    {
        return ManagedAt(RequireReference(_bipedModel1).AnimInfo->Flags, 0);
    }

    void PlayerEntity::SetBiped1Flags(MphRead::AnimFlags value)
    {
        ManagedAt(RequireReference(_bipedModel1).AnimInfo->Flags, 0) = value;
    }

    MphRead::AnimFlags PlayerEntity::Biped2Flags() const
    {
        return ManagedAt(RequireReference(_bipedModel2).AnimInfo->Flags, 0);
    }

    void PlayerEntity::SetBiped2Flags(MphRead::AnimFlags value)
    {
        ManagedAt(RequireReference(_bipedModel2).AnimInfo->Flags, 0) = value;
    }

    void PlayerEntity::Construct(Scene* scene)
    {
        for (std::int32_t i = 0; i < static_cast<std::int32_t>(_players.size()); ++i)
        {
            std::shared_ptr<PlayerEntity>& slot = ManagedAt(_players, i);
            if (slot == nullptr)
            {
                slot = std::shared_ptr<PlayerEntity>(new PlayerEntity(i, scene));
            }
        }
    }

    void PlayerEntity::ReleaseReferences() noexcept
    {
        _halfturret.reset();
        _enemySpawner.reset();
        _attachedEnemy.reset();
        _field35C.reset();
        _morphCamera.reset();
        _octolithFlag.reset();
        _lastJumpPad.reset();
        _burnedBy.reset();
        _burnLaunchFrame = 0;
        _lastTarget.reset();
        _shockCoilTarget.reset();
        if (_beams)
        {
            _beams->ReleaseReferences();
        }
        _beams.reset();
        _equipInfo.reset();
        _deathaltEffect.reset();
        _doubleDmgEffect.reset();
        _burnEffect.reset();
        _furlEffect.reset();
        _boostEffect.reset();
        _muzzleEffect.reset();
        _chargeEffect.reset();
        _syluxBombs.fill(nullptr);
        AiData.reset();
    }

    void PlayerEntity::Reset()
    {
        for (std::int32_t i = 0; i < static_cast<std::int32_t>(_players.size()); ++i)
        {
            if (const std::shared_ptr<PlayerEntity>& slot = ManagedAt(_players, i))
            {
                slot->ReleaseReferences();
            }
            ManagedAt(_players, i).reset();
        }
        PlayerAiData::InitializeGlobals();
        _playerCount = 0;
        _playersCreated = 0;
        _mainPlayerIndex = 0;
    }

    std::shared_ptr<PlayerEntity> PlayerEntity::Create(MphRead::Hunter hunter, std::int32_t recolor)
    {
        if (_playersCreated >= _maxPlayers)
        {
            return nullptr;
        }
        std::shared_ptr<PlayerEntity> player = ManagedAt(_players, _playersCreated++);
        PlayerEntity& value = RequireReference(player);
        value._hunter = hunter;
        value.SetRecolor(recolor);
        if (value._isBot)
        {
            // C# source intentionally leaves the bot-controls update empty.
        }
        value._loadFlags |= LoadFlags::SlotActive;
        value._loadFlags &= ~LoadFlags::Spawned;
        value.CreateHalfturret();
        return player;
    }

    void PlayerEntity::CreateHalfturret()
    {
        if (_halfturret == nullptr)
        {
            _halfturret = std::make_shared<HalfturretEntity>(SharedFrom(this), _scene);
            _halfturret->Create();
            RequireReference(_scene).InitEntity(_halfturret);
        }
    }

    void PlayerEntity::Initialize()
    {
        Vector3 prevPos = _position;
        Vector3 prevUp = _upVector;
        Vector3 prevFacing = _facingVector;
        MphRead::Formats::Culling::NodeRef prevNodeRef = NodeRef;
        std::int32_t prevHealth = _health;

        _models = EntityBase::ModelList{};
        const auto hunterModelsIt = Metadata::HunterModels.find(_hunter);
        if (hunterModelsIt == Metadata::HunterModels.end())
        {
            throw MphRead::SceneDetail::KeyNotFoundException();
        }
        const auto& hunterModels = hunterModelsIt->second;
        _bipedModelLods[0] = Read::GetModelInstance(hunterModels[0]);
        _bipedModelLods[1] = Read::GetModelInstance(hunterModels[1]);
        _bipedModel1 = Read::GetModelInstance(hunterModels[0]);
        _bipedModel2 = Read::GetModelInstance(hunterModels[0]);
        _altModel = Read::GetModelInstance(hunterModels[2]);
        _gunModel = Read::GetModelInstance(hunterModels[3]);
        _gunSmokeModel = Read::GetModelInstance("gunSmoke");
        _bipedIceModel = Read::GetModelInstance(
            _hunter == MphRead::Hunter::Noxus || _hunter == MphRead::Hunter::Trace
                ? "nox_ice" : "samus_ice");
        _altIceModel = Read::GetModelInstance("alt_ice");
        _doubleDmgModel = Read::GetModelInstance("doubleDamage_img");
        _octolithSimpleModel = Read::GetModelInstance("octolith_simple");
        _models.Add(_bipedModel1);
        _models.Add(_bipedModel2);
        _models.Add(_altModel);
        _models.Add(_gunModel);
        _models.Add(_gunSmokeModel);
        _models.Add(_bipedIceModel);
        _models.Add(_altIceModel);
        _models.Add(_doubleDmgModel);
        _models.Add(_octolithSimpleModel);

        _trailModel = Read::GetModelInstance("trail");
        const auto& trailMaterials
            = RequireReference(RequireReference(RequireReference(_trailModel).Model()).Materials);
        std::shared_ptr<MphRead::Material> material = ManagedReadOnlyListAt(trailMaterials, 0);
        _trailBindingId1 = RequireReference(_scene).BindGetTexture(
            RequireReference(_trailModel).Model(), RequireReference(material).TextureId,
            RequireReference(material).PaletteId, 0);
        material = ManagedReadOnlyListAt(trailMaterials, 1);
        _trailBindingId2 = RequireReference(_scene).BindGetTexture(
            RequireReference(_trailModel).Model(), RequireReference(material).TextureId,
            RequireReference(material).PaletteId, 0);
        _doubleDmgBindingId = RequireReference(_scene).BindGetTexture(
            RequireReference(_doubleDmgModel).Model(), 0, 0, 0);

        EntityBase::Initialize();
        RequireReference(_equipInfo).Beams = _beams;
        std::destroy_at(std::addressof(_values));
        std::construct_at(std::addressof(_values),
            ManagedReadOnlyListAt(Metadata::PlayerValues, static_cast<std::int32_t>(_hunter)));
        _hudObjects = ManagedReadOnlyListAt(
            RequireReference(HudElements::HunterObjects), static_cast<std::int32_t>(_hunter));
        if (IsMainPlayer())
        {
            SetUpHud();
        }
        if (GameState::Multiplayer())
        {
            _healthMax = 2 * _values.EnergyTank - 1;
            _ammoMax[UA] = _ammoMax[Missiles] = _values.MpAmmoCap;
        }
        else
        {
            StorySave& save = RequireReference(::MphRead::GameState::StorySave);
            _healthMax = save.HealthMax;
            _ammoMax[UA] = ManagedAt(save.AmmoMax, UA);
            _ammoMax[Missiles] = ManagedAt(save.AmmoMax, Missiles);
        }

        InitializeWeapon();
        _availableWeapons[MphRead::BeamType::PowerBeam] = true;
        TryEquipWeapon(MphRead::BeamType::PowerBeam, true);
        _facingVector = Negate(Vector3(0.0F, 0.0F, 1.0F));
        _upVector = Vector3(0.0F, 1.0F, 0.0F);
        SetTransform(_facingVector, _upVector, Vector3::Zero);
        _speed = Vector3::Zero;
        _gunVec1 = Negate(Vector3(0.0F, 0.0F, 1.0F));
        _gunVec2 = Negate(Vector3(1.0F, 0.0F, 0.0F));
        _volumeUnxf = PlayerVolumes[CheckedArrayIndex(static_cast<std::int32_t>(_hunter), PlayerVolumes.size())][0];
        _volume = CollisionVolume::Move(_volumeUnxf, static_cast<Vector3>(Position));
        const float aimDistance = Fixed::ToFloat(_values.AimDistance);
        _aimPosition = AddY(static_cast<Vector3>(Position)
                + Vector3(_gunVec1.X * aimDistance, _gunVec1.Y * aimDistance, _gunVec1.Z * aimDistance),
            Fixed::ToFloat(_values.AimYOffset));
        _aimY = 0.0F;
        _gunViewBob = 0.0F;
        _walkViewBob = 0.0F;
        _health = 0;
        _flags2 |= PlayerFlags2::HideModel;
        _attachedEnemy.reset();
        _field35C.reset();
        RequireReference(_equipInfo).ChargeLevel = 0;
        _timeSinceShot = 255;
        _nativeTimeSinceShot = 255;
        _nativeWeaponTimerFrame = std::numeric_limits<std::uint64_t>::max();
        _weavelLungeInput.Reset();
        _weavelNativeAttackPress = false;
        _weavelAltLife = false;
        _timeSinceDamage = 255;
        _timeSincePickup = 255;
        _timeSinceHeal = 255;
        _timeSinceStanding = 0;
        _field449 = 0;
        _respawnTimer = 0;
        _timeSinceDead = 0;
        _field551 = 255;
        _field552 = 0;
        _field553 = 0;
        _bombCooldown = 0;
        _bombRefillTimer = 0;
        _bombAmmo = 3;
        _damageInvulnTimer = 0;
        _spawnInvulnTimer = 0;
        _abilities = AbilityFlags::None;
        _walkSfxTimer = 0.0F;
        _walkSfxIndex = 0;
        _burnSfxAmount = 0.0F;
        if (_teamIndex == -1)
        {
            _teamIndex = _slotIndex;
        }
        _field4E8 = Vector3::Zero;
        _modelTransform = IdentityMatrix();
        _camSwitchTimer = static_cast<std::uint16_t>(_values.CamSwitchTime * 2);
        RequireReference(_cameraInfo).Reset();
        RequireReference(_cameraInfo).Position = Position;
        RequireReference(_cameraInfo).UpVector = Vector3(0.0F, 1.0F, 0.0F);
        RequireReference(_cameraInfo).Target = static_cast<Vector3>(Position) + _facingVector;
        RequireReference(_cameraInfo).NodeRef = MphRead::Formats::Culling::NodeRef::None;
        NodeRef = MphRead::Formats::Culling::NodeRef::None;
        _timeIdle = 0;
        _timeSinceInput = 0;
        _field40C = 0.0F;
        _doubleDmgTimer = 0;
        _octolithFlag.reset();
        ResetMorphBallTrail();

        const std::string shootNodeName = _hunter == MphRead::Hunter::Guardian ? "Head_1" : "R_elbow";
        for (std::int32_t i = 0; i < 2; ++i)
        {
            _spineNodes[static_cast<std::size_t>(i)]
                = RequireReference(_bipedModelLods[static_cast<std::size_t>(i)]).Model()->GetNodeByName("Spine_1");
            _shootNodes[static_cast<std::size_t>(i)]
                = RequireReference(_bipedModelLods[static_cast<std::size_t>(i)]).Model()->GetNodeByName(shootNodeName);
        }
        if (_hunter == MphRead::Hunter::Spire)
        {
            _spireAltNodes[0] = RequireReference(_altModel).Model()->GetNodeByName("L_Rock01");
            _spireAltNodes[1] = RequireReference(_altModel).Model()->GetNodeByName("R_Rock01");
            _spireAltNodes[2] = RequireReference(_altModel).Model()->GetNodeByName("R_POS_ROT");
            _spireAltNodes[3] = RequireReference(_altModel).Model()->GetNodeByName("R_POS_ROT_1");
        }
        else
        {
            _spireAltNodes.fill(nullptr);
        }
        for (Matrix4& transform : _bipedIceTransforms)
        {
            transform = IdentityMatrix();
        }

        if (_reloadInit)
        {
            MphRead::Formats::Culling::NodeRef nodeRef = RequireReference(_scene).GetNodeRefByName("rmMain");
            if (nodeRef == MphRead::Formats::Culling::NodeRef::None)
            {
                nodeRef = prevNodeRef;
            }
            Spawn(prevPos, prevFacing, prevUp, nodeRef, false);
            _health = prevHealth;
            _flags1 |= PlayerFlags1::Grounded;
            _flags1 |= PlayerFlags1::GroundedPrevious;
            _reloadInit = false;
        }
        else if (RequireReference(_scene).Room() == nullptr
            || RequireReference(RequireReference(_scene).Room()).LoadEntityId == -1)
        {
            const std::int32_t checkpointId = RequireReference(::MphRead::GameState::StorySave).CheckpointEntityId;
            std::shared_ptr<EntityBase> checkpoint{};
            if (IsMainPlayer() && GameState::Mode() == GameMode::SinglePlayer && checkpointId != -1
                && RequireReference(_scene).TryGetEntity(checkpointId, checkpoint))
            {
                Vector3 position{};
                Vector3 up{};
                Vector3 facing{};
                RequireReference(checkpoint).GetVectors(position, up, facing);
                Spawn(position, facing, up, RequireReference(checkpoint).NodeRef, false);
            }
        }
    }

    void PlayerEntity::Spawn(Vector3 pos, Vector3 facing, Vector3 up,
        MphRead::Formats::Culling::NodeRef nodeRef, bool respawn)
    {
        if (!Mods::Network::NetPlayerLifecycle::CanSpawn())
        {
            return;
        }
        Mods::Network::NetPlayerLifecycle::OnSpawn(*this);
        Mods::Network::NetSession::ContinuousPhase.ResetSlot(SlotIndex());
        Mods::Network::NetPlayerBridge::NoteSpawn(SlotIndex());
        if (respawn)
        {
            Mods::RespawnChoice::ApplyOnSpawn(SharedFrom(this));
        }
        _loadFlags |= LoadFlags::Spawned;
        if (IsMainPlayer())
        {
            UpdateDoubleDamageSfx(0, false);
            UpdateCloakSfx(0, false);
        }

        _abilities = AbilityFlags::AltForm;
        if (_hunter == MphRead::Hunter::Samus)
        {
            _abilities |= AbilityFlags::Bombs;
            _abilities |= AbilityFlags::Boost;
        }
        else if (_hunter == MphRead::Hunter::Kanden)
        {
            _abilities |= AbilityFlags::Bombs;
            for (Vector3& segment : _kandenSegPos)
            {
                segment = Vector3::Zero;
            }
        }
        else if (_hunter == MphRead::Hunter::Trace)
        {
            _abilities |= AbilityFlags::TraceAltAttack;
        }
        else if (_hunter == MphRead::Hunter::Sylux)
        {
            _abilities |= AbilityFlags::Bombs;
            _syluxBombs[0].reset();
            _syluxBombs[1].reset();
            _syluxBombs[2].reset();
            _syluxBombCount = 0;
        }
        else if (_hunter == MphRead::Hunter::Noxus)
        {
            _abilities |= AbilityFlags::NoxusAltAttack;
        }
        else if (_hunter == MphRead::Hunter::Spire)
        {
            _abilities |= AbilityFlags::SpireAltAttack;
            _spireRockPosL = pos;
            _spireRockPosR = pos;
            _spireAltFacing = Vector3(0.0F, 1.0F, 0.0F);
            _spireAltUp = Vector3(1.0F, 0.0F, 0.0F);
            for (Vector3& value : _spireAltVecs)
            {
                value = Vector3::Zero;
            }
        }
        else if (_hunter == MphRead::Hunter::Weavel)
        {
            _abilities |= AbilityFlags::WeavelAltAttack;
        }

        if (GameState::Multiplayer())
        {
            _health = _values.EnergyTank - 1;
        }
        else if (IsMainPlayer())
        {
            _healthMax = RequireReference(::MphRead::GameState::StorySave).HealthMax;
            _health = RequireReference(::MphRead::GameState::StorySave).Health;
        }
        else
        {
            _health = _healthMax;
        }

        _availableWeapons.ClearAll();
        _availableCharges.ClearAll();
        InitializeWeapon();
        RequireReference(_equipInfo).InfiniteAmmo = false;
        RequireReference(_equipInfo).ChargeLevel = 0;
        RequireReference(_equipInfo).SmokeLevel = 0;
        _doubleDmgTimer = 0;
        _cloakTimer = 0;
        _deathaltTimer = 0;
        _previousWeapon = MphRead::BeamType::PowerBeam;
        TryEquipWeapon(MphRead::BeamType::PowerBeam, true);
        Metadata::LoadEffectiveness(0x2AAAA, BeamEffectiveness);
        _frozenTimer = 0;
        _timeSinceFrozen = 255;
        _frozenGfxTimer = 0;
        _drawIceLayer = false;
        _hidingTimer = 0;
        _curAlpha = 1.0F;
        _targetAlpha = 1.0F;
        _disruptedTimer = 0;
        _burnedBy.reset();
        _burnLaunchFrame = 0;
        _burnTimer = 0;
        _hSpeedCap = Fixed::ToFloat(_values.WalkSpeedCap);
        _speed = Vector3::Zero;
        if (respawn)
        {
            pos.Y += 1.0F;
        }
        _upVector = up;
        _facingVector = facing;
        SetTransform(_facingVector, _upVector, pos);
        _prevPosition = Position;
        _idlePosition = Position;
        _gunVec2 = Vector3::Cross(up, facing).Normalized();
        _gunVec1 = facing;
        const float hMag = std::sqrt(facing.X * facing.X + facing.Z * facing.Z);
        _field70 = facing.X / hMag;
        _field74 = facing.Z / hMag;
        _field78 = _field74;
        _field7C = -_field70;
        _field80 = _field70;
        _field84 = _field74;
        const float aimDistance = Fixed::ToFloat(_values.AimDistance);
        _aimPosition = AddY(static_cast<Vector3>(Position)
                + Vector3(_gunVec1.X * aimDistance, _gunVec1.Y * aimDistance, _gunVec1.Z * aimDistance),
            Fixed::ToFloat(_values.AimYOffset));
        _acceleration = Vector3::Zero;
        _accelerationTimer = 0;
        _aimY = 0.0F;
        _buttonAimX = 0.0F;
        _buttonAimY = 0.0F;
        _nativeDual = {};
        _aimFrame = {};
        _input.Suspend();
        NodeRef = nodeRef;
        _gunViewBob = 0.0F;
        _walkViewBob = 0.0F;

        if (GameState::SinglePlayer() && MphRead::Formats::CameraSequence::Current() != nullptr)
        {
            _camSwitchTimer = static_cast<std::uint16_t>(_values.CamSwitchTime * 2);
            _viewTiltAngleH = 0.0F;
            _viewTiltAngleV = 0.0F;
        }
        else
        {
            MphRead::Formats::CameraSequence* currentSequence = nullptr;
            if (IsMainPlayer() && GameState::Multiplayer()
                && (currentSequence = MphRead::Formats::CameraSequence::Current()) != nullptr
                && currentSequence->IsIntro())
            {
                RequireReference(MphRead::Formats::CameraSequence::Current()).End();
            }
            RequireReference(_cameraInfo).Reset();
            RequireReference(_cameraInfo).Position = Position;
            RequireReference(_cameraInfo).UpVector = Vector3(0.0F, 1.0F, 0.0F);
            RequireReference(_cameraInfo).Target = static_cast<Vector3>(Position) + facing;
            RequireReference(_cameraInfo).Fov = Fixed::ToFloat(_values.NormalFov) * 2.0F;
            RequireReference(_cameraInfo).NodeRef = NodeRef;
            SwitchCamera(CameraType::First, facing);
            _camSwitchTimer = static_cast<std::uint16_t>(_values.CamSwitchTime * 2);
            _viewTiltAngleH = 0.0F;
            _viewTiltAngleV = 0.0F;
            UpdateCameraFirst();
            RequireReference(_cameraInfo).Update();
        }

        const float gunDrawFacingScale = Fixed::ToFloat(_values.FieldB8);
        const Vector3 gunDrawFacing(
            facing.X * gunDrawFacingScale, facing.Y * gunDrawFacingScale, facing.Z * gunDrawFacingScale);
        const Vector3 cameraPosition = RequireReference(_cameraInfo).Position;
        const float gunDrawLeftScale = Fixed::ToFloat(_values.FieldB0);
        const Vector3 gunDrawLeft(
            _gunVec2.X * gunDrawLeftScale, _gunVec2.Y * gunDrawLeftScale, _gunVec2.Z * gunDrawLeftScale);
        const float gunDrawUpScale = Fixed::ToFloat(_values.FieldB4);
        const Vector3 gunDrawUp(up.X * gunDrawUpScale, up.Y * gunDrawUpScale, up.Z * gunDrawUpScale);
        _gunDrawPos = gunDrawFacing + cameraPosition + gunDrawLeft + gunDrawUp;
        _aimVec = _aimPosition - _gunDrawPos;
        _timeSinceInput = 0;
        _flags1 = PlayerFlags1::Standing | PlayerFlags1::StandingPrevious | PlayerFlags1::CanTouchBoost;
        _flags2 = PlayerFlags2::NoShotsFired;
        _volumeUnxf = PlayerVolumes[CheckedArrayIndex(static_cast<std::int32_t>(_hunter), PlayerVolumes.size())][0];
        _volume = CollisionVolume::Move(_volumeUnxf, static_cast<Vector3>(Position));
        _attachedEnemy.reset();
        _field35C.reset();
        _timeSinceShot = 255;
        _nativeTimeSinceShot = 255;
        _nativeWeaponTimerFrame = std::numeric_limits<std::uint64_t>::max();
        _weavelLungeInput.Reset();
        _weavelNativeAttackPress = false;
        _weavelAltLife = false;
        _timeSinceDamage = 255;
        _timeSincePickup = 255;
        _timeSinceHeal = 255;
        _timeSinceStanding = 0;
        _timeStanding = 0;
        _field449 = 0;
        _respawnTimer = 0;
        _timeSinceDead = 0;
        _field551 = 255;
        _field552 = 0;
        _field553 = 0;
        _bombCooldown = 0;
        _bombOveruse = 0;
        _bombRefillTimer = 0;
        _bombAmmo = 3;
        _damageInvulnTimer = 0;
        if (_isBot && GameState::SinglePlayer())
        {
            _spawnInvulnTimer = 0;
        }
        else
        {
            _spawnInvulnTimer = static_cast<std::uint16_t>(_values.SpawnInvulnerability * 2);
        }
        _boostCharge = 0;
        _altAttackCooldown = 0;
        _field4E8 = Vector3::Zero;
        _modelTransform = IdentityMatrix();
        _timeSinceMorphCamera = std::numeric_limits<std::uint16_t>::max();
        SetBipedAnimation(PlayerAnimation::Spawn, AnimFlags::None);
        RequireReference(_altModel).SetAnimation(0, AnimFlags::Paused);
        SetGunAnimation(GunAnimation::Idle, AnimFlags::NoLoop);
        RequireReference(_gunSmokeModel).SetAnimation(0);
        _smokeAlpha = 0.0F;
        _morphCamera.reset();
        _octolithFlag.reset();
        ResetMorphBallTrail();
        _soundSource.StopAllSfx();
        if (IsMainPlayer())
        {
            _soundSource.Update(Position, -1);
        }
        else
        {
            std::int32_t rangeIndex = 1;
            if (GameState::SinglePlayer() && _hunter == MphRead::Hunter::Guardian)
            {
                rangeIndex = 21;
            }
            _soundSource.Update(Position, rangeIndex);
            UpdateNodeRefVolume();
        }
        if (respawn)
        {
            PlayHunterSfx(HunterSfx::Spawn);
        }
        _missileSfxHandle = -1;
        _lastJumpPad.reset();
        _jumpPadControlLock = 0;
        _jumpPadControlLockMin = 0;
        _timeSinceJumpPad = std::numeric_limits<std::uint16_t>::max();
        if (IsMainPlayer())
        {
            ResetReticle();
            RequireReference(_weaponIconInst).SetIndex(0, RequireReference(_scene));
        }
        _cloakTextTimer = 30.0F / 30.0F;
        _doubleDamageTextTimer = 60.0F / 30.0F;
        _drawIceLayer = false;
        _hudShiftX = 0;
        _hudShiftY = 0;
        _objShiftX = 0;
        _objShiftY = 0;
        _scanVisor = false;
        SwitchVisors(true);
        CloseDialogs();
        _altRollFbX = RequireReference(_cameraInfo).Field48;
        _altRollFbZ = RequireReference(_cameraInfo).Field4C;
        _altRollLrX = RequireReference(_cameraInfo).Field50;
        _altRollLrZ = RequireReference(_cameraInfo).Field54;
        _light1Vector = RequireReference(_scene).Light1Vector();
        _light1Color = RequireReference(_scene).Light1Color();
        _light2Vector = RequireReference(_scene).Light2Vector();
        _light2Color = RequireReference(_scene).Light2Color();
        Controls().ClearPressed();
        if (_isBot)
        {
            RequireReference(AiData).InitializeAtSpawn();
        }
        _lastTarget.reset();
        UpdateScanIds();
        if (respawn && (IsMainPlayer() || GameState::Multiplayer()))
        {
            const std::int32_t effectId
                = GameState::Multiplayer() && _playerCount > 2 && !Features::MaxPlayerDetail() ? 33 : 31;
            RequireReference(_scene).SpawnEffect(effectId, Vector3(1.0F, 0.0F, 0.0F), Vector3(0.0F, 1.0F, 0.0F), Position);
        }
        if (IsMainPlayer())
        {
            EndWhiteout();
            if (IsAltForm() || IsMorphing())
            {
                _healthbarYOffset = RequireReference(_hudObjects).HealthOffsetYAlt;
                _boostBombsYOffset = 160;
            }
            else
            {
                _healthbarYOffset = RequireReference(_hudObjects).HealthOffsetY;
                _boostBombsYOffset = 208;
            }
        }
    }

    void PlayerEntity::InitEnemyHunter()
    {
        assert(_enemySpawner != nullptr);
        assert(GameState::Mode() == GameMode::SinglePlayer);
        EnemySpawnFields09 data = RequireReference(_enemySpawner).Data.Fields.S09();
        _healthMax = data.HunterHealthMax;
        _health = data.HunterHealth;
        RequireReference(AiData).HealthThreshold = data.HunterHealthThreshold;
        if (_hunter == MphRead::Hunter::Guardian)
        {
            Music::PlayEncounterMusic(MphRead::Hunter::Guardian);
        }
        if (data.HunterWeapon != 255)
        {
            const auto weapon = static_cast<MphRead::BeamType>(data.HunterWeapon);
            _availableWeapons[weapon] = true;
            _availableCharges[weapon] = true;
            _weaponSlots[0] = MphRead::BeamType::None;
            _weaponSlots[1] = MphRead::BeamType::None;
            _weaponSlots[2] = weapon;
            _ammo[0] = 0;
            _ammo[1] = 0;
            MphRead::WeaponInfo& weaponInfo = CurrentWeaponAt(static_cast<std::int32_t>(weapon));
            ManagedAt(_ammo, static_cast<std::int32_t>(weaponInfo.AmmoType)) = std::numeric_limits<std::int32_t>::max();
            RequireReference(_equipInfo).InfiniteAmmo = true;
            RequireReference(_equipInfo).ChargeLevel = 0;
            RequireReference(_equipInfo).SmokeLevel = 0;
            _doubleDmgTimer = 0;
            _previousWeapon = weapon;
            TryEquipWeapon(weapon, true);
            if (_hunter == MphRead::Hunter::Guardian)
            {
                if (weapon == MphRead::BeamType::VoltDriver)
                {
                    Metadata::LoadEffectiveness(0x255A1, BeamEffectiveness);
                }
                else if (weapon == MphRead::BeamType::Magmaul)
                {
                    Metadata::LoadEffectiveness(0x24D55, BeamEffectiveness);
                }
                else if (weapon == MphRead::BeamType::Judicator)
                {
                    Metadata::LoadEffectiveness(0x27155, BeamEffectiveness);
                }
            }
        }
        _loadFlags |= LoadFlags::Active;
    }

    void PlayerEntity::SaveStatus()
    {
        if (_health == 0)
        {
            return;
        }
        StorySave& save = RequireReference(::MphRead::GameState::StorySave);
        for (std::int32_t i = 0; i < static_cast<std::int32_t>(_weaponSlots.size()); ++i)
        {
            ManagedAt(save.WeaponSlots, i)
                = static_cast<std::int32_t>(ManagedAt(_weaponSlots, i));
        }
        for (std::int32_t i = 0; i < static_cast<std::int32_t>(_ammo.size()); ++i)
        {
            ManagedAt(save.Ammo, i) = ManagedAt(_ammo, i);
        }
        save.Health = _health;
        if (RequireReference(_scene).FadeType() == MphRead::FadeType::None)
        {
            save.CheckpointRoomId = -1;
            save.CheckpointEntityId = -1;
        }
    }

    void PlayerEntity::ResetReferences()
    {
        _attachedEnemy.reset();
        _morphCamera.reset();
        _lastJumpPad.reset();
        _octolithFlag.reset();
        _enemySpawner.reset();
        _lastTarget.reset();
    }

    void PlayerEntity::GetPosition(Vector3& position)
    {
        position = AddY(static_cast<Vector3>(Position), IsAltForm() ? 0.0F : 0.5F);
    }

    void PlayerEntity::GetVectors(Vector3& position, Vector3& up, Vector3& facing)
    {
        position = AddY(static_cast<Vector3>(Position), IsAltForm() ? 0.0F : 0.5F);
        up = _upVector;
        facing = _facingVector;
    }

    bool PlayerEntity::GetTargetable()
    {
        return _health != 0;
    }

    std::int32_t PlayerEntity::GetScanId(bool alternate)
    {
        return alternate ? _altScanId : _scanId;
    }

    bool PlayerEntity::ScanVisible()
    {
        if (_health == 0 || IsMainPlayer())
        {
            return false;
        }
        return EntityBase::ScanVisible();
    }

    void PlayerEntity::UpdateScanIds()
    {
        const std::size_t hunterIndex = CheckedArrayIndex(
            static_cast<std::int32_t>(_hunter), ScanIds.size());
        _scanId = ScanIds[hunterIndex][IsAltForm() ? 1 : 0];
        _altScanId = ScanIds[hunterIndex][IsAltForm() ? 3 : 2];
    }

    void PlayerEntity::SetBiped1Animation(PlayerAnimation anim, MphRead::AnimFlags animFlags)
    {
        SetBipedAnimation(anim, animFlags, true, false, false);
    }

    void PlayerEntity::SetBiped2Animation(PlayerAnimation anim, MphRead::AnimFlags animFlags)
    {
        SetBipedAnimation(anim, animFlags, false, true, false);
    }

    void PlayerEntity::SetBipedAnimation(PlayerAnimation anim, MphRead::AnimFlags animFlags,
        bool setBiped1, bool setBiped2, bool setIfMorphing)
    {
        if (setIfMorphing || !IsMorphing())
        {
            if (setBiped2 && (setIfMorphing || !IsUnmorphing()))
            {
                RequireReference(_bipedModel2).SetAnimation(static_cast<std::int32_t>(anim), animFlags);
            }
            if (setBiped1)
            {
                RequireReference(_bipedModel1).SetAnimation(static_cast<std::int32_t>(anim), animFlags);
            }
        }
    }

    void PlayerEntity::Teleport(Vector3 position, Vector3 facing,
        MphRead::Formats::Culling::NodeRef nodeRef)
    {
        _soundSource.PlaySfx(SfxId::TELEPORT_OUT, true);
        Reposition(position, facing, nodeRef);
        if (IsAltForm() || IsMorphing() || IsUnmorphing())
        {
            ResumeOwnCamera();
            RequireReference(_cameraInfo).Update();
        }
    }

    void PlayerEntity::Reposition(Vector3 position, Vector3 facing,
        MphRead::Formats::Culling::NodeRef nodeRef)
    {
        _gunVec1 = facing;
        _facingVector = facing;
        SetTransform(facing, _upVector, position);
        if (nodeRef != MphRead::Formats::Culling::NodeRef::None)
        {
            NodeRef = nodeRef;
            RequireReference(_cameraInfo).NodeRef = nodeRef;
        }
    }

    void PlayerEntity::Reposition(Vector3 offset, MphRead::Formats::Culling::NodeRef nodeRef)
    {
        Position = static_cast<Vector3>(Position) + offset;
        _prevPosition = _prevPosition + offset;
        _aimPosition = _aimPosition + offset;
        _gunDrawPos = _gunDrawPos + offset;
        _muzzlePos = _muzzlePos + offset;
        _field544 = _field544 + offset;
        MphRead::Entities::CameraInfo& camInfo = RequireReference(_cameraInfo);
        camInfo.Position = camInfo.Position + offset;
        camInfo.PrevPosition = camInfo.PrevPosition + offset;
        camInfo.Target = camInfo.Target + offset;
        _volume = CollisionVolume::Move(_volumeUnxf, static_cast<Vector3>(Position));
        for (std::int32_t i = 0; i < _beams->Length(); ++i)
        {
            RequireReference((*_beams)[i]).Reposition(offset);
        }
        NodeRef = nodeRef;
        camInfo.NodeRef = nodeRef;
        if (TestFlag(_flags2, PlayerFlags2::Halfturret))
        {
            RequireReference(_halfturret).Reposition(offset, nodeRef);
        }
    }

    void PlayerEntity::BlockFormSwitch()
    {
        _flags2 |= PlayerFlags2::NoFormSwitch;
    }

    void PlayerEntity::SetBipedStuck(bool stuck)
    {
        if (stuck)
        {
            _flags2 |= PlayerFlags2::BipedStuck;
        }
        else
        {
            _flags2 &= ~PlayerFlags2::BipedStuck;
        }
    }

    bool PlayerEntity::CheckHitByBomb(BombEntity* bomb, bool halfturret)
    {
        BombEntity& value = RequireReference(bomb);
        if (value.Owner() == this
            && ((!TestFlag(value.Flags(), BombFlags::Exploding)
                    && !TestFlag(value.Flags(), BombFlags::Exploded)) || halfturret))
        {
            return false;
        }
        bool hit = false;
        Vector3 between{};
        if (halfturret)
        {
            between = static_cast<Vector3>(RequireReference(_halfturret).Position) - static_cast<Vector3>(value.Position);
        }
        else
        {
            between = _volume.SpherePosition - static_cast<Vector3>(value.Position);
        }
        const float distSqr = LengthSquared(between);
        const float hitRadiusSqr = Fixed::ToFloat(_values.BombSelfRadiusSquared);
        if (value.Owner() == this)
        {
            if (distSqr <= hitRadiusSqr && between.Y > -_volume.SphereRadius)
            {
                hit = true;
                const float ySpeed = Fixed::ToFloat(_values.BombJumpSpeed);
                if (_speed.Y < ySpeed)
                {
                    _speed = WithY(_speed, ySpeed);
                }
            }
        }
        else if (distSqr <= value.Radius() * value.Radius())
        {
            hit = true;
            DamageFlags flags = DamageFlags::NoDmgInvuln;
            if (halfturret)
            {
                flags |= DamageFlags::Halfturret;
            }
            TakeDamage(value.Damage(), flags, std::nullopt, &value);
            RequireReference(_scene).SendMessage(
                MphRead::Message::Impact, &value, value.Owner(), BoxEntity(this), BoxInt32(0));
        }
        if (hit)
        {
            const float shake = (hitRadiusSqr - distSqr) / hitRadiusSqr * 0.1F;
            RequireReference(_cameraInfo).SetShake(shake);
        }
        return hit;
    }

    void PlayerEntity::OnHalfturretDied()
    {
        _flags2 &= ~PlayerFlags2::Halfturret;
    }

    void PlayerEntity::ResetMorphBallTrail()
    {
        for (std::int32_t i = 0; i < _mbTrailSegments; ++i)
        {
            _mbTrailAlphas[CheckedArrayIndex(_slotIndex, SlotCapacity)][static_cast<std::size_t>(i)] = 0.0F;
        }
        _mbTrailIndices[CheckedArrayIndex(_slotIndex, SlotCapacity)] = 0;
    }

    void PlayerEntity::UpdateMorphBallTrail()
    {
        const std::size_t slot = CheckedArrayIndex(_slotIndex, SlotCapacity);
        for (std::int32_t i = 0; i < _mbTrailSegments; ++i)
        {
            float alpha = _mbTrailAlphas[slot][static_cast<std::size_t>(i)] - 3.0F / 31.0F / 2.0F;
            if (alpha < 0.0F)
            {
                alpha = 0.0F;
            }
            _mbTrailAlphas[slot][static_cast<std::size_t>(i)] = alpha;
        }
        if (IsAltForm())
        {
            const Vector3 row0(_modelTransform.M11, _modelTransform.M12, _modelTransform.M13);
            if (Vector3::Dot(Vector3(0.0F, 1.0F, 0.0F), row0) < 0.5F && _hSpeedMag >= Fixed::ToFloat(1269))
            {
                const Vector3 cross = Vector3::Cross(row0, Vector3(0.0F, 1.0F, 0.0F)).Normalized();
                const Vector3 cross2 = Vector3::Cross(cross, row0);
                const std::int32_t index = _mbTrailIndices[slot];
                _mbTrailAlphas[slot][static_cast<std::size_t>(index)] = 25.0F / 31.0F;
                _mbTrailMatrices[slot][static_cast<std::size_t>(index)] = Matrix4(
                    Vector4(row0.X, row0.Y, row0.Z, 0.0F),
                    Vector4(cross2.X, cross2.Y, cross2.Z, 0.0F),
                    Vector4(cross.X, cross.Y, cross.Z, 0.0F),
                    Vector4(Position.X, Position.Y, Position.Z, 1.0F));
                _mbTrailIndices[slot] = (index + 1) % _mbTrailSegments;
            }
        }
    }

    void PlayerEntity::InitializeWeapon()
    {
        _availableWeapons.ClearAll();
        _availableCharges.ClearAll();
        if (GameState::SinglePlayer() && IsMainPlayer())
        {
            _availableWeapons.Set(RequireReference(::MphRead::GameState::StorySave).Weapons);
            _availableCharges.CopyFrom(_availableWeapons);
            StorySave& save = RequireReference(::MphRead::GameState::StorySave);
            for (std::int32_t i = 0; i < static_cast<std::int32_t>(_weaponSlots.size()); ++i)
            {
                ManagedAt(_weaponSlots, i)
                    = static_cast<MphRead::BeamType>(ManagedAt(save.WeaponSlots, i));
            }
            for (std::int32_t i = 0; i < static_cast<std::int32_t>(_ammo.size()); ++i)
            {
                ManagedAt(_ammo, i) = ManagedAt(save.Ammo, i);
            }
        }
        else if (GameState::SinglePlayer() && _isBot)
        {
            const MphRead::BeamType affinityBeam = Weapons::GetAffinityBeam(_hunter);
            MphRead::WeaponInfo& affinityInfo = CurrentWeaponAt(static_cast<std::int32_t>(affinityBeam));
            _availableWeapons[affinityBeam] = true;
            _availableCharges[affinityBeam] = true;
            _weaponSlots[0] = _weaponSlots[1] = MphRead::BeamType::None;
            _weaponSlots[2] = affinityBeam;
            _ammo[UA] = _ammo[Missiles] = 0;
            ManagedAt(_ammo, static_cast<std::int32_t>(affinityInfo.AmmoType)) = -1;
            _previousWeapon = affinityBeam;
            TryEquipWeapon(affinityBeam, true);
        }
        else
        {
            MphRead::WeaponInfo& missileInfo = CurrentWeaponAt(static_cast<std::int32_t>(MphRead::BeamType::Missile));
            _availableWeapons[MphRead::BeamType::PowerBeam] = true;
            _availableWeapons[MphRead::BeamType::Missile] = true;
            _availableCharges[MphRead::BeamType::PowerBeam] = true;
            _availableCharges[MphRead::BeamType::Missile] = true;
            _weaponSlots[0] = MphRead::BeamType::PowerBeam;
            _weaponSlots[1] = MphRead::BeamType::Missile;
            _weaponSlots[2] = MphRead::BeamType::None;
            _ammo[UA] = _ammo[Missiles] = 0;
            ManagedAt(_ammo, static_cast<std::int32_t>(missileInfo.AmmoType)) = 10 * missileInfo.AmmoCost;
        }
        if (IsMainPlayer())
        {
            // The source intentionally has no executable HUD work here.
        }
    }

    bool PlayerEntity::TryEquipWeapon(MphRead::BeamType beam, bool silent, bool debug, std::source_location caller)
    {
        const std::int32_t index = static_cast<std::int32_t>(beam);
        if (index < 0 || index >= 9)
        {
            return false;
        }
        MphRead::WeaponInfo& info = CurrentWeaponAt(index);
        const std::uint8_t ammoType = info.AmmoType;
        if (debug && Cheats::FreeWeaponSelect())
        {
            _availableWeapons[beam] = true;
            _availableCharges[beam] = true;
            ManagedAt(_ammo, static_cast<std::int32_t>(ammoType)) = ManagedAt(_ammoMax, static_cast<std::int32_t>(ammoType));
        }
        const bool hasAmmo = beam == MphRead::BeamType::PowerBeam
            || ManagedAt(_ammo, static_cast<std::int32_t>(ammoType)) >= info.AmmoCost
            || ManagedAt(_ammo, static_cast<std::int32_t>(ammoType)) == -1;
        if (!silent && (!hasAmmo || !_availableWeapons[beam] || _gunAnimation == GunAnimation::UpDown))
        {
            if (IsMainPlayer())
            {
                if (!GameState::MenuPause())
                {
                    _soundSource.PlayFreeSfx(SfxId::BEAM_SWITCH_FAIL);
                }
                if (!hasAmmo)
                {
                    ShowNoAmmoMessage();
                }
            }
            return false;
        }

        if (IsMainPlayer() && Mods::DebugLog::Active() && beam != _currentWeapon)
        {
            // Which input asked: "a weapon changed and nobody touched anything".
            Mods::DebugLog::Line("weapon", "frame " + std::to_string(RequireReference(_scene).FrameCount())
                + " " + std::to_string(static_cast<int>(_currentWeapon)) + " -> " + std::to_string(index)
                + " silent=" + (silent ? "1" : "0") + " bind=" + (debug ? "1" : "0")
                + " health=" + std::to_string(_health)
                + " shoot=" + (_controls.Shoot().IsDown() ? "d" : "-") + (_controls.Shoot().IsPressed() ? "p" : "-")
                + (_controls.Shoot().IsReleased() ? "r" : "-")
                + " missileKey=" + (_controls.Missile().IsPressed() ? "p" : "-")
                + " menu=" + (_controls.WeaponMenu().IsDown() ? "d" : "-")
                + " selection=" + std::to_string(static_cast<int>(_weaponSelection))
                + " from=" + std::string(std::string_view(caller.file_name()).substr(
                    std::string_view(caller.file_name()).find_last_of("/\\") + 1))
                + ":" + std::to_string(caller.line()));
        }
        StopBeamChargeSfx(_currentWeapon);
        UpdateZoom(false);
        _previousWeapon = _currentWeapon;
        _currentWeapon = _weaponSelection = beam;
        if (beam == Weapons::GetAffinityBeam(_hunter)
            || (GameState::SinglePlayer()
                && ((_hunter == MphRead::Hunter::Samus
                        && (beam == MphRead::BeamType::PowerBeam || beam == MphRead::BeamType::OmegaCannon))
                    || (_hunter == MphRead::Hunter::Guardian && beam == MphRead::BeamType::VoltDriver))))
        {
            RequireReference(_equipInfo).Weapon = CurrentWeaponPtrAt(index + 9);
        }
        else
        {
            RequireReference(_equipInfo).Weapon = CurrentWeaponPtrAt(index);
        }
        RequireReference(_equipInfo).ChargeLevel = 0;
        RequireReference(_equipInfo).SmokeLevel = 0;
        RequireReference(_equipInfo).GetAmmo = [this, ammoType]() {
            return ManagedAt(_ammo, static_cast<std::int32_t>(ammoType));
        };
        RequireReference(_equipInfo).SetAmmo = [this, ammoType](std::int32_t newAmmo) {
            ManagedAt(_ammo, static_cast<std::int32_t>(ammoType)) = newAmmo;
        };
        _timeSinceInput = 0;

        if (!silent)
        {
            if (IsMainPlayer() && !IsAltForm() && beam != MphRead::BeamType::Missile && !GameState::MenuPause())
            {
                const auto& hunterSfx = RequireReference(Metadata::HunterSfx());
            const auto& sfxRow = hunterSfx[CheckedArrayIndex(
                static_cast<std::int32_t>(_hunter), hunterSfx.size())];
            const std::int32_t sfx = sfxRow[CheckedArrayIndex(
                static_cast<std::int32_t>(HunterSfx::BeamSwitch), sfxRow.size())];
                if (sfx != -1)
                {
                    _soundSource.PlayFreeSfx(sfx);
                }
            }
            if (beam == MphRead::BeamType::Missile)
            {
                SetGunAnimation(GunAnimation::MissileOpen, AnimFlags::NoLoop);
            }
            else if (_previousWeapon == MphRead::BeamType::Missile)
            {
                SetGunAnimation(GunAnimation::MissileClose, AnimFlags::NoLoop);
            }
            else
            {
                _currentWeapon = _weaponSelection = _previousWeapon;
                SetGunAnimation(GunAnimation::Switch, AnimFlags::NoLoop);
                _currentWeapon = _weaponSelection = beam;
            }
        }
        if (beam != MphRead::BeamType::PowerBeam && beam != MphRead::BeamType::Missile)
        {
            UpdateAffinityWeaponSlot(beam);
        }
        if (IsMainPlayer())
        {
            HudOnWeaponSwitch(beam);
        }
        return true;
    }

    void PlayerEntity::ShowNoAmmoMessage()
    {
        const std::string message = Strings::GetHudMessage(9);
        QueueHudMessage(128, 120, Align::Center, 256, 8, ColorRgba(0x295F), 1,
            45.0F / 30.0F, 1, message);
    }

    void PlayerEntity::UpdateZoom(bool zoom)
    {
        MphRead::EquipInfo& equipInfo = RequireReference(_equipInfo);
        if (IsMainPlayer() && equipInfo.Zoomed != zoom)
        {
            _soundSource.PlayFreeSfx(zoom ? SfxId::SNIPER_ZOOM_IN : SfxId::SNIPER_ZOOM_OUT);
            if (_currentWeapon == MphRead::BeamType::Imperialist)
            {
                HudOnZoom(zoom);
            }
        }
        equipInfo.Zoomed = zoom;
    }

    void PlayerEntity::UpdateAffinityWeaponSlot(MphRead::BeamType beam, std::int32_t slot)
    {
        assert(slot == 2);
        const std::size_t index = CheckedArrayIndex(slot, _weaponSlots.size());
        if (_weaponSlots[index] == MphRead::BeamType::OmegaCannon && beam != MphRead::BeamType::OmegaCannon)
        {
            _availableCharges[MphRead::BeamType::OmegaCannon] = false;
            _availableWeapons[MphRead::BeamType::OmegaCannon] = false;
        }
        _weaponSlots[index] = beam;
    }

    void PlayerEntity::UnequipOmegaCannon()
    {
        if (_currentWeapon == MphRead::BeamType::OmegaCannon && GameState::Multiplayer())
        {
            _availableCharges[MphRead::BeamType::OmegaCannon] = false;
            _availableWeapons[MphRead::BeamType::OmegaCannon] = false;
            std::int32_t priority = 0;
            MphRead::BeamType nextBeam = MphRead::BeamType::None;
            for (std::int32_t i = 1; i < 9; ++i)
            {
                if (i != 2 && _availableWeapons[i])
                {
                    MphRead::WeaponInfo& info = CurrentWeaponAt(i);
                    if (info.Priority > priority
                        && ManagedAt(_ammo, static_cast<std::int32_t>(info.AmmoType)) >= info.AmmoCost)
                    {
                        priority = info.Priority;
                        nextBeam = static_cast<MphRead::BeamType>(i);
                    }
                }
            }
            UpdateAffinityWeaponSlot(nextBeam);
            if (nextBeam == MphRead::BeamType::None)
            {
                TryEquipWeapon(MphRead::BeamType::PowerBeam);
            }
            else
            {
                TryEquipWeapon(nextBeam);
            }
        }
    }

    void PlayerEntity::SetGunAnimation(MphRead::Entities::GunAnimation anim, MphRead::AnimFlags animFlags)
    {
        _gunAnimation = anim;
        const auto& hunterAnimations = ManagedAt(
            Metadata::GunAnimationIds, static_cast<std::int32_t>(_hunter));
        const auto& animationIds = ManagedAt(hunterAnimations, static_cast<std::int32_t>(anim));
        std::int32_t animId = ManagedAt(animationIds, 0);
        MphRead::SetFlags setFlags = MphRead::SetFlags::Texture | MphRead::SetFlags::Texcoord
            | MphRead::SetFlags::Material | MphRead::SetFlags::Unused | MphRead::SetFlags::Node;
        RequireReference(_gunModel).SetAnimation(animId, 0, setFlags, animFlags);
        animId = ManagedAt(animationIds, static_cast<std::int32_t>(_currentWeapon) + 1);
        if (animId >= 0)
        {
            setFlags &= ~MphRead::SetFlags::Node;
            if (_hunter == MphRead::Hunter::Sylux)
            {
                setFlags &= ~MphRead::SetFlags::Texcoord;
            }
            RequireReference(_gunModel).SetAnimation(animId, 1, setFlags, animFlags);
        }
        if (IsMainPlayer())
        {
            if (anim == GunAnimation::MissileClose)
            {
                _soundSource.StopSfxByHandle(_missileSfxHandle);
                _missileSfxHandle = -1;
                if (!IsAltForm())
                {
                    PlayMissileSfx(HunterSfx::MissileClose);
                }
            }
            else if (anim == GunAnimation::MissileOpen && RequireReference(_equipInfo).ChargeLevel == 0)
            {
                _soundSource.StopSfxByHandle(_missileSfxHandle);
                if (!IsAltForm() && _health > 0)
                {
                    _missileSfxHandle = PlayMissileSfx(HunterSfx::MissileSwitch);
                }
            }
        }
        if (anim == GunAnimation::FullChargeMissile || anim == GunAnimation::ChargingMissile
            || anim == GunAnimation::MissileClose || anim == GunAnimation::MissileOpen
            || anim == GunAnimation::Unknown9 || anim == GunAnimation::MissileShot)
        {
            _flags1 |= PlayerFlags1::GunOpenAnimation;
        }
        else
        {
            _flags1 &= ~PlayerFlags1::GunOpenAnimation;
        }
    }

    void PlayerEntity::UpdateGunAnimation()
    {
        AnimationInfo& animInfo = RequireReference(RequireReference(_gunModel).AnimInfo);
        if (_timeSinceInput == 0)
        {
            if (_gunAnimation == GunAnimation::UpDown
                && TestFlag(ManagedAt(animInfo.Flags, 0), AnimFlags::Reverse))
            {
                ManagedAt(animInfo.Flags, 0) &= ~AnimFlags::Reverse;
                ManagedAt(animInfo.Flags, 0) &= ~AnimFlags::Ended;
            }
        }
        else if (!Features::NoIdleSway()
            && static_cast<std::uint64_t>(_timeSinceInput) >= static_cast<std::uint64_t>(_values.GunIdleTime) * 2)
        {
            if (_gunAnimation != GunAnimation::UpDown)
            {
                if (_currentWeapon != MphRead::BeamType::Missile || _gunAnimation == GunAnimation::MissileClose)
                {
                    SetGunAnimation(GunAnimation::UpDown, AnimFlags::NoLoop | AnimFlags::Reverse);
                }
                else
                {
                    SetGunAnimation(GunAnimation::MissileClose, AnimFlags::NoLoop);
                }
            }
            return;
        }
        if (_gunAnimation == GunAnimation::UpDown)
        {
            if (!TestFlag(ManagedAt(animInfo.Flags, 0), AnimFlags::Ended))
            {
                return;
            }
            if (_currentWeapon == MphRead::BeamType::Missile)
            {
                SetGunAnimation(GunAnimation::MissileOpen, AnimFlags::NoLoop);
            }
        }
        if (TestFlag(_flags1, PlayerFlags1::ShotUncharged))
        {
            if (!TestFlag(_flags1, PlayerFlags1::ShotMissile))
            {
                SetGunAnimation(GunAnimation::Shot, AnimFlags::NoLoop);
                return;
            }
            if (!TestFlag(_flags1, PlayerFlags1::GunOpenAnimation))
            {
                SetGunAnimation(GunAnimation::Unknown9, AnimFlags::NoLoop);
                return;
            }
            SetGunAnimation(GunAnimation::MissileShot, AnimFlags::NoLoop);
            return;
        }
        if (TestFlag(_flags1, PlayerFlags1::ShotCharged))
        {
            if (TestFlag(_flags1, PlayerFlags1::ShotMissile))
            {
                SetGunAnimation(GunAnimation::MissileShot, AnimFlags::NoLoop);
                return;
            }
            SetGunAnimation(GunAnimation::ChargeShot, AnimFlags::NoLoop);
            return;
        }

        MphRead::EquipInfo& equipInfo = RequireReference(_equipInfo);
        MphRead::WeaponInfo& equipWeapon = RequireReference(equipInfo.Weapon);
        if (equipInfo.ChargeLevel >= equipWeapon.MinCharge * 2)
        {
            if ((_gunAnimation == GunAnimation::Charging || _gunAnimation == GunAnimation::ChargingMissile)
                && RequireReference(_scene).FrameCount() != 0 && RequireReference(_scene).FrameCount() % 2 == 0)
            {
                const std::int32_t frame = (ManagedAt(animInfo.FrameCount, 0) - 1)
                    * (equipInfo.ChargeLevel / 2 - equipWeapon.MinCharge)
                    / (equipWeapon.FullCharge - equipWeapon.MinCharge);
                ManagedAt(animInfo.Frame, 0) = frame;
            }
            if (_currentWeapon == MphRead::BeamType::Missile)
            {
                if (_gunAnimation != GunAnimation::ChargingMissile && _gunAnimation != GunAnimation::FullChargeMissile)
                {
                    SetGunAnimation(GunAnimation::ChargingMissile, AnimFlags::NoLoop);
                }
                else if (_gunAnimation != GunAnimation::FullChargeMissile
                    && TestFlag(ManagedAt(animInfo.Flags, 0), AnimFlags::Ended))
                {
                    SetGunAnimation(GunAnimation::FullChargeMissile);
                }
            }
            else if (_gunAnimation != GunAnimation::Charging && _gunAnimation != GunAnimation::FullCharge)
            {
                SetGunAnimation(GunAnimation::Charging, AnimFlags::NoLoop);
            }
            else if (_gunAnimation != GunAnimation::FullCharge
                && TestFlag(ManagedAt(animInfo.Flags, 0), AnimFlags::Ended))
            {
                SetGunAnimation(GunAnimation::FullCharge);
            }
            return;
        }
        if ((_gunAnimation == GunAnimation::Charging || _gunAnimation == GunAnimation::ChargingMissile)
            && !TestFlag(ManagedAt(animInfo.Flags, 0), AnimFlags::Reverse))
        {
            ManagedAt(animInfo.Flags, 0) |= AnimFlags::Reverse;
            return;
        }
        if (_gunAnimation == GunAnimation::FullCharge)
        {
            SetGunAnimation(GunAnimation::Idle, AnimFlags::NoLoop);
            return;
        }
        if (_gunAnimation == GunAnimation::FullChargeMissile)
        {
            SetGunAnimation(GunAnimation::MissileOpen, AnimFlags::NoLoop);
            ManagedAt(animInfo.Frame, 0) = ManagedAt(animInfo.FrameCount, 0) - 1;
            return;
        }
        if (TestFlag(ManagedAt(animInfo.Flags, 0), AnimFlags::Ended))
        {
            if (!TestFlag(_flags1, PlayerFlags1::GunOpenAnimation) || _gunAnimation == GunAnimation::MissileClose)
            {
                SetGunAnimation(GunAnimation::Idle, AnimFlags::NoLoop);
            }
            else if (_currentWeapon == MphRead::BeamType::Missile && _weaponSelection != MphRead::BeamType::Missile)
            {
                SetGunAnimation(GunAnimation::MissileClose, AnimFlags::NoLoop);
            }
        }
    }

    void PlayerEntity::TakeDamage(std::int32_t damage, DamageFlags flags,
        std::optional<Vector3> direction, EntityBase* source)
    {
        // C# unchecked conversion from Int32 to UInt32.
        TakeDamage(static_cast<std::uint32_t>(damage), flags, direction, source);
    }

    void PlayerEntity::TakeDamage(std::uint32_t damage, DamageFlags flags,
        std::optional<Vector3> direction, EntityBase* source)
    {
        const Mods::Network::NetDamage::PredictionScoreScope predictedScores(
            Mods::Network::NetHitPrediction::Predicting());
        if (Mods::Network::NetDamage::Suppress(*this, source, flags))
        {
            return;
        }
        if (_health == 0)
        {
            return;
        }
        MphRead::Formats::CameraSequence* currentSequence = nullptr;
        if (IsMainPlayer()
            && (currentSequence = MphRead::Formats::CameraSequence::Current()) != nullptr
            && currentSequence->BlockInput())
        {
            if (!TestFlag(flags, DamageFlags::Death))
            {
                return;
            }
            CamSeqEntity::CancelCurrent();
        }
        if (_spawnInvulnTimer > 0 && !TestFlag(flags, DamageFlags::Death)
            && !TestFlag(flags, DamageFlags::IgnoreInvuln))
        {
            return;
        }
        if (_isBot && TestFlag(RequireReference(AiData).Flags2, AiFlags2::Bit13))
        {
            return;
        }
        if (!TestAny(flags, DamageFlags::Death | DamageFlags::IgnoreInvuln | DamageFlags::NoDmgInvuln))
        {
            if (_damageInvulnTimer > 0)
            {
                return;
            }
            _damageInvulnTimer = static_cast<std::uint16_t>(_values.DamageInvuln * 2);
        }

        PlayerEntity* attacker = nullptr;
        bool fromHalfturret = false;
        BombEntity* bomb = nullptr;
        BeamProjectileEntity* beam = nullptr;
        if (source != nullptr)
        {
            if (source->Type == MphRead::EntityType::BeamProjectile)
            {
                beam = static_cast<BeamProjectileEntity*>(source);
                const Effectiveness effectiveness
                    = BeamEffectiveness[CheckedArrayIndex(static_cast<std::int32_t>(beam->Beam()), BeamEffectiveness.size())];
                if (effectiveness == Effectiveness::Zero)
                {
                    return;
                }
                std::shared_ptr<EntityBase> owner = beam->Owner();
                if (owner != nullptr && owner->Type == MphRead::EntityType::Player)
                {
                    attacker = static_cast<PlayerEntity*>(owner.get());
                }
                else if (owner != nullptr && owner->Type == MphRead::EntityType::Halfturret)
                {
                    std::shared_ptr<PlayerEntity> ownerPlayer
                        = static_cast<HalfturretEntity*>(owner.get())->Owner();
                    attacker = ownerPlayer.get();
                    fromHalfturret = true;
                }
                if (damage > 0)
                {
                    damage = static_cast<std::uint32_t>(
                        damage * ManagedReadOnlyListAt(
                            Metadata::DamageMultipliers, static_cast<std::int32_t>(effectiveness)));
                    if (damage == 0)
                    {
                        damage = 1;
                    }
                }
                if (!IsMainPlayer())
                {
                    beam->SpawnDamageEffect(effectiveness);
                }
            }
            else if (source->Type == MphRead::EntityType::Player)
            {
                attacker = static_cast<PlayerEntity*>(source);
                // Not again for a rescued hit claim: the shooter's own
                // machine already doubled it before it sent the number.
                // Mods.Network.NetDamage.ApplyingClaim.
                if (attacker->_doubleDmgTimer > 0 && !Mods::Network::NetDamage::ApplyingClaim())
                {
                    damage *= 2;
                }
            }
            else if (source->Type == MphRead::EntityType::Bomb)
            {
                bomb = static_cast<BombEntity*>(source);
                attacker = bomb->Owner();
            }
        }

        bool ignoreDamage = false;
        if ((GameState::SinglePlayer() && _isBot && attacker == this)
            || (GameState::Teams() && !GameState::FriendlyFire()
                && attacker != nullptr && attacker != this
                && Mods::Multiplayer::TeamRules::AreAllies(attacker->_teamIndex, _teamIndex)))
        {
            ignoreDamage = true;
            damage = 0;
        }
        if (!ignoreDamage && TestFlag(flags, DamageFlags::Headshot) && attacker == Main().get())
        {
            const std::int32_t messageId = GameState::Multiplayer() ? 228 : 121;
            QueueHudMessage(128, 40, 20.0F / 30.0F, 0, messageId);
        }
        if (attacker != nullptr && attacker != this && beam != nullptr)
        {
            ManagedAt(GameState::BeamDamageDealt(), attacker->_slotIndex) = std::min(
                UncheckedAdd(
                    ManagedAt(GameState::BeamDamageDealt(), attacker->_slotIndex),
                    std::bit_cast<std::int32_t>(damage)),
                ManagedAt(GameState::BeamDamageMax(), attacker->_slotIndex));
        }
        // Likewise: the damage level is in the number a claim carries
        // already. Mods.Network.NetDamage.ApplyingClaim.
        if (damage > 0 && !Mods::Network::NetDamage::ApplyingClaim())
        {
            damage = static_cast<std::uint32_t>(
                damage * ManagedReadOnlyListAt(Metadata::DamageLevels, GameState::DamageLevel()));
            if (damage == 0)
            {
                damage = 1;
            }
        }
        if (TestFlag(_flags2, PlayerFlags2::Halfturret) && attacker != nullptr && !ignoreDamage)
        {
            RequireReference(_halfturret).OnTakeDamage(SharedFrom<EntityBase>(attacker), damage);
        }
        std::uint32_t turretDamage = 0;
        if (TestFlag(flags, DamageFlags::Halfturret) && !ignoreDamage)
        {
            turretDamage = HalfturretShare(damage);
            DamageHalfturret(turretDamage);
            damage -= turretDamage;
            if (_isBot && GameState::SinglePlayer() && RequireReference(AiData).Flags1)
            {
                if (_health > RequireReference(AiData).HealthThreshold
                    && (static_cast<std::int64_t>(_health) - static_cast<std::int64_t>(damage))
                        <= RequireReference(AiData).HealthThreshold)
                {
                    damage = static_cast<std::uint32_t>(UncheckedSubtract(
                        UncheckedSubtract(_health, RequireReference(AiData).HealthThreshold), 1));
                }
            }
            else if (ManagedInt32LessThanOrEqualUInt32(_health, damage))
            {
                damage = static_cast<std::uint32_t>(UncheckedSubtract(_health, 1));
            }
            if (_isBot)
            {
                RequireReference(AiData).DamageFromHalfturret = turretDamage;
            }
        }
        if (_isBot && source != nullptr)
        {
            PlayerAiData& aiData = RequireReference(AiData);
            aiData.OnTakeDamage(
                std::bit_cast<std::int32_t>(damage), *source, SharedFrom(attacker));
        }

        // beam.ModLaunchFrame identifies the *shot*, not the moment it
        // landed, so the authority's record of this hit and a claim for the
        // same shot can be paired however long the projectile was in the
        // air. Mods.Network.NetHitClaims.
        Mods::Network::NetDamage::Note(*this, attacker,
            beam != nullptr ? beam->Beam() : MphRead::BeamType::None,
            flags, direction, damage, bomb != nullptr, beam != nullptr ? beam->ModLaunchFrame : 0U);
        Mods::Network::NetHitPrediction::NoteHit(*this, attacker, flags, damage,
            beam != nullptr ? beam->Beam() : MphRead::BeamType::None,
            beam != nullptr ? beam->ModLaunchFrame
                : TestFlag(flags, DamageFlags::Burn) ? _burnLaunchFrame : 0U,
            beam != nullptr ? beam->Age() : 0.0F,
            direction, beam != nullptr ? beam->Afflictions() : MphRead::Affliction::None,
            beam != nullptr ? beam->ModShotSequence : 0U, turretDamage);
        if (attacker != this)
        {
            Mods::Input::AimAssist::AimAssistTelemetry::Hit(attacker,
                beam != nullptr ? beam->Beam() : MphRead::BeamType::None, damage);
        }

        bool dead = false;
        if (_isBot && GameState::SinglePlayer() && RequireReference(AiData).Flags1
            && _health <= RequireReference(AiData).HealthThreshold)
        {
            dead = true;
        }
        else if (ManagedInt32LessThanOrEqualUInt32(_health, damage) || TestFlag(flags, DamageFlags::Death))
        {
            dead = true;
        }

        if (attacker != nullptr)
        {
            if (attacker == Main().get())
            {
                RequireReference(Main()).UpdateOpponent(_slotIndex);
            }
            if (attacker != this)
            {
                ManagedAt(GameState::DamageCount(), attacker->_slotIndex) = UncheckedAdd(
                    ManagedAt(GameState::DamageCount(), attacker->_slotIndex), 1);
                attacker->_hidingTimer = 0;
                _hidingTimer = 0;
            }
        }
        if (damage > 0 || dead)
        {
            ModControllerFeedback(dead ? Mods::Input::GamepadFeedback::Death
                : bomb != nullptr || (beam != nullptr && (beam->Beam() == MphRead::BeamType::Missile
                    || beam->Beam() == MphRead::BeamType::Magmaul)) ? Mods::Input::GamepadFeedback::Explosion
                : Mods::Input::GamepadFeedback::Damage);
        }

        if (dead)
        {
            MphRead::BeamType beamType = MphRead::BeamType::Platform;
            if (beam != nullptr)
            {
                beamType = beam->Beam();
            }
            else if (Mods::Network::NetDamage::ReplayBeam() != MphRead::BeamType::None)
            {
                beamType = Mods::Network::NetDamage::ReplayBeam();
            }
            if (source == attacker || fromHalfturret || bomb != nullptr)
            {
                flags |= DamageFlags::FromAlt;
            }
            RequireReference(_scene).SendMessage(
                MphRead::Message::Destroyed, this, nullptr, BoxInt32(0), BoxInt32(0), 1);
            if (_enemySpawner != nullptr)
            {
                RequireReference(_scene).SendMessage(
                    MphRead::Message::Destroyed, this, _enemySpawner.get(), BoxInt32(0), BoxInt32(0));
                assert(_enemySpawner->Type == MphRead::EntityType::EnemySpawn);
                ItemSpawnEntity::SpawnItemDrop(_enemySpawner->Data.ItemType,
                    Position, NodeRef, _enemySpawner->Data.ItemChance, _scene);
            }
            ResetCombatVisor();
            if (TestFlag(_flags2, PlayerFlags2::Halfturret))
            {
                RequireReference(_halfturret).Die();
            }
            _healthRecovery = 0;
            _ammoRecovery[0] = 0;
            _ammoRecovery[1] = 0;
            RequireReference(_equipInfo).ChargeLevel = 0;
            RequireReference(_cameraInfo).Shake = 0.0F;
            _doubleDmgTimer = 0;
            _deathaltTimer = 0;
            _cloakTimer = 0;
            _flags2 &= ~PlayerFlags2::Cloaking;
            ManagedAt(GameState::KillStreak(), _slotIndex) = 0;
            if (IsMainPlayer())
            {
                HudEndDisrupted();
                if (_frozenGfxTimer > 0)
                {
                    _drawIceLayer = false;
                }
            }
            _frozenTimer = 0;
            _frozenGfxTimer = 0;
            _disruptedTimer = 0;
            _burnTimer = 0;
            if (_furlEffect != nullptr)
            {
                RequireReference(_scene).UnlinkEffectEntry(_furlEffect);
                _furlEffect.reset();
            }
            if (_boostEffect != nullptr)
            {
                RequireReference(_scene).UnlinkEffectEntry(_boostEffect);
                _boostEffect.reset();
            }
            if (_burnEffect != nullptr)
            {
                if (_burnEffect->EffectId == 188)
                {
                    RequireReference(_scene).UnlinkEffectEntry(_burnEffect);
                }
                else
                {
                    RequireReference(_scene).DetachEffectEntry(_burnEffect, false);
                }
                _burnEffect.reset();
            }
            if (_chargeEffect != nullptr)
            {
                RequireReference(_scene).UnlinkEffectEntry(_chargeEffect);
                _chargeEffect.reset();
            }
            if (_muzzleEffect != nullptr)
            {
                RequireReference(_scene).UnlinkEffectEntry(_muzzleEffect);
                _muzzleEffect.reset();
            }
            if (_doubleDmgEffect != nullptr)
            {
                RequireReference(_scene).UnlinkEffectEntry(_doubleDmgEffect);
                _doubleDmgEffect.reset();
            }
            if (_deathaltEffect != nullptr)
            {
                RequireReference(_scene).UnlinkEffectEntry(_deathaltEffect);
                _deathaltEffect.reset();
            }
            if (_health > 0)
            {
                _soundSource.StopAllSfx(true);
                if (IsMainPlayer())
                {
                    UpdateDoubleDamageSfx(0, false);
                    UpdateCloakSfx(0, false);
                    _soundSource.StopFreeSfxScripts();
                    if (GameState::Multiplayer())
                    {
                        PlayHunterSfx(HunterSfx::Death);
                    }
                    else
                    {
                        _sfxStopTimer = 10.0F / 30.0F;
                        _soundSource.PlayFreeSfx(SfxId::SAMUS_DEATH);
                    }
                }
                else
                {
                    Music::UpdateEncounterMusic(static_cast<std::int32_t>(_hunter));
                    PlayHunterSfx(HunterSfx::Death);
                }
                StopBeamChargeSfx(_currentWeapon);
            }
            if (_isBot && RequireReference(AiData).Flags1)
            {
                _health = UncheckedAdd(RequireReference(AiData).HealthThreshold, 1);
                RequireReference(AiData).Flags2 |= AiFlags2::Bit13;
            }
            else
            {
                _health = 0;
            }
            UpdateZoom(false);
            _boostCharge = 0;
            ManagedAt(GameState::Deaths(), _slotIndex) = UncheckedAdd(
                ManagedAt(GameState::Deaths(), _slotIndex), 1);
            if (this == Main().get() && beamType == MphRead::BeamType::OmegaCannon)
            {
                RequireReference(_scene).SetFade(MphRead::FadeType::FadeInWhite, 90.0F / 30.0F, true);
            }
            _speed = Vector3::Zero;
            _respawnTimer = RespawnTime();
            _timeSinceDead = 0;
            if (GameState::SinglePlayer())
            {
                if (IsAltForm())
                {
                    const std::int32_t effectId = IsMainPlayer() ? 10 : 216;
                    RequireReference(_scene).SpawnEffect(effectId, Vector3(1.0F, 0.0F, 0.0F), Vector3(0.0F, 1.0F, 0.0F), Position);
                }
                if (_isBot && GameState::GetAreaState(RequireReference(_scene).AreaId()) == AreaState::Clear)
                {
                    bool unlockDoors = true;
                    auto spawnerEnumerator = RequireReference(_scene).GetEnemySpawnEntities().GetEnumerator();
                    while (spawnerEnumerator.MoveNext())
                    {
                        std::shared_ptr<EnemySpawnEntity> spawner = spawnerEnumerator.Current();
                        EnemySpawnEntity& value = RequireReference(spawner);
                        if (value.Data.EnemyType != MphRead::EnemyType::Hunter)
                        {
                            continue;
                        }
                        if (TestFlag(value.Flags, SpawnerFlags::Active)
                            && (value.Data.SpawnTotal == 0 || value.SpawnedCount < value.Data.SpawnTotal
                                || value.ActiveCount != 0))
                        {
                            for (std::int32_t i = 1; i < _maxPlayers; ++i)
                            {
                                PlayerEntity& other = RequireReference(ManagedAt(_players, i));
                                if (other._enemySpawner == spawner)
                                {
                                    unlockDoors = false;
                                    break;
                                }
                            }
                        }
                        if (!unlockDoors)
                        {
                            break;
                        }
                    }
                    if (unlockDoors)
                    {
                        GameState::CompleteRandomEncounter(RequireReference(_scene).RoomId());
                        auto doorEnumerator = RequireReference(_scene).GetDoorEntities().GetEnumerator();
                        while (doorEnumerator.MoveNext())
                        {
                            std::shared_ptr<DoorEntity> door = doorEnumerator.Current();
                            DoorEntity& value = RequireReference(door);
                            if (value.Data().ConnectorId == 255 && value.Id != -1)
                            {
                                continue;
                            }
                            if (value.Data().PaletteId != 9)
                            {
                                value.SetFlags(value.Flags() & ~DoorFlags::ShowLock);
                            }
                            if (value.Id == -1
                                || RequireReference(::MphRead::GameState::StorySave).GetRoomState(RequireReference(_scene).RoomId(), value.Id) == 0)
                            {
                                value.Unlock(false, false);
                            }
                        }
                    }
                }
                if (IsMainPlayer())
                {
                    if (RequireReference(RequireReference(::MphRead::GameState::StorySave).Stats).Deaths != std::numeric_limits<std::uint32_t>::max())
                    {
                        RequireReference(RequireReference(::MphRead::GameState::StorySave).Stats).Deaths++;
                    }
                    if (attacker != nullptr && !attacker->IsMainPlayer()
                        && attacker->_hunter != MphRead::Hunter::Guardian
                        && RequireReference(RequireReference(::MphRead::GameState::StorySave).Stats).EnemyHunterDeaths != std::numeric_limits<std::uint32_t>::max())
                    {
                        RequireReference(RequireReference(::MphRead::GameState::StorySave).Stats).EnemyHunterDeaths++;
                    }
                    GameState::PausePrevented(true);
                    _deathCountdown = 150.0F / 30.0F;
                    _deathProcessed = false;
                    _deathLostOctolithSfxPlayed = false;
                    _deathLostOctolithDialogShown = false;
                    _respawnTimer = std::numeric_limits<std::uint16_t>::max();
                    RequireReference(_cameraInfo).SetShake(0.25F);
                    _lostOctolithEnemyIndex = -1;
                    if (RequireReference(::MphRead::GameState::StorySave).CurrentOctoliths != 0 && _playerCount > 1
                        && (GameState::EscapeTimer() != 0 || GameState::EscapeState() != EscapeState::Escape))
                    {
                        float minDistance = 0.0F;
                        for (std::int32_t i = 1; i < _playerCount; ++i)
                        {
                            PlayerEntity& enemyHunter = RequireReference(ManagedAt(_players, i));
                            if (enemyHunter._health == 0 || enemyHunter._hunter == MphRead::Hunter::Guardian)
                            {
                                continue;
                            }
                            const float distance = LengthSquared(
                                static_cast<Vector3>(enemyHunter.Position) - static_cast<Vector3>(Position));
                            if (_lostOctolithEnemyIndex == -1 || distance < minDistance)
                            {
                                _lostOctolithEnemyIndex = i;
                                minDistance = distance;
                            }
                        }
                        if (_lostOctolithEnemyIndex != -1)
                        {
                            _lostOctolithDrawPos = AddY(static_cast<Vector3>(Position), 0.6F);
                            _lostOctolithSpeed = 0.2F * 30.0F;
                        }
                    }
                    Music::UpdateEncounterMusic(-2);
                    Music::Stop(150.0F / 30.0F);
                }
                else if (attacker != nullptr && attacker->IsMainPlayer())
                {
                    const std::uint32_t hunterBit = ManagedShiftLeftInt32(
                        1, static_cast<std::int32_t>(_hunter));
                    RequireReference(::MphRead::GameState::StorySave).DefeatedHunters |= static_cast<std::uint8_t>(hunterBit);
                    ManagedAt(RequireReference(::MphRead::GameState::StorySave).AreaHunters, RequireReference(_scene).AreaId() / 2)
                        &= static_cast<std::uint8_t>(~hunterBit);
                    if (RequireReference(RequireReference(::MphRead::GameState::StorySave).Stats).HunterKills != std::numeric_limits<std::uint32_t>::max())
                    {
                        RequireReference(RequireReference(::MphRead::GameState::StorySave).Stats).HunterKills++;
                    }
                    if (_hunter == MphRead::Hunter::Guardian
                        && RequireReference(RequireReference(::MphRead::GameState::StorySave).Stats).EnemyKills != std::numeric_limits<std::uint32_t>::max())
                    {
                        RequireReference(RequireReference(::MphRead::GameState::StorySave).Stats).EnemyKills++;
                    }

                    const auto restoreOctolith = [](std::int32_t dropId)
                    {
                        const std::uint32_t shift = static_cast<std::uint32_t>(4 * dropId);
                        const std::uint32_t lostMask
                            = ~(std::uint32_t{15} << shift) | (std::uint32_t{8} << shift);
                        RequireReference(::MphRead::GameState::StorySave).LostOctoliths
                            = RequireReference(::MphRead::GameState::StorySave).LostOctoliths & lostMask;
                        RequireReference(::MphRead::GameState::StorySave).CurrentOctoliths
                            |= static_cast<std::uint16_t>(std::uint32_t{1} << dropId);
                    };

                    std::int32_t dropId
                        = RequireReference(::MphRead::GameState::StorySave).GetEnemyOctolithDrop(static_cast<std::int32_t>(_hunter));
                    if (dropId < 8)
                    {
                        restoreOctolith(dropId);
                        auto artifactEnumerator = RequireReference(_scene).GetArtifactEntities().GetEnumerator();
                        while (artifactEnumerator.MoveNext())
                        {
                            std::shared_ptr<ArtifactEntity> artifact = artifactEnumerator.Current();
                            ArtifactEntity& value = RequireReference(artifact);
                            if (value.Id != -1)
                            {
                                continue;
                            }
                            if (value.ModelId() >= 8 && value.ArtifactId() == dropId)
                            {
                                value.Position = AddY(static_cast<Vector3>(Position), 1.5F);
                                value.NodeRef = NodeRef;
                                value.SetActive(true);
                                break;
                            }
                        }
                    }
                    while (true)
                    {
                        dropId = RequireReference(::MphRead::GameState::StorySave).GetEnemyOctolithDrop(static_cast<std::int32_t>(_hunter));
                        if (dropId >= 8)
                        {
                            break;
                        }
                        restoreOctolith(dropId);
                    }
                }
            }
            else
            {
                if (attacker != nullptr)
                {
                    if (IsMainPlayer())
                    {
                        CloseDialogs();
                        if (attacker == this)
                        {
                            QueueHudMessage(128, 70, 140, 90.0F / 30.0F, 2, 235, false);
                        }
                        else
                        {
                            const std::string nickname = ManagedAt(GameState::Nicknames(), attacker->_slotIndex);
                            std::string message = Strings::GetHudMessage(
                                TestFlag(flags, DamageFlags::Headshot) ? 236 : 237);
                            QueueHudMessage(128, 70, 140, 90.0F / 30.0F, 2,
                                ReplaceToken(std::move(message), "%s", nickname));
                        }
                        std::optional<std::string> killedBy{};
                        if (TestFlag(flags, DamageFlags::Deathalt))
                        {
                            killedBy = Strings::GetHudMessage(250);
                        }
                        else if (TestFlag(flags, DamageFlags::Burn))
                        {
                            killedBy = Strings::GetHudMessage(251);
                        }
                        else if (fromHalfturret)
                        {
                            killedBy = _altAttackNames[CheckedArrayIndex(
                                static_cast<std::int32_t>(attacker->_hunter), _altAttackNames.size())];
                        }
                        else if (static_cast<std::int32_t>(beamType)
                            <= static_cast<std::int32_t>(MphRead::BeamType::OmegaCannon))
                        {
                            killedBy = _weaponNames[CheckedArrayIndex(
                                static_cast<std::int32_t>(beamType), _weaponNames.size())];
                        }
                        else if (source == attacker)
                        {
                            if (attacker->_hunter == MphRead::Hunter::Weavel)
                            {
                                killedBy = Strings::GetHudMessage(253);
                            }
                            else
                            {
                                killedBy = _altAttackNames[CheckedArrayIndex(
                                    static_cast<std::int32_t>(attacker->_hunter), _altAttackNames.size())];
                            }
                        }
                        else if (bomb != nullptr)
                        {
                            if (bomb->BombType() == MphRead::BombType::MorphBall)
                            {
                                killedBy = Strings::GetHudMessage(252);
                            }
                            else if (bomb->BombType() == MphRead::BombType::Stinglarva)
                            {
                                killedBy = _altAttackNames[static_cast<std::size_t>(MphRead::Hunter::Kanden)];
                            }
                            else if (bomb->BombType() == MphRead::BombType::Lockjaw)
                            {
                                killedBy = _altAttackNames[static_cast<std::size_t>(MphRead::Hunter::Sylux)];
                            }
                        }
                        if (Mods::Network::NetLog::Enabled())
                        {
                            // What killed this player, as their screen names it: the
                            // one way to tell a kill's cause reached a machine.
                            Mods::Network::NetLog::Event("[death] killed by " + std::to_string(attacker->_slotIndex)
                                + " (" + killedBy.value_or("no cause") + ")");
                        }
                        if (killedBy.has_value())
                        {
                            QueueHudMessage(128, 70, 140, 90.0F / 30.0F, 2,
                                "(" + *killedBy + ")");
                        }
                    }
                    if (attacker == this)
                    {
                        ManagedAt(GameState::Suicides(), _slotIndex) = UncheckedAdd(
                                    ManagedAt(GameState::Suicides(), _slotIndex), 1);
                        if (GameState::Mode() == GameMode::Battle || GameState::Mode() == GameMode::BattleTeams)
                        {
                            ManagedAt(GameState::Points(), _slotIndex) = UncheckedSubtract(
                                ManagedAt(GameState::Points(), _slotIndex), 1);
                        }
                    }
                    else
                    {
                        if (Mods::Multiplayer::TeamRules::AreAllies(attacker->_teamIndex, _teamIndex))
                        {
                            ManagedAt(GameState::FriendlyKills(), attacker->_slotIndex) = UncheckedAdd(
                                    ManagedAt(GameState::FriendlyKills(), attacker->_slotIndex), 1);
                            ManagedAt(GameState::KillStreak(), attacker->_slotIndex) = 0;
                            if (attacker == Main().get())
                            {
                                const std::string nickname = ManagedAt(GameState::Nicknames(), _slotIndex);
                                std::string message = Strings::GetHudMessage(240);
                                QueueHudMessage(128, 70, 140, 60.0F / 30.0F, 2,
                                    ReplaceToken(std::move(message), "%s", nickname));
                            }
                        }
                        else
                        {
                            if (attacker == Main().get())
                            {
                                const std::string nickname = ManagedAt(GameState::Nicknames(), _slotIndex);
                                std::string message = Strings::GetHudMessage(
                                    TestFlag(flags, DamageFlags::Headshot) ? 239 : 238);
                                QueueHudMessage(128, 70, 140, 60.0F / 30.0F, 2,
                                    ReplaceToken(std::move(message), "%s", nickname));
                            }
                            if (TestFlag(flags, DamageFlags::Headshot))
                            {
                                ManagedAt(GameState::HeadshotKills(), attacker->_slotIndex) = UncheckedAdd(
                                    ManagedAt(GameState::HeadshotKills(), attacker->_slotIndex), 1);
                            }
                            ManagedAt(GameState::Kills(), attacker->_slotIndex) = UncheckedAdd(
                                    ManagedAt(GameState::Kills(), attacker->_slotIndex), 1);
                            if (attacker->IsPrimeHunter())
                            {
                                ManagedAt(GameState::KillsAsPrime(), attacker->_slotIndex) = UncheckedAdd(
                                    ManagedAt(GameState::KillsAsPrime(), attacker->_slotIndex), 1);
                            }
                            if (static_cast<std::int32_t>(beamType)
                                <= static_cast<std::int32_t>(MphRead::BeamType::OmegaCannon))
                            {
                                ManagedAt(
                                    ManagedAt(GameState::BeamKills(), attacker->_slotIndex),
                                    static_cast<std::int32_t>(beamType)) = UncheckedAdd(
                                        ManagedAt(
                                            ManagedAt(GameState::BeamKills(), attacker->_slotIndex),
                                            static_cast<std::int32_t>(beamType)),
                                        1);
                            }
                            if (ManagedAt(GameState::KillStreak(), attacker->_slotIndex) < 255)
                            {
                                ManagedAt(GameState::KillStreak(), attacker->_slotIndex) = UncheckedAdd(
                                    ManagedAt(GameState::KillStreak(), attacker->_slotIndex), 1);
                            }
                            if (ManagedAt(GameState::KillStreak(), attacker->_slotIndex) == 5)
                            {
                                _soundSource.QueueStream(VoiceId::VOICE_CONSECUTIVE_KILLS, 1);
                                std::string message;
                                if (attacker->IsMainPlayer())
                                {
                                    message = Strings::GetHudMessage(254);
                                }
                                else
                                {
                                    const std::string nickname = ManagedAt(GameState::Nicknames(), attacker->_slotIndex);
                                    message = ReplaceToken(Strings::GetHudMessage(255), "%s", nickname);
                                }
                                QueueHudMessage(128, 70, 140, 90.0F / 30.0F, 2, message);
                            }
                            if (GameState::Mode() == GameMode::PrimeHunter)
                            {
                                if (attacker->IsPrimeHunter())
                                {
                                    attacker->GainHealth(70);
                                }
                                else if (attacker->_health > 0
                                    && (GameState::PrimeHunter() == -1 || IsPrimeHunter()))
                                {
                                    GameState::PrimeHunter(attacker->_slotIndex);
                                    ManagedAt(GameState::PrimesKilled(), attacker->_slotIndex) = UncheckedAdd(
                                    ManagedAt(GameState::PrimesKilled(), attacker->_slotIndex), 1);
                                    if (RequireReference(Main()).IsPrimeHunter())
                                    {
                                        _soundSource.QueueStream(VoiceId::VOICE_PRIME, 1);
                                    }
                                    const std::string nickname = ManagedAt(GameState::Nicknames(), attacker->_slotIndex);
                                    std::string message = Strings::GetHudMessage(241);
                                    QueueHudMessage(128, 70, 140, 90.0F / 30.0F, 2,
                                        ReplaceToken(std::move(message), "%s", nickname));
                                }
                            }
                            else if (GameState::Mode() == GameMode::Battle || GameState::Mode() == GameMode::BattleTeams)
                            {
                                if (ManagedAt(GameState::Points(), attacker->_slotIndex) < 99999)
                                {
                                    ManagedAt(GameState::Points(), attacker->_slotIndex) = UncheckedAdd(
                                        ManagedAt(GameState::Points(), attacker->_slotIndex), 1);
                                }
                            }
                            else if (GameState::IsOctolithMode() && _octolithFlag != nullptr)
                            {
                                ManagedAt(GameState::OctolithStops(), attacker->_slotIndex) = UncheckedAdd(
                                    ManagedAt(GameState::OctolithStops(), attacker->_slotIndex), 1);
                            }
                            if (TestFlag(flags, DamageFlags::FromAlt))
                            {
                                ManagedAt(GameState::AltDamageCount(), attacker->_slotIndex) = UncheckedAdd(
                                    ManagedAt(GameState::AltDamageCount(), attacker->_slotIndex), 1);
                            }
                        }
                    }
                }
                else
                {
                    ManagedAt(GameState::Suicides(), _slotIndex) = UncheckedAdd(
                                    ManagedAt(GameState::Suicides(), _slotIndex), 1);
                    if (GameState::Mode() == GameMode::Battle || GameState::Mode() == GameMode::BattleTeams)
                    {
                        ManagedAt(GameState::Points(), _slotIndex) = UncheckedSubtract(
                                ManagedAt(GameState::Points(), _slotIndex), 1);
                    }
                }
                if (IsAltForm() || IsMorphing())
                {
                    RequireReference(_scene).SpawnEffect(216, Vector3(1.0F, 0.0F, 0.0F), Vector3(0.0F, 1.0F, 0.0F), Position);
                }
                if (attacker == nullptr || attacker == this)
                {
                    const Vector3 camFacing = RequireReference(_cameraInfo).Position
                        + RequireReference(_cameraInfo).Facing;
                    SwitchCamera(CameraType::Free, camFacing);
                }
                else
                {
                    SwitchCamera(CameraType::Free, attacker->Position);
                }
                if (IsPrimeHunter())
                {
                    GameState::PrimeHunter(-1);
                    QueueHudMessage(128, 70, 140, 90.0F / 30.0F, 2, 242, false);
                }
            }
            if (GameState::Multiplayer() && attacker != nullptr && attacker != this)
            {
                MphRead::ItemType itemType = MphRead::ItemType::UASmall;
                if (RequireReference(RequireReference(attacker->_equipInfo).Weapon).AmmoType == 1)
                {
                    itemType = MphRead::ItemType::MissileSmall;
                }
                const Vector3 position = AddY(_volume.SpherePosition, 0.35F);
                ItemSpawnEntity::SpawnItem(itemType, position, NodeRef, 300 * 2, _scene);
            }
            _weaponSelection = _currentWeapon;
            _flags1 &= ~PlayerFlags1::WeaponMenuOpen;
        }
        else
        {
            bool skipSfx = false;
            _health = UncheckedSubtract(_health, std::bit_cast<std::int32_t>(damage));
            if (beam != nullptr && !ignoreDamage)
            {
                if (TestFlag(beam->Afflictions(), MphRead::Affliction::Freeze))
                {
                    if (TestFlag(flags, DamageFlags::Halfturret))
                    {
                        _soundSource.PlaySfx(SfxId::SHOTGUN_FREEZE);
                        RequireReference(_halfturret).OnFrozen();
                    }
                    else
                    {
                        _soundSource.PlaySfx(SfxId::SHOTGUN_FREEZE);
                        if (IsMainPlayer())
                        {
                            ResetCombatVisor();
                            _drawIceLayer = true;
                        }
                        if (_frozenTimer == 0)
                        {
                            if (_timeSinceFrozen > 60 * 2)
                            {
                                const std::int32_t time
                                    = (GameState::Multiplayer() || attacker != nullptr ? 75 : 30) * 2;
                                _frozenTimer = static_cast<std::uint16_t>(time);
                            }
                            else if (_frozenTimer < 15 * 2)
                            {
                                _frozenTimer = 15 * 2;
                            }
                            _frozenGfxTimer = static_cast<std::uint16_t>(_frozenTimer + 5 * 2);
                        }
                        EndAltAttack();
                    }
                }
                if (TestFlag(beam->Afflictions(), MphRead::Affliction::Disrupt)
                    && !TestFlag(flags, DamageFlags::Halfturret))
                {
                    _disruptedTimer = 60 * 2;
                    if (IsMainPlayer())
                    {
                        skipSfx = true;
                        HudOnDisrupted();
                        _soundSource.PlaySfx(SfxId::LOB_DISRUPT);
                    }
                }
                if (TestFlag(beam->Afflictions(), MphRead::Affliction::Burn))
                {
                    if (TestFlag(flags, DamageFlags::Halfturret))
                    {
                        RequireReference(_halfturret).OnSetOnFire();
                    }
                    else
                    {
                        std::uint16_t time = 150 * 2;
                        if (attacker != nullptr)
                        {
                            const std::int32_t encounter = ManagedAt(GameState::EncounterState(), attacker->_slotIndex);
                            if (attacker->_isBot && GameState::SinglePlayer()
                                && (encounter == 1 || encounter == 3 || encounter == 4))
                            {
                                time = 75 * 2;
                            }
                        }
                        _burnedBy = beam->Owner();
                        _burnLaunchFrame = beam->ModLaunchFrame;
                        _burnTimer = time;
                        CreateBurnEffect();
                    }
                }
            }
            if (!skipSfx && !TestFlag(flags, DamageFlags::NoSfx))
            {
                PlayHunterSfx(HunterSfx::Damage);
            }
            if (IsMainPlayer() && !IsAltForm())
            {
                PlayRandomDamageSfx();
            }
        }

        _timeSinceDamage = 0;
        if (_health > 0 && _frozenTimer == 0)
        {
            std::optional<Vector3> hitDirection{};
            if (direction.has_value())
            {
                if (!IsAltForm())
                {
                    if (!TestFlag(flags, DamageFlags::Halfturret))
                    {
                        Vector3 speed = _speed + WithY(*direction, 0.0F);
                        if (direction->Y <= 0.0F)
                        {
                            speed.Y += direction->Y;
                        }
                        else if (speed.Y < 0.25F)
                        {
                            speed.Y += direction->Y;
                            if (speed.Y > 0.25F)
                            {
                                speed.Y = 0.25F;
                            }
                        }
                        _speed = speed;
                    }
                    if (!VectorEquals(*direction, Vector3::Zero))
                    {
                        hitDirection = *direction;
                    }
                    else if (beam != nullptr)
                    {
                        hitDirection = beam->Velocity();
                    }
                }
                else if (!TestFlag(flags, DamageFlags::Halfturret))
                {
                    _speed = _speed + WithY(
                        Vector3(direction->X * 0.4F, direction->Y * 0.4F, direction->Z * 0.4F), 0.0F);
                }
            }
            else if (attacker != nullptr)
            {
                hitDirection = static_cast<Vector3>(Position) - static_cast<Vector3>(attacker->Position);
            }
            if (hitDirection.has_value() && !IsAltForm())
            {
                const float hitZ = hitDirection->Z;
                const float hitX = -hitDirection->X;
                const float v126 = -hitZ * _field74;
                float dirHorizontal = -hitZ * _gunVec2.Z;
                const float dirLeftRight = hitX * _gunVec2.X + dirHorizontal;
                if (dirLeftRight < 0.0F)
                {
                    dirHorizontal = -dirLeftRight;
                }
                const float v125 = hitX * _field70;
                const float dirUpDown = v125 + v126;
                if (dirLeftRight >= 0.0F)
                {
                    dirHorizontal = dirLeftRight;
                }
                float dirVertical;
                if (dirUpDown >= 0.0F)
                {
                    dirVertical = v125 + v126;
                }
                else
                {
                    dirVertical = -dirUpDown;
                }
                PlayerAnimation anim = PlayerAnimation::None;
                if (dirVertical <= dirHorizontal)
                {
                    if (dirLeftRight <= 0.0F)
                    {
                        anim = PlayerAnimation::DamageRight;
                        if (IsMainPlayer())
                        {
                            _damageIndicatorTimers[2] = 63 * 2;
                        }
                    }
                    else
                    {
                        anim = PlayerAnimation::DamageLeft;
                        if (IsMainPlayer())
                        {
                            _damageIndicatorTimers[6] = 63 * 2;
                        }
                    }
                }
                else if (dirUpDown <= 0.0F)
                {
                    anim = PlayerAnimation::DamageBack;
                    if (IsMainPlayer())
                    {
                        _damageIndicatorTimers[4] = 63 * 2;
                    }
                }
                else
                {
                    anim = PlayerAnimation::DamageFront;
                    if (IsMainPlayer())
                    {
                        _damageIndicatorTimers[0] = 63 * 2;
                    }
                }
                if (anim != PlayerAnimation::None)
                {
                    SetBipedAnimation(anim, AnimFlags::NoLoop, false, true, false);
                }
            }
        }
        if (_health > 0 && !IsAltForm())
        {
            float shake = 0.03F;
            if (!TestFlag(flags, DamageFlags::Burn))
            {
                shake = ::MphRead::NativeRuntime::MathMax(damage * 0.01F, 0.05F);
            }
            RequireReference(_cameraInfo).SetShake(shake);
        }
        if (IsMainPlayer())
        {
            // C# source contains only the rumble TODO here.
        }
    }

    void PlayerEntity::LoadWeaponNames()
    {
        for (std::int32_t i = 0; i < 9; ++i)
        {
            _weaponNames[static_cast<std::size_t>(i)]
                = Strings::GetMessage('W', i + 1, StringTables::WeaponNames);
        }
        for (std::int32_t i = 0; i < 8; ++i)
        {
            _hunterNames[static_cast<std::size_t>(i)]
                = Strings::GetMessage('H', i + 1, StringTables::WeaponNames);
        }
        for (std::int32_t i = 0; i < 7; ++i)
        {
            _altAttackNames[static_cast<std::size_t>(i)]
                = Strings::GetMessage('A', i + 1, StringTables::WeaponNames);
        }
    }

    void PlayerEntity::GeneratePlayerVolumes()
    {
        for (std::int32_t i = 0; i < 8; ++i)
        {
            const PlayerValues values = ManagedReadOnlyListAt(Metadata::PlayerValues, i);
            float radius = Fixed::ToFloat(values.BipedColRadius);
            Vector3 center(0.0F, Fixed::ToFloat(values.MinPickupHeight) + radius, 0.0F);
            PlayerVolumes[static_cast<std::size_t>(i)][0] = CollisionVolume(center, radius);
            center = Vector3(0.0F, Fixed::ToFloat(values.MaxPickupHeight) - radius, 0.0F);
            PlayerVolumes[static_cast<std::size_t>(i)][1] = CollisionVolume(center, radius);
            radius = Fixed::ToFloat(values.AltColRadius);
            center = Vector3(0.0F, Fixed::ToFloat(values.AltColYPos), 0.0F);
            PlayerVolumes[static_cast<std::size_t>(i)][2] = CollisionVolume(center, radius);
        }
    }

    void PlayerEntity::GenerateKandenAltNodeDistances()
    {
        if (KandenAltNodeDistances[0] == 0.0F && KandenAltNodeDistances[1] == 0.0F
            && KandenAltNodeDistances[2] == 0.0F && KandenAltNodeDistances[3] == 0.0F)
        {
            std::shared_ptr<ModelInstance> instance = Read::GetModelInstance("KandenAlt_lod0");
            std::shared_ptr<MphRead::Model> model = RequireReference(instance).Model();
            RequireReference(model).ComputeNodeMatrices(0);
            const auto& nodes = *RequireReference(model).Nodes;
            for (std::int32_t i = 0; i < 4; ++i)
            {
                const Matrix4& transform1 = RequireReference(ManagedReadOnlyListAt(nodes, i)).Transform;
                const Matrix4& transform2 = RequireReference(ManagedReadOnlyListAt(nodes, i + 1)).Transform;
                const Vector3 pos1(transform1.M41, transform1.M42, transform1.M43);
                const Vector3 pos2(transform2.M41, transform2.M42, transform2.M43);
                KandenAltNodeDistances[static_cast<std::size_t>(i)] = Distance(pos1, pos2);
            }
        }
    }
}

namespace MphRead::Entities
{
    namespace
    {
        // PlayerEntity.cs [Flags] LoadFlags : byte
        constexpr ::MphRead::NativeRuntime::EnumNameEntry LoadFlagsNames[] = {
            {0x0ULL, "None"},
            {0x1ULL, "Connected"},
            {0x2ULL, "WasConnected"},
            {0x4ULL, "Disconnected"},
            {0x8ULL, "Initial"},
            {0x10ULL, "Unknown4"},
            {0x20ULL, "Active"},
            {0x40ULL, "SlotActive"},
            {0x80ULL, "Spawned"},
        };
    }

    std::string ToString(LoadFlags value)
    {
        return ::MphRead::NativeRuntime::ManagedEnumToString(
            value, LoadFlagsNames, std::size(LoadFlagsNames), true);
    }
}
