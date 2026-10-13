#include "EntityBase.hpp"

#include "../NativeRuntime/System/Buffers.hpp"

#include "../Formats/Collision.hpp"
#include "../Formats/Entity.hpp"
#include "Mods/Network/Replay/DemoPlayback.hpp"
#include "../Messaging.hpp"
#include "../Read.hpp"
#include "../Renderer.hpp"
#include "../Scene.hpp"
#include "../Selection.hpp"
#include "../Formats/Types.hpp"
#include "../NativeRuntime/System/Managed.hpp"
#include "../NativeRuntime/OpenTK/Mathematics.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <utility>

using ::MphRead::NativeRuntime::ManagedAt;
using ::MphRead::NativeRuntime::ManagedListAt;
using ::MphRead::NativeRuntime::MathClamp;
using ::MphRead::NativeRuntime::RequireReference;
using ::OpenTK::Mathematics::Add;
using ::OpenTK::Mathematics::ClearScale;
using ::OpenTK::Mathematics::CreateRotationX;
using ::OpenTK::Mathematics::CreateRotationY;
using ::OpenTK::Mathematics::CreateRotationZ;
using ::OpenTK::Mathematics::CreateScale;
using ::OpenTK::Mathematics::CreateTranslation;
using ::OpenTK::Mathematics::Determinant;
using ::OpenTK::Mathematics::Divide;
using ::OpenTK::Mathematics::Equal;
using ::OpenTK::Mathematics::IdentityMatrix;
using ::OpenTK::Mathematics::Inverted;
using ::OpenTK::Mathematics::Length;
using ::OpenTK::Mathematics::Multiply;

namespace
{
    using OpenTK::Mathematics::Matrix3;
    using OpenTK::Mathematics::Matrix4;
    using OpenTK::Mathematics::Vector3;
    using OpenTK::Mathematics::Vector4;

    template <typename T>
    [[nodiscard]] const T& ManagedReadOnlyListAt(
        const std::vector<T>& values, std::int32_t index)
    {
        if (index < 0 || static_cast<std::size_t>(index) >= values.size())
        {
            throw System::ArgumentOutOfRangeException();
        }
        return values[static_cast<std::size_t>(index)];
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

    struct Quaternion
    {
        float X = 0.0F;
        float Y = 0.0F;
        float Z = 0.0F;
        float W = 0.0F;
    };

    [[nodiscard]] Vector3 ExtractEulerAngles(Matrix4 value) noexcept
    {
        Vector3 row0(value.M11, value.M12, value.M13);
        Vector3 row1(value.M21, value.M22, value.M23);
        Vector3 row2(value.M31, value.M32, value.M33);
        row0 = row0.Normalized();
        row1 = row1.Normalized();
        row2 = row2.Normalized();

        Quaternion q{};
        const double trace = 0.25 * (row0.X + row1.Y + row2.Z + 1.0);
        if (trace > 0.0)
        {
            double sq = std::sqrt(trace);
            q.W = static_cast<float>(sq);
            sq = 1.0 / (4.0 * sq);
            q.X = static_cast<float>((row1.Z - row2.Y) * sq);
            q.Y = static_cast<float>((row2.X - row0.Z) * sq);
            q.Z = static_cast<float>((row0.Y - row1.X) * sq);
        }
        else if (row0.X > row1.Y && row0.X > row2.Z)
        {
            double sq = 2.0 * std::sqrt(1.0 + row0.X - row1.Y - row2.Z);
            q.X = static_cast<float>(0.25 * sq);
            sq = 1.0 / sq;
            q.W = static_cast<float>((row2.Y - row1.Z) * sq);
            q.Y = static_cast<float>((row1.X + row0.Y) * sq);
            q.Z = static_cast<float>((row2.X + row0.Z) * sq);
        }
        else if (row1.Y > row2.Z)
        {
            double sq = 2.0 * std::sqrt(1.0 + row1.Y - row0.X - row2.Z);
            q.Y = static_cast<float>(0.25 * sq);
            sq = 1.0 / sq;
            q.W = static_cast<float>((row2.X - row0.Z) * sq);
            q.X = static_cast<float>((row1.X + row0.Y) * sq);
            q.Z = static_cast<float>((row2.Y + row1.Z) * sq);
        }
        else
        {
            double sq = 2.0 * std::sqrt(1.0 + row2.Z - row0.X - row1.Y);
            q.Z = static_cast<float>(0.25 * sq);
            sq = 1.0 / sq;
            q.W = static_cast<float>((row1.X - row0.Y) * sq);
            q.X = static_cast<float>((row2.X + row0.Z) * sq);
            q.Y = static_cast<float>((row2.Y + row1.Z) * sq);
        }

        const float xyzLengthSquared = (q.X * q.X + q.Y * q.Y) + q.Z * q.Z;
        const float inverseLength = 1.0F / std::sqrt(
            q.W * q.W + xyzLengthSquared);
        q.X *= inverseLength;
        q.Y *= inverseLength;
        q.Z *= inverseLength;
        q.W *= inverseLength;

        constexpr float singularityThreshold = 0.4999995F;
        constexpr float piOver2 = 1.57079632679489661923F;
        const float sqw = q.W * q.W;
        const float sqx = q.X * q.X;
        const float sqy = q.Y * q.Y;
        const float sqz = q.Z * q.Z;
        const float unit = sqx + sqy + sqz + sqw;
        const float singularityTest = q.X * q.Z + q.W * q.Y;

        Vector3 angles{};
        if (singularityTest > singularityThreshold * unit)
        {
            angles.Z = 2.0F * std::atan2(q.X, q.W);
            angles.Y = piOver2;
            angles.X = 0.0F;
        }
        else if (singularityTest < -singularityThreshold * unit)
        {
            angles.Z = -2.0F * std::atan2(q.X, q.W);
            angles.Y = -piOver2;
            angles.X = 0.0F;
        }
        else
        {
            angles.Z = std::atan2(2.0F * (q.W * q.Z - q.X * q.Y),
                sqw + sqx - sqy - sqz);
            angles.Y = std::asin(2.0F * singularityTest / unit);
            angles.X = std::atan2(2.0F * (q.W * q.X - q.Y * q.Z),
                sqw - sqx - sqy + sqz);
        }
        return angles;
    }

}

namespace MphRead::Entities
{
    EntityBase::MatrixProperty::MatrixProperty(EntityBase* owner) noexcept
        : M11(owner->_transform.M11), M12(owner->_transform.M12),
          M13(owner->_transform.M13), M14(owner->_transform.M14),
          M21(owner->_transform.M21), M22(owner->_transform.M22),
          M23(owner->_transform.M23), M24(owner->_transform.M24),
          M31(owner->_transform.M31), M32(owner->_transform.M32),
          M33(owner->_transform.M33), M34(owner->_transform.M34),
          M41(owner->_transform.M41), M42(owner->_transform.M42),
          M43(owner->_transform.M43), M44(owner->_transform.M44),
          _owner(owner)
    {
    }

