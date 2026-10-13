#pragma once

#include "../Formats/Culling.hpp"
#include "../Formats/Types.hpp"
#include "EntityBase.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace MphRead
{
    class Model;
    class Node;
    class RoomMetadata;

    namespace Formats
    {
        class NodeData;
        class NodeData3;

        namespace Collision
        {
            class CollisionInstance;
            class Portal;
        }
    }
}

namespace MphRead::Mods::Network
{
    class ReplicaRoomPlacement;
}

namespace MphRead::Entities
{
    class DoorEntity;

    class RoomEntity : public EntityBase
    {
        friend class Mods::Network::ReplicaRoomPlacement;

    public:
        explicit RoomEntity(Scene* scene);

        RoomEntity(const RoomEntity&) = delete;
        RoomEntity& operator=(const RoomEntity&) = delete;
        RoomEntity(RoomEntity&&) = delete;
        RoomEntity& operator=(RoomEntity&&) = delete;

        [[nodiscard]] const std::vector<std::shared_ptr<Formats::Collision::CollisionInstance>>&
            RoomCollision() const noexcept;
        [[nodiscard]] std::shared_ptr<Formats::NodeData> NodeData() const noexcept;
        [[nodiscard]] std::int32_t RoomId() const noexcept;
        [[nodiscard]] const RoomMetadata& Meta() const;

        void Setup(std::string name, const RoomMetadata* meta,
            std::shared_ptr<Formats::Collision::CollisionInstance> collision,
            std::int32_t layerMask, std::int32_t roomId);
        void SetNodeData(std::shared_ptr<Formats::NodeData> nodeData);
        [[nodiscard]] std::shared_ptr<Formats::Collision::Portal>
            GetPortalByName(const std::string& name) const;
        void AddConnector(DoorEntity* door);
        void ActivateConnector(DoorEntity* door);
        void UpdateTransition();

        DoorEntity* LoaderDoor = nullptr;
        std::int32_t LoadEntityId = -1;

        void LoadRoom(bool resume);
        void CancelTransition();

        [[nodiscard]] Formats::Culling::NodeRef GetNodeRefByName(
            const std::string& nodeName) const;
        [[nodiscard]] bool PartCouldContain(std::int32_t partIndex,
            ::OpenTK::Mathematics::Vector3 position, float margin);
        [[nodiscard]] Formats::Culling::NodeRef GetNodeRefByPosition(
            ::OpenTK::Mathematics::Vector3 position);
        [[nodiscard]] Formats::Culling::NodeRef UpdateNodeRef(
            Formats::Culling::NodeRef current,
            ::OpenTK::Mathematics::Vector3 prevPos,
            ::OpenTK::Mathematics::Vector3 curPos) const;
        [[nodiscard]] bool IsNodeRefAudible(Formats::Culling::NodeRef nodeRef) const;
        [[nodiscard]] bool IsNodeRefVisible(Formats::Culling::NodeRef nodeRef) const;

        void GetDrawInfo() override;
        void GetDisplayVolumes() override;

    protected:
        [[nodiscard]] bool UseNodeTransform() const override;
        void GetCollisionDrawInfo() override;

    private:
        struct PortalNodeRef final
        {
            const std::shared_ptr<Formats::Collision::Portal> Portal;
            const std::int32_t NodeIndex;

            PortalNodeRef(std::shared_ptr<Formats::Collision::Portal> portal,
                std::int32_t nodeIndex) noexcept;
        };

        static constexpr std::int32_t _roomPartMax = 32;
        static constexpr float _partBoundsMargin = 4.0F;

        [[nodiscard]] const std::vector<std::shared_ptr<Node>>& Nodes() const;
        [[nodiscard]] Formats::Culling::NodeRef AddDoorPortal(DoorEntity* door);
        void StartTransition(bool fromDoor, bool resume = false);
        void ProcessTransition(std::shared_ptr<const std::atomic_bool> token,
            std::shared_ptr<RoomEntity> self);
        void EndTransition();

