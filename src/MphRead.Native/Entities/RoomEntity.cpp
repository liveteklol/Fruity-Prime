#include "RoomEntity.hpp"

#include "../NativeRuntime/System/Buffers.hpp"
#include "../NativeRuntime/System/SceneGate.hpp"

#include "../Formats/Collision.hpp"
#include "../Formats/CollisionDetection.hpp"
#include "../Formats/Entity.hpp"
#include "../Formats/AiPersonality.hpp"
#include "../Formats/NodeData.hpp"
#include "../GameState.hpp"
#include "../Metadata/Metadata.hpp"
#include "../Metadata/Rooms.hpp"
#include "Mods/Network/Session/NetRoomChange.hpp"
#include "Mods/Network/Diagnostics/NetLog.hpp"
#include "../Program.hpp"
#include "../Read.hpp"
#include "../Renderer.hpp"
#include "../Scene.hpp"
#include "../SceneSetup.hpp"
#include "../Selection.hpp"
#include "../Sound/Music.hpp"
#include "../Sound/Sfx.hpp"
#include "../Utility/Rng.hpp"
#include "BeamEffectEntity.hpp"
#include "BeamProjectileEntity.hpp"
#include "BombEntity.hpp"
#include "CamSeq/CamSeqEntity.hpp"
#include "CamSeq/CameraSequence.hpp"
#include "DoorEntity.hpp"
#include "EnemySpawnEntity.hpp"
#include "Players/PlayerEntity.hpp"
#include "../Mods/MapGen/CustomRooms.hpp"
#include "../Mods/MapGen/Q3Movers.hpp"
#include "../Mods/MapGen/Q3Speakers.hpp"
#include "../NativeRuntime/System/Managed.hpp"
#include "../NativeRuntime/OpenTK/Mathematics.hpp"
#include "../Formats/Types.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <future>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

using ::MphRead::NativeRuntime::ManagedAt;
using ::MphRead::NativeRuntime::ManagedListAt;
using ::MphRead::NativeRuntime::MathMax;
using ::MphRead::NativeRuntime::MathMin;
using ::MphRead::NativeRuntime::RequireReference;
using ::MphRead::NativeRuntime::UncheckedIncrement;
using ::MphRead::TestFlag;
using ::OpenTK::Mathematics::Add;
using ::OpenTK::Mathematics::ComponentMax;
using ::OpenTK::Mathematics::ComponentMin;
using ::OpenTK::Mathematics::CreateScale;
using ::OpenTK::Mathematics::Divide;
using ::OpenTK::Mathematics::Equal;
using ::OpenTK::Mathematics::IdentityMatrix;
using ::OpenTK::Mathematics::Length;
using ::OpenTK::Mathematics::Negate;
using ::OpenTK::Mathematics::ScaleVector;
using ::OpenTK::Mathematics::Subtract;

namespace
{
    using MphRead::Formats::Culling::FrustumInfo;
    using MphRead::Formats::Culling::FrustumPlane;
    using MphRead::Formats::Culling::NodeRef;
    using OpenTK::Mathematics::Matrix4;
    using OpenTK::Mathematics::Vector3;
    using OpenTK::Mathematics::Vector4;

    [[noreturn]] void ThrowNullReference()
    {
        throw System::NullReferenceException();
    }

    [[nodiscard]] Vector3& PointAt14(Vector3* values, std::int32_t index)
    {
        if (values == nullptr)
        {
            ThrowNullReference();
        }
        if (index < 0 || index >= 14)
        {
            throw MphRead::SceneDetail::IndexOutOfRangeException();
        }
        return values[index];
    }

    [[nodiscard]] const Vector3& PointAt14(const Vector3* values, std::int32_t index)
    {
        if (values == nullptr)
        {
            ThrowNullReference();
        }
        if (index < 0 || index >= 14)
        {
            throw MphRead::SceneDetail::IndexOutOfRangeException();
        }
        return values[index];
    }

    [[nodiscard]] std::vector<float> CopyManagedArray(
        const MphRead::ManagedArray<float>& values)
    {
        std::vector<float> result;
        result.reserve(values.Length());
        for (std::size_t i = 0; i < values.Length(); ++i)
        {
            result.push_back(values[i]);
        }
        return result;
    }

    [[nodiscard]] bool StartsWith(std::string_view value, std::string_view prefix) noexcept
    {
        return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
    }

    template <typename TEnum>
    [[nodiscard]] constexpr TEnum OrFlag(TEnum value, TEnum flag) noexcept
    {
        using U = std::underlying_type_t<TEnum>;
        return static_cast<TEnum>(static_cast<U>(value) | static_cast<U>(flag));
    }

    template <typename TEnum>
    [[nodiscard]] constexpr TEnum AndNotFlag(TEnum value, TEnum flag) noexcept
    {
        using U = std::underlying_type_t<TEnum>;
        return static_cast<TEnum>(static_cast<U>(value) & ~static_cast<U>(flag));
    }

}

namespace MphRead::Entities
{
    using Formats::Collision::CollisionInstance;
    using Formats::Collision::Portal;
    using Formats::CollisionDetection;
    using Formats::Culling::RoomFrustumItem;
    using Formats::Culling::RoomPartVisInfo;

    const std::array<Vector3, 27> RoomEntity::_connectorSizes{
        Vector3(10.0F, 0.0F, 0.0F), Vector3(10.0F, 0.0F, 0.0F),
        Vector3(0.0F, 0.0F, 10.0F), Vector3(0.0F, 0.0F, 10.0F),
        Vector3(10.0F, 0.0F, 0.0F), Vector3(10.0F, 0.0F, 0.0F),
        Vector3(0.0F, 0.0F, 10.0F), Vector3(0.0F, 0.0F, 10.0F),
        Vector3(Fixed::ToFloat(0xA60F), 0.0F, 0.0F),
        Vector3(Fixed::ToFloat(0xA60F), 0.0F, 0.0F),
        Vector3(0.0F, 0.0F, Fixed::ToFloat(0xA60F)),
        Vector3(0.0F, 0.0F, Fixed::ToFloat(0xA60F)),
        Vector3(10.0F, 0.0F, 0.0F), Vector3(10.0F, 0.0F, 0.0F),
        Vector3(0.0F, 0.0F, 10.0F), Vector3(0.0F, 0.0F, 10.0F),
        Vector3(10.0F, 0.0F, 0.0F), Vector3(10.0F, 0.0F, 0.0F),
        Vector3(0.0F, 0.0F, 10.0F), Vector3(0.0F, 0.0F, 10.0F),
        Vector3(0.0F, Fixed::ToFloat(0x24B9), Fixed::ToFloat(0x16A77)),
        Vector3(0.0F, Fixed::ToFloat(-7659), Fixed::ToFloat(0x16A76)),
        Vector3(10.0F, 0.0F, 0.0F), Vector3(10.0F, 0.0F, 0.0F),
        Vector3(0.0F, 0.0F, 20.0F), Vector3(0.0F, 0.0F, 10.0F),
        Vector3(0.0F, 0.0F, 10.0F)
    };

    const std::array<bool, 27> RoomEntity::_keepEntities{
        false, false, false, false, false, false, false, false, false,
        false, false, false, false, false, false, false, false, false,
        false, false, false, true, true, false, true, true, true
    };

    std::array<Vector3, 14> RoomEntity::_startPointList{};
    std::array<Vector3, 14> RoomEntity::_destPointList{};

    RoomEntity::PortalNodeRef::PortalNodeRef(
        std::shared_ptr<MphRead::Formats::Collision::Portal> portal, std::int32_t nodeIndex) noexcept
        : Portal(std::move(portal)), NodeIndex(nodeIndex)
    {
    }

    RoomEntity::RoomEntity(Scene* scene)
        : EntityBase(EntityType::Room, scene)
    {
        for (std::int32_t i = 0; i < _roomPartMax; ++i)
        {
            _partVisInfo[static_cast<std::size_t>(i)] = std::make_shared<RoomPartVisInfo>();
            _roomFrustumItems[static_cast<std::size_t>(i)] = std::make_shared<RoomFrustumItem>();
        }
    }

    const std::vector<std::shared_ptr<CollisionInstance>>& RoomEntity::RoomCollision() const noexcept
    {
        return _roomCollision;
    }

    std::shared_ptr<Formats::NodeData> RoomEntity::NodeData() const noexcept
    {
        return _nodeData;
    }

    std::int32_t RoomEntity::RoomId() const noexcept
    {
        return _roomId;
    }

    const RoomMetadata& RoomEntity::Meta() const
    {
        return RequireReference(_meta);
    }

    const std::vector<std::shared_ptr<Node>>& RoomEntity::Nodes() const
    {
        ModelInstance& inst = RequireReference(ManagedListAt(_models.Items(), 0));
        return RequireReference(RequireReference((inst).Model()).Nodes);
    }

    bool RoomEntity::UseNodeTransform() const
    {
        return false;
    }

