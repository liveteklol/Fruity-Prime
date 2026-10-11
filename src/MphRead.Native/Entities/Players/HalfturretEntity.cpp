#include "HalfturretEntity.hpp"
#include "../../Mods/Multiplayer/TeamLayout.hpp"
#include "../../Mods/Gameplay/NativeGameplayClock.hpp"
#include "../../Mods/Combat/Extensions/HalfturretEnemyExtension.hpp"

#include "../BeamProjectileEntity.hpp"
#include "../RoomEntity.hpp"
#include "../../Formats/CollisionDetection.hpp"
#include "../../Formats/Effects.hpp"
#include "../../GameState.hpp"
#include "../../MemoryArrays.hpp"
#include "../../Metadata/Metadata.hpp"
#include "../../Metadata/Weapons.hpp"
#include "../../Scene.hpp"
#include "../../Strings.hpp"
#include "PlayerEntity.hpp"
#include "../../NativeRuntime/System/Managed.hpp"
#include "../../Formats/Types.hpp"
#include "../../NativeRuntime/System/Globalization.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using ::MphRead::NativeRuntime::ManagedAt;
using ::MphRead::NativeRuntime::RequireReference;
using ::MphRead::NativeRuntime::UInt64ToInt32;
using ::OpenTK::Mathematics::CreateScale;
using ::OpenTK::Mathematics::IsZero;

namespace
{
    using OpenTK::Mathematics::Matrix4;
    using OpenTK::Mathematics::Vector3;
    using OpenTK::Mathematics::Vector4;

    constexpr Vector3 UnitX(1.0F, 0.0F, 0.0F);
    constexpr Vector3 UnitY(0.0F, 1.0F, 0.0F);
    constexpr Vector3 One(1.0F, 1.0F, 1.0F);

    [[nodiscard]] constexpr Matrix4 Identity() noexcept
    {
        return Matrix4(
            Vector4(1.0F, 0.0F, 0.0F, 0.0F),
            Vector4(0.0F, 1.0F, 0.0F, 0.0F),
            Vector4(0.0F, 0.0F, 1.0F, 0.0F),
            Vector4(0.0F, 0.0F, 0.0F, 1.0F));
    }

    [[nodiscard]] constexpr Matrix4 Translation(Vector3 position) noexcept
    {
        return Matrix4(
            Vector4(1.0F, 0.0F, 0.0F, 0.0F),
            Vector4(0.0F, 1.0F, 0.0F, 0.0F),
            Vector4(0.0F, 0.0F, 1.0F, 0.0F),
            Vector4(position, 1.0F));
    }

    [[nodiscard]] Matrix4 RotationZ(float radians)
    {
        const float c = std::cos(radians);
        const float s = std::sin(radians);
        return Matrix4(
            Vector4(c, s, 0.0F, 0.0F),
            Vector4(-s, c, 0.0F, 0.0F),
            Vector4(0.0F, 0.0F, 1.0F, 0.0F),
            Vector4(0.0F, 0.0F, 0.0F, 1.0F));
    }

    [[nodiscard]] Matrix4 RotationY(float radians)
    {
        const float c = std::cos(radians);
        const float s = std::sin(radians);
        return Matrix4(
            Vector4(c, 0.0F, -s, 0.0F),
            Vector4(0.0F, 1.0F, 0.0F, 0.0F),
            Vector4(s, 0.0F, c, 0.0F),
            Vector4(0.0F, 0.0F, 0.0F, 1.0F));
    }

    template <typename T>
    [[nodiscard]] const T& ReadOnlyListAt(
        const std::vector<T>& values, std::int32_t index)
    {
        if (index < 0 || static_cast<std::size_t>(index) >= values.size())
        {
            throw MphRead::Memory::Detail::ArgumentOutOfRangeException();
        }
        return values[static_cast<std::size_t>(index)];
    }

}

namespace MphRead::Entities
{
    HalfturretEntity::HalfturretEntity(std::shared_ptr<PlayerEntity> owner, Scene* scene)
        : DynamicLightEntityBase(EntityType::Halfturret, scene),
          _owner(std::move(owner)),
          _equipInfo(std::make_shared<MphRead::EquipInfo>())
    {
    }

