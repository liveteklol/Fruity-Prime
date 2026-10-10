#include "PlayerEntityNetAim.hpp"
#include "../../Entities/Players/HalfturretEntity.hpp"

#include "../Input/GamepadOptions.hpp"
#include "../../Entities/EntityBase.hpp"
#include "../../Entities/Players/PlayerCamera.hpp"
#include "../../Entities/PlayerSpawnEntity.hpp"
#include "../../Formats/Model.hpp"
#include "../../Entities/CamSeq/CameraSequence.hpp"
#include "../../GameState.hpp"
#include "../../Metadata/Metadata.hpp"
#include "../Input/GamepadInput.hpp"
#include "../SpectatorMode.hpp"
#include "NetDamage.hpp"
#include "NetHooks.hpp"
#include "NetLog.hpp"
#include "NetPlayerBridge.hpp"
#include "NetSession.hpp"
#include "NetTestScript.hpp"
#include "../../Scene.hpp"
#include "../../NativeRuntime/System/Managed.hpp"
#include "../../NativeRuntime/OpenTK/Mathematics.hpp"
#include "../../Formats/Types.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

using ::MphRead::NativeRuntime::MathClamp;
using ::OpenTK::Mathematics::AddY;
using ::OpenTK::Mathematics::LengthSquared;
using ::OpenTK::Mathematics::Multiply;

namespace
{
    using Vector3 = OpenTK::Mathematics::Vector3;
    namespace Detail = MphRead::Entities::PlayerEntityNetAimDetail;

    constexpr std::uint8_t LoadFlagSpawned = 0x80U;
    constexpr std::uint32_t PlayerFlagMorphing = 0x800U;
    constexpr std::uint32_t PlayerFlagUnmorphing = 0x1000U;
    constexpr std::uint32_t PlayerFlagNoAimInput = 0x1000000U;
    constexpr std::uint32_t PlayerFlagShooting = 0x4U;
    constexpr std::uint32_t PlayerFlagSpectating = 0x40000U;

    constexpr std::uint32_t WeaponFlagPartialCharge = 0x100U;
    constexpr std::uint32_t WeaponFlagCanCharge = 0x200U;
    constexpr std::uint32_t WeaponFlagCanZoom = 0x800U;

    constexpr std::uint16_t AnimFlagEnded = 0x10U;

    [[nodiscard]] constexpr Vector3 NegativeUnitZ() noexcept
    {
        return Vector3(0.0F, 0.0F, -1.0F);
    }

    [[nodiscard]] constexpr bool HasFlag(std::uint32_t value, std::uint32_t flag) noexcept
    {
        return (value & flag) == flag;
    }

    [[nodiscard]] std::string BoolText(bool value)
    {
        return value ? "True" : "False";
    }

    [[nodiscard]] std::string BeamTypeText(MphRead::BeamType value)
    {
        return ::MphRead::ToString(value);
    }

    [[nodiscard]] std::string AfflictionText(MphRead::Affliction value)
    {
        const std::uint8_t bits = static_cast<std::uint8_t>(value);
        if (bits == 0U)
        {
            return "None";
        }
        if ((bits & ~0x7U) != 0U)
        {
            return std::to_string(static_cast<std::uint32_t>(bits));
        }
        std::string text;
        if ((bits & 0x1U) != 0U)
        {
            text = "Freeze";
        }
        if ((bits & 0x2U) != 0U)
        {
            if (!text.empty()) text += ", ";
            text += "Disrupt";
        }
        if ((bits & 0x4U) != 0U)
        {
            if (!text.empty()) text += ", ";
            text += "Burn";
        }
        return text;
    }

    [[nodiscard]] std::string PlayerAnimationText(std::int32_t value)
    {
        switch (value)
        {
        case -1: return "None";
        case 0: return "Morph";
        case 1: return "Flourish";
        case 2: return "WalkForward";
        case 3: return "Unmorph";
        case 4: return "DamageBack";
        case 5: return "DamageFront";
        case 6: return "DamageLeft";
        case 7: return "DamageRight";
        case 8: return "Idle";
        case 9: return "LandNeutral";
        case 10: return "LandLeft";
        case 11: return "LandRight";
        case 12: return "JumpNeutral";
        case 13: return "JumpBack";
        case 14: return "JumpForward";
        case 15: return "JumpLeft";
        case 16: return "JumpRight";
        case 17: return "Unused17";
        case 18: return "WalkBackward";
        case 19: return "Spawn";
        case 20: return "WalkLeft";
        case 21: return "WalkRight";
        case 22: return "Turn";
        case 23: return "Charge";
        case 24: return "ChargeShoot";
        case 25: return "Shoot";
        default: return std::to_string(value);
        }
    }
}

namespace MphRead::Entities
{
    void PlayerEntity::ModRecordNetworkPosition(std::uint32_t frame)
    {
        const std::int32_t count = std::min(
            _networkPositionHistoryCount, NetworkHistoryLength - 1);
        for (std::int32_t i = count; i > 0; --i)
        {
            _networkPositionHistory[static_cast<std::size_t>(i)]
                = _networkPositionHistory[static_cast<std::size_t>(i - 1)];
            _networkPositionFrames[static_cast<std::size_t>(i)]
                = _networkPositionFrames[static_cast<std::size_t>(i - 1)];
        }
        _networkPositionHistory[0] = static_cast<Vector3>((*this).Position);
        _networkPositionFrames[0] = frame;
        _networkPositionHistoryCount = std::min(count + 1, NetworkHistoryLength);
    }

