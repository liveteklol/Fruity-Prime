#include "Model.hpp"

#include "../NativeRuntime/System/Globalization.hpp"
#include "../NativeRuntime/System/Runtime.hpp"

#include "../Program.hpp"
#include "../Read.hpp"
#include "Types.hpp"
#include "../NativeRuntime/System/Managed.hpp"
#include "../NativeRuntime/OpenTK/Mathematics.hpp"
#include "../NativeRuntime/System/Encoding.hpp"

#include <bit>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using ::MphRead::NativeRuntime::ManagedAt;
using ::MphRead::NativeRuntime::RequireReference;
using ::MphRead::NativeRuntime::ShiftLeft;
using ::MphRead::NativeRuntime::ShiftRight;
using ::MphRead::NativeRuntime::UncheckedAdd;
using ::MphRead::NativeRuntime::UncheckedMultiply;
using ::MphRead::NativeRuntime::UncheckedSubtract;
using ::OpenTK::Mathematics::CreateRotationX;
using ::OpenTK::Mathematics::CreateRotationY;
using ::OpenTK::Mathematics::CreateRotationZ;
using ::OpenTK::Mathematics::CreateScale;
using ::OpenTK::Mathematics::CreateTranslation;
using ::OpenTK::Mathematics::Multiply;

namespace
{
    using MphRead::ManagedArray;
    using OpenTK::Mathematics::Matrix4;
    using OpenTK::Mathematics::Vector3;
    using OpenTK::Mathematics::Vector4;

    [[nodiscard]] Matrix4 Identity() noexcept
    {
        return Matrix4(
            Vector4(1.0F, 0.0F, 0.0F, 0.0F),
            Vector4(0.0F, 1.0F, 0.0F, 0.0F),
            Vector4(0.0F, 0.0F, 1.0F, 0.0F),
            Vector4(0.0F, 0.0F, 0.0F, 1.0F));
    }

    [[nodiscard]] Matrix4 ClearRotation(Matrix4 matrix) noexcept
    {
        const float row0Length = std::sqrt(
            matrix.M11 * matrix.M11
            + matrix.M12 * matrix.M12
            + matrix.M13 * matrix.M13);
        const float row1Length = std::sqrt(
            matrix.M21 * matrix.M21
            + matrix.M22 * matrix.M22
            + matrix.M23 * matrix.M23);
        const float row2Length = std::sqrt(
            matrix.M31 * matrix.M31
            + matrix.M32 * matrix.M32
            + matrix.M33 * matrix.M33);

        matrix.M11 = row0Length;
        matrix.M12 = 0.0F;
        matrix.M13 = 0.0F;
        matrix.M21 = 0.0F;
        matrix.M22 = row1Length;
        matrix.M23 = 0.0F;
        matrix.M31 = 0.0F;
        matrix.M32 = 0.0F;
        matrix.M33 = row2Length;
        return matrix;
    }

    [[nodiscard]] std::vector<char32_t> DecodeManagedByteString(
        const std::string& value)
    {
        // Formats.cpp maps each source byte to the same Unicode scalar before
        // encoding Native strings as UTF-8. Decode those scalars again so
        // four-character node-name chunks retain C# string indexing.
        std::vector<char32_t> result;
        for (std::size_t i = 0; i < value.size();)
        {
            const auto first = static_cast<std::uint8_t>(value[i]);
            if (first < 0x80U)
            {
                result.push_back(first);
                ++i;
            }
            else if ((first & 0xE0U) == 0xC0U && i + 1 < value.size())
            {
                const auto second = static_cast<std::uint8_t>(value[i + 1]);
                result.push_back(
                    static_cast<char32_t>(((first & 0x1FU) << 6)
                    | (second & 0x3FU)));
                i += 2;
            }
            else if ((first & 0xF0U) == 0xE0U && i + 2 < value.size())
            {
                const auto second = static_cast<std::uint8_t>(value[i + 1]);
                const auto third = static_cast<std::uint8_t>(value[i + 2]);
                result.push_back(
                    static_cast<char32_t>(((first & 0x0FU) << 12)
                    | ((second & 0x3FU) << 6)
                    | (third & 0x3FU)));
                i += 3;
            }
            else
            {
                // This path is unreachable for strings produced by the
                // completed MarshalString adapter; retain one code unit if an
                // externally constructed Native string is malformed.
                result.push_back(first);
                ++i;
            }
        }
        return result;
    }

    [[nodiscard]] bool ChunkEquals(
        const std::vector<char32_t>& text,
        std::size_t start,
        char32_t c0,
        char32_t c1,
        char32_t c2,
        char32_t c3) noexcept
    {
        return text[start] == c0
            && text[start + 1] == c1
            && text[start + 2] == c2
            && text[start + 3] == c3;
    }

    [[nodiscard]] bool IsDefinedTextureFormat(
        MphRead::TextureFormat format) noexcept
    {
        switch (format)
        {
        case MphRead::TextureFormat::Palette2Bit:
        case MphRead::TextureFormat::Palette4Bit:
        case MphRead::TextureFormat::Palette8Bit:
        case MphRead::TextureFormat::PaletteA5I3:
        case MphRead::TextureFormat::DirectRgb:
        case MphRead::TextureFormat::PaletteA3I5:
            return true;
        default:
            return false;
        }
    }

    [[nodiscard]] std::string TextureFormatString(
        MphRead::TextureFormat format)
    {
        return std::to_string(static_cast<std::uint8_t>(format));
    }
}

namespace MphRead
{
    AnimationInfo::AnimationInfo()
        : Index(std::make_shared<ManagedArray<std::int32_t>>(2)),
          PrevIndex(std::make_shared<ManagedArray<std::int32_t>>(2)),
          Frame(std::make_shared<ManagedArray<std::int32_t>>(2)),
          FrameCount(std::make_shared<ManagedArray<std::int32_t>>(2)),
          Flags(std::make_shared<ManagedArray<AnimFlags>>(2)),
          Step(std::make_shared<ManagedArray<std::int32_t>>(2)),
          Node(std::make_shared<NodeAnimationInfo>()),
          Material(std::make_shared<MaterialAnimationInfo>()),
          Texcoord(std::make_shared<TexcoordAnimationInfo>()),
          Texture(std::make_shared<TextureAnimationInfo>())
    {
        (*Index)[0] = -1;
        (*Index)[1] = -1;
        (*PrevIndex)[0] = -1;
        (*PrevIndex)[1] = -1;
        (*Frame)[0] = 0;
        (*Frame)[1] = 0;
        (*FrameCount)[0] = 0;
        (*FrameCount)[1] = 0;
        (*Flags)[0] = AnimFlags::None;
        (*Flags)[1] = AnimFlags::None;
        (*Step)[0] = 1;
        (*Step)[1] = 1;
    }

    std::int32_t AnimationInfo::NodeFrame() const
    {
        return (*Frame)[static_cast<std::size_t>(RequireReference(Node).Slot)];
    }

