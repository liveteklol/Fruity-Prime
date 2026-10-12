#pragma once

#include "Culling.hpp"
#include "Enums.hpp"
#include "Types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace MphRead
{
    class ModelMetadata;
    class RoomMetadata;
    class Scene;
}

namespace MphRead::Entities
{
    class EntityBase;
}

namespace MphRead::Formats::Collision
{
    class CollisionInstance;
    class CollisionInfo;
    class MphCollisionInfo;
    class FhCollisionInfo;

    struct FhCollisionPortal;
    struct FhCollisionVector;

    class EntityCollision
    {
    public:
        OpenTK::Mathematics::Matrix4 Transform{};
        OpenTK::Mathematics::Matrix4 Inverse1{};
        OpenTK::Mathematics::Matrix4 Inverse2{};
        OpenTK::Mathematics::Vector3 InitialCenter{};
        OpenTK::Mathematics::Vector3 CurrentCenter{};
        float MaxDistance = 0.0F;

        MphRead::Entities::EntityBase* const Entity;
        const std::shared_ptr<CollisionInstance> Collision;
        const std::shared_ptr<std::vector<OpenTK::Mathematics::Vector3>> DrawPoints;

        EntityCollision(
            std::shared_ptr<CollisionInstance> collision,
            MphRead::Entities::EntityBase* entity);

        EntityCollision(const EntityCollision&) = delete;
        EntityCollision& operator=(const EntityCollision&) = delete;
        EntityCollision(EntityCollision&&) = delete;
        EntityCollision& operator=(EntityCollision&&) = delete;
    };

    enum class CollisionFlags : std::uint16_t
    {
        None = 0x0000,
        Damaging = 0x0001,
        Bit01 = 0x0002,
        Bit02 = 0x0004,
        ReflectBeams = 0x0200,
        Bit10 = 0x0400,
        Bit11 = 0x0800,
        Bit12 = 0x1000,
        IgnorePlayers = 0x2000,
        IgnoreBeams = 0x4000,
        IgnoreScan = 0x8000
    };

    [[nodiscard]] constexpr CollisionFlags operator|(
        CollisionFlags left, CollisionFlags right) noexcept
    {
        return static_cast<CollisionFlags>(
            static_cast<std::uint16_t>(left)
            | static_cast<std::uint16_t>(right));
    }

    [[nodiscard]] constexpr CollisionFlags operator&(
        CollisionFlags left, CollisionFlags right) noexcept
    {
        return static_cast<CollisionFlags>(
            static_cast<std::uint16_t>(left)
            & static_cast<std::uint16_t>(right));
    }

    [[nodiscard]] constexpr CollisionFlags operator^(
        CollisionFlags left, CollisionFlags right) noexcept
    {
        return static_cast<CollisionFlags>(
            static_cast<std::uint16_t>(left)
            ^ static_cast<std::uint16_t>(right));
    }

    [[nodiscard]] constexpr CollisionFlags operator~(
        CollisionFlags value) noexcept
    {
        return static_cast<CollisionFlags>(
            static_cast<std::uint16_t>(
                ~static_cast<std::uint16_t>(value)));
    }

    constexpr CollisionFlags& operator|=(
        CollisionFlags& left, CollisionFlags right) noexcept
    {
        left = left | right;
        return left;
    }

    constexpr CollisionFlags& operator&=(
        CollisionFlags& left, CollisionFlags right) noexcept
    {
        left = left & right;
        return left;
    }

    constexpr CollisionFlags& operator^=(
        CollisionFlags& left, CollisionFlags right) noexcept
    {
        left = left ^ right;
        return left;
    }

    struct CollisionHeader final
    {
        const std::array<char, 4> Type{};
        const std::uint32_t PointCount = 0;
        const std::uint32_t PointOffset = 0;
        const std::uint32_t PlaneCount = 0;
        const std::uint32_t PlaneOffset = 0;
        const std::uint32_t PointIndexCount = 0;
        const std::uint32_t PointIndexOffset = 0;
        const std::uint32_t DataCount = 0;
        const std::uint32_t DataOffset = 0;
        const std::uint32_t DataIndexCount = 0;
        const std::uint32_t DataIndexOffset = 0;
        const std::int32_t PartsX = 0;
        const std::int32_t PartsY = 0;
        const std::int32_t PartsZ = 0;
        const MphRead::Vector3Fx MinPosition{};
        const std::uint32_t EntryCount = 0;
        const std::uint32_t EntryOffset = 0;
        const std::uint32_t PortalCount = 0;
        const std::uint32_t PortalOffset = 0;

