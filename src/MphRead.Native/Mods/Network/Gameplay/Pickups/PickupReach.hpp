#pragma once

#include "NativeRuntime/OpenTK/Mathematics.hpp"

#include <cstdint>

namespace MphRead::Mods::Network
{
    // Whether a player whose position is `player` could have touched an item
    // at `item`. The game's own test (PlayerEntity::PickUpItems) reaches about
    // a unit across and a little over one up; this allows twice that, for
    // the alt form's sphere and for where two machines put the same player.
    [[nodiscard]] inline bool WithinPickupReach(OpenTK::Mathematics::Vector3 player,
        OpenTK::Mathematics::Vector3 item) noexcept
    {
        constexpr float Lateral = 2.0F;
        constexpr float Below = -2.0F;
        constexpr float Above = 2.5F;
        const float dx = item.X - player.X;
        const float dz = item.Z - player.Z;
        const float dy = item.Y - player.Y;
        return dx * dx + dz * dz <= Lateral * Lateral && dy >= Below && dy <= Above;
    }

    // Authority frames, both ends included.
    struct FrameWindow
    {
        std::uint32_t From = 0;
        std::uint32_t To = 0;
    };

    // When the authority's copy of a player stood where the player stood at
    // `contact` -- the world frame the player's screen was showing. The
    // position that came with that world reaches the authority one measured
    // round trip later, plus the snapshot playout hold (two to eight frames)
    // and jitter: ArrivalSlack. Never later than `now`.
    inline constexpr std::uint32_t ArrivalSlack = 20;

    [[nodiscard]] constexpr FrameWindow ContactArrivalWindow(std::uint32_t contact, std::int32_t pingMs,
        std::uint32_t now) noexcept
    {
        const std::uint32_t roundTrip = pingMs > 0 ? static_cast<std::uint32_t>((pingMs * 60 + 999) / 1000) : 0U;
        const std::uint32_t latest = contact + roundTrip + ArrivalSlack;
        return FrameWindow{ contact, latest < now ? latest : now };
    }
}