    std::int32_t AnimationInfo::MaterialFrame() const
    {
        return (*Frame)[static_cast<std::size_t>(RequireReference(Material).Slot)];
    }

    std::int32_t AnimationInfo::TextureFrame() const
    {
        return (*Frame)[static_cast<std::size_t>(RequireReference(Texture).Slot)];
    }

    std::int32_t AnimationInfo::TexcoordFrame() const
    {
        return (*Frame)[static_cast<std::size_t>(RequireReference(Texcoord).Slot)];
    }

    std::int32_t AnimationInfo::NodeIndex() const
    {
        return (*Index)[static_cast<std::size_t>(RequireReference(Node).Slot)];
    }

    std::int32_t AnimationInfo::MaterialIndex() const
    {
        return (*Index)[static_cast<std::size_t>(RequireReference(Material).Slot)];
    }

    std::int32_t AnimationInfo::TextureIndex() const
    {
        return (*Index)[static_cast<std::size_t>(RequireReference(Texture).Slot)];
    }

    std::int32_t AnimationInfo::TexcoordIndex() const
    {
        return (*Index)[static_cast<std::size_t>(RequireReference(Texcoord).Slot)];
    }

    AnimationOffsets::AnimationOffsets(
        const std::shared_ptr<AnimationResults>& animations)
        : AnimationOffsets(BuildInit(animations))
    {
    }

    AnimationOffsets::Init AnimationOffsets::BuildInit(
        const std::shared_ptr<AnimationResults>& animations)
    {
        if (!animations)
        {
            throw System::NullReferenceException();
        }
        return Init{
            animations->NodeGroupOffsets,
            animations->MaterialGroupOffsets,
            animations->TexcoordGroupOffsets,
            animations->TextureGroupOffsets};
    }

    AnimationOffsets::AnimationOffsets(Init init)
        : Node(std::move(init.Node)),
          Material(std::move(init.Material)),
          Texcoord(std::move(init.Texcoord)),
          Texture(std::move(init.Texture))
    {
    }

    AnimationGroups::AnimationGroups(
        const std::shared_ptr<AnimationResults>& animations)
        : AnimationGroups(BuildInit(animations))
    {
    }

    AnimationGroups::Init AnimationGroups::BuildInit(
        const std::shared_ptr<AnimationResults>& animations)
    {
        if (!animations)
        {
            throw System::NullReferenceException();
        }

        std::shared_ptr<const std::vector<std::shared_ptr<NodeAnimationGroup>>> node
            = animations->NodeAnimationGroups;
        std::shared_ptr<const std::vector<std::shared_ptr<MaterialAnimationGroup>>> material
            = animations->MaterialAnimationGroups;
        std::shared_ptr<const std::vector<std::shared_ptr<TexcoordAnimationGroup>>> texcoord
            = animations->TexcoordAnimationGroups;
        std::shared_ptr<const std::vector<std::shared_ptr<TextureAnimationGroup>>> texture
            = animations->TextureAnimationGroups;

        const bool any = !RequireReference(node).empty()
            || !RequireReference(material).empty()
            || !RequireReference(texcoord).empty()
            || !RequireReference(texture).empty();

        auto offsets = std::make_shared<AnimationOffsets>(animations);
#ifndef NDEBUG
        assert(RequireReference(offsets->Node).size() >= RequireReference(node).size());
        assert(RequireReference(offsets->Material).size() >= RequireReference(material).size());
        assert(RequireReference(offsets->Texcoord).size() >= RequireReference(texcoord).size());
        assert(RequireReference(offsets->Texture).size() >= RequireReference(texture).size());
#endif

        return Init{
            any,
            std::move(node),
            std::move(material),
            std::move(texcoord),
            std::move(texture),
            std::move(offsets)};
    }

    AnimationGroups::AnimationGroups(Init init)
        : Any(init.Any),
          Node(std::move(init.Node)),
          Material(std::move(init.Material)),
          Texcoord(std::move(init.Texcoord)),
          Texture(std::move(init.Texture)),
          Offsets(std::move(init.Offsets))
    {
    }

    ModelInstance::ModelInstance(std::shared_ptr<MphRead::Model> model)
        : _model(std::move(model)),
          AnimInfo(std::make_shared<AnimationInfo>())
    {
    }

    std::shared_ptr<MphRead::Model> ModelInstance::Model() const noexcept
    {
        return _model;
    }

    void ModelInstance::SetModel(std::shared_ptr<MphRead::Model> model)
    {
        _model = std::move(model);
    }

    void ModelInstance::SetAnimation(std::int32_t index, AnimFlags animFlags)
    {
        auto& model = RequireReference(_model);
        auto& groups = RequireReference(model.AnimationGroups);
        auto& info = RequireReference(AnimInfo);

        if (groups.Any && index >= 0)
        {
            (*info.Step)[0] = 1;
            (*info.Flags)[0] = animFlags;
            (*info.PrevIndex)[0] = (*info.Index)[0];
            (*info.Index)[0] = index;
            RequireReference(info.Material).Slot = 0;
            RequireReference(info.Texture).Slot = 0;
            RequireReference(info.Texcoord).Slot = 0;
            RequireReference(info.Node).Slot = 0;

            RequireReference(info.Material).Group = ManagedAt(groups.Material, index);
            RequireReference(info.Texture).Group = ManagedAt(groups.Texture, index);
            RequireReference(info.Texcoord).Group = ManagedAt(groups.Texcoord, index);
            RequireReference(info.Node).Group = ManagedAt(groups.Node, index);

            if (RequireReference(RequireReference(info.Node).Group).Count > 0)
            {
                (*info.FrameCount)[0] = RequireReference(RequireReference(info.Node).Group).FrameCount;
            }
            else if (RequireReference(RequireReference(info.Material).Group).Count > 0)
            {
                (*info.FrameCount)[0] = RequireReference(RequireReference(info.Material).Group).FrameCount;
            }
            else if (RequireReference(RequireReference(info.Texture).Group).Count > 0)
            {
                (*info.FrameCount)[0] = RequireReference(RequireReference(info.Texture).Group).FrameCount;
            }
            else if (RequireReference(RequireReference(info.Texcoord).Group).Count > 0)
            {
                (*info.FrameCount)[0] = RequireReference(RequireReference(info.Texcoord).Group).FrameCount;
            }

            (*info.Frame)[0] = TypeExtensions::TestFlag(animFlags, AnimFlags::Reverse)
                ? UncheckedSubtract((*info.FrameCount)[0], 1)
                : 0;
        }
        else
        {
            RequireReference(info.Material).Group.reset();
            RequireReference(info.Texture).Group.reset();
            RequireReference(info.Texcoord).Group.reset();
            RequireReference(info.Node).Group.reset();
        }
    }

