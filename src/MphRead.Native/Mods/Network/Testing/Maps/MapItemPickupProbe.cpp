#include "Mods/Network/Testing/Maps/MapItemPickupProbe.hpp"

#include "Entities/CamSeq/CameraSequence.hpp"
#include "Entities/EntityBase.hpp"
#include "Entities/ItemInstanceEntity.hpp"
#include "Entities/ItemSpawnEntity.hpp"
#include "Entities/Players/PlayerEntity.hpp"
#include "Formats/Entity.hpp"
#include "Mods/Network/Testing/Harness/NetTestScript.hpp"
#include "NativeRuntime/OpenTK/Mathematics.hpp"
#include "NativeRuntime/System/Globalization.hpp"
#include "Scene.hpp"

#include <iostream>
#include <string_view>
#include <utility>

using ::OpenTK::Mathematics::AddY;
using ::OpenTK::Mathematics::Vector3;

namespace MphRead::Mods::Network
{
    MapItemPickupProbe::MapItemPickupProbe(Scene& scene, std::string room)
        : _scene(scene),
          _room(std::move(room))
    {
    }

    MapItemPickupProbe::~MapItemPickupProbe() = default;

    void MapItemPickupProbe::Start()
    {
        if (_started)
        {
            return;
        }
        CollectTargets();
        _started = true;
    }

    bool MapItemPickupProbe::Step(const std::shared_ptr<Entities::PlayerEntity>& player)
    {
        if (!_started)
        {
            Start();
        }
        if (_index >= static_cast<std::int32_t>(_targets.size()))
        {
            return false;
        }
        if (player == nullptr)
        {
            FailCurrent("main player could not be resolved");
            return _index < static_cast<std::int32_t>(_targets.size());
        }
        return StepTarget(player);
    }

    std::int32_t MapItemPickupProbe::TargetCount() const noexcept
    {
        return static_cast<std::int32_t>(_targets.size());
    }

    std::int32_t MapItemPickupProbe::FailureCount() const noexcept
    {
        return _failures;
    }

    void MapItemPickupProbe::CollectTargets()
    {
        auto enumerator = _scene.Entities().GetEnumerator();
        while (enumerator.MoveNext())
        {
            const std::shared_ptr<Entities::EntityBase> entity = enumerator.Current();
            if (entity->Type == EntityType::ItemSpawn)
            {
                const auto spawn = std::dynamic_pointer_cast<Entities::ItemSpawnEntity>(entity);
                const ItemType type = spawn->Data().ItemType;
                if (spawn->Active && type != ItemType::None)
                {
                    _targets.push_back(Target{
                        type,
                        AddY(static_cast<Vector3>(entity->Position), 0.65F)});
                }
            }
            else if (entity->Type == EntityType::ItemInstance)
            {
                const auto item = std::dynamic_pointer_cast<Entities::ItemInstanceEntity>(entity);
                if (item->Owner() == nullptr && item->DespawnTimer() != 0)
                {
                    _targets.push_back(Target{
                        item->ItemType(),
                        static_cast<Vector3>(item->Position)});
                    item->SetDespawnTimer(0);
                }
            }
        }
    }

    bool MapItemPickupProbe::StepTarget(
        const std::shared_ptr<Entities::PlayerEntity>& player)
    {
        if (_frames < 0)
        {
            if (!::MphRead::TestFlag(player->LoadFlags(), Entities::LoadFlags::Spawned)
                || player->Health() == 0)
            {
                NetTestScript::Rest(player, true);
                return true;
            }

            const Target& target = _targets[static_cast<std::size_t>(_index)];
            const Vector3 pickupPosition = static_cast<Vector3>(player->Position);
            _item = Entities::ItemSpawnEntity::SpawnItem(
                target.Type,
                pickupPosition,
                player->NodeRef,
                _blockedFrameLimit + _timeoutFrames + 1,
                &_scene);
            if (_item == nullptr)
            {
                FailCurrent("item instance could not be created");
                return true;
            }

            player->ModForceForm(false);
            player->SetIgnoreItemPickups(false);
            NetTestScript::Rest(player, true);
            _frames = 0;
            _blockedFrames = 0;
            return true;
        }

        NetTestScript::Rest(player, true);
        if (_item->DespawnTimer() == 0)
        {
            const Target& target = _targets[static_cast<std::size_t>(_index)];
            std::cout << "ITEMPICKUP PASS " << _room
                << " | " << _index << ' ' << ::MphRead::ToString(target.Type) << '\n';
            ++_index;
            _frames = -1;
            _item.reset();
            return true;
        }

        Formats::CameraSequence* sequence = Formats::CameraSequence::Current();
        if (sequence != nullptr && sequence->BlockInput())
        {
            ++_blockedFrames;
            if (_blockedFrames < _blockedFrameLimit)
            {
                return true;
            }

            const std::string reason = "camera sequence blocked player input for "
                + std::to_string(_blockedFrames) + " frames";
            FailCurrent(reason);
            return true;
        }

        ++_frames;
        if (_frames >= _timeoutFrames)
        {
            const Target& target = _targets[static_cast<std::size_t>(_index)];
            const Vector3 playerPosition = static_cast<Vector3>(player->Position);
            const Vector3 itemPosition = static_cast<Vector3>(_item->Position);
            const Vector3 difference = itemPosition - playerPosition;
            std::cout << "ITEMPICKUP FAIL " << _room
                << " | " << _index << ' ' << ::MphRead::ToString(target.Type)
                << " | no pickup within " << _timeoutFrames << " frames"
                << " | player " << ::MphRead::NativeRuntime::ToString(playerPosition.X, "0.00") << ','
                << ::MphRead::NativeRuntime::ToString(playerPosition.Y, "0.00") << ','
                << ::MphRead::NativeRuntime::ToString(playerPosition.Z, "0.00")
                << " hp " << player->Health()
                << " alt " << player->IsAltForm()
                << " ignore " << player->IgnoreItemPickups()
                << " | item " << ::MphRead::NativeRuntime::ToString(itemPosition.X, "0.00") << ','
                << ::MphRead::NativeRuntime::ToString(itemPosition.Y, "0.00") << ','
                << ::MphRead::NativeRuntime::ToString(itemPosition.Z, "0.00")
                << " | source " << ::MphRead::NativeRuntime::ToString(target.SourcePosition.X, "0.00") << ','
                << ::MphRead::NativeRuntime::ToString(target.SourcePosition.Y, "0.00") << ','
                << ::MphRead::NativeRuntime::ToString(target.SourcePosition.Z, "0.00")
                << " | delta " << ::MphRead::NativeRuntime::ToString(difference.X, "0.00") << ','
                << ::MphRead::NativeRuntime::ToString(difference.Y, "0.00") << ','
                << ::MphRead::NativeRuntime::ToString(difference.Z, "0.00")
                << " | camera blocked frames " << _blockedFrames << '\n';
            FinishFailedTarget();
        }
        return true;
    }

    void MapItemPickupProbe::FailCurrent(std::string_view reason)
    {
        const Target& target = _targets[static_cast<std::size_t>(_index)];
        std::cout << "ITEMPICKUP FAIL " << _room
            << " | " << _index << ' ' << ::MphRead::ToString(target.Type)
            << " | " << reason << '\n';
        FinishFailedTarget();
    }

    void MapItemPickupProbe::FinishFailedTarget()
    {
        ++_failures;
        ++_index;
        _frames = -1;
        if (_item != nullptr)
        {
            _item->SetDespawnTimer(0);
            _item.reset();
        }
    }
}
