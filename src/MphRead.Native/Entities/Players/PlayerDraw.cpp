#include "PlayerDraw.hpp"

#include "../../NativeRuntime/System/Buffers.hpp"

#include "../../Features.hpp"
#include "../../Formats/CollisionDetection.hpp"
#include "../../GameState.hpp"
#include "Mods/SpectatorMode.hpp"
#include "../../Metadata/Metadata.hpp"
#include "../../Renderer.hpp"
#include "../../Scene.hpp"
#include "../CamSeq/CameraSequence.hpp"
#include "PlayerEntity.hpp"
#include "../../NativeRuntime/System/Managed.hpp"
#include "../../Formats/Types.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

using ::MphRead::NativeRuntime::ManagedAt;
using ::MphRead::NativeRuntime::RequireReference;
using ::MphRead::TestFlag;
using ::OpenTK::Mathematics::CreateRotationY;
using ::OpenTK::Mathematics::CreateRotationZ;
using ::OpenTK::Mathematics::CreateScale;
using ::OpenTK::Mathematics::IdentityMatrix;
using ::OpenTK::Mathematics::LengthSquared;
using ::OpenTK::Mathematics::MathHelper::DegreesToRadians;
using ::OpenTK::Mathematics::Multiply;
using ::OpenTK::Mathematics::Negate;
using ::OpenTK::Mathematics::ScaleVector;
using ::OpenTK::Mathematics::SetRow3;

namespace
{
    using MphRead::ManagedArray;
    using OpenTK::Mathematics::Matrix4;
    using OpenTK::Mathematics::Vector3;
    using OpenTK::Mathematics::Vector4;

    template <typename TEnum>
    [[nodiscard]] constexpr TEnum AddFlag(TEnum value, TEnum flag) noexcept
    {
        using U = std::underlying_type_t<TEnum>;
        return static_cast<TEnum>(static_cast<U>(value) | static_cast<U>(flag));
    }

    template <typename TEnum>
    [[nodiscard]] constexpr TEnum RemoveFlag(TEnum value, TEnum flag) noexcept
    {
        using U = std::underlying_type_t<TEnum>;
        return static_cast<TEnum>(static_cast<U>(value) & ~static_cast<U>(flag));
    }

    template <typename T>
    [[nodiscard]] T* ObjectPointer(T* value) noexcept
    {
        return value;
    }

    template <typename T>
    [[nodiscard]] T* ObjectPointer(const std::shared_ptr<T>& value) noexcept
    {
        return value.get();
    }

    template <typename T>
    [[nodiscard]] T* ObjectPointer(T& value) noexcept
    {
        return std::addressof(value);
    }

    template <typename T>
    [[nodiscard]] decltype(auto) Storage(T& value)
    {
        if constexpr (std::is_pointer_v<std::remove_reference_t<T>>)
        {
            return RequireReference(value);
        }
        else if constexpr (requires { value.get(); })
        {
            return RequireReference(value);
        }
        else
        {
            return (value);
        }
    }

    template <typename T>
    [[nodiscard]] std::int32_t ManagedLength(T& values)
    {
        auto&& storage = Storage(values);
        if constexpr (requires { storage.Length(); })
        {
            return static_cast<std::int32_t>(storage.Length());
        }
        else if constexpr (requires { storage.size(); })
        {
            return static_cast<std::int32_t>(storage.size());
        }
        else
        {
            return static_cast<std::int32_t>(std::size(storage));
        }
    }

    [[nodiscard]] constexpr Vector3 MatrixRow3(const Matrix4& matrix) noexcept
    {
        return Vector3(matrix.M41, matrix.M42, matrix.M43);
    }

    void SetRow0(Matrix4& matrix, Vector3 value) noexcept
    {
        matrix.M11 = value.X; matrix.M12 = value.Y; matrix.M13 = value.Z;
    }

    void SetRow1(Matrix4& matrix, Vector3 value) noexcept
    {
        matrix.M21 = value.X; matrix.M22 = value.Y; matrix.M23 = value.Z;
    }

    void SetRow2(Matrix4& matrix, Vector3 value) noexcept
    {
        matrix.M31 = value.X; matrix.M32 = value.Y; matrix.M33 = value.Z;
    }

    [[nodiscard]] Vector3 GetRow0(const Matrix4& matrix) noexcept
    {
        return Vector3(matrix.M11, matrix.M12, matrix.M13);
    }

    [[nodiscard]] Vector3 GetRow1(const Matrix4& matrix) noexcept
    {
        return Vector3(matrix.M21, matrix.M22, matrix.M23);
    }

    [[nodiscard]] Vector3 GetRow2(const Matrix4& matrix) noexcept
    {
        return Vector3(matrix.M31, matrix.M32, matrix.M33);
    }

