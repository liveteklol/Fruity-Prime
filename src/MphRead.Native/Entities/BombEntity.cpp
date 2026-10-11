#include "BombEntity.hpp"
#include "../Mods/Combat/LockjawCollision.hpp"
#include "../Mods/Combat/Extensions/LockjawEnemyExtension.hpp"
#include "../Mods/Multiplayer/TeamLayout.hpp"
#include "../Mods/Render/LockjawTrailNoise.hpp"

#include "../NativeRuntime/System/Buffers.hpp"

#include "../Formats/CollisionDetection.hpp"
#include "../Formats/Effects.hpp"
#include "../GameState.hpp"
#include "../Metadata/Metadata.hpp"
#include "../Messaging.hpp"
#include "../Read.hpp"
#include "../Renderer.hpp"
#include "../Scene.hpp"
#include "../Utility/Rng.hpp"
#include "../Mods/Network/NetBombs.hpp"
#include "../Mods/Network/NetDamage.hpp"
#include "DoorEntity.hpp"
#include "Enemies/02_Temroid.hpp"
#include "EnemyInstanceEntity.hpp"
#include "Players/HalfturretEntity.hpp"
#include "Players/PlayerEntity.hpp"
#include "../NativeRuntime/System/Managed.hpp"
#include "../Formats/Types.hpp"

#include <algorithm>
#include <any>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <iterator>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

using ::MphRead::NativeRuntime::ManagedAt;
using ::MphRead::NativeRuntime::RequireReference;
using ::MphRead::TestFlag;
using ::OpenTK::Mathematics::AddY;
using ::OpenTK::Mathematics::CreateTranslation;
using ::OpenTK::Mathematics::Divide;
using ::OpenTK::Mathematics::Length;
using ::OpenTK::Mathematics::LengthSquared;
using ::OpenTK::Mathematics::ScaleVector;
using ::OpenTK::Mathematics::WithY;

namespace MphRead::Entities
{
    namespace
    {
        using OpenTK::Mathematics::Matrix4;
        using OpenTK::Mathematics::Vector3;
        using OpenTK::Mathematics::Vector4;

        template <typename T>
        [[nodiscard]] T* RawPointer(T* value) noexcept
        {
            return value;
        }

        template <typename T>
        [[nodiscard]] T* RawPointer(const std::shared_ptr<T>& value) noexcept
        {
            return value.get();
        }

        template <typename T>
        [[nodiscard]] const T* RawPointer(const std::shared_ptr<const T>& value) noexcept
        {
            return value.get();
        }

        template <typename TValue>
        [[nodiscard]] decltype(auto) ManagedStorage(TValue& value)
        {
            using Value = std::remove_cvref_t<TValue>;
            if constexpr (std::is_pointer_v<Value>)
            {
                return RequireReference(value);
            }
            else if constexpr (requires { value.get(); })
            {
                return RequireReference(value);
            }
            else
            {
                return (value);
            }
        }

        template <typename TContainer>
        [[nodiscard]] std::int32_t ManagedLength(TContainer& values)
        {
            auto&& storage = ManagedStorage(values);
            if constexpr (requires { storage.Length(); })
            {
                return static_cast<std::int32_t>(storage.Length());
            }
            else
            {
                return static_cast<std::int32_t>(std::size(storage));
            }
        }

        template <typename TPlayer>
        [[nodiscard]] decltype(auto) SyluxBombArray(TPlayer& owner)
        {
            return owner.SyluxBombs();
        }

        [[nodiscard]] BombEntity* BombAt(PlayerEntity& owner, std::int32_t index)
        {
            auto&& bombs = SyluxBombArray(owner);
            return RawPointer(ManagedAt(bombs, index));
        }

        template <typename TValue>
        [[nodiscard]] auto ObjectPointer(TValue&& value) noexcept
        {
            using Value = std::remove_cvref_t<TValue>;
            if constexpr (std::is_pointer_v<Value>)
            {
                return value;
            }
            else if constexpr (requires { value.get(); })
            {
                return value.get();
            }
            else
            {
                return std::addressof(value);
            }
        }

        [[nodiscard]] HalfturretEntity* GetHalfturret(PlayerEntity& player)
        {
            return ObjectPointer(player.Halfturret());
        }

        [[nodiscard]] Mods::Combat::LockjawCollision::Triangle SnarePositions(PlayerEntity& owner)
        {
            return {RequireReference(BombAt(owner, 0)).Position,
                RequireReference(BombAt(owner, 1)).Position,
                RequireReference(BombAt(owner, 2)).Position};
        }
    }

    BombEntity::BombEntity(Scene* scene)
        : EntityBase(EntityType::Bomb, scene)
    {
    }

    BombFlags BombEntity::Flags() const noexcept
    {
        return _flags;
    }

    PlayerEntity* BombEntity::Owner() const noexcept
    {
        return _owner;
    }