    EntityBase::MatrixProperty& EntityBase::MatrixProperty::operator=(Matrix4 value)
    {
        _owner->SetTransformProperty(value);
        return *this;
    }

    EntityBase::MatrixProperty::operator Matrix4() const noexcept
    {
        return _owner->_transform;
    }

    Vector4 EntityBase::MatrixProperty::Row0() const noexcept
    {
        return Vector4(M11, M12, M13, M14);
    }

    Vector4 EntityBase::MatrixProperty::Row1() const noexcept
    {
        return Vector4(M21, M22, M23, M24);
    }

    Vector4 EntityBase::MatrixProperty::Row2() const noexcept
    {
        return Vector4(M31, M32, M33, M34);
    }

    Vector4 EntityBase::MatrixProperty::Row3() const noexcept
    {
        return Vector4(M41, M42, M43, M44);
    }

    EntityBase::VectorProperty::VectorProperty(
        EntityBase* owner, VectorPropertyKind kind) noexcept
        : X(kind == VectorPropertyKind::Scale ? owner->_scale.X
            : kind == VectorPropertyKind::Rotation ? owner->_rotation.X : owner->_position.X),
          Y(kind == VectorPropertyKind::Scale ? owner->_scale.Y
            : kind == VectorPropertyKind::Rotation ? owner->_rotation.Y : owner->_position.Y),
          Z(kind == VectorPropertyKind::Scale ? owner->_scale.Z
            : kind == VectorPropertyKind::Rotation ? owner->_rotation.Z : owner->_position.Z),
          _owner(owner), _kind(kind)
    {
    }

    EntityBase::VectorProperty& EntityBase::VectorProperty::operator=(Vector3 value)
    {
        if (_kind == VectorPropertyKind::Scale)
        {
            _owner->SetScaleProperty(value);
        }
        else if (_kind == VectorPropertyKind::Rotation)
        {
            _owner->SetRotationProperty(value);
        }
        else
        {
            _owner->SetPositionProperty(value);
        }
        return *this;
    }

    EntityBase::VectorProperty::operator Vector3() const noexcept
    {
        if (_kind == VectorPropertyKind::Scale)
        {
            return _owner->_scale;
        }
        if (_kind == VectorPropertyKind::Rotation)
        {
            return _owner->_rotation;
        }
        return _owner->_position;
    }

    Vector3 EntityBase::VectorProperty::Normalized() const
    {
        return static_cast<Vector3>(*this).Normalized();
    }

    std::size_t EntityBase::ModelList::Size() const noexcept
    {
        return _items.size();
    }

    ModelInstance& EntityBase::ModelList::operator[](std::int32_t index)
    {
        const std::shared_ptr<ModelInstance>& value
            = ManagedReadOnlyListAt(_items, index);
        return RequireReference(value);
    }

    const ModelInstance& EntityBase::ModelList::operator[](std::int32_t index) const
    {
        const std::shared_ptr<ModelInstance>& value
            = ManagedReadOnlyListAt(_items, index);
        return RequireReference(value);
    }

    void EntityBase::ModelList::Clear() noexcept
    {
        _items.clear();
    }

    void EntityBase::ModelList::Add(std::shared_ptr<ModelInstance> value)
    {
        _items.push_back(std::move(value));
    }

    const std::vector<std::shared_ptr<ModelInstance>>& EntityBase::ModelList::Items() const noexcept
    {
        return _items;
    }

    EntityBase::EntityBase(EntityType type, Scene* scene)
        : Type(type),
          _scene(scene),
          Transform(this),
          Scale(this, VectorPropertyKind::Scale),
          Rotation(this, VectorPropertyKind::Rotation),
          Position(this, VectorPropertyKind::Position)
    {
    }

    EntityBase::EntityBase(EntityType type, std::string nodeName, Scene* scene)
        : Type(type),
          _scene(scene),
          _nodeName(std::move(nodeName)),
          Transform(this),
          Scale(this, VectorPropertyKind::Scale),
          Rotation(this, VectorPropertyKind::Rotation),
          Position(this, VectorPropertyKind::Position)
    {
    }

    EntityBase::EntityBase(EntityType type, MphRead::Formats::Culling::NodeRef nodeRef, Scene* scene)
        : Type(type),
          NodeRef(std::move(nodeRef)),
          _scene(scene),
          Transform(this),
          Scale(this, VectorPropertyKind::Scale),
          Rotation(this, VectorPropertyKind::Rotation),
          Position(this, VectorPropertyKind::Position)
    {
    }

