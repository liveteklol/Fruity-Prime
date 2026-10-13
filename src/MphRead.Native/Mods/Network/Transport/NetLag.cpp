#include "Mods/Network/Transport/NetLag.hpp"

#include "NativeRuntime/System/Globalization.hpp"
#include "NativeRuntime/System/Number.hpp"

#include <cmath>
#include <string>
#include <string_view>
#include <vector>

namespace MphRead::Mods::Network
{
    namespace Runtime = ::MphRead::NativeRuntime;

    std::int32_t NetLag::_roundTripMs = 0;
    std::int32_t NetLag::_jitterMs = 0;
    std::atomic<double> NetLag::_lossPercent{0};
    double NetLag::_reorderRate = 0;
    double NetLag::_duplicateRate = 0;
    std::int32_t NetLag::_seed = 1;

    bool NetLag::Active() noexcept
    {
        return _roundTripMs > 0 || _jitterMs > 0 || LossPercent() > 0 || _reorderRate > 0 || _duplicateRate > 0
            || _spikesPerMinute > 0;
    }

    bool NetLag::ConfigureSeed(const std::optional<std::string>& value)
    {
        std::int32_t seed = 0;
        if (!value.has_value() || !Runtime::Int32TryParseInvariant(*value, seed))
        {
            return false;
        }
        _seed = seed;
        return true;
    }

    bool NetLag::ConfigureJitter(const std::optional<std::string>& value)
    {
        std::int32_t jitter = 0;
        if (!value.has_value() || !Runtime::Int32TryParseInvariant(*value, jitter) || jitter < 0 || jitter > 5000)
        {
            return false;
        }
        _jitterMs = jitter;
        return true;
    }

    bool NetLag::ConfigureReorder(const std::optional<std::string>& value)
    {
        double rate = 0;
        if (!Rate(value, rate))
        {
            return false;
        }
        _reorderRate = rate;
        return true;
    }

    bool NetLag::ConfigureDuplicate(const std::optional<std::string>& value)
    {
        double rate = 0;
        if (!Rate(value, rate))
        {
            return false;
        }
        _duplicateRate = rate;
        return true;
    }

    bool NetLag::Rate(const std::optional<std::string>& value, double& rate)
    {
        rate = 0;
        const bool percent = value.has_value() && !value->empty() && value->back() == '%';
        if (!value.has_value())
        {
            return false;
        }
        // value.TrimEnd('%')
        std::string_view text = *value;
        while (!text.empty() && text.back() == '%')
        {
            text.remove_suffix(1);
        }
        if (!Runtime::DoubleTryParseInvariant(text, rate) || !std::isfinite(rate) || rate < 0 || rate > 100)
        {
            return false;
        }
        if (percent || rate > 1)
        {
            rate /= 100;
        }
        return true;
    }

    bool NetLag::Configure(const std::optional<std::string>& value)
    {
        if (Runtime::StringIsNullOrWhiteSpace(value))
        {
            return false;
        }
        // value.Split(':', ',')
        std::vector<std::string> parts;
        std::string current;
        for (const char ch : *value)
        {
            if (ch == ':' || ch == ',')
            {
                parts.push_back(current);
                current.clear();
                continue;
            }
            current.push_back(ch);
        }
        parts.push_back(current);
        if (parts.size() > 2)
        {
            return false;
        }
        std::int32_t rtt = 0;
        if (!Runtime::Int32TryParseInvariant(parts[0], rtt) || rtt < 0 || rtt > 10000)
        {
            return false;
        }
        std::int32_t jitter = 0;
        if (parts.size() > 1 && (!Runtime::Int32TryParseInvariant(parts[1], jitter) || jitter < 0 || jitter > 5000))
        {
            return false;
        }
        _roundTripMs = rtt;
        _jitterMs = jitter;
        return true;
    }

    bool NetLag::ConfigureSpikes(const std::optional<std::string>& value)
    {
        if (!value.has_value())
        {
            return false;
        }
        const std::size_t colon = value->find(':');
        const std::size_t dash = value->find('-', colon == std::string::npos ? 0 : colon);
        if (colon == std::string::npos || dash == std::string::npos)
        {
            return false;
        }
        double rate = 0;
        std::int32_t low = 0;
        std::int32_t high = 0;
        if (!Runtime::DoubleTryParseInvariant(value->substr(0, colon), rate) || !std::isfinite(rate) || rate < 0 || rate > 600
            || !Runtime::Int32TryParseInvariant(value->substr(colon + 1, dash - colon - 1), low)
            || !Runtime::Int32TryParseInvariant(value->substr(dash + 1), high) || low < 0 || high < low || high > 10000)
        {
            return false;
        }
        _spikesPerMinute = rate;
        _spikeMinMs = low;
        _spikeMaxMs = high;
        return true;
    }

    bool NetLag::ConfigureLoss(const std::optional<std::string>& value)
    {
        double rate = 0;
        if (!Rate(value, rate))
        {
            return false;
        }
        _lossPercent.store(rate * 100, std::memory_order_relaxed);
        return true;
    }

    std::optional<std::string> NetLag::Describe()
    {
        if (!Active())
        {
            return std::nullopt;
        }
        std::string text = _roundTripMs > 0
            ? "+" + Runtime::ToString(_roundTripMs) + " ms round trip"
            : std::string("no added latency");
        if (_jitterMs > 0)
        {
            text += " (jitter up to " + Runtime::ToString(_jitterMs) + " ms each way)";
        }
        const double lossPercent = LossPercent();
        if (lossPercent > 0)
        {
            text += ", " + Runtime::ToString(lossPercent, "0.##") + "% packet loss each way";
        }
        if (_spikesPerMinute > 0)
        {
            text += ", " + Runtime::ToString(_spikesPerMinute, "0.##") + " spikes a minute each way of +"
                + Runtime::ToString(static_cast<std::int32_t>(_spikeMinMs)) + ".."
                + Runtime::ToString(static_cast<std::int32_t>(_spikeMaxMs)) + " ms";
        }
        return text + ", reorder " + Runtime::ToString(_reorderRate, "P1") + ", duplicate "
            + Runtime::ToString(_duplicateRate, "P1") + ", seed " + Runtime::ToString(_seed);
    }
}