    MphRead::BombType BombEntity::BombType() const noexcept
    {
        return _bombType;
    }

    std::int32_t BombEntity::BombIndex() const noexcept
    {
        return _bombIndex;
    }

    void BombEntity::SetBombIndex(std::int32_t value) noexcept
    {
        _bombIndex = value;
    }

    std::int32_t BombEntity::Countdown() const noexcept
    {
        return _countdown;
    }

    void BombEntity::SetCountdown(std::int32_t value) noexcept
    {
        _countdown = value;
    }

    float BombEntity::Radius() const noexcept
    {
        return _radius;
    }

    void BombEntity::SetRadius(float value) noexcept
    {
        _radius = value;
    }

    float BombEntity::SelfRadius() const noexcept
    {
        return _selfRadius;
    }

    void BombEntity::SetSelfRadius(float value) noexcept
    {
        _selfRadius = value;
    }

    std::uint16_t BombEntity::Damage() const noexcept
    {
        return _damage;
    }

    void BombEntity::SetDamage(std::uint16_t value) noexcept
    {
        _damage = value;
    }

    std::uint16_t BombEntity::EnemyDamage() const noexcept
    {
        return _enemyDamage;
    }

    void BombEntity::SetEnemyDamage(std::uint16_t value) noexcept
    {
        _enemyDamage = value;
    }

    std::shared_ptr<Effects::EffectEntry> BombEntity::Effect() const noexcept
    {
        return _effect;
    }

    void BombEntity::Initialize()
    {
        EntityBase::Initialize();
        std::int32_t effectId = 0;
        if (_bombType == MphRead::BombType::Stinglarva)
        {
            SetUpModel("KandenAlt_TailBomb");
            _flags |= BombFlags::HasModel;
            _countdown = 43 * 2;
        }
        else if (_bombType == MphRead::BombType::Lockjaw)
        {
            if (Recolor() == 0)
            {
                _trailModel = Read::GetModelInstance("arcWelder");
            }
            else
            {
                _trailModel = Read::GetModelInstance("arcWelder1");
            }
            _countdown = 900 * 2;
            const std::int32_t recolor = Recolor();
            if (recolor < 0
                || static_cast<std::size_t>(recolor) >= Metadata::SyluxBombEffects.size())
            {
                throw SceneDetail::IndexOutOfRangeException();
            }
            effectId = Metadata::SyluxBombEffects[static_cast<std::size_t>(recolor)];
            PlayerEntity& owner = RequireReference(_owner);
            if (owner.SyluxBombCount() == 1)
            {
                Formats::CollisionResult colRes{};
                BombEntity& firstBomb = RequireReference(BombAt(owner, 0));
                Vector3 between = static_cast<Vector3>(firstBomb.Position)
                    - static_cast<Vector3>(Position);
                if (LengthSquared(between) >= 100.0F
                    || Formats::CollisionDetection::CheckBetweenPoints(
                        static_cast<Vector3>(firstBomb.Position),
                        static_cast<Vector3>(Position),
                        Formats::TestFlags::Players,
                        _scene,
                        colRes))
                {
                    _countdown = 1;
                    firstBomb._countdown = 1;
                }
            }
        }
        else if (_bombType == MphRead::BombType::MorphBall)
        {
            _countdown = 43 * 2;
            effectId = GameState::Multiplayer() && PlayerEntity::PlayerCount() > 2 ? 119 : 9;
        }
        if (effectId != 0)
        {
            _effect = RequireReference(_scene).SpawnEffectGetEntry(
                effectId, static_cast<Matrix4>(Transform));
            if (_effect)
            {
                _effect->SetElementExtension(true);
            }
        }
        if (_trailModel)
        {
            std::int32_t recolor = Recolor();
            if (Recolor() > 0)
            {
                --recolor;
            }
            std::shared_ptr<Model> model = _trailModel->Model();
            Model& modelRef = RequireReference(model);
            Material& material = RequireReference(ManagedAt(modelRef.Materials, 0));
            _bindingId = RequireReference(_scene).BindGetTexture(
                model, material.TextureId, material.PaletteId, recolor);
        }
    }

    void BombEntity::ModMoveTo(Vector3 position)
    {
        const Vector3 step = position - static_cast<Vector3>(Position);
        Position = position;
        // As ProcessTargeting does: a Stinglarva faces its motion.
        if (_bombType != MphRead::BombType::Lockjaw && (step.X != 0.0F || step.Z != 0.0F))
        {
            SetTransform(step.Normalized(), UpVector(), position);
        }
    }

    void BombEntity::Reposition(Vector3 offset)
    {
        Position = static_cast<Vector3>(Position) + offset;
        SetTarget(nullptr);
    }

