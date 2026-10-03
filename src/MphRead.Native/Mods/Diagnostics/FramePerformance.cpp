#include "FramePerformance.hpp"
#include "../../Renderer.hpp"
#include "../../Scene.hpp"
#include "../../NativeRuntime/Rhi/SceneBackend.hpp"
#include "../Render/FrameTiming.hpp"
#include "../RenderOptions.hpp"
#include "../DebugLog.hpp"
#include "../../GameState.hpp"
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <locale>
#include <sstream>

namespace MphRead::Mods::Diagnostics
{
    namespace
    {
        std::string configuredPath;
        bool configuredGpu = false;
        std::optional<int> configuredCap;
        FramePerformance* active = nullptr;
        namespace Rhi = NativeRuntime::Rhi;
        double Seconds(std::chrono::steady_clock::duration value) { return std::chrono::duration<double>(value).count(); }
    }
    void FramePerformance::Configure(std::string path, bool gpu, std::optional<int> cap)
    { configuredPath = std::move(path); configuredGpu = gpu; configuredCap = cap; }
    int FramePerformance::EffectiveCap(int configured) noexcept
    { return !configuredPath.empty() && configuredCap ? *configuredCap : configured; }
    FramePerformance::Conditions FramePerformance::ReadConditions(const RenderWindow& window, const Rhi::Swapchain& swapchain)
    {
        const auto size = window.FramebufferSize();
        return {static_cast<int>(Rhi::SelectedSceneBackend()), window.HasScene() ? window.Scene().RoomId() : -1,
            size.X, size.Y, EffectiveCap(Render::FrameTiming::FrameRateCap()),
            static_cast<int>(swapchain.RequestedPresentMode()), static_cast<int>(swapchain.Desc().presentMode), RenderOptions::ResolutionScale(),
            RenderOptions::ShowFps(), RenderOptions::CelShading(), RenderOptions::Fog(),
            GameState::MenuPause() || GameState::DialogPause(), window.IsFocused()};
    }
    std::string FramePerformance::Describe(const Conditions& conditions)
    {
        std::ostringstream out; out.imbue(std::locale::classic());
        out << Rhi::SceneBackendName(static_cast<Rhi::GraphicsBackend>(conditions.backend)) << ','
            << conditions.room << ',' << conditions.width << ',' << conditions.height << ','
            << (conditions.cap == -1 ? "unlimited" : Render::FrameTiming::CapString(conditions.cap)) << ','
            << conditions.requestedPresent << ',' << conditions.actualPresent << ','
            << conditions.scale << ',' << conditions.fpsCounter << ',' << conditions.cel << ',' << conditions.fog << ','
            << conditions.paused << ',' << conditions.focused;
        return out.str();
    }
    std::unique_ptr<FramePerformance> FramePerformance::Create()
    { return configuredPath.empty() ? nullptr : std::unique_ptr<FramePerformance>(new FramePerformance(configuredPath, configuredGpu)); }
    FramePerformance::FramePerformance(std::string path, bool gpu) : _gpu(gpu)
    {
        const auto file = std::filesystem::absolute(std::filesystem::u8path(path));
        std::filesystem::create_directories(file.parent_path());
        _csv.open(file, std::ios::out | std::ios::trunc); _csv.imbue(std::locale::classic());
        if (!_csv) throw std::runtime_error("Cannot open FPS measurement CSV: " + path);
        _csv << "segment,backend,room_id,width,height,fps_cap,present_requested,present_actual,resolution_scale,fps_counter,cel,fog,paused,focused,frames,seconds,fps,mean_frame_ms,p50_ms,p95_ms,p99_ms,percentile_samples,mean_loop_ms,mean_present_ms,gpu_scene_samples,gpu_scene_mean_ms,gpu_dropped_samples\n";
        active = this;
        std::cout << "[fps measure] CSV=" << file.string() << "; warmup=2s per context; window=1s; gpu=" << gpu << '\n';
    }
    FramePerformance::~FramePerformance() { Reset(); if (active == this) active = nullptr; }
    void FramePerformance::Reset()
    {
        _pending.clear(); _context.clear(); _conditions.reset(); _previous.reset(); _statistics.Reset();
        _draws = _gpuSamples = _gpuDrops = 0; _gpuMs = 0;
    }
    void FramePerformance::BeginFrame(const RenderWindow& window, const Rhi::Swapchain& swapchain)
    {
        _start = std::chrono::steady_clock::now();
        const auto conditions = ReadConditions(window, swapchain);
        if (!_conditions || conditions != *_conditions)
        {
            Reset(); _conditions = conditions; _context = Describe(conditions);
            ++_segment; _warmup = _start + std::chrono::seconds(2);
        }
        PollGpu();
    }
    void FramePerformance::Presented(const RenderWindow& window, const Rhi::Swapchain& swapchain, double presentSeconds)
    {
        if (!_conditions || ReadConditions(window, swapchain) != *_conditions) { _conditions.reset(); _previous.reset(); return; }
        const auto now = std::chrono::steady_clock::now(); ++_draws;
        if (now < _warmup) { _previous = now; return; }
        if (_previous) _statistics.Add(Seconds(now - *_previous), Seconds(now - _start), presentSeconds);
        _previous = now;
        if (_statistics.Seconds() >= 1) Report();
    }
    std::unique_ptr<Rhi::TimestampQuerySet> FramePerformance::BeginGpu(Rhi::GraphicsDevice& device, Rhi::CommandList& commands)
    {
        if (!active || !active->_gpu || active->_draws % 64 || std::chrono::steady_clock::now() < active->_warmup
            || !device.GetCapabilities().supportsTimestampQueries) return {};
        auto sample = device.CreateTimestampQuerySet(2, "Scene frame");
        if (!sample) { ++active->_gpuDrops; return {}; }
        commands.EndRendering(); commands.InitializeTimestamps(*sample); commands.WriteTimestamp(*sample, 0);
        commands.BeginDebugLabel({"Scene frame"});
        return sample;
    }
    void FramePerformance::EndGpu(std::unique_ptr<Rhi::TimestampQuerySet> sample, Rhi::CommandList& commands)
    {
        if (!sample) return;
        commands.EndDebugLabel(); commands.WriteTimestamp(*sample, 1);
        active->_pending.push_back(std::move(sample));
    }
    void FramePerformance::PollGpu()
    {
        for (auto it = _pending.begin(); it != _pending.end();)
        {
            std::array<std::uint64_t, 2> values{};
            const auto status = (*it)->ReadResults(values);
            if (status == Rhi::TimestampStatus::Pending) { ++it; continue; }
            if (status == Rhi::TimestampStatus::Ready)
            { _gpuMs += Rhi::TimestampNanoseconds(values[0], values[1], (*it)->Properties()) / 1e6; ++_gpuSamples; }
            it = _pending.erase(it);
        }
    }
    void FramePerformance::Report()
    {
        const auto result = _statistics.Result();
        _csv << std::setprecision(9) << _segment << ',' << _context << ',' << result.frames << ',' << result.seconds << ','
            << result.fps << ',' << result.meanFrameMs << ',' << result.p50Ms << ',' << result.p95Ms << ',' << result.p99Ms << ','
            << result.percentileSamples << ',' << result.meanLoopMs << ',' << result.meanPresentMs << ',' << _gpuSamples << ',';
        if (_gpuSamples) _csv << _gpuMs / _gpuSamples;
        _csv << ',' << _gpuDrops << '\n';
        std::ostringstream text; text.imbue(std::locale::classic());
        text << _context << "; fps=" << result.fps << "; p95_ms=" << result.p95Ms
            << "; loop_ms=" << result.meanLoopMs << "; present_ms=" << result.meanPresentMs << "; gpu_samples=" << _gpuSamples;
        if (_gpuSamples) text << "; gpu_scene_ms=" << _gpuMs / _gpuSamples;
        std::cout << "[fps measure] " << text.str() << '\n'; DebugLog::Line("fps measure", text.str());
        _statistics.Reset(); _gpuSamples = _gpuDrops = 0; _gpuMs = 0;
    }
}
