#include "PlayerEntity.hpp"
#include "HalfturretEntity.hpp"
#include "../../Scene.hpp"
#include "../../Mods/Gameplay/NativeGameplayClock.hpp"
#include "../../NativeRuntime/System/Managed.hpp"
#include "../../Mods/Network/NetLog.hpp"

#include <string>

#include <algorithm>
#include <limits>

namespace MphRead::Entities
{
    using OpenTK::Mathematics::Vector3;

    void PlayerEntity::AdvanceNativeWeaponTimers()
    {
        const auto frame = NativeRuntime::RequireReference(_scene).FrameCount();
        if (!Mods::Gameplay::NativeGameplayClock::IsNativeTick(frame) || _nativeWeaponTimerFrame == frame) return;
        _nativeWeaponTimerFrame = frame;
        if (_nativeTimeSinceShot < std::numeric_limits<std::uint8_t>::max()) ++_nativeTimeSinceShot;
        if (_hunter == Hunter::Weavel && _altAttackCooldown > 0) --_altAttackCooldown;
    }

    void PlayerEntity::FinalizeWeavelForm(bool desiredAlt)
    {
        if (!desiredAlt && TypeExtensions::TestFlag(_flags2, PlayerFlags2::AltAttack))
        {
            EndAltAttack();
        }
        const bool transitioning = IsMorphing() || IsUnmorphing();
        if (IsAltForm() == desiredAlt && !transitioning) return;
        if (IsAltForm() != desiredAlt) UpdateForm(desiredAlt);
        _flags1 &= ~(PlayerFlags1::Morphing | PlayerFlags1::Unmorphing);
        if (desiredAlt)
        {
            SwitchCamera(_values.AltFormStrafe != 0 ? CameraType::Third2 : CameraType::Third1,
                Vector3(_field70, 0.0F, _field74));
        }
        else
        {
            SwitchCamera(CameraType::First, _facingVector);
            SetBipedAnimation(PlayerAnimation::Idle, AnimFlags::None);
        }
        _weavelLungeInput.Reset();
        _weavelNativeAttackPress = false;
    }

    std::uint32_t PlayerEntity::HalfturretShare(std::uint32_t damage) const
    {
        const HalfturretEntity& turret = NativeRuntime::RequireReference(_halfturret);
        return _health > turret.Health() ? damage - damage / 2 : damage / 2;
    }

    void PlayerEntity::GainDrainedHealth(std::uint32_t health)
    {
        const auto amount = static_cast<std::int32_t>(std::min<std::uint32_t>(health, 0x7FFFFFFFU));
        std::int32_t playerHealth = _health;
        if (playerHealth <= 0)
        {
            return;
        }
        if (TypeExtensions::TestFlag(_flags2, PlayerFlags2::Halfturret))
        {
            HalfturretEntity& halfturret = NativeRuntime::RequireReference(_halfturret);
            std::int32_t turretHealth = halfturret.Health();
            if (playerHealth <= turretHealth)
            {
                playerHealth += amount - amount / 2;
                turretHealth += amount / 2;
            }
            else
            {
                playerHealth += amount / 2;
                turretHealth += amount - amount / 2;
            }
            halfturret.SetHealth(std::min(turretHealth, 100));
        }
        else
        {
            playerHealth += amount;
        }
        SetHealth(std::min(playerHealth, HealthMax()));
    }

    void PlayerEntity::DamageHalfturret(std::uint32_t damage)
    {
        HalfturretEntity& turret = NativeRuntime::RequireReference(_halfturret);
        if (turret.Health() <= 0 || static_cast<std::uint32_t>(turret.Health()) <= damage)
        {
            turret.Die();
        }
        else
        {
            turret.SetHealth(turret.Health() - static_cast<std::int32_t>(damage));
        }
        turret.SetTimeSinceDamage(0);
    }

    void PlayerEntity::ModForceWeavelState(bool desiredAlt, bool desiredTurretActive,
        std::optional<std::int32_t> desiredTurretHealth)
    {
        if (_hunter != Hunter::Weavel) return;
        const bool active = TypeExtensions::TestFlag(_flags2, PlayerFlags2::Halfturret)
            && _halfturret && _halfturret->Health() > 0;
        if (!desiredAlt)
        {
            _weavelLungeInput.Reset();
            _weavelNativeAttackPress = false;
            _flags2 &= ~PlayerFlags2::Halfturret;
            if (active) GainHealth(_halfturret->Health());
            if (_halfturret) _halfturret->Die();
            _weavelAltLife = false;
        }
        else if (desiredTurretActive && !_weavelAltLife)
        {
            EnterAltForm(); // local unsplit life: create and split exactly once
        }
        else if (!desiredTurretActive)
        {
            if (_halfturret) _halfturret->Die();
            _weavelAltLife = true; // a dead turret remains dead throughout this Alt life
        }
        if (desiredAlt && desiredTurretActive && desiredTurretHealth
            && TypeExtensions::TestFlag(_flags2, PlayerFlags2::Halfturret))
        {
            _halfturret->SetHealth(*desiredTurretHealth);
        }
        FinalizeWeavelForm(desiredAlt);
    }