    [[nodiscard]] std::int32_t ManagedFloatToInt32(float value) noexcept
    {
        if (std::isnan(value))
        {
            return 0;
        }
        if (value <= static_cast<float>(std::numeric_limits<std::int32_t>::min()))
        {
            return std::numeric_limits<std::int32_t>::min();
        }
        if (value >= static_cast<float>(std::numeric_limits<std::int32_t>::max()))
        {
            return std::numeric_limits<std::int32_t>::max();
        }
        return static_cast<std::int32_t>(value);
    }

    [[nodiscard]] std::int32_t RotationValue(std::uint64_t value) noexcept
    {
        const std::uint32_t low = static_cast<std::uint32_t>(value);
        const std::int32_t signedValue = std::bit_cast<std::int32_t>(low);
        return signedValue >> 20;
    }
}


namespace MphRead::Entities
{
    using Formats::CameraSequence;
    using Formats::CollisionDetection;
    using Formats::CollisionResult;
    using Formats::TestFlags;

    void PlayerEntity::Draw()
    {
        if (TestFlag(Flags2(), PlayerFlags2::Spectating)
            || MphRead::Mods::SpectatorMode::ShouldHideLocalPlayerModel(SlotIndex()))
        {
            return;
        }
        DrawShadow();
        if (IsMainPlayer() && ScanVisor())
        {
            DrawScanModels();
        }
        if (TestFlag(Flags2(), PlayerFlags2::HideModel))
        {
            return;
        }
        if (Hunter() == MphRead::Hunter::Spire && TestFlag(Flags2(), PlayerFlags2::AltAttack))
        {
            AnimateSpireAltAttack();
        }

        std::int32_t lod = 0;
        SetFlags2(RemoveFlag(Flags2(), PlayerFlags2::Lod1));
        if (!IsMainPlayer() && !Features::MaxPlayerDetail())
        {
            PlayerEntity& main = RequireReference(Main());
            ::MphRead::Entities::CameraInfo& mainCamera = RequireReference(main.CameraInfo());
            if (LengthSquared(static_cast<Vector3>(Position) - mainCamera.Position) >= 9.0F)
            {
                lod = 1;
                SetFlags2(AddFlag(Flags2(), PlayerFlags2::Lod1));
            }
        }

        ModelInstance& biped1 = RequireReference(_bipedModel1.get());
        biped1.SetModel(RequireReference(ManagedAt(_bipedModelLods, lod).get()).Model());
        ModelInstance& biped2 = RequireReference(_bipedModel2.get());
        biped2.SetModel(RequireReference(ManagedAt(_bipedModelLods, lod).get()).Model());
        SetFlags2(RemoveFlag(Flags2(), PlayerFlags2::DrawnThirdPerson));

        bool drawBiped = false;
        if (IsMainPlayer() || IsVisible(NodeRef) || ModNodeUnresolved()
            || ModMayBypassReplicaPartCulling())
        {
            drawBiped = !IsMainPlayer() || CameraType() != Entities::CameraType::First
                || CameraSequence::Current() != nullptr || _camSwitchTimer < Values().CamSwitchTime * 2;
            if (IsAltForm())
            {
                SetRow3(_modelTransform, Position);
                if (_timeSinceDamage < Values().DamageFlashTime * 2)
                {
                    SetPaletteOverride(Metadata::RedPalette);
                }
                if (Hunter() == MphRead::Hunter::Kanden)
                {
                    DrawKandenAlt();
                }
                else if (Hunter() == MphRead::Hunter::Spire
                    && TestFlag(Flags2(), PlayerFlags2::AltAttack))
                {
                    DrawSpireAltAttack();
                }
                else
                {
                    ModelInstance& alt = RequireReference(_altModel.get());
                    UpdateTransforms(alt, _modelTransform, Recolor());
                    Model& altModel = RequireReference(alt.Model());
                    GetDrawItems(alt, RequireReference(ManagedAt(altModel.Nodes, 0)), _curAlpha);
                }
                SetPaletteOverride(std::nullopt);
                if (_frozenGfxTimer > 0)
                {
                    const float radius = _volume.SphereRadius + 0.2F;
                    Matrix4 transform = Multiply(CreateScale(radius), _modelTransform);
                    transform.M42 += Fixed::ToFloat(Values().AltColYPos);
                    ModelInstance& altIce = RequireReference(_altIceModel.get());
                    UpdateTransforms(altIce, transform, 0);
                    Model& altIceModel = RequireReference(altIce.Model());
                    GetDrawItems(altIce, RequireReference(ManagedAt(altIceModel.Nodes, 0)), 1.0F, -1, 0);
                }
                if (Hunter() == MphRead::Hunter::Samus
                    && !TestFlag(Flags2(), PlayerFlags2::Cloaking))
                {
                    DrawMorphBallTrail();
                }
                SetRow3(_modelTransform, Vector3::Zero);
                SetFlags2(AddFlag(Flags2(), PlayerFlags2::DrawnThirdPerson));
            }
            else if (drawBiped)
            {
                Node& spineNode = RequireReference(ManagedAt(_spineNodes, lod).get());
                spineNode.AnimIgnoreChild = true;
                const Vector3 facing = _facingVector;
                const float limit = Fixed::ToFloat(2896);
                float cosValue = std::sqrt(1.0F - facing.Y * facing.Y);
                float sinValue = facing.Y;
                if (std::abs(facing.Y) > limit)
                {
                    cosValue = limit;
                    sinValue = facing.Y <= 0.0F ? -limit : limit;
                }
                const float angle = std::atan2(sinValue, cosValue);
                spineNode.AfterTransform = CreateRotationZ(angle);
                Model& model = RequireReference(biped1.Model());
                model.AnimateNodes(0, false, IdentityMatrix(), Vector3(1.0F, 1.0F, 1.0F), biped1.AnimInfo);
                spineNode.AnimIgnoreChild = false;
                model.AnimateNodes(spineNode.ChildIndex, false, IdentityMatrix(),
                    Vector3(1.0F, 1.0F, 1.0F), biped2.AnimInfo);
                spineNode.AfterTransform.reset();

                const auto scaleIt = Metadata::HunterScales.find(Hunter());
                if (scaleIt == Metadata::HunterScales.end())
                {
                    throw SceneDetail::KeyNotFoundException();
                }
                const float scale = scaleIt->second;
                const float bottom = Fixed::ToFloat(Values().MinPickupHeight);
                const Vector3 lateral(_field70, 0.0F, _field74);
                Matrix4 transform = IdentityMatrix();
                SetRow0(transform, Negate(_gunVec2));
                SetRow1(transform, Vector3::Cross(lateral, _gunVec2));
                SetRow2(transform, Negate(lateral));
                SetRow3(transform, Position);
                transform.M42 += bottom + bottom * (1.0F - scale);
                SetRow0(transform, ScaleVector(GetRow0(transform), scale));
                SetRow1(transform, ScaleVector(GetRow1(transform), scale));
                SetRow2(transform, ScaleVector(GetRow2(transform), scale));
                for (std::int32_t i = 0; i < ManagedLength(model.Nodes); ++i)
                {
                    Node& node = RequireReference(ManagedAt(model.Nodes, i));
                    node.Animation = Multiply(node.Animation, transform);
                }
                model.UpdateMatrixStack();

                if (_health > 0)
                {
                    if (_timeSinceDamage < Values().DamageFlashTime * 2)
                    {
                        SetPaletteOverride(Metadata::RedPalette);
                    }
                    float alpha = _curAlpha;
                    if (IsMainPlayer() && CameraSequence::Current() == nullptr
                        && ManagedAt(biped1.AnimInfo->Index, 0) == static_cast<std::int32_t>(PlayerAnimation::Unmorph))
                    {
                        alpha -= alpha * static_cast<float>(ManagedAt(biped1.AnimInfo->Frame, 0))
                            / static_cast<float>(ManagedAt(biped1.AnimInfo->FrameCount, 0));
                        alpha = std::clamp(alpha, 0.0F, 1.0F);
                    }
                    UpdateMaterials(biped2, Recolor());
                    GetDrawItems(biped2, RequireReference(ManagedAt(model.Nodes, 0)), alpha);
                    SetPaletteOverride(std::nullopt);
                    if (_chargeEffect != nullptr || _muzzleEffect != nullptr)
                    {
                        Vector3 muzzlePos = ManagedAt(Metadata::MuzzleOffests,
                            static_cast<std::int32_t>(Hunter()));
                        muzzlePos = Matrix::Vec3MultMtx4(
                            muzzlePos, RequireReference(ManagedAt(_shootNodes, lod).get()).Animation);
                        if (_chargeEffect != nullptr)
                        {
                            _chargeEffect->SetDrawEnabled(true);
                            _chargeEffect->Transform(_gunVec2, _gunVec1, muzzlePos);
                        }
                        if (_muzzleEffect != nullptr)
                        {
                            _muzzleEffect->SetDrawEnabled(true);
                            _muzzleEffect->Transform(_gunVec2, _gunVec1, muzzlePos);
                        }
                    }
                    if (_frozenGfxTimer > 0)
                    {
                        ModelInstance& bipedIce = RequireReference(_bipedIceModel.get());
                        Model& iceModel = RequireReference(bipedIce.Model());
                        for (std::int32_t i = 0; i < ManagedLength(iceModel.Nodes); ++i)
                        {
                            const Matrix4 animation = RequireReference(ManagedAt(model.Nodes, i)).Animation;
                            RequireReference(ManagedAt(iceModel.Nodes, i)).Animation = animation;
                            ManagedAt(_bipedIceTransforms, i) = animation;
                        }
                        iceModel.UpdateMatrixStack();
                        UpdateMaterials(bipedIce, 0);
                        GetDrawItems(bipedIce, RequireReference(ManagedAt(iceModel.Nodes, 0)), 1.0F, -1, 0);
                    }
                }
                _modelTransform = transform;
                if (_health == 0)
                {
                    DrawDeathParticles();
                }
                SetFlags2(AddFlag(Flags2(), PlayerFlags2::DrawnThirdPerson));
            }
            else if (AttachedEnemy() == nullptr && !_field6D0 && Hunter() != MphRead::Hunter::Guardian)
            {
                Matrix4 transform = GetTransformMatrix(_aimVec, _upVector, _gunDrawPos);
                ModelInstance& gun = RequireReference(_gunModel.get());
                UpdateTransforms(gun, transform, Recolor());
                Model& gunModel = RequireReference(gun.Model());
                GetDrawItems(gun, RequireReference(ManagedAt(gunModel.Nodes, 0)), _curAlpha);
                if (TestFlag(Flags1(), PlayerFlags1::DrawGunSmoke))
                {
                    Vector3 drawPos(0.0F, 0.0F, Fixed::ToFloat(Values().MuzzleOffset));
                    drawPos = Matrix::Vec3MultMtx4(drawPos, transform);
                    SetRow3(transform, drawPos);
                    ModelInstance& smoke = RequireReference(_gunSmokeModel.get());
                    UpdateTransforms(smoke, transform, 0);
                    Model& smokeModel = RequireReference(smoke.Model());
                    GetDrawItems(smoke, RequireReference(ManagedAt(smokeModel.Nodes, 0)), _smokeAlpha, -1, 0);
                }
            }
        }

        if (!IsMainPlayer() && !drawBiped)
        {
            if (_chargeEffect != nullptr)
            {
                _chargeEffect->SetDrawEnabled(false);
            }
            if (_muzzleEffect != nullptr)
            {
                _muzzleEffect->SetDrawEnabled(false);
            }
        }
        if (GameState::SinglePlayer() && IsMainPlayer() && _deathCountdown > 0.0F
            && _deathCountdown <= 119.0F / 30.0F)
        {
            if (GameState::StorySave == nullptr)
            {
                throw System::NullReferenceException();
            }
            if (std::popcount(static_cast<std::uint32_t>(GameState::StorySave->CurrentOctoliths)) > 0)
            {
                Matrix4 transform = IdentityMatrix();
                SetRow3(transform, _lostOctolithDrawPos);
                ModelInstance& octolith = RequireReference(_octolithSimpleModel.get());
                UpdateTransforms(octolith, transform, 0);
                Model& octolithModel = RequireReference(octolith.Model());
                GetDrawItems(octolith, RequireReference(ManagedAt(octolithModel.Nodes, 0)), 1.0F, -1, 0);
            }
        }
        DrawVolumes();
    }

