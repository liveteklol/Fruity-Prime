#include "PlayerProcess.hpp"

#include "PlayerEntity.hpp"
#include "HalfturretEntity.hpp"
#include "../BeamProjectileEntity.hpp"
#include "../DoorEntity.hpp"
#include "../EnemyInstanceEntity.hpp"
#include "../EnemySpawnEntity.hpp"
#include "../Enemies/02_Temroid.hpp"
#include "../Enemies/37_Quadtroid.hpp"
#include "../ItemInstanceEntity.hpp"
#include "../JumpPadEntity.hpp"
#include "../PlayerSpawnEntity.hpp"
#include "../RoomEntity.hpp"
#include "../TeleporterEntity.hpp"
#include "../CamSeq/CameraSequence.hpp"
#include "../../Features.hpp"
#include "../../GameState.hpp"
#include "../../Messaging.hpp"
#include "../../Scene.hpp"
#include "../../Strings.hpp"
#include "../../Metadata/Metadata.hpp"
#include "../../Metadata/Weapons.hpp"
#include "../../Mods/Network/NetHooks.hpp"
#include "../../Mods/Network/NetHealthSync.hpp"
#include "../../Mods/Network/NetPlayerBridge.hpp"
#include "../../Mods/Multiplayer/MapResourceRules.hpp"
#include "../../Sound/Sfx.hpp"
#include "../../Utility/Rng.hpp"
#include "../../NativeRuntime/System/Console.hpp"
#include "../../NativeRuntime/System/Managed.hpp"
#include "../../NativeRuntime/OpenTK/Mathematics.hpp"
#include "../../Formats/Types.hpp"

#include <algorithm>
#include <any>
#include <array>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

using ::MphRead::NativeRuntime::EnvironmentGetVariable;
using ::MphRead::NativeRuntime::ManagedAt;
using ::MphRead::NativeRuntime::MathClamp;
using ::MphRead::NativeRuntime::MathMax;
using ::MphRead::NativeRuntime::MathMin;
using ::MphRead::NativeRuntime::RequireReference;
using ::MphRead::NativeRuntime::UInt32ToInt32;
using ::MphRead::NativeRuntime::UncheckedAdd;
using ::MphRead::NativeRuntime::UncheckedMultiply;
using ::MphRead::NativeRuntime::UncheckedSubtract;
using ::MphRead::TestAny;
using ::MphRead::TestFlag;
using ::OpenTK::Mathematics::Add;
using ::OpenTK::Mathematics::AddX;
using ::OpenTK::Mathematics::AddZ;
using ::OpenTK::Mathematics::Clamp;
using ::OpenTK::Mathematics::CreateFromAxisAngle;
using ::OpenTK::Mathematics::CreateRotationX;
using ::OpenTK::Mathematics::CreateRotationY;
using ::OpenTK::Mathematics::DistanceSquared;
using ::OpenTK::Mathematics::Divide;
using ::OpenTK::Mathematics::Length;
using ::OpenTK::Mathematics::LengthSquared;
using ::OpenTK::Mathematics::MathHelper::DegreesToRadians;
using ::OpenTK::Mathematics::Multiply;
using ::OpenTK::Mathematics::Negate;
using ::OpenTK::Mathematics::Normalize;
using ::OpenTK::Mathematics::ScaleVector;
using ::OpenTK::Mathematics::SetRow3;
using ::OpenTK::Mathematics::Subtract;
using ::OpenTK::Mathematics::WithY;

namespace
{
    using MphRead::CollisionVolume;
    using MphRead::Fixed;
    using MphRead::MessageObject;
    using MphRead::Entities::EntityBase;
    using MphRead::Entities::PlayerEntity;
    using OpenTK::Mathematics::Matrix4;
    using OpenTK::Mathematics::Vector3;
    using OpenTK::Mathematics::Vector4;

    constexpr Vector3 UnitX(1.0F, 0.0F, 0.0F);
    constexpr Vector3 UnitY(0.0F, 1.0F, 0.0F);
    constexpr Vector3 UnitZ(0.0F, 0.0F, 1.0F);

    [[nodiscard]] std::shared_ptr<EntityBase> TryUnboxEntity(const MessageObject& value)
    {
        if (!value || !value->has_value())
        {
            return nullptr;
        }
        if (const auto shared = std::any_cast<std::shared_ptr<EntityBase>>(value.get()))
        {
            return *shared;
        }
        if (const auto raw = std::any_cast<EntityBase*>(value.get()))
        {
            if (*raw == nullptr)
            {
                return nullptr;
            }
            return SharedFrom(*raw);
        }
        return nullptr;
    }

    [[nodiscard]] std::uint16_t ManagedUInt16Add(std::uint16_t left, std::uint16_t right) noexcept
    {
        return static_cast<std::uint16_t>(static_cast<std::uint32_t>(left) + right);
    }

    [[nodiscard]] std::uint16_t ManagedUInt16Multiply(std::uint16_t left, std::uint16_t right) noexcept
    {
        return static_cast<std::uint16_t>(static_cast<std::uint32_t>(left) * right);
    }

    [[nodiscard]] std::uint8_t ManagedByteFromInt32(std::int32_t value) noexcept
    {
        return static_cast<std::uint8_t>(static_cast<std::uint32_t>(value));
    }

    [[nodiscard]] Vector3 Row0(const Matrix4& value) noexcept
    {
        return Vector3(value.M11, value.M12, value.M13);
    }

    [[nodiscard]] Vector3 Row1(const Matrix4& value) noexcept
    {
        return Vector3(value.M21, value.M22, value.M23);
    }

    [[nodiscard]] Vector3 Row2(const Matrix4& value) noexcept
    {
        return Vector3(value.M31, value.M32, value.M33);
    }

    [[nodiscard]] Vector3 Row3(const Matrix4& value) noexcept
    {
        return Vector3(value.M41, value.M42, value.M43);
    }

    void SetRow0(Matrix4& value, Vector3 row) noexcept
    {
        value.M11 = row.X;
        value.M12 = row.Y;
        value.M13 = row.Z;
    }

    void SetRow1(Matrix4& value, Vector3 row) noexcept
    {
        value.M21 = row.X;
        value.M22 = row.Y;
        value.M23 = row.Z;
    }

    void SetRow2(Matrix4& value, Vector3 row) noexcept
    {
        value.M31 = row.X;
        value.M32 = row.Y;
        value.M33 = row.Z;
    }

    [[nodiscard]] std::uint16_t FloatToUInt16Unchecked(float value) noexcept
    {
        if (!std::isfinite(value))
        {
            return 0;
        }
        const double truncated = std::trunc(static_cast<double>(value));
        const double modulo = std::fmod(truncated, 65536.0);
        const double normalized = modulo < 0.0 ? modulo + 65536.0 : modulo;
        return static_cast<std::uint16_t>(normalized);
    }
}

namespace MphRead::Entities
{
    bool PlayerEntity::BombCountCheck()
    {
        if (!_bombCountCheck.has_value())
        {
            _bombCountCheck = EnvironmentGetVariable("MPHREAD_BOMB_CHECK").has_value();
        }
        return *_bombCountCheck;
    }

    void PlayerEntity::CheckSyluxBombCount()
    {
        std::int32_t placed = 0;
        for (std::int32_t i = 0; i < static_cast<std::int32_t>(_syluxBombs.size()); ++i)
        {
            if (ManagedAt(_syluxBombs, i))
            {
                placed = UncheckedAdd(placed, 1);
            }
        }
        if (_syluxBombCount == placed || _lastBombCountReport == _syluxBombCount)
        {
            return;
        }
        _lastBombCountReport = _syluxBombCount;
        std::cout << "[bombcheck] slot " << _slotIndex << " count="
            << static_cast<std::int32_t>(_syluxBombCount) << " but " << placed
            << " bomb(s) placed -- Lockjaw is now inert for this player" << std::endl;
    }

    bool PlayerEntity::Process()
    {
        const bool result = ProcessPlayer();
        SetTransform(_facingVector, _upVector, Position);
        return result;
    }