    EntityBase::~EntityBase() = default;

    std::int32_t EntityBase::Recolor() const
    {
        return _recolor;
    }

    void EntityBase::SetRecolor(std::int32_t value)
    {
        _recolor = value;
    }

    void EntityBase::SetTransformProperty(Matrix4 value)
    {
        if (!Equal(_transform, value))
        {
            _scale = value.ExtractScale();
            _rotation = ExtractEulerAngles(value);
            _position = Vector3(value.M41, value.M42, value.M43);
            _transform = value;
            _drawColUpdated = false;
        }
    }

    void EntityBase::SetScaleProperty(Vector3 value)
    {
        if (!Equal(_scale, value))
        {
            Matrix4 transform = Multiply(CreateScale(value), CreateRotationZ(Rotation.Z));
            transform = Multiply(transform, CreateRotationY(Rotation.Y));
            transform = Multiply(transform, CreateRotationX(Rotation.X));
            transform.M41 = Position.X;
            transform.M42 = Position.Y;
            transform.M43 = Position.Z;
            _transform = transform;
            _scale = value;
            _drawColUpdated = false;
        }
    }

    void EntityBase::SetRotationProperty(Vector3 value)
    {
        if (!Equal(_rotation, value))
        {
            Matrix4 transform = Multiply(CreateScale(Scale), CreateRotationZ(value.Z));
            transform = Multiply(transform, CreateRotationY(value.Y));
            transform = Multiply(transform, CreateRotationX(value.X));
            transform.M41 = Position.X;
            transform.M42 = Position.Y;
            transform.M43 = Position.Z;
            _transform = transform;
            _rotation = value;
            _drawColUpdated = false;
        }
    }

    void EntityBase::SetPositionProperty(Vector3 value)
    {
        if (!Equal(_position, value))
        {
            Matrix4 transform = _transform;
            transform.M41 = value.X;
            transform.M42 = value.Y;
            transform.M43 = value.Z;
            _transform = transform;
            _position = value;
            _drawColUpdated = false;
        }
    }

    Matrix4 EntityBase::CollisionTransform() const
    {
        return _colAttachNode == nullptr ? _transform : _colAttachNode->Animation;
    }

    Vector3 EntityBase::RightVector() const
    {
        return Vector3(Transform.M11, Transform.M12, Transform.M13).Normalized();
    }

    Vector3 EntityBase::UpVector() const
    {
        return Vector3(Transform.M21, Transform.M22, Transform.M23).Normalized();
    }

    Vector3 EntityBase::FacingVector() const
    {
        return Vector3(Transform.M31, Transform.M32, Transform.M33).Normalized();
    }

    bool EntityBase::UseNodeTransform() const
    {
        return true;
    }

    std::optional<Vector4> EntityBase::OverrideColor() const
    {
        return std::nullopt;
    }

    std::optional<Vector4> EntityBase::PaletteOverride() const
    {
        return _paletteOverride;
    }

    void EntityBase::SetPaletteOverride(std::optional<Vector4> value)
    {
        _paletteOverride = value;
    }

    void EntityBase::Initialize()
    {
        bool anyLighting = false;
        for (const std::shared_ptr<ModelInstance>& instValue : _models.Items())
        {
            ModelInstance& inst = RequireReference(instValue);
            const std::shared_ptr<Model> modelValue = inst.Model();
            Model& model = RequireReference(modelValue);
            const auto& materials = RequireReference(model.Materials);
            for (const std::shared_ptr<Material>& materialValue : materials)
            {
                Material& material = RequireReference(materialValue);
                if (material.Lighting != 0)
                {
                    anyLighting = true;
                    break;
                }
            }
            if (anyLighting)
            {
                break;
            }
        }
        _anyLighting = _anyLighting | anyLighting;
        if (_nodeName.has_value())
        {
            NodeRef = RequireReference(_scene).GetNodeRefByName(*_nodeName);
        }
    }

    ModelInstance& EntityBase::SetUpModel(
        std::string name, std::int32_t animIndex, AnimFlags animFlags, bool firstHunt)
    {
        std::shared_ptr<ModelInstance> inst = Read::GetModelInstance(name, firstHunt);
        if (inst == nullptr)
        {
            throw System::NullReferenceException();
        }
        inst->SetAnimation(animIndex, animFlags);
        _models.Add(inst);
        return *inst;
    }

    void EntityBase::SetCollision(
        const std::shared_ptr<MphRead::Formats::Collision::CollisionInstance>& collision,
        std::int32_t slot, ModelInstance* attach)
    {
        auto entCol = std::make_shared<MphRead::Formats::Collision::EntityCollision>(
            collision, this);
        SetCollisionMaxAvg(*entCol);
        ManagedAt(EntityCollision, slot) = entCol;
        _drawColUpdated = false;
        UpdateCollisionTransform(slot, ClearScale(Transform));
        UpdateLinkedInverse(slot);
        if (entCol->Collision != nullptr)
        {
            auto& info = RequireReference(entCol->Collision->Info);
            const auto& points = RequireReference(info.Points);
            auto& drawPoints = RequireReference(entCol->DrawPoints);
            const std::int32_t count = static_cast<std::int32_t>(points.size());
            for (std::int32_t i = 0; i < count; ++i)
            {
                drawPoints.push_back(ManagedReadOnlyListAt(points, i));
            }
        }
        if (attach != nullptr)
        {
            const std::shared_ptr<Model> modelValue = attach->Model();
            Model& model = RequireReference(modelValue);
            _colAttachNode = model.GetNodeByName("attach");
        }
    }

