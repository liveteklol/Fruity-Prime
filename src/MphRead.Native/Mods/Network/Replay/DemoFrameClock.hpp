#pragma once

#include <cstdint>

namespace MphRead::Mods::Network
{
    // Maps local monotonic event timestamps and authority snapshots onto the
    // demo's fixed 60 Hz timeline. Snapshot frames are the authoritative tick
    // source; timestamps anchor the first snapshot to local recording events.
    class DemoFrameClock final
    {
    public:
        void Start(std::int64_t timestamp, std::int64_t frequency) noexcept;
        void Reset() noexcept;
        [[nodiscard]] std::uint32_t FrameAt(std::int64_t timestamp) const noexcept;
        void AnchorAuthorityFrame(std::uint32_t authorityFrame,
            std::uint32_t demoFrame) noexcept;
        [[nodiscard]] bool HasAuthorityFrame() const noexcept;
        [[nodiscard]] std::uint32_t FrameAtAuthority(std::uint32_t authorityFrame,
            std::uint32_t fallbackFrame) const noexcept;

    private:
        std::int64_t _startTimestamp = 0;
        std::int64_t _frequency = 0;
        bool _started = false;
        std::uint32_t _authorityStartFrame = 0;
        std::uint32_t _authorityDemoFrame = 0;
        bool _hasAuthorityFrame = false;
    };
}