    void PlayerEntity::DrawKandenAlt()
    {
        ModelInstance& alt = RequireReference(_altModel.get());
        Model& model = RequireReference(alt.Model());
        for (std::int32_t i = 0; i < ManagedLength(_kandenSegMtx); ++i)
        {
            RequireReference(ManagedAt(model.Nodes, i)).Animation = ManagedAt(_kandenSegMtx, i);
        }
        model.UpdateMatrixStack();
        UpdateMaterials(alt, Recolor());
        GetDrawItems(alt, RequireReference(ManagedAt(model.Nodes, 0)), _curAlpha);
    }

    void PlayerEntity::DrawSpireAltAttack()
    {
        ModelInstance& alt = RequireReference(_altModel.get());
        Model& model = RequireReference(alt.Model());
        RequireReference(ManagedAt(model.Nodes, 0)).Animation = _modelTransform;
        for (std::int32_t i = 1; i < ManagedLength(model.Nodes); ++i)
        {
            Node& node = RequireReference(ManagedAt(model.Nodes, i));
            Matrix4 animation = node.Animation;
            SetRow3(animation, MatrixRow3(animation) + MatrixRow3(_modelTransform));
            node.Animation = animation;
        }
        model.UpdateMatrixStack();
        UpdateMaterials(alt, Recolor());
        GetDrawItems(alt, RequireReference(ManagedAt(model.Nodes, 0)), _curAlpha);
    }