    bool PlayerEntity::ModGetNetworkPosition(
        std::uint32_t frame, OpenTK::Mathematics::Vector3& position)
    {
        for (std::int32_t i = 0; i < _networkPositionHistoryCount; ++i)
        {
            const std::size_t index = static_cast<std::size_t>(i);
            if (_networkPositionFrames[index] <= frame)
            {
                position = _networkPositionHistory[index];
                return true;
            }
        }
        position = OpenTK::Mathematics::Vector3::Zero;
        return false;
    }

    OpenTK::Mathematics::Vector3 PlayerEntity::ModGunVector() const
    {
        return (*this)._gunVec1;
    }

    void PlayerEntity::ModRefreshNetworkAim()
    {
        if (!Mods::Network::NetSession::Active())
        {
            return;
        }
        const std::int32_t slotForLocal = (*this).SlotIndex();
        const std::int32_t localSlot = Mods::Network::NetHooks::LocalSlot();
        if (slotForLocal == localSlot)
        {
            return;
        }
        const std::int32_t slotForValid = (*this).SlotIndex();
        if (_isBot || slotForValid < 0 || static_cast<std::size_t>(slotForValid) >= Mods::Network::NetSession::RemoteIntents.size())
        {
            return;
        }
        // Packet admission belongs to ApplyModAim. Even when an intent expires,
        // the previously committed gun direction follows the current camera.
        // Rotation/Follow was admitted before movement. Camera has now moved:
        // only reproject the committed gun direction, without another Follow
        // or an invented camera position overwriting UpdateCamera's result.
        ProjectAimTarget();
    }

    void PlayerEntity::ModSetAim(OpenTK::Mathematics::Vector3 aim)
    {
        if (!std::isfinite(aim.X) || !std::isfinite(aim.Y) || !std::isfinite(aim.Z)
            || !std::isfinite(LengthSquared(aim)) || !(LengthSquared(aim) > 0.0001F))
        {
            return;
        }

        if (Mods::Network::NetSession::Active())
        {
            const std::int32_t slot = (*this).SlotIndex();
            const std::int32_t localSlot = Mods::Network::NetHooks::LocalSlot();
            if (slot != localSlot)
            {
                Vector3 cameraPosition{};
                const bool field6D0 = (*this)._field6D0;
                if (field6D0)
                {
                    cameraPosition = static_cast<Vector3>((*this).Position);
                }
                else
                {
                    const Vector3 position = static_cast<Vector3>((*this).Position);
                    const std::int32_t aimYOffset = (*this).Values().AimYOffset;
                    cameraPosition = AddY(
                        position, MphRead::Fixed::ToFloat(aimYOffset));
                }
                ((*this).CameraInfo()->Position = cameraPosition);
            }
        }

        const Vector3 gun = aim.Normalized();
        _aimTrace.Count(Mods::Input::AimOperation::Normalize);
        ((*this)._gunVec1 = gun);
        const float flat = std::sqrt(gun.X * gun.X + gun.Z * gun.Z);
        constexpr float RadiansToDegrees = ::OpenTK::Mathematics::MathHelper::RadToDeg;
        const float aimY = std::clamp(
            std::atan2(gun.Y, flat) * RadiansToDegrees, -85.0F, 85.0F);
        ((*this)._aimY = aimY);
        ProjectAimTarget();
        (*this).UpdateAimFacing();
    }

    Formats::Culling::NodeRef PlayerEntity::ModWalkNodeRef(
        MphRead::Scene& scene,
        Formats::Culling::NodeRef current,
        OpenTK::Mathematics::Vector3 previous,
        OpenTK::Mathematics::Vector3 position)
    {
        if (current == Formats::Culling::NodeRef::None
            || LengthSquared(position - previous) >= _nodeWalkStepMax * _nodeWalkStepMax)
        {
            return Formats::Culling::NodeRef::None;
        }

        Formats::Culling::NodeRef walked
            = scene.UpdateNodeRef(current, previous, position);
        if (walked.PartIndex == -1
            || !scene.PartCouldContain(walked.PartIndex, position))
        {
            return Formats::Culling::NodeRef::None;
        }
        return walked;
    }

    void PlayerEntity::ModPlaceAt(OpenTK::Mathematics::Vector3 position)
    {
        const Vector3 previous = static_cast<Vector3>((*this).Position);
        ((*this).Position = position);
        (*this).SetPrevPosition(position);
        ModRefreshNodeRef(previous);
    }

    bool PlayerEntity::ModPlacementBelongsHere(OpenTK::Mathematics::Vector3 position)
    {
        bool any = false;
        return ModNearestSpawn(position, any) != nullptr
            // A room with no spawn points at all is not a room this rule
            // can say anything about, so it says nothing.
            || !any;
    }