    bool BombEntity::Process()
    {
        if (_bombType == MphRead::BombType::Lockjaw)
        {
            ++_lockjawVisualTick;
        }
        EntityBase* hitEntity = nullptr;
        _soundSource.Update(static_cast<Vector3>(Position), 5);
        UpdateNodeRefVolume();
        if (_countdown > 0)
        {
            --_countdown;
        }
        if (_countdown == 0)
        {
            _flags |= BombFlags::Exploding;
        }
        if (!TestFlag(_flags, BombFlags::Exploded))
        {
            // A copy's bomb goes off when its owner's does (NetBombs), not by
            // touching somebody here.
            auto playerEnumerator = RequireReference(_scene).GetPlayerEntities().GetEnumerator();
            while (Mods::Network::NetBombs::DetonatesOnContact(*this) && playerEnumerator.MoveNext())
            {
                PlayerEntity& player = RequireReference(playerEnumerator.Current());
                if (&player == _owner
                    || player.Health() == 0
                    || Mods::Multiplayer::TeamRules::AreAllies(
                        player.TeamIndex(), RequireReference(_owner).TeamIndex()))
                {
                    if (&player != _owner && player.Health() > 0)
                    {
                        ::MphRead::NativeRuntime::IncrementInPlace(Mods::Network::NetDamage::BombTeamSkips);
                    }
                    continue;
                }
                ::MphRead::NativeRuntime::IncrementInPlace(Mods::Network::NetDamage::BombPlayerChecks);
                const Vector3 gapVector = player.Volume().SpherePosition
                    - static_cast<Vector3>(Position);
                const float gap = Length(gapVector);
                if (gap < Mods::Network::NetDamage::BombNearest)
                {
                    Mods::Network::NetDamage::BombNearest = gap;
                }
                if (_radius > Mods::Network::NetDamage::BombRadiusSeen)
                {
                    Mods::Network::NetDamage::BombRadiusSeen = _radius;
                }
                if (player.CheckHitByBomb(this, false))
                {
                    ::MphRead::NativeRuntime::IncrementInPlace(Mods::Network::NetDamage::BombHits);
                    hitEntity = &player;
                    _flags |= BombFlags::Exploding;
                }
                if (TestFlag(player.Flags2(), PlayerFlags2::Halfturret)
                    && player.CheckHitByBomb(this, true))
                {
                    hitEntity = &player;
                    _flags |= BombFlags::Exploding;
                }
                if (_target != nullptr)
                {
                    continue;
                }
                if (_bombType == MphRead::BombType::Lockjaw)
                {
                    LockjawCheckTargeting(player, hitEntity);
                }
                else if (_bombType == MphRead::BombType::Stinglarva)
                {
                    Vector3 between = static_cast<Vector3>(player.Position)
                        - static_cast<Vector3>(Position);
                    if (LengthSquared(between) < 5.0F * 5.0F)
                    {
                        SetTarget(&player);
                        _speed = ScaleVector(FacingVector(), 0.3F);
                    }
                }
            }
            if (_bombType == MphRead::BombType::Stinglarva && _target == nullptr)
            {
                auto halfturretEnumerator
                    = RequireReference(_scene).GetHalfturretEntities().GetEnumerator();
                while (halfturretEnumerator.MoveNext())
                {
                    HalfturretEntity& halfturret
                        = RequireReference(halfturretEnumerator.Current());
                    Vector3 between = static_cast<Vector3>(halfturret.Position)
                        - static_cast<Vector3>(Position);
                    if (LengthSquared(between) < 5.0F * 5.0F)
                    {
                        SetTarget(&halfturret);
                        _speed = ScaleVector(FacingVector(), 0.3F);
                    }
                }
            }
            {
                auto enemyEnumerator
                    = RequireReference(_scene).GetEnemyInstanceEntities().GetEnumerator();
                while (enemyEnumerator.MoveNext())
                {
                    EnemyInstanceEntity& enemy
                        = RequireReference(enemyEnumerator.Current());
                    if (TestFlag(enemy.Flags(), EnemyFlags::CollideBeam)
                        && (_bombType != MphRead::BombType::Lockjaw || enemy.Health() != 0)
                        && (enemy.EnemyType() != MphRead::EnemyType::Temroid
                            || enemy.StateA() != 8))
                    {
                        if (enemy.CheckHitByBomb(this))
                        {
                            hitEntity = &enemy;
                            _flags |= BombFlags::Exploding;
                        }
                        else if (_bombType == MphRead::BombType::Lockjaw
                            && _target == nullptr
                            && !TestFlag(_flags, BombFlags::Exploding))
                        {
                            if (Mods::Combat::Extensions::LockjawEnemyExtension::TryHit(
                                *this, enemy, RequireReference(_scene)))
                            {
                                hitEntity = &enemy;
                            }
                        }
                    }
                }
            }
            {
                auto enemyEnumerator
                    = RequireReference(_scene).GetEnemyInstanceEntities().GetEnumerator();
                while (enemyEnumerator.MoveNext())
                {
                    EnemyInstanceEntity& enemy
                        = RequireReference(enemyEnumerator.Current());
                    if (TestFlag(enemy.Flags(), EnemyFlags::CollideBeam)
                        && enemy.EnemyType() == MphRead::EnemyType::Temroid
                        && enemy.StateA() == 8)
                    {
                        auto* temroid = dynamic_cast<Enemies::Enemy02Entity*>(&enemy);
                        if (temroid == nullptr)
                        {
                            throw SceneDetail::InvalidCastException();
                        }
                        if (temroid->CheckTemroidHitByBomb(this))
                        {
                            hitEntity = &enemy;
                        }
                    }
                }
            }
            if (RequireReference(_owner).IsAltForm())
            {
                (void)RequireReference(_owner).CheckHitByBomb(this, false);
            }
            if (TestFlag(_flags, BombFlags::Exploding))
            {
                auto doorEnumerator = RequireReference(_scene).GetDoorEntities().GetEnumerator();
                while (doorEnumerator.MoveNext())
                {
                    DoorEntity& door = RequireReference(doorEnumerator.Current());
                    const Vector3 doorFacing = door.FacingVector();
                    Vector3 between = static_cast<Vector3>(Position) - door.LockPosition();
                    const float dot = Vector3::Dot(doorFacing, between);
                    const float radius = _selfRadius + 0.4F;
                    if (dot < radius && dot > -radius)
                    {
                        between = between - ScaleVector(doorFacing, dot);
                        if (LengthSquared(between) <= door.RadiusSquared())
                        {
                            if (TestFlag(door.Flags(), DoorFlags::Locked)
                                && door.Data().PaletteId == 8)
                            {
                                door.Unlock(true, true);
                            }
                            door.SetFlags(door.Flags() | DoorFlags::ShotOpen);
                        }
                    }
                }
            }
            if (_bombType == MphRead::BombType::Lockjaw
                && _bombIndex == 0
                && RequireReference(_owner).SyluxBombCount() == 3
                && _target == nullptr
                && hitEntity == nullptr)
            {
                PlayerEntity& owner = RequireReference(_owner);
                for (std::int32_t i = 0; i < 3; ++i)
                {
                    BombEntity* bomb = BombAt(owner, i);
                    assert(bomb != nullptr);
                    BombEntity& bombRef = RequireReference(bomb);
                    bombRef._countdown = 1;
                    bombRef.SetTarget(_owner);
                }
            }
        }
        if (_target != nullptr)
        {
            if (_target->GetTargetable())
            {
                ProcessTargeting();
            }
            else
            {
                SetTarget(nullptr);
            }
        }
        if (_bombType == MphRead::BombType::Lockjaw)
        {
            PlayerEntity& owner = RequireReference(_owner);
            if (owner.Health() == 0)
            {
                _flags |= BombFlags::Exploding;
                _countdown = 0;
            }
            if (hitEntity != nullptr)
            {
                for (std::int32_t i = 0; i < owner.SyluxBombCount(); ++i)
                {
                    BombEntity* bomb = BombAt(owner, i);
                    assert(bomb != nullptr);
                    BombEntity& bombRef = RequireReference(bomb);
                    bombRef.SetTarget(hitEntity);
                    if (bombRef._countdown > 22 * 2)
                    {
                        bombRef._countdown = 22 * 2;
                    }
                }
            }
        }
        if (TestFlag(_flags, BombFlags::Exploding))
        {
            _flags &= ~BombFlags::Exploding;
            if (TestFlag(_flags, BombFlags::Exploded))
            {
                return false;
            }
            _flags |= BombFlags::Exploded;
            SetTarget(nullptr);
            _models = ModelList{};
            if (_effect)
            {
                RequireReference(_scene).UnlinkEffectEntry(_effect);
                _effect.reset();
            }
            if (_bombType == MphRead::BombType::Stinglarva)
            {
                RequireReference(_scene).SpawnEffect(128, static_cast<Matrix4>(Transform));
            }
            else if (_bombType == MphRead::BombType::Lockjaw)
            {
                RequireReference(_scene).SpawnEffect(146, static_cast<Matrix4>(Transform));
            }
            else if (_bombType == MphRead::BombType::MorphBall)
            {
                RequireReference(_scene).SpawnEffect(145, static_cast<Matrix4>(Transform));
            }
            _countdown = 0;
            _soundSource.StopSfx(SfxId::KANDEN_ALT_ATTACK);
            _soundSource.StopSfx(SfxId::MORPH_BALL_BOMB_PLACE);
            _soundSource.PlaySfx(SfxId::MORPH_BALL_BOMB);
            if (hitEntity == nullptr)
            {
                RequireReference(_scene).SendMessage(
                    Message::Impact, this, _owner, BoxInt32(0), BoxInt32(0));
            }
        }
        if (_effect)
        {
            _effect->Transform(
                static_cast<Vector3>(Position), static_cast<Matrix4>(Transform));
        }
        return EntityBase::Process();
    }