    std::shared_ptr<PlayerEntity> HalfturretEntity::Owner() const noexcept
    {
        return _owner;
    }

    std::shared_ptr<EntityBase> HalfturretEntity::Target() const noexcept
    {
        return _target;
    }

    std::shared_ptr<Formats::NodeData3> HalfturretEntity::ClosestNode() const noexcept
    {
        return _closestNode;
    }

    void HalfturretEntity::SetClosestNode(std::shared_ptr<Formats::NodeData3> value) noexcept
    {
        _closestNode = std::move(value);
    }

    std::int32_t HalfturretEntity::Health() const noexcept
    {
        return _health;
    }

    void HalfturretEntity::SetHealth(std::int32_t value) noexcept
    {
        _health = value;
    }

    std::uint16_t HalfturretEntity::TimeSinceDamage() const noexcept
    {
        return _timeSinceDamage;
    }

    void HalfturretEntity::SetTimeSinceDamage(std::uint16_t value) noexcept
    {
        _timeSinceDamage = value;
    }

    std::shared_ptr<MphRead::EquipInfo> HalfturretEntity::EquipInfo() const noexcept
    {
        return _equipInfo;
    }

    void HalfturretEntity::Create()
    {
        ModelInstance& inst = SetUpModel("WeavelAlt_Turret_lod0");
        const std::shared_ptr<Model> model = inst.Model();
        _baseNode = RequireReference(model).GetNodeByName("TurretBase");
        const std::int32_t parentIndex = RequireReference(_baseNode).ParentIndex;
        const auto& nodes = RequireReference(RequireReference(model).Nodes);
        _baseNodeParent = ReadOnlyListAt(nodes, parentIndex);
        SetUpModel("alt_ice");
        _altIceModel = _models.Items().back();
    }

    void HalfturretEntity::ResetForSpawn()
    {
        if (_burnEffect != nullptr)
        {
            RequireReference(_scene).UnlinkEffectEntry(_burnEffect);
            _burnEffect.reset();
        }
        _target.reset();
        _closestNode.reset();
        _health = 0;
        _timeSinceDamage = std::numeric_limits<std::uint16_t>::max();
        _timeSinceFrozen = 0;
        _freezeTimer = 0;
        _burnTimer = 0;
        _ySpeed = 0.0F;
        _grounded = false;
        _aimVector = {};
        _targetTimer = 0;
        _cooldownTimer = 0;
        _cooldownFactorRaw = Mods::Combat::HalfturretFireRate::Normal;
        // EquipInfo owns transient overrides; shared weapon metadata stays intact.
        RequireReference(_equipInfo) = MphRead::EquipInfo{};
    }

    std::uint32_t HalfturretEntity::NativeShotThreshold() const
    {
        return Mods::Combat::HalfturretFireRate::Threshold(
            RequireReference(RequireReference(_equipInfo).Weapon).ShotCooldown,
            _cooldownFactorRaw);
    }

    void HalfturretEntity::Initialize()
    {
        InitializeSpawn(true);
    }

    void HalfturretEntity::InitializeFromNetworkState(std::int32_t health, Vector3 position, bool grounded)
    {
        InitializeSpawn(false);
        // Placed where the authority has it, not walked there from its owner:
        // a Weavel in its alt form is often in no room part at all, and a
        // turret walked from nowhere is placed nowhere -- drawn by nobody and
        // still firing.
        Position = position;
        NodeRef = RequireReference(_scene).GetNodeRefByPosition(position);
        ApplyNetworkState(health, position, grounded);
    }

    void HalfturretEntity::ApplyNetworkState(std::int32_t health, Vector3 position, bool grounded)
    {
        const Vector3 previous = Position;
        _health = health;
        Position = position;
        _grounded = grounded;
        if (grounded) _ySpeed = 0.0F;
        NodeRef = RequireReference(_scene).UpdateNodeRef(NodeRef, previous, position);
        _closestNode.reset();
        UpdateLightSources(position);
    }