        CollisionHeader() noexcept = default;
        CollisionHeader(const CollisionHeader&) noexcept = default;
        CollisionHeader& operator=(const CollisionHeader& other) noexcept;
    };

    struct CollisionData final
    {
        const std::int32_t Counter = 0;
        const std::uint16_t PlaneIndex = 0;
        const CollisionFlags Flags = CollisionFlags::None;
        const std::uint16_t LayerMask = 0;
        const std::uint16_t PaddingA = 0;
        const std::uint16_t PointIndexCount = 0;
        const std::uint16_t PointStartIndex = 0;

        CollisionData() noexcept = default;
        CollisionData(const CollisionData&) noexcept = default;
        CollisionData& operator=(const CollisionData& other) noexcept;

        [[nodiscard]] std::int32_t Slipperiness() const noexcept;
        [[nodiscard]] MphRead::Terrain Terrain() const noexcept;
        [[nodiscard]] bool IgnorePlayers() const noexcept;
        [[nodiscard]] bool IgnoreBeams() const noexcept;
        [[nodiscard]] std::int32_t Axis() const noexcept;
    };

    // A cell of the grid as the file stores it: wc01 in 16 bits, wc32 (a
    // level too large for them, MapCollisionPacker) in 32.
    struct RawCollisionEntry final
    {
        std::uint16_t DataCount = 0;
        std::uint16_t DataStartIndex = 0;
    };

    struct RawCollisionEntry32 final
    {
        std::uint32_t DataCount = 0;
        std::uint32_t DataStartIndex = 0;
    };

    // A cell as the game holds it: where its faces start may pass 16 bits
    // once a layer's cells stop sharing runs (ReadMphCollision).
    struct CollisionEntry final
    {
        const std::uint16_t DataCount = 0;
        const std::uint16_t Padding2 = 0;
        const std::uint32_t DataStartIndex = 0;

        CollisionEntry() noexcept = default;

        constexpr CollisionEntry(
            std::uint16_t dataCount,
            std::uint32_t dataStartIndex) noexcept
            : DataCount(dataCount),
              DataStartIndex(dataStartIndex)
        {
        }

        CollisionEntry(const CollisionEntry&) noexcept = default;
        CollisionEntry& operator=(const CollisionEntry& other) noexcept;
    };

    struct RawCollisionPortal final
    {
        const std::array<char, 40> Name{};
        const std::array<char, 24> NodeName1{};
        const std::array<char, 24> NodeName2{};
        const MphRead::Vector3Fx Point1{};
        const MphRead::Vector3Fx Point2{};
        const MphRead::Vector3Fx Point3{};
        const MphRead::Vector3Fx Point4{};
        const MphRead::Vector4Fx Plane1{};
        const MphRead::Vector4Fx Plane2{};
        const MphRead::Vector4Fx Plane3{};
        const MphRead::Vector4Fx Plane4{};
        const MphRead::Vector4Fx Plane{};
        const std::uint16_t Flags = 0;
        const std::uint16_t LayerMask = 0;
        const std::uint16_t PointCount = 0;
        const std::uint8_t UnusedDE = 0;
        const std::uint8_t UnusedDF = 0;

        RawCollisionPortal() noexcept = default;
        RawCollisionPortal(const RawCollisionPortal&) noexcept = default;
        RawCollisionPortal& operator=(const RawCollisionPortal& other) noexcept;
    };

    class Portal
    {
    public:
        bool Active = true;

        const std::string Name;
        const std::string NodeName1;
        const std::string NodeName2;

        MphRead::Formats::Culling::NodeRef NodeRef1
            = MphRead::Formats::Culling::NodeRef::None;
        MphRead::Formats::Culling::NodeRef NodeRef2
            = MphRead::Formats::Culling::NodeRef::None;

        const std::uint16_t LayerMask;
        const bool IsForceField;

        const std::shared_ptr<
            const std::vector<OpenTK::Mathematics::Vector3>> Points;
        const std::shared_ptr<
            const std::vector<OpenTK::Mathematics::Vector4>> Planes;

        const OpenTK::Mathematics::Vector4 Plane;
        const OpenTK::Mathematics::Vector3 Position;