    void PlayerEntity::GetDrawItems(
        ModelInstance& inst, Node& node, float alpha, std::int32_t polygonId, std::int32_t recolor)
    {
        if (alpha <= 0.0F)
        {
            return;
        }
        if (polygonId == -1)
        {
            polygonId = RequireReference(_scene).GetNextPolygonId();
        }
        const std::shared_ptr<Model> modelValue = inst.Model();
        Model& model = RequireReference(modelValue);
        if (node.Enabled)
        {
            const std::int32_t start = node.MeshId / 2;
            for (std::int32_t i = 0; i < node.MeshCount; ++i)
            {
                const std::shared_ptr<Mesh> meshValue
                    = ManagedAt(model.Meshes, start + i);
                Mesh& mesh = RequireReference(meshValue);
                if (!mesh.Visible)
                {
                    continue;
                }
                Material& material = RequireReference(ManagedAt(model.Materials, mesh.MaterialId));
                const Vector3 emission = GetEmission(inst, material, mesh.MaterialId);
                const Matrix4 texcoordMatrix = GetTexcoordMatrix(inst, material, mesh.MaterialId, node, recolor);
                const std::optional<Vector4> color = std::nullopt;
                const SelectionType selectionType = SelectionType::None;
                const std::optional<std::int32_t> bindingOverride
                    = GetBindingOverride(inst, material, mesh.MaterialId);
                Scene& renderScene = RequireReference(_scene);
                const auto& matrixStackValues = RequireReference(model.MatrixStackValues);
                std::vector<float> matrixStack;
                matrixStack.reserve(matrixStackValues.Length());
                for (std::size_t matrixIndex = 0; matrixIndex < matrixStackValues.Length(); ++matrixIndex)
                {
                    matrixStack.push_back(matrixStackValues[matrixIndex]);
                }
                renderScene.AddRenderItem(material, polygonId, alpha, emission,
                    GetLightInfo(), texcoordMatrix, node.Animation, modelValue, meshValue,
                    ManagedLength(model.NodeMatrixIds), matrixStack, color,
                    PaletteOverride(), selectionType, node.BillboardMode, _drawScale, bindingOverride);
            }
            if (node.ChildIndex != -1)
            {
                GetDrawItems(inst, RequireReference(ManagedAt(model.Nodes, node.ChildIndex)),
                    alpha, polygonId, recolor);
            }
        }
        if (node.NextIndex != -1)
        {
            GetDrawItems(inst, RequireReference(ManagedAt(model.Nodes, node.NextIndex)),
                alpha, polygonId, recolor);
        }
    }