    void ModelInstance::SetAnimation(
        std::int32_t index,
        std::int32_t slot,
        SetFlags setFlags,
        AnimFlags animFlags)
    {
        auto& model = RequireReference(_model);
        auto& groups = RequireReference(model.AnimationGroups);
        auto& info = RequireReference(AnimInfo);
        const std::size_t slotIndex = static_cast<std::size_t>(slot);

        if (groups.Any && index >= 0)
        {
            (*info.Step)[slotIndex] = 1;
            (*info.Flags)[slotIndex] = animFlags;
            (*info.PrevIndex)[slotIndex] = (*info.Index)[slotIndex];
            (*info.Index)[slotIndex] = index;

            if (TypeExtensions::TestFlag(setFlags, SetFlags::Material))
            {
                RequireReference(info.Material).Slot = slot;
                RequireReference(info.Material).Group = ManagedAt(groups.Material, index);
                if (RequireReference(RequireReference(info.Material).Group).Count > 0)
                {
                    (*info.FrameCount)[slotIndex]
                        = RequireReference(RequireReference(info.Material).Group).FrameCount;
                }
            }
            if (TypeExtensions::TestFlag(setFlags, SetFlags::Texcoord))
            {
                RequireReference(info.Texcoord).Slot = slot;
                RequireReference(info.Texcoord).Group = ManagedAt(groups.Texcoord, index);
                if (RequireReference(RequireReference(info.Texcoord).Group).Count > 0)
                {
                    (*info.FrameCount)[slotIndex]
                        = RequireReference(RequireReference(info.Texcoord).Group).FrameCount;
                }
            }
            if (TypeExtensions::TestFlag(setFlags, SetFlags::Texture))
            {
                RequireReference(info.Texture).Slot = slot;
                RequireReference(info.Texture).Group = ManagedAt(groups.Texture, index);
                if (RequireReference(RequireReference(info.Texture).Group).Count > 0)
                {
                    (*info.FrameCount)[slotIndex]
                        = RequireReference(RequireReference(info.Texture).Group).FrameCount;
                }
            }
            if (TypeExtensions::TestFlag(setFlags, SetFlags::Node))
            {
                RequireReference(info.Node).Slot = slot;
                RequireReference(info.Node).Group = ManagedAt(groups.Node, index);
                if (RequireReference(RequireReference(info.Node).Group).Count > 0)
                {
                    (*info.FrameCount)[slotIndex]
                        = RequireReference(RequireReference(info.Node).Group).FrameCount;
                }
            }

            (*info.Frame)[slotIndex]
                = TypeExtensions::TestFlag(animFlags, AnimFlags::Reverse)
                ? UncheckedSubtract((*info.FrameCount)[slotIndex], 1)
                : 0;
        }
        else
        {
            RequireReference(info.Material).Group.reset();
            RequireReference(info.Texture).Group.reset();
            RequireReference(info.Texcoord).Group.reset();
            RequireReference(info.Node).Group.reset();
        }
    }

    void ModelInstance::UpdateAnimFrames()
    {
        auto& info = RequireReference(AnimInfo);
        if (RequireReference(info.Node).Slot == 0
            || RequireReference(info.Material).Slot == 0
            || RequireReference(info.Texcoord).Slot == 0
            || RequireReference(info.Texture).Slot == 0)
        {
            UpdateAnimFrames(0);
        }
        if (RequireReference(info.Node).Slot == 1
            || RequireReference(info.Material).Slot == 1
            || RequireReference(info.Texcoord).Slot == 1
            || RequireReference(info.Texture).Slot == 1)
        {
            UpdateAnimFrames(1);
        }
    }

    void ModelInstance::UpdateAnimFrames(std::int32_t slot)
    {
        auto& info = RequireReference(AnimInfo);
        const std::size_t slotIndex = static_cast<std::size_t>(slot);
        const AnimFlags flags = (*info.Flags)[slotIndex];
        const std::int32_t frame = (*info.Frame)[slotIndex];
        const std::int32_t step = (*info.Step)[slotIndex];
        const std::int32_t frameCount = (*info.FrameCount)[slotIndex];

        if (!TypeExtensions::TestFlag(flags, AnimFlags::Paused)
            && !TypeExtensions::TestFlag(flags, AnimFlags::Ended))
        {
            if (TypeExtensions::TestFlag(flags, AnimFlags::PingPong))
            {
                if (TypeExtensions::TestFlag(flags, AnimFlags::Reverse))
                {
                    if (frame <= step)
                    {
                        (*info.Frame)[slotIndex] = UncheckedSubtract(step, frame);
                        (*info.Flags)[slotIndex] ^= AnimFlags::Reverse;
                    }
                    else
                    {
                        (*info.Frame)[slotIndex] = UncheckedSubtract(frame, step);
                    }
                }
                else
                {
                    const std::int32_t nextFrame = UncheckedAdd(frame, step);
                    (*info.Frame)[slotIndex] = nextFrame;
                    if (nextFrame >= UncheckedSubtract(frameCount, 1))
                    {
                        (*info.Frame)[slotIndex] = UncheckedSubtract(
                            UncheckedSubtract(UncheckedMultiply(2, frameCount), nextFrame),
                            2);
                        (*info.Flags)[slotIndex] ^= AnimFlags::Reverse;
                    }
                }
            }
            else if (TypeExtensions::TestFlag(flags, AnimFlags::Reverse))
            {
                if (frame > step)
                {
                    (*info.Frame)[slotIndex] = UncheckedSubtract(frame, step);
                }
                else if (TypeExtensions::TestFlag(flags, AnimFlags::NoLoop))
                {
                    (*info.Frame)[slotIndex] = 0;
                    (*info.Flags)[slotIndex] |= AnimFlags::Ended;
                }
                else if (frame == step)
                {
                    (*info.Frame)[slotIndex] = 0;
                }
                else
                {
                    (*info.Frame)[slotIndex] = UncheckedSubtract(
                        frameCount, UncheckedSubtract(step, frame));
                }
            }
            else
            {
                const std::int32_t nextFrame = UncheckedAdd(frame, step);
                (*info.Frame)[slotIndex] = nextFrame;
                if (nextFrame >= UncheckedSubtract(frameCount, 1))
                {
                    if (TypeExtensions::TestFlag(flags, AnimFlags::NoLoop))
                    {
                        (*info.Frame)[slotIndex] = UncheckedSubtract(frameCount, 1);
                        (*info.Flags)[slotIndex] |= AnimFlags::Ended;
                    }
                    else if (nextFrame >= frameCount)
                    {
                        (*info.Frame)[slotIndex] = UncheckedSubtract(nextFrame, frameCount);
                    }
                }
            }
        }
    }