        const std::uint16_t Flags;
        const std::uint8_t Unknown00;
        const std::uint8_t Unknown01;

        explicit Portal(RawCollisionPortal raw);

        Portal(
            FhCollisionPortal raw,
            std::shared_ptr<const std::vector<FhCollisionVector>> rawVectors,
            std::shared_ptr<const std::vector<MphRead::Vector3Fx>> rawPoints,
            std::shared_ptr<const std::vector<MphRead::Vector4Fx>> rawPlanes);

        Portal(
            std::string nodeName1,
            std::string nodeName2,
            std::shared_ptr<
                const std::vector<OpenTK::Mathematics::Vector3>> points,
            std::shared_ptr<
                const std::vector<OpenTK::Mathematics::Vector4>> planes,
            OpenTK::Mathematics::Vector4 plane);

        Portal(const Portal&) = delete;
        Portal& operator=(const Portal&) = delete;
        Portal(Portal&&) = delete;
        Portal& operator=(Portal&&) = delete;

    private:
        struct Initializer;

        explicit Portal(Initializer initializer);

        [[nodiscard]] static Initializer Initialize(
            RawCollisionPortal raw);

        [[nodiscard]] static Initializer Initialize(
            FhCollisionPortal raw,
            const std::shared_ptr<
                const std::vector<FhCollisionVector>>& rawVectors,
            const std::shared_ptr<
                const std::vector<MphRead::Vector3Fx>>& rawPoints,
            const std::shared_ptr<
                const std::vector<MphRead::Vector4Fx>>& rawPlanes);
    };

    class CollisionInstance
    {
    public:
        const std::string Name;
        bool Active = true;
        const std::shared_ptr<CollisionInfo> Info;
        const bool IsEntity;

        std::optional<std::string> ConnectorName{};
        OpenTK::Mathematics::Vector3 Translation{};

        CollisionInstance(
            std::string name,
            std::shared_ptr<CollisionInfo> info,
            bool isEntity);

        CollisionInstance(const CollisionInstance&) = delete;
        CollisionInstance& operator=(const CollisionInstance&) = delete;
        CollisionInstance(CollisionInstance&&) = delete;
        CollisionInstance& operator=(CollisionInstance&&) = delete;
    };

    class CollisionInfo
    {
    public:
        const bool FirstHunt;

        const std::shared_ptr<
            const std::vector<OpenTK::Mathematics::Vector3>> Points;
        const std::shared_ptr<
            const std::vector<OpenTK::Mathematics::Vector4>> Planes;
        const std::shared_ptr<
            const std::vector<std::shared_ptr<Portal>>> Portals;

        CollisionInfo(
            std::shared_ptr<const std::vector<MphRead::Vector3Fx>> points,
            std::shared_ptr<const std::vector<MphRead::Vector4Fx>> planes,
            std::shared_ptr<
                const std::vector<std::shared_ptr<Portal>>> portals,
            bool firstHunt);

        virtual ~CollisionInfo() = default;

        CollisionInfo(const CollisionInfo&) = delete;
        CollisionInfo& operator=(const CollisionInfo&) = delete;
        CollisionInfo(CollisionInfo&&) = delete;
        CollisionInfo& operator=(CollisionInfo&&) = delete;

        virtual void GetDrawInfo(
            std::shared_ptr<
                const std::vector<OpenTK::Mathematics::Vector3>> points,
            OpenTK::Mathematics::Vector3 translation,
            MphRead::EntityType entityType,
            MphRead::Scene* scene) = 0;
    };

    class MphCollisionInfo : public CollisionInfo
    {
    public:
        const CollisionHeader Header;
        const std::shared_ptr<
            const std::vector<std::uint16_t>> PointIndices;
        const std::shared_ptr<
            const std::vector<CollisionData>> Data;
        const std::shared_ptr<
            const std::vector<std::uint16_t>> DataIndices;
        const std::shared_ptr<
            const std::vector<CollisionEntry>> Entries;
        const OpenTK::Mathematics::Vector3 MinPosition;

        MphCollisionInfo(
            CollisionHeader header,
            std::shared_ptr<const std::vector<MphRead::Vector3Fx>> points,
            std::shared_ptr<const std::vector<MphRead::Vector4Fx>> planes,
            std::shared_ptr<const std::vector<std::uint16_t>> ptIdxs,
            std::shared_ptr<const std::vector<CollisionData>> data,
            std::shared_ptr<const std::vector<std::uint16_t>> dataIdxs,
            std::shared_ptr<const std::vector<CollisionEntry>> entries,
            std::shared_ptr<
                const std::vector<std::shared_ptr<Portal>>> portals);