    void BombEntity::LockjawCheckTargeting(PlayerEntity& player, EntityBase*& hitEntity)
    {
        Vector3 targetPos{};
        if (player.IsAltForm())
        {
            targetPos = player.Volume().SpherePosition;
        }
        else
        {
            targetPos = AddY(
                static_cast<Vector3>(player.Position),
                Fixed::ToFloat(player.Values().MinPickupHeight));
        }
        const float cylHeight
            = Fixed::ToFloat(player.Values().MaxPickupHeight)
            - Fixed::ToFloat(player.Values().MinPickupHeight);
        Formats::CollisionResult discard{};
        bool lineHitPlayer = false;
        bool lineHitHalfturret = false;
        if (_bombIndex == 1)
        {
            PlayerEntity& owner = RequireReference(_owner);
            BombEntity* bombZero = BombAt(owner, 0);
            assert(bombZero != nullptr);
            BombEntity& zero = RequireReference(bombZero);
            if (player.IsAltForm()
                && Formats::CollisionDetection::CheckCylinderOverlapSphere(
                    static_cast<Vector3>(Position),
                    static_cast<Vector3>(zero.Position),
                    targetPos,
                    player.Volume().SphereRadius,
                    discard))
            {
                lineHitPlayer = true;
            }
            else if (!player.IsAltForm()
                && Formats::CollisionDetection::CheckCylindersOverlap(
                    static_cast<Vector3>(Position),
                    static_cast<Vector3>(zero.Position),
                    targetPos,
                    Vector3(0.0F, 1.0F, 0.0F),
                    cylHeight,
                    player.Volume().SphereRadius,
                    discard))
            {
                lineHitPlayer = true;
            }
            else if (TestFlag(player.Flags2(), PlayerFlags2::Halfturret))
            {
                HalfturretEntity& halfturret = RequireReference(GetHalfturret(player));
                if (Formats::CollisionDetection::CheckCylinderOverlapSphere(
                    static_cast<Vector3>(Position),
                    static_cast<Vector3>(zero.Position),
                    static_cast<Vector3>(halfturret.Position),
                    0.45F,
                    discard))
                {
                    lineHitHalfturret = true;
                }
            }
        }
        else if (_bombIndex == 2)
        {
            PlayerEntity& owner = RequireReference(_owner);
            BombEntity* bombZero = BombAt(owner, 0);
            BombEntity* bombOne = BombAt(owner, 1);
            assert(bombZero != nullptr);
            assert(bombOne != nullptr);
            BombEntity& zero = RequireReference(bombZero);
            BombEntity& one = RequireReference(bombOne);
            if (player.IsAltForm()
                && Formats::CollisionDetection::CheckCylinderOverlapSphere(
                    static_cast<Vector3>(Position),
                    static_cast<Vector3>(zero.Position),
                    targetPos,
                    player.Volume().SphereRadius,
                    discard))
            {
                lineHitPlayer = true;
            }
            else if (!player.IsAltForm()
                && Formats::CollisionDetection::CheckCylindersOverlap(
                    static_cast<Vector3>(Position),
                    static_cast<Vector3>(zero.Position),
                    targetPos,
                    Vector3(0.0F, 1.0F, 0.0F),
                    cylHeight,
                    player.Volume().SphereRadius,
                    discard))
            {
                lineHitPlayer = true;
            }
            if (player.IsAltForm()
                && Formats::CollisionDetection::CheckCylinderOverlapSphere(
                    static_cast<Vector3>(Position),
                    static_cast<Vector3>(one.Position),
                    targetPos,
                    player.Volume().SphereRadius,
                    discard))
            {
                lineHitPlayer = true;
            }
            else if (!player.IsAltForm()
                && Formats::CollisionDetection::CheckCylindersOverlap(
                    static_cast<Vector3>(Position),
                    static_cast<Vector3>(one.Position),
                    targetPos,
                    Vector3(0.0F, 1.0F, 0.0F),
                    cylHeight,
                    player.Volume().SphereRadius,
                    discard))
            {
                lineHitPlayer = true;
            }
            else if (TestFlag(player.Flags2(), PlayerFlags2::Halfturret))
            {
                HalfturretEntity& halfturret = RequireReference(GetHalfturret(player));
                if (Formats::CollisionDetection::CheckCylinderOverlapSphere(
                    static_cast<Vector3>(Position),
                    static_cast<Vector3>(zero.Position),
                    static_cast<Vector3>(halfturret.Position),
                    0.45F,
                    discard))
                {
                    lineHitHalfturret = true;
                }
                else if (Formats::CollisionDetection::CheckCylinderOverlapSphere(
                    static_cast<Vector3>(Position),
                    static_cast<Vector3>(one.Position),
                    static_cast<Vector3>(halfturret.Position),
                    0.45F,
                    discard))
                {
                    lineHitHalfturret = true;
                }
            }
        }
        else if (RequireReference(_owner).SyluxBombCount() == 3)
        {
            PlayerEntity& owner = RequireReference(_owner);
            assert(_bombIndex == 0);
            if (LockjawCheckSnare(static_cast<Vector3>(player.Position)))
            {
                hitEntity = &player;
                for (std::int32_t i = 0; i < owner.SyluxBombCount(); ++i)
                {
                    BombEntity* bomb = BombAt(owner, i);
                    assert(bomb != nullptr);
                    BombEntity& bombRef = RequireReference(bomb);
                    bombRef._damage = 60;
                    bombRef._enemyDamage = 60;
                    if (owner.IsBot() && GameState::SinglePlayer())
                    {
                        const std::int32_t encounter
                            = ManagedAt(GameState::EncounterState(), owner.SlotIndex());
                        if (encounter == 1
                            || encounter == 3
                            || encounter == 4
                            || (encounter == 0 && owner.BotLevel() == 0))
                        {
                            bombRef._enemyDamage = 4;
                            bombRef._damage = bombRef._enemyDamage;
                        }
                        else if (encounter != 0 || owner.BotLevel() < 2)
                        {
                            bombRef._enemyDamage = 7;
                            bombRef._damage = bombRef._enemyDamage;
                        }
                        else
                        {
                            bombRef._enemyDamage = 10;
                            bombRef._damage = bombRef._enemyDamage;
                        }
                    }
                }
            }
            else if (TestFlag(player.Flags2(), PlayerFlags2::Halfturret))
            {
                HalfturretEntity& halfturret = RequireReference(GetHalfturret(player));
                if (LockjawCheckSnare(static_cast<Vector3>(halfturret.Position)))
                {
                    hitEntity = &halfturret;
                    for (std::int32_t i = 0; i < owner.SyluxBombCount(); ++i)
                    {
                        BombEntity* bomb = BombAt(owner, i);
                        assert(bomb != nullptr);
                        BombEntity& bombRef = RequireReference(bomb);
                        bombRef._damage = 60;
                        bombRef._enemyDamage = 60;
                    }
                }
            }
            return;
        }
        if (lineHitPlayer)
        {
            assert(!lineHitHalfturret);
            hitEntity = &player;
            std::uint32_t damage = 20;
            PlayerEntity& owner = RequireReference(_owner);
            if (owner.IsBot() && GameState::SinglePlayer())
            {
                const std::int32_t encounter
                    = ManagedAt(GameState::EncounterState(), owner.SlotIndex());
                if (encounter == 1
                    || encounter == 3
                    || encounter == 4
                    || (encounter == 0 && owner.BotLevel() == 0))
                {
                    damage = 1;
                }
                else
                {
                    damage = 3;
                }
            }
            player.TakeDamage(
                damage, DamageFlags::NoDmgInvuln, std::nullopt, this);
        }
        else if (lineHitHalfturret)
        {
            HalfturretEntity& halfturret = RequireReference(GetHalfturret(player));
            hitEntity = &halfturret;
            player.TakeDamage(
                20,
                DamageFlags::NoDmgInvuln | DamageFlags::Halfturret,
                std::nullopt,
                this);
        }
    }