        [[nodiscard]] std::shared_ptr<Formats::Culling::RoomPartVisInfo>
            GetPartVisInfo(Formats::Culling::NodeRef nodeRef);
        [[nodiscard]] std::shared_ptr<Formats::Culling::RoomFrustumItem>
            GetRoomFrustumItem();
        void ClearRoomPartState();
        [[nodiscard]] bool ModCanPlace(Formats::Culling::NodeRef nodeRef) const;
        [[nodiscard]] bool ModKnowsRoom(const std::string& name) const;
        void ModNoteStaleNodeRef(Formats::Culling::NodeRef nodeRef,
            const std::string& where);
        void UpdateRoomParts();
        void FindVisibleRoomParts(
            const std::shared_ptr<Formats::Culling::RoomFrustumItem>& frustumItem,
            Formats::Culling::NodeRef mainNodeRef);
        [[nodiscard]] float Func2117F84(::OpenTK::Mathematics::Vector3 point,
            ::OpenTK::Mathematics::Vector3& dest) const;
        [[nodiscard]] std::int32_t Func21180A8(
            const Formats::Culling::FrustumInfo& frustumInfo,
            ::OpenTK::Mathematics::Vector3* pointList, std::int32_t pointCount,
            ::OpenTK::Mathematics::Vector3* destList) const;
        [[nodiscard]] float GetDistanceToPortal(
            ::OpenTK::Mathematics::Vector3 pos,
            ::OpenTK::Mathematics::Vector4 plane, bool otherSide) const noexcept;
        void FindAudibleRoomParts(Formats::Culling::NodeRef nodeRef,
            Formats::Culling::NodeRef mainNodeRef);

        void EnsurePartBounds();
        void AddPartBounds(ModelInstance& inst,
            ::OpenTK::Mathematics::Vector3 offset);
        [[nodiscard]] float PartBoundsDepth(std::int32_t partIndex,
            ::OpenTK::Mathematics::Vector3 position);

        [[nodiscard]] bool IsNodeVisible(
            const Formats::Culling::FrustumInfo& frustumInfo,
            const Node& node, std::int32_t mask,
            ::OpenTK::Mathematics::Vector3 offset) const;
        void DrawRoomParts(ModelInstance& roomInst);
        void DrawAllNodes(ModelInstance& inst, bool connector = false);
        void GetItems(ModelInstance& inst, Node& node,
            const std::shared_ptr<Formats::Collision::Portal>& portal = nullptr);
        [[nodiscard]] float GetPortalAlpha(
            ::OpenTK::Mathematics::Vector3 portalPosition,
            ::OpenTK::Mathematics::Vector3 cameraPosition) const;

        std::vector<std::shared_ptr<Formats::Collision::CollisionInstance>> _roomCollision{};
        std::vector<std::shared_ptr<Formats::Collision::Portal>> _portals{};
        std::vector<std::vector<std::pair<std::shared_ptr<Formats::Collision::Portal>, bool>>>
            _portalSides{};
        std::vector<PortalNodeRef> _forceFields{};
        std::int32_t _nextRoomPartId = 0;
        std::int32_t _doorPortalCount = 0;
        const RoomMetadata* _meta = nullptr;
        std::shared_ptr<Formats::NodeData> _nodeData{};
        std::vector<std::shared_ptr<ModelInstance>> _connectorModels{};
        const std::shared_ptr<ManagedArray<float>> _emptyMatrixStack
            = ManagedArray<float>::Empty();
        std::int32_t _roomId = 0;

        std::vector<::OpenTK::Mathematics::Vector3> _partBoundsMin{};
        std::vector<::OpenTK::Mathematics::Vector3> _partBoundsMax{};
        std::int32_t _partBoundsBuiltFor = -1;

        std::unordered_map<const Node*, std::shared_ptr<Node>> _nodePairs{};
        std::unordered_set<const Node*> _excludedNodes{};
        std::vector<std::shared_ptr<Node>> _morphCameraExcludeNodes{};

        std::shared_ptr<std::atomic_bool> _cts = std::make_shared<std::atomic_bool>(false);
        std::shared_ptr<Model> _unloadModel{};

        std::array<bool, _roomPartMax> _activeRoomParts{};
        std::array<bool, _roomPartMax> _audibleRoomParts{};
        std::int32_t _visNodeRefRecursionDepth = 0;
        std::int32_t _audNodeRefRecursionDepth = 0;
        std::shared_ptr<Formats::Culling::RoomPartVisInfo> _partVisInfoHead{};
        std::array<std::shared_ptr<Formats::Culling::RoomPartVisInfo>, _roomPartMax>
            _partVisInfo{};
        std::int32_t _roomFrustumIndex = 0;
        std::array<std::shared_ptr<Formats::Culling::RoomFrustumItem>, _roomPartMax>
            _roomFrustumItems{};
        std::array<std::shared_ptr<Formats::Culling::RoomFrustumItem>, _roomPartMax>
            _roomFrustumLinks{};
        bool _modReportedStaleNodeRef = false;

        std::unordered_set<const Formats::NodeData3*> _drawnNodeData{};

        static const std::array<::OpenTK::Mathematics::Vector3, 27> _connectorSizes;
        static const std::array<bool, 27> _keepEntities;
        static std::array<::OpenTK::Mathematics::Vector3, 14> _startPointList;
        static std::array<::OpenTK::Mathematics::Vector3, 14> _destPointList;
    };
}
