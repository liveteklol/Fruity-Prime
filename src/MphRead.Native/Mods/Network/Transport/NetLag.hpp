#pragma once

#include "Mods/Network/Transport/NetFaultQueue.hpp"
#include "NativeRuntime/System/Managed.hpp"

#include <atomic>
#include <cstdint>
#include <optional>
#include <string>

namespace MphRead::Mods::Network
{
    class NetLag final
    {
    public:
        NetLag() = delete;
        NetLag(const NetLag&) = delete;
        NetLag& operator=(const NetLag&) = delete;

        [[nodiscard]] static std::int32_t RoundTripMs() noexcept { return _roundTripMs; }
        [[nodiscard]] static std::int32_t JitterMs() noexcept { return _jitterMs; }
        [[nodiscard]] static double LossPercent() noexcept
        {
            return _lossPercent.load(std::memory_order_relaxed);
        }
        [[nodiscard]] static double ReorderRate() noexcept { return _reorderRate; }
        [[nodiscard]] static double DuplicateRate() noexcept { return _duplicateRate; }
        [[nodiscard]] static std::int32_t Seed() noexcept { return _seed; }
        [[nodiscard]] static bool Active() noexcept;

        template <typename T>
        [[nodiscard]] static NetFaultQueue<T> CreateQueue(bool outbound)
        {
            NetFaultQueue<T> queue(::MphRead::NativeRuntime::UncheckedAdd(
                _seed, outbound ? std::int32_t{1} : std::int32_t{0}),
                _roundTripMs / 2.0, _jitterMs, LossPercent() / 100, _reorderRate, _duplicateRate,
                &_lossPercent);
            if (_spikesPerMinute > 0)
            {
                queue.ConfigureSpikes(::MphRead::NativeRuntime::UncheckedAdd(_seed, outbound ? 101 : 100),
                    _spikesPerMinute, _spikeMinMs, _spikeMaxMs);
            }
            return queue;
        }

        [[nodiscard]] static bool ConfigureSeed(const std::optional<std::string>& value);
        [[nodiscard]] static bool ConfigureJitter(const std::optional<std::string>& value);
        [[nodiscard]] static bool ConfigureReorder(const std::optional<std::string>& value);
        [[nodiscard]] static bool ConfigureDuplicate(const std::optional<std::string>& value);
        [[nodiscard]] static bool Configure(const std::optional<std::string>& value);
        [[nodiscard]] static bool ConfigureLoss(const std::optional<std::string>& value);
        // -netspike N:MIN-MAX -- N latency spikes a minute each way, each
        // adding MIN..MAX ms for 0.2-1.5 s (NetFaultQueue::ConfigureSpikes).
        [[nodiscard]] static bool ConfigureSpikes(const std::optional<std::string>& value);
        [[nodiscard]] static std::optional<std::string> Describe();

    private:
        [[nodiscard]] static bool Rate(const std::optional<std::string>& value, double& rate);

        static std::int32_t _roundTripMs;
        static std::int32_t _jitterMs;
        static std::atomic<double> _lossPercent;
        static double _reorderRate;
        static double _duplicateRate;
        static std::int32_t _seed;
        inline static double _spikesPerMinute = 0;
        inline static double _spikeMinMs = 0;
        inline static double _spikeMaxMs = 0;
    };
}
