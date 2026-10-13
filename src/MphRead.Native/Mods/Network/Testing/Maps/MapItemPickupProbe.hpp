#pragma once

#include "Formats/Enums.hpp"
#include "NativeRuntime/OpenTK/Mathematics.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace MphRead
{
    class Scene;

    namespace Entities
    {
        class ItemInstanceEntity;
        class PlayerEntity;
    }
}

namespace MphRead::Mods::Network
{
    class MapItemPickupProbe final
    {
    private:
        struct Target final
        {
            ItemType Type;
            OpenTK::Mathematics::Vector3 SourcePosition;
        };

        Scene& _scene;
        std::string _room;
        bool _started = false;
        std::vector<Target> _targets{};
        std::shared_ptr<Entities::ItemInstanceEntity> _item{};
        std::int32_t _index = 0;
        std::int32_t _frames = -1;
        std::int32_t _blockedFrames = 0;
        std::int32_t _failures = 0;
        static constexpr std::int32_t _timeoutFrames = 30;
        static constexpr std::int32_t _blockedFrameLimit = 600;

        void CollectTargets();
        [[nodiscard]] bool StepTarget(
            const std::shared_ptr<Entities::PlayerEntity>& player);
        void FailCurrent(std::string_view reason);
        void FinishFailedTarget();

    public:
        MapItemPickupProbe(Scene& scene, std::string room);
        ~MapItemPickupProbe();

        MapItemPickupProbe(const MapItemPickupProbe&) = delete;
        MapItemPickupProbe& operator=(const MapItemPickupProbe&) = delete;

        void Start();
        [[nodiscard]] bool Step(const std::shared_ptr<Entities::PlayerEntity>& player);
        [[nodiscard]] std::int32_t TargetCount() const noexcept;
        [[nodiscard]] std::int32_t FailureCount() const noexcept;
    };
}
