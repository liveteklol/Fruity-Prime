#include "Mods/Network/Gameplay/Weapons/HomingTargetHistory.hpp"

#include "Entities/EntityBase.hpp"
#include "Entities/EnemyInstanceEntity.hpp"
#include "Entities/PlatformEntity.hpp"
#include "Entities/Players/HalfturretEntity.hpp"
#include "Entities/Players/PlayerEntity.hpp"
#include "Mods/Network/Gameplay/Weapons/HomingTargetReference.hpp"
#include "Scene.hpp"

#include <cstddef>

namespace MphRead::Mods::Network
{
    std::size_t HomingTargetHistory::At(std::uint32_t frame) noexcept
    {
        return static_cast<std::size_t>(frame % static_cast<std::uint32_t>(HistoryFrames));
    }

    void HomingTargetHistory::Reset() noexcept
    {
        for (Frame& frame : _frames)
        {
            frame.Stamp = 0;
            frame.Targets.clear();
        }
    }

    HomingTargetSnapshot HomingTargetHistory::Capture(Entities::EntityBase& entity)
    {
        HomingTargetSnapshot snapshot{};
        snapshot.Reference = HomingTargetReference::Encode(&entity);
        if (!snapshot.Reference.IsValid())
        {
            return snapshot;
        }
        snapshot.Type = entity.Type;
        entity.GetPosition(snapshot.Position);
        snapshot.Targetable = entity.GetTargetable();
        if (entity.Type == EntityType::Player)
        {
            auto& player = static_cast<Entities::PlayerEntity&>(entity);
            snapshot.TeamIndex = player.TeamIndex();
            snapshot.OwnerSlot = player.SlotIndex();
            snapshot.AltForm = player.IsAltForm();
            snapshot.Morphing = player.IsMorphing();
        }
        else if (entity.Type == EntityType::Halfturret)
        {
            auto& turret = static_cast<Entities::HalfturretEntity&>(entity);
            const auto owner = turret.Owner();
            if (owner)
            {
                snapshot.TeamIndex = owner->TeamIndex();
                snapshot.OwnerSlot = owner->SlotIndex();
            }
        }
        else if (entity.Type == EntityType::EnemyInstance)
        {
            snapshot.Flags = static_cast<std::uint32_t>(
                static_cast<Entities::EnemyInstanceEntity&>(entity).Flags());
        }
        else if (entity.Type == EntityType::Platform)
        {
            snapshot.Flags = static_cast<std::uint32_t>(
                static_cast<Entities::PlatformEntity&>(entity).Flags());
        }
        return snapshot;
    }

    void HomingTargetHistory::Record(std::uint32_t frameNumber, Scene& scene)
    {
        if (frameNumber == 0)
        {
            return;
        }
        Frame& frame = _frames[At(frameNumber)];
        frame.Stamp = frameNumber;
        frame.Targets.clear();
        for (EntityType type : TargetTypes)
        {
            auto enumerator = scene.Entities().GetEnumerator();
            while (enumerator.MoveNext())
            {
                const std::shared_ptr<Entities::EntityBase> entityPtr = enumerator.Current();
                if (!entityPtr || entityPtr->Type != type)
                {
                    continue;
                }
                HomingTargetSnapshot snapshot = Capture(*entityPtr);
                if (!snapshot.Reference.IsValid())
                {
                    continue;
                }
                frame.Targets.push_back(snapshot);
            }
        }
    }

    std::optional<HomingTargetSnapshot> HomingTargetHistory::Find(
        std::uint32_t frameNumber, const IntentPacket::ShotTargetReference& reference)
    {
        if (frameNumber == 0 || !reference.IsValid())
        {
            return std::nullopt;
        }
        const Frame& frame = _frames[At(frameNumber)];
        if (frame.Stamp != frameNumber)
        {
            return std::nullopt;
        }
        for (const HomingTargetSnapshot& candidate : frame.Targets)
        {
            if (candidate.Reference == reference)
            {
                return candidate;
            }
        }
        return std::nullopt;
    }

    std::optional<HomingTargetSnapshot> HomingTargetHistory::FindPlayer(
        std::uint32_t frameNumber, std::int32_t slot)
    {
        if (frameNumber == 0 || slot < 0)
        {
            return std::nullopt;
        }
        const Frame& frame = _frames[At(frameNumber)];
        if (frame.Stamp != frameNumber)
        {
            return std::nullopt;
        }
        for (const HomingTargetSnapshot& candidate : frame.Targets)
        {
            if (candidate.Type == EntityType::Player && candidate.OwnerSlot == slot)
            {
                return candidate;
            }
        }
        return std::nullopt;
    }

    IntentPacket::ShotTargetReference HomingTargetHistory::Select(
        std::uint32_t frameNumber, const HomingTargetSelectionRequest& request)
    {
        if (frameNumber == 0)
        {
            return {};
        }
        const Frame& frame = _frames[At(frameNumber)];
        if (frame.Stamp != frameNumber)
        {
            return {};
        }
        return HomingTargetSelection::Select(frame.Targets, request);
    }
}