    void ModelInstance::SetNodeAnim(std::int32_t index)
    {
        auto& info = RequireReference(AnimInfo);
        auto& nodeInfo = RequireReference(info.Node);

        if (index <= -1)
        {
            (*info.Index)[static_cast<std::size_t>(nodeInfo.Slot)] = -1;
            nodeInfo.Group.reset();
            return;
        }

        auto& model = RequireReference(_model);
        auto& groups = RequireReference(model.AnimationGroups);
        if (index >= static_cast<std::int32_t>(RequireReference(groups.Node).size()))
        {
            (*info.Index)[static_cast<std::size_t>(nodeInfo.Slot)] = -1;
            nodeInfo.Group.reset();
        }
        else
        {
            SetAnimation(index, nodeInfo.Slot, SetFlags::Node);
        }
    }

    void ModelInstance::SetMaterialAnim(std::int32_t index)
    {
        auto& info = RequireReference(AnimInfo);
        auto& materialInfo = RequireReference(info.Material);

        if (index <= -1)
        {
            (*info.Index)[static_cast<std::size_t>(materialInfo.Slot)] = -1;
            materialInfo.Group.reset();
            return;
        }

        auto& model = RequireReference(_model);
        auto& groups = RequireReference(model.AnimationGroups);
        if (index >= static_cast<std::int32_t>(RequireReference(groups.Material).size()))
        {
            (*info.Index)[static_cast<std::size_t>(materialInfo.Slot)] = -1;
            materialInfo.Group.reset();
        }
        else
        {
            SetAnimation(index, materialInfo.Slot, SetFlags::Material);
        }
    }

    std::int32_t Model::_nextId = 0;

    Model::Model(Init init)
        : Id(init.Id),
          Name(std::move(init.Name)),
          FirstHunt(init.FirstHunt),
          Header(init.Header),
          Nodes(std::move(init.Nodes)),
          Meshes(std::move(init.Meshes)),
          Materials(std::move(init.Materials)),
          DisplayLists(std::move(init.DisplayLists)),
          TextureMatrices(std::move(init.TextureMatrices)),
          RenderInstructionLists(std::move(init.RenderInstructionLists)),
          Recolors(std::move(init.Recolors)),
          NodeMatrixIds(std::move(init.NodeMatrixIds)),
          MatrixStackValues(init.MatrixStackValues),
          AnimationGroups(std::move(init.AnimationGroups)),
          RawNodes(std::move(init.RawNodes)),
          NodePos(std::move(init.NodePos)),
          NodeInitPos(std::move(init.NodeInitPos)),
          NodePosCounts(std::move(init.NodePosCounts)),
          NodePosScales(std::move(init.NodePosScales)),
          Scale(init.Scale),
          _matrixStackValues(std::move(init.MatrixStackValues))
    {
    }

    std::int32_t Model::NextId() noexcept
    {
        const std::int32_t result = _nextId;
        _nextId = UncheckedAdd(_nextId, 1);
        return result;
    }

    void Model::FilterNodes(std::int32_t layerMask)
    {
        for (const auto& nodePointer : RequireReference(Nodes))
        {
            auto& node = RequireReference(nodePointer);
            node.Enabled = true;

            const std::vector<char32_t> name = DecodeManagedByteString(node.Name);
            const std::u32string_view nameView = name.empty()
                ? std::u32string_view{}
                : std::u32string_view(name.data(), name.size());
            if (!NativeRuntime::StringStartsWithCurrentCulture(NativeRuntime::Utf32ToUtf8(nameView), "_"))
            {
                continue;
            }

            std::int32_t flags = 0;
            for (std::size_t i = 0; name.size() - i >= 4; i += 4)
            {
                const std::u32string_view chunk(name.data() + i, 4);
                std::int32_t id = 0;
                if (NativeRuntime::StringStartsWithCurrentCulture(NativeRuntime::Utf32ToUtf8(chunk), "_s")
                    && NativeRuntime::Int32TryParseCurrentCulture(
                        NativeRuntime::Utf32ToUtf8(chunk.substr(2)), id))
                {
                    const std::uint32_t oldFlags
                        = std::bit_cast<std::uint32_t>(flags);
                    const std::uint32_t bit
                        = 1U << (std::bit_cast<std::uint32_t>(id) & 31U);
                    const std::uint32_t nextFlags
                        = (oldFlags & 0xC03FU)
                        | (((oldFlags << 18U) >> 24U) | bit) << 6U;
                    flags = std::bit_cast<std::int32_t>(nextFlags);
                }
                else if (ChunkEquals(name, i, U'_', U'm', U'l', U'0'))
                {
                    flags |= static_cast<std::int32_t>(
                        static_cast<std::uint16_t>(NodeLayer::MultiplayerLod0));
                }
                else if (ChunkEquals(name, i, U'_', U'm', U'l', U'1'))
                {
                    flags |= static_cast<std::int32_t>(
                        static_cast<std::uint16_t>(NodeLayer::MultiplayerLod1));
                }
                else if (ChunkEquals(name, i, U'_', U'm', U'p', U'u'))
                {
                    flags |= static_cast<std::int32_t>(
                        static_cast<std::uint16_t>(NodeLayer::MultiplayerU));
                }
                else if (ChunkEquals(name, i, U'_', U'c', U't', U'f'))
                {
                    flags |= static_cast<std::int32_t>(
                        static_cast<std::uint16_t>(NodeLayer::CaptureTheFlag));
                }
            }

            if ((flags & layerMask) == 0)
            {
                node.Enabled = false;
            }
        }
    }

    void Model::ComputeNodeMatrices(std::int32_t index)
    {
        const auto& nodes = RequireReference(Nodes);
        if (nodes.empty() || index == -1)
        {
            return;
        }

        for (std::int32_t i = index; i != -1;)
        {
            auto& node = RequireReference(nodes.at(static_cast<std::size_t>(i)));
            const Vector3 position(
                node.Position.X / Scale.X,
                node.Position.Y / Scale.Y,
                node.Position.Z / Scale.Z);
            Matrix4 transform = ComputeNodeTransforms(
                node.Scale, node.Angle, position);
            if (node.ParentIndex == -1)
            {
                node.Transform = transform;
            }
            else
            {
                node.Transform = Multiply(
                    transform,
                    RequireReference(nodes.at(
                        static_cast<std::size_t>(node.ParentIndex))).Transform);
            }
            if (node.ChildIndex != -1)
            {
                ComputeNodeMatrices(node.ChildIndex);
            }
            i = node.NextIndex;
        }
    }