    bool BombEntity::LockjawCheckSnare(Vector3 position)
    {
        return Mods::Combat::LockjawCollision::SnareContainsPoint(
            SnarePositions(RequireReference(_owner)), position);
    }

    void BombEntity::SetTarget(EntityBase* target)
    {
        // Resolve ownership before changing either field. Players own their
        // bombs, so retain only enemies to avoid a player/bomb ownership cycle.
        auto enemy = target != nullptr && target->Type == EntityType::EnemyInstance
            ? SharedFrom(static_cast<EnemyInstanceEntity*>(target)) : nullptr;
        _target = target;
        _enemyTarget = std::move(enemy);
    }

    void BombEntity::ProcessTargeting()
    {
        assert(_target != nullptr);
        EntityBase& target = RequireReference(_target);
        Vector3 targetPos{};
        target.GetPosition(targetPos);
        const Vector3 prevPos = static_cast<Vector3>(Position);
        Vector3 newSpeed{};
        if (_bombType == MphRead::BombType::Lockjaw)
        {
            Vector3 between = targetPos - static_cast<Vector3>(Position);
            const float magSqr = LengthSquared(between);
            if (magSqr > 0.0F)
            {
                between = Divide(between, std::sqrt(magSqr));
            }
            newSpeed = _speed + ScaleVector(between - _speed, 0.15F);
        }
        else
        {
            assert(_bombType == MphRead::BombType::Stinglarva);
            Vector3 between = WithY(
                targetPos - static_cast<Vector3>(Position), 0.0F);
            const float hMagSqr = between.X * between.X + between.Z * between.Z;
            if (hMagSqr > 0.0F)
            {
                between = Divide(between, std::sqrt(hMagSqr));
            }
            const float deltaX = (between.X - _speed.X) * 0.05F;
            const float deltaZ = (between.Z - _speed.Z) * 0.05F;
            newSpeed = Vector3(
                _speed.X + deltaX,
                _speed.Y - 0.05F,
                _speed.Z + deltaZ);
        }
        _speed = _speed + Divide(newSpeed - _speed, 2.0F);
        Position = static_cast<Vector3>(Position) + Divide(_speed, 2.0F);
        ManagedArray<Formats::CollisionResult> results(8);
        const std::int32_t count
            = Formats::CollisionDetection::CheckSphereBetweenPoints(
                prevPos,
                static_cast<Vector3>(Position),
                0.4F,
                8,
                false,
                Formats::TestFlags::None,
                _scene,
                &results);
        for (std::int32_t i = 0; i < count; ++i)
        {
            const Formats::CollisionResult result
                = results[static_cast<std::size_t>(i)];
            assert(result.Field0 == 0);
            const Vector3 normal = result.Plane.Xyz();
            const float dotw = result.Plane.W
                - Vector3::Dot(static_cast<Vector3>(Position), normal)
                + 0.4F;
            if (dotw > 0.0F)
            {
                Position = static_cast<Vector3>(Position) + ScaleVector(normal, dotw);
                const float dot = Vector3::Dot(Divide(_speed, 2.0F), normal);
                if (dot < 0.0F)
                {
                    _speed = _speed + ScaleVector(normal, -dot);
                }
            }
        }
        if (_bombType != MphRead::BombType::Lockjaw
            && (_speed.X != 0.0F || _speed.Z != 0.0F))
        {
            SetTransform(
                _speed.Normalized(),
                UpVector(),
                static_cast<Vector3>(Position));
        }
    }