    std::optional<std::int32_t> PlayerEntity::GetBindingOverride(
        ModelInstance& inst, Material& material, std::int32_t index)
    {
        if (_doubleDmgTimer > 0
            && (Hunter() != MphRead::Hunter::Spire
                || !(&inst == _gunModel.get() && index == 0))
            && material.Lighting > 0)
        {
            return _doubleDmgBindingId;
        }
        return EntityBase::GetBindingOverride(inst, material, index);
    }

    Vector3 PlayerEntity::GetEmission(ModelInstance& inst, Material& material, std::int32_t index)
    {
        if (_doubleDmgTimer > 0
            && (Hunter() != MphRead::Hunter::Spire
                || !(&inst == _gunModel.get() && index == 0))
            && material.Lighting > 0)
        {
            return Metadata::EmissionGray;
        }
        if (Team() == MphRead::Team::Orange)
        {
            return Metadata::EmissionOrange;
        }
        if (Team() == MphRead::Team::Green)
        {
            return Metadata::EmissionGreen;
        }
        return EntityBase::GetEmission(inst, material, index);
    }

    Matrix4 PlayerEntity::GetTexcoordMatrix(
        ModelInstance& inst, Material& material, std::int32_t materialId,
        Node& node, std::int32_t recolor)
    {
        if (_doubleDmgTimer > 0
            && (Hunter() != MphRead::Hunter::Spire
                || !(&inst == _gunModel.get() && materialId == 0))
            && material.Lighting > 0 && node.BillboardMode == BillboardMode::None)
        {
            ModelInstance& doubleDamage = RequireReference(_doubleDmgModel.get());
            Model& doubleDamageModel = RequireReference(doubleDamage.Model());
            ::MphRead::Recolor& doubleRecolor = RequireReference(ManagedAt(doubleDamageModel.Recolors, 0));
            const Texture& texture = ManagedAt(doubleRecolor.Textures, 0);

            Matrix4 texgenMatrix = IdentityMatrix();
            Model& model = RequireReference(inst.Model());
            if (model.Scale.X != 1.0F || model.Scale.Y != 1.0F || model.Scale.Z != 1.0F)
            {
                texgenMatrix = Multiply(CreateScale(model.Scale), texgenMatrix);
            }
            Matrix4 product = texgenMatrix;
            product.M12 *= -1.0F;
            product.M13 *= -1.0F;
            product.M22 *= -1.0F;
            product.M23 *= -1.0F;
            product.M32 *= -1.0F;
            product.M33 *= -1.0F;

            const std::uint64_t frame = RequireReference(_scene).LiveFrames() / 2U;
            const std::uint64_t zMul = 53248ULL * frame;
            const std::uint64_t zInner = (781874935307ULL * zMul >> 32U) + 2048ULL;
            const std::uint64_t yMul = 26624ULL * frame;
            const std::uint64_t yInner = (781874935307ULL * yMul + 0x80000000000ULL) >> 32U;
            const float rotZ = static_cast<float>(RotationValue(16ULL * zInner)) * (360.0F / 4096.0F);
            const float rotY = static_cast<float>(RotationValue(16ULL * yInner)) * (360.0F / 4096.0F);
            Matrix4 rot = CreateRotationZ(DegreesToRadians(rotZ));
            rot = Multiply(rot, CreateRotationY(DegreesToRadians(rotY)));
            product = Multiply(rot, product);

            const std::int32_t halfWidth = texture.Width / 2;
            const float textureScale = 1.0F / static_cast<float>(halfWidth);
            product.M11 *= textureScale; product.M12 *= textureScale;
            product.M13 *= textureScale; product.M14 *= textureScale;
            product.M21 *= textureScale; product.M22 *= textureScale;
            product.M23 *= textureScale; product.M24 *= textureScale;
            product.M31 *= textureScale; product.M32 *= textureScale;
            product.M33 *= textureScale; product.M34 *= textureScale;
            product.M41 *= textureScale; product.M42 *= textureScale;
            product.M43 *= textureScale; product.M44 *= textureScale;
            return Matrix4(
                ScaleVector(product.Row0(), 16.0F),
                ScaleVector(product.Row1(), 16.0F),
                ScaleVector(product.Row2(), 16.0F),
                product.Row3());
        }
        return EntityBase::GetTexcoordMatrix(inst, material, materialId, node, recolor);
    }

