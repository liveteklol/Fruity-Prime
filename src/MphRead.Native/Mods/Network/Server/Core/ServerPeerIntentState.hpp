#pragma once

#include <cstdint>

namespace MphRead::Mods::Network
{
    // Tracks the newest client intent frame while retaining delayed shot
    // events carried by otherwise stale intents.
    class ServerPeerIntentState final
    {
    public:
        enum class Disposition : std::uint8_t
        {
            Newest,
            StaleWithoutShots,
            StaleWithShots
        };

        [[nodiscard]] Disposition Observe(std::uint32_t frame, bool hasShotHistory) noexcept;
        void Reset() noexcept { _lastFrame = 0; }
        [[nodiscard]] std::uint32_t LastFrame() const noexcept { return _lastFrame; }

    private:
        std::uint32_t _lastFrame = 0;
    };
}
