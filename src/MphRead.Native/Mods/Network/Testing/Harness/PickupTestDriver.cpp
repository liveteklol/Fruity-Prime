#include "Mods/Network/Testing/Harness/PickupTestDriver.hpp"

#include "Entities/ItemInstanceEntity.hpp"
#include "Entities/ItemSpawnEntity.hpp"
#include "Entities/Players/PlayerEntity.hpp"
#include "Mods/Network/Domain/PowerUps.hpp"
#include "Mods/Network/Gameplay/Pickups/NetItemState.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <utility>

namespace MphRead::Mods::Network
{
    namespace
    {
        constexpr float TurnRate = 6.0F;
        constexpr float WalkAlignmentDegrees = 35.0F;
        constexpr float StopDistanceSquared = 0.25F;
    }

    PickupTestSteering PickupTestDriver::Steer(Entities::PlayerEntity& player)
    {
        const auto& players = Entities::PlayerEntity::Players();
        constexpr std::size_t ControlSlot = 0;
        if (players.size() <= ControlSlot || players[ControlSlot] == nullptr)
        {
            return {};
        }
        const std::size_t humanPlayers = static_cast<std::size_t>(std::count_if(
            players.begin(), players.end(), [](const std::shared_ptr<Entities::PlayerEntity>& candidate)
            {
                return candidate != nullptr && !candidate->IsBot();
            }));
        if (humanPlayers < 2)
        {
            // Let both real clients join before either starts collecting. A
            // single client must not consume the target before the other can
            // observe the same contested spawn.
            return {};
        }
        const OpenTK::Mathematics::Vector3 controlPosition
            = static_cast<OpenTK::Mathematics::Vector3>(players[ControlSlot]->Position);
        OpenTK::Mathematics::Vector3 targetAnchor = controlPosition;
        for (const auto& candidate : players)
        {
            if (candidate == nullptr || candidate->IsBot()
                || candidate->SlotIndex() == static_cast<std::int32_t>(ControlSlot))
            {
                continue;
            }
            const OpenTK::Mathematics::Vector3 candidatePosition
                = static_cast<OpenTK::Mathematics::Vector3>(candidate->Position);
            targetAnchor = OpenTK::Mathematics::Vector3(
                (controlPosition.X + candidatePosition.X) * 0.5F,
                (controlPosition.Y + candidatePosition.Y) * 0.5F,
                (controlPosition.Z + candidatePosition.Z) * 0.5F);
            break;
        }

        std::shared_ptr<Entities::ItemInstanceEntity> target{};
        float targetDistanceSquared = std::numeric_limits<float>::max();

        // Both clients choose by the shared human-pair midpoint so neither
        // has a systematically shorter route to the contested spawn.
        for (const auto& spawn : NetItemState::RegisteredSpawns())
        {
            if (spawn == nullptr || !PowerUpOf(spawn->Data().ItemType).has_value())
            {
                continue;
            }

            ItemSpawnState state{};
            if (!NetItemState::TryGet(static_cast<std::int16_t>(spawn->Id), state)
                || !state.Available || !state.Active || state.SpawnCount != spawn->SpawnCount())
            {
                continue;
            }

            std::shared_ptr<Entities::ItemInstanceEntity> item = spawn->Item();
            if (item == nullptr || item->DespawnTimer() == 0)
            {
                continue;
            }

            const OpenTK::Mathematics::Vector3 itemPosition
                = static_cast<OpenTK::Mathematics::Vector3>(item->Position);
            const float distanceSquared = OpenTK::Mathematics::LengthSquared(itemPosition - targetAnchor);
            if (!std::isfinite(distanceSquared) || distanceSquared >= targetDistanceSquared)
            {
                continue;
            }

            target = std::move(item);
            targetDistanceSquared = distanceSquared;
        }

        if (target == nullptr)
        {
            return {};
        }

        const OpenTK::Mathematics::Vector3 targetPosition
            = static_cast<OpenTK::Mathematics::Vector3>(target->Position);
        const OpenTK::Mathematics::Vector3 playerPosition
            = static_cast<OpenTK::Mathematics::Vector3>(player.Position);
        const float distanceSquared = OpenTK::Mathematics::LengthSquared(targetPosition - playerPosition);
        const auto [turnX, turnY] = player.ModAimDeltaTowards(targetPosition);
        if (!std::isfinite(distanceSquared) || !std::isfinite(turnX) || !std::isfinite(turnY))
        {
            return {};
        }

        return PickupTestSteering{
            true,
            distanceSquared > StopDistanceSquared && std::abs(turnX) < WalkAlignmentDegrees,
            std::clamp(turnX, -TurnRate, TurnRate),
            std::clamp(turnY, -TurnRate, TurnRate)
        };
    }
}