    void BombEntity::GetDrawInfo()
    {
        const std::uint32_t rngBefore
            = ModAuditLockjawDrawRng && _bombType == MphRead::BombType::Lockjaw
            ? Rng::Rng1() : 0U;
        if (_bombType == MphRead::BombType::Lockjaw)
        {
            if (_bombIndex == 1)
            {
                PlayerEntity& owner = RequireReference(_owner);
                BombEntity& bombZero = RequireReference(BombAt(owner, 0));
                DrawLockjawTrail(
                    static_cast<Vector3>(Position),
                    static_cast<Vector3>(bombZero.Position),
                    Fixed::ToFloat(614),
                    10,
                    0);
            }
            else if (_bombIndex == 2)
            {
                PlayerEntity& owner = RequireReference(_owner);
                BombEntity& bombOne = RequireReference(BombAt(owner, 1));
                DrawLockjawTrail(
                    static_cast<Vector3>(Position),
                    static_cast<Vector3>(bombOne.Position),
                    Fixed::ToFloat(614),
                    10,
                    1);
                BombEntity& bombZero = RequireReference(BombAt(owner, 0));
                DrawLockjawTrail(
                    static_cast<Vector3>(Position),
                    static_cast<Vector3>(bombZero.Position),
                    Fixed::ToFloat(614),
                    10,
                    0);
            }
        }
        EntityBase::GetDrawInfo();
        if (ModAuditLockjawDrawRng && _bombType == MphRead::BombType::Lockjaw
            && Rng::Rng1() != rngBefore)
        {
            ::MphRead::NativeRuntime::IncrementInPlace(ModLockjawDrawRngChanges);
        }
    }

