#pragma once

#include <atomic>
#include <cstdlib>
#include <string_view>

namespace MphRead::NativeRuntime::Rhi::FastScene
{
    // Performance mode's scene path, drawn the way the cpp-port renderer
    // draws: on Vulkan the main program's draws read their constant blocks
    // from per-frame storage-buffer records indexed by push constants
    // (VulkanCommandList::DrawSceneFast) instead of uniform slices and
    // descriptor sets written for each draw. The picture is the same; only
    // how a draw reaches the GPU changes. Requested by the renderer while
    // performance mode is on; FRUITY_FAST_SCENE=0 keeps the ordinary path
    // (A/B runs).
    namespace Detail
    {
        inline std::atomic<bool> requested{false};
    }

    inline void Request(bool on) noexcept
    {
        Detail::requested.store(on, std::memory_order_relaxed);
    }

    [[nodiscard]] inline bool Allowed() noexcept
    {
        static const bool allowed = []
        {
            const char* value = std::getenv("FRUITY_FAST_SCENE");
            return !value || std::string_view(value) != "0";
        }();
        return allowed;
    }

    [[nodiscard]] inline bool Enabled() noexcept
    {
        return Detail::requested.load(std::memory_order_relaxed) && Allowed();
    }
}