    std::shared_ptr<PlayerSpawnEntity> PlayerEntity::ModNearestSpawn(OpenTK::Mathematics::Vector3 position, bool& any)
    {
        // Generous: the authority may have published a frame or two after
        // the placement, by which time the player has begun to fall to
        // the floor, and a spawn point sits above it.
        constexpr float reach = 12.0F;
        std::shared_ptr<PlayerSpawnEntity> nearest{};
        float nearestDist = reach * reach;
        auto enumerator = (*(*this)._scene).GetPlayerSpawnEntities().GetEnumerator();
        while (enumerator.MoveNext())
        {
            const std::shared_ptr<PlayerSpawnEntity> spawn = enumerator.Current();
            any = true;
            const Vector3 between = static_cast<Vector3>(spawn->Position) - position;
            const float dist = LengthSquared(between);
            if (dist <= nearestDist)
            {
                nearestDist = dist;
                nearest = spawn;
            }
        }
        return nearest;
    }

    std::optional<OpenTK::Mathematics::Vector3> PlayerEntity::ModSpawnFacingAt(OpenTK::Mathematics::Vector3 position)
    {
        bool any = false;
        const std::shared_ptr<PlayerSpawnEntity> spawn = ModNearestSpawn(position, any);
        return spawn != nullptr ? std::optional<Vector3>(spawn->FacingVector()) : std::nullopt;
    }

    void PlayerEntity::ModSetSpawnFacing(OpenTK::Mathematics::Vector3 facing)
    {
        ModSetFacing(facing);
        ModSetAim(facing);
    }

    void PlayerEntity::ModRefreshNodeRef(OpenTK::Mathematics::Vector3 previousPosition)
    {
        (*this)._volume = CollisionVolume::Move((*this)._volumeUnxf, (*this).Position);
        MphRead::Scene& scene = (*(*this)._scene);
        const Formats::Culling::NodeRef currentForWalk = (*this).NodeRef;
        const Vector3 positionForWalk = static_cast<Vector3>((*this).Position);
        Formats::Culling::NodeRef walked = ModWalkNodeRef(
            scene, currentForWalk, previousPosition, positionForWalk);
        if (walked != Formats::Culling::NodeRef::None)
        {
            ((*this).NodeRef = walked);
            _modNodeUnresolved = false;
            return;
        }

        const Vector3 directPosition = static_cast<Vector3>((*this).Position);
        Formats::Culling::NodeRef found
            = scene.GetNodeRefByPosition(directPosition);
        if (found.PartIndex == -1)
        {
            found = scene.GetNodeRefByPosition((*this)._volume.SpherePosition);
        }
        if (found.PartIndex == -1)
        {
            const Vector3 upperPosition = static_cast<Vector3>((*this).Position);
            found = scene.GetNodeRefByPosition(upperPosition + Multiply(::OpenTK::Mathematics::Vector3::UnitY, 0.5F));
        }
        if (found.PartIndex == -1)
        {
            const Vector3 lowerPosition = static_cast<Vector3>((*this).Position);
            found = scene.GetNodeRefByPosition(lowerPosition - Multiply(::OpenTK::Mathematics::Vector3::UnitY, 0.5F));
        }
        if (found.PartIndex != -1)
        {
            ((*this).NodeRef = found);
            _modNodeUnresolved = false;
            return;
        }

        _modNodeUnresolved = true;
        NativeRuntime::IncrementInPlace(Mods::Network::NetPlayerBridge::NodeLookupsUnresolved);
        const Formats::Culling::NodeRef currentForCheck = (*this).NodeRef;
        if (currentForCheck != Formats::Culling::NodeRef::None)
        {
            const Formats::Culling::NodeRef currentForUpdate = (*this).NodeRef;
            const Vector3 updatePosition = static_cast<Vector3>((*this).Position);
            const Formats::Culling::NodeRef updated = scene.UpdateNodeRef(currentForUpdate, previousPosition, updatePosition);
            ((*this).NodeRef = updated);
        }
    }

    bool PlayerEntity::ModNodeUnresolved() const noexcept
    {
        return _modNodeUnresolved;
    }

    void PlayerEntity::ModLogCollisionRange()
    {
        if (!Mods::Network::NetLog::Enabled() || !Mods::Network::NetSession::Active())
        {
            return;
        }
        const std::int32_t slot = (*this).SlotIndex();
        const Vector3 previous = (*this).PrevPosition();
        const Vector3 position = static_cast<Vector3>((*this).Position);
        Mods::Network::NetLog::CollisionRange(slot, "pre-check", previous, position);
    }

    void PlayerEntity::ModSetFacing(OpenTK::Mathematics::Vector3 facing)
    {
        if (!(LengthSquared(facing) > 0.0001F))
        {
            return;
        }
        const Vector3 normalized = facing.Normalized();
        (*this)._facingVector = normalized;
        const Vector3 up = (*this)._upVector;
        const Vector3 position = static_cast<Vector3>((*this).Position);
        (*this).SetTransform(normalized, up, position);
    }

    void PlayerEntity::ModSetSpectating(bool value)
    {
        std::uint32_t flags = static_cast<std::uint32_t>((*this).Flags2());
        if (value)
        {
            flags |= PlayerFlagSpectating;
        }
        else
        {
            flags &= ~PlayerFlagSpectating;
        }
        (*this).SetFlags2(static_cast<PlayerFlags2>(flags));
    }

    bool PlayerEntity::ModInPlay() const
    {
        return (*this).Health() > 0
            && !::HasFlag(static_cast<std::uint32_t>((*this).Flags2()), PlayerFlagSpectating);
    }