    void EntityBase::SetCollisionMaxAvg(
        MphRead::Formats::Collision::EntityCollision& entCol)
    {
        if (entCol.Collision == nullptr)
        {
            return;
        }
        auto& info = RequireReference(entCol.Collision->Info);
        const auto& points = RequireReference(info.Points);
        const std::int32_t count = static_cast<std::int32_t>(points.size());
        Vector3 average = Vector3::Zero;
        for (std::int32_t i = 0; i < count; ++i)
        {
            const Vector3 point = ManagedReadOnlyListAt(points, i);
            average.X += point.X;
            average.Y += point.Y;
            average.Z += point.Z;
        }
        average = Divide(average, static_cast<float>(count));
        entCol.InitialCenter = average;
        float maxDistance = 0.0F;
        for (std::int32_t i = 0; i < count; ++i)
        {
            const Vector3 point = ManagedReadOnlyListAt(points, i);
            const float distance = Vector3::Distance(average, point);
            if (distance > maxDistance)
            {
                maxDistance = distance;
            }
        }
        entCol.MaxDistance = maxDistance;
    }

    void EntityBase::UpdateCollisionTransform(std::int32_t slot, Matrix4 transform)
    {
        std::shared_ptr<MphRead::Formats::Collision::EntityCollision>& entCol
            = ManagedAt(EntityCollision, slot);
        if (entCol != nullptr)
        {
            entCol->Transform = transform;
            entCol->Inverse1 = Inverted(transform);
            entCol->CurrentCenter = Matrix::Vec3MultMtx4(
                entCol->InitialCenter, transform);
        }
    }

    void EntityBase::UpdateLinkedInverse(std::int32_t slot)
    {
        std::shared_ptr<MphRead::Formats::Collision::EntityCollision>& entCol
            = ManagedAt(EntityCollision, slot);
        if (entCol != nullptr)
        {
            entCol->Inverse2 = Inverted(entCol->Transform);
        }
    }

    void EntityBase::UpdateDrawCollision()
    {
        if (!_drawColUpdated || _colAttachNode != nullptr)
        {
            const Matrix4 transform = CollisionTransform();
            for (std::int32_t i = 0; i < 2; ++i)
            {
                const std::shared_ptr<MphRead::Formats::Collision::EntityCollision>& entCol
                    = EntityCollision[static_cast<std::size_t>(i)];
                if (entCol != nullptr && entCol->Collision != nullptr)
                {
                    auto& info = RequireReference(entCol->Collision->Info);
                    const auto& points = RequireReference(info.Points);
                    auto& drawPoints = RequireReference(entCol->DrawPoints);
                    const std::int32_t count = static_cast<std::int32_t>(points.size());
                    for (std::int32_t j = 0; j < count; ++j)
                    {
                        ManagedListAt(drawPoints, j) = Matrix::Vec3MultMtx4(
                            ManagedReadOnlyListAt(points, j), transform);
                    }
                }
            }
            _drawColUpdated = true;
        }
    }

    void EntityBase::Destroy()
    {
    }

    Matrix4 EntityBase::GetModelTransform(ModelInstance& inst, std::int32_t index)
    {
        (void)index;
        const std::shared_ptr<Model> modelValue = inst.Model();
        Model& model = RequireReference(modelValue);
        return Multiply(CreateScale(model.Scale), _transform);
    }

    void EntityBase::GetPosition(Vector3& position)
    {
        position = Position;
    }

    void EntityBase::GetVectors(Vector3& position, Vector3& up, Vector3& facing)
    {
        position = Position;
        up = UpVector();
        facing = FacingVector();
    }

    bool EntityBase::GetTargetable()
    {
        return true;
    }

    std::int32_t EntityBase::GetScanId(bool alternate)
    {
        (void)alternate;
        return _scanId;
    }

    void EntityBase::OnScanned()
    {
    }

    bool EntityBase::Process()
    {
        if (Active)
        {
            for (std::int32_t i = 0;
                i < static_cast<std::int32_t>(_models.Size()); ++i)
            {
                const std::shared_ptr<ModelInstance>& instValue
                    = ManagedReadOnlyListAt(_models.Items(), i);
                Scene& scene = RequireReference(_scene);
                if (scene.FrameCount() != 0 && scene.FrameCount() % 2 == 0)
                {
                    RequireReference(instValue).UpdateAnimFrames();
                }
            }
        }
        return true;
    }

    void EntityBase::UpdateAnimFrames(ModelInstance& inst)
    {
        Scene& scene = RequireReference(_scene);
        if (scene.FrameCount() != 0 && scene.FrameCount() % 2 == 0)
        {
            inst.UpdateAnimFrames();
        }
    }

    std::int32_t EntityBase::GetModelRecolor(ModelInstance& inst, std::int32_t index)
    {
        (void)inst;
        (void)index;
        return Recolor();
    }

    const std::vector<std::shared_ptr<ModelInstance>>& EntityBase::GetModels() const
    {
        return _models.Items();
    }

    void EntityBase::AddPlaceholderModel()
    {
        std::shared_ptr<ModelInstance> inst = Read::GetModelInstance("pick_wpn_missile", false);
        if (inst == nullptr)
        {
            throw System::NullReferenceException();
        }
        inst->IsPlaceholder = true;
        _models.Add(std::move(inst));
    }

    std::optional<Vector4> EntityBase::GetOverrideColor(ModelInstance& inst, std::int32_t index)
    {
        (void)inst;
        (void)index;
        return OverrideColor();
    }

    LightInfo EntityBase::GetLightInfo()
    {
        Scene& scene = RequireReference(_scene);
        const Vector3 light1Vector = scene.Light1Vector();
        const Vector3 light1Color = scene.Light1Color();
        const Vector3 light2Vector = scene.Light2Vector();
        const Vector3 light2Color = scene.Light2Color();
        return LightInfo(light1Vector, light1Color, light2Vector, light2Color);
    }

