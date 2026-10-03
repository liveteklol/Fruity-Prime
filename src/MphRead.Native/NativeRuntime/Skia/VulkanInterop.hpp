#pragma once

#include <cstdint>
#include <memory>

// Skia's Ganesh on the game window's Vulkan device. This adapter is the only
// code that turns the RHI's opaque interop handles back into Vk types; the
// Skia canvas API above it (Skia.hpp) and the RHI below it name none.
//
// One device and one graphics queue: Skia is handed the RHI's VkInstance,
// VkPhysicalDevice, VkDevice and queue, never makes its own. The UI target is
// an RHI texture. Before Skia draws into it the RHI holds nothing unsubmitted
// and leaves the image COLOR_ATTACHMENT_OPTIMAL (ResourceState::ColorAttachment,
// the layout Skia renders in; GENERAL makes Skia derive host access its own
// barriers cannot carry). Skia's flush ends it there again, its submit is
// waited on, and the RHI is told, so its next use inserts the barrier.
class GrDirectContext;
class SkSurface;
template <typename T> class sk_sp;

namespace MphRead::NativeRuntime::Rhi
{
    class Texture;
}

namespace MphRead::NativeRuntime::Skia::VulkanInterop
{
    // Skia was built with Vulkan and the window presents through it.
    [[nodiscard]] bool Active() noexcept;
    // Whether this build's Skia can do it at all.
    [[nodiscard]] bool Available() noexcept;

    struct Target final
    {
        Target();
        ~Target();
        Target(Target&&) noexcept;
        Target& operator=(Target&&) noexcept;
        std::unique_ptr<Rhi::Texture> Texture;
        std::int32_t Width = 0;
        std::int32_t Height = 0;
    };

    [[nodiscard]] sk_sp<GrDirectContext> MakeContext();
    [[nodiscard]] sk_sp<SkSurface> MakeSurface(GrDirectContext& context, Target& target,
        std::int32_t width, std::int32_t height);
    // Before Skia records into the target.
    void BeginFrame(Target& target);
    // Skia's work is submitted and finished; the RHI owns the image again.
    void EndFrame(GrDirectContext& context, SkSurface& surface, Target& target);
}