    bool PlayerEntity::ProcessPlayer()
    {
        const auto scene = [&]() -> Scene& { return RequireReference(_scene); };
        if (GameState::Multiplayer() && !TestFlag(_loadFlags, LoadFlags::Connected)
            && TestFlag(_loadFlags, LoadFlags::WasConnected))
        {
            _loadFlags |= LoadFlags::Disconnected;
            _loadFlags &= ~LoadFlags::Active;
        }
        if (!TestFlag(_loadFlags, LoadFlags::Active))
        {
            if (GameState::Multiplayer())
            {
                return Mods::Network::NetHooks::KeepSlotAlive(*this);
            }
            return TestFlag(_loadFlags, LoadFlags::SlotActive);
        }
        if (TestFlag(_flags2, PlayerFlags2::UnequipOmegaCannon))
        {
            UnequipOmegaCannon();
            _flags2 &= ~PlayerFlags2::UnequipOmegaCannon;
        }
        if (_isBot)
        {
            PlayerAiData& aiData = RequireReference(AiData);
            if (TestFlag(aiData.Flags3, AiFlags3::Bit5))
            {
                auto enumerator = scene().GetPlayerEntities().GetEnumerator();
                while (enumerator.MoveNext())
                {
                    std::shared_ptr<PlayerEntity> other = enumerator.Current();
                    PlayerEntity& otherRef = RequireReference(other);
                    if (!otherRef._isBot || other.get() == this)
                    {
                        continue;
                    }
                    scene().SendMessage(Message::Destroyed, this, nullptr,
                        BoxInt32(0), BoxInt32(0), 1);
                    if (otherRef._enemySpawner)
                    {
                        scene().SendMessage(Message::Destroyed, this, otherRef._enemySpawner.get(),
                            BoxInt32(0), BoxInt32(0));
                    }
                    RequireReference(otherRef.AiData).Flags2 |= AiFlags2::Bit13;
                }
                aiData.Flags3 &= ~AiFlags3::Bit5;
            }
            if (TestFlag(aiData.Flags3, AiFlags3::Invulnerable))
            {
                _spawnInvulnTimer = 2;
                aiData.Flags3 &= ~AiFlags3::Invulnerable;
            }
            if (TestFlag(aiData.Flags3, AiFlags3::Bit1))
            {
                const std::int32_t effectId
                    = GameState::Multiplayer() && PlayerCount() > 2 && !Features::MaxPlayerDetail()
                        ? 33 : 31;
                scene().SpawnEffect(effectId, UnitX, UnitY, Position);
                PlayHunterSfx(HunterSfx::Spawn);
                aiData.Flags3 &= ~AiFlags3::Bit1;
                aiData.Flags3 |= AiFlags3::Bit2;
            }
            else if (TestFlag(aiData.Flags3, AiFlags3::Bit2) && _curAlpha <= 1.0F / 31.0F)
            {
                _health = 0;
                _flags2 |= PlayerFlags2::HideModel;
                aiData.Flags3 &= ~AiFlags3::Bit2;
                aiData.Flags1 = false;
                _soundSource.StopAllSfx(true);
            }
            if (TestFlag(aiData.Flags3, AiFlags3::Despawned))
            {
                scene().SendMessage(Message::Destroyed, this, nullptr,
                    BoxInt32(0), BoxInt32(0), 1);
                if (_enemySpawner)
                {
                    scene().SendMessage(Message::Destroyed, this, _enemySpawner.get(),
                        BoxInt32(0), BoxInt32(0));
                }
                _health = 0;
                _flags2 |= PlayerFlags2::HideModel;
                aiData.Flags3 &= ~AiFlags3::Despawned;
                aiData.Flags1 = false;
                _soundSource.StopAllSfx(true);
            }
            assert(TestFlag(_loadFlags, LoadFlags::Active));
        }

        if (IsMainPlayer())
        {
            Formats::CameraSequence* currentSequence = Formats::CameraSequence::Current();
            if (currentSequence != nullptr && currentSequence->BlockInput())
            {
                _controls.ClearAll();
            }
        }
        _prevPosition = Position;
        _prevSpeed = _speed;
        _flags1 &= ~PlayerFlags1::AltFormPrevious;
        if (TestFlag(_flags1, PlayerFlags1::AltForm))
        {
            _flags1 |= PlayerFlags1::AltFormPrevious;
        }
        _flags1 &= ~PlayerFlags1::MovingBiped;
        _flags1 &= ~PlayerFlags1::ShotCharged;
        _flags1 &= ~PlayerFlags1::ShotMissile;
        _flags1 &= ~PlayerFlags1::ShotUncharged;
        _crushBits = 0;
        if (_respawnTimer > 0)
        {
            --_respawnTimer;
            if ((GameState::Mode() == GameMode::Survival || GameState::Mode() == GameMode::SurvivalTeams)
                && ManagedAt(GameState::TeamDeaths(), _slotIndex) > GameState::PointGoal())
            {
                if (IsMainPlayer())
                {
                    QueueHudMessage(128.0F, 152.0F, 1.0F / 1000.0F, 0, 243);
                }
                if (_respawnTimer == 0)
                {
                    _respawnTimer = 1;
                }
            }
        }
        if (_health == 0 && _respawnTimer == 0 && !_enemySpawner)
        {
            if (scene().Room() != nullptr && scene().Room()->LoadEntityId >= 0)
            {
                std::shared_ptr<TeleporterEntity> targetTeleporter{};
                auto teleporterEnumerator = scene().GetTeleporterEntities().GetEnumerator();
                while (teleporterEnumerator.MoveNext())
                {
                    std::shared_ptr<TeleporterEntity> teleporter = teleporterEnumerator.Current();
                    if (RequireReference(teleporter).Data().LoadIndex == scene().Room()->LoadEntityId)
                    {
                        targetTeleporter = std::move(teleporter);
                        break;
                    }
                }
                if (targetTeleporter)
                {
                    TeleporterEntity& teleporter = *targetTeleporter;
                    teleporter.SetTriggered();
                    Spawn(teleporter.Position, teleporter.FacingVector(), teleporter.UpVector(),
                        teleporter.NodeRef, true);
                    if (GameState::TransitionAltForm())
                    {
                        static_cast<void>(TrySwitchForms(true));
                        UpdateForm(true);
                        HalfturretEntity& halfturret = RequireReference(_halfturret);
                        if (halfturret.Health() > 0)
                        {
                            halfturret.ResetGroundedState();
                        }
                        ResumeOwnCamera();
                        HudOnMorphStart();
                    }
                }
                else
                {
                    std::shared_ptr<DoorEntity> targetDoor{};
                    auto doorEnumerator = scene().GetDoorEntities().GetEnumerator();
                    while (doorEnumerator.MoveNext())
                    {
                        std::shared_ptr<DoorEntity> door = doorEnumerator.Current();
                        if (RequireReference(door).Data().OutConnectorId == scene().Room()->LoadEntityId)
                        {
                            targetDoor = std::move(door);
                            break;
                        }
                    }
                    if (targetDoor)
                    {
                        DoorEntity& door = *targetDoor;
                        const Vector3 facing = door.FacingVector();
                        const Vector3 position = Add(door.Position, ScaleVector(facing, 2.0F));
                        Spawn(position, facing, door.UpVector(), door.NodeRef, true);
                    }
                }
                scene().Room()->LoadEntityId = -1;
                GameState::TransitionAltForm(false);
            }
            else
            {
                const std::int32_t time = GetTimeUntilRespawn();
                if (IsMainPlayer() && GameState::Multiplayer())
                {
                    const Formats::CameraSequence* sequence = Formats::CameraSequence::Current();
                    const std::int32_t messageId
                        = sequence != nullptr && sequence->IsIntro() ? 245 : 244;
                    if (!Bugfixes::NoStrayRespawnText() || time > 0
                        || (GameState::Mode() != GameMode::Survival
                            && GameState::Mode() != GameMode::SurvivalTeams))
                    {
                        QueueHudMessage(128.0F, 162.0F, 1.0F / 1000.0F, 0, messageId);
                        if (time < 150 * 2)
                        {
                            std::string message = Text::Strings::GetHudMessage(246);
                            const std::int32_t seconds = UncheckedAdd(time, 30 * 2) / (30 * 2);
                            const std::string needle = "%d";
                            const std::string replacement = std::to_string(seconds);
                            std::size_t pos = 0;
                            while ((pos = message.find(needle, pos)) != std::string::npos)
                            {
                                message.replace(pos, needle.size(), replacement);
                                pos += replacement.size();
                            }
                            QueueHudMessage(128.0F, 152.0F, 1.0F / 1000.0F, 0, message);
                        }
                    }
                }
                if (GameState::SinglePlayer() || _controls.Shoot().IsDown()
                    || Mods::Network::NetPlayerBridge::RespawnRequested(SlotIndex()) || time <= 0 || _isBot
                    || Mods::Network::NetHooks::ForceSpawn(*this))
                {
                    std::shared_ptr<PlayerSpawnEntity> respawn = GetRespawnPoint();
                    if (respawn)
                    {
                        const Vector3 position = _forcedSpawnPos.has_value()
                            ? *_forcedSpawnPos : static_cast<Vector3>(respawn->Position);
                        Spawn(position, respawn->FacingVector(), respawn->UpVector(), respawn->NodeRef, true);
                    }
                }
            }
        }

        _volume = CollisionVolume::Move(_volumeUnxf, Position);
        if (IsMainPlayer() && !IsAltForm())
        {
            _soundSource.Update(Position, -1);
        }
        else
        {
            std::int32_t rangeIndex = 1;
            if (GameState::SinglePlayer() && _hunter == Hunter::Guardian)
            {
                rangeIndex = 21;
            }
            _soundSource.Update(Position, rangeIndex);
        }
        if (IsMainPlayer())
        {
            UpdateHealthSfx(_health);
        }
        else
        {
            UpdateNodeRefVolume();
        }
        if (_damageInvulnTimer > 0)
        {
            --_damageInvulnTimer;
        }
        if (_spawnInvulnTimer > 0)
        {
            --_spawnInvulnTimer;
        }
        if (_disruptedTimer > 0)
        {
            --_disruptedTimer;
        }

        if (GameState::Mode() == GameMode::Survival || GameState::Mode() == GameMode::SurvivalTeams)
        {
            if (TestFlag(_flags2, PlayerFlags2::RadarReveal))
            {
                _flags2 |= PlayerFlags2::RadarRevealPrevious;
            }
            _flags2 &= ~PlayerFlags2::RadarReveal;
            if (_health == 0)
            {
                _hidingTimer = 0;
            }
            else if (GameState::RadarPlayers())
            {
                if (IsMainPlayer())
                {
                    QueueHudMessage(128.0F, 170.0F, 1.0F / 1000.0F, 0, 247);
                }
                _hidingTimer = 0;
            }
            else
            {
                const std::int32_t revealTime = (PlayerCount() > 2 ? 600 : 300) * 2;
                const Vector3 moved = Subtract(static_cast<Vector3>(Position), _idlePosition);
                if (LengthSquared(moved) >= 25.0F)
                {
                    _hidingTimer = static_cast<std::uint16_t>(
                        _hidingTimer > 35 * 2 ? _hidingTimer - 35 * 2 : 0);
                    if (_hidingTimer < revealTime && _hidingTimer > revealTime - 35 * 2)
                    {
                        _hidingTimer = static_cast<std::uint16_t>(revealTime - 35 * 2);
                    }
                }
                if (_hidingTimer < revealTime + 150 * 2)
                {
                    ++_hidingTimer;
                }
                if (_hidingTimer >= revealTime)
                {
                    _flags2 |= PlayerFlags2::RadarReveal;
                    if (IsMainPlayer() && (scene().FrameCount() & static_cast<std::uint64_t>(8 * 2)) == 0)
                    {
                        QueueHudMessage(128.0F, 150.0F, 1.0F / 1000.0F, 0, 248);
                        QueueHudMessage(128.0F, 160.0F, 1.0F / 1000.0F, 0, 249);
                    }
                }
            }
        }

        if (_timeSinceHitTarget != std::numeric_limits<std::uint16_t>::max()
            && ++_timeSinceHitTarget > 1)
        {
            _shockCoilTimer = 0;
            _shockCoilTarget.reset();
            if (_timeSinceHitTarget >= 210 * 2)
            {
                _lastTarget.reset();
            }
        }
        if (_timeSinceShot != std::numeric_limits<std::uint16_t>::max())
        {
            ++_timeSinceShot;
        }
        AdvanceNativeWeaponTimers();
        if (_altAttackCooldown > 0 && _hunter != Hunter::Weavel)
        {
            --_altAttackCooldown;
        }
        if (_jumpPadControlLock > 0)
        {
            --_jumpPadControlLock;
        }
        if (_jumpPadControlLock == 0)
        {
            _lastJumpPad.reset();
        }
        if (_jumpPadControlLockMin > 0)
        {
            --_jumpPadControlLockMin;
        }
        if (_timeSinceJumpPad != std::numeric_limits<std::uint16_t>::max())
        {
            ++_timeSinceJumpPad;
        }

        if (_hunter == Hunter::Samus)
        {
            if (_bombRefillTimer > 0)
            {
                --_bombRefillTimer;
            }
            else
            {
                _bombAmmo = 3;
            }
        }
        else if (_hunter == Hunter::Sylux)
        {
            if (BombCountCheck())
            {
                CheckSyluxBombCount();
            }
            if (_bombCooldown > 0)
            {
                _bombAmmo = 0;
            }
            else
            {
                _bombAmmo = ManagedByteFromInt32(3 - static_cast<std::int32_t>(_syluxBombCount));
            }
        }
        else
        {
            _bombAmmo = 1;
        }
        if (_bombCooldown > 0)
        {
            --_bombCooldown;
            if (_hunter == Hunter::Kanden && _bombCooldown == 10 * 2)
            {
                RequireReference(_altModel).SetAnimation(
                    static_cast<std::int32_t>(KandenAltAnim::TailIn), AnimFlags::NoLoop);
            }
        }
        if (_timeSinceMorphCamera != std::numeric_limits<std::uint16_t>::max())
        {
            ++_timeSinceMorphCamera;
        }
        if (_hunter == Hunter::Sylux && _bombOveruse > 0)
        {
            --_bombOveruse;
        }

        if (_deathaltTimer > 0)
        {
            --_deathaltTimer;
            if (!_deathaltEffect)
            {
                _deathaltEffect = scene().SpawnEffectGetEntry(181, UnitX, UnitY, _volume.SpherePosition);
                if (_deathaltEffect)
                {
                    _deathaltEffect->SetElementExtension(true);
                }
            }
            else
            {
                _deathaltEffect->Transform(UnitY, UnitX, _volume.SpherePosition);
            }
        }
        else if (_deathaltEffect)
        {
            scene().UnlinkEffectEntry(_deathaltEffect);
            _deathaltEffect.reset();
        }

        if (TestFlag(_flags2, PlayerFlags2::Cloaking))
        {
            assert(_cloakTimer != 0);
            --_cloakTimer;
            if (_cloakTimer > 0)
            {
                _targetAlpha = 3.0F / 31.0F;
                if (IsMainPlayer())
                {
                    if (_cloakTimer == 210 * 2)
                    {
                        UpdateCloakSfx(1, true);
                    }
                    else if (_cloakTimer == 120 * 2)
                    {
                        UpdateCloakSfx(2, true);
                    }
                }
            }
            else
            {
                _flags2 &= ~PlayerFlags2::Cloaking;
                _targetAlpha = 1.0F;
                _soundSource.PlayFreeSfx(SfxId::CLOAK_OFF);
                if (IsMainPlayer())
                {
                    UpdateCloakSfx(0, false);
                }
            }
        }
        else
        {
            _targetAlpha = 1.0F;
            if ((_hunter == Hunter::Trace || IsPrimeHunter()) && _hSpeedMag < 0.05F
                && _speed.Y < 0.05F && _speed.Y > -0.05F)
            {
                if (_cloakTimer >= 30 * 2)
                {
                    if (_hunter == Hunter::Trace && IsAltForm())
                    {
                        _targetAlpha = 1.0F / 31.0F;
                    }
                    else if (_currentWeapon == BeamType::Imperialist)
                    {
                        _targetAlpha = 5.0F / 31.0F;
                    }
                }
                else
                {
                    ++_cloakTimer;
                }
            }
            else
            {
                _cloakTimer = 0;
            }
        }
        if (_isBot && TestFlag(RequireReference(AiData).Flags3, AiFlags3::Bit2))
        {
            _targetAlpha = 0.0F;
        }
        if (_health > 0)
        {
            if (TestFlag(_flags2, PlayerFlags2::Cloaking) || !TestFlag(_flags2, PlayerFlags2::AltAttack))
            {
                if (_curAlpha < _targetAlpha)
                {
                    _curAlpha += 2.0F / 31.0F / 2.0F;
                    if (_curAlpha > _targetAlpha)
                    {
                        _curAlpha = _targetAlpha;
                    }
                }
                else if (_curAlpha > _targetAlpha)
                {
                    _curAlpha -= 1.0F / 31.0F / 2.0F;
                    if (_curAlpha < _targetAlpha)
                    {
                        _curAlpha = _targetAlpha;
                    }
                }
            }
            else
            {
                _cloakTimer = 0;
                _curAlpha = 1.0F;
                _targetAlpha = 1.0F;
            }
        }
        else if (IsAltForm() || IsMorphing())
        {
            _curAlpha -= 2.0F / 31.0F / 2.0F;
            if (_curAlpha < 0.0F)
            {
                _curAlpha = 0.0F;
            }
        }

        const std::int32_t ammo = RequireReference(_equipInfo).Ammo();
        if (ammo >= 0 && ammo < RequireReference(RequireReference(_equipInfo).Weapon).AmmoCost)
        {
            std::int32_t slot = 0;
            std::int32_t priority = 0;
            for (std::int32_t i = 0; i < 3; ++i)
            {
                const BeamType slotWeap = ManagedAt(_weaponSlots, i);
                if (slotWeap != BeamType::None)
                {
                    const WeaponInfo& slotInfo = RequireReference(ManagedAt(
                    RequireReference(Weapons::Current), static_cast<std::int32_t>(slotWeap)));
                    if (slotInfo.Priority > priority
                        && ManagedAt(_ammo, slotInfo.AmmoType) >= slotInfo.AmmoCost)
                    {
                        priority = slotInfo.Priority;
                        slot = i;
                    }
                }
            }
            if (IsMainPlayer())
            {
                ShowNoAmmoMessage();
            }
            static_cast<void>(TryEquipWeapon(ManagedAt(_weaponSlots, slot)));
        }

        ProcessInput();
        if (TestFlag(_flags1, PlayerFlags1::Boosting)
            && _hSpeedMag <= Fixed::ToFloat(_values.AltMinHSpeed))
        {
            _flags1 &= ~PlayerFlags1::Boosting;
        }
        bool bobWalking = TestFlag(_flags1, PlayerFlags1::Walking);
        if (Mods::Network::NetHooks::IsPuppet(*this))
        {
            bobWalking = !IsAltForm() && !IsMorphing() && !IsUnmorphing()
                && _speed.X * _speed.X + _speed.Z * _speed.Z > BobWalkSpeedSquared;
        }
        if (bobWalking)
        {
            _gunViewBob += 14.0F / 2.0F;
            if (_gunViewBob > 450.0F)
            {
                _gunViewBob -= 180.0F;
            }
            if (_walkViewBob < Fixed::ToFloat(_values.WalkBobMax))
            {
                _walkViewBob += 1.0F / 2.0F;
                if (_walkViewBob > Fixed::ToFloat(_values.WalkBobMax))
                {
                    _walkViewBob = Fixed::ToFloat(_values.WalkBobMax);
                }
            }
        }
        else
        {
            if (_walkViewBob > 0.0F)
            {
                _walkViewBob -= Fixed::ToFloat(_values.WalkBobMax) / 32.0F / 2.0F;
                if (_walkViewBob < 0.0F)
                {
                    _walkViewBob = 0.0F;
                }
            }
            if (_gunViewBob >= 360.0F)
            {
                _gunViewBob += 14.0F / 2.0F;
                if (_gunViewBob > 450.0F)
                {
                    _gunViewBob = 450.0F;
                }
            }
            else
            {
                _gunViewBob -= 14.0F / 2.0F;
                if (_gunViewBob < 270.0F)
                {
                    _gunViewBob = 270.0F;
                }
            }
        }

        if (_healthRecovery > 0)
        {
            if (_tickedHealthRecovery)
            {
                _tickedHealthRecovery = false;
            }
            else
            {
                if (_healthRecovery <= 3)
                {
                    _health = UncheckedAdd(_health, _healthRecovery);
                    _healthRecovery = 0;
                }
                else
                {
                    _health = UncheckedAdd(_health, 3);
                    _healthRecovery = UncheckedSubtract(_healthRecovery, 3);
                    _scrollSfxTimer = 2.0F / 30.0F;
                }
                if (_health > _healthMax)
                {
                    _health = _healthMax;
                }
            }
        }
        else
        {
            _tickedHealthRecovery = false;
        }
        for (std::int32_t i = 0; i < 2; ++i)
        {
            std::int32_t& recovery = ManagedAt(_ammoRecovery, i);
            bool& ticked = ManagedAt(_tickedAmmoRecovery, i);
            std::int32_t& currentAmmo = ManagedAt(_ammo, i);
            if (recovery > 0)
            {
                if (ticked)
                {
                    ticked = false;
                }
                else
                {
                    if (recovery <= 3)
                    {
                        currentAmmo = UncheckedAdd(currentAmmo, recovery);
                        recovery = 0;
                    }
                    else
                    {
                        currentAmmo = UncheckedAdd(currentAmmo, 3);
                        recovery = UncheckedSubtract(recovery, 3);
                        _scrollSfxTimer = 2.0F / 30.0F;
                    }
                    if (currentAmmo > ManagedAt(_ammoMax, i))
                    {
                        currentAmmo = ManagedAt(_ammoMax, i);
                    }
                }
            }
            else
            {
                ticked = false;
            }
        }

        if (_health > 0)
        {
            if (TestFlag(_flags1, PlayerFlags1::Grounded)
                && !TestFlag(_flags1, PlayerFlags1::GroundedPrevious))
            {
                PlayLandingSfx();
            }
            if (IsAltForm())
            {
                UpdateAltMovementSfx();
            }
        }
        UpdateGunAnimation();
        if ((scene().FrameCount() != 0 && scene().FrameCount() % 2 == 0)
            || (_currentWeapon == BeamType::ShockCoil && _gunAnimation == GunAnimation::Shot))
        {
            RequireReference(_gunModel).UpdateAnimFrames();
        }
        if (IsMainPlayer()
            && ManagedAt(RequireReference(RequireReference(_gunModel).AnimInfo->Frame), 0) == 15
            && scene().FrameCount() % 2 == 0
            && (_gunAnimation == GunAnimation::Unknown9 || _gunAnimation == GunAnimation::MissileShot))
        {
            _soundSource.StopSfxByHandle(_missileSfxHandle);
            _missileSfxHandle = PlayMissileSfx(HunterSfx::MissileOpen);
        }

        PickUpItems();
        if (!IsAltForm())
        {
            UpdateAimVecs();
        }
        if (_muzzleEffect)
        {
            const std::int32_t id = ManagedAt(Metadata::MuzzleEffectIds,
                static_cast<std::int32_t>(BeamType::ShockCoil));
            if (_muzzleEffect->IsFinished()
                || (_muzzleEffect->EffectId == id && !TestFlag(_flags1, PlayerFlags1::ShotUncharged)))
            {
                scene().UnlinkEffectEntry(_muzzleEffect);
                _muzzleEffect.reset();
            }
            else if (IsMainPlayer())
            {
                _muzzleEffect->Transform(_gunVec2, _gunVec1, _muzzlePos);
            }
        }

        MphRead::EquipInfo& equipInfo = RequireReference(_equipInfo);
        if (equipInfo.ChargeLevel < RequireReference(equipInfo.Weapon).MinCharge * 2)
        {
            if (_chargeEffect)
            {
                scene().UnlinkEffectEntry(_chargeEffect);
                _chargeEffect.reset();
            }
        }
        else
        {
            if (equipInfo.ChargeLevel == RequireReference(equipInfo.Weapon).MinCharge * 2)
            {
                if (_chargeEffect)
                {
                    scene().UnlinkEffectEntry(_chargeEffect);
                    _chargeEffect.reset();
                }
                const std::int32_t effectId = ManagedAt(
                    Metadata::ChargeEffectIds, static_cast<std::int32_t>(_currentWeapon));
                _chargeEffect = scene().SpawnEffectGetEntry(effectId, _gunVec2, _gunVec1, _muzzlePos);
                if (!IsMainPlayer() && _chargeEffect)
                {
                    _chargeEffect->SetDrawEnabled(false);
                }
                _flags2 &= ~PlayerFlags2::ChargeEffect;
            }
            else if (equipInfo.ChargeLevel == RequireReference(equipInfo.Weapon).FullCharge * 2)
            {
                if (!TestFlag(_flags2, PlayerFlags2::ChargeEffect))
                {
                    if (_chargeEffect)
                    {
                        scene().UnlinkEffectEntry(_chargeEffect);
                        _chargeEffect.reset();
                    }
                    const std::int32_t effectId = ManagedAt(
                        Metadata::ChargeLoopEffectIds, static_cast<std::int32_t>(_currentWeapon));
                    _chargeEffect = scene().SpawnEffectGetEntry(effectId, _gunVec2, _gunVec1, _muzzlePos);
                    if (_chargeEffect)
                    {
                        _chargeEffect->SetElementExtension(true);
                        _flags2 |= PlayerFlags2::ChargeEffect;
                        if (!IsMainPlayer())
                        {
                            _chargeEffect->SetDrawEnabled(false);
                        }
                    }
                }
                RequireReference(_cameraInfo).SetShake(0.023F);
            }
            if (IsMainPlayer() && _chargeEffect)
            {
                _chargeEffect->Transform(_gunVec2, _gunVec1, _muzzlePos);
            }
        }

        if (_frozenTimer == 0)
        {
            UpdateAnimFrames(RequireReference(_bipedModel1));
            UpdateAnimFrames(RequireReference(_bipedModel2));
        }
        if ((IsAltForm() || IsMorphing()) && _frozenTimer == 0)
        {
            UpdateAnimFrames(RequireReference(_altModel));
        }
        if (_hunter == Hunter::Spire && TestFlag(_flags2, PlayerFlags2::AltAttack))
        {
            UpdateSpireAltCollisionPose();
        }
        if (_boostEffect)
        {
            _boostEffect->Transform(_gunVec2, _facingVector, Position);
            if (!IsAltForm() && !IsMorphing())
            {
                scene().UnlinkEffectEntry(_boostEffect);
                _boostEffect.reset();
            }
            else if (!TestFlag(_flags1, PlayerFlags1::Boosting))
            {
                if (_boostEffect->IsFinished())
                {
                    scene().UnlinkEffectEntry(_boostEffect);
                    _boostEffect.reset();
                }
                else
                {
                    _boostEffect->SetElementExtension(false);
                }
            }
        }
        if (_furlEffect)
        {
            _furlEffect->Transform(_gunVec2, _facingVector, Position);
            if ((!IsAltForm() && !IsMorphing()) || _furlEffect->IsFinished())
            {
                scene().UnlinkEffectEntry(_furlEffect);
                _furlEffect.reset();
            }
        }
        if (_hunter == Hunter::Samus)
        {
            UpdateMorphBallTrail();
        }

        if (_timeSinceDamage != std::numeric_limits<std::uint16_t>::max())
        {
            ++_timeSinceDamage;
        }
        if (_timeSincePickup != std::numeric_limits<std::uint16_t>::max())
        {
            ++_timeSincePickup;
        }
        if (_timeSinceHeal != std::numeric_limits<std::uint16_t>::max())
        {
            ++_timeSinceHeal;
        }
        if (!TestFlag(_flags1, PlayerFlags1::Standing)
            && _timeSinceStanding != std::numeric_limits<std::uint16_t>::max())
        {
            ++_timeSinceStanding;
        }
        if (_field449 != std::numeric_limits<std::uint16_t>::max())
        {
            ++_field449;
        }
        if (_input.HasInput || (_isBot && !TestFlag(RequireReference(AiData).Flags3, AiFlags3::NoInput)))
        {
            _timeSinceInput = 0;
        }
        else
        {
            _timeSinceInput = ManagedUInt16Add(_timeSinceInput, 1);
        }

        if (_aimY < 60.0F && _aimY > -60.0F && !equipInfo.Zoomed && _health > 0
            && !Features::NoIdleSway())
        {
            std::int32_t swayStart = UncheckedMultiply(_values.SwayStartTime, 2);
            if (Features::DelayedIdleSway())
            {
                swayStart = UncheckedMultiply(swayStart, 4);
            }
            if (_timeSinceInput == swayStart)
            {
                _field40C = 0.0F;
                const float factor1 = static_cast<float>(
                    static_cast<std::int64_t>(Rng::GetRandomInt2(_values.SwayLimit))
                    - static_cast<std::int64_t>(_values.SwayLimit / 2)) / 4096.0F;
                const float factor2 = static_cast<float>(
                    static_cast<std::int64_t>(Rng::GetRandomInt2(_values.SwayLimit))
                    - static_cast<std::int64_t>(_values.SwayLimit / 2)) / 4096.0F;
                _field410 = _facingVector;
                _field41C = _field410;
                _field428 = _field410;
                _field41C = Add(_field41C, Add(ScaleVector(_gunVec2, factor1), ScaleVector(_upVector, factor2)));
            }
            else if (_timeSinceInput > swayStart)
            {
                _field40C += 1.0F / static_cast<float>(_values.SwayIncrement) / 2.0F;
                if (_field40C >= 1.0F)
                {
                    _field40C = 0.0F;
                    const float factor1 = static_cast<float>(
                        static_cast<std::int64_t>(Rng::GetRandomInt2(_values.SwayLimit))
                        - static_cast<std::int64_t>(_values.SwayLimit / 2)) / 4096.0F;
                    const float factor2 = static_cast<float>(
                        static_cast<std::int64_t>(Rng::GetRandomInt2(_values.SwayLimit))
                        - static_cast<std::int64_t>(_values.SwayLimit / 2)) / 4096.0F;
                    _field410 = _field41C;
                    _field41C = _field428;
                    _field41C = Add(_field41C, Add(ScaleVector(_gunVec2, factor1), ScaleVector(_upVector, factor2)));
                }
                const float angle = 180.0F * _field40C + 180.0F;
                const float factor = (std::cos(DegreesToRadians(angle)) + 1.0F) / 2.0F;
                _facingVector = Add(_field410, ScaleVector(Subtract(_field41C, _field410), factor));
                _facingVector = Normalize(_facingVector);
            }
        }

        if (_attachedEnemy && !IsAltForm()
            && (Biped2Anim() != PlayerAnimation::Unmorph
                || TestFlag(Biped2Flags(), AnimFlags::Ended)))
        {
            if (_attachedEnemy->EnemyType() == EnemyType::Temroid)
            {
                auto temroid = std::dynamic_pointer_cast<Enemies::Enemy02Entity>(_attachedEnemy);
                if (!temroid)
                {
                    throw SceneDetail::InvalidCastException();
                }
                temroid->UpdateAttached(this);
            }
            else if (_attachedEnemy->EnemyType() == EnemyType::Quadtroid)
            {
                auto quadtroid = std::dynamic_pointer_cast<Enemies::Enemy37Entity>(_attachedEnemy);
                if (!quadtroid)
                {
                    throw SceneDetail::InvalidCastException();
                }
                quadtroid->UpdateAttached(this);
            }
        }

        if (equipInfo.SmokeLevel < RequireReference(equipInfo.Weapon).SmokeStart * 2)
        {
            if (equipInfo.SmokeLevel > RequireReference(equipInfo.Weapon).SmokeMinimum * 2)
            {
                if (TestFlag(_flags1, PlayerFlags1::DrawGunSmoke) && _smokeAlpha < 1.0F)
                {
                    _smokeAlpha = MathMin(_smokeAlpha + 1.0F / 31.0F / 2.0F, 1.0F);
                }
            }
            else if (_smokeAlpha > 0.0F)
            {
                _smokeAlpha = MathMax(_smokeAlpha - 1.0F / 31.0F / 2.0F, 0.0F);
            }
            else if (TestFlag(_flags1, PlayerFlags1::DrawGunSmoke))
            {
                equipInfo.SmokeLevel = 0;
                _flags1 &= ~PlayerFlags1::DrawGunSmoke;
            }
        }
        else
        {
            if (!TestFlag(_flags1, PlayerFlags1::DrawGunSmoke))
            {
                _flags1 |= PlayerFlags1::DrawGunSmoke;
                RequireReference(_gunSmokeModel).SetAnimation(0);
            }
            if (_smokeAlpha < 1.0F)
            {
                _smokeAlpha = MathMin(_smokeAlpha + 1.0F / 31.0F / 2.0F, 1.0F);
            }
        }
        if (TestFlag(_flags1, PlayerFlags1::DrawGunSmoke))
        {
            UpdateAnimFrames(RequireReference(_gunSmokeModel));
        }

        if (_health == 0)
        {
            if (_timeSinceDead != std::numeric_limits<std::uint16_t>::max())
            {
                ++_timeSinceDead;
            }
            if (GameState::SinglePlayer() && IsMainPlayer() && _deathCountdown > 0.0F)
            {
                _deathCountdown -= scene().FrameTime();
                const float pct = (150.0F / 30.0F - _deathCountdown) / (150.0F / 30.0F);
                RequireReference(_cameraInfo).SetShake(0.15F * pct);
                if (_lostOctolithEnemyIndex != -1 && _deathCountdown <= 119.0F / 30.0F)
                {
                    if (!_deathLostOctolithSfxPlayed)
                    {
                        _soundSource.PlayFreeSfx(SfxId::DIE_LOSE_CRYSTAL);
                        _deathLostOctolithSfxPlayed = true;
                    }
                    if (_deathCountdown <= 90.0F / 30.0F)
                    {
                        if (!_deathLostOctolithDialogShown)
                        {
                            ShowDialog(DialogType::Hud, 117, 90, 1);
                            _deathLostOctolithDialogShown = true;
                        }
                    }
                    else if (_deathCountdown >= 117.0F / 30.0F)
                    {
                        RequireReference(_cameraInfo).SetShake(0.4F);
                    }
                    const auto [lostSpeed, displacement] = Drag(0.88F, _lostOctolithSpeed);
                    _lostOctolithSpeed = lostSpeed;
                    if (!IsAltForm())
                    {
                        _lostOctolithDrawPos = AddZ(
                            AddX(_lostOctolithDrawPos, _field70 * displacement),
                            _field74 * displacement);
                    }
                    else
                    {
                        _lostOctolithDrawPos = AddZ(
                            AddX(_lostOctolithDrawPos, -_field80 * displacement),
                            -_field84 * displacement);
                    }
                }
                if (!IsAltForm())
                {
                    _facingVector.Y = ExponentialDecay(0.9F, _facingVector.Y);
                    _facingVector = Normalize(_facingVector);
                    _gunVec1 = _facingVector;
                }
                if (_deathCountdown <= 1.0F / 30.0F && !_deathProcessed)
                {
                    _deathProcessed = true;
                    _flags2 |= PlayerFlags2::HideModel;
                    _flags1 &= ~PlayerFlags1::AltForm;
                    _flags1 &= ~PlayerFlags1::Morphing;
                    _flags1 &= ~PlayerFlags1::Unmorphing;
                    if (_lostOctolithEnemyIndex != -1)
                    {
                        StorySave& storySave = RequireReference(GameState::StorySave);
                        const std::int32_t octolithCount
                            = static_cast<std::int32_t>(std::popcount(
                                static_cast<std::uint32_t>(storySave.CurrentOctoliths)));
                        const std::uint32_t lostNum = Rng::GetRandomInt2(octolithCount);
                        std::uint32_t curNum = 0;
                        for (std::int32_t i = 0; i < 8; ++i)
                        {
                            const std::int32_t bit = 1 << i;
                            if ((storySave.CurrentOctoliths & bit) != 0)
                            {
                                if (curNum == lostNum)
                                {
                                    PlayerEntity& enemyPlayer = RequireReference(
                                        ManagedAt(_players, _lostOctolithEnemyIndex));
                                    const std::int32_t hunter = static_cast<std::int32_t>(enemyPlayer._hunter);
                                    storySave.CurrentOctoliths = static_cast<std::uint16_t>(
                                        storySave.CurrentOctoliths & static_cast<std::uint16_t>(~bit));
                                    const std::int32_t shift = 4 * i;
                                    const std::uint32_t clearMask
                                        = ~(std::uint32_t{15} << static_cast<std::uint32_t>(shift));
                                    const std::uint32_t hunterBits
                                        = static_cast<std::uint32_t>(hunter)
                                            << static_cast<std::uint32_t>(shift);
                                    const std::int32_t packed = std::bit_cast<std::int32_t>(
                                        clearMask | hunterBits);
                                    storySave.LostOctoliths = storySave.LostOctoliths
                                        & std::bit_cast<std::uint32_t>(packed);
                                    const std::int32_t areaIndex = scene().AreaId() / 2;
                                    std::uint8_t& areaHunters = ManagedAt(RequireReference(storySave.AreaHunters), areaIndex);
                                    areaHunters = static_cast<std::uint8_t>(areaHunters
                                        & static_cast<std::uint8_t>(~(1 << hunter)));
                                    break;
                                }
                                ++curNum;
                            }
                        }
                    }
                }
            }
            else if (_respawnTimer <= 1)
            {
                _flags2 |= PlayerFlags2::HideModel;
            }
        }

        if (!equipInfo.Zoomed && Formats::CameraSequence::Current() == nullptr)
        {
            float currentFov = RequireReference(_cameraInfo).Fov;
            const float normalFov = Fixed::ToFloat(_values.NormalFov) * 2.0F;
            const float diff = normalFov - currentFov;
            if (std::abs(diff) >= 0.1F * 2.0F)
            {
                currentFov += diff / 4.0F;
                RequireReference(_cameraInfo).Fov = currentFov;
            }
            else
            {
                RequireReference(_cameraInfo).Fov = normalFov;
            }
        }

        if (TestFlag(Biped2Flags(), AnimFlags::Ended))
        {
            if (IsMorphing())
            {
                _flags1 &= ~PlayerFlags1::Morphing;
                UpdateForm(true);
            }
            else if (IsUnmorphing())
            {
                if (IsMainPlayer() && Formats::CameraSequence::Current() != nullptr)
                {
                    Formats::CameraSequence::Current()->InitialCamInfo().NodeRef = NodeRef;
                }
                else
                {
                    RequireReference(_cameraInfo).NodeRef = NodeRef;
                }
                _flags1 &= ~PlayerFlags1::Unmorphing;
                if (_burnTimer > 0)
                {
                    CreateBurnEffect();
                }
            }
        }

        UpdateLightSources(_volume.SpherePosition);
        if (NodeRef != Formats::Culling::NodeRef::None)
        {
            std::int32_t index = TestFlag(_flags1, PlayerFlags1::AltFormPrevious) ? 2 : 0;
            Vector3 prevPos = Add(_prevPosition,
                ManagedAt(ManagedAt(PlayerVolumes, static_cast<std::int32_t>(_hunter)), index).SpherePosition);
            index = IsAltForm() ? 2 : 0;
            const Vector3 curPos = Add(static_cast<Vector3>(Position),
                ManagedAt(ManagedAt(PlayerVolumes, static_cast<std::int32_t>(_hunter)), index).SpherePosition);
            NodeRef = scene().UpdateNodeRef(NodeRef, prevPos, curPos);
            if (Formats::CameraSequence::Current() == nullptr || !IsMainPlayer())
            {
                MphRead::Entities::CameraInfo& cameraInfo = RequireReference(_cameraInfo);
                if (_cameraType == CameraType::Free)
                {
                    cameraInfo.NodeRef = scene().UpdateNodeRef(
                        cameraInfo.NodeRef, cameraInfo.PrevPosition, cameraInfo.Position);
                }
                else if (_cameraType != CameraType::Spectator)
                {
                    cameraInfo.NodeRef = scene().UpdateNodeRef(NodeRef, curPos, cameraInfo.Position);
                }
            }
        }

        if (TestFlag(_flags1, PlayerFlags1::Standing)
            && (_timeStanding == 0 || scene().FrameCount() % (4 * 2) == 0)
            && (TestFlag(_flags1, PlayerFlags1::OnAcid)
                || (TestFlag(_flags1, PlayerFlags1::OnLava) && _hunter != Hunter::Spire)))
        {
            DamageFlags flags = DamageFlags::IgnoreInvuln;
            if (TestFlag(_flags1, PlayerFlags1::OnLava))
            {
                flags |= DamageFlags::NoSfx;
            }
            TakeDamage(1, flags, std::nullopt, nullptr);
        }

        assert(scene().Room() != nullptr);
        RoomEntity& room = RequireReference(scene().Room());
        if (GameState::Multiplayer() && room.Meta().HasLimits)
        {
            if (Position.Y < room.Meta().PlayerMin.Y)
            {
                TakeDamage(0, DamageFlags::Death, std::nullopt, nullptr);
            }
            Position = Clamp(Position,
                WithY(room.Meta().PlayerMin, Position.Y), room.Meta().PlayerMax);
        }
        if (Position.Y < room.Meta().KillHeight)
        {
            TakeDamage(0, DamageFlags::Death, std::nullopt, nullptr);
        }
        _flags2 &= ~PlayerFlags2::NoFormSwitch;

        if (_doubleDmgTimer > 0)
        {
            --_doubleDmgTimer;
            if (IsMainPlayer())
            {
                if (_doubleDmgTimer == 0)
                {
                    UpdateDoubleDamageSfx(0, false);
                    if (_doubleDmgEffect)
                    {
                        scene().UnlinkEffectEntry(_doubleDmgEffect);
                        _doubleDmgEffect.reset();
                    }
                }
                else
                {
                    if (!IsAltForm() && !IsMorphing() && !IsUnmorphing())
                    {
                        if (_doubleDmgEffect)
                        {
                            _doubleDmgEffect->Transform(_upVector, _gunVec1, _muzzlePos);
                        }
                        else
                        {
                            _doubleDmgEffect = scene().SpawnEffectGetEntry(
                                244, _upVector, _gunVec1, _muzzlePos);
                            if (_doubleDmgEffect)
                            {
                                _doubleDmgEffect->SetElementExtension(true);
                            }
                        }
                    }
                    else if (_doubleDmgEffect)
                    {
                        scene().UnlinkEffectEntry(_doubleDmgEffect);
                        _doubleDmgEffect.reset();
                    }
                    if (_doubleDmgTimer == 210 * 2)
                    {
                        UpdateDoubleDamageSfx(1, true);
                        UpdateDoubleDamageSpeed(2);
                    }
                    else if (_doubleDmgTimer == 120 * 2)
                    {
                        UpdateDoubleDamageSfx(2, true);
                        UpdateDoubleDamageSpeed(3);
                    }
                }
            }
        }

        if (_burnTimer > 0)
        {
            --_burnTimer;
            if (_burnTimer % (8 * 2) == 0)
            {
                TakeDamage(1, DamageFlags::NoSfx | DamageFlags::Burn | DamageFlags::NoDmgInvuln,
                    std::nullopt, _burnedBy.get());
            }
            if (_burnEffect)
            {
                Formats::CameraSequence* sequence = Formats::CameraSequence::Current();
                if (sequence != nullptr && sequence->BlockInput())
                {
                    scene().UnlinkEffectEntry(_burnEffect);
                    _burnEffect.reset();
                }
                else if (!IsMainPlayer() || IsAltForm() || IsMorphing())
                {
                    const Vector3 facing(_field70, 0.0F, _field74);
                    _burnEffect->Transform(facing, UnitY, _volume.SpherePosition);
                }
                else
                {
                    _burnEffect->Transform(_upVector, _gunVec1, _muzzlePos);
                }
            }
        }
        else if (_burnEffect)
        {
            scene().UnlinkEffectEntry(_burnEffect);
            _burnEffect.reset();
        }
        return true;
    }