    bool PlayerEntity::ModIsInPlay() const
    {
        return (static_cast<std::uint8_t>((*this).LoadFlags()) & LoadFlagSpawned) == LoadFlagSpawned
            && (*this).Health() > 0;
    }

    void PlayerEntity::ModNetSpawn(
        OpenTK::Mathematics::Vector3 position,
        OpenTK::Mathematics::Vector3 facing)
    {
        const Vector3 forward = LengthSquared(facing) > 0.0001F
            ? facing.Normalized()
            : NegativeUnitZ();
        const Vector3 up = ::OpenTK::Mathematics::Vector3::UnitY;
        MphRead::Scene& scene = (*(*this)._scene);
        const Formats::Culling::NodeRef nodeRef = ModSpawnNodeRef(scene, position);
        (*this).Spawn(position, forward, up, nodeRef, true);
    }

    Formats::Culling::NodeRef PlayerEntity::ModSpawnNodeRef(
        MphRead::Scene& scene, OpenTK::Mathematics::Vector3 position)
    {
        std::shared_ptr<EntityBase> closest{};
        float closestDist = 2.0F * 2.0F;
        auto enumerator = scene.Entities().GetEnumerator();
        while (enumerator.MoveNext())
        {
            const std::shared_ptr<EntityBase> entity = enumerator.Current();
            if (entity->Type != MphRead::EntityType::PlayerSpawn
                || entity->NodeRef == Formats::Culling::NodeRef::None)
            {
                continue;
            }
            const float dist
                = LengthSquared(static_cast<Vector3>(entity->Position) - position);
            if (dist < closestDist)
            {
                closestDist = dist;
                closest = entity;
            }
        }
        if (closest != nullptr)
        {
            return closest->NodeRef;
        }
        return scene.GetNodeRefByPosition(position);
    }

    std::pair<float, float> PlayerEntity::ModAimDeltaTowards(
        OpenTK::Mathematics::Vector3 target)
    {
        const Vector3 desired = ModAimVectorTowards(target);
        const Vector3 gun = (*this)._gunVec1;
        const float flatLength
            = std::sqrt(desired.X * desired.X + desired.Z * desired.Z);
        const float aimFlat
            = std::sqrt(gun.X * gun.X + gun.Z * gun.Z);
        if (flatLength < 0.001F || aimFlat < 0.001F)
        {
            return {0.0F, 0.0F};
        }

        const float tx = desired.X / flatLength;
        const float tz = desired.Z / flatLength;
        const float ax = gun.X / aimFlat;
        const float az = gun.Z / aimFlat;
        constexpr float RadiansToDegrees = ::OpenTK::Mathematics::MathHelper::RadToDeg;
        const float turn = -std::atan2(
            ax * tz - az * tx, ax * tx + az * tz) * RadiansToDegrees;
        const float targetPitch
            = std::atan2(desired.Y, flatLength) * RadiansToDegrees;
        const float currentPitch
            = std::atan2(gun.Y, aimFlat) * RadiansToDegrees;
        return {turn, targetPitch - currentPitch};
    }

    OpenTK::Mathematics::Vector3 PlayerEntity::ModAimVectorTowards(
        OpenTK::Mathematics::Vector3 target) const
    {
        const Vector3 eye = (*this).CameraInfo()->Position;
        const float aimDistance = MphRead::Fixed::ToFloat((*this).Values().AimDistance);
        const Vector3 muzzleForFrom = (*this)._muzzlePos;
        const Vector3 fromMuzzle = target - muzzleForFrom;
        if (LengthSquared(fromMuzzle) < 0.0001F || aimDistance <= 0.0F)
        {
            return target - eye;
        }

        const Vector3 direction = fromMuzzle.Normalized();
        const Vector3 muzzleForOffset = (*this)._muzzlePos;
        const Vector3 offset = muzzleForOffset - eye;
        const float b = Vector3::Dot(offset, direction);
        const float c = Vector3::Dot(offset, offset) - aimDistance * aimDistance;
        const float discriminant = b * b - c;
        if (discriminant < 0.0F)
        {
            return target - eye;
        }
        const float t = -b + std::sqrt(discriminant);
        const Vector3 muzzleForReturn = (*this)._muzzlePos;
        return muzzleForReturn + Multiply(direction, t) - eye;
    }

    OpenTK::Mathematics::Vector3 PlayerEntity::ModAimTarget() const
    {
        const Vector3 position = static_cast<Vector3>((*this).Position);
        const Vector3 offset = PlayerVolumes[static_cast<std::int32_t>((*this).Hunter())][0].SpherePosition;
        return position + offset;
    }

    void PlayerEntity::ModSetHunter(MphRead::Hunter hunter)
    {
        if (hunter != (*this).Hunter())
        {
            ((*this)._syluxBombs[0] = nullptr);
            ((*this)._syluxBombs[1] = nullptr);
            ((*this)._syluxBombs[2] = nullptr);
            (*this).SetSyluxBombCount(0);
        }
        (*this)._hunter = hunter;
    }

    bool PlayerEntity::ModDamageIndicatorActive() const
    {
        for (std::int32_t i = 0;
            i < static_cast<std::int32_t>((*this)._damageIndicatorTimers.size()); ++i)
        {
            if ((*this)._damageIndicatorTimers[i] > 0)
            {
                return true;
            }
        }
        return false;
    }