    void HalfturretEntity::InitializeSpawn(bool splitHealth)
    {
        ResetForSpawn();
        PlayerEntity& owner = RequireReference(_owner);
        SetRecolor(owner.Recolor());
        DynamicLightEntityBase::Initialize();
        const float minY = Fixed::ToFloat(owner.Values().MinPickupHeight);
        const Vector3 position = TypeExtensions::AddY(owner.Position, minY + 0.45F);
        const Vector3 facing(owner.Field70(), 0.0F, owner.Field74());
        Transform = GetTransformMatrix(facing, UnitY, position);
        _aimVector = facing;
        NodeRef = owner.NodeRef;
        const std::int32_t health = owner.Health();
        if (splitHealth && health > 1)
        {
            _health = health / 2;
            owner.SetHealth(health - _health);
        }
        else if (splitHealth)
        {
            _health = 1;
        }
        _grounded = TypeExtensions::TestFlag(owner.Flags1(), PlayerFlags1::Standing);
        MphRead::EquipInfo& equipInfo = RequireReference(_equipInfo);
        equipInfo.Beams = RequireReference(owner.EquipInfo()).Beams;
        equipInfo.Weapon = ReadOnlyListAt(RequireReference(Weapons::Current), 3);
        _models[0].SetAnimation(1, AnimFlags::NoLoop);
        _light1Vector = owner.Light1Vector();
        _light1Color = owner.Light1Color();
        _light2Vector = owner.Light2Vector();
        _light2Color = owner.Light2Color();
        _scanId = PlayerEntity::ScanIds.at(static_cast<std::size_t>(Hunter::Weavel)).at(1);
    }

    void HalfturretEntity::GetVectors(Vector3& position, Vector3& up, Vector3& facing)
    {
        position = Position;
        up = UnitY;
        facing = FacingVector();
    }

    void HalfturretEntity::Reposition(Vector3 offset, Formats::Culling::NodeRef nodeRef)
    {
        Position = static_cast<Vector3>(Position) + offset;
        _target.reset();
        _targetTimer = 0;
        NodeRef = nodeRef;
    }

    void HalfturretEntity::OnTakeDamage(std::shared_ptr<EntityBase> attacker, std::uint32_t damage)
    {
        _target = std::move(attacker);
        _targetTimer = 30;
        _cooldownFactorRaw = Mods::Combat::HalfturretFireRate::AfterDamage(
            _cooldownFactorRaw, damage);
    }

    void HalfturretEntity::OnFrozen()
    {
        if (_timeSinceFrozen > 60)
        {
            _freezeTimer = 75;
        }
        else if (_freezeTimer < 15)
        {
            _freezeTimer = 15;
        }
    }

    void HalfturretEntity::OnSetOnFire()
    {
        _burnTimer = 150;
        if (_burnEffect != nullptr)
        {
            RequireReference(_scene).UnlinkEffectEntry(_burnEffect);
            _burnEffect.reset();
        }
        Vector3 facing = FacingVector();
        facing.Y = 0.0F;
        _burnEffect = RequireReference(_scene).SpawnEffectGetEntry(
            187, facing, UnitY, Position);
        if (_burnEffect != nullptr)
        {
            _burnEffect->SetElementExtension(true);
        }
    }

    bool HalfturretEntity::Process()
    {
        if (_health == 0
            || !TypeExtensions::TestFlag(
                RequireReference(_owner).Flags2(), PlayerFlags2::Halfturret))
        {
            return false;
        }
        // Every turret observes the same scene phase, independent of input resets.
        if (!Mods::Gameplay::NativeGameplayClock::IsNativeTick(
            RequireReference(_scene).FrameCount()))
        {
            QueueEnergyHudMessage();
            return true;
        }
        // Halfturret precedes Player in the scene's entity list. Advance the
        // owner's native clocks once, before either entity's decision reads them.
        RequireReference(_owner).AdvanceNativeWeaponTimers();
        const bool active = ProcessNativeGameplay();
        QueueEnergyHudMessage();
        return active;
    }

