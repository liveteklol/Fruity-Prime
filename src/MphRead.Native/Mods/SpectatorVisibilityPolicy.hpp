#pragma once

#include <cstdint>

namespace MphRead::Mods
{
    class SpectatorVisibilityPolicy final
    {
    public:
        SpectatorVisibilityPolicy() = delete;

        [[nodiscard]] static constexpr bool ShouldHideLocalPlayerModel(bool spectatorView,
            std::int32_t playerSlot, std::int32_t localSlot) noexcept
        {
            return spectatorView && localSlot >= 0 && playerSlot == localSlot;
        }
    };
}