    void PlayerEntity::ModStartFormSwitch()
    {
        const bool switched = (*this).TrySwitchForms(true);
        const std::int32_t slot = (*this).SlotIndex();
        const std::string formState = ModFormState();
        Mods::Network::NetLog::Event(("slot " + std::to_string(slot)
            + " form switch requested -> " + BoolText(switched)
            + ", now " + formState));
    }

    void PlayerEntity::ModForceForm(bool altForm)
    {
        if (_hunter == Hunter::Weavel)
        {
            const bool active = TypeExtensions::TestFlag(_flags2, PlayerFlags2::Halfturret)
                && _halfturret && _halfturret->Health() > 0;
            ModForceWeavelState(altForm, altForm && (!_weavelAltLife || active));
            return;
        }
        if (altForm == (*this).IsAltForm())
        {
            // Unmorph changes the form bit before its animation ends. If
            // that animation stalls, UpdateForm must not run a second time:
            // it would shift the position by the collision-centre offset.
            // A stalled morph being cancelled back to biped also needs
            // ExitAltForm to remove Weavel's halfturret and other effects.
            if (IsMorphing() && !altForm)
            {
                ExitAltForm();
            }
            if (IsUnmorphing() && !altForm)
            {
                Mods::Network::NetLog::Event("slot " + std::to_string(SlotIndex())
                    + " stalled unmorph completed from " + ModFormState());
                if (IsMainPlayer() && Formats::CameraSequence::Current() != nullptr)
                {
                    Formats::CameraSequence::Current()->InitialCamInfo().NodeRef = NodeRef;
                }
                else
                {
                    ::MphRead::NativeRuntime::RequireReference(_cameraInfo).NodeRef = NodeRef;
                }
                _flags1 &= ~PlayerFlags1::Unmorphing;
                SetBipedAnimation(PlayerAnimation::Idle, AnimFlags::None);
                if (_burnTimer > 0)
                {
                    CreateBurnEffect();
                }
                if (_cameraType != CameraType::First)
                {
                    SwitchCamera(CameraType::First, _facingVector);
                }
            }
            else if (IsMorphing())
            {
                // The form was already applied; only the stale animation
                // flag remains. Preserve the alt model and camera.
                _flags1 &= ~PlayerFlags1::Morphing;
            }
            return;
        }
        const std::int32_t slot = (*this).SlotIndex();
        const std::string formState = ModFormState();
        Mods::Network::NetLog::Event(("slot " + std::to_string(slot)
            + " form forced to " + (altForm ? std::string("alt") : std::string("biped"))
            + " from " + formState));

        std::uint32_t flags = static_cast<std::uint32_t>((*this).Flags1());
        flags &= ~PlayerFlagMorphing;
        (*this).SetFlags1(static_cast<PlayerFlags1>(flags));
        flags = static_cast<std::uint32_t>((*this).Flags1());
        flags &= ~PlayerFlagUnmorphing;
        (*this).SetFlags1(static_cast<PlayerFlags1>(flags));

        (*this).UpdateForm(altForm);
        if (altForm)
        {
            const std::uint8_t altFormStrafe = (*this).Values().AltFormStrafe;
            const ::MphRead::Entities::CameraType cameraType = altFormStrafe != 0
                ? ::MphRead::Entities::CameraType::Third2
                : ::MphRead::Entities::CameraType::Third1;
            const float field70 = (*this)._field70;
            const float field74 = (*this)._field74;
            const Vector3 cameraFacing(field70, 0.0F, field74);
            (*this).SwitchCamera(cameraType, cameraFacing);
        }
        else
        {
            const Vector3 facing = (*this)._facingVector;
            (*this).SwitchCamera(::MphRead::Entities::CameraType::First, facing);
        }
    }

    void PlayerEntity::ModSetWeapon(MphRead::BeamType weapon)
    {
        if (weapon == (*this).CurrentWeapon()
            || static_cast<std::int32_t>(weapon)
                < static_cast<std::int32_t>(MphRead::BeamType::PowerBeam)
            || static_cast<std::int32_t>(weapon)
                > static_cast<std::int32_t>(MphRead::BeamType::OmegaCannon))
        {
            return;
        }
        ((*this)._availableWeapons[weapon] = true);
        ((*this)._availableCharges[weapon] = true);
        static_cast<void>((*this).TryEquipWeapon(weapon, true));
    }

    std::pair<std::int32_t, std::int32_t> PlayerEntity::ModAmmo() const
    {
        const std::int32_t ua = (*this)._ammo[0];
        const std::int32_t missiles = (*this)._ammo[1];
        return {ua, missiles};
    }

    // The owner's DS touch state, so this machine's ProcessAlt runs the same
    // touch roll and the same touch/shoulder boost arbitration the owner did.
    void PlayerEntity::ModSetReportedTouch(const Mods::Input::NativeTouchState::Reported& reported,
        std::uint16_t generation, std::uint16_t life)
    {
        if (SlotIndex() == Mods::Network::NetHooks::LocalSlot())
        {
            return;
        }
        _input.ApplyReportedTouch(reported, generation, life);
    }

