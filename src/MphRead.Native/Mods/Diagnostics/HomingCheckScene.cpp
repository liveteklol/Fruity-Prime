#include "Mods/Diagnostics/HomingCheckScene.hpp"

#include "Entities/BeamProjectileEntity.hpp"
#include "Entities/PlayerSpawnEntity.hpp"
#include "Mods/Combat/BeamObstacleTrace.hpp"
#include "Entities/Players/PlayerEntity.hpp"
#include "GameState.hpp"
#include "Metadata/Metadata.hpp"
#include "Mods/Headless.hpp"
#include "Mods/Network/Integration/Launch/NetLaunch.hpp"
#include "Mods/Network/Testing/Maps/MapAudit.hpp"
#include "NativeRuntime/System/Managed.hpp"
#include "Scene.hpp"
#include "SceneSetup.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <limits>
#include <stdexcept>

namespace MphRead::Mods::Diagnostics
{
    using Entities::BeamProjectileEntity;
    using Entities::EnemyFlags;
    using Entities::PlayerEntity;
    using OpenTK::Mathematics::Vector3;

    HomingCheckTarget::HomingCheckTarget(Scene* scene, std::uint64_t stableId, float radius)
        : EnemyInstanceEntity({EnemyType::WarWasp, nullptr, stableId}, {}, scene), _radius(radius)
    {
        _health = _healthMax = 200;
        Flags = EnemyFlags::CollideBeam;
        MoveTo(Vector3::Zero);
    }

    void HomingCheckTarget::MoveTo(Vector3 position)
    {
        Position = position;
        _hurtVolume = CollisionVolume(position, _radius);
    }

    bool HomingCheckTarget::EnemyTakeDamage(Entities::EntityBase*)
    {
        ++_hits;
        _health = _healthMax;
        return false;
    }

    HomingCheckScene::HomingCheckScene(const std::string& room, bool singlePlayer)
        : _keyboard(Input::SyntheticInput::CreateKeyboard()), _mouse(Input::SyntheticInput::CreateMouse())
    {
        Headless::Enter();
        const GameMode mode = singlePlayer ? GameMode::SinglePlayer : GameMode::Battle;
        const std::int32_t players = singlePlayer ? 1 : 2;
        GameState::Mode(mode);
        PlayerEntity::SetMaxPlayers(players);
        _scene = std::make_shared<Scene>(OpenTK::Mathematics::Vector2i(256, 192), *_keyboard, *_mouse,
            [](std::string) {}, []() {});
        _scene->AddPlayer(Hunter::Samus, 0, -1);
        if (!singlePlayer)
        {
            _scene->AddPlayer(Hunter::Kanden, 0, -1);
        }
        for (const std::shared_ptr<PlayerEntity>& player : PlayerEntity::Players())
        {
            NativeRuntime::RequireReference(player).SetIsBot(false);
        }
        // Offline, only the main player spawns by itself; -maptest's switch
        // brings everybody in.
        Network::MapAudit::ForceEveryone(true);
        PlayerEntity::SetPlayerCount(players);
        PlayerEntity::SetMainPlayerIndex(0);
        if (singlePlayer)
        {
            _scene->AddRoom(room, mode, players, BossFlags::None);
        }
        else
        {
            _scene->AddRoom(room, mode, Network::NetLaunch::RoomPlayerCount());
        }
        _scene->OnLoad();
        _equip = std::make_shared<EquipInfo>(nullptr, SceneSetup::CreateBeamList(64, _scene.get()));
        _equip->GetAmmo = []() { return std::numeric_limits<std::int32_t>::max() / 2; };
        _equip->SetAmmo = [](std::int32_t) {};
        Step();
        SpawnOthers();
    }

    void HomingCheckScene::SpawnOthers()
    {
        const auto& players = PlayerEntity::Players();
        for (std::int32_t frame = 0; frame < 600; ++frame)
        {
            if (std::all_of(players.begin(), players.end(),
                    [](const std::shared_ptr<PlayerEntity>& player)
                    {
                        return !::MphRead::TestFlag(player->LoadFlags(), Entities::LoadFlags::Active)
                            || player->ModIsInPlay();
                    }))
            {
                return;
            }
            Step();
        }
        std::string states{};
        for (const std::shared_ptr<PlayerEntity>& player : players)
        {
            states += " [slot " + std::to_string(player->SlotIndex()) + " " + Entities::ToString(player->LoadFlags())
                + " health " + std::to_string(player->Health()) + "]";
        }
        throw std::runtime_error("the other players never spawned:" + states);
    }