    void PlayerEntity::DrawShadow()
    {
        // Mods consumes this draw's result so replica visibility does not raycast again.
        ModSetReplicaShadowVisible(false);
        if (IsMainPlayer() && CameraType() == Entities::CameraType::First)
        {
            return;
        }
        ModelInstance& trail = RequireReference(_trailModel.get());
        Model& trailModel = RequireReference(trail.Model());
        Material& material = RequireReference(ManagedAt(trailModel.Materials, 1));
        const Vector3 point1 = _volume.SpherePosition;
        const Vector3 point2(point1.X, point1.Y - 10.0F, point1.Z);
        CollisionResult colRes{};
        if (CollisionDetection::CheckBetweenPoints(
                point1, point2, TestFlags::None, _scene, colRes)
            && colRes.Plane.Y >= Fixed::ToFloat(4))
        {
            const float height = point1.Y - colRes.Position.Y;
            if (height < 10.0F)
            {
                const float pct = 1.0F - height / 10.0F;
                float alpha = _curAlpha * pct;
                if (_health == 0)
                {
                    const float respawnTime = _deathCountdown > 0.0F
                        ? static_cast<float>(std::numeric_limits<std::uint16_t>::max())
                        : static_cast<float>(RespawnTime());
                    const float decrease = 2.0F * (respawnTime - _respawnTimer) / 2.0F;
                    alpha -= decrease;
                }
                if (alpha > 0.0F)
                {
                    Vector3 row1 = Vector3::Cross(colRes.Plane.Xyz(), Vector3(0.0F, 0.0F, 1.0F)).Normalized();
                    Vector3 row2 = colRes.Plane.Xyz();
                    Vector3 row3 = Vector3::Cross(row1, colRes.Plane.Xyz());
                    row1 = ScaleVector(row1, pct);
                    row2 = ScaleVector(row2, pct);
                    row3 = ScaleVector(row3, pct);
                    const float factor = Fixed::ToFloat(100);
                    const Vector3 row4(
                        colRes.Position.X + colRes.Plane.X * factor,
                        colRes.Position.Y + colRes.Plane.Y * factor,
                        colRes.Position.Z + colRes.Plane.Z * factor);
                    const Matrix4 transform(
                        Vector4(row1, 0.0F), Vector4(row2, 0.0F),
                        Vector4(row3, 0.0F), Vector4(row4, 1.0F));
                    auto uvsAndVerts = MphRead::NativeRuntime::RentFromSharedArrayPool(8);
                    (*uvsAndVerts)[0] = Vector3(0.0F, 0.0F, 0.0F);
                    (*uvsAndVerts)[1] = Vector3(-0.75F, 0.03125F, -0.75F);
                    (*uvsAndVerts)[2] = Vector3(0.0F, 1.0F, 0.0F);
                    (*uvsAndVerts)[3] = Vector3(-0.75F, 0.03125F, 0.75F);
                    (*uvsAndVerts)[4] = Vector3(1.0F, 1.0F, 0.0F);
                    (*uvsAndVerts)[5] = Vector3(0.75F, 0.03125F, 0.75F);
                    (*uvsAndVerts)[6] = Vector3(1.0F, 0.0F, 0.0F);
                    (*uvsAndVerts)[7] = Vector3(0.75F, 0.03125F, -0.75F);
                    const std::int32_t polygonId = RequireReference(_scene).GetNextPolygonId();
                    const Vector3 color(0.0F, 0.0F, 0.0F);
                    RequireReference(_scene).AddRenderItem(RenderItemType::Particle, alpha,
                        polygonId, color, material.XRepeat, material.YRepeat,
                        material.ScaleS, material.ScaleT, transform, uvsAndVerts, _trailBindingId2);
                    ModSetReplicaShadowVisible(true);
                }
            }
        }
    }