    void RoomEntity::Setup(std::string name, const RoomMetadata* metaValue,
        std::shared_ptr<CollisionInstance> collisionValue,
        std::int32_t layerMask, std::int32_t roomId)
    {
        _portals.clear();
        _portalSides.clear();
        _forceFields.clear();
        _nodePairs.clear();
        _morphCameraExcludeNodes.clear();
        _partBoundsBuiltFor = -1;
        _nextRoomPartId = 0;
        _doorPortalCount = 0;

        std::shared_ptr<ModelInstance> instValue = Read::GetRoomModelInstance(name);
        if (_models.Size() == 0)
        {
            _models.Add(instValue);
            Scene& scene = RequireReference(_scene);
            ModelInstance& inst = RequireReference(instValue);
            scene.LoadModel(inst.Model(), true);
            inst.SetAnimation(0);
        }
        else
        {
            ModelInstance& previous = RequireReference(ManagedListAt(_models.Items(), 0));
            _unloadModel = previous.Model();
            ModelInstance& inst = RequireReference(instValue);
            if (_unloadModel == inst.Model())
            {
                _unloadModel.reset();
            }
            auto& items = const_cast<std::vector<std::shared_ptr<ModelInstance>>&>(_models.Items());
            items[0] = instValue;
        }

        ModelInstance& inst = RequireReference(instValue);
        RequireReference((inst).Model()).FilterNodes(layerMask);
        const RoomMetadata& meta = RequireReference(metaValue);

        if (meta.Name == "UNIT2_C6")
        {
            RequireReference(ManagedListAt(Nodes(), 46)).Enabled = false;
        }
        else if (meta.Name == "UNIT1_RM4" || meta.Name == "MP3 PROVING GROUND")
        {
            const std::shared_ptr<Node> key1 = ManagedListAt(Nodes(), 16);
            const std::shared_ptr<Node> value1 = ManagedListAt(Nodes(), 26);
            if (!_nodePairs.emplace(key1.get(), value1).second)
            {
                throw SceneDetail::DuplicateKeyException();
            }
            const std::shared_ptr<Node> key2 = ManagedListAt(Nodes(), 25);
            const std::shared_ptr<Node> value2 = ManagedListAt(Nodes(), 17);
            if (!_nodePairs.emplace(key2.get(), value2).second)
            {
                throw SceneDetail::DuplicateKeyException();
            }
            const std::shared_ptr<Node> key3 = ManagedListAt(Nodes(), 17);
            const std::shared_ptr<Node> value3 = ManagedListAt(Nodes(), 25);
            if (!_nodePairs.emplace(key3.get(), value3).second)
            {
                throw SceneDetail::DuplicateKeyException();
            }
            const std::shared_ptr<Node> key4 = ManagedListAt(Nodes(), 26);
            const std::shared_ptr<Node> value4 = ManagedListAt(Nodes(), 16);
            if (!_nodePairs.emplace(key4.get(), value4).second)
            {
                throw SceneDetail::DuplicateKeyException();
            }
        }
        else if (meta.Name == "UNIT3_C2")
        {
            _morphCameraExcludeNodes.push_back(ManagedListAt(Nodes(), 16));
        }

        _meta = metaValue;
        std::shared_ptr<Model> modelValue = inst.Model();
        CollisionInstance& collision = RequireReference(collisionValue);
        const auto& collisionPortals = RequireReference(RequireReference(collision.Info).Portals);
        _portals.insert(_portals.end(), collisionPortals.begin(), collisionPortals.end());

        if (!_portals.empty())
        {
            Model& model = RequireReference(modelValue);
            const auto& nodes = RequireReference(model.Nodes);
            for (const std::shared_ptr<Node>& nodeValue : nodes)
            {
                Node& node = RequireReference(nodeValue);
                bool contains = false;
                for (const std::shared_ptr<Portal>& portalValue : _portals)
                {
                    if (RequireReference(portalValue).NodeName1 == node.Name)
                    {
                        contains = true;
                        break;
                    }
                }
                if (!contains)
                {
                    for (const std::shared_ptr<Portal>& portalValue : _portals)
                    {
                        if (RequireReference(portalValue).NodeName2 == node.Name)
                        {
                            contains = true;
                            break;
                        }
                    }
                }
                if (contains)
                {
                    node.RoomPartId = _nextRoomPartId;
                    _nextRoomPartId = UncheckedIncrement(_nextRoomPartId);
                    _portalSides.emplace_back();
                }
            }

            for (const std::shared_ptr<Portal>& portalValue : _portals)
            {
                Portal& portal = RequireReference(portalValue);
                for (const std::shared_ptr<Node>& nodeValue : nodes)
                {
                    Node& node = RequireReference(nodeValue);
                    if (node.Name == portal.NodeName1)
                    {
                        assert(node.RoomPartId >= 0);
                        assert(node.ChildIndex != -1);
                        portal.NodeRef1 = Formats::Culling::NodeRef(meta.Name, node.RoomPartId, node.ChildIndex, 0);
                        ManagedListAt(_portalSides, node.RoomPartId).emplace_back(portalValue, false);
                    }
                    if (node.Name == portal.NodeName2)
                    {
                        assert(node.RoomPartId >= 0);
                        assert(node.ChildIndex != -1);
                        portal.NodeRef2 = Formats::Culling::NodeRef(meta.Name, node.RoomPartId, node.ChildIndex, 0);
                        ManagedListAt(_portalSides, node.RoomPartId).emplace_back(portalValue, true);
                    }
                }
            }

            std::int32_t pmagCount = 0;
            for (const std::shared_ptr<Portal>& portalValue : _portals)
            {
                Portal& portal = RequireReference(portalValue);
                if (!StartsWith(portal.Name, "pmag"))
                {
                    continue;
                }
                pmagCount = UncheckedIncrement(pmagCount);
                const std::string geometryName = "geo" + portal.Name.substr(1);
                for (std::size_t j = 0; j < nodes.size(); ++j)
                {
                    if (RequireReference(nodes[j]).Name == geometryName)
                    {
                        _forceFields.emplace_back(portalValue, static_cast<std::int32_t>(j));
                        break;
                    }
                }
            }
            assert(static_cast<std::int32_t>(_forceFields.size()) == pmagCount
                || model.Name == "biodefense chamber 04"
                || model.Name == "biodefense chamber 07");
        }
        else if (meta.RoomNodeName.has_value())
        {
            Model& model = RequireReference(modelValue);
            const auto& nodes = RequireReference(model.Nodes);
            std::shared_ptr<Node> roomNode{};
            for (const std::shared_ptr<Node>& nodeValue : nodes)
            {
                Node& node = RequireReference(nodeValue);
                if (node.Name == *meta.RoomNodeName && node.ChildIndex != -1)
                {
                    roomNode = nodeValue;
                    break;
                }
            }
            if (roomNode != nullptr)
            {
                Node& node = RequireReference(roomNode);
                node.RoomPartId = _nextRoomPartId;
                _nextRoomPartId = UncheckedIncrement(_nextRoomPartId);
            }
            else
            {
                for (const std::shared_ptr<Node>& nodeValue : nodes)
                {
                    Node& node = RequireReference(nodeValue);
                    if (StartsWith(node.Name, "rm"))
                    {
                        node.RoomPartId = _nextRoomPartId;
                        _nextRoomPartId = UncheckedIncrement(_nextRoomPartId);
                        break;
                    }
                }
            }
        }
        else
        {
            Model& model = RequireReference(modelValue);
            const auto& nodes = RequireReference(model.Nodes);
            for (const std::shared_ptr<Node>& nodeValue : nodes)
            {
                Node& node = RequireReference(nodeValue);
                if (StartsWith(node.Name, "rm"))
                {
                    node.RoomPartId = _nextRoomPartId;
                    _nextRoomPartId = UncheckedIncrement(_nextRoomPartId);
                    break;
                }
            }
        }

        assert([&]()
        {
            const auto& nodes = RequireReference(RequireReference(modelValue).Nodes);
            for (const std::shared_ptr<Node>& nodeValue : nodes)
            {
                if (RequireReference(nodeValue).RoomPartId >= 0)
                {
                    return true;
                }
            }
            return false;
        }());

        collision.Translation = Vector3::Zero;
        if (_roomCollision.empty())
        {
            _roomCollision.push_back(std::move(collisionValue));
        }
        else
        {
            _roomCollision[0] = std::move(collisionValue);
        }
        _roomId = roomId;
        RequireReference(_scene).RoomId(roomId);
    }

    void RoomEntity::SetNodeData(std::shared_ptr<Formats::NodeData> nodeData)
    {
        _nodeData = std::move(nodeData);
        if (_nodeData != nullptr && _models.Size() < 2)
        {
            _models.Add(Read::GetModelInstance(
                "pick_wpn_missile", false, static_cast<MetaDir>(0), true));
        }
    }

    std::shared_ptr<Portal> RoomEntity::GetPortalByName(const std::string& name) const
    {
        for (const std::shared_ptr<Portal>& portalValue : _portals)
        {
            if (RequireReference(portalValue).Name == name)
            {
                return portalValue;
            }
        }
        return nullptr;
    }

    Formats::Culling::NodeRef RoomEntity::AddDoorPortal(DoorEntity* doorValue)
    {
        if (!GameState::SinglePlayer())
        {
            return Formats::Culling::NodeRef::None;
        }
        DoorEntity& door = RequireReference(doorValue);
        _doorPortalCount = UncheckedIncrement(_doorPortalCount);
        std::string roomNodeName;
        std::int32_t roomPartId = -1;
        std::int32_t roomNodeIndex = -1;
        ModelInstance& roomInst = RequireReference(ManagedListAt(_models.Items(), 0));
        const auto& roomNodes = RequireReference(RequireReference((roomInst).Model()).Nodes);
        for (const std::shared_ptr<Node>& nodeValue : roomNodes)
        {
            Node& node = RequireReference(nodeValue);
            if (node.ChildIndex == door.NodeRef.NodeIndex && node.RoomPartId == door.NodeRef.PartIndex)
            {
                roomNodeName = node.Name;
                roomPartId = node.RoomPartId;
                roomNodeIndex = node.ChildIndex;
            }
        }
        if (roomPartId == -1)
        {
            throw ProgramException("Connector did not match room part node.");
        }
        const DoorEntityData doorData = door.Data();
        const RoomMetadata* meta = Metadata::GetRoomById(static_cast<std::int32_t>(doorData.ConnectorId));
        assert(meta != nullptr);
        std::shared_ptr<ModelInstance> conInstValue = Read::GetRoomModelInstance(RequireReference(meta).Name);
        ModelInstance& conInst = RequireReference(conInstValue);
        const auto& conNodes = RequireReference(RequireReference((conInst).Model()).Nodes);
        const std::string connectorName = ::MphRead::MarshalExtensions::MarshalUtf8(door.Data().RoomName);
        for (const std::shared_ptr<Node>& nodeValue : conNodes)
        {
            Node& node = RequireReference(nodeValue);
            if (StartsWith(node.Name, "rm"))
            {
                node.RoomPartId = _nextRoomPartId;
                _nextRoomPartId = UncheckedIncrement(_nextRoomPartId);
                std::vector<std::pair<std::shared_ptr<Portal>, bool>> sides;
                std::shared_ptr<Portal> portalValue = door.SetUpPort(roomNodeName, node.Name);
                Portal& portal = RequireReference(portalValue);
                portal.NodeRef1 = Formats::Culling::NodeRef(Meta().Name, roomPartId, roomNodeIndex, 0);
                portal.NodeRef2 = Formats::Culling::NodeRef(connectorName, node.RoomPartId, node.ChildIndex, _doorPortalCount);
                if (_portalSides.empty())
                {
                    assert(roomPartId == 0);
                    _portalSides.emplace_back();
                }
                ManagedListAt(_portalSides, roomPartId).emplace_back(portalValue, false);
                sides.emplace_back(portalValue, true);
                _portalSides.push_back(std::move(sides));
                _portals.push_back(portalValue);
                return portal.NodeRef2;
            }
        }
        return Formats::Culling::NodeRef::None;
    }

    void RoomEntity::AddConnector(DoorEntity* doorValue)
    {
        DoorEntity& door = RequireReference(doorValue);
        const DoorEntityData doorData = door.Data();
        const std::int32_t connectorId = static_cast<std::int32_t>(doorData.ConnectorId);
        assert(connectorId >= 0 && connectorId < static_cast<std::int32_t>(_connectorSizes.size()));
        Vector3 size = ManagedAt(_connectorSizes, connectorId);
        const Vector3 doorFacing = door.FacingVector();
        if (doorFacing.X > Fixed::ToFloat(2896) || doorFacing.Z > Fixed::ToFloat(2896))
        {
            size = Negate(size);
        }
        const RoomMetadata* meta = Metadata::GetRoomById(connectorId);
        assert(meta != nullptr);
        std::shared_ptr<ModelInstance> conInstValue = Read::GetRoomModelInstance(RequireReference(meta).Name);
        ModelInstance& conInst = RequireReference(conInstValue);
        Scene& scene = RequireReference(_scene);
        scene.LoadModel(conInst.Model());
        _connectorModels.push_back(conInstValue);
        std::shared_ptr<CollisionInstance> collisionValue
            = Formats::Collision::Collision::GetCollision(meta, -1);
        CollisionInstance& collision = RequireReference(collisionValue);
        collision.ConnectorName = ::MphRead::MarshalExtensions::MarshalUtf8(door.Data().RoomName);
        collision.Translation = Add(static_cast<Vector3>(door.Position), Divide(size, 2.0F));
        _roomCollision.push_back(collisionValue);
        conInst.Active = false;
        collision.Active = false;
        if (!GameState::InRoomTransition())
        {
            conInst.NodeAnimIgnoreRoot = true;
        }
        door.SetConnectorModel(conInstValue);
        door.SetConnectorCollision(collisionValue);
        const EntityDataHeader header(
            static_cast<std::uint16_t>(EntityType::Door), static_cast<std::int16_t>(-1),
            Add(static_cast<Vector3>(door.Position), size), door.UpVector(), Negate(doorFacing));
        const DoorEntityData currentDoorData = door.Data();
        const DoorEntityData data(
            header, std::nullopt, currentDoorData.PaletteId, currentDoorData.DoorType,
            255, 0, 0, 255, currentDoorData.OutLoaderId, std::nullopt, std::nullopt);
        const auto& nodes = RequireReference(RequireReference((conInst).Model()).Nodes);
        std::string nodeName = "rmMain";
        for (const std::shared_ptr<Node>& nodeValue : nodes)
        {
            Node& node = RequireReference(nodeValue);
            if (StartsWith(node.Name, "rm"))
            {
                nodeName = node.Name;
                break;
            }
        }
        const DoorEntityData layerDoorData = door.Data();
        const std::int32_t layerId = layerDoorData.TargetLayerId == 255
            ? -1 : static_cast<std::int32_t>(layerDoorData.TargetLayerId);
        auto newDoor = std::make_shared<DoorEntity>(data, nodeName, _scene, door.TargetRoomId(), layerId);
        scene.AddEntity(newDoor);
        newDoor->SetConnectorInactive(true);
        door.SetLoaderDoor(newDoor);
        newDoor->SetConnectorDoor(SharedFrom(doorValue));
        if (!GameState::InRoomTransition())
        {
            newDoor->NodeRef = AddDoorPortal(doorValue);
        }
    }