    bool HalfturretEntity::ProcessNativeGameplay()
    {
        PlayerEntity& owner = RequireReference(_owner);
        if (_burnTimer > 0)
        {
            --_burnTimer;
            if (_burnTimer % 8 == 0)
            {
                owner.TakeDamage(1,
                    DamageFlags::NoSfx | DamageFlags::Burn | DamageFlags::NoDmgInvuln | DamageFlags::Halfturret,
                    std::nullopt, owner.BurnedBy().get());
            }
            if (_burnEffect != nullptr)
            {
                Vector3 facing = FacingVector();
                facing = Vector3(facing.X, 0.0F, facing.Z);
                _burnEffect->Transform(facing, UnitY, Position);
            }
        }
        else if (_burnEffect != nullptr)
        {
            RequireReference(_scene).UnlinkEffectEntry(_burnEffect);
            _burnEffect.reset();
        }
        if (_freezeTimer == 0)
        {
            (void)DynamicLightEntityBase::Process();
            if (_targetTimer > 0)
            {
                --_targetTimer;
            }
            else
            {
                _target.reset();
            }
            _cooldownFactorRaw = Mods::Combat::HalfturretFireRate::Recover(_cooldownFactorRaw);
            if (_target != nullptr
                && !Mods::Combat::Extensions::HalfturretEnemyExtension::KeepTarget(
                    RequireReference(_scene), owner, *_target, Position))
            {
                _target.reset();
            }
            if (_target == nullptr)
            {
                float minDistSqr = 15.0F * 15.0F;
                auto enumerator = RequireReference(_scene).GetPlayerEntities().GetEnumerator();
                while (enumerator.MoveNext())
                {
                    std::shared_ptr<PlayerEntity> playerValue = enumerator.Current();
                    if (playerValue == _owner)
                    {
                        continue;
                    }
                    PlayerEntity& player = RequireReference(playerValue);
                    if (player.Health() == 0
                        || Mods::Multiplayer::TeamRules::AreAllies(player.TeamIndex(), owner.TeamIndex())
                        || player.CurAlpha() < 6.0F / 31.0F)
                    {
                        continue;
                    }
                    const Vector3 between = static_cast<Vector3>(player.Position) - static_cast<Vector3>(Position);
                    const float distSqr = between.X * between.X + between.Y * between.Y + between.Z * between.Z;
                    if (distSqr < minDistSqr)
                    {
                        minDistSqr = distSqr;
                        _target = playerValue;
                    }
                }
            }
            // The ROM hunter scan above retains priority; Adventure enemies
            // are supplied by an explicit extension at the same native tick.
            if (_target == nullptr)
            {
                _target = Mods::Combat::Extensions::HalfturretEnemyExtension::FindTarget(
                    RequireReference(_scene), owner, Position);
            }
            if (_target != nullptr)
            {
                const Vector3 muzzlePos = TypeExtensions::AddY(Position, 0.4F);
                const std::int32_t encounter = ManagedAt(
                    GameState::EncounterState(), owner.SlotIndex());
                if (owner.IsBot() && GameState::SinglePlayer()
                    && (encounter == 1 || encounter == 3 || encounter == 4))
                {
                    if (_cooldownTimer > 0)
                    {
                        --_cooldownTimer;
                    }
                    else
                    {
                        _cooldownTimer = 65;
                    }
                }
                else
                {
                    _cooldownTimer = 1;
                }
                (void)UpdateAim(muzzlePos,
                    Mods::Combat::Extensions::HalfturretEnemyExtension::AimPosition(*_target),
                    _equipInfo, _aimVector);
                if (owner.NativeTimeSinceShot() >= NativeShotThreshold() && _cooldownTimer < 60)
                {
                    if (owner.IsBot() && GameState::SinglePlayer()
                        && (encounter == 1 || encounter == 3 || encounter == 4))
                    {
                        RequireReference(_equipInfo).UnchargedDamage(3);
                        RequireReference(_equipInfo).HeadshotDamage(3);
                        RequireReference(_equipInfo).SplashDamage(3);
                        RequireReference(_equipInfo).MinChargeSplashDamage(3);
                        RequireReference(_equipInfo).ChargedSplashDamage(3);
                        RequireReference(_equipInfo).DmgDirTypes[0] = 0;
                        RequireReference(_equipInfo).DmgDirTypes[1] = 0;
                    }
                    const BeamSpawnFlags flags = owner.DoubleDamage()
                        ? BeamSpawnFlags::DoubleDamage : BeamSpawnFlags::None;
                    const BeamResultFlags result = BeamProjectileEntity::Spawn(
                        SharedFrom<EntityBase>(this),
                        _equipInfo, muzzlePos, _aimVector, flags, NodeRef, _scene);
                    if (result != BeamResultFlags::NoSpawn)
                    {
                        _models[0].SetAnimation(0, AnimFlags::NoLoop);
                        owner.SetTimeSinceShot(0);
                    }
                }
            }
        }
        else
        {
            --_freezeTimer;
            _timeSinceFrozen = 0;
        }
        if (_timeSinceFrozen != std::numeric_limits<std::uint16_t>::max())
        {
            ++_timeSinceFrozen;
        }
        if (_timeSinceDamage != std::numeric_limits<std::uint16_t>::max())
        {
            ++_timeSinceDamage;
        }
        if (!_grounded)
        {
            const Vector3 prevPos = Position;
            _ySpeed -= 0.02F;
            Position = TypeExtensions::AddY(Position, _ySpeed);
            ManagedArray<Formats::CollisionResult> results(1);
            if (Formats::CollisionDetection::CheckSphereBetweenPoints(
                prevPos, Position, 0.45F, 1, false, Formats::TestFlags::None,
                _scene, &results) > 0)
            {
                const Formats::CollisionResult result = results[0];
                const Vector3 plane = result.Plane.Xyz();
                float dot = Vector3::Dot(Position, plane) - result.Plane.W;
                dot = 0.45F - dot;
                Position = Vector3(
                    result.Position.X + plane.X * dot,
                    result.Position.Y + plane.Y * dot,
                    result.Position.Z + plane.Z * dot);
                _ySpeed = 0.0F;
                _grounded = true;
            }
            UpdateLightSources(Position);
            NodeRef = RequireReference(_scene).UpdateNodeRef(NodeRef, prevPos, Position);
            _closestNode.reset();
        }
        assert(RequireReference(_scene).Room() != nullptr);
        const std::shared_ptr<RoomEntity> room = RequireReference(_scene).Room();
        if (Position.Y < RequireReference(room).Meta().KillHeight)
        {
            Die();
        }
        return true;
    }