    HomingCheckScene::~HomingCheckScene()
    {
        Network::MapAudit::ForceEveryone(false);
        _scene->DoCleanup();
    }

    PlayerEntity& HomingCheckScene::Shooter()
    {
        return NativeRuntime::RequireReference(NativeRuntime::ManagedAt(PlayerEntity::Players(), 0));
    }

    std::shared_ptr<HomingCheckTarget> HomingCheckScene::AddTarget(Vector3 position, float radius)
    {
        auto target = std::make_shared<HomingCheckTarget>(_scene.get(), _nextTargetId++, radius);
        target->MoveTo(position);
        _scene->AddEntity(target);
        return target;
    }

    void HomingCheckScene::RemoveTarget(const std::shared_ptr<HomingCheckTarget>& target)
    {
        _scene->RemoveEntity(target);
        target->Destroy();
    }

    std::shared_ptr<BeamProjectileEntity> HomingCheckScene::Fire(const std::shared_ptr<WeaponInfo>& weapon,
        std::uint16_t chargeLevel, Vector3 origin, Vector3 direction, std::optional<Vector3> drawnDirection)
    {
        PlayerEntity& shooter = Shooter();
        _equip->Weapon = weapon;
        _equip->ChargeLevel = chargeLevel;
        auto& beams = NativeRuntime::RequireReference(_equip->Beams);
        const Entities::BeamResultFlags result = BeamProjectileEntity::Spawn(
            Entities::SharedFrom<Entities::EntityBase>(&shooter), _equip, origin, direction,
            Entities::BeamSpawnFlags::NoMuzzle, shooter.NodeRef, _scene.get(), nullptr, std::nullopt,
            drawnDirection);
        if (result == Entities::BeamResultFlags::NoSpawn)
        {
            throw std::runtime_error("the weapon row did not spawn a projectile");
        }
        // Spawn hands back no projectile: the new one is the one that has
        // not lived a frame yet. A continuous beam reuses its list entry.
        for (std::int32_t i = 0; i < beams.Length(); ++i)
        {
            const std::shared_ptr<BeamProjectileEntity>& beam = beams[i];
            if (beam->Lifespan() > 0.0F && beam->Age() == 0.0F)
            {
                return beam;
            }
        }
        std::string seen{};
        for (std::int32_t i = 0; i < beams.Length(); ++i)
        {
            if (beams[i]->Lifespan() > 0.0F || beams[i]->Age() > 0.0F)
            {
                seen += " [" + std::to_string(i) + " life " + std::to_string(beams[i]->Lifespan()) + " age "
                    + std::to_string(beams[i]->Age()) + " flags "
                    + std::to_string(static_cast<std::uint32_t>(beams[i]->Flags())) + "]";
            }
        }
        throw std::runtime_error("the spawned projectile was not found in its list:" + seen);
    }

    std::optional<HomingWallSite> HomingCheckScene::FindWall()
    {
        auto spawns = _scene->GetPlayerSpawnEntities().GetEnumerator();
        while (spawns.MoveNext())
        {
            const Vector3 from = static_cast<Vector3>(spawns.Current()->Position) + Vector3(0.0F, 1.0F, 0.0F);
            for (std::int32_t i = 0; i < 24; ++i)
            {
                const float angle = static_cast<float>(i) * std::numbers::pi_v<float> / 12.0F;
                const Vector3 direction(std::sin(angle), 0.0F, std::cos(angle));
                const Vector3 to = from + ::OpenTK::Mathematics::Scale(direction, 40.0F);
                const auto hit = Combat::TraceFirstBeamObstacle(from, to, *_scene);
                if (!hit.has_value() || hit->Entity != nullptr)
                {
                    continue;
                }
                const float distance = hit->Collision.Distance * 40.0F;
                const Vector3 normal(hit->Collision.Plane.X, hit->Collision.Plane.Y, hit->Collision.Plane.Z);
                if (distance >= 14.0F && distance <= 30.0F && Vector3::Dot(normal, direction) < -0.85F)
                {
                    return HomingWallSite{from, direction, distance};
                }
            }
        }
        return std::nullopt;
    }

    void HomingCheckScene::Step()
    {
        // No clock: a match that ends mid-measurement stops the world.
        GameState::MatchTime(-1);
        GameState::ForceEndGame(false);
        _scene->OnSimulationFrame();
    }
}