    void PlayerEntity::ModSetShotState(std::int32_t chargeLevel, std::int32_t boostDamage, bool doubleDamage)
    {
        if (SlotIndex() == Mods::Network::NetHooks::LocalSlot())
        {
            // Never the machine's own player: this is its own state coming
            // back to it a round trip later.
            return;
        }
        ::MphRead::NativeRuntime::RequireReference(EquipInfo()).ChargeLevel = static_cast<std::uint16_t>(std::clamp(chargeLevel, 0, 0xFFFF));
        _boostDamage = static_cast<std::uint16_t>(std::clamp(boostDamage, 0, 0xFFFF));
        if (doubleDamage)
        {
            // Held up rather than counted down: the owner says so again
            // every couple of frames for as long as it lasts.
            if (_doubleDmgTimer < 8)
            {
                _doubleDmgTimer = 8;
            }
        }
        else
        {
            _doubleDmgTimer = 0;
        }
    }

    void PlayerEntity::ModSetAmmo(std::int32_t ua, std::int32_t missiles)
    {
        const std::int32_t uaMax = (*this)._ammoMax[0];
        ((*this)._ammo[0] = MathClamp(ua, 0, uaMax));
        const std::int32_t missileMax = (*this)._ammoMax[1];
        ((*this)._ammo[1] = MathClamp(missiles, 0, missileMax));
    }

    void PlayerEntity::ModSetZoom(bool zoomed)
    {
        const bool canZoom = ((*this).EquipInfo()->Weapon != nullptr)
            && (static_cast<std::uint32_t>((*this).EquipInfo()->Weapon->Flags) & WeaponFlagCanZoom) != 0;
        // Dead here is unzoomed, whatever the owner or a snapshot not yet
        // told of the kill still says: death unzooms, and nothing alive
        // brings it back before the next life.
        const bool wanted = zoomed && canZoom && (*this).Health() > 0;
        if ((*this).EquipInfo()->Zoomed != wanted)
        {
            (*this).UpdateZoom(wanted);
        }
    }

    void PlayerEntity::ModArmAffinityWeapon()
    {
        const MphRead::Hunter hunter = (*this).Hunter();
        const MphRead::BeamType beam = Weapons::GetAffinityBeam(hunter);
        ModArmWeapon(beam);
    }

    void PlayerEntity::ModArmWeapon(MphRead::BeamType beam)
    {
        if (static_cast<std::int32_t>(beam)
                < static_cast<std::int32_t>(MphRead::BeamType::PowerBeam)
            || static_cast<std::int32_t>(beam)
                > static_cast<std::int32_t>(MphRead::BeamType::OmegaCannon))
        {
            return;
        }
        ((*this)._availableWeapons[beam] = true);
        ((*this)._availableCharges[beam] = true);
        const std::int32_t ammoType
            = static_cast<std::int32_t>((*Weapons::Current)[static_cast<std::int32_t>(beam)]->AmmoType);
        const std::int32_t ammoMax = (*this)._ammoMax[ammoType];
        ((*this)._ammo[ammoType] = ammoMax);
        if ((*this).CurrentWeapon() != beam)
        {
            static_cast<void>((*this).TryEquipWeapon(beam, true));
        }
    }

    void PlayerEntity::ModArmZoomWeapon()
    {
        MphRead::BeamType beam = MphRead::BeamType::Imperialist;
        if ((static_cast<std::uint32_t>((*Weapons::Current)[static_cast<std::int32_t>(beam)]->Flags) & WeaponFlagCanZoom) == 0)
        {
            beam = MphRead::BeamType::Judicator;
        }
        ModArmWeapon(beam);
    }

    void PlayerEntity::ModApplyScriptAim(float deltaX, float deltaY)
    {
        (*this).UpdateAimY(deltaY);
        (*this).UpdateAimX(deltaX);
    }

    std::string PlayerEntity::ModWeaponState() const
    {
        const MphRead::BeamType currentForText = (*this).CurrentWeapon();
        const MphRead::BeamType equipBeam = (*this).EquipInfo()->Weapon->Beam;
        const std::uint16_t chargeLevel = (*this).EquipInfo()->ChargeLevel;
        const std::uint16_t minCharge = (*this).EquipInfo()->Weapon->MinCharge;
        const MphRead::BeamType currentForCharge = (*this).CurrentWeapon();
        const bool chargeable = (*this)._availableCharges[currentForCharge];
        const MphRead::Affliction affliction = (*(*this).EquipInfo()->Weapon->Afflictions)[1];
        const bool shooting = ::HasFlag(
            static_cast<std::uint32_t>((*this).Flags2()), PlayerFlagShooting);

        return BeamTypeText(currentForText)
            + " equip=" + BeamTypeText(equipBeam)
            + " charge=" + std::to_string(chargeLevel)
            + "/" + std::to_string(static_cast<std::int32_t>(minCharge) * 2)
            + " chargeable=" + BoolText(chargeable)
            + " affliction=" + AfflictionText(affliction)
            + " shooting=" + BoolText(shooting);
    }

    std::int32_t PlayerEntity::ModChargeLevel() const
    {
        return static_cast<std::int32_t>((*this).EquipInfo()->ChargeLevel);
    }

