#include "VulkanNvidiaReflex.hpp"
#if defined(FRUITY_HAS_VULKAN)
#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>

namespace MphRead::NativeRuntime::Rhi::Vulkan
{
    namespace
    {
        bool Inject(const char* value)
        { const auto* flag = std::getenv("FRUITY_REFLEX_TEST_FAILURE"); return flag && std::string_view(flag) == value; }
    }
    VulkanNvidiaReflex::VulkanNvidiaReflex(Dispatch dispatch, std::uint64_t& sequence)
        : _dispatch(std::move(dispatch)), _sequence(sequence), _reason(_dispatch.reason)
    {
        if (!_dispatch.enabled) return;
        if (!_dispatch.create || !_dispatch.destroy || !_dispatch.wait || !_dispatch.setMode || !_dispatch.sleep || !_dispatch.marker || !_dispatch.timings)
        { _reason = "NVIDIA Reflex: required device entry point is missing."; return; }
        VkSemaphoreTypeCreateInfo type{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
        type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
        VkSemaphoreCreateInfo create{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO}; create.pNext = &type;
        const auto result = Inject("semaphore") ? VK_ERROR_OUT_OF_HOST_MEMORY
            : _dispatch.create(_dispatch.device, &create, nullptr, &_semaphore);
        if (result != VK_SUCCESS || !_semaphore) { Fail("vkCreateSemaphore(Reflex sleep)", result); return; }
        _available = true; _reason.clear();
    }
    VulkanNvidiaReflex::~VulkanNvidiaReflex() { Shutdown(); }
    void VulkanNvidiaReflex::Fail(const char* operation, VkResult result)
    {
        _reason = std::string("NVIDIA Reflex: ") + operation + " failed (VkResult=" + std::to_string(result) + ").";
        // Attempt to leave driver pacing disabled even after a native failure.
        if (_swapchain && _dispatch.setMode)
        { VkLatencySleepModeInfoNV off{VK_STRUCTURE_TYPE_LATENCY_SLEEP_MODE_INFO_NV}; (void)_dispatch.setMode(_dispatch.device, _swapchain, &off); }
        _available = _ready = false; _modeApplied = false;
        std::cout << "[reflex] fallback: " << _reason << '\n';
    }
    void VulkanNvidiaReflex::SetSwapchain(VkSwapchainKHR swapchain)
    {
        if (_swapchain == swapchain) return;
        if (_swapchain && _dispatch.setMode && _modeApplied)
        { VkLatencySleepModeInfoNV off{VK_STRUCTURE_TYPE_LATENCY_SLEEP_MODE_INFO_NV}; (void)_dispatch.setMode(_dispatch.device, _swapchain, &off); }
        // Resize may arrive during input collection, after this frame's sleep.
        // Keep its identity and readiness through the detach/attach pair;
        // the next successful present ends that application-rendered frame.
        _swapchain = swapchain; _modeApplied = false;
        if (swapchain) { ++_stats.swapchainGeneration; ApplyMode(); }
    }
    void VulkanNvidiaReflex::SetMode(LowLatencyMode mode, std::uint32_t intervalUs)
    { _mode = mode; _interval = intervalUs; ApplyMode(); }
    void VulkanNvidiaReflex::ApplyMode()
    {
        if (!Available() || !_swapchain || (_modeApplied && _mode == _applied && _interval == _appliedInterval)) return;
        VkLatencySleepModeInfoNV info{VK_STRUCTURE_TYPE_LATENCY_SLEEP_MODE_INFO_NV};
        info.lowLatencyMode = _mode != LowLatencyMode::Off;
        info.lowLatencyBoost = _mode == LowLatencyMode::OnBoost;
        info.minimumIntervalUs = _mode == LowLatencyMode::Off ? 0 : _interval;
        ++_stats.modeCalls;
        const auto result = Inject("set-mode") && _mode != LowLatencyMode::Off ? VK_ERROR_UNKNOWN
            : _dispatch.setMode(_dispatch.device, _swapchain, &info);
        if (result != VK_SUCCESS) { Fail("vkSetLatencySleepModeNV", result); return; }
        _applied = _mode; _appliedInterval = _interval; _modeApplied = true;
        if (_mode == LowLatencyMode::Off) _ready = false;
        std::cout << "[reflex] mode=" << static_cast<int>(_mode) << " lowLatencyMode=" << info.lowLatencyMode
            << " lowLatencyBoost=" << info.lowLatencyBoost << " minimumIntervalUs=" << info.minimumIntervalUs << '\n';
    }
    bool VulkanNvidiaReflex::BeginFrame()
    {
        if (!Active()) return true;
        if (_ready) return true;
        if (!_sleepPending)
        {
            _frameId = ++_sequence; _stats.frameId = _frameId; _markers = 0;
            VkLatencySleepInfoNV info{VK_STRUCTURE_TYPE_LATENCY_SLEEP_INFO_NV}; info.signalSemaphore = _semaphore; info.value = _frameId;
            ++_stats.sleepCalls;
            const auto result = Inject("sleep") ? VK_ERROR_UNKNOWN : _dispatch.sleep(_dispatch.device, _swapchain, &info);
            if (result != VK_SUCCESS) { Fail("vkLatencySleepNV", result); return true; }
            _sleepPending = true; _sleepStart = std::chrono::steady_clock::now();
        }
        VkSemaphoreWaitInfo wait{VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO}; wait.semaphoreCount = 1;
        wait.pSemaphores = &_semaphore; wait.pValues = &_frameId;
        ++_stats.waitCalls;
        const auto result = _dispatch.wait(_dispatch.device, &wait, 2'000'000);
        if (result == VK_TIMEOUT && std::chrono::steady_clock::now() - _sleepStart < std::chrono::milliseconds(250)) return false;
        if (result != VK_SUCCESS) { Fail("vkWaitSemaphores(Reflex sleep)", result); return true; }
        _sleepPending = false; _ready = true; return true;
    }
    void VulkanNvidiaReflex::Mark(LowLatencyMarker marker)
    {
        if (!FrameId()) return;
        const auto bit = 1U << static_cast<unsigned>(marker);
        if (_markers & bit) return;
        constexpr std::array values{VK_LATENCY_MARKER_INPUT_SAMPLE_NV, VK_LATENCY_MARKER_SIMULATION_START_NV,
            VK_LATENCY_MARKER_SIMULATION_END_NV, VK_LATENCY_MARKER_RENDERSUBMIT_START_NV, VK_LATENCY_MARKER_RENDERSUBMIT_END_NV,
            VK_LATENCY_MARKER_PRESENT_START_NV, VK_LATENCY_MARKER_PRESENT_END_NV};
        VkSetLatencyMarkerInfoNV info{VK_STRUCTURE_TYPE_SET_LATENCY_MARKER_INFO_NV}; info.presentID = _frameId;
        info.marker = values.at(static_cast<std::size_t>(marker));
        _dispatch.marker(_dispatch.device, _swapchain, &info); _markers |= bit; ++_stats.markerCalls;
    }
    void VulkanNvidiaReflex::PollTimings()
    {
        if (!Available() || !_swapchain || !std::getenv("FRUITY_RENDER_METRICS") || _stats.sleepCalls % 120) return;
        VkGetLatencyMarkerInfoNV info{VK_STRUCTURE_TYPE_GET_LATENCY_MARKER_INFO_NV};
        _dispatch.timings(_dispatch.device, _swapchain, &info);
        std::array<VkLatencyTimingsFrameReportNV, 64> reports{};
        info.timingCount = std::min<std::uint32_t>(info.timingCount, reports.size());
        if (!info.timingCount) return;
        for (auto& report : reports) report.sType = VK_STRUCTURE_TYPE_LATENCY_TIMINGS_FRAME_REPORT_NV;
        info.pTimings = reports.data(); _dispatch.timings(_dispatch.device, _swapchain, &info);
        _stats.timingReports += info.timingCount;
        if (info.timingCount)
        {
            const auto& r = reports[info.timingCount - 1];
            std::cout << "[reflex timings] reports=" << info.timingCount << " frame=" << r.presentID << " input_us=" << r.inputSampleTimeUs
                << " sim_start_us=" << r.simStartTimeUs << " sim_end_us=" << r.simEndTimeUs
                << " submit_start_us=" << r.renderSubmitStartTimeUs << " submit_end_us=" << r.renderSubmitEndTimeUs
                << " present_start_us=" << r.presentStartTimeUs << " present_end_us=" << r.presentEndTimeUs << '\n';
        }
    }
    void VulkanNvidiaReflex::FinishFrame() { PollTimings(); _ready = false; _markers = 0; }
    void VulkanNvidiaReflex::Shutdown() noexcept
    {
        // Owner drains the device first. Keep the semaphore until that boundary
        // even when optional runtime pacing failed.
        SetSwapchain(VK_NULL_HANDLE);
        if (_semaphore) _dispatch.destroy(_dispatch.device, _semaphore, nullptr);
        _semaphore = VK_NULL_HANDLE; _available = false;
    }
}
#endif
