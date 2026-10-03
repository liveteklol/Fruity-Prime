#pragma once

#include "../SceneShaderAbi.hpp"
#include <algorithm>
#include <cstring>
#include <span>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace MphRead::NativeRuntime::Rhi::Vulkan
{
    // The production std140 packing adapter. Descriptions are generated from
    // logical values and independently checked against compiled SPIR-V.
    class VulkanSceneUniforms
    {
    public:
        struct BlockDesc final
        {
            std::string_view semantic;
            std::uint32_t group, binding, size;
            bool small = false;
        };
        struct MemberDesc final
        {
            std::string_view name;
            std::uint32_t block;
            SceneShaderAbi::ValueType type;
            std::uint32_t offset, size, count;
        };
        struct Block final
        {
            std::uint32_t Group, Binding;
            std::vector<std::byte> Data;
            std::uint64_t Generation = 1;
            bool Small = false;
        };
        VulkanSceneUniforms(std::span<const BlockDesc> blocks, std::span<const MemberDesc> members)
        {
            for (const auto& desc : blocks)
            {
                if (desc.group >= SceneShaderAbi::GroupCount || !desc.size || desc.size % 16)
                    throw std::invalid_argument("Invalid Vulkan scene uniform block.");
                for (const auto& previous : Blocks)
                    if (previous.Group == desc.group && previous.Binding == desc.binding)
                        throw std::invalid_argument("Duplicate Vulkan scene uniform block.");
                if (desc.small && desc.size > 128) throw std::invalid_argument("Small constant budget exceeded.");
                Blocks.push_back({desc.group, desc.binding, std::vector<std::byte>(desc.size), 1, desc.small});
            }
            for (const auto& desc : members)
            {
                if (desc.name.empty() || desc.block >= Blocks.size())
                    throw std::invalid_argument("Invalid Vulkan scene uniform member.");
                const auto width = ValueSize(desc.type);
                const auto size = desc.count ? std::uint64_t(desc.count) * ((width + 15) / 16 * 16) : width;
                const auto capacity = Blocks[desc.block].Data.size();
                const auto alignment = desc.count || width >= 12 ? 16U : 4U;
                if (desc.size != size || desc.offset % alignment || desc.offset > capacity || desc.size > capacity - desc.offset)
                    throw std::invalid_argument("Vulkan scene uniform member exceeds its ABI.");
                for (const auto& [name, previous] : Members)
                    if (previous.block == desc.block && desc.offset < previous.offset + previous.size
                        && previous.offset < desc.offset + desc.size)
                        throw std::invalid_argument("Overlapping Vulkan scene uniform members.");
                if (!Members.emplace(desc.name, desc).second)
                    throw std::invalid_argument("Duplicate Vulkan scene uniform member.");
            }
        }
        [[nodiscard]] const MemberDesc* Find(std::string_view name) const
        {
            const auto found = Members.find(name);
            return found == Members.end() ? nullptr : &found->second;
        }
        void Write(std::string_view name, const void* data, std::size_t size, SceneShaderAbi::ValueType type)
        {
            const auto* member = Find(name);
            if (!member) return; // OpenGL's inactive uniform semantics
            const auto intBool = type == SceneShaderAbi::ValueType::Int && member->type == SceneShaderAbi::ValueType::Bool;
            if (member->count || size != member->size || (type != member->type && !intBool) || !data)
                throw std::invalid_argument("Vulkan scene constant write does not match the shader ABI.");
            if (intBool)
            {
                std::int32_t value; std::memcpy(&value, data, sizeof(value));
                if (value != 0 && value != 1) throw std::invalid_argument("Invalid Vulkan scene boolean.");
            }
            auto& block = Blocks[member->block];
            auto* destination = block.Data.data() + member->offset;
            if (std::memcmp(destination, data, size) != 0)
            { std::memcpy(destination, data, size); ++block.Generation; }
        }
        void WriteArray(std::string_view name, const float* data, std::size_t elementFloats, std::size_t count)
        {
            const auto* member = Find(name);
            if (!member) return;
            if (!member->count || elementFloats != ValueSize(member->type) / sizeof(float) || (count && !data))
                throw std::invalid_argument("Vulkan scene array write does not match the shader ABI.");
            const auto stride = member->size / member->count;
            count = std::min<std::size_t>(count, member->count);
            auto& block = Blocks[member->block];
            bool changed = false;
            for (std::size_t i = 0; i < count; ++i)
            {
                auto* destination = block.Data.data() + member->offset + i * stride;
                const auto* source = data + i * elementFloats;
                const auto size = ValueSize(member->type);
                if (std::memcmp(destination, source, size) != 0)
                { std::memcpy(destination, source, size); changed = true; }
            }
            if (changed) ++block.Generation;
        }
        std::vector<Block> Blocks;
    private:
        static std::uint32_t ValueSize(SceneShaderAbi::ValueType type)
        {
            using SceneShaderAbi::ValueType;
            switch (type)
            {
            case ValueType::Bool: case ValueType::Int: case ValueType::Float: return 4;
            case ValueType::Vec3: return 12;
            case ValueType::Vec4: return 16;
            case ValueType::Mat4: return 64;
            }
            throw std::invalid_argument("Unknown Vulkan scene value type.");
        }
        std::unordered_map<std::string_view, MemberDesc> Members;
    };
}
