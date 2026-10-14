#pragma once

#include "NativeRuntime/OpenTK/Mathematics.hpp"

#include <cstdint>

namespace MphRead::Entities
{
    class BeamProjectileEntity;
}

namespace MphRead::Mods::Combat
{
    // The weapon values a ballistic projectile flies by, in 60 Hz units.
    struct ProjectileFlightParameters final
    {
        // Velocity gained per 30 Hz tick (gravity).
        OpenTK::Mathematics::Vector3 Acceleration{};
        float SpeedDecayTime = 0.0F;
        std::int32_t SpeedInterpolation = 0;
        float InitialSpeed = 0.0F;
        float FinalSpeed = 0.0F;
    };

    struct ProjectileFlightState final
    {
        OpenTK::Mathematics::Vector3 Position{};
        OpenTK::Mathematics::Vector3 Velocity{};
        float Speed = 0.0F;
        std::int32_t TickPhase = 0;
    };

    // A projectile's flight at 60 Hz that lands exactly where the DS's 30 Hz
    // flight lands: it moves every frame by half a tick's travel, and its
    // velocity changes -- gravity, the speed curve -- only as each 30 Hz tick
    // ends, which is also when it steers. The frame between is the straight
    // midpoint of the tick, for drawing and for collision.
    class ProjectileFlight final
    {
    public:
        ProjectileFlight() = delete;

        static constexpr std::int32_t FramesPerTick = 2;

        // One 60 Hz frame; age is the projectile's age once the frame is done.
        static void Move(const ProjectileFlightParameters& parameters, float age, ProjectileFlightState& state);
        [[nodiscard]] static ProjectileFlightParameters ParametersOf(const Entities::BeamProjectileEntity& beam);
        // The physical flight of a beam.
        static void MoveFlight(Entities::BeamProjectileEntity& beam);
    };
}