    void BombEntity::DrawLockjawTrail(
        Vector3 point1,
        Vector3 point2,
        float height,
        std::int32_t segments,
        std::int32_t targetBombIndex)
    {
        assert(_trailModel != nullptr);
        if (segments < 2)
        {
            return;
        }
        const std::int32_t count = 4 * segments;
        std::int32_t recolor = Recolor();
        if (Recolor() > 0)
        {
            --recolor;
        }
        const Vector3 vec = point2 - point1;
        ModelInstance& trailModel = RequireReference(_trailModel);
        Model& model = RequireReference(trailModel.Model());
        MphRead::Recolor& recolorData = RequireReference(ManagedAt(model.Recolors, recolor));
        const Texture& texture = ManagedAt(recolorData.Textures, 0);
        const float uvT = (texture.Height - (1.0F / 16.0F)) / texture.Height;
        auto uvsAndVerts = MphRead::NativeRuntime::RentFromSharedArrayPool(count);
        for (std::int32_t i = 0; i < segments; ++i)
        {
            float uvS = 0.0F;
            if (i > 0)
            {
                uvS = (texture.Width / static_cast<float>(segments - 1) * i
                    - (1.0F / 16.0F)) / texture.Width;
            }
            const float pct = i * (1.0F / (segments - 1));
            float x = vec.X * pct;
            float y = vec.Y * pct;
            float z = vec.Z * pct;
            if (i > 0 && i < segments - 1)
            {
                const std::int32_t ownerSlot = RequireReference(_owner).SlotIndex();
                x += Mods::Render::LockjawTrailNoise::Sample(_lockjawVisualTick, ownerSlot,
                    _bombIndex, targetBombIndex, i, 0);
                y += Mods::Render::LockjawTrailNoise::Sample(_lockjawVisualTick, ownerSlot,
                    _bombIndex, targetBombIndex, i, 1);
                z += Mods::Render::LockjawTrailNoise::Sample(_lockjawVisualTick, ownerSlot,
                    _bombIndex, targetBombIndex, i, 2);
            }
            (*uvsAndVerts)[static_cast<std::size_t>(4 * i)]
                = Vector3(uvS, 0.0F, 0.0F);
            (*uvsAndVerts)[static_cast<std::size_t>(4 * i + 1)]
                = Vector3(x, y - height, z);
            (*uvsAndVerts)[static_cast<std::size_t>(4 * i + 2)]
                = Vector3(uvS, uvT, 0.0F);
            (*uvsAndVerts)[static_cast<std::size_t>(4 * i + 3)]
                = Vector3(x, y + height, z);
        }
        Material& material = RequireReference(ManagedAt(model.Materials, 0));
        RequireReference(_scene).AddRenderItem(
            RenderItemType::TrailMulti,
            1.0F,
            RequireReference(_scene).GetNextPolygonId(),
            Vector3(1.0F, 1.0F, 1.0F),
            material.XRepeat,
            material.YRepeat,
            material.ScaleS,
            material.ScaleT,
            CreateTranslation(point1),
            uvsAndVerts,
            _bindingId,
            BillboardMode::None,
            count);
    }

