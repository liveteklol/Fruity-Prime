#pragma once
#include <cstdint>
#include <string>
#include <string_view>

namespace MphRead::NativeRuntime::Rhi
{
    enum class LowLatencyMode : std::uint8_t { Off, On, OnBoost };
    enum class LowLatencyProvider : std::uint8_t { None, Generic, Nvidia, Amd };
    enum class PacingAuthority : std::uint8_t { Generic, Native };
    struct LowLatencyCapabilities final
    {
        bool supportsLowLatency = false;
        bool supportsBoost = false;
        LowLatencyProvider provider = LowLatencyProvider::None;
        std::string_view unavailableReason;
    };
    struct LowLatencyState final
    {
        LowLatencyMode requested = LowLatencyMode::Off, effective = LowLatencyMode::Off;
        LowLatencyProvider provider = LowLatencyProvider::None;
        bool boostSupported = false;
        PacingAuthority authority = PacingAuthority::Generic;
        std::string fallbackReason;
        bool operator==(const LowLatencyState&) const = default;
    };
    struct PresentationWaitStatistics final { std::uint64_t count = 0, nanoseconds = 0; };
    inline LowLatencyState ResolveLowLatency(LowLatencyMode requested, LowLatencyCapabilities caps)
    {
        LowLatencyState state{requested, LowLatencyMode::Off, LowLatencyProvider::None, caps.supportsBoost};
        if (requested == LowLatencyMode::Off) return state;
        if (!caps.supportsLowLatency)
        { state.fallbackReason = "Low latency is unavailable on this renderer."; return state; }
        state.effective = requested;
        state.provider = caps.provider;
        if (requested == LowLatencyMode::OnBoost && !caps.supportsBoost)
        {
            state.effective = LowLatencyMode::On;
            state.fallbackReason = caps.unavailableReason.empty() ? "Boost is unavailable; On is active."
                : std::string(caps.unavailableReason) + " Using generic Low Latency On.";
        }
        if (caps.provider == LowLatencyProvider::Nvidia || caps.provider == LowLatencyProvider::Amd)
            state.authority = PacingAuthority::Native;
        return state;
    }

    enum class LowLatencyMarker : std::uint8_t { InputSample, SimulationStart, SimulationEnd, RenderSubmitStart, RenderSubmitEnd, PresentStart, PresentEnd };
    struct LowLatencyDiagnostics final
    { std::uint64_t sleepCalls = 0, waitCalls = 0, modeCalls = 0, markerCalls = 0, timingReports = 0, frameId = 0, swapchainGeneration = 0; };
}
