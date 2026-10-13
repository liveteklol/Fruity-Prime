#pragma once

#include <cstdint>

namespace MphRead::Mods::Network
{
    // When a hit claim still waiting for its verdict goes out again: on the
    // first chance, and then once `interval` frames have passed since it last
    // went -- measured from that send, not from the claim's age. An age taken
    // modulo the interval skipped the resend whenever the interval (a
    // measured round trip) moved, or a full packet held the claim past the
    // one frame it was due on, and waited for the next multiple instead.
    [[nodiscard]] constexpr bool ClaimResendDue(std::int32_t sends, std::int32_t maxSends,
        std::int32_t age, std::int32_t lastSentAge, std::int32_t interval) noexcept
    {
        if (sends == 0)
        {
            return true;
        }
        return sends < maxSends && age - lastSentAge >= (interval > 0 ? interval : 1);
    }

    // Frames between sends of a claim: a little over one round trip at the
    // sender's measured ping, never under 8 frames or over 40.
    [[nodiscard]] constexpr std::int32_t ClaimResendInterval(std::int32_t pingMs) noexcept
    {
        const std::int32_t frames = static_cast<std::int32_t>(static_cast<float>(pingMs) * 0.06F) + 6;
        return frames < 8 ? 8 : frames > 40 ? 40 : frames;
    }
}
