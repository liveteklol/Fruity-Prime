#include "Mods/Combat/ProjectileHoming.hpp"

#include "Entities/BeamProjectileEntity.hpp"
#include "Mods/Combat/HomingSteeringRules.hpp"

namespace MphRead::Mods::Combat
{
    using Entities::BeamFlags;
    using OpenTK::Mathematics::Vector3;

    ProjectileHoming::Result ProjectileHoming::Steer(const Entities::BeamProjectileEntity& beam,
        std::int32_t& phase, Vector3 position, Vector3& velocity)
    {
        const std::shared_ptr<Entities::EntityBase> target = beam.Target();
        if (!::MphRead::TestFlag(beam.Flags(), BeamFlags::Homing)
            || ::MphRead::TestFlag(beam.Flags(), BeamFlags::Continuous)
            || target == nullptr || !HomingSteeringRules::Advance(phase))
        {
            return Result::Idle;
        }
        Vector3 targetPosition{};
        target->GetPosition(targetPosition);
        const auto steered = HomingSteeringRules::Step(
            position, targetPosition, velocity, beam.Speed(), beam.Homing());
        if (!steered.has_value())
        {
            return Result::Released;
        }
        velocity = *steered;
        return Result::Steered;
    }

    void ProjectileHoming::SteerFlight(Entities::BeamProjectileEntity& beam)
    {
        Vector3 velocity = beam.Velocity();
        const Result result = Steer(beam, beam.ModHomingPhase, static_cast<Vector3>(beam.Position), velocity);
        if (result == Result::Steered)
        {
            beam.SetVelocity(velocity);
        }
        else if (result == Result::Released)
        {
            beam.SetTarget(nullptr);
        }
    }
}