    void HalfturretEntity::QueueEnergyHudMessage()
    {
        // This one-frame HUD message must be refreshed at 60 Hz, independently
        // of the native 30 Hz timers, combat decisions and physics.
        if (_health == 0 || _owner != PlayerEntity::Main()) return;
        std::string message = Text::Strings::GetHudMessage(233);
        const std::string replacement = ::MphRead::NativeRuntime::ToString(_health);
        std::size_t pos = 0;
        while ((pos = message.find("%d", pos)) != std::string::npos)
        {
            message.replace(pos, 2, replacement);
            pos += replacement.size();
        }
        RequireReference(_owner).QueueHudMessage(128, 150, 1.0F / 1000.0F, 0, message, true);
    }

    void HalfturretEntity::ResetGroundedState()
    {
        _grounded = false;
    }

    bool HalfturretEntity::UpdateAim(
        Vector3 muzzlePos, Vector3 targetPos,
        const std::shared_ptr<MphRead::EquipInfo>& equipInfo, Vector3& aimVector)
    {
        MphRead::EquipInfo& equip = RequireReference(equipInfo);
        WeaponInfo& weapon = RequireReference(equip.Weapon);
        float chargePct = 0.0F;
        if (TypeExtensions::TestFlag(weapon.Flags, WeaponFlags::CanCharge)
            && equip.ChargeLevel >= weapon.MinCharge * 2)
        {
            chargePct = (equip.ChargeLevel - weapon.MinCharge * 2)
                / (weapon.FullCharge * 2 - weapon.MinCharge * 2);
        }
        aimVector = targetPos - muzzlePos;
        const float hMagSqr = aimVector.X * aimVector.X + aimVector.Z * aimVector.Z;
        const float hMag = std::sqrt(hMagSqr);
        const float uncSpeed = Fixed::ToFloat(weapon.UnchargedSpeed);
        const float speed = (Fixed::ToFloat(weapon.MinChargeSpeed) - uncSpeed) * chargePct;
        const float uncGravity = Fixed::ToFloat(weapon.UnchargedGravity);
        const float gravity = (Fixed::ToFloat(weapon.MinChargeGravity) - uncGravity) * chargePct;
        float v23 = hMagSqr * (uncGravity + gravity)
            / ((uncSpeed + speed) * (uncSpeed + speed));
        const float v24 = v23 / 2.0F;
        bool result = true;
        if (v24 >= 1.0F / 4096.0F || v24 <= -1.0F / 4096.0F)
        {
            if ((Fixed::ToInt(v23) & 1) == 1)
            {
                v23 -= 1.0F / 4096.0F;
            }
            const float v26 = hMagSqr - 4.0F * v24 * (v24 - aimVector.Y);
            if (v26 > 0.0F)
            {
                aimVector.Y = (std::sqrt(v26) - hMag) / v23 * hMag;
            }
            else if (v26 > -1.0F / 4096.0F)
            {
                aimVector.Y = -hMag / v23 * hMag;
            }
            else
            {
                aimVector.Y = hMag;
                result = false;
            }
        }
        if (!IsZero(aimVector))
        {
            aimVector = aimVector.Normalized();
        }
        else
        {
            aimVector = UnitX;
        }
        return result;
    }

