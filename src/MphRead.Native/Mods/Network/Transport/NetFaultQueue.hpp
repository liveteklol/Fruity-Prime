#pragma once

#include "NativeRuntime/System/Exceptions.hpp"
#include "NativeRuntime/System/Managed.hpp"
#include "NativeRuntime/System/Random.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <queue>
#include <utility>
#include <vector>

namespace MphRead::Mods::Network
{
    // A bounded, seeded datagram scheduler. Time is supplied by the caller so
    // tests need no sleeps.
    //
    // A C# generic, so the whole class is here; NetFaultQueue.cpp is its pair
    // and holds nothing else.
    template <typename T>
    class NetFaultQueue final
    {
    public:
        NetFaultQueue(std::int32_t seed, double delayMs, double jitterMs, double loss,
            double reorder, double duplicate, const std::atomic<double>* liveLossPercent = nullptr,
            std::int32_t capacity = 2048)
            : _random(seed)
        {
            if (!std::isfinite(delayMs) || !std::isfinite(jitterMs) || delayMs < 0 || jitterMs < 0
                || !Probability(loss) || !Probability(reorder) || !Probability(duplicate) || capacity < 1)
            {
                throw System::ArgumentOutOfRangeException("delayMs");
            }
            _delay = delayMs;
            _jitter = jitterMs;
            _loss = loss;
            _liveLossPercent = liveLossPercent;
            _reorder = reorder;
            _duplicate = duplicate;
            _capacity = capacity;
        }

        [[nodiscard]] std::int32_t Count() const noexcept
        {
            return static_cast<std::int32_t>(_queue.size());
        }
        [[nodiscard]] std::int32_t Capacity() const noexcept { return _capacity; }
        [[nodiscard]] std::int64_t Dropped() const noexcept { return _dropped; }
        [[nodiscard]] std::int64_t DroppedByLoss() const noexcept { return _lossDropped; }
        [[nodiscard]] std::int64_t DroppedByCapacity() const noexcept { return _capacityDropped; }
        [[nodiscard]] std::int64_t Duplicated() const noexcept { return _duplicated; }
        [[nodiscard]] std::int64_t Reordered() const noexcept { return _reordered; }

        // Latency spikes (-netspike): episodes arriving at random, on average
        // perMinute of them a minute, each adding minMs..maxMs to every
        // datagram for 0.2-1.5 s. Datagrams stay in order, as behind a real
        // queue that fills: when a spike ends, what it held arrives at once.
        void ConfigureSpikes(std::int32_t seed, double perMinute, double minMs, double maxMs)
        {
            _spikeRandom = ::MphRead::NativeRuntime::Random(seed);
            _spikeRate = std::max(0.0, perMinute) / 60000.0;
            _spikeMin = std::max(0.0, minMs);
            _spikeMax = std::max(_spikeMin, maxMs);
        }
        [[nodiscard]] std::int64_t Spikes() const noexcept { return _spikes; }

        void Enqueue(double nowMs, T value, double extraDelayMs = 0)
        {
            if (_spikeRate > 0)
            {
                if (nowMs >= _spikeEnd)
                {
                    const double elapsed = _lastSpikeCheck > 0 ? std::max(0.0, nowMs - _lastSpikeCheck) : 0.0;
                    if (_spikeRandom.NextDouble() < 1.0 - std::exp(-_spikeRate * elapsed))
                    {
                        _spikeExtra = _spikeMin + _spikeRandom.NextDouble() * (_spikeMax - _spikeMin);
                        _spikeEnd = nowMs + 200.0 + _spikeRandom.NextDouble() * 1300.0;
                        ::MphRead::NativeRuntime::IncrementInPlace(_spikes);
                    }
                }
                _lastSpikeCheck = nowMs;
                if (nowMs < _spikeEnd)
                {
                    extraDelayMs += _spikeExtra;
                }
            }
            const double loss = _liveLossPercent == nullptr
                ? _loss
                : _liveLossPercent->load(std::memory_order_relaxed) / 100.0;
            if (_random.NextDouble() < loss)
            {
                ::MphRead::NativeRuntime::IncrementInPlace(_dropped);
                ::MphRead::NativeRuntime::IncrementInPlace(_lossDropped);
                return;
            }
            // Jitter alone retains the old FIFO contract. Explicit reordering
            // delays one datagram without holding the following datagrams back.
            const double jitter = _random.NextDouble() * _jitter;
            double due = DotNetMathMax(_lastDue, nowMs + _delay + jitter + extraDelayMs);
            _lastDue = due;
            if (_random.NextDouble() < _reorder)
            {
                const double spread = 1 + _random.NextDouble();
                due += std::max(20.0, _jitter + _delay) * spread;
                ::MphRead::NativeRuntime::IncrementInPlace(_reordered);
            }
            Add(value, due);
            if (_random.NextDouble() < _duplicate)
            {
                const double extra = 1 + _random.NextDouble() * std::max(1.0, _jitter);
                Add(value, due + extra);
                ::MphRead::NativeRuntime::IncrementInPlace(_duplicated);
            }
        }

