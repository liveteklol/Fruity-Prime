#pragma once
#include <array>
#include "FrameStatistics.hpp"
#include "../../NativeRuntime/Rhi/GpuDiagnostics.hpp"
#include "../../NativeRuntime/FrameTelemetry.hpp"
#include <chrono>
#include <deque>
#include <fstream>
#include <optional>
#include <string>
namespace MphRead::NativeRuntime::Rhi { class GraphicsDevice; class CommandList; class Swapchain; }
namespace MphRead { class RenderWindow; }
namespace MphRead::Mods::Diagnostics
{
    class CpuSampling;
    // Single main window, render-thread owned. HUD display is independent.
    class FramePerformance final
    {
    public:
        static void Configure(std::string path, bool gpu, std::optional<int> cap = {});
        static int EffectiveCap(int configured) noexcept;
        static std::unique_ptr<FramePerformance> Create();
        ~FramePerformance();
        void BeginFrame(const RenderWindow& window, const NativeRuntime::Rhi::Swapchain& swapchain);
        void Presented(const RenderWindow& window, const NativeRuntime::Rhi::Swapchain& swapchain, double presentSeconds);
        void Reset();
        static std::unique_ptr<NativeRuntime::Rhi::TimestampQuerySet> BeginGpu(
            NativeRuntime::Rhi::GraphicsDevice& device, NativeRuntime::Rhi::CommandList& commands);
        static void EndGpu(std::unique_ptr<NativeRuntime::Rhi::TimestampQuerySet> sample,
            NativeRuntime::Rhi::CommandList& commands);
        // FRUITY_GPU_PASSES=1 with -gpuprofile: a timestamp after each scene
        // pass, reported as the mean GPU time of each one.
        static constexpr std::uint32_t PassMarks = 6;
        static void MarkGpu(NativeRuntime::Rhi::TimestampQuerySet* sample,
            NativeRuntime::Rhi::CommandList& commands, std::uint32_t pass);
    private:
        FramePerformance(std::string path, bool gpu);
        void PollGpu();
        void Report();
        struct Conditions final
        {
            int backend, room, width, height, cap, requestedPresent, actualPresent, scale;
            bool fpsCounter, cel, fog, paused, focused, mainActive;
            bool operator==(const Conditions&) const = default;
        };
        static Conditions ReadConditions(const RenderWindow& window, const NativeRuntime::Rhi::Swapchain& swapchain);
        static std::string Describe(const Conditions& conditions);
        std::ofstream _csv;
        std::unique_ptr<CpuSampling> _cpuSampling;
        std::unique_ptr<NativeRuntime::FrameTelemetry::Session> _telemetry;
        bool _gpu = false;
        std::string _context;
        std::optional<Conditions> _conditions;
        std::size_t _segment = 0, _draws = 0, _gpuSamples = 0, _gpuDrops = 0;
        double _gpuMs = 0;
        std::array<double, PassMarks> _passMs{};
        std::chrono::steady_clock::time_point _start{}, _warmup{};
        std::optional<std::chrono::steady_clock::time_point> _previous;
        FrameStatistics _statistics;
        std::deque<std::unique_ptr<NativeRuntime::Rhi::TimestampQuerySet>> _pending;
    };
}
