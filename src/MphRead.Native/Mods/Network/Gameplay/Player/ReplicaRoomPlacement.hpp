#pragma once

#include "Formats/Culling.hpp"
#include "Mods/Network/Gameplay/Player/ReplicaRoomPlacementRules.hpp"

namespace MphRead::Entities
{
    class RoomEntity;
}

namespace MphRead::Mods::Network
{
    // Remote placement confidence. Reads the room's existing geometry without
    // changing its gameplay lookup, camera visibility, or rendering policy.
    class ReplicaRoomPlacement final
    {
    public:
        using Resolution = ReplicaRoomPlacementRules::Resolution;

        ReplicaRoomPlacement() = delete;

        [[nodiscard]] static Resolution Find(const Entities::RoomEntity* room,
            OpenTK::Mathematics::Vector3 probe, OpenTK::Mathematics::Vector3 bodyCenter);

    private:
        [[nodiscard]] static bool GeometryBoundsContain(const Entities::RoomEntity& room,
            Formats::Culling::NodeRef nodeRef, OpenTK::Mathematics::Vector3 position);
    };
}
