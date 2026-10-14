#include "Mods/Combat/ProjectileFlight.hpp"

#include "Entities/BeamProjectileEntity.hpp"
#include "Mods/Combat/ProjectileValueCurve.hpp"

namespace MphRead::Mods::Combat
{
    using OpenTK::Mathematics::Vector3;

    void ProjectileFlight::Move(const ProjectileFlightParameters& parameters, float age, ProjectileFlightState& state)
    {
        state.Position = state.Position + state.Velocity;
        state.TickPhase = (state.TickPhase + 1) % FramesPerTick;
        if (state.TickPhase != 0)
        {
            return;
        }
        state.Velocity = state.Velocity + parameters.Acceleration;
        if (parameters.SpeedDecayTime > 0.0F && age <= parameters.SpeedDecayTime)
        {
            const float magnitude = ::OpenTK::Mathematics::Length(state.Velocity);
            if (magnitude > 0.0F)
            {
                state.Speed = InterpolateProjectileValue(parameters.SpeedInterpolation,
                    parameters.InitialSpeed, parameters.FinalSpeed, age / parameters.SpeedDecayTime);
                state.Velocity = ::OpenTK::Mathematics::Scale(state.Velocity, state.Speed / magnitude);
            }
        }
    }

    ProjectileFlightParameters ProjectileFlight::ParametersOf(const Entities::BeamProjectileEntity& beam)
    {
        return ProjectileFlightParameters{beam.Acceleration(), beam.SpeedDecayTime(), beam.SpeedInterpolation(),
            beam.InitialSpeed(), beam.FinalSpeed()};
    }

    void ProjectileFlight::MoveFlight(Entities::BeamProjectileEntity& beam)
    {
        ProjectileFlightState state{static_cast<Vector3>(beam.Position), beam.Velocity(), beam.Speed(),
            beam.ModFlightPhase};
        Move(ParametersOf(beam), beam.Age(), state);
        beam.Position = state.Position;
        beam.SetVelocity(state.Velocity);
        beam.SetSpeed(state.Speed);
        beam.ModFlightPhase = state.TickPhase;
    }
}
