#pragma once

#include "Mods/Network/Gameplay/Weapons/HomingTargetSelection.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace MphRead
{
    class Scene;
}

namespace MphRead::Entities
{
    class EntityBase;
}

namespace MphRead::Mods::Network
{
    class HomingTargetHistory final
    {
    public:
        HomingTargetHistory() = delete;

        static constexpr std::int32_t HistoryFrames = 128;
        static constexpr std::array<EntityType, 5> TargetTypes{
            EntityType::Player,
            EntityType::Halfturret,
            EntityType::EnemyInstance,
            EntityType::Door,
            EntityType::Platform
        };

        static void Reset() noexcept;
        [[nodiscard]] static HomingTargetSnapshot Capture(Entities::EntityBase& entity);
        static void Record(std::uint32_t frame, Scene& scene);
        [[nodiscard]] static std::optional<HomingTargetSnapshot> Find(
            std::uint32_t frame, const IntentPacket::ShotTargetReference& reference);
        [[nodiscard]] static std::optional<HomingTargetSnapshot> FindPlayer(
            std::uint32_t frame, std::int32_t slot);
        [[nodiscard]] static IntentPacket::ShotTargetReference Select(
            std::uint32_t frame, const HomingTargetSelectionRequest& request);

    private:
        struct Frame final
        {
            std::uint32_t Stamp = 0;
            std::vector<HomingTargetSnapshot> Targets{};
        };

        [[nodiscard]] static std::size_t At(std::uint32_t frame) noexcept;
        inline static std::array<Frame, HistoryFrames> _frames{};
    };
}