    void PlayerEntity::ActivateJumpPad(
        JumpPadEntity* jumpPad, Vector3 vector, std::uint16_t lockTime)
    {
        if (_timeSinceJumpPad > 5 * 2)
        {
            _soundSource.PlaySfx(SfxId::JUMP_PAD);
        }
        _speed = vector;
        _jumpPadAccel = vector;
        if (jumpPad == nullptr)
        {
            _lastJumpPad.reset();
        }
        else
        {
            _lastJumpPad = SharedFrom(jumpPad);
        }
        _flags1 |= PlayerFlags1::UsedJumpPad;
        lockTime = ManagedUInt16Multiply(lockTime, 2);
        _jumpPadControlLock = lockTime;
        _jumpPadControlLockMin = std::max(lockTime, static_cast<std::uint16_t>(5 * 2));
        _timeSinceJumpPad = 0;
        _flags1 &= ~PlayerFlags1::UsedJump;
        _flags1 |= PlayerFlags1::Standing;
        if (IsAltForm())
        {
            const float accelY = _jumpPadAccel.Y;
            const float altGrav = Fixed::ToFloat(_values.AltAirGravity);
            const float bipedGrav = Fixed::ToFloat(_values.BipedGravity);
            const float altFactor = -accelY / altGrav;
            const float bipedFactor = -accelY / bipedGrav;
            const float lockInc = ((accelY * bipedFactor)
                + (bipedGrav * (bipedFactor * bipedFactor) / 2.0F)
                - ((accelY * altFactor) + (altGrav * (altFactor * altFactor) / 2.0F)))
                / accelY + 2.0F;
            _jumpPadControlLock = ManagedUInt16Add(
                _jumpPadControlLock, FloatToUInt16Unchecked(lockInc * 2.0F));
        }
    }