    bool PlayerEntity::ModChargeReady() const
    {
        const std::uint32_t flags = static_cast<std::uint32_t>((*this).EquipInfo()->Weapon->Flags);
        if ((flags & WeaponFlagCanCharge) == 0)
        {
            return false;
        }
        const std::int32_t needed
            = (flags & WeaponFlagPartialCharge) != 0
                ? static_cast<std::int32_t>((*this).EquipInfo()->Weapon->MinCharge) * 2
                : static_cast<std::int32_t>((*this).EquipInfo()->Weapon->FullCharge) * 2;
        return static_cast<std::int32_t>((*this).EquipInfo()->ChargeLevel) >= needed;
    }

    bool PlayerEntity::ModFrozen() const
    {
        return (*this)._frozenTimer > 0;
    }

    void PlayerEntity::ModSetFrozen(bool frozen)
    {
        if ((*this).Health() <= 0)
        {
            return;
        }

        const std::uint16_t timer = (*this)._frozenTimer;
        if (frozen)
        {
            if (timer == 0)
            {
                (*this)._soundSource.PlaySfx(SfxId::SHOTGUN_FREEZE);
                if ((*this).IsMainPlayer())
                {
                    (*this).ResetCombatVisor();
                    ((*this)._drawIceLayer = true);
                }
                const std::uint16_t next
                    = (*this)._timeSinceFrozen > 60 * 2
                        ? static_cast<std::uint16_t>(75 * 2)
                        : static_cast<std::uint16_t>(15 * 2);
                ((*this)._frozenTimer = next);
                ((*this)._frozenGfxTimer = static_cast<std::uint16_t>(next + 5 * 2));
                (*this).EndAltAttack();
            }
            else if (timer < 2)
            {
                ((*this)._frozenTimer = 2);
            }
        }
        else if (timer > 1)
        {
            ((*this)._frozenTimer = 1);
        }
    }

    void PlayerEntity::ModRefreshVolume()
    {
        (*this)._volume = CollisionVolume::Move((*this)._volumeUnxf, (*this).Position);
    }

    // The boost trail follows Position in the simulation step, which for a
    // remote player runs before the network moves it: drawn from there it
    // trails the ball by a step. Put it where the ball now is.
    void PlayerEntity::ModRefreshAttachedEffects()
    {
        if ((*this)._boostEffect != nullptr)
        {
            (*this)._boostEffect->Transform((*this)._gunVec2, (*this)._facingVector, (*this).Position);
        }
    }

    // A remote player's shot event, fired now. Its owner's machine already
    // kept the cooldown and had the gun up, so neither this copy's cooldown
    // nor its gun -- lowered for want of input, or still rising after an
    // unmorph -- holds the shot back. Mods.Network.NetShotEvents.FireReady.
    bool PlayerEntity::ModFireShotEvent()
    {
        SetTimeSinceShot(std::numeric_limits<std::uint16_t>::max());
        _timeSinceInput = 0;
        if (_gunAnimation == GunAnimation::UpDown)
        {
            SetGunAnimation(GunAnimation::Idle, AnimFlags::None);
        }
        return TryFireWeapon();
    }

    bool PlayerEntity::ModBurning() const
    {
        return (*this)._burnTimer > 0;
    }

    bool PlayerEntity::ModDisrupted() const
    {
        return (*this)._disruptedTimer > 0;
    }

    void PlayerEntity::ModSetDisrupted(bool disrupted)
    {
        if ((*this).Health() <= 0)
        {
            return;
        }

        const std::uint16_t timer = (*this)._disruptedTimer;
        if (disrupted)
        {
            if (timer == 0)
            {
                ((*this)._disruptedTimer = 60 * 2);
                if ((*this).IsMainPlayer())
                {
                    (*this).HudOnDisrupted();
                    (*this)._soundSource.PlaySfx(SfxId::LOB_DISRUPT);
                }
            }
            else if (timer < 2)
            {
                ((*this)._disruptedTimer = 2);
            }
        }
        else if (timer > 1)
        {
            ((*this)._disruptedTimer = 1);
        }
    }

    void PlayerEntity::ModSetBurning(bool burning)
    {
        if ((*this).Health() <= 0)
        {
            return;
        }

        const std::uint16_t timer = (*this)._burnTimer;
        if (burning)
        {
            if (timer == 0)
            {
                ((*this)._burnTimer = 150 * 2);
                (*this).CreateBurnEffect();
            }
            else if (timer < 2)
            {
                ((*this)._burnTimer = 2);
            }
        }
        else if (timer > 1)
        {
            ((*this)._burnTimer = 1);
        }
    }

    bool PlayerEntity::ModCanZoom() const
    {
        return ((*this).EquipInfo()->Weapon != nullptr)
            && (static_cast<std::uint32_t>((*this).EquipInfo()->Weapon->Flags) & WeaponFlagCanZoom) != 0;
    }

    std::string PlayerEntity::ModFormState() const
    {
        std::string form = (*this).IsAltForm() ? "alt" : "biped";
        if ((*this).IsMorphing())
        {
            form += "+morphing";
        }
        if ((*this).IsUnmorphing())
        {
            form += "+unmorphing";
        }

        std::string result
            = form + "/" + PlayerAnimationText(static_cast<std::int32_t>((*this).Biped2Anim()));
        if ((static_cast<std::uint32_t>((*this).Biped2Flags()) & AnimFlagEnded) != 0)
        {
            result += "/ended";
        }
        const std::uint16_t frozenTimer = (*this)._frozenTimer;
        if (frozenTimer > 0)
        {
            result += "/frozen:" + std::to_string(frozenTimer);
        }
        if ((*this).Health() == 0)
        {
            result += "/dead";
        }
        return result;
    }