        [[nodiscard]] bool TryDequeue(double nowMs, T& value)
        {
            if (!_queue.empty() && _queue.top().Due <= nowMs)
            {
                value = _queue.top().Value;
                _queue.pop();
                return true;
            }
            value = T{};
            return false;
        }

    private:
        struct Entry final
        {
            double Due = 0;
            std::int64_t Order = 0;
            T Value{};
        };

        // PriorityQueue<T, (double Due, long Order)>: the smallest (Due,
        // Order) first. Order is unique, so the ordering is total and any
        // correct heap dequeues in the same sequence as .NET's.
        struct Later final
        {
            [[nodiscard]] bool operator()(const Entry& left, const Entry& right) const noexcept
            {
                // ValueTuple<double, long> uses Double.CompareTo, where NaN
                // sorts before every number and equals another NaN.
                if (std::isnan(left.Due))
                {
                    return std::isnan(right.Due) && left.Order > right.Order;
                }
                if (std::isnan(right.Due))
                {
                    return true;
                }
                return left.Due != right.Due ? left.Due > right.Due : left.Order > right.Order;
            }
        };

        [[nodiscard]] static bool Probability(double value) noexcept
        {
            return std::isfinite(value) && value >= 0 && value <= 1;
        }

        void Add(const T& value, double due)
        {
            if (static_cast<std::int32_t>(_queue.size()) >= _capacity)
            {
                ::MphRead::NativeRuntime::IncrementInPlace(_dropped);
                ::MphRead::NativeRuntime::IncrementInPlace(_capacityDropped);
                return;
            }
            const std::int64_t order = _order;
            ::MphRead::NativeRuntime::IncrementInPlace(_order);
            _queue.push(Entry{due, order, value});
        }

        // Math.Max(double) propagates either NaN and returns +0 when one input
        // is +0. std::max has different NaN and signed-zero rules.
        [[nodiscard]] static double DotNetMathMax(double left, double right) noexcept
        {
            if (left != right)
            {
                if (!std::isnan(left))
                {
                    return right < left ? left : right;
                }
                return left;
            }
            return std::signbit(right) ? left : right;
        }

        std::priority_queue<Entry, std::vector<Entry>, Later> _queue;
        ::MphRead::NativeRuntime::Random _random;
        double _delay = 0;
        double _jitter = 0;
        double _loss = 0;
        const std::atomic<double>* _liveLossPercent = nullptr;
        double _reorder = 0;
        double _duplicate = 0;
        std::int32_t _capacity = 0;
        std::int64_t _order = 0;
        double _lastDue = 0;
        std::int64_t _dropped = 0;
        std::int64_t _lossDropped = 0;
        std::int64_t _capacityDropped = 0;
        std::int64_t _duplicated = 0;
        std::int64_t _reordered = 0;
        ::MphRead::NativeRuntime::Random _spikeRandom{1};
        double _spikeRate = 0;
        double _spikeMin = 0;
        double _spikeMax = 0;
        double _spikeExtra = 0;
        double _spikeEnd = 0;
        double _lastSpikeCheck = 0;
        std::int64_t _spikes = 0;
    };
}