    void PlayerEntity::ModApplyOwnWeavelTurret(bool authorityHeadingAlt, bool authorityTurretActive,
        std::int32_t turretHealth)
    {
        if (_hunter != Hunter::Weavel) return;
        const bool alive = TypeExtensions::TestFlag(_flags2, PlayerFlags2::Halfturret)
            && _halfturret && _halfturret->Health() > 0;
        switch (_weavelOwnedTurret.Decide(IsAltForm() || IsMorphing(), alive, authorityHeadingAlt, authorityTurretActive))
        {
        case WeavelOwnedTurret::Step::AdoptHealth:
            if (turretHealth > 0 && turretHealth < _halfturret->Health())
            {
                _halfturret->SetHealth(turretHealth);
            }
            break;
        case WeavelOwnedTurret::Step::Destroy:
            Mods::Network::NetLog::Event("slot " + std::to_string(SlotIndex())
                + " own turret destroyed on the authority: destroyed here too");
            _halfturret->Die();
            break;
        case WeavelOwnedTurret::Step::Keep:
            break;
        }
    }

    void PlayerEntity::ModApplyWeavelState(bool desiredAlt, bool turretActive, std::int32_t turretHealth,
        Vector3 turretPosition, bool turretGrounded)
    {
        if (_hunter != Hunter::Weavel) return;
        // WeavelReplicaTransition decides; the turret flags are arranged here
        // so the copy neither spawns a turret of its own nor merges one's
        // health -- the turret below, and the health, are the authority's.
        const auto frame = static_cast<std::uint64_t>(
            ::MphRead::NativeRuntime::RequireReference(_scene).FrameCount());
        switch (_weavelReplicaTransition.Decide(desiredAlt, IsAltForm(), IsMorphing(), IsUnmorphing(),
            _health > 0, frame))
        {
        case WeavelReplicaTransition::Step::StartMorph:
            _weavelAltLife = true;
            EnterAltForm();
            Mods::Network::NetLog::Event("slot " + std::to_string(SlotIndex()) + " replica Weavel morph started");
            break;
        case WeavelReplicaTransition::Step::StartUnmorph:
            _flags2 &= ~PlayerFlags2::Halfturret;
            ExitAltForm();
            Mods::Network::NetLog::Event("slot " + std::to_string(SlotIndex()) + " replica Weavel unmorph started");
            break;
        case WeavelReplicaTransition::Step::Wait:
            break;
        case WeavelReplicaTransition::Step::Finalize:
            if (_weavelReplicaTransition.Started())
            {
                Mods::Network::NetLog::Event("slot " + std::to_string(SlotIndex()) + " replica Weavel "
                    + (IsMorphing() || IsUnmorphing() ? "transition stalled, snapped" : "transition finished")
                    + " after " + std::to_string(frame - _weavelReplicaTransition.StartFrame()) + " frames");
                _weavelReplicaTransition.Close();
            }
            FinalizeWeavelForm(desiredAlt);
            break;
        }
        _weavelAltLife = desiredAlt;
        if (!desiredAlt)
        {
            _weavelLungeInput.Reset();
            _weavelNativeAttackPress = false;
        }
        const bool wanted = desiredAlt && turretActive && turretHealth > 0;
        const bool active = TypeExtensions::TestFlag(_flags2, PlayerFlags2::Halfturret)
            && _halfturret && _halfturret->Health() > 0;
        Scene& scene = ::MphRead::NativeRuntime::RequireReference(_scene);
        if (wanted)
        {
            if (!active)
            {
                // Snapshot health already includes the authority's split. Never AddEntity,
                // whose Initialize would subtract half of that health a second time.
                scene.RemoveEntity(_halfturret);
                _halfturret->InitializeFromNetworkState(turretHealth, turretPosition, turretGrounded);
                scene.InsertEntity(_halfturret);
                scene.InitEntity(_halfturret);
                _flags2 |= PlayerFlags2::Halfturret;
            }
            else
            {
                _halfturret->ApplyNetworkState(turretHealth, turretPosition, turretGrounded);
            }
        }
        else if (_halfturret)
        {
            _halfturret->Die(); // no merge of authority HP during replica reconciliation
        }
    }

}
