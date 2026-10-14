#pragma once

#include "Formats/Enums.hpp"

#include <array>
#include <cstdint>

namespace MphRead::Mods::Network
{
    // -hitrig homing: what a shooter fires, frame by frame. Every weapon
    // whose homing depends on the charge, released at the charge levels on
    // either side of each boundary, then the Shock Coil held -- in turn, one
    // shot at a time with the flight given time to land. Slots start at
    // different steps so several shooters cover the list in parallel.
    class HomingRigSchedule final
    {
    public:
        HomingRigSchedule() = delete;

        struct Step final
        {
            BeamType Weapon{};
            // Frames the trigger is held: 0 is a tap.
            std::int32_t Hold = 0;
        };

        struct Command final
        {
            BeamType Weapon{};
            bool Trigger = false;
            std::int32_t Index = 0;
        };

        static constexpr std::int32_t SettleFrames = 30;
        static constexpr std::int32_t TapFrames = 3;
        static constexpr std::int32_t FlightFrames = 50;
        static constexpr std::array<Step, 13> Steps{{
            {BeamType::PowerBeam, 0}, {BeamType::PowerBeam, 35}, {BeamType::PowerBeam, 37},
            {BeamType::PowerBeam, 48}, {BeamType::PowerBeam, 60},
            {BeamType::Missile, 0}, {BeamType::Missile, 89}, {BeamType::Missile, 92},
            {BeamType::VoltDriver, 0}, {BeamType::VoltDriver, 119}, {BeamType::VoltDriver, 122},
            {BeamType::ShockCoil, 90}, {BeamType::ShockCoil, 150},
        }};

        [[nodiscard]] static Command At(std::int32_t frame, std::int32_t slot) noexcept;
        [[nodiscard]] static std::int32_t LengthOf(const Step& step) noexcept;
    };
}
