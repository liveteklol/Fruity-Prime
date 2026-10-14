#pragma once

#include "NativeRuntime/OpenTK/Mathematics.hpp"

#include <cstdint>

namespace MphRead::Entities
{
    class BeamProjectileEntity;
}

namespace MphRead::Mods::Combat
{
    // A ballistic projectile turning toward the target it took at launch, on
    // the shared steering cadence. One rule for the physical flight and the
    // drawn flight of a remote shot; only the physical one lets go of the
    // target when it falls behind.
    class ProjectileHoming final
    {
    public:
        ProjectileHoming() = delete;

        enum class Result : std::uint8_t
        {
            Idle,
            Steered,
            Released
        };

        // Turns velocity toward the beam's target when a step is due.
        [[nodiscard]] static Result Steer(const Entities::BeamProjectileEntity& beam,
            std::int32_t& phase, OpenTK::Mathematics::Vector3 position,
            OpenTK::Mathematics::Vector3& velocity);
        // The physical flight: steers the beam itself, dropping a target
        // that is behind it (never to look for another).
        static void SteerFlight(Entities::BeamProjectileEntity& beam);
    };
}