    void RoomEntity::ActivateConnector(DoorEntity* doorValue)
    {
        DoorEntity& door = RequireReference(doorValue);
        assert(door.ConnectorModel() != nullptr);
        assert(door.ConnectorCollision() != nullptr);
        for (std::size_t i = 0; i < _connectorModels.size(); ++i)
        {
            const std::shared_ptr<ModelInstance> conInstValue = _connectorModels[i];
            const std::shared_ptr<CollisionInstance> conColValue
                = ManagedListAt(_roomCollision, static_cast<std::int32_t>(i + 1));
            RequireReference(conInstValue).Active = false;
            RequireReference(conColValue).Active = false;
        }
        RequireReference(door.ConnectorModel()).Active = true;
        RequireReference(door.ConnectorCollision()).Active = true;
        assert(door.LoaderDoor() != nullptr);
        auto enumerator = RequireReference(_scene).GetDoorEntities().GetEnumerator();
        while (enumerator.MoveNext())
        {
            DoorEntity& other = RequireReference(enumerator.Current());
            if (other.LoaderDoor() != nullptr)
            {
                RequireReference(other.LoaderDoor()).SetConnectorInactive(true);
            }
        }
        RequireReference(door.LoaderDoor()).SetConnectorInactive(false);
    }

    void RoomEntity::UpdateTransition()
    {
        if (GameState::TransitionState() == TransitionState::Start)
        {
            StartTransition(true);
        }
        else if (GameState::TransitionState() == TransitionState::Process)
        {
            RequireReference(_scene).InitLoadedEntity(1);
        }
        else if (GameState::TransitionState() == TransitionState::End)
        {
            EndTransition();
        }
    }

    void RoomEntity::LoadRoom(bool resume)
    {
        std::shared_ptr<PlayerEntity> player = PlayerEntity::Main();
        PlayerEntity& main = RequireReference(player);
        main.StopAllSfx();
        const Hunter hunter = main.Hunter();
        const std::int32_t recolor = main.Recolor();
        Scene& scene = RequireReference(_scene);
        if (GameState::TransitionRoomId() == -1)
        {
            GameState::TransitionRoomId(scene.RoomId());
        }
        scene.ResetFrameCount();
        Rng::SetRng2(0);
        StartTransition(false, resume);
        scene.ClearEffects();
        if (!resume)
        {
            PlayerEntity::Reset();
            PlayerEntity::Construct(_scene);
            if (Mods::Network::NetRoomChange::Rebuilding())
            {
                player = Mods::Network::NetRoomChange::RebuildPlayers(scene, hunter, recolor);
            }
            else
            {
                player = PlayerEntity::Create(hunter, recolor);
                assert(player != nullptr);
                PlayerEntity& created = RequireReference(player);
                created.SetLoadFlags(OrFlag(created.LoadFlags(), LoadFlags::SlotActive));
                created.SetLoadFlags(OrFlag(created.LoadFlags(), LoadFlags::Active));
                created.SetLoadFlags(OrFlag(created.LoadFlags(), LoadFlags::Initial));
                PlayerEntity::SetPlayerCount(UncheckedIncrement(PlayerEntity::PlayerCount()));
            }
        }
        const std::shared_ptr<RoomEntity> self = SharedFrom(this);
        assert(self != nullptr);
        ProcessTransition(std::shared_ptr<const std::atomic_bool>{}, self);
        EndTransition();
        GameState::PausePrevented(false);
        Music::TryPlayRoomMusic(
            scene.RoomId(),
            GameState::SinglePlayer()
                && ((static_cast<std::int32_t>(RequireReference(::MphRead::GameState::StorySave).BossFlags) >> (2 * scene.AreaId())) & 3) != 0
                ? 1 : 0);
        if (!resume)
        {
            scene.InsertEntity(player);
        }
        PlayerEntity& loaded = RequireReference(player);
        loaded.SetReloadInit(resume);
        loaded.Initialize();
        if (!resume)
        {
            scene.InitEntity(player);
            scene.InitEntity(loaded.Halfturret());
            if (Mods::Network::NetRoomChange::Rebuilding())
            {
                Mods::Network::NetRoomChange::AfterRebuild(scene);
            }
        }
    }

    void RoomEntity::StartTransition(bool fromDoor, bool resume)
    {
        assert(GameState::TransitionRoomId() != -1);
        GameState::TransitionState(TransitionState::Process);
        Music::UpdateEncounterMusic(-1);
        Scene& scene = RequireReference(_scene);
        auto enumerator = scene.Entities().GetEnumerator();
        while (enumerator.MoveNext())
        {
            std::shared_ptr<EntityBase> entityValue = enumerator.Current();
            EntityBase& entity = RequireReference(entityValue);
            if (entity.Type == EntityType::Room || entity.Type == EntityType::Model
                || (entity.Type == EntityType::Player && resume))
            {
                continue;
            }
            if (LoaderDoor != nullptr && entity.Type == EntityType::Door)
            {
                auto doorValue = std::dynamic_pointer_cast<DoorEntity>(entityValue);
                if (!doorValue)
                {
                    throw SceneDetail::InvalidCastException();
                }
                DoorEntity& door = *doorValue;
                if (&door == LoaderDoor || door.LoaderDoor().get() == LoaderDoor)
                {
                    scene.RemoveEntityFromMap(doorValue);
                }
                else
                {
                    scene.RemoveEntity(doorValue);
                    door.Destroy();
                }
            }
            else if (LoaderDoor != nullptr
                && ManagedAt(_keepEntities, static_cast<std::int32_t>(entity.Type)))
            {
                if ((entity.Type == EntityType::Player && &entity != PlayerEntity::Main().get())
                    || (entity.Type == EntityType::Halfturret
                        && &entity != RequireReference(PlayerEntity::Main()).Halfturret().get()))
                {
                    scene.RemoveEntity(entityValue);
                    entity.Destroy();
                }
                else if (entity.Type == EntityType::BeamProjectile)
                {
                    auto beamValue = std::dynamic_pointer_cast<BeamProjectileEntity>(entityValue);
                    if (!beamValue)
                    {
                        throw SceneDetail::InvalidCastException();
                    }
                    if (beamValue->Owner() != PlayerEntity::Main())
                    {
                        scene.RemoveEntity(beamValue);
                        beamValue->Destroy();
                    }
                }
            }
            else
            {
                scene.RemoveEntity(entityValue);
                entity.Destroy();
            }
        }
        scene.ClearNonPersistentEffects();
        Sound::Sfx::SfxMute = false;
        Sound::Sfx::ForceFieldSfxMute = 0;
        Sound::Sfx::TimedSfxMute = 0;
        Sound::Sfx::LongSfxMute = 0;
        CamSeqEntity::Current(nullptr);
        Formats::CameraSequence::Current(nullptr);
        scene.ClearMessageQueue();
        if (GameState::EscapeTimer() != -1 && GameState::EscapeState() != EscapeState::Escape)
        {
            GameState::ResetEscapeState(false);
        }
        for (std::size_t i = 0; i < PlayerEntity::Players().size(); ++i)
        {
            RequireReference(PlayerEntity::Players()[i]).ResetReferences();
        }
        scene.AreaId(Metadata::GetAreaInfo(GameState::TransitionRoomId()));
        if (fromDoor)
        {
            const std::shared_ptr<const std::atomic_bool> token = _cts;
            if (token->load())
            {
                return;
            }
            const std::shared_ptr<RoomEntity> self = SharedFrom(this);
            assert(self != nullptr);
            std::packaged_task<void()> task([self, token]()
            {
                if (token->load())
                {
                    return;
                }
                const std::lock_guard<std::recursive_mutex> gate(NativeRuntime::SceneGate());
                self->ProcessTransition(token, self);
            });
            std::thread(std::move(task)).detach();
        }
    }

    void RoomEntity::CancelTransition()
    {
        _cts->store(true);
    }