    std::optional<std::int32_t> EntityBase::GetBindingOverride(
        ModelInstance& inst, Material& material, std::int32_t index)
    {
        (void)inst;
        (void)material;
        (void)index;
        return std::nullopt;
    }

    void EntityBase::UpdateTransforms(ModelInstance& inst, std::int32_t index)
    {
        const std::shared_ptr<Model> modelValue = inst.Model();
        Model& model = RequireReference(modelValue);
        model.AnimateMaterials(inst.AnimInfo);
        model.AnimateTextures(inst.AnimInfo);
        model.ComputeNodeMatrices(0);
        const Matrix4 transform = GetModelTransform(inst, index);
        const bool useNodeTransform = UseNodeTransform()
            || RequireReference(_scene).TransformRoomNodes();
        model.AnimateNodes(0, useNodeTransform, transform, model.Scale, inst.AnimInfo);
        model.UpdateMatrixStack();
        Scene* scene = _scene;
        const std::int32_t modelRecolor = GetModelRecolor(inst, index);
        RequireReference(scene).UpdateMaterials(modelValue, modelRecolor);
        if (RequireReference(_scene).ShowCollision())
        {
            UpdateDrawCollision();
        }
    }

    void EntityBase::UpdateTransforms(
        ModelInstance& inst, Matrix4 transform, std::int32_t recolor)
    {
        const std::shared_ptr<Model> modelValue = inst.Model();
        Model& model = RequireReference(modelValue);
        model.AnimateMaterials(inst.AnimInfo);
        model.AnimateTextures(inst.AnimInfo);
        model.ComputeNodeMatrices(0);
        const bool useNodeTransform = UseNodeTransform();
        model.AnimateNodes(0, useNodeTransform, transform, model.Scale, inst.AnimInfo);
        model.UpdateMatrixStack();
        RequireReference(_scene).UpdateMaterials(modelValue, recolor);
    }

    void EntityBase::UpdateMaterials(ModelInstance& inst, std::int32_t recolor)
    {
        const std::shared_ptr<Model> modelValue = inst.Model();
        Model& model = RequireReference(modelValue);
        model.AnimateMaterials(inst.AnimInfo);
        model.AnimateTextures(inst.AnimInfo);
        RequireReference(_scene).UpdateMaterials(modelValue, recolor);
    }

    void EntityBase::GetDrawItems(
        ModelInstance& inst, std::int32_t i, std::optional<LightInfo> lightInfo)
    {
        const std::int32_t polygonId = RequireReference(_scene).GetNextPolygonId();

        const std::shared_ptr<Model> rootModelValue = inst.Model();
        Model& rootModel = RequireReference(rootModelValue);
        const auto& rootNodes = RequireReference(rootModel.Nodes);
        const std::shared_ptr<Node>& rootNodeValue = ManagedReadOnlyListAt(rootNodes, 0);
        Node& rootNode = RequireReference(rootNodeValue);

        auto getItems = [&](auto&& self, Node& node) -> void
        {
            const std::shared_ptr<Model> modelValue = inst.Model();
            if (node.Enabled)
            {
                const std::int32_t start = node.MeshId / 2;
                for (std::int32_t k = 0; k < node.MeshCount; ++k)
                {
                    Model& model = RequireReference(modelValue);
                    const auto& meshes = RequireReference(model.Meshes);
                    const std::shared_ptr<Mesh>& meshValue
                        = ManagedReadOnlyListAt(meshes, start + k);
                    Mesh& mesh = RequireReference(meshValue);
                    if (!mesh.Visible)
                    {
                        continue;
                    }
                    const auto& materials = RequireReference(model.Materials);
                    const std::shared_ptr<Material>& materialValue
                        = ManagedReadOnlyListAt(materials, mesh.MaterialId);
                    Material& material = RequireReference(materialValue);
                    const Vector3 emission = GetEmission(
                        inst, material, mesh.MaterialId);
                    const Matrix4 texcoordMatrix = GetTexcoordMatrix(
                        inst, material, mesh.MaterialId, node);
                    const std::optional<Vector4> color = inst.IsPlaceholder
                        ? GetOverrideColor(inst, i) : std::nullopt;
                    const SelectionType selectionType
                        = Selection::CheckSelection(this, inst, node, mesh);
                    const std::optional<std::int32_t> bindingOverride
                        = GetBindingOverride(inst, material, mesh.MaterialId);

                    Scene* renderScene = _scene;
                    const float alpha = Alpha;
                    const LightInfo resolvedLightInfo = lightInfo.has_value()
                        ? *lightInfo : GetLightInfo();
                    const Matrix4 nodeAnimation = node.Animation;
                    const auto& nodeMatrixIds
                        = RequireReference(model.NodeMatrixIds);
                    const std::int32_t matrixStackCount
                        = static_cast<std::int32_t>(nodeMatrixIds.size());
                    const std::shared_ptr<const ManagedArray<float>>
                        matrixStackValues = model.MatrixStackValues;
                    const std::optional<Vector4> paletteOverride
                        = PaletteOverride();
                    const BillboardMode billboardMode = node.BillboardMode;
                    const float drawScale = _drawScale;
                    const std::vector<float> matrixStack
                        = CopyManagedArray(RequireReference(matrixStackValues));

                    RequireReference(renderScene).AddRenderItem(
                        material, polygonId, alpha, emission, resolvedLightInfo,
                        texcoordMatrix, nodeAnimation, modelValue, meshValue,
                        matrixStackCount, matrixStack, color, paletteOverride, selectionType,
                        billboardMode, drawScale, bindingOverride);
                }
                if (node.ChildIndex != -1)
                {
                    Model& model = RequireReference(modelValue);
                    const auto& nodes = RequireReference(model.Nodes);
                    const std::shared_ptr<Node>& childValue
                        = ManagedReadOnlyListAt(nodes, node.ChildIndex);
                    Node& child = RequireReference(childValue);
                    self(self, child);
                }
            }
            if (node.NextIndex != -1)
            {
                Model& model = RequireReference(modelValue);
                const auto& nodes = RequireReference(model.Nodes);
                const std::shared_ptr<Node>& nextValue
                    = ManagedReadOnlyListAt(nodes, node.NextIndex);
                Node& next = RequireReference(nextValue);
                self(self, next);
            }
        };
        getItems(getItems, rootNode);
    }

