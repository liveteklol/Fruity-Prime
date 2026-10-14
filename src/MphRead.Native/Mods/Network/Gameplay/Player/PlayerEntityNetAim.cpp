#include "Mods/Network/Gameplay/Player/PlayerEntityNetAim.hpp"
#include "Entities/Players/HalfturretEntity.hpp"

#include "Mods/Input/GamepadOptions.hpp"
#include "Entities/EntityBase.hpp"
#include "Entities/Players/PlayerCamera.hpp"
#include "Entities/PlayerSpawnEntity.hpp"
#include "Formats/Model.hpp"
#include "Entities/CamSeq/CameraSequence.hpp"
#include "GameState.hpp"
#include "Metadata/Metadata.hpp"
#include "Mods/Input/GamepadInput.hpp"
#include "Mods/SpectatorMode.hpp"
#include "Mods/Network/Gameplay/Combat/NetDamage.hpp"
#include "Mods/Network/Gameplay/Player/NetInputHistory.hpp"
#include "Mods/Network/Gameplay/Player/PlayerCombatPolicy.hpp"
#include "Mods/Network/Integration/Hooks/NetHooks.hpp"
#include "Mods/Network/Diagnostics/NetLog.hpp"
#include "Mods/Network/Gameplay/Player/NetPlayerBridge.hpp"
#include "Mods/Network/Domain/NetworkVectorRules.hpp"
#include "Mods/Network/Session/NetSession.hpp"
#include "Mods/Network/Testing/Harness/NetTestScript.hpp"
#include "Scene.hpp"
#include "NativeRuntime/System/Managed.hpp"
#include "NativeRuntime/OpenTK/Mathematics.hpp"
#include "Formats/Types.hpp"

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
        if (_isBot || slotForValid < 0 || static_cast<std::size_t>(slotForValid) >= Mods::Network::NetSession::RemotePlayerSlots)
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
        if (!Mods::Network::Domain::IsFiniteVector3(aim))
        {
            return;
        }
        const float aimLengthSquared = LengthSquared(aim);
        if (!std::isfinite(aimLengthSquared) || !(aimLengthSquared > 0.0001F))
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
        if (ModIsSpectating() == value)
        {
            return;
        }
        ModApplyReplicatedSpectating(value);
        ModClearSpectatorInput();
    }

    void PlayerEntity::ModClearSpectatorInput()
    {
        (*this).Controls().ClearAll();
        ::MphRead::Mods::Network::NetInputHistory::ForgetSlot(SlotIndex());
    }

    void PlayerEntity::ModApplyReplicatedSpectating(bool value)
    {
        if (ModIsSpectating() == value)
        {
            return;
        }
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

    bool PlayerEntity::ModIsSpectating() const
    {
        return ::HasFlag(static_cast<std::uint32_t>((*this).Flags2()), PlayerFlagSpectating);
    }

    bool PlayerEntity::ModInPlay() const
    {
        return ModIsInPlay();
    }

    bool PlayerEntity::ModIsInPlay() const
    {
        return ::MphRead::Mods::Network::PlayerCombatPolicy::IsInPlay(
            ::MphRead::TestFlag((*this).LoadFlags(), LoadFlags::Active),
            ::MphRead::TestFlag((*this).LoadFlags(), LoadFlags::Spawned),
            (*this).Health(), ModIsSpectating());
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

    void PlayerEntity::ModApplyFormTransition(bool morphing)
    {
        if (morphing)
        {
            if (IsMorphing()) return;
            if (IsUnmorphing() || IsAltForm()) ModForceForm(false);
            EnterAltForm();
            return;
        }

        if (IsUnmorphing()) return;
        if (IsMorphing() || !IsAltForm()) ModForceForm(true);
        ExitAltForm();
    }

    bool PlayerEntity::ModFormTransitionAnimationEnded(bool morphing) const noexcept
    {
        const PlayerAnimation expected = morphing ? PlayerAnimation::Morph : PlayerAnimation::Unmorph;
        return Biped2Anim() == expected
            && (static_cast<std::uint32_t>(Biped2Flags()) & AnimFlagEnded) != 0;
    }

    std::uint8_t PlayerEntity::ModFormTransitionProgress() const noexcept
    {
        if (_bipedModel2 == nullptr || _bipedModel2->AnimInfo == nullptr
            || _bipedModel2->AnimInfo->Frame == nullptr || _bipedModel2->AnimInfo->FrameCount == nullptr)
        {
            return 0;
        }
        const std::int32_t frame = std::max(0, ::MphRead::NativeRuntime::ManagedAt(
            _bipedModel2->AnimInfo->Frame, 0));
        const std::int32_t frameCount = ::MphRead::NativeRuntime::ManagedAt(
            _bipedModel2->AnimInfo->FrameCount, 0);
        if (frameCount <= 1) return 0;
        const std::int32_t lastFrame = frameCount - 1;
        const std::int32_t boundedFrame = std::min(frame, lastFrame);
        return static_cast<std::uint8_t>((boundedFrame * 15 + lastFrame / 2) / lastFrame);
    }

    void PlayerEntity::ModSyncFormTransitionProgress(bool morphing, std::uint8_t progress) noexcept
    {
        SyncFormTransitionProgress(morphing, progress, false);
    }

    void PlayerEntity::ModHoldFormTransitionAtProgress(bool morphing, std::uint8_t progress) noexcept
    {
        SyncFormTransitionProgress(morphing, progress, true);
    }

    void PlayerEntity::SyncFormTransitionProgress(bool morphing, std::uint8_t progress,
        bool holdAtProgress) noexcept
    {
        const PlayerAnimation expected = morphing ? PlayerAnimation::Morph : PlayerAnimation::Unmorph;
        const std::uint8_t boundedProgress = std::min<std::uint8_t>(progress, 15);
        constexpr std::uint8_t finalPendingPhase = 12;
        const auto advance = [expected, boundedProgress, holdAtProgress](
            const std::shared_ptr<MphRead::ModelInstance>& model)
        {
            if (model == nullptr || model->AnimInfo == nullptr || model->AnimInfo->Index == nullptr
                || model->AnimInfo->Frame == nullptr || model->AnimInfo->FrameCount == nullptr
                || model->AnimInfo->Flags == nullptr)
            {
                return;
            }
            auto& info = *model->AnimInfo;
            if (::MphRead::NativeRuntime::ManagedAt(info.Index, 0) != static_cast<std::int32_t>(expected)) return;
            const std::int32_t frameCount = ::MphRead::NativeRuntime::ManagedAt(info.FrameCount, 0);
            if (frameCount <= 1) return;
            const std::int32_t lastFrame = frameCount - 1;
            const std::int32_t target = (static_cast<std::int32_t>(boundedProgress) * lastFrame + 7) / 15;
            std::int32_t& frame = ::MphRead::NativeRuntime::ManagedAt(info.Frame, 0);
            const std::int32_t activeTarget = std::min(target, lastFrame - 1);
            // Snap forward only. A snapshot is already in transit when it is
            // applied; rewinding to its quantized phase would make animation
            // stutter under ordinary latency. Pause at the final pending frame
            // until the authority reports that the transition has finished.
            // Phase 13 can arrive after the local animation has already ended:
            // it is quantized and the authority publishes the stable form only
            // after the next simulation step. Hold one phase earlier so the
            // form bit cannot flip ahead of that stable snapshot.
            if (holdAtProgress)
            {
                // A remote replica is drawn from NetSmoothing's delayed
                // presentation clock. Hold the native clip at that sampled
                // phase so local animation playback cannot run ahead of the
                // pose, bombs, or other tracks on the same clock.
                frame = activeTarget;
                ::MphRead::NativeRuntime::ManagedAt(info.Flags, 0) |= AnimFlags::Paused;
            }
            else if (boundedProgress >= finalPendingPhase)
            {
                frame = lastFrame - 1;
                ::MphRead::NativeRuntime::ManagedAt(info.Flags, 0) |= AnimFlags::Paused;
            }
            else
            {
                if (frame >= lastFrame)
                {
                    frame = activeTarget;
                }
                else if (activeTarget > frame) frame = activeTarget;
                ::MphRead::NativeRuntime::ManagedAt(info.Flags, 0) &= ~AnimFlags::Paused;
            }
            ::MphRead::NativeRuntime::ManagedAt(info.Flags, 0) &= ~AnimFlags::Ended;
        };
        advance(_bipedModel1);
        advance(_bipedModel2);
    }

    void PlayerEntity::ModReleaseFormTransitionHold() noexcept
    {
        const auto release = [](const std::shared_ptr<MphRead::ModelInstance>& model)
        {
            if (model == nullptr || model->AnimInfo == nullptr || model->AnimInfo->Index == nullptr
                || model->AnimInfo->Flags == nullptr)
            {
                return;
            }
            const std::int32_t animation = ::MphRead::NativeRuntime::ManagedAt(
                model->AnimInfo->Index, 0);
            if (animation == static_cast<std::int32_t>(PlayerAnimation::Morph)
                || animation == static_cast<std::int32_t>(PlayerAnimation::Unmorph))
            {
                ::MphRead::NativeRuntime::ManagedAt(model->AnimInfo->Flags, 0) &= ~AnimFlags::Paused;
            }
        };
        release(_bipedModel1);
        release(_bipedModel2);
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

    // Double damage is not among them: the authority grants that itself
    // (Mods.Network.NetPowerUps), and an owner's word for it is checked
    // against the grant, never adopted.
    void PlayerEntity::ModSetShotState(std::int32_t chargeLevel, std::int32_t boostDamage)
    {
        if (SlotIndex() == Mods::Network::NetHooks::LocalSlot())
        {
            // Never the machine's own player: this is its own state coming
            // back to it a round trip later.
            return;
        }
        ::MphRead::NativeRuntime::RequireReference(EquipInfo()).ChargeLevel = static_cast<std::uint16_t>(std::clamp(chargeLevel, 0, 0xFFFF));
        _boostDamage = static_cast<std::uint16_t>(std::clamp(boostDamage, 0, 0xFFFF));
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

    std::uint16_t PlayerEntity::ModFrozenFrames() const
    {
        return (*this)._frozenTimer;
    }

    void PlayerEntity::ModMirrorFrozen(std::uint16_t frames)
    {
        if ((*this).Health() <= 0)
        {
            return;
        }
        if (frames == 0)
        {
            if ((*this)._frozenTimer > 1)
            {
                // Thaws on the next step, the way running out does.
                ((*this)._frozenTimer = 1);
            }
            return;
        }
        if ((*this)._frozenTimer == 0 || ((*this)._frozenTimer == 1 && frames > 1))
        {
            (*this)._soundSource.PlaySfx(SfxId::SHOTGUN_FREEZE);
            if ((*this).IsMainPlayer())
            {
                (*this).ResetCombatVisor();
                ((*this)._drawIceLayer = true);
            }
            (*this).EndAltAttack();
        }
        ((*this)._frozenTimer = frames);
        // The ice outlasts the freeze by the five frames it always has.
        ((*this)._frozenGfxTimer = static_cast<std::uint16_t>(frames + 5 * 2));
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

    std::uint16_t PlayerEntity::ModBurningFrames() const
    {
        return (*this)._burnTimer;
    }

    std::uint16_t PlayerEntity::ModDisruptedFrames() const
    {
        return (*this)._disruptedTimer;
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

    void PlayerEntity::ModMirrorDisruptedFrames(std::uint16_t frames)
    {
        if ((*this).Health() <= 0)
        {
            return;
        }
        if (frames == 0)
        {
            if ((*this)._disruptedTimer > 1)
            {
                ((*this)._disruptedTimer = 1);
            }
            return;
        }
        if ((*this)._disruptedTimer <= 1 && frames > 1 && (*this).IsMainPlayer())
        {
            (*this).HudOnDisrupted();
            (*this)._soundSource.PlaySfx(SfxId::LOB_DISRUPT);
        }
        ((*this)._disruptedTimer = frames);
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

    void PlayerEntity::ModMirrorBurningFrames(std::uint16_t frames)
    {
        if ((*this).Health() <= 0)
        {
            return;
        }
        if (frames == 0)
        {
            if ((*this)._burnTimer > 1)
            {
                ((*this)._burnTimer = 1);
            }
            return;
        }
        if ((*this)._burnTimer <= 1)
        {
            (*this).CreateBurnEffect();
        }
        ((*this)._burnTimer = frames);
    }

    void PlayerEntity::ModIgnite(PlayerEntity& by, std::uint32_t launchFrame)
    {
        if ((*this).Health() <= 0)
        {
            return;
        }
        (*this).ModSetBurning(true);
        (*this)._burnedBy = SharedFrom<EntityBase>(&by);
        (*this)._burnLaunchFrame = launchFrame;
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
        if (Mods::Network::Domain::IsFiniteVector3(facing))
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
        if (Mods::Network::Domain::IsFiniteVector3(gun))
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
        if (Mods::Network::Domain::IsFiniteVector3(positionForFinite))
        {
            _modLastGoodPosition = static_cast<Vector3>((*this).Position);
        }
        else
        {
            ((*this).Position = _modLastGoodPosition);
            const std::int32_t slot = (*this).SlotIndex();
            Mods::Network::NetLog::Event(("slot " + std::to_string(slot) + " position repaired"));
        }

        if (!Mods::Network::Domain::IsFiniteVector3((*this).Speed()))
        {
            (*this).SetSpeed(OpenTK::Mathematics::Vector3::Zero);
        }
        if (!Mods::Network::Domain::IsFiniteVector3((*this)._aimPosition))
        {
            const Vector3 positionForAim = static_cast<Vector3>((*this).Position);
            const Vector3 gunForAim = (*this)._gunVec1;
            ((*this)._aimPosition = positionForAim + gunForAim);
        }
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
        const auto& intent = Mods::Network::NetSession::RemoteIntent(static_cast<std::size_t>(slotForAim));
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
