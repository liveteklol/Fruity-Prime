#include "Mods/Combat/HomingSteeringRules.hpp"

#include <algorithm>
#include <cmath>

namespace MphRead::Mods::Combat
{
    namespace
    {
        std::int32_t _frequencyHz = HomingSteeringRules::DefaultFrequencyHz;
    }

    std::int32_t HomingSteeringRules::FrequencyHz() noexcept
    {
        return _frequencyHz;
    }

    bool HomingSteeringRules::ConfigureFrequencyHz(std::int32_t value) noexcept
    {
        if (value < 0 || value > SimulationHz)
        {
            return false;
        }
        _frequencyHz = value;
        return true;
    }

    bool HomingSteeringRules::Advance(std::int32_t& phase) noexcept
    {
        if (phase < 0 || phase >= SimulationHz)
        {
            phase = 0;
        }
        phase += FrequencyHz();
        if (phase < SimulationHz)
        {
            return false;
        }
        phase -= SimulationHz;
        return true;
    }

    float HomingSteeringRules::MaximumDeltaPerStep(float deltaAtReferenceHz) noexcept
    {
        const std::int32_t frequency = FrequencyHz();
        return frequency > 0
            ? deltaAtReferenceHz * static_cast<float>(DefaultFrequencyHz) / static_cast<float>(frequency)
            : 0.0F;
    }

    std::optional<OpenTK::Mathematics::Vector3> HomingSteeringRules::Step(
        OpenTK::Mathematics::Vector3 position,
        OpenTK::Mathematics::Vector3 target,
        OpenTK::Mathematics::Vector3 velocity,
        float speed,
        float maximumDelta) noexcept
    {
        using OpenTK::Mathematics::Vector3;
        const Vector3 toward = target - position;
        const float towardLengthSquared = Vector3::Dot(toward, toward);
        const float velocityLengthSquared = Vector3::Dot(velocity, velocity);
        const float steeringDelta = MaximumDeltaPerStep(maximumDelta);
        if (!(towardLengthSquared > 0.0F) || !(velocityLengthSquared > 0.0F)
            || !std::isfinite(towardLengthSquared) || !std::isfinite(velocityLengthSquared)
            || !std::isfinite(speed) || !std::isfinite(steeringDelta))
        {
            return velocity;
        }
        if (steeringDelta <= 0.0F)
        {
            return velocity;
        }
        const float inverseLength = 1.0F / std::sqrt(towardLengthSquared);
        const Vector3 desired = ::OpenTK::Mathematics::Scale(toward, inverseLength * speed);
        if (Vector3::Dot(desired, velocity) < 0.0F)
        {
            return std::nullopt;
        }
        Vector3 delta = desired - velocity;
        const float deltaLengthSquared = Vector3::Dot(delta, delta);
        const float maximumSquared = steeringDelta * steeringDelta;
        if (deltaLengthSquared > maximumSquared)
        {
            delta = ::OpenTK::Mathematics::Scale(delta, steeringDelta / std::sqrt(deltaLengthSquared));
        }
        return velocity + delta;
    }
}
