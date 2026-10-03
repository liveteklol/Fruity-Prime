#pragma once

#include "Resources.hpp"

#include <cstdint>
#include <variant>
#include <vector>

namespace MphRead::NativeRuntime::Rhi
{
    enum class BindingType : std::uint8_t
    {
        UniformBuffer,
        StorageBuffer,
        SampledTexture,
        StorageTexture,
        Sampler
    };

    struct BindingLayoutEntry final
    {
        std::uint32_t binding = 0;
        BindingType type = BindingType::UniformBuffer;
        ShaderStage stages = ShaderStage::None;
        std::uint32_t count = 1;

        bool operator==(const BindingLayoutEntry&) const = default;
    };

    struct BindingLayoutDesc final
    {
        std::vector<BindingLayoutEntry> entries;

        bool operator==(const BindingLayoutDesc&) const = default;
    };

    // Complete, value-owned contract of a pipeline's logical groups. Backend
    // creation maps each group to its native layout. Keeping descriptions by
    // value lets a cached pipeline outlive the input BindingLayout resources.
    struct PipelineLayout final
    {
        std::vector<BindingLayoutDesc> groups;

        bool operator==(const PipelineLayout&) const = default;
    };

    struct BufferBinding final
    {
        const Buffer* buffer = nullptr;
        std::uint64_t offset = 0;
        std::uint64_t size = 0;

        bool operator==(const BufferBinding&) const = default;
    };

    struct TextureBinding final
    {
        const TextureView* view = nullptr;

        bool operator==(const TextureBinding&) const = default;
    };

    struct SamplerBinding final
    {
        const Sampler* sampler = nullptr;

        bool operator==(const SamplerBinding&) const = default;
    };

    using BindingResource = std::variant<BufferBinding, TextureBinding, SamplerBinding>;

    struct BindingSetEntry final
    {
        std::uint32_t binding = 0;
        BindingResource resource{BufferBinding{}};
        // Index within the layout entry's descriptor array.
        std::uint32_t arrayElement = 0;

        bool operator==(const BindingSetEntry&) const = default;
    };

    class BindingLayout;

    struct BindingSetDesc final
    {
        const BindingLayout* layout = nullptr;
        std::vector<BindingSetEntry> entries;

        bool operator==(const BindingSetDesc&) const = default;
    };

    class BindingLayout
    {
    public:
        virtual ~BindingLayout() = default;
        BindingLayout(const BindingLayout&) = delete;
        BindingLayout& operator=(const BindingLayout&) = delete;
        BindingLayout(BindingLayout&&) = delete;
        BindingLayout& operator=(BindingLayout&&) = delete;

        [[nodiscard]] virtual const BindingLayoutDesc& Desc() const noexcept = 0;

    protected:
        BindingLayout() = default;
    };

    class BindingSet
    {
    public:
        virtual ~BindingSet() = default;
        BindingSet(const BindingSet&) = delete;
        BindingSet& operator=(const BindingSet&) = delete;
        BindingSet(BindingSet&&) = delete;
        BindingSet& operator=(BindingSet&&) = delete;

        [[nodiscard]] virtual const BindingSetDesc& Desc() const noexcept = 0;

    protected:
        BindingSet() = default;
    };
}