    void PlayerEntity::DrawMorphBallTrail()
    {
        assert(_trailModel != nullptr);
        ModelInstance& trail = RequireReference(_trailModel.get());
        Model& model = RequireReference(trail.Model());
        Material& material = RequireReference(ManagedAt(model.Materials, 0));
        ::MphRead::Recolor& recolor = RequireReference(ManagedAt(model.Recolors, 0));
        assert(ManagedAt(recolor.Textures, material.TextureId).Width == 32);

        std::vector<float> matrixStack(static_cast<std::size_t>(16 * _mbTrailSegments));
        for (std::int32_t i = 0; i < _mbTrailSegments; ++i)
        {
            const Matrix4 matrix = ManagedAt(ManagedAt(_mbTrailMatrices, SlotIndex()), i);
            const std::size_t offset = static_cast<std::size_t>(i) * 16U;
            matrixStack[offset] = matrix.M11; matrixStack[offset + 1U] = matrix.M12;
            matrixStack[offset + 2U] = matrix.M13; matrixStack[offset + 3U] = matrix.M14;
            matrixStack[offset + 4U] = matrix.M21; matrixStack[offset + 5U] = matrix.M22;
            matrixStack[offset + 6U] = matrix.M23; matrixStack[offset + 7U] = matrix.M24;
            matrixStack[offset + 8U] = matrix.M31; matrixStack[offset + 9U] = matrix.M32;
            matrixStack[offset + 10U] = matrix.M33; matrixStack[offset + 11U] = matrix.M34;
            matrixStack[offset + 12U] = matrix.M41; matrixStack[offset + 13U] = matrix.M42;
            matrixStack[offset + 14U] = matrix.M43; matrixStack[offset + 15U] = matrix.M44;
        }

        std::int32_t count = 0;
        const std::int32_t index = ManagedAt(_mbTrailIndices, SlotIndex());
        auto uvsAndVerts = MphRead::NativeRuntime::RentFromSharedArrayPool(8 * _mbTrailSegments);
        for (std::int32_t i = 0; i < _mbTrailSegments; ++i)
        {
            const std::int32_t base = index - 1 - i;
            const std::int32_t mtxId1 = base + (base < 0 ? _mbTrailSegments : 0);
            const std::int32_t base2 = mtxId1 - 1;
            const std::int32_t mtxId2 = base2 + (base2 < 0 ? _mbTrailSegments : 0);
            const float alpha1 = ManagedAt(ManagedAt(_mbTrailAlphas, SlotIndex()), mtxId1);
            const float alpha2 = ManagedAt(ManagedAt(_mbTrailAlphas, SlotIndex()), mtxId2);
            if (alpha1 > 0.0F && alpha2 > 0.0F)
            {
                const float uvS1 = static_cast<float>(31 - ManagedFloatToInt32(alpha1 * 31.0F)) / 32.0F;
                const float uvS2 = static_cast<float>(31 - ManagedFloatToInt32(alpha2 * 31.0F)) / 32.0F;
                const std::size_t offset = static_cast<std::size_t>(i) * 8U;
                (*uvsAndVerts)[offset] = Vector3(uvS1, 0.0F, static_cast<float>(mtxId1));
                (*uvsAndVerts)[offset + 1U] = Vector3(0.0F, 0.375F, 0.0F);
                (*uvsAndVerts)[offset + 2U] = Vector3(uvS1, 1.0F, static_cast<float>(mtxId1));
                (*uvsAndVerts)[offset + 3U] = Vector3(0.0F, -0.375F, 0.0F);
                (*uvsAndVerts)[offset + 4U] = Vector3(uvS2, 1.0F, static_cast<float>(mtxId2));
                (*uvsAndVerts)[offset + 5U] = Vector3(0.0F, -0.375F, 0.0F);
                (*uvsAndVerts)[offset + 6U] = Vector3(uvS2, 0.0F, static_cast<float>(mtxId2));
                (*uvsAndVerts)[offset + 7U] = Vector3(0.0F, 0.375F, 0.0F);
                ++count;
            }
        }
        if (count > 0)
        {
            const Vector3 color(1.0F, 27.0F / 31.0F, 11.0F / 31.0F);
            RequireReference(_scene).AddRenderItem(RenderItemType::TrailStack,
                RequireReference(_scene).GetNextPolygonId(), color,
                material.XRepeat, material.YRepeat, material.ScaleS, material.ScaleT,
                _mbTrailSegments, matrixStack, uvsAndVerts, count, _trailBindingId1);
        }
    }