    Matrix4 Model::ComputeNodeTransforms(
        Vector3 scale,
        Vector3 angle,
        Vector3 position) const
    {
        const float sinAx = std::sin(angle.X);
        const float sinAy = std::sin(angle.Y);
        const float sinAz = std::sin(angle.Z);
        const float cosAx = std::cos(angle.X);
        const float cosAy = std::cos(angle.Y);
        const float cosAz = std::cos(angle.Z);

        const float v18 = cosAx * cosAz;
        const float v19 = cosAx * sinAz;
        const float v20 = cosAx * cosAy;
        const float v22 = sinAx * sinAy;
        const float v17 = v19 * sinAy;

        Matrix4 transform{};

        transform.M11 = scale.X * cosAy * cosAz;
        transform.M12 = scale.X * cosAy * sinAz;
        transform.M13 = scale.X * -sinAy;

        transform.M21 = scale.Y * ((v22 * cosAz) - v19);
        transform.M22 = scale.Y * ((v22 * sinAz) + v18);
        transform.M23 = scale.Y * sinAx * cosAy;

        transform.M31 = scale.Z * (v18 * sinAy + sinAx * sinAz);
        transform.M32 = scale.Z
            * (v17 + (v19 * sinAy) - (sinAx * cosAz));
        transform.M33 = scale.Z * v20;

        transform.M41 = position.X;
        transform.M42 = position.Y;
        transform.M43 = position.Z;

        transform.M14 = 0.0F;
        transform.M24 = 0.0F;
        transform.M34 = 0.0F;
        transform.M44 = 1.0F;

        return transform;
    }

    void Model::AnimateNodes(
        std::int32_t index,
        bool useNodeTransform,
        Matrix4 parentTansform,
        Vector3 scale,
        const std::shared_ptr<AnimationInfo>& info)
    {
        const auto& nodes = RequireReference(Nodes);
        for (std::int32_t i = index; i != -1;)
        {
            auto& node = RequireReference(nodes.at(static_cast<std::size_t>(i)));
            Matrix4 transform = useNodeTransform ? node.Transform : Identity();

            auto& animationInfo = RequireReference(info);
            const auto group = RequireReference(animationInfo.Node).Group;
            if (group)
            {
                const auto& animations = RequireReference(group->Animations);
                const auto found = animations.find(node.Name);
                if (found != animations.end())
                {
                    transform = AnimateNode(
                        group, found->second, scale, animationInfo.NodeFrame());
                    if (node.ParentIndex != -1 && !node.AnimIgnoreParent)
                    {
                        transform = Multiply(
                            transform,
                            RequireReference(nodes.at(static_cast<std::size_t>(
                                node.ParentIndex))).Animation);
                    }
                }
            }

            node.Animation = transform;
            if (node.ChildIndex != -1 && !node.AnimIgnoreChild)
            {
                AnimateNodes(
                    node.ChildIndex,
                    useNodeTransform,
                    parentTansform,
                    scale,
                    info);
            }
            if (node.AfterTransform.has_value())
            {
                node.Animation = Multiply(
                    Multiply(node.AfterTransform.value(), node.Animation),
                    parentTansform);
            }
            else if (node.BeforeTransform.has_value())
            {
                node.Animation = Multiply(
                    Multiply(node.Animation, parentTansform),
                    node.BeforeTransform.value());
            }
            node.Animation = Multiply(node.Animation, parentTansform);
            i = node.NextIndex;
        }
    }

    void Model::AnimateNodes2(
        std::int32_t index,
        bool useNodeTransform,
        Matrix4 parentTansform,
        Vector3 scale,
        const std::shared_ptr<AnimationInfo>& info)
    {
        const auto& nodes = RequireReference(Nodes);
        for (std::int32_t i = index; i != -1;)
        {
            auto& node = RequireReference(nodes.at(static_cast<std::size_t>(i)));
            Matrix4 transform = useNodeTransform ? node.Transform : Identity();

            auto& animationInfo = RequireReference(info);
            const auto group = RequireReference(animationInfo.Node).Group;
            if (group)
            {
                const auto& animations = RequireReference(group->Animations);
                const auto found = animations.find(node.Name);
                if (found != animations.end())
                {
                    transform = AnimateNode(
                        group, found->second, scale, animationInfo.NodeFrame());
                    if (node.ParentIndex != -1 && !node.AnimIgnoreParent)
                    {
                        transform = Multiply(
                            transform,
                            RequireReference(nodes.at(static_cast<std::size_t>(
                                node.ParentIndex))).Animation);
                    }
                }
            }

            node.Animation = transform;
            if (node.AfterTransform.has_value())
            {
                node.Animation = Multiply(
                    Multiply(node.AfterTransform.value(), node.Animation),
                    parentTansform);
            }
            else if (node.BeforeTransform.has_value())
            {
                node.Animation = Multiply(
                    Multiply(node.Animation, parentTansform),
                    node.BeforeTransform.value());
            }
            if (node.ChildIndex != -1 && !node.AnimIgnoreChild)
            {
                AnimateNodes2(
                    node.ChildIndex,
                    useNodeTransform,
                    parentTansform,
                    scale,
                    info);
            }
            node.Animation = Multiply(node.Animation, parentTansform);
            i = node.NextIndex;
        }
    }

    Matrix4 Model::AnimateNode(
        const std::shared_ptr<NodeAnimationGroup>& group,
        NodeAnimation animation,
        Vector3 modelScale,
        std::int32_t currentFrame) const
    {
        auto& value = RequireReference(group);

        const float scaleX = InterpolateAnimation(
            value.Scales, animation.ScaleLutIndexX, currentFrame,
            animation.ScaleBlendX, animation.ScaleLutLengthX, value.FrameCount);
        const float scaleY = InterpolateAnimation(
            value.Scales, animation.ScaleLutIndexY, currentFrame,
            animation.ScaleBlendY, animation.ScaleLutLengthY, value.FrameCount);
        const float scaleZ = InterpolateAnimation(
            value.Scales, animation.ScaleLutIndexZ, currentFrame,
            animation.ScaleBlendZ, animation.ScaleLutLengthZ, value.FrameCount);
        const float rotateX = InterpolateAnimation(
            value.Rotations, animation.RotateLutIndexX, currentFrame,
            animation.RotateBlendX, animation.RotateLutLengthX,
            value.FrameCount, true);
        const float rotateY = InterpolateAnimation(
            value.Rotations, animation.RotateLutIndexY, currentFrame,
            animation.RotateBlendY, animation.RotateLutLengthY,
            value.FrameCount, true);
        const float rotateZ = InterpolateAnimation(
            value.Rotations, animation.RotateLutIndexZ, currentFrame,
            animation.RotateBlendZ, animation.RotateLutLengthZ,
            value.FrameCount, true);
        const float translateX = InterpolateAnimation(
            value.Translations, animation.TranslateLutIndexX, currentFrame,
            animation.TranslateBlendX, animation.TranslateLutLengthX,
            value.FrameCount);
        const float translateY = InterpolateAnimation(
            value.Translations, animation.TranslateLutIndexY, currentFrame,
            animation.TranslateBlendY, animation.TranslateLutLengthY,
            value.FrameCount);
        const float translateZ = InterpolateAnimation(
            value.Translations, animation.TranslateLutIndexZ, currentFrame,
            animation.TranslateBlendZ, animation.TranslateLutLengthZ,
            value.FrameCount);

        Matrix4 nodeMatrix = CreateTranslation(
            translateX / modelScale.X,
            translateY / modelScale.Y,
            translateZ / modelScale.Z);
        nodeMatrix = Multiply(
            Multiply(
                Multiply(
                    CreateRotationX(rotateX),
                    CreateRotationY(rotateY)),
                CreateRotationZ(rotateZ)),
            nodeMatrix);
        nodeMatrix = Multiply(
            CreateScale(scaleX, scaleY, scaleZ),
            nodeMatrix);
        return nodeMatrix;
    }

