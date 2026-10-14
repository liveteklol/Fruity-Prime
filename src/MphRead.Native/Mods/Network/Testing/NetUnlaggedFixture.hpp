#pragma once

#include "Mods/Network/Gameplay/Combat/NetUnlagged.hpp"

#include <cstddef>

namespace MphRead::Mods::Network
{
    class NetUnlaggedFixture final
    {
    public:
        struct PrimeHistoryEntry final
        {
            std::uint32_t Stamp = 0;
            std::int32_t Holder = -1;
        };

        [[nodiscard]] static PrimeHistoryEntry CapturePrimeHistory(std::uint32_t frame) noexcept
        {
            const std::size_t index = static_cast<std::size_t>(
                frame % static_cast<std::uint32_t>(NetUnlagged::HistoryFrames));
            return {NetUnlagged::_primeHunterStamp[index], NetUnlagged::_primeHunter[index]};
        }

        static void SetPrimeHistory(std::uint32_t frame, std::int32_t holder) noexcept
        {
            const std::size_t index = static_cast<std::size_t>(
                frame % static_cast<std::uint32_t>(NetUnlagged::HistoryFrames));
            NetUnlagged::_primeHunterStamp[index] = frame;
            NetUnlagged::_primeHunter[index] = holder;
        }

        static void RestorePrimeHistory(std::uint32_t frame, PrimeHistoryEntry entry) noexcept
        {
            const std::size_t index = static_cast<std::size_t>(
                frame % static_cast<std::uint32_t>(NetUnlagged::HistoryFrames));
            NetUnlagged::_primeHunterStamp[index] = entry.Stamp;
            NetUnlagged::_primeHunter[index] = entry.Holder;
        }
    };
}