    void PlayerEntity::PickUpItems()
    {
        const auto scene = [&]() -> Scene& { return RequireReference(_scene); };
        if (_health == 0 || (_isBot && GameState::SinglePlayer()) || _ignoreItemPickups)
        {
            return;
        }
        if (IsMainPlayer())
        {
            Formats::CameraSequence* sequence = Formats::CameraSequence::Current();
            if (sequence != nullptr && sequence->BlockInput())
            {
                return;
            }
        }
        float distSqr = _volume.SphereRadius + 0.45F;
        distSqr *= distSqr;
        auto enumerator = scene().GetItemInstanceEntities().GetEnumerator();
        while (enumerator.MoveNext())
        {
            std::shared_ptr<ItemInstanceEntity> itemPtr = enumerator.Current();
            ItemInstanceEntity& item = RequireReference(itemPtr);
            if (!Mods::Network::NetHealthSync::OwnsPickup(item) || item.DespawnTimer() == 0)
            {
                continue;
            }
            bool inRange = false;
            if (IsAltForm())
            {
                const Vector3 between = Subtract(item.Position, _volume.SpherePosition);
                if (Vector3::Dot(between, between) < distSqr)
                {
                    inRange = true;
                }
            }
            else
            {
                const Vector3 between = Subtract(item.Position, Position);
                const Vector3 lateral = WithY(between, 0.0F);
                if (Vector3::Dot(lateral, lateral) < distSqr
                    && between.Y >= Fixed::ToFloat(_values.MinPickupHeight)
                    && between.Y <= Fixed::ToFloat(_values.MaxPickupHeight))
                {
                    inRange = true;
                }
            }
            if (!inRange)
            {
                continue;
            }

            const auto playSfx = [&](SfxId sfx)
            {
                if (IsMainPlayer() && Sound::Sfx::TimedSfxMute == 0)
                {
                    _soundSource.PlayFreeSfx(sfx);
                }
            };

            bool pickedUp = false;
            const ItemType itemType = item.ItemType();
            switch (itemType)
            {
            case ItemType::HealthMedium:
            case ItemType::HealthSmall:
            case ItemType::HealthBig:
                if (!IsPrimeHunter())
                {
                    pickedUp = true;
                    _timeSinceHeal = 0;
                    GainHealth(ManagedAt(_healthPickupAmounts, static_cast<std::int32_t>(itemType)));
                    PlayHealthPickupSfx(itemType);
                }
                break;
            case ItemType::UASmall:
            case ItemType::UABig:
            case ItemType::MissileSmall:
            case ItemType::MissileBig:
            {
                pickedUp = true;
                const std::int32_t slot
                    = itemType == ItemType::UASmall || itemType == ItemType::UABig ? 0 : 1;
                _timeSincePickup = 0;
                std::int32_t amount;
                if (itemType == ItemType::UABig || itemType == ItemType::MissileBig)
                {
                    amount = GameState::Multiplayer() ? 100 : 250;
                    playSfx(SfxId::AMMO_POWER_UP2);
                }
                else
                {
                    amount = GameState::Multiplayer() ? 50 : 100;
                    playSfx(SfxId::AMMO_POWER_UP1);
                }
                std::int32_t& currentAmmo = ManagedAt(_ammo, slot);
                currentAmmo = UncheckedAdd(currentAmmo, amount);
                if (currentAmmo > ManagedAt(_ammoMax, slot))
                {
                    currentAmmo = ManagedAt(_ammoMax, slot);
                }
                break;
            }
            case ItemType::VoltDriver:
            case ItemType::Battlehammer:
            case ItemType::Imperialist:
            case ItemType::Judicator:
            case ItemType::Magmaul:
            case ItemType::ShockCoil:
            case ItemType::OmegaCannon:
            case ItemType::AffinityWeapon:
                pickedUp = true;
                PickUpWeapon(itemType);
                break;
            case ItemType::DoubleDamage:
                pickedUp = true;
                _timeSincePickup = 0;
                _doubleDmgTimer = 900 * 2;
                if (IsMainPlayer())
                {
                    _soundSource.PlayFreeSfx(SfxId::DOUBLE_DAMAGE_POWER_UP);
                    UpdateDoubleDamageSfx(0, true);
                    UpdateDoubleDamageSpeed(1);
                }
                break;
            case ItemType::Cloak:
                pickedUp = true;
                _timeSincePickup = 0;
                _cloakTimer = 900 * 2;
                _flags2 |= PlayerFlags2::Cloaking;
                if (IsMainPlayer())
                {
                    _soundSource.PlayFreeSfx(SfxId::CLOAK_POWER_UP);
                    UpdateCloakSfx(0, true);
                }
                break;
            case ItemType::Deathalt:
                pickedUp = true;
                _timeSincePickup = 0;
                _deathaltTimer = 900 * 2;
                if (!IsAltForm() && !IsMorphing())
                {
                    static_cast<void>(TrySwitchForms(true));
                }
                if (IsMainPlayer())
                {
                    _soundSource.PlayFreeSfx(SfxId::DOUBLE_DAMAGE_POWER_UP);
                }
                break;
            case ItemType::EnergyTank:
                if (!_isBot)
                {
                    pickedUp = true;
                    _timeSincePickup = 0;
                    _healthMax = UncheckedAdd(_healthMax, _values.EnergyTank);
                    _healthRecovery = UncheckedSubtract(_healthMax, _health);
                    RequireReference(GameState::StorySave).HealthMax = _healthMax;
                    if (IsMainPlayer())
                    {
                        ShowDialog(DialogType::Event, 4,
                            static_cast<std::int32_t>(EventType::EnergyTank));
                    }
                }
                break;
            case ItemType::MissileExpansion:
                if (!_isBot)
                {
                    pickedUp = true;
                    _timeSincePickup = 0;
                    ManagedAt(_ammoMax, 1) = UncheckedAdd(ManagedAt(_ammoMax, 1), 100);
                    ManagedAt(_ammoRecovery, 1)
                        = UncheckedSubtract(ManagedAt(_ammoMax, 1), ManagedAt(_ammo, 1));
                    ManagedAt(RequireReference(RequireReference(GameState::StorySave).AmmoMax), 1)
                        = ManagedAt(_ammoMax, 1);
                    if (IsMainPlayer())
                    {
                        ShowDialog(DialogType::Event, 3,
                            static_cast<std::int32_t>(EventType::MissileTank));
                    }
                }
                break;
            case ItemType::UAExpansion:
                if (!_isBot)
                {
                    pickedUp = true;
                    _timeSincePickup = 0;
                    ManagedAt(_ammoMax, 0) = UncheckedAdd(ManagedAt(_ammoMax, 0), 300);
                    ManagedAt(_ammoRecovery, 0)
                        = UncheckedSubtract(ManagedAt(_ammoMax, 0), ManagedAt(_ammo, 0));
                    ManagedAt(RequireReference(RequireReference(GameState::StorySave).AmmoMax), 0)
                        = ManagedAt(_ammoMax, 0);
                    if (IsMainPlayer())
                    {
                        ShowDialog(DialogType::Event, 46,
                            static_cast<std::int32_t>(EventType::UATank));
                    }
                }
                break;
            case ItemType::ArtifactKey:
                pickedUp = true;
                if (IsMainPlayer())
                {
                    _soundSource.PlayFreeSfx(SfxId::KEY_PICKUP);
                }
                break;
            default:
                pickedUp = true;
                break;
            }
            if (pickedUp)
            {
                item.OnPickedUp(this);
            }
        }
    }