    void Model::AnimateMaterials(const std::shared_ptr<AnimationInfo>& info)
    {
        const auto& materials = RequireReference(Materials);
        for (std::size_t i = 0; i < materials.size(); ++i)
        {
            auto& material = RequireReference(materials[i]);
            material.CurrentDiffuse = material.Diffuse / 31.0F;
            material.CurrentAmbient = material.Ambient / 31.0F;
            material.CurrentSpecular = material.Specular / 31.0F;
            material.CurrentAlpha = material.Alpha / 31.0F;

            auto& animationInfo = RequireReference(info);
            const auto group = RequireReference(animationInfo.Material).Group;
            if (group)
            {
                const auto& animations = RequireReference(group->Animations);
                const auto found = animations.find(material.Name);
                if (found != animations.end())
                {
                    const MaterialAnimation& animation = found->second;
                    const std::int32_t currentFrame
                        = animationInfo.MaterialFrame();

                    if (!TypeExtensions::TestFlag(
                        material.AnimationFlags,
                        MatAnimFlags::DisableColor))
                    {
                        const float diffuseR = InterpolateAnimation(
                            group->Colors, animation.DiffuseLutIndexR,
                            currentFrame, animation.DiffuseBlendR,
                            animation.DiffuseLutLengthR, group->FrameCount);
                        const float diffuseG = InterpolateAnimation(
                            group->Colors, animation.DiffuseLutIndexG,
                            currentFrame, animation.DiffuseBlendG,
                            animation.DiffuseLutLengthG, group->FrameCount);
                        const float diffuseB = InterpolateAnimation(
                            group->Colors, animation.DiffuseLutIndexB,
                            currentFrame, animation.DiffuseBlendB,
                            animation.DiffuseLutLengthB, group->FrameCount);
                        const float ambientR = InterpolateAnimation(
                            group->Colors, animation.AmbientLutIndexR,
                            currentFrame, animation.AmbientBlendR,
                            animation.AmbientLutLengthR, group->FrameCount);
                        const float ambientG = InterpolateAnimation(
                            group->Colors, animation.AmbientLutIndexG,
                            currentFrame, animation.AmbientBlendG,
                            animation.AmbientLutLengthG, group->FrameCount);
                        const float ambientB = InterpolateAnimation(
                            group->Colors, animation.AmbientLutIndexB,
                            currentFrame, animation.AmbientBlendB,
                            animation.AmbientLutLengthB, group->FrameCount);
                        const float specularR = InterpolateAnimation(
                            group->Colors, animation.SpecularLutIndexR,
                            currentFrame, animation.SpecularBlendR,
                            animation.SpecularLutLengthR, group->FrameCount);
                        const float specularG = InterpolateAnimation(
                            group->Colors, animation.SpecularLutIndexG,
                            currentFrame, animation.SpecularBlendG,
                            animation.SpecularLutLengthG, group->FrameCount);
                        const float specularB = InterpolateAnimation(
                            group->Colors, animation.SpecularLutIndexB,
                            currentFrame, animation.SpecularBlendB,
                            animation.SpecularLutLengthB, group->FrameCount);

                        material.CurrentDiffuse = Vector3(
                            diffuseR / 31.0F,
                            diffuseG / 31.0F,
                            diffuseB / 31.0F);
                        material.CurrentAmbient = Vector3(
                            ambientR / 31.0F,
                            ambientG / 31.0F,
                            ambientB / 31.0F);
                        material.CurrentSpecular = Vector3(
                            specularR / 31.0F,
                            specularG / 31.0F,
                            specularB / 31.0F);
                    }
                    if (!TypeExtensions::TestFlag(
                        material.AnimationFlags,
                        MatAnimFlags::DisableAlpha))
                    {
                        material.CurrentAlpha = InterpolateAnimation(
                            group->Colors, animation.AlphaLutIndex,
                            currentFrame, animation.AlphaBlend,
                            animation.AlphaLutLength, group->FrameCount)
                            / 31.0F;
                    }
                }
            }
        }
    }

    Matrix4 Model::AnimateTexcoords(
        const std::shared_ptr<TexcoordAnimationGroup>& group,
        TexcoordAnimation animation,
        std::int32_t currentFrame)
    {
        auto& value = RequireReference(group);
        const float scaleS = InterpolateAnimation(
            value.Scales, animation.ScaleLutIndexS, currentFrame,
            animation.ScaleBlendS, animation.ScaleLutLengthS, value.FrameCount);
        const float scaleT = InterpolateAnimation(
            value.Scales, animation.ScaleLutIndexT, currentFrame,
            animation.ScaleBlendT, animation.ScaleLutLengthT, value.FrameCount);
        const float rotate = InterpolateAnimation(
            value.Rotations, animation.RotateLutIndexZ, currentFrame,
            animation.RotateBlendZ, animation.RotateLutLengthZ,
            value.FrameCount, true);
        const float translateS = InterpolateAnimation(
            value.Translations, animation.TranslateLutIndexS, currentFrame,
            animation.TranslateBlendS, animation.TranslateLutLengthS,
            value.FrameCount);
        const float translateT = InterpolateAnimation(
            value.Translations, animation.TranslateLutIndexT, currentFrame,
            animation.TranslateBlendT, animation.TranslateLutLengthT,
            value.FrameCount);

        Matrix4 textureMatrix = CreateTranslation(
            translateS, translateT, 0.0F);
        if (rotate != 0.0F)
        {
            textureMatrix = Multiply(
                CreateTranslation(0.5F, 0.5F, 0.0F),
                textureMatrix);
            textureMatrix = Multiply(
                CreateRotationZ(rotate),
                textureMatrix);
            textureMatrix = Multiply(
                CreateTranslation(-0.5F, -0.5F, 0.0F),
                textureMatrix);
        }
        textureMatrix = Multiply(
            CreateScale(scaleS, scaleT, 1.0F),
            textureMatrix);
        return textureMatrix;
    }

