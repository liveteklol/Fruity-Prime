#pragma once
#if defined(FRUITY_HAS_VULKAN)
#include "../LowLatency.hpp"
#include <vulkan/vulkan.h>
#include <chrono>
#include <optional>

namespace MphRead::NativeRuntime::Rhi::Vulkan
{
    // Owned by the swapchain; its timeline belongs to driver sleep, never to
    // queue-completion serials. All calls run on the presentation thread.
    class VulkanNvidiaReflex final
    {
    public:
        struct Dispatch final
        {
            VkDevice device = VK_NULL_HANDLE;
            bool enabled = false;
            std::uint32_t revision = 0;
            std::string reason;
            PFN_vkCreateSemaphore create = nullptr;
            PFN_vkDestroySemaphore destroy = nullptr;
            PFN_vkWaitSemaphores wait = nullptr;
            PFN_vkSetLatencySleepModeNV setMode = nullptr;
            PFN_vkLatencySleepNV sleep = nullptr;
            PFN_vkSetLatencyMarkerNV marker = nullptr;
            PFN_vkGetLatencyTimingsNV timings = nullptr;
        };
        explicit VulkanNvidiaReflex(Dispatch dispatch, std::uint64_t& sequence);
        ~VulkanNvidiaReflex(); // Owner must drain before destroying the timeline.
        void SetSwapchain(VkSwapchainKHR swapchain);
        void SetMode(LowLatencyMode mode, std::uint32_t intervalUs);
        [[nodiscard]] bool BeginFrame(); // bounded poll; does not repeat sleep
        void Mark(LowLatencyMarker marker);
        void FinishFrame();
        void Shutdown() noexcept;
        [[nodiscard]] bool Available() const noexcept { return _available; }
        [[nodiscard]] bool Active() const noexcept { return _available && _swapchain && _mode != LowLatencyMode::Off; }
        [[nodiscard]] LowLatencyCapabilities Caps() const noexcept
        { return _available && _swapchain ? LowLatencyCapabilities{true, true, LowLatencyProvider::Nvidia}
            : LowLatencyCapabilities{true, false, LowLatencyProvider::Generic, _reason}; }
        [[nodiscard]] std::uint64_t FrameId() const noexcept { return Active() && _ready ? _frameId : 0; }
        [[nodiscard]] std::optional<std::uint64_t> SubmissionId() const noexcept
        { return _dispatch.enabled && _dispatch.revision >= 3 ? std::optional(FrameId()) : std::nullopt; }
        [[nodiscard]] LowLatencyDiagnostics Stats() const noexcept { return _stats; }
    private:
        void Fail(const char* operation, VkResult result);
        void ApplyMode();
        void PollTimings();
        Dispatch _dispatch;
        std::uint64_t& _sequence;
        VkSemaphore _semaphore = VK_NULL_HANDLE;
        VkSwapchainKHR _swapchain = VK_NULL_HANDLE;
        LowLatencyMode _mode = LowLatencyMode::Off, _applied = LowLatencyMode::Off;
        std::uint32_t _interval = 0, _appliedInterval = 0;
        bool _available = false, _modeApplied = false, _sleepPending = false, _ready = false;
        std::uint64_t _frameId = 0;
        std::uint32_t _markers = 0;
        std::chrono::steady_clock::time_point _sleepStart{};
        std::string _reason;
        LowLatencyDiagnostics _stats;
    };
}
#endif
