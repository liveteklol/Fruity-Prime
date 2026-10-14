#pragma once

#include "Entities/EnemyInstanceEntity.hpp"
#include "Formats/Enums.hpp"
#include "Mods/Input/SyntheticInput.hpp"
#include "NativeRuntime/OpenTK/Mathematics.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace MphRead
{
    class EquipInfo;
    class Scene;
    class WeaponInfo;
}

namespace MphRead::Entities
{
    class BeamProjectileEntity;
    class PlayerEntity;
}

namespace MphRead::Mods::Diagnostics
{
    // A target the check moves by hand: an enemy body with no behaviour of
    // its own, which a homing shot may take and a beam may hit.
    class HomingCheckTarget final : public Entities::EnemyInstanceEntity
    {
    public:
        HomingCheckTarget(Scene* scene, std::uint64_t stableId, float radius);

        void Initialize() override {}
        [[nodiscard]] bool Process() override { return !_retired; }
        void MoveTo(OpenTK::Mathematics::Vector3 position);
        // Its next scene step takes it out the way any entity leaves.
        void Retire() noexcept { _retired = true; }
        void SetTargetable(bool value) noexcept { _health = value ? _healthMax : 0; }
        [[nodiscard]] std::int32_t Hits() const noexcept { return _hits; }

    protected:
        bool EnemyTakeDamage(Entities::EntityBase* source) override;

    private:
        float _radius = 0.5F;
        std::int32_t _hits = 0;
        bool _retired = false;
    };

    // A wall a shot can fly into: where to stand, which way to face, and how
    // far it is.
    struct HomingWallSite final
    {
        OpenTK::Mathematics::Vector3 From{};
        OpenTK::Mathematics::Vector3 Direction{};
        float Distance = 0.0F;
    };

    // One room loaded with no window and no network: a shooter in slot 0 and
    // whatever targets a check places, stepped as the game steps a frame.
    class HomingCheckScene final
    {
    public:
        // A match room with a second player, or a story room as its
        // bosses stand before they are beaten (singlePlayer).
        explicit HomingCheckScene(const std::string& room, bool singlePlayer = false);
        ~HomingCheckScene();
        HomingCheckScene(const HomingCheckScene&) = delete;
        HomingCheckScene& operator=(const HomingCheckScene&) = delete;

        [[nodiscard]] Scene& Get() noexcept { return *_scene; }
        [[nodiscard]] Entities::PlayerEntity& Shooter();
        [[nodiscard]] std::shared_ptr<HomingCheckTarget> AddTarget(
            OpenTK::Mathematics::Vector3 position, float radius = 0.5F);
        void RemoveTarget(const std::shared_ptr<HomingCheckTarget>& target);
        // Spawns a weapon row through the projectile's own Spawn, at a charge
        // level, and returns the projectile it made.
        [[nodiscard]] std::shared_ptr<Entities::BeamProjectileEntity> Fire(
            const std::shared_ptr<WeaponInfo>& weapon, std::uint16_t chargeLevel,
            OpenTK::Mathematics::Vector3 origin, OpenTK::Mathematics::Vector3 direction,
            std::optional<OpenTK::Mathematics::Vector3> drawnDirection = std::nullopt);
        // One simulation step, as the game takes it at 60 Hz.
        void Step();
        // A flat wall 14 to 30 units out from one of the room's player
        // spawns, looking along the floor.
        [[nodiscard]] std::optional<HomingWallSite> FindWall();

    private:
        void SpawnOthers();

        Input::KeyboardState _keyboard;
        Input::MouseState _mouse;
        std::shared_ptr<Scene> _scene{};
        std::shared_ptr<EquipInfo> _equip{};
        std::uint64_t _nextTargetId = 1;
    };
}