    void PlayerEntity::DrawDeathParticles()
    {
        const float respawnTime = static_cast<float>(RespawnTime());
        const float timePct = 1.0F
            - ((_respawnTimer - (2.0F / 3.0F * respawnTime)) / (1.0F / 3.0F * respawnTime));
        if (timePct < 0.0F || timePct > 1.0F)
        {
            return;
        }
        const float scale = timePct / 2.0F + 0.1F;
        const float angle = std::sin(DegreesToRadians(270.0F - 90.0F * timePct));
        const float sin270 = std::sin(DegreesToRadians(270.0F));
        const float sin180 = std::sin(DegreesToRadians(180.0F));
        const float offset = (angle - sin270) / (sin180 - sin270);
        ModelInstance& biped = RequireReference(_bipedModel1.get());
        Model& model = RequireReference(biped.Model());
        for (std::int32_t i = 1; i < ManagedLength(model.Nodes); ++i)
        {
            Node& node = RequireReference(ManagedAt(model.Nodes, i));
            Vector3 nodePos = MatrixRow3(node.Animation);
            nodePos.Y += offset;
            if (node.ChildIndex != -1)
            {
                assert(node.ChildIndex > 0);
                Vector3 childPos = MatrixRow3(RequireReference(ManagedAt(model.Nodes, node.ChildIndex)).Animation);
                childPos.Y += offset;
                for (std::int32_t j = 1; j < 5; ++j)
                {
                    Vector3 segPos(
                        nodePos.X + static_cast<float>(j) * (childPos.X - nodePos.X) / 5.0F,
                        nodePos.Y + static_cast<float>(j) * (childPos.Y - nodePos.Y) / 5.0F,
                        nodePos.Z + static_cast<float>(j) * (childPos.Z - nodePos.Z) / 5.0F);
                    segPos = segPos + ScaleVector((segPos - static_cast<Vector3>(Position)).Normalized(), offset);
                    RequireReference(_scene).AddSingleParticle(
                        SingleType::Death, segPos, Vector3(1.0F, 1.0F, 1.0F), 1.0F - timePct, scale);
                }
            }
            if (node.NextIndex != -1)
            {
                assert(node.NextIndex > 0);
                Vector3 nextPos = MatrixRow3(RequireReference(ManagedAt(model.Nodes, node.NextIndex)).Animation);
                nextPos.Y += offset;
                for (std::int32_t j = 1; j < 5; ++j)
                {
                    Vector3 segPos(
                        nodePos.X + static_cast<float>(j) * (nextPos.X - nodePos.X) / 5.0F,
                        nodePos.Y + static_cast<float>(j) * (nextPos.Y - nodePos.Y) / 5.0F,
                        nodePos.Z + static_cast<float>(j) * (nextPos.Z - nodePos.Z) / 5.0F);
                    segPos = segPos + ScaleVector((segPos - static_cast<Vector3>(Position)).Normalized(), offset);
                    RequireReference(_scene).AddSingleParticle(
                        SingleType::Death, segPos, Vector3(1.0F, 1.0F, 1.0F), 1.0F - timePct, scale);
                }
            }
            nodePos = nodePos + ScaleVector((nodePos - static_cast<Vector3>(Position)).Normalized(), offset);
            RequireReference(_scene).AddSingleParticle(
                SingleType::Death, nodePos, Vector3(1.0F, 1.0F, 1.0F), 1.0F - timePct, scale);
        }
    }

    void PlayerEntity::DrawVolumes()
    {
        if (RequireReference(_scene).ShowVolumes() == VolumeDisplay::KillPlane)
        {
            if (!IsAltForm() && !IsMorphing() && !IsUnmorphing())
            {
                AddVectorItem(_gunDrawPos, ScaleVector(_aimVec, 3.0F), Vector3(0.0F, 0.0F, 1.0F));
                AddDotItem(_muzzlePos, Vector3(1.0F, 0.0F, 0.0F));
                AddDotItem(_muzzlePos + ScaleVector((_aimPosition - _muzzlePos).Normalized(), 3.0F),
                    Vector3(1.0F, 0.0F, 0.0F));
            }
            AddVectorItem(_position, ScaleVector(_facingVector, 3.0F), Vector3(0.0F, 1.0F, 0.0F));
        }
    }

    void PlayerEntity::GetDrawInfo()
    {
    }
}
