#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace MphRead::NativeRuntime::Rhi
{
    struct TimestampProperties final
    {
        std::uint32_t validBits = 0;
        double nanosecondsPerTick = 0;
    };
    inline double TimestampNanoseconds(std::uint64_t first, std::uint64_t last, TimestampProperties properties)
    {
        if (!properties.validBits || properties.validBits > 64
            || !std::isfinite(properties.nanosecondsPerTick) || properties.nanosecondsPerTick <= 0)
            throw std::invalid_argument("Invalid GPU timestamp properties.");
        const auto mask = properties.validBits == 64 ? UINT64_MAX : (UINT64_C(1) << properties.validBits) - 1;
        return static_cast<double>((last - first) & mask) * properties.nanosecondsPerTick;
    }
    struct DebugLabel final
    {
        std::string name;
        std::array<float, 4> color{0.2F, 0.5F, 0.9F, 1.0F};
    };
    inline void ValidateDebugLabel(const DebugLabel& label)
    {
        if (label.name.empty() || label.name.size() > 255 || label.name.find('\0') != std::string::npos)
            throw std::invalid_argument("GPU labels require 1..255 UTF-8 bytes without NUL.");
        for (const auto component : label.color)
            if (!std::isfinite(component) || component < 0 || component > 1)
                throw std::invalid_argument("GPU label colors require finite values in 0..1.");
    }
    enum class TimestampStatus : std::uint8_t { Pending, Ready, Cancelled };

    // One-shot set: initialize once, write each index once, poll without
    // submission or waits. The caller owns the copied CPU values. Durations
    // must be shorter than one counter wrap; clocks are not cross-device clocks.
    class TimestampQuerySet
    {
    public:
        virtual ~TimestampQuerySet() = default;
        [[nodiscard]] virtual std::uint32_t Count() const noexcept = 0;
        [[nodiscard]] virtual TimestampProperties Properties() const noexcept = 0;
        // Pending/Cancelled leave destination unchanged. Ready copies all values.
        [[nodiscard]] virtual TimestampStatus ReadResults(std::span<std::uint64_t> destination) = 0;
    };
    class TimestampWriteState final
    {
    public:
        explicit TimestampWriteState(std::uint32_t count) : _count(count)
        {
            if (!count || count > 64) throw std::invalid_argument("GPU timestamp count must be 1..64.");
        }
        void Initialize()
        {
            if (_initialized) throw std::logic_error("GPU timestamp sets cannot be reused.");
            _initialized = true;
        }
        void Write(std::uint32_t index)
        {
            if (!_initialized) throw std::logic_error("GPU timestamp set was not initialized.");
            if (index >= _count) throw std::out_of_range("GPU timestamp index is outside the set.");
            const auto bit = UINT64_C(1) << index;
            if (_written & bit) throw std::logic_error("GPU timestamp index was already written.");
            _written |= bit;
        }
        [[nodiscard]] bool AllWritten() const noexcept
        { return _written == (_count == 64 ? UINT64_MAX : (UINT64_C(1) << _count) - 1); }
        [[nodiscard]] std::uint32_t Count() const noexcept { return _count; }
    private:
        std::uint32_t _count;
        std::uint64_t _written = 0;
        bool _initialized = false;
    };
    // Counts native sets including retired sets, not just frontend wrappers.
    // A reservation is retained until actual native destruction or shutdown.
    class TimestampBudget final : public std::enable_shared_from_this<TimestampBudget>
    {
        struct Lease final
        {
            std::shared_ptr<TimestampBudget> budget;
            explicit Lease(std::shared_ptr<TimestampBudget> value) : budget(std::move(value)) { ++budget->_sets; }
            ~Lease() { --budget->_sets; }
        };
    public:
        static constexpr std::uint32_t Limit = 8;
        [[nodiscard]] std::shared_ptr<void> TryReserve()
        { return _sets == Limit ? nullptr : std::make_shared<Lease>(shared_from_this()); }
        [[nodiscard]] std::uint32_t Sets() const noexcept { return _sets; }
    private:
        std::uint32_t _sets = 0;
    };
}