    void Model::AnimateTextures(const std::shared_ptr<AnimationInfo>& info)
    {
        const auto& materials = RequireReference(Materials);
        for (std::size_t i = 0; i < materials.size(); ++i)
        {
            auto& material = RequireReference(materials[i]);
            material.CurrentTextureId = material.TextureId;
            material.CurrentPaletteId = material.PaletteId;

            auto& animationInfo = RequireReference(info);
            const auto group = RequireReference(animationInfo.Texture).Group;
            if (group)
            {
                const auto& animations = RequireReference(group->Animations);
                const auto found = animations.find(material.Name);
                if (found != animations.end())
                {
                    const TextureAnimation& animation = found->second;
                    const std::int32_t end = static_cast<std::int32_t>(
                        animation.StartIndex)
                        + static_cast<std::int32_t>(animation.Count);
                    // A translated Quake 3 animMap (MapPacker) repeats over
                    // Field2A frames, which no cartridge animation sets: each
                    // image from its frame until the next one's.
                    if (animation.Field2A != 0)
                    {
                        const std::int32_t at = animationInfo.TextureFrame() % animation.Field2A;
                        for (std::int32_t j = animation.StartIndex; j < end; ++j)
                        {
                            if (ManagedAt(group->FrameIndices, j) <= at)
                            {
                                material.CurrentTextureId = ManagedAt(group->TextureIds, j);
                                material.CurrentPaletteId = ManagedAt(group->PaletteIds, j);
                            }
                        }
                        continue;
                    }
                    for (std::int32_t j = animation.StartIndex; j < end; ++j)
                    {
                        if (ManagedAt(group->FrameIndices, j)
                            == animationInfo.TextureFrame())
                        {
                            material.CurrentTextureId = ManagedAt(group->TextureIds, j);
                            material.CurrentPaletteId = ManagedAt(group->PaletteIds, j);
                            break;
                        }
                    }
                }
            }
        }
    }

    float Model::InterpolateAnimation(
        const std::shared_ptr<const std::vector<float>>& values,
        std::int32_t start,
        std::int32_t frame,
        std::int32_t blend,
        std::int32_t lutLength,
        std::int32_t frameCount,
        bool isRotation) const
    {
        if (lutLength == 1)
        {
            return ManagedAt(values, start);
        }
        if (blend == 1)
        {
            return ManagedAt(values, UncheckedAdd(start, frame));
        }

        const std::int32_t shift = ShiftRight(blend, 1);
        const std::int32_t limit = ShiftLeft(
            ShiftRight(UncheckedSubtract(frameCount, 1), shift),
            shift);
        if (frame >= limit)
        {
            const std::int32_t tail = UncheckedSubtract(
                UncheckedSubtract(frameCount, limit),
                UncheckedSubtract(frame, limit));
            return ManagedAt(
                values,
                UncheckedSubtract(UncheckedAdd(start, lutLength), tail));
        }

        if (blend == 0)
        {
            NativeRuntime::ThrowDivideByZeroException();
        }
        if (frame == std::numeric_limits<std::int32_t>::min()
            && blend == -1)
        {
            throw System::OverflowException();
        }

        const std::int32_t index = frame / blend;
        const std::int32_t remainder = frame % blend;
        if (remainder == 0)
        {
            return ManagedAt(values, UncheckedAdd(start, index));
        }

        float first = ManagedAt(values, UncheckedAdd(start, index));
        float second = ManagedAt(values, UncheckedAdd(UncheckedAdd(start, index), 1));
        if (isRotation)
        {
            if (first - second > std::numbers::pi_v<float>)
            {
                second += std::numbers::pi_v<float> * 2.0F;
            }
            else if (first - second < -std::numbers::pi_v<float>)
            {
                first += std::numbers::pi_v<float> * 2.0F;
            }
        }

        const float factor = 1.0F / static_cast<float>(blend)
            * static_cast<float>(remainder);
        return first + (second - first) * factor;
    }

    void Model::UpdateMatrixStack()
    {
        const auto& ids = RequireReference(NodeMatrixIds);
        const auto& nodes = RequireReference(Nodes);
        for (std::size_t i = 0; i < ids.size(); ++i)
        {
            auto& node = RequireReference(nodes.at(static_cast<std::size_t>(ids[i])));
            Matrix4 transform = node.Animation;
            if (node.BillboardMode == BillboardMode::Sphere
                || node.BillboardMode == BillboardMode::Cylinder)
            {
                transform = ClearRotation(transform);
            }
            SetMatrixStackValues(static_cast<std::int32_t>(i), transform);
        }
    }

    void Model::SetMatrixStackValues(std::int32_t index, Matrix4 matrix)
    {
        auto& values = RequireReference(_matrixStackValues);
        const std::int32_t offset = UncheckedMultiply(16, index);
        values[static_cast<std::size_t>(UncheckedAdd(offset, 0))] = matrix.M11;
        values[static_cast<std::size_t>(UncheckedAdd(offset, 1))] = matrix.M12;
        values[static_cast<std::size_t>(UncheckedAdd(offset, 2))] = matrix.M13;
        values[static_cast<std::size_t>(UncheckedAdd(offset, 3))] = matrix.M14;
        values[static_cast<std::size_t>(UncheckedAdd(offset, 4))] = matrix.M21;
        values[static_cast<std::size_t>(UncheckedAdd(offset, 5))] = matrix.M22;
        values[static_cast<std::size_t>(UncheckedAdd(offset, 6))] = matrix.M23;
        values[static_cast<std::size_t>(UncheckedAdd(offset, 7))] = matrix.M24;
        values[static_cast<std::size_t>(UncheckedAdd(offset, 8))] = matrix.M31;
        values[static_cast<std::size_t>(UncheckedAdd(offset, 9))] = matrix.M32;
        values[static_cast<std::size_t>(UncheckedAdd(offset, 10))] = matrix.M33;
        values[static_cast<std::size_t>(UncheckedAdd(offset, 11))] = matrix.M34;
        values[static_cast<std::size_t>(UncheckedAdd(offset, 12))] = matrix.M41;
        values[static_cast<std::size_t>(UncheckedAdd(offset, 13))] = matrix.M42;
        values[static_cast<std::size_t>(UncheckedAdd(offset, 14))] = matrix.M43;
        values[static_cast<std::size_t>(UncheckedAdd(offset, 15))] = matrix.M44;
    }

    bool Model::NodeParentsEnabled(const std::shared_ptr<Node>& node) const
    {
        const auto& nodes = RequireReference(Nodes);
        std::int32_t parentIndex = RequireReference(node).ParentIndex;
        while (parentIndex != -1)
        {
            auto& parent = RequireReference(
                nodes.at(static_cast<std::size_t>(parentIndex)));
            if (!parent.Enabled)
            {
                return false;
            }
            parentIndex = parent.ParentIndex;
        }
        return true;
    }

    std::shared_ptr<Node> Model::GetNodeByName(const std::string& name) const
    {
        for (const auto& node : RequireReference(Nodes))
        {
            if (RequireReference(node).Name == name)
            {
                return node;
            }
        }
        return {};
    }

    std::int32_t Model::GetNodeIndexByName(const std::string& name) const
    {
        const auto& nodes = RequireReference(Nodes);
        for (std::size_t i = 0; i < nodes.size(); ++i)
        {
            if (RequireReference(nodes[i]).Name == name)
            {
                return static_cast<std::int32_t>(i);
            }
        }
        return -1;
    }

