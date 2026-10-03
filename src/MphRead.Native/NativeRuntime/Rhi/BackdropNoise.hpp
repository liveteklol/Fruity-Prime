#pragma once
#include "Resources.hpp"
#include <array>
#include <memory>

namespace MphRead::NativeRuntime::Rhi
{
    class GraphicsDevice;
    inline constexpr std::uint32_t BackdropNoiseSize = 64;
    [[nodiscard]] constexpr std::uint32_t BackdropNoiseHash(std::uint32_t x, std::uint32_t y) noexcept
    {
        std::uint32_t h = x * 0x9E3779B1u + y * 0x85EBCA77u + 0xC2B2AE3Du;
        h ^= h >> 16; h *= 0x7FEB352Du;
        h ^= h >> 15; h *= 0x846CA68Bu;
        h ^= h >> 16; return h;
    }
    // Unsigned arithmetic only; identical bytes on every backend/platform.
    [[nodiscard]] constexpr auto BackdropNoiseBytes() noexcept
    {
        std::array<std::uint8_t, BackdropNoiseSize * BackdropNoiseSize * 4> rgba{};
        for (std::uint32_t y = 0; y < BackdropNoiseSize; ++y)
            for (std::uint32_t x = 0; x < BackdropNoiseSize; ++x)
            {
                const auto i = (y * BackdropNoiseSize + x) * 4;
                const auto value = static_cast<std::uint8_t>(BackdropNoiseHash(x, y) >> 24);
                rgba[i] = rgba[i+1] = rgba[i+2] = value; rgba[i+3] = 255;
            }
        return rgba;
    }
    struct BackdropNoiseResources final
    {
        std::unique_ptr<Rhi::Texture> Texture;
        std::unique_ptr<Rhi::Sampler> Sampler;
    };
    [[nodiscard]] BackdropNoiseResources CreateBackdropNoiseResources(GraphicsDevice& device);
}