    void EntityBase::UpdateNodeRefVolume()
    {
        _soundSource.Volume = IsAudible(NodeRef) ? 1.0F : 0.0F;
    }

    bool EntityBase::IsAudible(MphRead::Formats::Culling::NodeRef nodeRef)
    {
        if (nodeRef == MphRead::Formats::Culling::NodeRef::None
            || RequireReference(_scene).CameraMode() != CameraMode::Player)
        {
            return true;
        }
        return RequireReference(_scene).IsNodeRefAudible(nodeRef);
    }

    bool EntityBase::IsVisible(MphRead::Formats::Culling::NodeRef nodeRef)
    {
        if (nodeRef == MphRead::Formats::Culling::NodeRef::None
            || RequireReference(_scene).CameraMode() != CameraMode::Player
            || RequireReference(_scene).ShowInvisibleEntities()
            || MphRead::Mods::Network::DemoPlayback::IsActive())
        {
            return true;
        }
        return RequireReference(_scene).IsNodeRefVisible(nodeRef);
    }

    bool EntityBase::ScanVisible()
    {
        return IsVisible(NodeRef);
    }

    void EntityBase::GetDrawInfo()
    {
        for (std::int32_t i = 0;
            i < static_cast<std::int32_t>(_models.Size()); ++i)
        {
            ModelInstance& inst = _models[i];
            if ((!inst.Active && !RequireReference(_scene).ShowAllEntities())
                || (inst.IsPlaceholder
                    && !RequireReference(_scene).ShowInvisibleEntities()
                    && !RequireReference(_scene).ShowAllEntities()))
            {
                continue;
            }
            UpdateTransforms(inst, i);
            if (!Hidden)
            {
                GetDrawItems(inst, i);
            }
        }
        if (RequireReference(_scene).ShowCollision()
            && (RequireReference(_scene).ColEntDisplay() == EntityType::All
                || RequireReference(_scene).ColEntDisplay() == Type))
        {
            GetCollisionDrawInfo();
        }
    }

    void EntityBase::GetCollisionDrawInfo()
    {
        for (std::size_t i = 0; i < 2; ++i)
        {
            const std::shared_ptr<MphRead::Formats::Collision::EntityCollision>& entCol
                = EntityCollision[i];
            if (entCol != nullptr && entCol->Collision != nullptr
                && entCol->Collision->Active)
            {
                auto& info = RequireReference(entCol->Collision->Info);
                info.GetDrawInfo(entCol->DrawPoints, Vector3::Zero, Type, _scene);
            }
        }
    }

    Vector3 EntityBase::GetEmission(ModelInstance& inst, Material& material, std::int32_t index)
    {
        (void)inst;
        (void)material;
        (void)index;
        return Vector3::Zero;
    }

    Matrix4 EntityBase::GetTexcoordMatrix(
        ModelInstance& inst, Material& material, std::int32_t materialId,
        Node& node, std::int32_t recolor)
    {
        (void)materialId;
        (void)node;
        const std::shared_ptr<Model> modelValue = inst.Model();
        Matrix4 texcoordMatrix = IdentityMatrix();

        AnimationInfo& animInfo = RequireReference(inst.AnimInfo);
        TexcoordAnimationInfo& texcoordInfo = RequireReference(animInfo.Texcoord);
        const std::shared_ptr<TexcoordAnimationGroup> group = texcoordInfo.Group;
        std::shared_ptr<const TexcoordAnimationDictionary> animationsValue;
        const TexcoordAnimation* animation = nullptr;
        if (group != nullptr)
        {
            animationsValue = group->Animations;
            const auto& animations = RequireReference(animationsValue);
            const auto found = animations.find(material.Name);
            if (found != animations.end())
            {
                animation = &found->second;
            }
        }
        if (group != nullptr && animation != nullptr)
        {
            const std::shared_ptr<Model> currentModelValue = inst.Model();
            Model& currentModel = RequireReference(currentModelValue);
            if (!currentModel.FirstHunt
                || material.TexgenMode != TexgenMode::None)
            {
                const std::int32_t texcoordFrame = animInfo.TexcoordFrame();
                texcoordMatrix = RequireReference(modelValue).AnimateTexcoords(
                    group, *animation, texcoordFrame);
            }
        }
        if (material.TexgenMode != TexgenMode::None)
        {
            Model& model = RequireReference(modelValue);
            Matrix4 materialMatrix;
            const auto& textureMatrices
                = RequireReference(model.TextureMatrices);
            if (!textureMatrices.empty())
            {
                materialMatrix = ManagedReadOnlyListAt(
                    textureMatrices, material.MatrixId);
            }
            else
            {
                materialMatrix = CreateTranslation(
                    material.ScaleS * material.TranslateS,
                    material.ScaleT * material.TranslateT, 0.0F);
                materialMatrix = Multiply(
                    CreateScale(material.ScaleS, material.ScaleT, 1.0F),
                    materialMatrix);
                materialMatrix = Multiply(
                    CreateRotationZ(material.RotateZ), materialMatrix);
            }
            if (group == nullptr || animation == nullptr)
            {
                texcoordMatrix = materialMatrix;
            }
            if (material.TexgenMode == TexgenMode::Normal)
            {
                const auto recolorsValue = model.Recolors;
                const std::int32_t recolorIndex
                    = recolor == -1 ? Recolor() : recolor;
                const auto& recolors = RequireReference(recolorsValue);
                const std::shared_ptr<MphRead::Recolor>& recolorValue
                    = ManagedReadOnlyListAt(recolors, recolorIndex);
                MphRead::Recolor& recolorEntry
                    = RequireReference(recolorValue);
                const auto& textures = RequireReference(recolorEntry.Textures);
                const Texture& texture = ManagedReadOnlyListAt(
                    textures, material.TextureId);

                Matrix4 texgenMatrix = IdentityMatrix();
                if (model.Scale.X != 1.0F || model.Scale.Y != 1.0F
                    || model.Scale.Z != 1.0F)
                {
                    texgenMatrix = CreateScale(model.Scale);
                }
                Matrix4 product = texgenMatrix;
                product.M12 *= -1.0F;
                product.M13 *= -1.0F;
                product.M22 *= -1.0F;
                product.M23 *= -1.0F;
                product.M32 *= -1.0F;
                product.M33 *= -1.0F;
                product = Multiply(product, materialMatrix);
                product = Multiply(product, texcoordMatrix);
                product = Multiply(
                    product, 1.0F / static_cast<float>(texture.Width / 2));
                texcoordMatrix = Matrix4(
                    Vector4(product.M11 * 16.0F, product.M12 * 16.0F,
                        product.M13 * 16.0F, product.M14 * 16.0F),
                    Vector4(product.M21 * 16.0F, product.M22 * 16.0F,
                        product.M23 * 16.0F, product.M24 * 16.0F),
                    Vector4(product.M31 * 16.0F, product.M32 * 16.0F,
                        product.M33 * 16.0F, product.M34 * 16.0F),
                    product.Row3());
            }
        }
        return texcoordMatrix;
    }

