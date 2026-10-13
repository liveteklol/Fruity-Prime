#pragma once

#include <cstddef>

namespace MphRead::Mods::Network
{
    // Shared fixed slot count used by the wire protocol and network-owned state.
    inline constexpr std::size_t NetworkSlotCapacity = 8;
}
