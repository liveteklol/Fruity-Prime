#pragma once

#include "Mods/Network/Domain/NetworkCapacity.hpp"

#include <array>
#include <cstdint>

namespace MphRead::Mods::Network
{
    // Tracks the short interval where a locally predicted freeze may not yet
    // appear in the authority's next snapshot.
    class FreezePredictionLedger final
    {
    public:
        static constexpr std::uint32_t ArrivalSlackFrames = 20;

        void NotePredicted(std::int32_t slot, std::uint32_t frame) noexcept;
        [[nodiscard]] bool HasPrediction(std::int32_t slot) const noexcept;
        [[nodiscard]] bool Awaiting(
            std::int32_t slot, std::uint32_t currentFrame, std::uint32_t roundTripFrames) const noexcept;
        void ForgetSlot(std::int32_t slot) noexcept;
        void Reset() noexcept;

    private:
        [[nodiscard]] static bool IsValidSlot(std::int32_t slot) noexcept;

        std::array<std::uint32_t, NetworkSlotCapacity> _predictedAt{};
    };
}