    void EntityBase::SetTransform(Vector3Fx facing, Vector3Fx up, Vector3Fx position)
    {
        SetTransform(facing.ToFloatVector(), up.ToFloatVector(), position.ToFloatVector());
    }

    void EntityBase::SetTransform(Vector3 facing, Vector3 up, Vector3 position)
    {
        Matrix4 transform = GetTransformMatrix(facing, up);
        transform = Multiply(CreateScale(_scale), transform);
        transform.M41 = position.X;
        transform.M42 = position.Y;
        transform.M43 = position.Z;
        Transform = transform;
    }

    Matrix4 EntityBase::GetTransformMatrix(Vector3 facing, Vector3 up)
    {
        const Vector3 right = Vector3::Cross(up, facing).Normalized();
        up = Vector3::Cross(facing, right);
        Matrix4 transform{};
        transform.M11 = right.X;
        transform.M12 = right.Y;
        transform.M13 = right.Z;
        transform.M14 = 0.0F;
        transform.M21 = up.X;
        transform.M22 = up.Y;
        transform.M23 = up.Z;
        transform.M24 = 0.0F;
        transform.M31 = facing.X;
        transform.M32 = facing.Y;
        transform.M33 = facing.Z;
        transform.M34 = 0.0F;
        transform.M41 = 0.0F;
        transform.M42 = 0.0F;
        transform.M43 = 0.0F;
        transform.M44 = 1.0F;
        return transform;
    }

    Matrix4 EntityBase::GetTransformMatrix(Vector3 facing, Vector3 up, Vector3 position)
    {
        Matrix4 transform = GetTransformMatrix(facing, up);
        transform.M41 = position.X;
        transform.M42 = position.Y;
        transform.M43 = position.Z;
        return transform;
    }

    void EntityBase::AddDotItem(Vector3 position, Vector3 color)
    {
        AddVolumeItem(CollisionVolume(position, 0.1F), color, 1.0F);
    }