        void GetDrawInfo(
            std::shared_ptr<
                const std::vector<OpenTK::Mathematics::Vector3>> points,
            OpenTK::Mathematics::Vector3 translation,
            MphRead::EntityType entityType,
            MphRead::Scene* scene) override;

        [[nodiscard]] OpenTK::Mathematics::Vector3i PartIndexFromEntry(
            std::int32_t index) const;

        [[nodiscard]] std::int32_t EntryIndexFromPoint(
            OpenTK::Mathematics::Vector3 point) const;

        void GetPartition(
            OpenTK::Mathematics::Vector3 point,
            std::shared_ptr<
                const std::vector<OpenTK::Mathematics::Vector3>> points,
            MphRead::EntityType entityType,
            MphRead::Scene* scene) const;

    private:
        [[nodiscard]] static const std::array<
            OpenTK::Mathematics::Vector4, 12>& Colors();
    };

    struct FhCollisionPortal final
    {
        const std::array<char, 40> Name{};
        const std::array<char, 16> NodeName1{};
        const std::array<char, 16> NodeName2{};
        const MphRead::Vector4Fx Plane{};
        const std::uint16_t VectorCount = 0;
        const std::uint16_t VectorStartIndex = 0;
        const std::uint8_t Field5C = 0;
        const std::uint8_t Field5D = 0;
        const std::uint16_t Padding5E = 0;

        FhCollisionPortal() noexcept = default;
        FhCollisionPortal(const FhCollisionPortal&) noexcept = default;
        FhCollisionPortal& operator=(
            const FhCollisionPortal& other) noexcept;
    };

    struct FhCollisionHeader final
    {
        const std::uint32_t PointCount = 0;
        const std::uint32_t PointOffset = 0;
        const std::uint32_t PlaneCount = 0;
        const std::uint32_t PlaneOffset = 0;
        const std::uint32_t VectorCount = 0;
        const std::uint32_t VectorOffset = 0;
        const std::uint16_t DataCount = 0;
        const std::uint16_t DataStartIndex = 0;
        const std::uint32_t DataOffset = 0;
        const std::uint32_t DataIndexCount = 0;
        const std::uint32_t DataIndexOffset = 0;
        const std::uint32_t EntryCount = 0;
        const std::uint32_t EntryOffset = 0;
        const std::uint32_t TreeNodeIndexCount = 0;
        const std::uint32_t TreeNodeIndexOffset = 0;
        const std::uint32_t TreeNodeCount = 0;
        const std::uint32_t TreeNodeOffset = 0;
        const std::uint32_t PortalCount = 0;
        const std::uint32_t PortalOffset = 0;

        FhCollisionHeader() noexcept = default;
        FhCollisionHeader(const FhCollisionHeader&) noexcept = default;
        FhCollisionHeader& operator=(
            const FhCollisionHeader& other) noexcept;
    };

    struct FhCollisionData final
    {
        const std::uint16_t PlaneIndex = 0;
        const std::uint16_t VectorCount = 0;
        const std::uint16_t VectorStartIndex = 0;

        FhCollisionData() noexcept = default;
        FhCollisionData(const FhCollisionData&) noexcept = default;
        FhCollisionData& operator=(
            const FhCollisionData& other) noexcept;
    };

    struct FhCollisionVector final
    {
        const std::uint16_t Point1Index = 0;
        const std::uint16_t Point2Index = 0;
        const std::uint16_t PlaneIndex = 0;

        FhCollisionVector() noexcept = default;
        FhCollisionVector(const FhCollisionVector&) noexcept = default;
        FhCollisionVector& operator=(
            const FhCollisionVector& other) noexcept;
    };

    struct FhCollisionEntry final
    {
        const MphRead::Vector3Fx MinBounds{};
        const MphRead::Vector3Fx MaxBounds{};
        const std::uint16_t DataCount = 0;
        const std::uint16_t DataStartIndex = 0;

        FhCollisionEntry() noexcept = default;
        FhCollisionEntry(const FhCollisionEntry&) noexcept = default;
        FhCollisionEntry& operator=(
            const FhCollisionEntry& other) noexcept;
    };