    void PlayerEntity::PlayHealthPickupSfx(ItemType itemType)
    {
        if (!IsMainPlayer() || Sound::Sfx::TimedSfxMute != 0
            || !Mods::Multiplayer::MapResourceRules::IsHealth(itemType))
        {
            return;
        }
        (void)_soundSource.PlayFreeSfx(itemType == ItemType::HealthSmall ? SfxId::POWER_UP1 : SfxId::POWER_UP2);
    }

    void PlayerEntity::PickUpWeapon(ItemType itemType)
    {
        BeamType weapon;
        if (itemType == ItemType::AffinityWeapon)
        {
            if (_hunter == Hunter::Samus || _hunter == Hunter::Guardian)
            {
                ManagedAt(_ammo, 1) = std::min(
                    UncheckedAdd(ManagedAt(_ammo, 1), 50), ManagedAt(_ammoMax, 1));
                if (IsMainPlayer() && Sound::Sfx::TimedSfxMute == 0)
                {
                    _soundSource.PlayFreeSfx(SfxId::AMMO_POWER_UP1);
                }
                return;
            }
            weapon = Weapons::GetAffinityBeam(_hunter);
        }
        else
        {
            switch (itemType)
            {
            case ItemType::VoltDriver: weapon = BeamType::VoltDriver; break;
            case ItemType::Battlehammer: weapon = BeamType::Battlehammer; break;
            case ItemType::Imperialist: weapon = BeamType::Imperialist; break;
            case ItemType::Judicator: weapon = BeamType::Judicator; break;
            case ItemType::Magmaul: weapon = BeamType::Magmaul; break;
            case ItemType::ShockCoil: weapon = BeamType::ShockCoil; break;
            case ItemType::OmegaCannon: weapon = BeamType::OmegaCannon; break;
            default: weapon = BeamType::None; break;
            }
        }
        if (weapon == BeamType::None)
        {
            return;
        }

        std::shared_ptr<MphRead::StorySave> storySave = GameState::StorySave;
        const std::int32_t weaponId = static_cast<std::int32_t>(weapon);
        if (GameState::SinglePlayer()
            && (RequireReference(storySave).Weapons & (1 << weaponId)) == 0)
        {
            storySave->Weapons = static_cast<std::uint16_t>(storySave->Weapons | (1 << weaponId));
            const std::string value1 = ManagedAt(Metadata::WeaponNamesUpper, weaponId);
            std::string value2;
            const std::int32_t messageId = ManagedAt(Metadata::WeaponMessageIds, weaponId);
            if (messageId != 0)
            {
                value2 = Text::Strings::GetHudMessage(messageId);
            }
            ShowDialog(DialogType::Event, 5, weaponId, 0, value1, value2);
        }

        const WeaponInfo& info = RequireReference(ManagedAt(RequireReference(Weapons::Current), weaponId));
        std::int32_t& ammo = ManagedAt(_ammo, info.AmmoType);
        if (ammo < 60)
        {
            ammo = std::min(UncheckedAdd(ammo, 60), 60);
        }
        if (!_availableWeapons[weapon])
        {
            _availableWeapons[weapon] = true;
            _availableCharges[weapon] = true;
            const BeamType slot2Weapon = ManagedAt(_weaponSlots, 2);
            const std::int32_t slot2Index = static_cast<std::int32_t>(slot2Weapon);
            const BeamType affinityWeapon = Weapons::GetAffinityBeam(_hunter);
            if (slot2Weapon == BeamType::None || weapon == BeamType::OmegaCannon
                || ((info.Priority > RequireReference(ManagedAt(RequireReference(Weapons::Current), slot2Index)).Priority
                        || weapon == affinityWeapon)
                    && (!TestFlag(_flags2, PlayerFlags2::Shooting)
                        || _currentWeapon != slot2Weapon)))
            {
                if ((info.Priority > RequireReference(RequireReference(_equipInfo).Weapon).Priority
                        || weapon == BeamType::OmegaCannon || weapon == affinityWeapon)
                    && !TestFlag(_flags2, PlayerFlags2::Shooting))
                {
                    if (!TryEquipWeapon(weapon))
                    {
                        UpdateAffinityWeaponSlot(weapon, 2);
                    }
                }
                else if (!TestFlag(_flags2, PlayerFlags2::Shooting)
                    || _currentWeapon != slot2Weapon)
                {
                    UpdateAffinityWeaponSlot(weapon, 2);
                }
            }
            if (IsMainPlayer() && Sound::Sfx::TimedSfxMute == 0)
            {
                _soundSource.PlayFreeSfx(SfxId::WEAPON_POWER_UP);
            }
        }
        else if (IsMainPlayer() && Sound::Sfx::TimedSfxMute == 0)
        {
            _soundSource.PlayFreeSfx(SfxId::AMMO_POWER_UP1);
        }
    }