    void EntityBase::AddVolumeItem(CollisionVolume volume, Vector3 color, float alpha)
    {
        if (!Selection::CheckVolume(this))
        {
            return;
        }
        std::shared_ptr<ManagedArray<Vector3>> verts = ManagedArray<Vector3>::Empty();
        if (volume.Type == VolumeType::Box)
        {
            verts = MphRead::NativeRuntime::RentFromSharedArrayPool(8);
            const Vector3 point0 = volume.BoxPosition;
            const Vector3 sideX = Multiply(volume.BoxVector1, volume.BoxDot1);
            const Vector3 sideY = Multiply(volume.BoxVector2, volume.BoxDot2);
            const Vector3 sideZ = Multiply(volume.BoxVector3, volume.BoxDot3);
            (*verts)[0] = point0;
            (*verts)[1] = Add(point0, sideZ);
            (*verts)[2] = Add(point0, sideX);
            (*verts)[3] = Add(Add(point0, sideX), sideZ);
            (*verts)[4] = Add(point0, sideY);
            (*verts)[5] = Add(Add(point0, sideY), sideZ);
            (*verts)[6] = Add(Add(point0, sideX), sideY);
            (*verts)[7] = Add(Add(Add(point0, sideX), sideY), sideZ);
        }
        else if (volume.Type == VolumeType::Cylinder)
        {
            verts = MphRead::NativeRuntime::RentFromSharedArrayPool(34);
            const Vector3 vector = volume.CylinderVector.Normalized();
            const float radius = volume.CylinderRadius;
            const Matrix3 rotation = Matrix::RotateAlign(Vector3(0.0F, 1.0F, 0.0F), vector);
            Vector3 start;
            Vector3 end;
            if (Equal(vector, Vector3(1.0F, 0.0F, 0.0F))
                || Equal(vector, Vector3(0.0F, 1.0F, 0.0F))
                || Equal(vector, Vector3(0.0F, 0.0F, 1.0F)))
            {
                start = volume.CylinderPosition;
                end = Add(volume.CylinderPosition, Multiply(vector, volume.CylinderDot));
            }
            else
            {
                start = Add(volume.CylinderPosition, Multiply(vector, volume.CylinderDot));
                end = volume.CylinderPosition;
            }
            for (std::size_t i = 0; i < 16; i++)
            {
                (*verts)[i] = Add(Multiply(GetDiscVertices(radius, static_cast<std::int32_t>(i)), rotation), start);
            }
            for (std::size_t i = 0; i < 16; i++)
            {
                (*verts)[i + 16] = Add(Multiply(GetDiscVertices(radius, static_cast<std::int32_t>(i)), rotation), end);
            }
            (*verts)[32] = start;
            (*verts)[33] = end;
        }
        else if (volume.Type == VolumeType::Sphere)
        {
            constexpr std::int32_t stackCount = Scene::DisplaySphereStacks;
            constexpr std::int32_t sectorCount = Scene::DisplaySphereSectors;
            verts = MphRead::NativeRuntime::RentFromSharedArrayPool((stackCount + 1) * (sectorCount + 1));
            const float radius = volume.SphereRadius;
            const float pi = std::acos(-1.0F);
            const float sectorStep = 2.0F * pi / static_cast<float>(sectorCount);
            const float stackStep = pi / static_cast<float>(stackCount);
            for (std::int32_t i = 0; i <= stackCount; i++)
            {
                const float stackAngle = pi / 2.0F - static_cast<float>(i) * stackStep;
                const float xy = radius * std::cos(stackAngle);
                const float z = radius * std::sin(stackAngle);
                for (std::int32_t j = 0; j <= sectorCount; j++)
                {
                    const float sectorAngle = static_cast<float>(j) * sectorStep;
                    const float x = xy * std::cos(sectorAngle);
                    const float y = xy * std::sin(sectorAngle);
                    (*verts)[static_cast<std::size_t>(i * (sectorCount + 1) + j)]
                        = Add(Vector3(x, z, y), volume.SpherePosition);
                }
            }
        }
        Scene& scene = RequireReference(_scene);
        const CullingMode cullingMode = volume.TestPoint(scene.CameraPosition())
            ? CullingMode::Front : CullingMode::Back;
        scene.AddRenderItem(cullingMode, scene.GetNextPolygonId(),
            Vector4(color, alpha), static_cast<RenderItemType>(static_cast<std::int32_t>(volume.Type) + 1),
            verts);
    }

    Vector3 EntityBase::GetDiscVertices(float radius, std::int32_t index)
    {
        const float pi = std::acos(-1.0F);
        return Vector3(
            radius * std::cos(2.0F * pi * static_cast<float>(index) / 16.0F),
            0.0F,
            radius * std::sin(2.0F * pi * static_cast<float>(index) / 16.0F));
    }

    void EntityBase::AddVectorItem(Vector3 point, Vector3 vector, Vector3 color)
    {
        CollisionVolume volume;
        if (Equal(vector, Vector3::Zero))
        {
            volume = CollisionVolume(point, 0.1F);
        }
        else
        {
            volume = CollisionVolume(vector.Normalized(), point, 0.05F, Length(vector));
        }
        AddVolumeItem(volume, color);
    }

    void EntityBase::GetDisplayVolumes()
    {
    }

    void EntityBase::SetActive(bool active)
    {
        Active = active;
    }

    void EntityBase::SetScanId(std::int32_t scanId)
    {
        _scanId = scanId;
    }

    EntityBase* EntityBase::GetParent()
    {
        return nullptr;
    }

    EntityBase* EntityBase::GetChild()
    {
        return nullptr;
    }

    void EntityBase::HandleMessage(MessageInfo info)
    {
        (void)info;
    }

    void EntityBase::CheckContactDamage(DamageResult& result)
    {
        (void)result;
    }

    void EntityBase::CheckBeamReflection(bool& result)
    {
        (void)result;
    }

    std::tuple<float, float> EntityBase::ConstantAcceleration(
        float step, float velocity, float minVelocity, float maxVelocity)
    {
        float newVelocity = velocity + step * 30.0F * 31.0F
            * RequireReference(_scene).FrameTime();
        newVelocity = MathClamp(newVelocity, minVelocity, maxVelocity);
        const float displacement = velocity * RequireReference(_scene).FrameTime()
            + (newVelocity - velocity) / 2.0F
                * RequireReference(_scene).FrameTime();
        return {newVelocity, displacement};
    }

    std::tuple<float, float> EntityBase::Drag(float step, float velocity)
    {
        const float decay = std::pow(step, 30.0F);
        const float newVelocity = velocity
            * std::pow(decay, RequireReference(_scene).FrameTime());
        const float displacement = (newVelocity - velocity) / std::log(decay);
        return {newVelocity, displacement};
    }

    float EntityBase::ExponentialDecay(float step, float value)
    {
        const float decay = std::pow(step, 30.0F);
        return value * std::pow(
            decay, RequireReference(_scene).FrameTime());
    }

    ModelEntity::ModelEntity(std::shared_ptr<ModelInstance> model, Scene* scene, std::int32_t recolor)
        : EntityBase(EntityType::Model, scene)
    {
        SetRecolor(recolor);
        _models.Add(model);
        if (model == nullptr)
        {
            throw System::NullReferenceException();
        }
        model->SetAnimation(0);
    }
}