    void HalfturretEntity::Die()
    {
        PlayerEntity& owner = RequireReference(_owner);
        owner.OnHalfturretDied();
        if (_health > 0)
        {
            _health = 0;
            RequireReference(_scene).SpawnEffect(216, UnitX, UnitY, Position);
        }
    }

    void HalfturretEntity::Destroy()
    {
        if (_burnEffect != nullptr)
        {
            RequireReference(_scene).UnlinkEffectEntry(_burnEffect);
            _burnEffect.reset();
        }
        DynamicLightEntityBase::Destroy();
    }

    void HalfturretEntity::GetDrawInfo()
    {
        if (!IsVisible(NodeRef))
        {
            return;
        }
        ModelInstance& inst = _models[0];
        Model& model = RequireReference(inst.Model());
        const std::shared_ptr<AnimationInfo>& animInfo = inst.AnimInfo;
        PlayerEntity& owner = RequireReference(_owner);
        if (_timeSinceDamage < owner.Values().DamageFlashTime)
        {
            SetPaletteOverride(Metadata::RedPalette);
        }
        Matrix4 root = GetTransformMatrix(FacingVector(), UnitY);
        RequireReference(_baseNodeParent).AnimIgnoreChild = true;
        model.AnimateNodes2(0, false, root, One, animInfo);
        RequireReference(_baseNodeParent).AnimIgnoreChild = false;
        RequireReference(_baseNode).BeforeTransform = GetTransformMatrix(
            _aimVector, UnitY, RequireReference(_baseNodeParent).Animation.Row3().Xyz());
        RequireReference(_baseNode).AnimIgnoreParent = true;
        model.AnimateNodes2(RequireReference(_baseNodeParent).ChildIndex,
            false, Identity(), One, animInfo);
        RequireReference(_baseNode).AnimIgnoreParent = false;
        RequireReference(_baseNode).BeforeTransform.reset();
        root = Translation(TypeExtensions::AddY(Position, -0.45F));
        for (std::size_t i = 0; i < RequireReference(model.Nodes).size(); ++i)
        {
            Node& node = RequireReference((*model.Nodes)[i]);
            node.Animation *= root;
        }
        model.UpdateMatrixStack();
        UpdateMaterials(inst, Recolor());
        GetDrawItems(inst, 0);
        SetPaletteOverride(std::nullopt);
        if (_freezeTimer > 0)
        {
            _useRoomLights = true;
            const float radius = 0.65F;
            Matrix4 transform = CreateScale(Vector3(radius, radius, radius));
            transform.M41 = Position.X;
            transform.M42 = Position.Y;
            transform.M43 = Position.Z;
            UpdateTransforms(RequireReference(_altIceModel), transform, 0);
            GetDrawItems(RequireReference(_altIceModel), 1);
            _useRoomLights = false;
        }
    }

    std::optional<std::int32_t> HalfturretEntity::GetBindingOverride(
        ModelInstance& inst, Material& material, std::int32_t index)
    {
        PlayerEntity& owner = RequireReference(_owner);
        if (owner.DoubleDamage() && material.Lighting > 0)
        {
            return owner.DoubleDmgBindingId();
        }
        return DynamicLightEntityBase::GetBindingOverride(inst, material, index);
    }

