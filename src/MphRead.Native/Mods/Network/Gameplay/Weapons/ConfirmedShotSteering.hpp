#pragma once

namespace MphRead::Entities
{
    class BeamProjectileEntity;
}

namespace MphRead::Mods::Network
{
    // A remote shot that met this machine's player: held where it met the
    // body until the authority answers, then either flown onto the impact the
    // authority confirmed or let through. It moves the drawn flight when the
    // shot has one, the physical one otherwise.
    class ConfirmedShotSteering final
    {
    public:
        ConfirmedShotSteering() = delete;

        static void Apply(Entities::BeamProjectileEntity& beam);
    };
}