    std::shared_ptr<Material> Model::GetMaterialByName(
        const std::string& name) const
    {
        for (const auto& material : RequireReference(Materials))
        {
            if (RequireReference(material).Name == name)
            {
                return material;
            }
        }
        return {};
    }

    std::vector<ColorRgba> Model::GetPixels(
        std::int32_t textureId,
        std::int32_t paletteId,
        std::int32_t recolorId) const
    {
        const auto& recolors = RequireReference(Recolors);
        auto& recolor = RequireReference(
            recolors.at(static_cast<std::size_t>(recolorId)));

        if (textureId < 0)
        {
            throw std::invalid_argument("textureId");
        }
        const auto& textureData = RequireReference(recolor.TextureData);
        if (textureId >= static_cast<std::int32_t>(textureData.size()))
        {
            throw std::invalid_argument("textureId");
        }

        std::vector<ColorRgba> pixels;
        const TextureFormat textureFormat
            = ManagedAt(recolor.Textures, textureId).Format;

        if (textureFormat == TextureFormat::DirectRgb)
        {
            const auto& source
                = RequireReference(textureData.at(static_cast<std::size_t>(textureId)));
            pixels.reserve(source.size());
            for (const MphRead::TextureData& data : source)
            {
                pixels.emplace_back(data.Data, data.Alpha);
            }
        }
        else
        {
            if (paletteId < 0)
            {
                throw std::invalid_argument("paletteId");
            }
            const auto& paletteData = RequireReference(recolor.PaletteData);
            if (paletteId >= static_cast<std::int32_t>(paletteData.size()))
            {
                throw std::invalid_argument("paletteId");
            }

            const auto& source
                = RequireReference(textureData.at(static_cast<std::size_t>(textureId)));
            pixels.reserve(source.size());
            for (const MphRead::TextureData& data : source)
            {
                const std::int32_t index = std::bit_cast<std::int32_t>(data.Data);
                const auto& palette = RequireReference(
                    paletteData.at(static_cast<std::size_t>(paletteId)));
                const std::uint16_t color
                    = palette.at(static_cast<std::size_t>(index)).Data;
                pixels.emplace_back(color, data.Alpha);
            }
        }
        return pixels;
    }

    Recolor::Recolor(
        std::string name,
        std::shared_ptr<const std::vector<Texture>> textures,
        std::shared_ptr<const std::vector<Palette>> palettes,
        std::shared_ptr<const std::vector<
            std::shared_ptr<const std::vector<MphRead::TextureData>>>> textureData,
        std::shared_ptr<const std::vector<
            std::shared_ptr<const std::vector<MphRead::PaletteData>>>> paletteData)
        : Recolor(BuildInit(
            std::move(name),
            std::move(textures),
            std::move(palettes),
            std::move(textureData),
            std::move(paletteData)))
    {
    }

    Recolor::Recolor(Init init)
        : Name(std::move(init.Name)),
          Textures(std::move(init.Textures)),
          Palettes(std::move(init.Palettes)),
          TextureData(std::move(init.TextureData)),
          PaletteData(std::move(init.PaletteData))
    {
    }

    Recolor::Init Recolor::BuildInit(
        std::string name,
        std::shared_ptr<const std::vector<Texture>> textures,
        std::shared_ptr<const std::vector<Palette>> palettes,
        std::shared_ptr<const std::vector<
            std::shared_ptr<const std::vector<MphRead::TextureData>>>> textureData,
        std::shared_ptr<const std::vector<
            std::shared_ptr<const std::vector<MphRead::PaletteData>>>> paletteData)
    {
        ThrowIfInvalidEnums(textures);
#ifndef NDEBUG
        assert(RequireReference(textures).size() == RequireReference(textureData).size());
        assert(RequireReference(palettes).size() == RequireReference(paletteData).size());
#endif
        return Init{
            std::move(name),
            std::move(textures),
            std::move(palettes),
            std::move(textureData),
            std::move(paletteData)};
    }

    std::vector<ColorRgba> Recolor::GetPixels(
        std::int32_t textureId,
        std::int32_t palettteId) const
    {
        if (textureId < 0)
        {
            throw std::invalid_argument("textureId");
        }
        const auto& textureData = RequireReference(TextureData);
        if (textureId >= static_cast<std::int32_t>(textureData.size()))
        {
            throw std::invalid_argument("textureId");
        }

        std::vector<ColorRgba> pixels;
        const TextureFormat textureFormat = ManagedAt(Textures, textureId).Format;

        if (textureFormat == TextureFormat::DirectRgb)
        {
            const auto& source = RequireReference(
                textureData.at(static_cast<std::size_t>(textureId)));
            pixels.reserve(source.size());
            for (const MphRead::TextureData& data : source)
            {
                pixels.push_back(ColorFromShort(data.Data, data.Alpha));
            }
        }
        else
        {
            if (palettteId < 0)
            {
                throw std::invalid_argument("palettteId");
            }
            const auto& paletteData = RequireReference(PaletteData);
            if (palettteId >= static_cast<std::int32_t>(paletteData.size()))
            {
                throw std::invalid_argument("palettteId");
            }

            const auto& source = RequireReference(
                textureData.at(static_cast<std::size_t>(textureId)));
            pixels.reserve(source.size());
            for (const MphRead::TextureData& data : source)
            {
                const std::int32_t index = std::bit_cast<std::int32_t>(data.Data);
                const auto& palette = RequireReference(
                    paletteData.at(static_cast<std::size_t>(palettteId)));
                const std::uint16_t color
                    = palette.at(static_cast<std::size_t>(index)).Data;
                pixels.push_back(ColorFromShort(color, data.Alpha));
            }
        }
        return pixels;
    }

    std::vector<ColorRgba> Recolor::GetPalettePixels(
        std::int32_t palettteId) const
    {
        if (palettteId < 0)
        {
            throw std::invalid_argument("palettteId");
        }
        const auto& paletteData = RequireReference(PaletteData);
        if (palettteId >= static_cast<std::int32_t>(paletteData.size()))
        {
            throw std::invalid_argument("palettteId");
        }

        const auto& palette = RequireReference(
            paletteData.at(static_cast<std::size_t>(palettteId)));
        std::vector<ColorRgba> pixels;
        pixels.reserve(palette.size());
        for (const MphRead::PaletteData& data : palette)
        {
            pixels.push_back(ColorFromShort(data.Data, 255));
        }
        return pixels;
    }

    ColorRgba Recolor::ColorFromShort(
        std::uint32_t value,
        std::uint8_t alpha) const noexcept
    {
        return ColorRgba(value, alpha);
    }

    void Recolor::ThrowIfInvalidEnums(
        const std::shared_ptr<const std::vector<Texture>>& textures)
    {
        for (const Texture& texture : RequireReference(textures))
        {
            if (!IsDefinedTextureFormat(texture.Format))
            {
                throw ProgramException(
                    "Invalid texture format "
                    + TextureFormatString(texture.Format)
                    + ".");
            }
        }
    }
}
