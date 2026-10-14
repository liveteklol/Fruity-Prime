#pragma once

#include "NativeRuntime/OpenTK/Mathematics.hpp"

#include <cstdint>
#include <optional>

namespace MphRead::Mods::Combat
{
    class HomingSteeringRules final
    {
    public:
        static constexpr std::int32_t SimulationHz = 60;
        static constexpr std::int32_t DefaultFrequencyHz = 30;

        [[nodiscard]] static std::int32_t FrequencyHz() noexcept;
        [[nodiscard]] static bool ConfigureFrequencyHz(std::int32_t value) noexcept;
        [[nodiscard]] static bool Advance(std::int32_t& phase) noexcept;
        [[nodiscard]] static float MaximumDeltaPerStep(float deltaAtReferenceHz) noexcept;
        [[nodiscard]] static std::optional<OpenTK::Mathematics::Vector3> Step(
            OpenTK::Mathematics::Vector3 position,
            OpenTK::Mathematics::Vector3 target,
            OpenTK::Mathematics::Vector3 velocity,
            float speed,
            float maximumDelta) noexcept;
    };
}
