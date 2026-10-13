#include "Mods/Network/Gameplay/Player/ReplicaRoomPlacement.hpp"

#include "Entities/RoomEntity.hpp"
#include "Formats/Collision.hpp"
#include "Formats/Model.hpp"

namespace MphRead::Mods::Network
{
    using OpenTK::Mathematics::Vector3;
    using Formats::Culling::NodeRef;
    using PlacementRules = ReplicaRoomPlacementRules;

    ReplicaRoomPlacement::Resolution ReplicaRoomPlacement::Find(const Entities::RoomEntity* room,
        Vector3 probe, Vector3 bodyCenter)
    {
        PlacementRules::Candidates candidates;
        if (!room) return candidates.Get();
        for (const auto& sides : room->_portalSides)
        {
            NodeRef candidate = NodeRef::None;
            bool allInside = true;
            for (const auto& side : sides)
            {
                const auto& portal = *side.first;
                if (!PlacementRules::IsInsidePortalSide(probe, portal.Plane, side.second))
                {
                    allInside = false;
                    break;
                }
                candidate = side.second ? portal.NodeRef2 : portal.NodeRef1;
            }
            if (!allInside || candidate == NodeRef::None
                || !GeometryBoundsContain(*room, candidate, bodyCenter)) continue;
            candidates.Consider(candidate);
            if (candidates.IsAmbiguous()) return candidates.Get();
        }
        return candidates.Get();
    }

    bool ReplicaRoomPlacement::GeometryBoundsContain(const Entities::RoomEntity& room,
        NodeRef nodeRef, Vector3 position)
    {
        if (!PlacementRules::IsFinite(position)
            || !PlacementRules::IsValidNodeRef(nodeRef, room._roomPartMax,
                room._connectorModels.size(), room._roomCollision.size()))
            return false;
        std::shared_ptr<ModelInstance> instance;
        Vector3 offset = Vector3::Zero;
        if (nodeRef.ModelIndex == 0)
        {
            const auto& models = room.GetModels();
            if (models.empty()) return false;
            instance = models[0];
        }
        else
        {
            const auto modelIndex = static_cast<std::size_t>(nodeRef.ModelIndex);
            if (!room._roomCollision[modelIndex]) return false;
            instance = room._connectorModels[modelIndex - 1];
            offset = room._roomCollision[modelIndex]->Translation;
        }
        const auto model = instance ? instance->Model() : nullptr;
        if (!model || !model->Nodes || (nodeRef.RoomName.HasValue() && !room.ModKnowsRoom(*nodeRef.RoomName)))
            return false;
        const auto& nodes = *model->Nodes;
        bool containsPosition = false;
        auto index = nodeRef.NodeIndex;
        std::size_t visited = 0;
        for (; index != -1 && visited < nodes.size(); ++visited)
        {
            if (index < 0 || static_cast<std::size_t>(index) >= nodes.size() || !nodes[static_cast<std::size_t>(index)])
                return false;
            const Node& node = *nodes[static_cast<std::size_t>(index)];
            // Original room assets name background meshes sky*. They describe
            // the view background, not the physical extent of a room part.
            if (node.Enabled && node.MeshCount > 0 && !node.Name.starts_with("sky"))
            {
                if (!node.Bounds || node.Bounds->Length() < 6) return false;
                const auto& bounds = *node.Bounds;
                const Vector3 nodeMin(bounds[0], bounds[1], bounds[2]);
                const Vector3 nodeMax(bounds[3], bounds[4], bounds[5]);
                const PlacementRules::BoundsResult boundsResult
                    = PlacementRules::CheckBounds(position, nodeMin, nodeMax, offset);
                if (boundsResult == PlacementRules::BoundsResult::Invalid) return false;
                // Do not bridge disconnected pieces with one aggregate AABB.
                // Portal membership is checked separately by Find.
                containsPosition |= boundsResult == PlacementRules::BoundsResult::Inside;
            }
            index = node.NextIndex;
        }
        return index == -1 && containsPosition;
    }
}