    Vector3 HalfturretEntity::GetEmission(
        ModelInstance& inst, Material& material, std::int32_t index)
    {
        if (RequireReference(_owner).DoubleDamage() && material.Lighting > 0)
        {
            return Metadata::EmissionGray;
        }
        return DynamicLightEntityBase::GetEmission(inst, material, index);
    }

    Matrix4 HalfturretEntity::GetTexcoordMatrix(
        ModelInstance& inst, Material& material, std::int32_t materialId,
        Node& node, std::int32_t recolor)
    {
        PlayerEntity& owner = RequireReference(_owner);
        if (owner.DoubleDamage() && material.Lighting > 0 && node.BillboardMode == BillboardMode::None)
        {
            const std::shared_ptr<ModelInstance> doubleDamageInstance
                = owner.DoubleDamageModel();
            const std::shared_ptr<Model> doubleDamageModel
                = RequireReference(doubleDamageInstance).Model();
            const auto& recolors = RequireReference(
                RequireReference(doubleDamageModel).Recolors);
            const std::shared_ptr<MphRead::Recolor> doubleDamageRecolor
                = ReadOnlyListAt(recolors, 0);
            const auto& textures = RequireReference(
                RequireReference(doubleDamageRecolor).Textures);
            const Texture texture = ReadOnlyListAt(textures, 0);
            Matrix4 texgenMatrix = Identity();
            const Vector3 modelScale = RequireReference(inst.Model()).Scale;
            if (modelScale.X != 1.0F || modelScale.Y != 1.0F || modelScale.Z != 1.0F)
            {
                texgenMatrix = CreateScale(modelScale) * texgenMatrix;
            }
            Matrix4 product = texgenMatrix;
            product.M12 *= -1.0F;
            product.M13 *= -1.0F;
            product.M22 *= -1.0F;
            product.M23 *= -1.0F;
            product.M32 *= -1.0F;
            product.M33 *= -1.0F;
            const std::uint64_t frame = RequireReference(_scene).LiveFrames() / 2U;
            const std::uint64_t zTerm = 781874935307ULL * (53248ULL * frame);
            const std::uint64_t zValue = 16ULL * ((zTerm >> 32) + 2048ULL);
            const std::int32_t zInt = UInt64ToInt32(zValue);
            const float rotZ = static_cast<float>(zInt >> 20) * (360.0F / 4096.0F);
            const std::uint64_t yTerm = 781874935307ULL * (26624ULL * frame)
                + 0x80000000000ULL;
            const std::uint64_t yValue = 16ULL * (yTerm >> 32);
            const std::int32_t yInt = UInt64ToInt32(yValue);
            const float rotY = static_cast<float>(yInt >> 20) * (360.0F / 4096.0F);
            constexpr float degreesToRadians = 0.01745329251994329576923690768489F;
            Matrix4 rot = RotationZ(rotZ * degreesToRadians);
            rot *= RotationY(rotY * degreesToRadians);
            product = rot * product;
            const float scalar = 1.0F / static_cast<float>(texture.Width / 2);
            product.M11 *= scalar;
            product.M12 *= scalar;
            product.M13 *= scalar;
            product.M14 *= scalar;
            product.M21 *= scalar;
            product.M22 *= scalar;
            product.M23 *= scalar;
            product.M24 *= scalar;
            product.M31 *= scalar;
            product.M32 *= scalar;
            product.M33 *= scalar;
            product.M34 *= scalar;
            product.M41 *= scalar;
            product.M42 *= scalar;
            product.M43 *= scalar;
            product.M44 *= scalar;
            product.M11 *= 16.0F;
            product.M12 *= 16.0F;
            product.M13 *= 16.0F;
            product.M14 *= 16.0F;
            product.M21 *= 16.0F;
            product.M22 *= 16.0F;
            product.M23 *= 16.0F;
            product.M24 *= 16.0F;
            product.M31 *= 16.0F;
            product.M32 *= 16.0F;
            product.M33 *= 16.0F;
            product.M34 *= 16.0F;
            return product;
        }
        return DynamicLightEntityBase::GetTexcoordMatrix(inst, material, materialId, node, recolor);
    }
}