    struct FhCollisionTreeNode final
    {
        const MphRead::Vector3Fx MinBounds{};
        const MphRead::Vector3Fx MaxBounds{};
        const std::uint16_t LeftIndex = 0;
        const std::uint16_t RightIndex = 0;

        FhCollisionTreeNode() noexcept = default;
        FhCollisionTreeNode(
            const FhCollisionTreeNode&) noexcept = default;
        FhCollisionTreeNode& operator=(
            const FhCollisionTreeNode& other) noexcept;
    };

    class FhCollisionInfo : public CollisionInfo
    {
    public:
        const FhCollisionHeader Header;
        const std::shared_ptr<
            const std::vector<FhCollisionData>> Data;
        const std::shared_ptr<
            const std::vector<FhCollisionVector>> Vectors;
        const std::shared_ptr<
            const std::vector<std::uint16_t>> DataIndices;
        const std::shared_ptr<
            const std::vector<FhCollisionEntry>> Entries;
        const std::shared_ptr<
            const std::vector<std::int32_t>> TreeNodeIndices;
        const std::shared_ptr<
            const std::vector<FhCollisionTreeNode>> TreeNodes;

        FhCollisionInfo(
            FhCollisionHeader header,
            std::shared_ptr<const std::vector<MphRead::Vector3Fx>> points,
            std::shared_ptr<const std::vector<MphRead::Vector4Fx>> planes,
            std::shared_ptr<const std::vector<FhCollisionData>> data,
            std::shared_ptr<const std::vector<FhCollisionVector>> vectors,
            std::shared_ptr<const std::vector<std::uint16_t>> dataIndices,
            std::shared_ptr<
                const std::vector<std::shared_ptr<Portal>>> portals,
            std::shared_ptr<const std::vector<FhCollisionEntry>> entries,
            std::shared_ptr<
                const std::vector<std::int32_t>> treeNodeIndices,
            std::shared_ptr<
                const std::vector<FhCollisionTreeNode>> treeNodes);

        void GetDrawInfo(
            std::shared_ptr<
                const std::vector<OpenTK::Mathematics::Vector3>> points,
            OpenTK::Mathematics::Vector3 translation,
            MphRead::EntityType entityType,
            MphRead::Scene* scene) override;

        void GetPartition(
            std::shared_ptr<
                std::vector<OpenTK::Mathematics::Vector3>> points,
            MphRead::Scene* scene) const;
    };

    class Collision final
    {
    public:
        Collision() = delete;

        [[nodiscard]] static std::shared_ptr<CollisionInstance> GetCollision(
            const MphRead::ModelMetadata* meta,
            bool extra = false);

        [[nodiscard]] static std::shared_ptr<CollisionInstance> GetCollision(
            const MphRead::RoomMetadata* meta,
            std::int32_t roomLayerMask = 0);

        // An entity's collision from a file under the game's files, every
        // layer of it (a translated Quake 3 mover's, Q3MoverEntity).
        [[nodiscard]] static std::shared_ptr<CollisionInstance> GetEntityCollision(
            const std::string& path);

        [[nodiscard]] static std::shared_ptr<MphCollisionInfo>
        ReadMphCollision(
            CollisionHeader header,
            std::span<const std::uint8_t> bytes,
            std::int32_t roomLayerMask);

    private:
        [[nodiscard]] static std::shared_ptr<CollisionInstance> GetCollision(
            std::optional<std::string> path,
            std::string name,
            bool firstHunt,
            std::int32_t roomLayerMask,
            bool isEntity);

        [[nodiscard]] static std::shared_ptr<FhCollisionInfo> ReadFhCollision(
            std::span<const std::uint8_t> bytes);
    };

    static_assert(sizeof(CollisionHeader) == 84);
    static_assert(sizeof(CollisionData) == 16);
    static_assert(sizeof(CollisionEntry) == 8);
    static_assert(sizeof(RawCollisionEntry) == 4);
    static_assert(sizeof(RawCollisionEntry32) == 8);
    static_assert(sizeof(RawCollisionPortal) == 224);

    static_assert(sizeof(FhCollisionPortal) == 96);
    static_assert(sizeof(FhCollisionHeader) == 72);
    static_assert(sizeof(FhCollisionData) == 6);
    static_assert(sizeof(FhCollisionVector) == 6);
    static_assert(sizeof(FhCollisionEntry) == 28);
    static_assert(sizeof(FhCollisionTreeNode) == 28);
}
