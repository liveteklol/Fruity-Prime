#pragma once

#include "Bindings.hpp"
#include "ShaderConstants.hpp"
#include "VertexSemantics.hpp"

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string_view>

// Logical scene inputs. A group is an update/lifetime boundary, not a Vulkan
// descriptor set or a byte packing convention. API adapters choose physical
// indices and pack ShaderConstants into their own representation.
namespace MphRead::NativeRuntime::Rhi::SceneShaderAbi
{
    enum class Group : std::uint8_t { Frame, Material, Draw, Post, Count };
    inline constexpr std::uint32_t GroupCount = static_cast<std::uint32_t>(Group::Count);

    struct Binding final
    {
        Group group;
        std::uint32_t binding;
        BindingType type;
        std::string_view semantic;
        bool smallConstants = false;

        bool operator==(const Binding&) const = default;
    };

#define RHI_SCENE_TYPE_UniformBuffer BindingType::UniformBuffer
#define RHI_SCENE_TYPE_SampledTexture BindingType::SampledTexture
#define RHI_SCENE_TYPE_Sampler BindingType::Sampler
#define RHI_SCENE_TYPE_SmallConstants BindingType::UniformBuffer
#define RHI_SCENE_SMALL_UniformBuffer false
#define RHI_SCENE_SMALL_SampledTexture false
#define RHI_SCENE_SMALL_Sampler false
#define RHI_SCENE_SMALL_SmallConstants true
#define RHI_SCENE_BINDING(name, group, index, type, semantic) \
    inline constexpr Binding name{Group::group, index, RHI_SCENE_TYPE_##type, semantic, RHI_SCENE_SMALL_##type};
#define RHI_SCENE_CONSTANT(...)
#define RHI_SCENE_TEXTURE(...)
#include "SceneShaderAbi.def"
#undef RHI_SCENE_BINDING
#undef RHI_SCENE_TYPE_UniformBuffer
#undef RHI_SCENE_TYPE_SampledTexture
#undef RHI_SCENE_TYPE_Sampler
#undef RHI_SCENE_TYPE_SmallConstants
#undef RHI_SCENE_SMALL_UniformBuffer
#undef RHI_SCENE_SMALL_SampledTexture
#undef RHI_SCENE_SMALL_Sampler
#undef RHI_SCENE_SMALL_SmallConstants
#define RHI_SCENE_BINDING(name, ...) name,
    inline constexpr std::array Bindings{
#include "SceneShaderAbi.def"
    };
#undef RHI_SCENE_BINDING
#undef RHI_SCENE_CONSTANT
#undef RHI_SCENE_TEXTURE

    enum class ValueType : std::uint8_t { Bool, Int, Float, Vec3, Vec4, Mat4 };
    struct Constant final
    {
        std::string_view program, name;
        ValueType type;
        std::uint32_t count;
        Binding block;
    };
    struct Texture final
    {
        std::string_view program, name;
        Binding image, sampler;
        std::uint32_t unit;
    };
#define RHI_SCENE_BINDING(...)
#define RHI_SCENE_CONSTANT(program, name, type, count, block) Constant{program, name, ValueType::type, count, block},
#define RHI_SCENE_TEXTURE(...)
    inline constexpr std::array Constants{
#include "SceneShaderAbi.def"
    };
#undef RHI_SCENE_CONSTANT
#undef RHI_SCENE_TEXTURE
#define RHI_SCENE_CONSTANT(...)
#define RHI_SCENE_TEXTURE(program, name, image, sampler, unit) Texture{program, name, image, sampler, unit},
    inline constexpr std::array Textures{
#include "SceneShaderAbi.def"
    };
#undef RHI_SCENE_BINDING
#undef RHI_SCENE_CONSTANT
#undef RHI_SCENE_TEXTURE

    [[nodiscard]] constexpr std::uint32_t TextureUnit(std::string_view program, std::string_view name)
    {
        for (const auto& texture : Textures)
            if (texture.program == program && texture.name == name) return texture.unit;
        throw std::out_of_range("Unknown logical scene texture.");
    }

    [[nodiscard]] constexpr bool IsValid() noexcept
    {
        for (std::size_t i = 0; i < Bindings.size(); ++i)
        {
            if (Bindings[i].group >= Group::Count || Bindings[i].semantic.empty()) return false;
            for (std::size_t j = 0; j < i; ++j)
                if ((Bindings[i].group == Bindings[j].group && Bindings[i].binding == Bindings[j].binding)
                    || Bindings[i].semantic == Bindings[j].semantic) return false;
        }
        for (std::size_t i = 0; i < Constants.size(); ++i)
        {
            const auto& c = Constants[i];
            if (c.program.empty() || c.name.empty() || c.block.type != BindingType::UniformBuffer) return false;
            for (std::size_t j = 0; j < i; ++j)
                if (c.program == Constants[j].program && c.name == Constants[j].name) return false;
        }
        for (std::size_t i = 0; i < Textures.size(); ++i)
        {
            const auto& t = Textures[i];
            if (t.program.empty() || t.name.empty() || t.image.type != BindingType::SampledTexture
                || t.sampler.type != BindingType::Sampler || t.image.group != t.sampler.group || t.unit >= 4) return false;
            for (std::size_t j = 0; j < i; ++j)
                if (t.program == Textures[j].program && (t.name == Textures[j].name || t.unit == Textures[j].unit)) return false;
        }
        return true;
    }
    static_assert(IsValid());
}
