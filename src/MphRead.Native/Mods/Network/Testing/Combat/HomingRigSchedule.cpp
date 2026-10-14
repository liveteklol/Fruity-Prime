#include "Mods/Network/Testing/Combat/HomingRigSchedule.hpp"

#include <algorithm>

namespace MphRead::Mods::Network
{
    std::int32_t HomingRigSchedule::LengthOf(const Step& step) noexcept
    {
        return SettleFrames + std::max(step.Hold, TapFrames) + FlightFrames;
    }

    HomingRigSchedule::Command HomingRigSchedule::At(std::int32_t frame, std::int32_t slot) noexcept
    {
        std::int32_t cycle = 0;
        for (const Step& step : Steps)
        {
            cycle += LengthOf(step);
        }
        const std::int32_t firstStep = (std::max(slot, 0) / 2 * 3) % static_cast<std::int32_t>(Steps.size());
        std::int32_t offset = 0;
        for (std::int32_t i = 0; i < firstStep; ++i)
        {
            offset += LengthOf(Steps[static_cast<std::size_t>(i)]);
        }
        std::int32_t at = (std::max(frame, 0) + offset) % cycle;
        for (std::int32_t i = 0; i < static_cast<std::int32_t>(Steps.size()); ++i)
        {
            const Step& step = Steps[static_cast<std::size_t>(i)];
            const std::int32_t length = LengthOf(step);
            if (at < length)
            {
                const std::int32_t held = std::max(step.Hold, TapFrames);
                return Command{step.Weapon, at >= SettleFrames && at < SettleFrames + held, i};
            }
            at -= length;
        }
        return Command{Steps.front().Weapon, false, 0};
    }
}
