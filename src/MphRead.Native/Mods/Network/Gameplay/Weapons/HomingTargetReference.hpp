#pragma once

#include "Mods/Network/Protocol/Packets/RealtimePackets.hpp"

#include <memory>

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
    class HomingTargetReference final
    {
    public:
        [[nodiscard]] static IntentPacket::ShotTargetReference Encode(
            const Entities::EntityBase* target) noexcept;
        [[nodiscard]] static std::shared_ptr<Entities::EntityBase> Resolve(
            const IntentPacket::ShotTargetReference& reference, Scene* scene);
    };
}