    void BombEntity::Destroy()
    {
        ModSequence = 0;
        _soundSource.StopAllSfx();
        std::int32_t owned = 0;
        if (_owner != nullptr)
        {
            auto&& bombs = SyluxBombArray(*_owner);
            owned = std::min<std::int32_t>(
                _owner->SyluxBombCount(), ManagedLength(bombs));
        }
        if (_bombType == MphRead::BombType::Lockjaw
            && _owner != nullptr
            && _bombIndex >= 0
            && _bombIndex < owned)
        {
            auto&& bombs = SyluxBombArray(*_owner);
            if (RawPointer(ManagedAt(bombs, _bombIndex)) == this)
            {
                for (std::int32_t i = _bombIndex; i < owned - 1; ++i)
                {
                    auto bomb = ManagedAt(bombs, i + 1);
                    ManagedAt(bombs, i) = bomb;
                    BombEntity* bombPtr = RawPointer(bomb);
                    if (bombPtr != nullptr)
                    {
                        bombPtr->_bombIndex = i;
                    }
                }
                ManagedAt(bombs, owned - 1) = nullptr;
                _owner->SetSyluxBombCount(
                    static_cast<std::uint8_t>(_owner->SyluxBombCount() - 1));
            }
        }
        _models = ModelList{};
        _trailModel.reset();
        if (_effect)
        {
            RequireReference(_scene).UnlinkEffectEntry(_effect);
        }
        _effect.reset();
        SetTarget(nullptr);
        _owner = nullptr;
        RequireReference(_scene).UnlinkBomb(this);
        EntityBase::Destroy();
    }

    void BombEntity::PlaySpawnSfx()
    {
        _soundSource.Update(static_cast<Vector3>(Position), 5);
        UpdateNodeRefVolume();
        const SfxId sfx = _bombType == MphRead::BombType::Stinglarva
            ? SfxId::KANDEN_ALT_ATTACK
            : SfxId::MORPH_BALL_BOMB_PLACE;
        _soundSource.PlaySfx(sfx);
    }

    std::shared_ptr<BombEntity> BombEntity::Spawn(
        PlayerEntity* owner,
        Matrix4 transform,
        Scene* scene)
    {
        PlayerEntity& ownerRef = RequireReference(owner);
        MphRead::BombType type = MphRead::BombType::MorphBall;
        if (ownerRef.Hunter() == Hunter::Kanden)
        {
            type = MphRead::BombType::Stinglarva;
        }
        else if (ownerRef.Hunter() == Hunter::Sylux)
        {
            type = MphRead::BombType::Lockjaw;
        }
        std::shared_ptr<BombEntity> bomb = RequireReference(scene).InitBomb();
        if (!bomb)
        {
            assert(false && "Failed to spawn bomb");
            return nullptr;
        }
        bomb->_owner = owner;
        bomb->_bombType = type;
        bomb->SetTarget(nullptr);
        bomb->_speed = Vector3::Zero;
        // Bomb entities are pooled; a new placement starts a new visual clock.
        bomb->_lockjawVisualTick = 0;
        bomb->ModSequence = 0;
        bomb->Transform = transform;
        bomb->SetRecolor(ownerRef.Recolor());
        bomb->_flags = BombFlags::None;
        bomb->NodeRef = Formats::Culling::NodeRef::None;
        RequireReference(scene).AddEntity(bomb);
        return bomb;
    }
}