    void RoomEntity::ProcessTransition(std::shared_ptr<const std::atomic_bool> token,
        std::shared_ptr<RoomEntity> self)
    {
        assert(GameState::TransitionRoomId() != -1);
        const RoomMetadata* roomMeta = Metadata::GetRoomById(GameState::TransitionRoomId());
        assert(roomMeta != nullptr);
        std::int32_t entityLayer = -1;
        if (LoaderDoor != nullptr)
        {
            entityLayer = LoaderDoor->TargetLayerId();
        }
        else
        {
            Rng::SetRng2(Rng::Rng2StartValue);
        }
        auto setup = SceneSetup::SetUpRoom(
            GameState::Mode(), Mods::Network::NetRoomChange::RoomPlayerCount(),
            BossFlags::Unspecified, 0, entityLayer, roomMeta, self, _scene, true);
        const auto& entities = std::get<1>(setup);
        if (token != nullptr && token->load())
        {
            return;
        }
        if (GameState::SinglePlayer())
        {
            SceneSetup::InitHunterSpawns(_scene, entities, true);
        }
        if (token != nullptr && token->load())
        {
            return;
        }
        Formats::AiPersonality::LoadAll(GameState::Mode());
        if (token != nullptr && token->load())
        {
            return;
        }
        SetNodeData(SceneSetup::LoadNodeData(
            RequireReference(roomMeta).NodePath, RequireReference(roomMeta).Id,
            GameState::Mode(), entities, RequireReference(roomMeta).FirstHunt));
        PlayerEntity::PlayerAiData::InitializeGlobals();
        if (token != nullptr && token->load())
        {
            return;
        }
        for (const std::shared_ptr<EntityBase>& entityValue : RequireReference(entities))
        {
            EntityBase& entity = RequireReference(entityValue);
            entity.Initialized = false;
            Scene& scene = RequireReference(_scene);
            scene.InsertEntity(entityValue);
            scene.LoadedEntities().Enqueue(entityValue);
            if (token != nullptr && token->load())
            {
                return;
            }
        }
        if (LoaderDoor == nullptr)
        {
            RequireReference(_scene).InitLoadedEntity(-1);
        }
        else
        {
            while (RequireReference(_scene).LoadedEntities().Count() != 0)
            {
                // The worker (the only caller passing a token) holds the scene
                // gate; the main thread cannot drain the queue until it lets go.
                if (token != nullptr)
                {
                    NativeRuntime::SceneGate().unlock();
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                if (token != nullptr)
                {
                    NativeRuntime::SceneGate().lock();
                }
                if (token != nullptr && token->load())
                {
                    return;
                }
            }
        }
        auto doorEnumerator = RequireReference(_scene).GetDoorEntities().GetEnumerator();
        while (doorEnumerator.MoveNext())
        {
            DoorEntity& door = RequireReference(doorEnumerator.Current());
            if (door.Data().ConnectorId == 255 || door.Portal() != nullptr)
            {
                continue;
            }
            std::shared_ptr<DoorEntity> loader = door.LoaderDoor();
            assert(loader != nullptr);
            DoorEntity& loaderRef = RequireReference(loader);
            const Formats::Culling::NodeRef portalRef = AddDoorPortal(&door);
            loaderRef.NodeRef = portalRef;
            if (token != nullptr && token->load())
            {
                return;
            }
        }
        GameState::TransitionState(TransitionState::End);
    }

    void RoomEntity::EndTransition()
    {
        const RoomMetadata* roomMeta = Metadata::GetRoomById(GameState::TransitionRoomId());
        assert(roomMeta != nullptr);
        const std::shared_ptr<ModelInstance> instValue = ManagedListAt(_models.Items(), 0);
        Scene& scene = RequireReference(_scene);
        ModelInstance& inst = RequireReference(instValue);
        scene.LoadModel(inst.Model(), true);
        inst.SetAnimation(0);
        scene.SetRoomValues(RequireReference(roomMeta));
        for (std::int32_t i = 0; i < static_cast<std::int32_t>(_connectorModels.size()); ++i)
        {
            const std::shared_ptr<ModelInstance> conInstValue = ManagedListAt(_connectorModels, i);
            const std::shared_ptr<CollisionInstance> conColValue = ManagedListAt(_roomCollision, i + 1);
            (void)conColValue;
            ModelInstance& conInst = RequireReference(conInstValue);
            if (conInst.NodeAnimIgnoreRoot)
            {
                _connectorModels.erase(_connectorModels.begin() + i);
                _roomCollision.erase(_roomCollision.begin() + i + 1);
                --i;
            }
            else
            {
                conInst.NodeAnimIgnoreRoot = true;
            }
        }
        if (!_roomCollision.empty())
        {
            RequireReference(_roomCollision[0]).Active = true;
        }
        Vector3 offset = Vector3::Zero;
        std::shared_ptr<DoorEntity> prevConnector{};
        std::shared_ptr<DoorEntity> newLoader{};
        std::shared_ptr<DoorEntity> loaderKeepAlive{};
        Formats::Culling::NodeRef nodeRef = Formats::Culling::NodeRef::None;
        auto enumerator = scene.Entities().GetEnumerator();
        while (enumerator.MoveNext())
        {
            std::shared_ptr<EntityBase> entityValue = enumerator.Current();
            EntityBase& entity = RequireReference(entityValue);
            entity.Initialized = true;
            if (LoaderDoor != nullptr && entity.Type == EntityType::Door)
            {
                auto doorValue = std::dynamic_pointer_cast<DoorEntity>(entityValue);
                if (!doorValue)
                {
                    throw SceneDetail::InvalidCastException();
                }
                DoorEntity& door = *doorValue;
                const std::shared_ptr<DoorEntity> doorLoader = door.LoaderDoor();
                if (&door == LoaderDoor || doorLoader.get() == LoaderDoor)
                {
                    if (&door == LoaderDoor)
                    {
                        loaderKeepAlive = doorValue;
                    }
                    else if (doorLoader.get() == LoaderDoor)
                    {
                        loaderKeepAlive = doorLoader;
                    }
                    if (doorLoader.get() == LoaderDoor)
                    {
                        prevConnector = doorValue;
                    }
                    scene.RemoveEntity(doorValue);
                    door.Destroy();
                }
                else if (door.Data().OutConnectorId == RequireReference(LoaderDoor).Data().OutLoaderId)
                {
                    assert(door.Portal() != nullptr);
                    door.SetFlags(OrFlag(door.Flags(), DoorFlags::ShotOpen));
                    door.SetFlags(AndNotFlag(door.Flags(), DoorFlags::Locked));
                    door.SetAnimationFrame(RequireReference(LoaderDoor).GetAnimationFrame());
                    ActivateConnector(&door);
                    offset = Subtract(static_cast<Vector3>(door.Position),
                        static_cast<Vector3>(RequireReference(LoaderDoor).Position));
                    nodeRef = RequireReference(door.Portal()).NodeRef2;
                    newLoader = door.LoaderDoor();
                    assert(newLoader != nullptr);
                    if (prevConnector == nullptr)
                    {
                        prevConnector = RequireReference(LoaderDoor).ConnectorDoor();
                    }
                    assert(prevConnector != nullptr);
                    DoorEntity& newLoaderRef = RequireReference(newLoader);
                    DoorEntity& prevConnectorRef = RequireReference(prevConnector);
                    newLoaderRef.SetAnimationFrame(prevConnectorRef.GetAnimationFrame());
                    if (TestFlag(prevConnectorRef.Flags(), DoorFlags::ShotOpen)
                        && !TestFlag(newLoaderRef.Flags(), DoorFlags::Locked))
                    {
                        newLoaderRef.SetFlags(OrFlag(newLoaderRef.Flags(), DoorFlags::ShotOpen));
                    }
                }
            }
        }
        if (LoaderDoor != nullptr)
        {
            assert(nodeRef != Formats::Culling::NodeRef::None);
            auto reposition = scene.Entities().GetEnumerator();
            while (reposition.MoveNext())
            {
                std::shared_ptr<EntityBase> entityValue = reposition.Current();
                EntityBase& entity = RequireReference(entityValue);
                if (entity.Type == EntityType::Player)
                {
                    auto player = std::dynamic_pointer_cast<PlayerEntity>(entityValue);
                    if (!player) throw SceneDetail::InvalidCastException();
                    player->Reposition(offset, nodeRef);
                }
                else if (entity.Type == EntityType::Bomb)
                {
                    auto bomb = std::dynamic_pointer_cast<BombEntity>(entityValue);
                    if (!bomb) throw SceneDetail::InvalidCastException();
                    bomb->Reposition(offset);
                }
                else if (entity.Type == EntityType::BeamEffect)
                {
                    auto effect = std::dynamic_pointer_cast<BeamEffectEntity>(entityValue);
                    if (!effect) throw SceneDetail::InvalidCastException();
                    effect->Reposition(offset);
                }
            }
        }
        for (const std::shared_ptr<PlayerEntity>& playerValue : PlayerEntity::Players())
        {
            PlayerEntity& player = RequireReference(playerValue);
            if (player.IsBot())
            {
                RequireReference(player.AiData).InitializeAtLoad();
            }
        }
        if (newLoader != nullptr && newLoader->ConnectorDoor() != nullptr)
        {
            DoorEntity& targetDoor = RequireReference(newLoader->ConnectorDoor());
            auto spawners = scene.GetEnemySpawnEntities().GetEnumerator();
            while (spawners.MoveNext())
            {
                EnemySpawnEntity& spawner = RequireReference(spawners.Current());
                if (spawner.Data.EnemyType != EnemyType::Cretaphid
                    && spawner.Data.EnemyType != EnemyType::Slench)
                {
                    continue;
                }
                if (RequireReference(::MphRead::GameState::StorySave).GetRoomState(scene.RoomId(), spawner.Id) != 0)
                {
                    Movie movieId;
                    if (spawner.Data.EnemyType == EnemyType::Cretaphid)
                    {
                        switch (spawner.Data.Fields.S05().EnemySubtype)
                        {
                        case 3: movieId = Movie::CretaphidArcterra2Intro; break;
                        case 2: movieId = Movie::CretaphidAlinso2Intro; break;
                        case 1: movieId = Movie::CretaphidVDO1Intro; break;
                        default: movieId = Movie::CretaphidCA1Intro; break;
                        }
                    }
                    else
                    {
                        switch (scene.RoomId())
                        {
                        case 76: movieId = Movie::SlenchVDO2Intro; break;
                        case 64: movieId = Movie::SlenchCA2Intro; break;
                        case 82: movieId = Movie::SlenchArcterra1Intro; break;
                        default: movieId = Movie::SlenchAlinos1Intro; break;
                        }
                    }
                    const float y = Fixed::ToFloat(-RequireReference(PlayerEntity::Main()).Values().MinPickupHeight);
                    const Vector3 newPosition = Add(
                        Add(static_cast<Vector3>(targetDoor.Position), ScaleVector(targetDoor.FacingVector(), 0.75F)),
                        Vector3(0.0F, y, 0.0F));
                    GameState::PausePrevented(true);
                    scene.StartMovie(movieId, FadeType::FadeOutInBlack, 0,
                        FadeType::FadeOutInBlack, 5.0F / 30.0F,
                        newPosition, targetDoor.FacingVector());
                }
                break;
            }
        }
        if (GameState::GetAreaState(scene.AreaId()) == AreaState::Clear && PlayerEntity::PlayerCount() > 1)
        {
            auto doors = scene.GetDoorEntities().GetEnumerator();
            while (doors.MoveNext())
            {
                std::shared_ptr<DoorEntity> doorValue = doors.Current();
                DoorEntity& entity = RequireReference(doorValue);
                if (entity.Type != EntityType::Door)
                {
                    continue;
                }
                std::shared_ptr<DoorEntity> door = doorValue;
                if (RequireReference(door).Id != -1 && RequireReference(door).Data().ConnectorId != 255)
                {
                    const std::shared_ptr<DoorEntity> doorLoader = RequireReference(door).LoaderDoor();
                    if (doorLoader != nullptr && doorLoader == newLoader)
                    {
                        door = doorLoader;
                    }
                    DoorEntity& target = RequireReference(door);
                    target.Lock(false);
                    target.SetFlags(OrFlag(target.Flags(), DoorFlags::ShowLock));
                }
            }
        }
        RequireReference(::MphRead::GameState::StorySave).SetVisitedRoom(_roomId);
        if (_unloadModel != nullptr)
        {
            scene.UnloadModel(_unloadModel);
        }
        _unloadModel.reset();
        LoaderDoor = nullptr;
        GameState::TransitionState(TransitionState::None);
        GameState::TransitionRoomId(-1);
    }

    void RoomEntity::GetCollisionDrawInfo()
    {
        for (const std::shared_ptr<CollisionInstance>& collisionValue : _roomCollision)
        {
            CollisionInstance& inst = RequireReference(collisionValue);
            Formats::Collision::CollisionInfo& info = RequireReference(inst.Info);
            info.GetDrawInfo(info.Points, inst.Translation, Type, _scene);
        }
    }

    std::shared_ptr<RoomPartVisInfo> RoomEntity::GetPartVisInfo(Formats::Culling::NodeRef nodeRef)
    {
        std::shared_ptr<RoomPartVisInfo>& value = ManagedAt(_partVisInfo, nodeRef.PartIndex);
        RoomPartVisInfo& visInfo = RequireReference(value);
        if (!ManagedAt(_activeRoomParts, nodeRef.PartIndex))
        {
            visInfo.NodeRef = nodeRef;
            visInfo.ViewMinX = 1.0F;
            visInfo.ViewMaxX = 0.0F;
            visInfo.ViewMinY = 1.0F;
            visInfo.ViewMaxY = 0.0F;
            visInfo.Next = _partVisInfoHead;
            _partVisInfoHead = value;
        }
        return value;
    }

    std::shared_ptr<RoomFrustumItem> RoomEntity::GetRoomFrustumItem()
    {
        assert(_roomFrustumIndex != _roomPartMax);
        return ManagedAt(_roomFrustumItems, _roomFrustumIndex);
    }

    void RoomEntity::ClearRoomPartState()
    {
        for (std::int32_t i = 0; i < _roomPartMax; ++i)
        {
            _activeRoomParts[static_cast<std::size_t>(i)] = false;
            _roomFrustumLinks[static_cast<std::size_t>(i)].reset();
            _audibleRoomParts[static_cast<std::size_t>(i)] = false;
        }
        _visNodeRefRecursionDepth = 0;
        _audNodeRefRecursionDepth = 0;
        _roomFrustumIndex = 0;
        _partVisInfoHead.reset();
    }

    bool RoomEntity::ModCanPlace(Formats::Culling::NodeRef nodeRef) const
    {
        if (nodeRef.PartIndex < 0 || nodeRef.PartIndex >= _roomPartMax
            || nodeRef.NodeIndex < 0 || nodeRef.ModelIndex < 0)
        {
            return false;
        }
        std::shared_ptr<ModelInstance> partInstValue{};
        if (nodeRef.ModelIndex == 0)
        {
            if (_models.Size() == 0) return false;
            partInstValue = ManagedListAt(_models.Items(), 0);
        }
        else
        {
            if (nodeRef.ModelIndex > static_cast<std::int32_t>(_connectorModels.size())
                || nodeRef.ModelIndex >= static_cast<std::int32_t>(_roomCollision.size()))
            {
                return false;
            }
            partInstValue = ManagedListAt(_connectorModels, nodeRef.ModelIndex - 1);
        }
        ModelInstance& partInst = RequireReference(partInstValue);
        Model& partModel = RequireReference((partInst).Model());
        const auto& partNodes = RequireReference(partModel.Nodes);
        if (nodeRef.NodeIndex >= static_cast<std::int32_t>(partNodes.size()))
        {
            return false;
        }
        if (nodeRef.RoomName.HasValue() && !ModKnowsRoom(*nodeRef.RoomName))
        {
            return false;
        }
        return true;
    }

    bool RoomEntity::ModKnowsRoom(const std::string& name) const
    {
        if (name == Meta().Name) return true;
        for (const std::shared_ptr<CollisionInstance>& collisionValue : _roomCollision)
        {
            const CollisionInstance& collision = RequireReference(collisionValue);
            if (collision.ConnectorName.has_value() && *collision.ConnectorName == name)
            {
                return true;
            }
        }
        return false;
    }

    void RoomEntity::ModNoteStaleNodeRef(Formats::Culling::NodeRef nodeRef, const std::string& where)
    {
        if (_modReportedStaleNodeRef) return;
        _modReportedStaleNodeRef = true;
        const std::string roomName = nodeRef.RoomName.HasValue() ? *nodeRef.RoomName : "?";
        const std::string message = "[room] " + where
            + ": node ref part " + std::to_string(nodeRef.PartIndex)
            + " node " + std::to_string(nodeRef.NodeIndex)
            + " model " + std::to_string(nodeRef.ModelIndex)
            + " from '" + roomName + "' does not belong to " + Meta().Name
            + "; drawing every part instead";
        std::cout << message << '\n';
        Mods::Network::NetLog::Event(message);
    }

    void RoomEntity::UpdateRoomParts()
    {
        const Formats::Culling::NodeRef curNodeRef = RequireReference(RequireReference(PlayerEntity::Main()).CameraInfo()).NodeRef;
        Scene& scene = RequireReference(_scene);
        if (scene.CameraMode() != CameraMode::Player || curNodeRef.PartIndex == -1) return;
        if (!ModCanPlace(curNodeRef))
        {
            ModNoteStaleNodeRef(curNodeRef, "camera");
            return;
        }
        if (GameState::Multiplayer() && GameState::MatchState() != MatchState::InProgress) return;
        if (!PartCouldContain(curNodeRef.PartIndex, scene.CameraPosition(), _partBoundsMargin)) return;
        assert(curNodeRef.NodeIndex != -1);
        RoomPartVisInfo& curVisInfo = RequireReference(GetPartVisInfo(curNodeRef));
        curVisInfo.ViewMinX = 0.0F;
        curVisInfo.ViewMaxX = 1.0F;
        curVisInfo.ViewMinY = 0.0F;
        curVisInfo.ViewMaxY = 1.0F;
        ManagedAt(_activeRoomParts, curNodeRef.PartIndex) = true;
        std::shared_ptr<RoomFrustumItem> curFrustumValue = GetRoomFrustumItem();
        _roomFrustumIndex = UncheckedIncrement(_roomFrustumIndex);
        RoomFrustumItem& curRoomFrustum = RequireReference(curFrustumValue);
        FrustumInfo& destInfo = RequireReference(curRoomFrustum.Info);
        destInfo.Count = scene.FrustumInfo().Count;
        destInfo.Index = scene.FrustumInfo().Index;
        const auto& destPlanes = RequireReference(destInfo.Planes);
        for (std::int32_t i = 0; i < static_cast<std::int32_t>(destPlanes.size()); ++i)
        {
            FrustumPlane& target = ManagedAt(RequireReference(destInfo.Planes), i);
            const FrustumPlane value = ManagedAt(RequireReference(scene.FrustumInfo().Planes), i);
            target = value;
        }
        curRoomFrustum.NodeRef = curNodeRef;
        curRoomFrustum.Next = ManagedAt(_roomFrustumLinks, curNodeRef.PartIndex);
        ManagedAt(_roomFrustumLinks, curNodeRef.PartIndex) = curFrustumValue;
        FindVisibleRoomParts(curFrustumValue, curNodeRef);
        FindAudibleRoomParts(curNodeRef, curNodeRef);
    }

    void RoomEntity::FindVisibleRoomParts(
        const std::shared_ptr<RoomFrustumItem>& frustumValue, Formats::Culling::NodeRef mainNodeRef)
    {
        bool otherSide = false;
        for (const std::shared_ptr<Portal>& portalValue : _portals)
        {
            Portal& portal = RequireReference(portalValue);
            if (!portal.Active) continue;
            RoomFrustumItem& frustumItem = RequireReference(frustumValue);
            if (portal.NodeRef1 == frustumItem.NodeRef) otherSide = false;
            else if (portal.NodeRef2 == frustumItem.NodeRef) otherSide = true;
            else continue;
            assert(portal.NodeRef1 != Formats::Culling::NodeRef::None);
            assert(portal.NodeRef2 != Formats::Culling::NodeRef::None);
            assert(portal.NodeRef1 != portal.NodeRef2);
            Scene& scene = RequireReference(_scene);
            float minX = 1.0F;
            float maxX = 0.0F;
            float minY = 1.0F;
            float maxY = 0.0F;
            const float dist = GetDistanceToPortal(scene.CameraPosition(), portal.Plane, otherSide);
            if (dist < 0.0F) continue;
            if (portal.IsForceField && GetPortalAlpha(portal.Position, scene.CameraPosition()) == 1.0F) continue;
            bool adjacent = false;
            if (dist < 0.5F)
            {
                adjacent = true;
                const auto& portalPlanes = RequireReference(portal.Planes);
                for (const Vector4& plane : portalPlanes)
                {
                    if (Vector3::Dot(scene.CameraPosition(), plane.Xyz()) - plane.W < Fixed::ToFloat(-4224))
                    {
                        adjacent = false;
                        break;
                    }
                }
            }

            std::int32_t v28;
            std::shared_ptr<RoomFrustumItem> nextValue = GetRoomFrustumItem();
            if (adjacent)
            {
                minX = 0.0F;
                maxX = 1.0F;
                minY = 0.0F;
                maxY = 1.0F;
                v28 = 4;
                RoomFrustumItem& nextFrustumItem = RequireReference(nextValue);
                FrustumInfo& nextInfo = RequireReference(nextFrustumItem.Info);
                FrustumInfo& sourceInfo = RequireReference(frustumItem.Info);
                nextInfo.Index = sourceInfo.Index;
                nextInfo.Count = sourceInfo.Count;
                for (std::int32_t j = 0; j < sourceInfo.Count; ++j)
                {
                    FrustumPlane& target = ManagedAt(RequireReference(nextInfo.Planes), j);
                    const FrustumPlane value = ManagedAt(RequireReference(sourceInfo.Planes), j);
                    target = value;
                }
            }
            else
            {
                const auto& points = RequireReference(portal.Points);
                for (std::size_t j = 0; j < points.size(); ++j)
                {
                    ManagedAt(_startPointList, static_cast<std::int32_t>(j)) = points[j];
                }
                FrustumInfo& sourceInfo = RequireReference(frustumItem.Info);
                v28 = Func21180A8(sourceInfo, _startPointList.data(),
                    static_cast<std::int32_t>(points.size()), _destPointList.data());
                if (v28 >= 3)
                {
                    assert(sourceInfo.Index + v28 <= 10);
                    const std::int32_t index = sourceInfo.Index;
                    RoomFrustumItem& nextFrustumItem = RequireReference(nextValue);
                    FrustumInfo& nextInfo = RequireReference(nextFrustumItem.Info);
                    nextInfo.Index = index;
                    for (std::int32_t j = 0; j < sourceInfo.Index; ++j)
                    {
                        FrustumPlane& target = ManagedAt(RequireReference(nextInfo.Planes), j);
                        const FrustumPlane value = ManagedAt(RequireReference(sourceInfo.Planes), j);
                        target = value;
                    }
                    nextInfo.Count = index;
                    for (std::int32_t j = 0; j < v28; ++j)
                    {
                        const Vector3 point1 = ManagedAt(_destPointList, j);
                        const Vector3 point2 = ManagedAt(_destPointList, j == v28 - 1 ? 0 : j + 1);
                        if (std::fabs(point1.X - point2.X) >= 1.0F / 4096.0F
                            || std::fabs(point1.Y - point2.Y) >= 1.0F / 4096.0F
                            || std::fabs(point1.Z - point2.Z) >= 1.0F / 4096.0F)
                        {
                            Vector3 normal;
                            const Vector3 vec1 = Subtract(point1, scene.CameraPosition());
                            const Vector3 vec2 = Subtract(point2, scene.CameraPosition());
                            normal = otherSide
                                ? Vector3::Cross(vec1, vec2).Normalized()
                                : Vector3::Cross(vec2, vec1).Normalized();
                            const Vector4 plane(normal, Vector3::Dot(normal, scene.CameraPosition()));
                            FrustumPlane& target
                                = ManagedAt(RequireReference(nextInfo.Planes), index + j);
                            const FrustumPlane value = Scene::SetBoundsIndices(plane);
                            target = value;
                            Vector3 destPoint = ManagedAt(_startPointList, j);
                            if (Func2117F84(point1, destPoint) >= 0.0F)
                            {
                                minX = MathMin(minX, destPoint.X);
                                maxX = MathMax(maxX, destPoint.X);
                                minY = MathMin(minY, destPoint.Y);
                                maxY = MathMax(maxY, destPoint.Y);
                            }
                            nextInfo.Count = UncheckedIncrement(nextInfo.Count);
                        }
                    }
                }
            }

            if (v28 >= 3)
            {
                minX = MathMax(minX, 0.0F);
                maxX = MathMin(maxX, 1.0F);
                minY = MathMax(minY, 0.0F);
                maxY = MathMin(maxY, 1.0F);
                if (minX < maxX - 1.0F / 800.0F && minY < maxY - 1.0F / 600.0F)
                {
                    const Formats::Culling::NodeRef nextNodeRef = otherSide ? portal.NodeRef1 : portal.NodeRef2;
                    if (nextNodeRef.PartIndex == mainNodeRef.PartIndex || _visNodeRefRecursionDepth < 6)
                    {
                        _visNodeRefRecursionDepth = UncheckedIncrement(_visNodeRefRecursionDepth);
                        RoomPartVisInfo& nextVisInfo = RequireReference(GetPartVisInfo(nextNodeRef));
                        nextVisInfo.ViewMinX = MathMin(nextVisInfo.ViewMinX, minX);
                        nextVisInfo.ViewMaxX = MathMax(nextVisInfo.ViewMaxX, maxX);
                        nextVisInfo.ViewMinY = MathMin(nextVisInfo.ViewMinY, minY);
                        nextVisInfo.ViewMaxY = MathMax(nextVisInfo.ViewMaxY, maxY);
                        ManagedAt(_activeRoomParts, nextNodeRef.PartIndex) = true;
                        _roomFrustumIndex = UncheckedIncrement(_roomFrustumIndex);
                        RoomFrustumItem& nextFrustumItem = RequireReference(nextValue);
                        nextFrustumItem.NodeRef = nextNodeRef;
                        nextFrustumItem.Next = ManagedAt(_roomFrustumLinks, nextNodeRef.PartIndex);
                        ManagedAt(_roomFrustumLinks, nextNodeRef.PartIndex) = nextValue;
                        FindVisibleRoomParts(nextValue, mainNodeRef);
                        --_visNodeRefRecursionDepth;
                    }
                }
            }
        }
    }

    float RoomEntity::Func2117F84(Vector3 point, Vector3& dest) const
    {
        Scene& scene = RequireReference(_scene);
        const Matrix4 matrix = scene.ViewMatrix() * scene.PerspectiveMatrix();
        const float v4 = point.X * matrix.M14 + point.Y * matrix.M24
            + point.Z * matrix.M34 + matrix.M44;
        if (v4 <= 0.0F) return v4;
        dest = Matrix::Vec3MultMtx4(point, matrix);
        dest.X = (dest.X * 400.0F / v4 + 400.0F) / 800.0F;
        dest.Y = (dest.Y * 300.0F / v4 + 300.0F) / 600.0F;
        dest.Z /= v4;
        return 1.0F / v4;
    }

    std::int32_t RoomEntity::Func21180A8(
        const FrustumInfo& frustumInfo, Vector3* pointList,
        std::int32_t pointCount, Vector3* destList) const
    {
        assert(frustumInfo.Count > 0);
        std::array<Vector3, 14> temp1{};
        std::array<Vector3, 14> temp2{};
        for (std::int32_t i = 0; i < frustumInfo.Count; ++i)
        {
            std::int32_t newPointCount = 0;
            Vector3* newList = i == frustumInfo.Count - 1
                ? destList : (i % 2 == 0 ? temp1.data() : temp2.data());
            const Vector4 plane = ManagedAt(RequireReference(frustumInfo.Planes), i).Plane;
            float dist1 = Vector3::Dot(PointAt14(pointList, 0), plane.Xyz()) - plane.W;
            bool v5 = dist1 >= 0.0F;
            assert(pointCount > 0);
            for (std::int32_t j = 0; j < pointCount; ++j)
            {
                const Vector3 point1 = PointAt14(pointList, j);
                const Vector3 point2 = PointAt14(pointList, j == pointCount - 1 ? 0 : j + 1);
                if (v5)
                {
                    const std::int32_t target = newPointCount;
                    newPointCount = UncheckedIncrement(newPointCount);
                    PointAt14(newList, target) = point1;
                }
                const float dist2 = Vector3::Dot(point2, plane.Xyz()) - plane.W;
                const bool v6 = dist2 >= 0.0F;
                if (v5 != v6)
                {
                    const float div = -dist1 / (dist2 - dist1);
                    const std::int32_t target = newPointCount;
                    newPointCount = UncheckedIncrement(newPointCount);
                    PointAt14(newList, target) = Add(point1, ScaleVector(Subtract(point2, point1), div));
                }
                dist1 = dist2;
                v5 = v6;
            }
            if (newPointCount == 0) return 0;
            pointList = newList;
            pointCount = newPointCount;
        }
        return pointCount;
    }

    float RoomEntity::GetDistanceToPortal(Vector3 pos, Vector4 plane, bool otherSide) const noexcept
    {
        float dist = Vector3::Dot(pos, plane.Xyz()) - plane.W;
        if (otherSide) dist *= -1.0F;
        return dist;
    }

    void RoomEntity::FindAudibleRoomParts(Formats::Culling::NodeRef nodeRef, Formats::Culling::NodeRef mainNodeRef)
    {
        ManagedAt(_audibleRoomParts, nodeRef.PartIndex) = true;
        bool otherSide = false;
        for (const std::shared_ptr<Portal>& portalValue : _portals)
        {
            Portal& portal = RequireReference(portalValue);
            if (!portal.Active) continue;
            if (portal.NodeRef1 == nodeRef) otherSide = false;
            else if (portal.NodeRef2 == nodeRef) otherSide = true;
            else continue;
            if (portal.IsForceField)
            {
                Scene& scene = RequireReference(_scene);
                if (GetPortalAlpha(portal.Position, scene.CameraPosition()) == 1.0F)
                {
                    continue;
                }
            }
            assert(portal.NodeRef1 != Formats::Culling::NodeRef::None);
            assert(portal.NodeRef2 != Formats::Culling::NodeRef::None);
            assert(portal.NodeRef1 != portal.NodeRef2);
            const float dist = GetDistanceToPortal(
                RequireReference(_scene).CameraPosition(), portal.Plane, otherSide);
            if (dist > Fixed::ToFloat(100000) || dist < Fixed::ToFloat(-100000)) continue;
            const Formats::Culling::NodeRef nextNodeRef = otherSide ? portal.NodeRef1 : portal.NodeRef2;
            if (nextNodeRef.PartIndex == mainNodeRef.PartIndex || _audNodeRefRecursionDepth < 2)
            {
                _audNodeRefRecursionDepth = UncheckedIncrement(_audNodeRefRecursionDepth);
                FindAudibleRoomParts(nextNodeRef, mainNodeRef);
                --_audNodeRefRecursionDepth;
            }
        }
    }

    Formats::Culling::NodeRef RoomEntity::GetNodeRefByName(const std::string& nodeName) const
    {
        ModelInstance& inst = RequireReference(ManagedListAt(_models.Items(), 0));
        std::shared_ptr<Model> modelValue = inst.Model();
        Model& model = RequireReference(modelValue);
        const auto& nodes = RequireReference(model.Nodes);
        for (const std::shared_ptr<Node>& nodeValue : nodes)
        {
            Node& node = RequireReference(nodeValue);
            if (node.Name == nodeName)
            {
                assert(node.RoomPartId >= 0);
                assert(node.ChildIndex != -1);
                return Formats::Culling::NodeRef(Meta().Name, node.RoomPartId, node.ChildIndex, 0);
            }
        }
        return Formats::Culling::NodeRef::None;
    }

    void RoomEntity::EnsurePartBounds()
    {
        if (_partBoundsBuiltFor == _nextRoomPartId) return;
        _partBoundsBuiltFor = _nextRoomPartId;
        _partBoundsMin.clear();
        _partBoundsMax.clear();
        for (std::int32_t i = 0; i < _nextRoomPartId; ++i)
        {
            _partBoundsMin.emplace_back(std::numeric_limits<float>::max(),
                std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
            _partBoundsMax.emplace_back(std::numeric_limits<float>::lowest(),
                std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest());
        }
        if (_models.Size() > 0)
        {
            AddPartBounds(RequireReference(ManagedListAt(_models.Items(), 0)), Vector3::Zero);
        }
        for (std::size_t i = 0; i < _connectorModels.size(); ++i)
        {
            const Vector3 offset = i + 1 < _roomCollision.size()
                ? RequireReference(_roomCollision[i + 1]).Translation : Vector3::Zero;
            AddPartBounds(RequireReference(_connectorModels[i]), offset);
        }
    }

    void RoomEntity::AddPartBounds(ModelInstance& inst, Vector3 offset)
    {
        const auto& nodes = RequireReference(RequireReference((inst).Model()).Nodes);
        for (const std::shared_ptr<Node>& pnodeValue : nodes)
        {
            Node& pnode = RequireReference(pnodeValue);
            const std::int32_t part = pnode.RoomPartId;
            if (part < 0 || part >= static_cast<std::int32_t>(_partBoundsMin.size())) continue;
            std::int32_t nodeIndex = pnode.ChildIndex;
            while (nodeIndex != -1)
            {
                Node& node = RequireReference(ManagedListAt(nodes, nodeIndex));
                if (node.MeshCount > 0)
                {
                    ManagedArray<float>& bounds = RequireReference(node.Bounds);
                    const Vector3 min = Add(Vector3(bounds[0], bounds[1], bounds[2]), offset);
                    const Vector3 max = Add(Vector3(bounds[3], bounds[4], bounds[5]), offset);
                    _partBoundsMin[static_cast<std::size_t>(part)]
                        = ComponentMin(_partBoundsMin[static_cast<std::size_t>(part)], min);
                    _partBoundsMax[static_cast<std::size_t>(part)]
                        = ComponentMax(_partBoundsMax[static_cast<std::size_t>(part)], max);
                }
                nodeIndex = node.NextIndex;
            }
        }
    }

    float RoomEntity::PartBoundsDepth(std::int32_t partIndex, Vector3 position)
    {
        EnsurePartBounds();
        if (partIndex < 0 || partIndex >= static_cast<std::int32_t>(_partBoundsMin.size()))
        {
            return std::numeric_limits<float>::max();
        }
        const Vector3 min = _partBoundsMin[static_cast<std::size_t>(partIndex)];
        const Vector3 max = _partBoundsMax[static_cast<std::size_t>(partIndex)];
        if (min.X > max.X) return std::numeric_limits<float>::max();
        float depth = MathMin(position.X - min.X, max.X - position.X);
        depth = MathMin(depth, MathMin(position.Y - min.Y, max.Y - position.Y));
        depth = MathMin(depth, MathMin(position.Z - min.Z, max.Z - position.Z));
        return depth;
    }

    bool RoomEntity::PartCouldContain(std::int32_t partIndex, Vector3 position, float margin)
    {
        const float depth = PartBoundsDepth(partIndex, position);
        return depth == std::numeric_limits<float>::max() || depth > -margin;
    }

    Formats::Culling::NodeRef RoomEntity::GetNodeRefByPosition(Vector3 position)
    {
        Formats::Culling::NodeRef best = Formats::Culling::NodeRef::None;
        float bestDepth = 0.0F;
        for (std::int32_t i = 0; i < static_cast<std::int32_t>(_portalSides.size()); ++i)
        {
            Formats::Culling::NodeRef result = Formats::Culling::NodeRef::None;
            bool allInside = true;
            const auto& partSides = ManagedListAt(_portalSides, i);
            for (const auto& side : partSides)
            {
                Portal& portal = RequireReference(side.first);
                float dist = Vector3::Dot(position, portal.Plane.Xyz()) - portal.Plane.W;
                if (side.second) dist *= -1.0F;
                if (dist < 0.0F)
                {
                    allInside = false;
                    break;
                }
                result = side.second ? portal.NodeRef2 : portal.NodeRef1;
            }
            if (!allInside || result == Formats::Culling::NodeRef::None) continue;
            const float depth = PartBoundsDepth(result.PartIndex, position);
            if (depth == std::numeric_limits<float>::max())
            {
                if (best == Formats::Culling::NodeRef::None) best = result;
                continue;
            }
            if (depth <= -_partBoundsMargin) continue;
            if (best == Formats::Culling::NodeRef::None || depth > bestDepth)
            {
                best = result;
                bestDepth = depth;
            }
        }
        return best;
    }

    Formats::Culling::NodeRef RoomEntity::UpdateNodeRef(Formats::Culling::NodeRef current, Vector3 prevPos, Vector3 curPos) const
    {
        assert(current.PartIndex != -1);
        for (const std::shared_ptr<Portal>& portalValue : _portals)
        {
            Portal& portal = RequireReference(portalValue);
            if (!portal.Active) continue;
            if (portal.NodeRef1.PartIndex == current.PartIndex
                && CollisionDetection::CheckPortBetweenPoints(&portal, prevPos, curPos, false))
            {
                return portal.NodeRef2;
            }
            if (portal.NodeRef2.PartIndex == current.PartIndex
                && CollisionDetection::CheckPortBetweenPoints(&portal, prevPos, curPos, true))
            {
                return portal.NodeRef1;
            }
        }
        return current;
    }

    bool RoomEntity::IsNodeRefAudible(Formats::Culling::NodeRef nodeRef) const
    {
        if (nodeRef.PartIndex == -1) return true;
        return ManagedAt(_audibleRoomParts, nodeRef.PartIndex);
    }

    bool RoomEntity::IsNodeRefVisible(Formats::Culling::NodeRef nodeRef) const
    {
        if (_partVisInfoHead == nullptr) return true;
        if (RequireReference(_scene).ShowAllNodes()) return true;
        if (nodeRef.PartIndex == -1) return false;
        return ManagedAt(_activeRoomParts, nodeRef.PartIndex);
    }

    void RoomEntity::GetDrawInfo()
    {
        if (!Hidden)
        {
            for (std::size_t i = 0; i < _connectorModels.size(); ++i)
            {
                ModelInstance& conInst = RequireReference(_connectorModels[i]);
                if (!conInst.Active) continue;
                Scene& scene = RequireReference(_scene);
                scene.UpdateMaterials(conInst.Model(), 0);
                if (GameState::InRoomTransition() || _partVisInfoHead == nullptr || scene.ShowAllNodes())
                {
                    Model& conModel = RequireReference((conInst).Model());
                    Matrix4 transform = CreateScale(conModel.Scale);
                    const Vector3 translation
                        = RequireReference(ManagedListAt(_roomCollision, static_cast<std::int32_t>(i + 1))).Translation;
                    transform.M41 = translation.X;
                    transform.M42 = translation.Y;
                    transform.M43 = translation.Z;
                    const auto& nodes = RequireReference(conModel.Nodes);
                    for (const std::shared_ptr<Node>& nodeValue : nodes)
                    {
                        RequireReference(nodeValue).Animation = transform;
                    }
                    DrawAllNodes(conInst, true);
                }
            }
            if (!GameState::InRoomTransition())
            {
                ModelInstance& inst = RequireReference(ManagedListAt(_models.Items(), 0));
                UpdateTransforms(inst, 0);
                Scene& scene = RequireReference(_scene);
                // A translated Quake 3 level's doors, bobbing platforms and
                // the rest, where they are now (Q3Movers).
                if (Mods::MapGen::CustomRooms::Q3StepHeight(_roomId) > 0.0F)
                {
                    std::vector<Vector3> players;
                    for (const std::shared_ptr<PlayerEntity>& player : PlayerEntity::Players())
                    {
                        if (player != nullptr && (player->LoadFlags() & LoadFlags::Active) == LoadFlags::Active
                            && player->Health() > 0)
                        {
                            Vector3 position{};
                            player->GetPosition(position);
                            players.push_back(position);
                        }
                    }
                    Mods::MapGen::Q3Movers::Update(_roomId, scene.FrameCount() + 1, players);
                    Mods::MapGen::Q3Speakers::Ensure(_roomId, scene.FrameCount());
                    Mods::MapGen::Q3Movers::Apply(_roomId, RequireReference(RequireReference(inst.Model()).Nodes));
                }
                if (scene.ProcessFrame())
                {
                    ClearRoomPartState();
                    UpdateRoomParts();
                }
                if (_partVisInfoHead == nullptr || scene.ShowAllNodes()) DrawAllNodes(inst);
                else DrawRoomParts(inst);
            }
        }
        else if (RequireReference(_scene).ProcessFrame())
        {
            ClearRoomPartState();
            UpdateRoomParts();
        }

        Scene& scene = RequireReference(_scene);
        if (scene.ShowCollision() && (scene.ColEntDisplay() == EntityType::All || scene.ColEntDisplay() == Type))
        {
            GetCollisionDrawInfo();
        }
        if (_nodeData != nullptr && (scene.ShowNodeData() || scene.ShowVolumes() == VolumeDisplay::NodeData))
        {
            _drawnNodeData.clear();
            assert(_models.Size() == 2);
            const std::shared_ptr<ModelInstance> nodeInstValue = ManagedListAt(_models.Items(), 1);
            const std::int32_t polygonId = scene.GetNextPolygonId();
            for (const auto& str1Value : RequireReference(RequireReference(_nodeData).Data))
            {
                const auto& str1 = RequireReference(str1Value);
                for (const auto& str2Value : str1)
                {
                    const auto& str2 = RequireReference(str2Value);
                    for (const std::shared_ptr<Formats::NodeData3>& str3Value : str2)
                    {
                        Formats::NodeData3& str3 = RequireReference(str3Value);
                        if (_drawnNodeData.find(&str3) == _drawnNodeData.end())
                        {
                            ModelInstance& nodeInst = RequireReference(nodeInstValue);
                            const std::shared_ptr<Model> nodeModel = nodeInst.Model();
                            Model& model = RequireReference(nodeModel);
                            Node& node = RequireReference(ManagedListAt(RequireReference(model.Nodes), 3));
                            if (node.Enabled)
                            {
                                const std::int32_t start = node.MeshId / 2;
                                for (std::int32_t k = 0; k < node.MeshCount; ++k)
                                {
                                    const std::shared_ptr<Mesh> meshValue
                                        = ManagedListAt(RequireReference(model.Meshes), start + k);
                                    Mesh& mesh = RequireReference(meshValue);
                                    if (!mesh.Visible) continue;
                                    Material& material
                                        = RequireReference(ManagedListAt(RequireReference(model.Materials), mesh.MaterialId));
                                    scene.AddRenderItem(material, polygonId, 1.0F, Vector3::Zero,
                                        GetLightInfo(), IdentityMatrix(), str3.Transform,
                                        nodeModel, meshValue, 0,
                                        CopyManagedArray(RequireReference(_emptyMatrixStack)), str3.Color, std::nullopt,
                                        SelectionType::None, node.BillboardMode);
                                }
                            }
                            _drawnNodeData.insert(&str3);
                            if (scene.ShowVolumes() == VolumeDisplay::NodeData)
                            {
                                const CollisionVolume sphere(str3.Position, str3.MaxDistance);
                                AddVolumeItem(sphere, Vector3(1.0F, 0.0F, 0.0F));
                            }
                        }
                    }
                }
            }
        }
    }

    bool RoomEntity::IsNodeVisible(
        const FrustumInfo& frustumInfo, const Node& node,
        std::int32_t mask, Vector3 offset) const
    {
        const std::shared_ptr<ManagedArray<float>> boundsValue = node.Bounds;
        for (std::int32_t i = 0; i < frustumInfo.Count; ++i)
        {
            assert((mask & (1 << i)) != 0);
            const FrustumPlane& frustumPlane = ManagedAt(RequireReference(frustumInfo.Planes), i);
            const Vector4 plane = frustumPlane.Plane;
            ManagedArray<float>& bounds = RequireReference(boundsValue);
            if (plane.X * (bounds[static_cast<std::size_t>(frustumPlane.XIndex2)] + offset.X)
                + plane.Y * (bounds[static_cast<std::size_t>(frustumPlane.YIndex2)] + offset.Y)
                + plane.Z * (bounds[static_cast<std::size_t>(frustumPlane.ZIndex2)] + offset.Z) - plane.W < 0.0F)
            {
                return false;
            }
            if (plane.X * (bounds[static_cast<std::size_t>(frustumPlane.XIndex1)] + offset.X)
                + plane.Y * (bounds[static_cast<std::size_t>(frustumPlane.YIndex1)] + offset.Y)
                + plane.Z * (bounds[static_cast<std::size_t>(frustumPlane.ZIndex1)] + offset.Z) - plane.W >= 0.0F)
            {
                mask &= ~(1 << i);
            }
        }
        return true;
    }

    void RoomEntity::DrawRoomParts(ModelInstance& roomInst)
    {
        _excludedNodes.clear();
        if (RequireReference(PlayerEntity::Main()).MorphCamera() != nullptr)
        {
            for (const std::shared_ptr<Node>& nodeValue : _morphCameraExcludeNodes)
            {
                _excludedNodes.insert(nodeValue.get());
            }
        }
        std::shared_ptr<RoomPartVisInfo> roomPart = _partVisInfoHead;
        while (roomPart != nullptr)
        {
            RoomPartVisInfo& part = RequireReference(roomPart);
            if (!ModCanPlace(part.NodeRef))
            {
                ModNoteStaleNodeRef(part.NodeRef, "visible part");
                roomPart = part.Next;
                continue;
            }
            std::shared_ptr<RoomFrustumItem> frustumItem = ManagedAt(_roomFrustumLinks, part.NodeRef.PartIndex);
            std::int32_t nodeIndex = part.NodeRef.NodeIndex;
            const std::int32_t modelIndex = part.NodeRef.ModelIndex;
            assert(frustumItem != nullptr);
            assert(nodeIndex != -1);
            assert(modelIndex != -1);
            Vector3 offset = Vector3::Zero;
            std::shared_ptr<ModelInstance> partInstValue{};
            Matrix4 transform = IdentityMatrix();
            if (modelIndex == 0)
            {
                partInstValue = ManagedListAt(_models.Items(), 0);
            }
            else
            {
                partInstValue = ManagedListAt(_connectorModels, modelIndex - 1);
                offset = RequireReference(ManagedListAt(_roomCollision, modelIndex)).Translation;
                ModelInstance& partInst = RequireReference(partInstValue);
                transform = CreateScale(RequireReference((partInst).Model()).Scale);
                transform.M41 = offset.X;
                transform.M42 = offset.Y;
                transform.M43 = offset.Z;
            }
            ModelInstance& partInst = RequireReference(partInstValue);
            if (!partInst.Active)
            {
                roomPart = part.Next;
                continue;
            }
            const auto& nodes = RequireReference(RequireReference((partInst).Model()).Nodes);
            while (nodeIndex != -1)
            {
                Node& node = RequireReference(ManagedListAt(nodes, nodeIndex));
                assert(node.ChildIndex == -1);
                if (!node.Enabled || node.MeshCount == 0 || _excludedNodes.find(&node) != _excludedNodes.end())
                {
                    nodeIndex = node.NextIndex;
                    continue;
                }
                std::shared_ptr<RoomFrustumItem> frustumLink = frustumItem;
                while (frustumLink != nullptr)
                {
                    RoomFrustumItem& link = RequireReference(frustumLink);
                    if (IsNodeVisible(RequireReference(link.Info), node, 0x8FFF, offset))
                    {
                        if (!Equal(offset, Vector3::Zero)) node.Animation = transform;
                        GetItems(partInst, node);
                        const auto pair = _nodePairs.find(&node);
                        if (pair != _nodePairs.end()) _excludedNodes.insert(pair->second.get());
                        break;
                    }
                    frustumLink = link.Next;
                }
                nodeIndex = node.NextIndex;
            }
            roomPart = part.Next;
        }
        if (RequireReference(_scene).ShowForceFields())
        {
            for (const PortalNodeRef& forceField : _forceFields)
            {
                Node& pnode = RequireReference(ManagedListAt(Nodes(), forceField.NodeIndex));
                if (pnode.ChildIndex != -1)
                {
                    Node* node = ManagedListAt(Nodes(), pnode.ChildIndex).get();
                    GetItems(roomInst, RequireReference(node), forceField.Portal);
                    std::int32_t nextIndex = RequireReference(node).NextIndex;
                    while (nextIndex != -1)
                    {
                        node = ManagedListAt(Nodes(), nextIndex).get();
                        GetItems(roomInst, RequireReference(node), forceField.Portal);
                        nextIndex = RequireReference(node).NextIndex;
                    }
                }
            }
        }
    }

    void RoomEntity::DrawAllNodes(ModelInstance& inst, bool connector)
    {
        _excludedNodes.clear();
        const auto& nodes = RequireReference(RequireReference((inst).Model()).Nodes);
        for (const std::shared_ptr<Node>& pnodeValue : nodes)
        {
            Node& pnode = RequireReference(pnodeValue);
            if (!pnode.Enabled) continue;
            if (RequireReference(_scene).ShowAllNodes() || connector)
            {
                GetItems(inst, pnode);
            }
            else if (pnode.RoomPartId >= 0)
            {
                std::int32_t nodeIndex = pnode.ChildIndex;
                while (nodeIndex != -1)
                {
                    Node& node = RequireReference(ManagedListAt(nodes, nodeIndex));
                    if (_excludedNodes.find(&node) == _excludedNodes.end())
                    {
                        GetItems(inst, node);
                        const auto pair = _nodePairs.find(&node);
                        if (pair != _nodePairs.end()) _excludedNodes.insert(pair->second.get());
                    }
                    nodeIndex = node.NextIndex;
                }
            }
        }
        if (RequireReference(_scene).ShowForceFields() && !connector)
        {
            for (const PortalNodeRef& forceField : _forceFields)
            {
                Node& pnode = RequireReference(ManagedListAt(Nodes(), forceField.NodeIndex));
                if (pnode.ChildIndex != -1)
                {
                    Node* node = ManagedListAt(Nodes(), pnode.ChildIndex).get();
                    GetItems(inst, RequireReference(node), forceField.Portal);
                    std::int32_t nextIndex = RequireReference(node).NextIndex;
                    while (nextIndex != -1)
                    {
                        node = ManagedListAt(Nodes(), nextIndex).get();
                        GetItems(inst, RequireReference(node), forceField.Portal);
                        nextIndex = RequireReference(node).NextIndex;
                    }
                }
            }
        }
    }

    void RoomEntity::GetItems(
        ModelInstance& inst, Node& node, const std::shared_ptr<Portal>& portal)
    {
        if (!node.Enabled) return;
        const std::shared_ptr<Model> modelValue = inst.Model();
        const std::int32_t start = node.MeshId / 2;
        for (std::int32_t k = 0; k < node.MeshCount; ++k)
        {
            Model& model = RequireReference(modelValue);
            std::int32_t polygonId = 0;
            const std::shared_ptr<Mesh> meshValue
                = ManagedListAt(RequireReference(model.Meshes), start + k);
            Mesh& mesh = RequireReference(meshValue);
            if (!mesh.Visible) continue;
            Material& material = RequireReference(ManagedListAt(RequireReference(model.Materials), mesh.MaterialId));
            float alpha = 1.0F;
            if (portal != nullptr)
            {
                Scene& scene = RequireReference(_scene);
                polygonId = scene.GetNextPolygonId();
                alpha = GetPortalAlpha(portal->Position, scene.CameraPosition());
            }
            else if (material.RenderMode == RenderMode::Translucent)
            {
                polygonId = RequireReference(_scene).GetNextPolygonId();
            }
            const Matrix4 texcoordMatrix = GetTexcoordMatrix(inst, material, mesh.MaterialId, node);
            const SelectionType selectionType = Selection::CheckSelection(this, inst, node, mesh);
            RequireReference(_scene).AddRenderItem(material, polygonId, alpha, Vector3::Zero, GetLightInfo(),
                texcoordMatrix, node.Animation, modelValue, meshValue,
                static_cast<std::int32_t>(RequireReference(model.NodeMatrixIds).size()),
                CopyManagedArray(RequireReference(model.MatrixStackValues)), std::nullopt, std::nullopt,
                selectionType, node.BillboardMode);
        }
    }

    float RoomEntity::GetPortalAlpha(Vector3 portalPosition, Vector3 cameraPosition) const
    {
        float between = Length(Subtract(portalPosition, cameraPosition));
        between /= 8.0F;
        if (between < 1.0F / 4096.0F) between = 0.0F;
        return MathMin(between, 1.0F);
    }

    void RoomEntity::GetDisplayVolumes()
    {
        Scene& scene = RequireReference(_scene);
        if (scene.ShowVolumes() == VolumeDisplay::NodeBounds)
        {
            const std::shared_ptr<Node> selectedNode = Selection::Node();
            if (selectedNode != nullptr)
            {
                bool contains = false;
                for (const std::shared_ptr<Node>& nodeValue : Nodes())
                {
                    if (nodeValue == selectedNode)
                    {
                        contains = true;
                        break;
                    }
                }
                if (contains)
                {
                    Node& node = RequireReference(selectedNode);
                    const float width = node.MaxBounds.X - node.MinBounds.X;
                    const float height = node.MaxBounds.Y - node.MinBounds.Y;
                    const float depth = node.MaxBounds.Z - node.MinBounds.Z;
                    const CollisionVolume box(Vector3(1.0F, 0.0F, 0.0F),
                        Vector3(0.0F, 1.0F, 0.0F), Vector3(0.0F, 0.0F, -1.0F),
                        TypeExtensions::WithZ(node.MinBounds, node.MaxBounds.Z), width, height, depth);
                    AddVolumeItem(box, Vector3(1.0F, 0.0F, 0.0F));
                }
            }
        }
        else if (scene.ShowVolumes() == VolumeDisplay::Portal)
        {
            for (const std::shared_ptr<Portal>& portalValue : _portals)
            {
                Portal& portal = RequireReference(portalValue);
                if (!portal.Active) continue;
                const auto& points = RequireReference(portal.Points);
                auto verts = MphRead::NativeRuntime::RentFromSharedArrayPool(static_cast<std::int32_t>(points.size()));
                for (std::size_t i = 0; i < points.size(); ++i)
                {
                    (*verts)[i] = points[i];
                }
                const float alpha = GetPortalAlpha(portal.Position, scene.CameraPosition());
                const Vector4 color = portal.IsForceField
                    ? Vector4(16.0F / 31.0F, 16.0F / 31.0F, 1.0F, alpha)
                    : Vector4(16.0F / 31.0F, 1.0F, 16.0F / 31.0F, alpha);
                scene.AddRenderItem(CullingMode::Neither, scene.GetNextPolygonId(), color,
                    RenderItemType::Ngon, verts, static_cast<std::int32_t>(verts->Length()), true);
            }
        }
        else if (scene.ShowVolumes() == VolumeDisplay::KillPlane && !Meta().FirstHunt)
        {
            auto verts = MphRead::NativeRuntime::RentFromSharedArrayPool(4);
            (*verts)[0] = Vector3(10000.0F, scene.KillHeight(), 10000.0F);
            (*verts)[1] = Vector3(10000.0F, scene.KillHeight(), -10000.0F);
            (*verts)[2] = Vector3(-10000.0F, scene.KillHeight(), -10000.0F);
            (*verts)[3] = Vector3(-10000.0F, scene.KillHeight(), 10000.0F);
            scene.AddRenderItem(CullingMode::Neither, scene.GetNextPolygonId(),
                Vector4(1.0F, 0.0F, 1.0F, 0.5F), RenderItemType::Quad, verts, 0, true);
        }
        else if ((scene.ShowVolumes() == VolumeDisplay::CameraLimit
                || scene.ShowVolumes() == VolumeDisplay::PlayerLimit) && Meta().HasLimits)
        {
            const Vector3 minLimit = scene.ShowVolumes() == VolumeDisplay::CameraLimit
                ? Meta().CameraMin : Meta().PlayerMin;
            const Vector3 maxLimit = scene.ShowVolumes() == VolumeDisplay::CameraLimit
                ? Meta().CameraMax : Meta().PlayerMax;
            auto bverts = MphRead::NativeRuntime::RentFromSharedArrayPool(8);
            const Vector3 point0 = minLimit;
            const Vector3 sideX(maxLimit.X - minLimit.X, 0.0F, 0.0F);
            const Vector3 sideY(0.0F, maxLimit.Y - minLimit.Y, 0.0F);
            const Vector3 sideZ(0.0F, 0.0F, maxLimit.Z - minLimit.Z);
            (*bverts)[0] = point0;
            (*bverts)[1] = Add(point0, sideZ);
            (*bverts)[2] = Add(point0, sideX);
            (*bverts)[3] = Add(Add(point0, sideX), sideZ);
            (*bverts)[4] = Add(point0, sideY);
            (*bverts)[5] = Add(Add(point0, sideY), sideZ);
            (*bverts)[6] = Add(Add(point0, sideX), sideY);
            (*bverts)[7] = Add(Add(Add(point0, sideX), sideY), sideZ);
            const Vector4 color = scene.ShowVolumes() == VolumeDisplay::CameraLimit
                ? Vector4(1.0F, 0.0F, 0.69F, 0.5F) : Vector4(1.0F, 0.0F, 0.0F, 0.5F);
            scene.AddRenderItem(CullingMode::Neither, scene.GetNextPolygonId(), color,
                RenderItemType::Box, bverts, 8);
        }
    }
}
