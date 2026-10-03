#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <vector>
namespace MphRead::Mods::Diagnostics
{
    struct FrameStatisticsResult final
    {
        std::size_t frames = 0, percentileSamples = 0;
        double seconds = 0, fps = 0, meanFrameMs = 0, p50Ms = 0, p95Ms = 0, p99Ms = 0;
        double meanLoopMs = 0, meanPresentMs = 0;
    };
    class FrameStatistics final
    {
    public:
        void Add(double interval, double loop, double present)
        {
            if (!std::isfinite(interval) || interval <= 0 || !std::isfinite(loop) || loop < 0
                || !std::isfinite(present) || present < 0)
                throw std::invalid_argument("Invalid measured frame duration.");
            ++_frames; _seconds += interval; _loop += loop; _present += present;
            if (_samples.size() < 8192) _samples.push_back(interval * 1000);
        }
        [[nodiscard]] double Seconds() const noexcept { return _seconds; }
        [[nodiscard]] FrameStatisticsResult Result() const
        {
            if (!_frames) return {};
            auto sorted = _samples; std::sort(sorted.begin(), sorted.end());
            const auto percentile = [&](double fraction) { return sorted[static_cast<std::size_t>(std::ceil(fraction * sorted.size())) - 1]; };
            return {_frames, sorted.size(), _seconds, _frames / _seconds, _seconds * 1000 / _frames,
                percentile(0.5), percentile(0.95), percentile(0.99), _loop * 1000 / _frames, _present * 1000 / _frames};
        }
        void Reset() { _frames = 0; _seconds = _loop = _present = 0; _samples.clear(); }
    private:
        std::size_t _frames = 0;
        double _seconds = 0, _loop = 0, _present = 0;
        std::vector<double> _samples;
    };
}