    void PlayerEntity::ModRepairVectors()
    {
        const Vector3 facing = (*this)._facingVector;
        if (Finite(facing))
        {
            _modLastGoodFacing = facing;
        }
        else
        {
            (*this)._facingVector = _modLastGoodFacing;
            const std::int32_t slot = (*this).SlotIndex();
            const std::string formState = ModFormState();
            const Vector3 gunForText = (*this)._gunVec1;
            const std::string gunText = gunForText.ToString();
            Mods::Network::NetLog::Event(("slot " + std::to_string(slot)
                + " facing repaired, " + formState + ", gun=" + gunText));
        }

        const Vector3 gun = (*this)._gunVec1;
        if (Finite(gun))
        {
            _modLastGoodGunVec = gun;
        }
        else
        {
            ((*this)._gunVec1 = _modLastGoodGunVec);
            const std::int32_t slot = (*this).SlotIndex();
            Mods::Network::NetLog::Event(("slot " + std::to_string(slot) + " aim repaired"));
        }

        const Vector3 positionForFinite = static_cast<Vector3>((*this).Position);
        if (Finite(positionForFinite))
        {
            _modLastGoodPosition = static_cast<Vector3>((*this).Position);
        }
        else
        {
            ((*this).Position = _modLastGoodPosition);
            const std::int32_t slot = (*this).SlotIndex();
            Mods::Network::NetLog::Event(("slot " + std::to_string(slot) + " position repaired"));
        }

        if (!Finite((*this).Speed()))
        {
            (*this).SetSpeed(OpenTK::Mathematics::Vector3::Zero);
        }
        if (!Finite((*this)._aimPosition))
        {
            const Vector3 positionForAim = static_cast<Vector3>((*this).Position);
            const Vector3 gunForAim = (*this)._gunVec1;
            ((*this)._aimPosition = positionForAim + gunForAim);
        }
    }

    bool PlayerEntity::Finite(OpenTK::Mathematics::Vector3 value) noexcept
    {
        return std::isfinite(value.X)
            && std::isfinite(value.Y)
            && std::isfinite(value.Z);
    }

    void PlayerEntity::ModNetDie()
    {
        Mods::Network::NetDamage::ReplayDeath(*this);
    }

    std::pair<std::int32_t, float> PlayerEntity::ModScoreboardSize() const
    {
        const std::int32_t rows = GameState::ActivePlayers();
        const float height = (*this).GetScoreboardHeight();
        return {rows, height};
    }

    bool PlayerEntity::ModCanBeHurt() const
    {
        for (std::int32_t i = 0;
            i < static_cast<std::int32_t>((*this).BeamEffectiveness.size()); ++i)
        {
            if (static_cast<std::int32_t>((*this).BeamEffectiveness[i]) != 0)
            {
                return true;
            }
        }
        return false;
    }

    void PlayerEntity::ApplyModAim()
    {
        if (_aimFrame.Source != Mods::Input::AimSource::Network && _aimFrame.Source != Mods::Input::AimSource::Script)
        {
            return;
        }

        const std::int32_t slotForLocal = (*this).SlotIndex();
        const std::int32_t localSlot = Mods::Network::NetHooks::LocalSlot();
        if (slotForLocal == localSlot)
        {
            if (_aimFrame.Source == Mods::Input::AimSource::Script)
            {
                const float deltaY = Mods::Network::NetTestScript::AimDeltaY();
                (*this).UpdateAimY(deltaY);
                const float deltaX = Mods::Network::NetTestScript::AimDeltaX();
                (*this).UpdateAimX(deltaX);
            }
            return;
        }
        const std::int32_t slotForValid = (*this).SlotIndex();
        if (!Mods::Network::NetPlayerBridge::AimAvailable(slotForValid))
        {
            return;
        }
        const std::int32_t slotForAim = (*this).SlotIndex();
        const auto& intent = Mods::Network::NetSession::RemoteIntents[slotForAim];
        const OpenTK::Mathematics::Vector3 origin = LengthSquared(_muzzlePos) > 0.0001F
            ? _muzzlePos : OpenTK::Mathematics::Vector3(Position.X, Position.Y + 0.6F, Position.Z);
        OpenTK::Mathematics::Vector3 aimedFrom{};
        OpenTK::Mathematics::Vector3 shotDirection{};
        std::uint32_t ackFrame = 0;
        Mods::Network::NetPlayerBridge::ShooterRay(*this, origin, aimedFrom, shotDirection, ackFrame);
        static_cast<void>(shotDirection);
        OpenTK::Mathematics::Vector3 aim = Mods::Network::NetPlayerBridge::RetargetAtLocal(*this, origin, intent.Aim, ackFrame, aimedFrom);
        static_cast<void>(Mods::Network::NetPlayerBridge::CoilAimFor(*this, origin, aim));
        ModSetAim(aim);
    }

    void PlayerEntity::ModNoteInput()
    {
        ((*this)._input.HasInput = true);
    }
}