    void PlayerEntity::GainHealth(std::uint32_t health)
    {
        GainHealth(UInt32ToInt32(health));
    }

    void PlayerEntity::GainHealth(std::int32_t health)
    {
        if (_health > 0)
        {
            if (TestFlag(_flags2, PlayerFlags2::Halfturret))
            {
                HalfturretEntity& halfturret = RequireReference(_halfturret);
                std::int32_t turretHealth = halfturret.Health();
                if (_health <= turretHealth)
                {
                    _health = UncheckedAdd(_health, UncheckedSubtract(health, health / 2));
                    turretHealth = UncheckedAdd(turretHealth, health / 2);
                }
                else
                {
                    _health = UncheckedAdd(_health, health / 2);
                    turretHealth = UncheckedAdd(turretHealth, UncheckedSubtract(health, health / 2));
                }
                if (turretHealth > 100)
                {
                    turretHealth = 100;
                }
                halfturret.SetHealth(turretHealth);
            }
            else
            {
                _health = UncheckedAdd(_health, health);
            }
            if (_health > _healthMax)
            {
                _health = _healthMax;
            }
        }
    }

    bool PlayerEntity::TrySwitchForms(bool force)
    {
        if (!force && (IsMorphing() || IsUnmorphing() || _frozenTimer > 0 || _field6D0
                || _deathaltTimer > 0 || TestFlag(_flags2, PlayerFlags2::NoFormSwitch)
                || (!IsAltForm() && TestFlag(_flags2, PlayerFlags2::BipedStuck))
                || (IsAltForm() && _morphCamera)))
        {
            if (IsMainPlayer())
            {
                Formats::CameraSequence* sequence = Formats::CameraSequence::Current();
                if (sequence == nullptr || !sequence->BlockInput())
                {
                    _soundSource.PlayFreeSfx(SfxId::BEAM_SWITCH_FAIL);
                }
            }
            return false;
        }
        if (_hunter == Hunter::Guardian)
        {
            return false;
        }

        const auto afterSwitch = [&]
        {
            UpdateZoom(false);
            RequireReference(_equipInfo).ChargeLevel = 0;
            RequireReference(_equipInfo).SmokeLevel = 0;
            if (_burnTimer > 0)
            {
                CreateBurnEffect();
            }
        };

        if (!IsAltForm())
        {
            EnterAltForm();
            afterSwitch();
            return true;
        }
        if (!TestFlag(_flags1, PlayerFlags1::NoUnmorph))
        {
            ExitAltForm();
            afterSwitch();
            return true;
        }
        if (IsMainPlayer())
        {
            _soundSource.PlayFreeSfx(SfxId::BEAM_SWITCH_FAIL);
        }
        return false;
    }

    void PlayerEntity::UpdateAimVecs()
    {
        const Vector3 facing = _facingVector;
        const Vector3 up = _upVector;
        _gunDrawPos = Add(
            Add(
                Add(ScaleVector(facing, Fixed::ToFloat(_values.FieldB8)), RequireReference(_cameraInfo).Position),
                ScaleVector(_gunVec2, Fixed::ToFloat(_values.FieldB0))),
            ScaleVector(up, Fixed::ToFloat(_values.FieldB4)));
        const float cosValue = std::cos(DegreesToRadians(_gunViewBob));
        _gunDrawPos.Y += Fixed::ToFloat(20) * cosValue;
        if (Features::FixedWeapon())
        {
            _aimVec = facing;
        }
        else
        {
            _aimVec = Subtract(_aimPosition, _gunDrawPos);
            const float dot = Vector3::Dot(_aimVec, facing);
            const Vector3 vec = ScaleVector(facing, dot);
            _aimVec = Normalize(Add(_aimVec, Divide(Subtract(vec, _aimVec), 2.0F)));
        }
        _muzzlePos = Add(_gunDrawPos, ScaleVector(_aimVec, Fixed::ToFloat(_values.MuzzleOffset)));
    }

    void PlayerEntity::InitAltTransform()
    {
        _field4E8 = _gunVec2;
        const Vector3 up = _upVector;
        _modelTransform.M11 = _gunVec2.X;
        _modelTransform.M12 = 0.0F;
        _modelTransform.M13 = _gunVec2.Z;
        _modelTransform.M21 = up.X;
        _modelTransform.M22 = up.Y;
        _modelTransform.M23 = up.Z;
        _modelTransform.M31 = _field70;
        _modelTransform.M32 = 0.0F;
        _modelTransform.M33 = _field74;
        SetRow2(_modelTransform, Vector3::Cross(Row0(_modelTransform), Row1(_modelTransform)));
        SetRow1(_modelTransform, Vector3::Cross(Row2(_modelTransform), Row0(_modelTransform)));
        SetRow0(_modelTransform, Normalize(Row0(_modelTransform)));
        SetRow1(_modelTransform, Normalize(Row1(_modelTransform)));
        SetRow2(_modelTransform, Normalize(Row2(_modelTransform)));
    }

    void PlayerEntity::UpdateAltTransform()
    {
        if (_hunter == Hunter::Noxus)
        {
            _altWobble += (5.0F - _altWobble) / 32.0F / 2.0F;
            _altWobble = MathClamp(_altWobble,
                Fixed::ToFloat(_values.AltMinWobble), Fixed::ToFloat(_values.AltMaxWobble));
            _altTiltX -= _altTiltX / 8.0F / 2.0F;
            _altTiltZ -= _altTiltZ / 8.0F / 2.0F;
            _altTiltX += -(_altTiltX + Fixed::ToFloat(25) * (_speed.X - _prevSpeed.X))
                / 32.0F / 2.0F;
            _altTiltZ += -(_altTiltZ + Fixed::ToFloat(25) * (_speed.Z - _prevSpeed.Z))
                / 32.0F / 2.0F;
            const float minSpinAccel = Fixed::ToFloat(_values.AltMinSpinAccel);
            const float maxSpinAccel = Fixed::ToFloat(_values.AltMaxSpinAccel);
            _altSpinSpeed += (minSpinAccel
                + (_altAttackTime * (maxSpinAccel - minSpinAccel)
                    / static_cast<float>(_values.AltAttackStartup * 2))
                - _altSpinSpeed) / 32.0F / 2.0F;
            _altSpinSpeed = MathClamp(_altSpinSpeed,
                Fixed::ToFloat(_values.AltMinSpinSpeed), Fixed::ToFloat(_values.AltMaxSpinSpeed));
            _altSpinRot += _altSpinSpeed / 2.0F;
            while (_altSpinRot > 360.0F)
            {
                _altSpinRot -= 360.0F;
            }
            const Matrix4 rotX = CreateRotationX(DegreesToRadians(_altWobble));
            const Matrix4 rotY = CreateRotationY(DegreesToRadians(_altSpinRot));
            Matrix4 transform = Multiply(rotX, rotY);
            const float mag = std::sqrt(_altTiltX * _altTiltX + _altTiltZ * _altTiltZ);
            if (mag != 0.0F)
            {
                const Vector3 axis(_altTiltZ / mag, 0.0F, -_altTiltX / mag);
                float angle = mag * Fixed::ToFloat(_values.AltTiltAngleMax);
                angle = MathMin(angle, Fixed::ToFloat(_values.AltTiltAngleCap));
                const Matrix4 rotAxis = CreateFromAxisAngle(axis, DegreesToRadians(angle));
                transform = Multiply(transform, rotAxis);
            }
            _modelTransform = transform;
        }
        else if (_hunter == Hunter::Kanden)
        {
            UpdateStinglarvaSegments();
        }
        else if (_hunter == Hunter::Samus || _hunter == Hunter::Spire)
        {
            Vector3 axis{};
            const float altRadius = Fixed::ToFloat(_values.AltColRadius);
            if (_hunter == Hunter::Spire || TestFlag(_flags1, PlayerFlags1::CollidingEntity))
            {
                axis.X = altRadius * (_speed.Z / 2.0F);
                axis.Z = -altRadius * (_speed.X / 2.0F);
            }
            else
            {
                axis.X = altRadius * (Position.Z - _prevPosition.Z);
                axis.Z = -altRadius * (Position.X - _prevPosition.X);
            }
            const float mag = Length(axis);
            if (mag > 0.0F)
            {
                axis = Divide(axis, mag);
                const float angle = mag / (altRadius * altRadius);
                Matrix4 rotMtx = CreateFromAxisAngle(axis, angle);
                Matrix4 transform = Multiply(_modelTransform, rotMtx);
                if (_hunter == Hunter::Samus)
                {
                    if (Vector3::Dot(Row0(transform), axis) < 0.0F)
                    {
                        axis = Negate(axis);
                    }
                    axis = Vector3::Cross(Row0(transform), axis);
                    float mbAngle = Length(axis);
                    if (mbAngle > 0.0F)
                    {
                        if (mbAngle > 0.125F)
                        {
                            const float div = MathMin(angle / Fixed::ToFloat(3216), 1.0F);
                            mbAngle *= div / 8.0F;
                        }
                        rotMtx = CreateFromAxisAngle(axis, mbAngle);
                        transform = Multiply(transform, rotMtx);
                    }
                }
                SetRow2(transform, Vector3::Cross(Row0(transform), Row1(transform)));
                SetRow1(transform, Vector3::Cross(Row2(transform), Row0(transform)));
                SetRow0(transform, Normalize(Row0(transform)));
                SetRow1(transform, Normalize(Row1(transform)));
                SetRow2(transform, Normalize(Row2(transform)));
                if (_hunter == Hunter::Spire)
                {
                    for (std::int32_t i = 0; i < static_cast<std::int32_t>(_spireAltVecs.size()); ++i)
                    {
                        ManagedAt(_spireAltVecs, i) = Matrix::Vec3MultMtx3(
                            ManagedAt(Metadata::SpireAltVectors, i), transform);
                    }
                }
                _modelTransform = transform;
            }
        }
        else
        {
            _modelTransform = GetTransformMatrix(Vector3(_field80, 0.0F, _field84), UnitY);
        }
    }

