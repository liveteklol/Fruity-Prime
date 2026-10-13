#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace MphRead::Mods::Network
{
    // 128 random bits a client draws once and sends in every Hello. The
    // client id names a connection; the secret proves the sender is the
    // client that opened it. A server moves a slot to a new address -- a
    // router that changed the port, a client that rebound its socket -- only
    // for a Hello carrying the secret the slot was opened with, so knowing
    // (or guessing) a 32-bit id is not enough to take somebody's slot.
    using ConnectionSecret = std::array<std::uint8_t, 16>;

    [[nodiscard]] constexpr bool IsSet(const ConnectionSecret& secret) noexcept
    {
        std::uint8_t any = 0;
        for (const std::uint8_t b : secret)
        {
            any = static_cast<std::uint8_t>(any | b);
        }
        return any != 0;
    }

    // Whether `offered` proves the connection that `held` was opened with.
    // Compared in constant time; an unset secret proves nothing.
    [[nodiscard]] constexpr bool MayRebind(const ConnectionSecret& held, const ConnectionSecret& offered) noexcept
    {
        std::uint8_t difference = 0;
        for (std::size_t i = 0; i < held.size(); i++)
        {
            difference = static_cast<std::uint8_t>(difference | (held[i] ^ offered[i]));
        }
        return IsSet(held) && difference == 0;
    }
}
