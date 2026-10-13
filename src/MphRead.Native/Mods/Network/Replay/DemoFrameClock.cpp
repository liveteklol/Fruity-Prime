#include "Mods/Network/Replay/DemoFrameClock.hpp"

#include <limits>

namespace MphRead::Mods::Network
{
    void DemoFrameClock::Start(std::int64_t timestamp, std::int64_t frequency) noexcept
    {
        _startTimestamp = timestamp;
        _frequency = frequency;
        _started = timestamp >= 0 && frequency > 0;
        _authorityStartFrame = 0;
        _authorityDemoFrame = 0;
        _hasAuthorityFrame = false;
    }

    void DemoFrameClock::Reset() noexcept
    {
        _startTimestamp = 0;
        _frequency = 0;
        _started = false;
        _authorityStartFrame = 0;
        _authorityDemoFrame = 0;
        _hasAuthorityFrame = false;
    }

    std::uint32_t DemoFrameClock::FrameAt(std::int64_t timestamp) const noexcept
    {
        if (!_started || timestamp < _startTimestamp)
        {
            return 0;
        }

        const std::uint64_t elapsed = static_cast<std::uint64_t>(timestamp - _startTimestamp);
        constexpr std::uint64_t scale = 60;
        if (elapsed > std::numeric_limits<std::uint64_t>::max() / scale)
        {
            return std::numeric_limits<std::uint32_t>::max();
        }

        const std::uint64_t frame = elapsed * scale / static_cast<std::uint64_t>(_frequency);
        return frame > std::numeric_limits<std::uint32_t>::max()
            ? std::numeric_limits<std::uint32_t>::max()
            : static_cast<std::uint32_t>(frame);
    }

    void DemoFrameClock::AnchorAuthorityFrame(std::uint32_t authorityFrame,
        std::uint32_t demoFrame) noexcept
    {
        _authorityStartFrame = authorityFrame;
        _authorityDemoFrame = demoFrame;
        _hasAuthorityFrame = true;
    }

    bool DemoFrameClock::HasAuthorityFrame() const noexcept
    {
        return _hasAuthorityFrame;
    }

    std::uint32_t DemoFrameClock::FrameAtAuthority(
        std::uint32_t authorityFrame, std::uint32_t fallbackFrame) const noexcept
    {
        if (!_hasAuthorityFrame)
        {
            return fallbackFrame;
        }
        const std::uint32_t delta = authorityFrame - _authorityStartFrame;
        // Sequence arithmetic crosses uint32 wrap, but a half-range-or-larger
        // delta is an old/out-of-order snapshot, not billions of elapsed frames.
        if (delta > std::numeric_limits<std::uint32_t>::max() / 2U)
        {
            return fallbackFrame;
        }
        return _authorityDemoFrame + delta;
    }
}