    void PlayerEntity::AnimateSpireAltAttack()
    {
        const Matrix4 transform = GetTransformMatrix(_spireAltFacing, _spireAltUp);
        ModelInstance& alt = RequireReference(_altModel.get());
        Model& model = RequireReference(alt.Model());
        model.AnimateNodes(0, false, transform, Vector3(1.0F, 1.0F, 1.0F), alt.AnimInfo);
    }

    void PlayerEntity::UpdateSpireAltCollisionPose()
    {
        // Animate the continuous rocks even without a draw pass; only the
        // even-frame sample below becomes collision geometry on the next tick.
        AnimateSpireAltAttack();
        _spireRockPosL = RequireReference(ManagedAt(_spireAltNodes, 0).get()).Animation.Row3().Xyz()
            + static_cast<Vector3>(Position);
        _spireRockPosR = RequireReference(ManagedAt(_spireAltNodes, 1).get()).Animation.Row3().Xyz()
            + static_cast<Vector3>(Position);
        const auto frame = RequireReference(_scene).FrameCount();
        if (DialancheNativeCollision::IsNativeCollisionStep(frame))
        {
            _dialancheNativeCollision.Record(DialancheNativeCollision::NativeTick(frame), _spireRockPosL, _spireRockPosR);
        }
    }

    std::pair<Vector3, Vector3> PlayerEntity::ModSpireAltCollisionPose() const
    {
        return {_spireRockPosL, _spireRockPosR};
    }

    void PlayerEntity::UpdateStinglarvaSegments()
    {
        const auto scene = [&]() -> Scene& { return RequireReference(_scene); };
        constexpr std::int32_t cycle = 13 * 2;
        float angle = 359.0F * static_cast<float>(scene().FrameCount() % cycle)
            / static_cast<float>(cycle - 1);
        float factor = 0.3F * std::sin(DegreesToRadians(angle)) * _hSpeedMag;
        ManagedAt(_kandenSegPos, 0)
            = AddZ(AddX(Position, _field78 * factor), _field7C * factor);
        Vector3 dir;
        if (LengthSquared(_speed) > 0.02F)
        {
            dir = Vector3(_speed.X + _field80 / 4.0F, _speed.Y, _speed.Z + _field84 / 4.0F);
        }
        else
        {
            dir = Vector3(_field80, 0.0F, _field84);
        }
        dir = Normalize(dir);
        if (Vector3::Dot(dir, Row2(ManagedAt(_kandenSegMtx, 0))) < Fixed::ToFloat(-5))
        {
            dir.X += Fixed::ToFloat(5);
        }
        dir = Add(Row2(ManagedAt(_kandenSegMtx, 0)),
            ScaleVector(Subtract(dir, Row2(ManagedAt(_kandenSegMtx, 0))), 0.3F));
        dir = Normalize(dir);
        if (dir.X != 0.0F || dir.Z != 0.0F)
        {
            ManagedAt(_kandenSegMtx, 0) = GetTransformMatrix(dir, UnitY);
        }
        else
        {
            const Vector3 facing(-_field70, 0.0F, -_field74);
            const Vector3 up(dir.X, dir.Y, 0.0F);
            ManagedAt(_kandenSegMtx, 0) = GetTransformMatrix(facing, up);
        }
        SetRow3(ManagedAt(_kandenSegMtx, 0), ManagedAt(_kandenSegPos, 0));
        assert(_kandenSegPos.size() == _kandenSegMtx.size());
        for (std::int32_t i = 1; i < static_cast<std::int32_t>(_kandenSegPos.size()); ++i)
        {
            angle += 85.0F;
            while (angle >= 360.0F)
            {
                angle -= 360.0F;
            }
            factor = 0.12F * std::sin(DegreesToRadians(angle)) * _hSpeedMag;
            Vector3 segPos = ManagedAt(_kandenSegPos, i);
            segPos = AddZ(AddX(segPos, _field78 * factor), _field7C * factor);
            ManagedAt(_kandenSegPos, i) = segPos;
            dir = Normalize(Subtract(ManagedAt(_kandenSegPos, i - 1), segPos));
            const Matrix4 prevMtx = ManagedAt(_kandenSegMtx, i - 1);
            const float dot = Vector3::Dot(dir, Row2(prevMtx));
            if (dot < Fixed::ToFloat(2896))
            {
                Vector3 axis = Vector3::Cross(dir, Row2(prevMtx));
                const float mag = Length(axis);
                axis = Divide(axis, mag);
                float atan = std::atan2(mag, dot);
                atan -= DegreesToRadians(45.0F);
                const Matrix4 rotMtx = CreateFromAxisAngle(axis, atan);
                dir = Matrix::Vec3MultMtx3(dir, rotMtx);
            }
            ManagedAt(_kandenSegMtx, i)
                = GetTransformMatrix(dir, Row1(ManagedAt(_kandenSegMtx, 0)));
            const float dist = ManagedAt(KandenAltNodeDistances, i - 1);
            dir = ScaleVector(dir, dist);
            ManagedAt(_kandenSegPos, i) = Subtract(ManagedAt(_kandenSegPos, i - 1), dir);
            SetRow3(ManagedAt(_kandenSegMtx, i), ManagedAt(_kandenSegPos, i));
        }
    }

    void PlayerEntity::EnterAltForm()
    {
        const auto scene = [&]() -> Scene& { return RequireReference(_scene); };
        _altRollFbX = _field70;
        _altRollFbZ = _field74;
        _altRollLrX = _gunVec2.X;
        _altRollLrZ = _gunVec2.Z;
        _flags1 |= PlayerFlags1::Morphing;
        const Vector3 camFacing(_field70, 0.0F, _field74);
        SwitchCamera(_values.AltFormStrafe != 0 ? CameraType::Third2 : CameraType::Third1, camFacing);
        InitAltTransform();
        SetRow3(_modelTransform, Vector3::Zero);
        if (_hunter == Hunter::Spire)
        {
            for (std::int32_t i = 0; i < static_cast<std::int32_t>(_spireAltVecs.size()); ++i)
            {
                ManagedAt(_spireAltVecs, i) = Vector3::Zero;
            }
            RequireReference(_altModel).SetAnimation(
                static_cast<std::int32_t>(SpireAltAnim::Attack), AnimFlags::Paused);
        }
        else if (_hunter == Hunter::Noxus)
        {
            _altSpinSpeed = Fixed::ToFloat(_values.AltMinSpinAccel);
            _altTiltX = 0.0F;
            _altTiltZ = 0.0F;
            _altSpinRot = 0.0F;
            _altWobble = 0.0F;
            RequireReference(_altModel).SetAnimation(
                static_cast<std::int32_t>(NoxusAltAnim::Extend), AnimFlags::Paused);
        }
        else if (_hunter == Hunter::Weavel)
        {
            _weavelLungeInput.Reset();
            _weavelNativeAttackPress = false;
            RequireReference(_altModel).SetAnimation(static_cast<std::int32_t>(WeavelAltAnim::Idle));
            if (!_weavelAltLife)
            {
                _weavelAltLife = true;
                _flags2 |= PlayerFlags2::Halfturret;
                // A new turret: the authority has yet to report it standing.
                _weavelOwnedTurret = WeavelOwnedTurret{};
                RequireReference(_halfturret).NodeRef = NodeRef;
                // An exit/re-enter can precede the next scene removal pass.
                scene().RemoveEntity(_halfturret);
                scene().AddEntity(_halfturret);
            }
        }
        else if (_hunter == Hunter::Samus)
        {
            _furlEffect = scene().SpawnEffectGetEntry(30, _gunVec2, _facingVector, Position);
        }
        else if (_hunter == Hunter::Kanden)
        {
            RequireReference(_altModel).SetAnimation(
                static_cast<std::int32_t>(KandenAltAnim::Idle), AnimFlags::Paused);
        }
        else if (_hunter == Hunter::Trace)
        {
            RequireReference(_altModel).SetAnimation(static_cast<std::int32_t>(TraceAltAnim::Idle));
        }
        else if (_hunter == Hunter::Sylux)
        {
            RequireReference(_altModel).SetAnimation(static_cast<std::int32_t>(SyluxAltAnim::Idle));
        }
        MphRead::EquipInfo& equipInfo = RequireReference(_equipInfo);
        if (equipInfo.ChargeLevel > 0)
        {
            StopBeamChargeSfx(_currentWeapon);
            SetGunAnimation(GunAnimation::Idle, AnimFlags::NoLoop);
        }
        equipInfo.ChargeLevel = 0;
        SetBipedAnimation(PlayerAnimation::Morph, AnimFlags::NoLoop);
        PlayHunterSfx(HunterSfx::Morph);
    }

    void PlayerEntity::ExitAltForm()
    {
        _weavelLungeInput.Reset();
        _weavelNativeAttackPress = false;
        _weavelAltLife = false;
        if (TestFlag(_flags2, PlayerFlags2::Halfturret))
        {
            _flags2 &= ~PlayerFlags2::Halfturret;
            HalfturretEntity& halfturret = RequireReference(_halfturret);
            if (halfturret.Health() > 0)
            {
                GainHealth(halfturret.Health());
            }
            halfturret.Die();
        }
        _flags1 &= ~PlayerFlags1::Morphing;
        _flags1 |= PlayerFlags1::Unmorphing;
        SwitchCamera(CameraType::First, _facingVector);
        _boostCharge = 0;
        SetBipedAnimation(PlayerAnimation::Unmorph, AnimFlags::NoLoop);
        if (TestFlag(_flags2, PlayerFlags2::AltAttack))
        {
            EndAltAttack();
        }
        UpdateZoom(false);
        RequireReference(_equipInfo).ChargeLevel = 0;
        RequireReference(_equipInfo).SmokeLevel = 0;
        PlayHunterSfx(HunterSfx::Unmorph);
        if (IsAltForm())
        {
            UpdateForm(false);
        }
    }

    void PlayerEntity::UpdateForm(bool altForm)
    {
        const auto scene = [&]() -> Scene& { return RequireReference(_scene); };
        if (altForm)
        {
            _flags1 |= PlayerFlags1::AltForm;
        }
        else
        {
            _flags1 &= ~PlayerFlags1::AltForm;
        }
        UpdateScanIds();
        if (altForm)
        {
            CollisionVolume altVolume = ManagedAt(
                ManagedAt(PlayerVolumes, static_cast<std::int32_t>(_hunter)), 2);
            Position = Add(Position, Subtract(_volumeUnxf.SpherePosition, altVolume.SpherePosition));
            _volumeUnxf = altVolume;
            InitAltTransform();
            _field80 = _field70;
            _field84 = _field74;
            if (_hunter == Hunter::Kanden)
            {
                ManagedAt(_kandenSegPos, 0) = Position;
                const Vector3 facing(_field70, 0.0F, _field74);
                ManagedAt(_kandenSegMtx, 0) = GetTransformMatrix(facing, UnitY, Position);
                assert(_kandenSegPos.size() == _kandenSegMtx.size());
                for (std::int32_t i = 1; i < static_cast<std::int32_t>(_kandenSegPos.size()); ++i)
                {
                    const float dist = -ManagedAt(KandenAltNodeDistances, i - 1);
                    ManagedAt(_kandenSegPos, i)
                        = Add(ManagedAt(_kandenSegPos, i - 1), ScaleVector(facing, dist));
                    Matrix4 matrix = ManagedAt(_kandenSegMtx, 0);
                    SetRow3(matrix, ManagedAt(_kandenSegPos, i));
                    ManagedAt(_kandenSegMtx, i) = matrix;
                }
            }
            else if (_hunter == Hunter::Spire)
            {
                scene().SpawnEffect(37, UnitX, UnitY, Position);
                RequireReference(_cameraInfo).SetShake(0.3F);
                auto enumerator = scene().GetPlayerEntities().GetEnumerator();
                while (enumerator.MoveNext())
                {
                    std::shared_ptr<PlayerEntity> other = enumerator.Current();
                    PlayerEntity& otherRef = RequireReference(other);
                    if (other.get() == this)
                    {
                        continue;
                    }
                    if (TestFlag(otherRef._flags1, PlayerFlags1::Standing)
                        && DistanceSquared(Position, otherRef.Position) < 16.0F)
                    {
                        RequireReference(otherRef._cameraInfo).SetShake(0.3F);
                        if (otherRef._speed.Y < 0.15F)
                        {
                            otherRef._speed = WithY(otherRef._speed, 0.15F);
                        }
                    }
                }
            }
        }
        else
        {
            _gunVec1 = _facingVector;
            CollisionVolume bipedVolume = ManagedAt(
                ManagedAt(PlayerVolumes, static_cast<std::int32_t>(_hunter)), 0);
            Position = Add(Position, Subtract(_volumeUnxf.SpherePosition, bipedVolume.SpherePosition));
            _volumeUnxf = bipedVolume;
        }
        StopAltFormSfx();
    }

