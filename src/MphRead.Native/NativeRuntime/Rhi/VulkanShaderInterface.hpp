#pragma once

#include "SceneShaderAbi.hpp"

#include <cstdint>

// Native binding adapter for the shared logical scene contract. Production
// std140 layouts are generated from SceneShaderAbi.def, verified against
// SPIR-V at build time, and packed by VulkanSceneUniforms.hpp.
namespace MphRead::NativeRuntime::Rhi::Vulkan
{
    // Descriptor sets by update frequency, so a draw rebinds only what changed.
    [[nodiscard]] constexpr std::uint32_t DescriptorSet(SceneShaderAbi::Group group) noexcept
    {
        return static_cast<std::uint32_t>(group);
    }
    inline constexpr auto FrameSet = DescriptorSet(SceneShaderAbi::Group::Frame);
    inline constexpr auto MaterialSet = DescriptorSet(SceneShaderAbi::Group::Material);
    inline constexpr auto DrawSet = DescriptorSet(SceneShaderAbi::Group::Draw);
    inline constexpr auto PostSet = DescriptorSet(SceneShaderAbi::Group::Post);

    struct BlockBinding final
    {
        std::uint32_t Set;
        std::uint32_t Binding;
    };

    [[nodiscard]] constexpr BlockBinding MapBinding(SceneShaderAbi::Binding binding) noexcept
    {
        return {DescriptorSet(binding.group), binding.binding};
    }
    inline constexpr auto FrameBlock = MapBinding(SceneShaderAbi::Frame);
    inline constexpr auto SceneLightBlock = MapBinding(SceneShaderAbi::Light);
    inline constexpr auto SceneFogBlock = MapBinding(SceneShaderAbi::Fog);
    inline constexpr auto MaterialBlock = MapBinding(SceneShaderAbi::Material);
    inline constexpr auto MaterialTexture = MapBinding(SceneShaderAbi::MaterialTexture);
    inline constexpr auto DrawBlock = MapBinding(SceneShaderAbi::Draw);
    inline constexpr auto CelPostBlock = MapBinding(SceneShaderAbi::Cel);
    inline constexpr auto HudPostBlock = MapBinding(SceneShaderAbi::Hud);
    inline constexpr auto DisruptionPostBlock = MapBinding(SceneShaderAbi::Disruption);
    inline constexpr auto DisruptionTablesBlock = MapBinding(SceneShaderAbi::DisruptionTables);

}