    void PlayerEntity::CreateBurnEffect()
    {
        const auto scene = [&]() -> Scene& { return RequireReference(_scene); };
        if (_burnEffect)
        {
            scene().UnlinkEffectEntry(_burnEffect);
            _burnEffect.reset();
        }
        if (!IsUnmorphing())
        {
            Vector3 up;
            Vector3 facing;
            Vector3 position;
            std::int32_t effectId;
            if (IsAltForm() || IsMorphing() || !IsMainPlayer())
            {
                position = _volume.SpherePosition;
                up = UnitY;
                facing = Vector3(_field70, 0.0F, _field74);
                effectId = IsAltForm() || IsMorphing() ? 187 : 189;
            }
            else
            {
                position = _muzzlePos;
                up = _gunVec1;
                facing = _upVector;
                effectId = 188;
            }
            _burnEffect = scene().SpawnEffectGetEntry(effectId, facing, up, position);
            if (_burnEffect)
            {
                _burnEffect->SetElementExtension(true);
            }
        }
    }

    void PlayerEntity::CreateIceBreakEffectGun()
    {
        const auto scene = [&]() -> Scene& { return RequireReference(_scene); };
        constexpr std::int32_t effectId = 231;
        const Vector3 playerUp = _upVector;
        const Vector3 up = _facingVector;
        Vector3 facing;
        if (up.Z <= -0.9F || up.Z >= 0.9F)
        {
            facing = Normalize(Vector3::Cross(UnitX, up));
        }
        else
        {
            facing = Normalize(Vector3::Cross(UnitZ, up));
        }
        const Vector3 position = Add(RequireReference(_cameraInfo).Position, Divide(up, 2.0F));
        Vector3 spawnPos = position;
        scene().SpawnEffect(effectId, facing, up, spawnPos);
        spawnPos = Add(position, ScaleVector(_gunVec2, 0.4F));
        scene().SpawnEffect(effectId, facing, up, spawnPos);
        spawnPos = Subtract(position, ScaleVector(_gunVec2, 0.4F));
        scene().SpawnEffect(effectId, facing, up, spawnPos);
        spawnPos = Add(position, ScaleVector(playerUp, 0.4F));
        scene().SpawnEffect(effectId, facing, up, spawnPos);
        spawnPos = Subtract(position, ScaleVector(playerUp, 0.4F));
        scene().SpawnEffect(effectId, facing, up, spawnPos);
    }

    void PlayerEntity::CreateIceBreakEffectBiped(Model& model)
    {
        const auto scene = [&]() -> Scene& { return RequireReference(_scene); };
        const auto& nodes = RequireReference(model.Nodes);
        assert(nodes.size() > 1);
        constexpr std::int32_t effectId = 231;
        for (std::int32_t i = 1; i < static_cast<std::int32_t>(nodes.size()); ++i)
        {
            Node& node = RequireReference(ManagedAt(nodes, i));
            const Vector3 pos = Row3(ManagedAt(_bipedIceTransforms, i));
            Vector3 up;
            if (node.ChildIndex <= 0)
            {
                up = Normalize(Subtract(pos, Position));
            }
            else
            {
                up = Normalize(Subtract(
                    Row3(ManagedAt(_bipedIceTransforms, node.ChildIndex)), pos));
            }
            Vector3 facing;
            if (up.Z <= -0.9F || up.Z >= 0.9F)
            {
                facing = Normalize(Vector3::Cross(UnitX, up));
            }
            else
            {
                facing = Normalize(Vector3::Cross(UnitZ, up));
            }
            scene().SpawnEffect(effectId, facing, up, pos);
        }
    }

    void PlayerEntity::CreateIceBreakEffectAlt()
    {
        const auto scene = [&]() -> Scene& { return RequireReference(_scene); };
        constexpr std::int32_t effectId = 231;
        scene().SpawnEffect(effectId, UnitX, UnitY, _volume.SpherePosition);
        scene().SpawnEffect(effectId, UnitY, UnitX, _volume.SpherePosition);
        scene().SpawnEffect(effectId, UnitY, Negate(UnitX), _volume.SpherePosition);
        scene().SpawnEffect(effectId, UnitY, UnitX, _volume.SpherePosition);
        scene().SpawnEffect(effectId, UnitY, Negate(UnitX), _volume.SpherePosition);
    }

    std::shared_ptr<PlayerSpawnEntity> PlayerEntity::GetRespawnPoint()
    {
        const auto scene = [&]() -> Scene& { return RequireReference(_scene); };
        assert(scene().Room() != nullptr);
        std::shared_ptr<PlayerSpawnEntity> chosenSpawn{};
        std::int32_t limit = 0;
        std::vector<std::shared_ptr<PlayerSpawnEntity>> valid{};
        std::shared_ptr<PlayerSpawnEntity> bestAvailable{};
        float bestDistance = 0.0F;
        auto candidateEnumerator = scene().GetPlayerSpawnEntities().GetEnumerator();
        while (candidateEnumerator.MoveNext())
        {
            std::shared_ptr<PlayerSpawnEntity> candidate = candidateEnumerator.Current();
            if (limit >= 25)
            {
                break;
            }
            PlayerSpawnEntity& candidateRef = RequireReference(candidate);
            if (!candidateRef.IsActive() || candidateRef.Cooldown() != 0
                || (scene().FrameCount() == 0 && candidateRef.Availability()))
            {
                limit = UncheckedAdd(limit, 1);
                continue;
            }
            const auto data = candidateRef.Data();
            if (GameState::Mode() == GameMode::Capture && data.TeamIndex != -1
                && data.TeamIndex != _teamIndex)
            {
                limit = UncheckedAdd(limit, 1);
                continue;
            }
            float minDistSqr = 100.0F;
            auto playerEnumerator = scene().GetPlayerEntities().GetEnumerator();
            while (playerEnumerator.MoveNext())
            {
                std::shared_ptr<PlayerEntity> player = playerEnumerator.Current();
                PlayerEntity& playerRef = RequireReference(player);
                if (playerRef._health > 0)
                {
                    const Vector3 between = Subtract(candidateRef.Position, playerRef.Position);
                    const float distSqr = Vector3::Dot(between, between);
                    if (distSqr < minDistSqr)
                    {
                        minDistSqr = distSqr;
                    }
                }
            }
            if (minDistSqr >= 100.0F)
            {
                valid.push_back(candidate);
            }
            else if (minDistSqr > bestDistance)
            {
                bestDistance = minDistSqr;
                bestAvailable = candidate;
            }
            limit = UncheckedAdd(limit, 1);
        }
        if (!valid.empty())
        {
            const std::int32_t index = static_cast<std::int32_t>(
                scene().FrameCount() % static_cast<std::uint64_t>(valid.size()));
            chosenSpawn = valid[static_cast<std::size_t>(index)];
        }
        else
        {
            chosenSpawn = bestAvailable;
        }
        if (!chosenSpawn)
        {
            auto fallbackEnumerator = scene().GetPlayerSpawnEntities().GetEnumerator();
            while (fallbackEnumerator.MoveNext())
            {
                std::shared_ptr<PlayerSpawnEntity> fallback = fallbackEnumerator.Current();
                if (RequireReference(fallback).IsActive())
                {
                    chosenSpawn = std::move(fallback);
                    break;
                }
            }
        }
        if (chosenSpawn)
        {
            chosenSpawn->SetCooldown(2 * 2);
        }
        return chosenSpawn;
    }

    std::int32_t PlayerEntity::GetTimeUntilRespawn()
    {
        std::int32_t count = 0;
        if (GameState::Mode() != GameMode::Survival && GameState::Mode() != GameMode::SurvivalTeams)
        {
            if (PlayerCount() > 3)
            {
                count = UncheckedSubtract(900 * 2, static_cast<std::int32_t>(_timeSinceDead));
            }
            else if (PlayerCount() > 2)
            {
                count = UncheckedSubtract(600 * 2, static_cast<std::int32_t>(_timeSinceDead));
            }
            else
            {
                count = UncheckedSubtract(300 * 2, static_cast<std::int32_t>(_timeSinceDead));
            }
        }
        else if (!TestFlag(_loadFlags, LoadFlags::Spawned))
        {
            count = UncheckedSubtract(210 * 2, static_cast<std::int32_t>(_timeSinceDead));
        }
        return count;
    }

    void PlayerEntity::HandleMessage(MessageInfo info)
    {
        if (info.Message == Message::Damage)
        {
            TakeDamage(UnboxInt32(info.Param1), DamageFlags::IgnoreInvuln, std::nullopt, nullptr);
        }
        else if (info.Message == Message::Death)
        {
            TakeDamage(UnboxInt32(info.Param1), DamageFlags::Death, std::nullopt, nullptr);
        }
        else if (info.Message == Message::Gravity)
        {
            const float gravity = Fixed::ToFloat(UnboxInt32(info.Param1));
            if (!TestFlag(_flags1, PlayerFlags1::Standing)
                && !TestFlag(_flags2, PlayerFlags2::AltAttack)
                && gravity != 0.0F && _jumpPadControlLock == 0)
            {
                _flags2 |= PlayerFlags2::GravityOverride;
                _gravity = gravity;
            }
        }
        else if (info.Message == Message::SetCamSeqAi)
        {
            if (_isBot)
            {
                RequireReference(AiData).Flags2 |= AiFlags2::AiStart;
            }
        }
        else if (info.Message == Message::Impact)
        {
            std::shared_ptr<EntityBase> targetRef
                = TryUnboxEntity(info.Param1);
            EntityBase* target = targetRef.get();
            if (target != nullptr && target != this
                && (target->Type == EntityType::EnemyInstance
                    || target->Type == EntityType::Halfturret
                    || target->Type == EntityType::Player))
            {
                _lastTarget = targetRef;
                _timeSinceHitTarget = 0;
                EntityBase& sender = RequireReference(info.Sender);
                if (sender.Type == EntityType::BeamProjectile)
                {
                    auto* beam = dynamic_cast<BeamProjectileEntity*>(&sender);
                    if (beam == nullptr)
                    {
                        throw SceneDetail::InvalidCastException();
                    }
                    if (beam->Beam() == BeamType::ShockCoil)
                    {
                        if (_shockCoilTarget.get() == target)
                        {
                            _shockCoilTimer = ManagedUInt16Add(_shockCoilTimer, 1);
                        }
                        else
                        {
                            _shockCoilTimer = 0;
                            _shockCoilTarget = targetRef;
                        }
                    }
                }
            }
        }
        else if (info.Message == Message::PreventFormSwitch)
        {
            _flags2 |= PlayerFlags2::NoFormSwitch;
        }
        else if (info.Message == Message::DripMoatPlatform)
        {
            if (UnboxInt32(info.Param1) == 0)
            {
                _flags2 &= ~PlayerFlags2::BipedLock;
            }
            else
            {
                _flags2 |= PlayerFlags2::BipedLock;
            }
        }
    }

    void PlayerEntity::Destroy()
    {
        const auto scene = [&]() -> Scene& { return RequireReference(_scene); };
        _soundSource.StopAllSfx();
        if (_furlEffect)
        {
            scene().UnlinkEffectEntry(_furlEffect);
            _furlEffect.reset();
        }
        if (_boostEffect)
        {
            scene().UnlinkEffectEntry(_boostEffect);
            _boostEffect.reset();
        }
        if (_burnEffect)
        {
            scene().UnlinkEffectEntry(_burnEffect);
            _burnEffect.reset();
        }
        if (_chargeEffect)
        {
            scene().UnlinkEffectEntry(_chargeEffect);
            _chargeEffect.reset();
        }
        if (_muzzleEffect)
        {
            scene().UnlinkEffectEntry(_muzzleEffect);
            _muzzleEffect.reset();
        }
        if (_doubleDmgEffect)
        {
            scene().UnlinkEffectEntry(_doubleDmgEffect);
            _doubleDmgEffect.reset();
        }
        if (_deathaltEffect)
        {
            scene().UnlinkEffectEntry(_deathaltEffect);
            _deathaltEffect.reset();
        }
        EntityBase::Destroy();
    }
}
