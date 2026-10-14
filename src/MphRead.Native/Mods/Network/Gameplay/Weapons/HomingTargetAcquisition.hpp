#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace MphRead
{
    class EquipInfo;
    class Scene;
}

namespace MphRead::Entities
{
    class BeamProjectileEntity;
}

namespace MphRead::Mods::Network
{
    // The target a homing projectile takes as it spawns, once, never looked
    // for again. Whoever fires the shot chooses it (best cosine inside the
    // weapon's range and cone) and its shot event carries the choice; every
    // other copy takes that same entity, and the authority first checks the
    // choice against its own history of the fire frame.
    class HomingTargetAcquisition final
    {
    public:
        HomingTargetAcquisition() = delete;

        // How the last acquisition went, for the shot trace.
        enum class Outcome : std::uint8_t
        {
            NotAttempted,
            // This copy chose: a target, or none in range and cone.
            Chosen,
            NoneInCone,
            // The shooter's choice arrived and was taken, or named nobody.
            Received,
            ReceivedNone,
            // It arrived, but the entity it names is not one this machine
            // can lock now (gone, dead, untargetable here).
            ReceivedIneligible,
            // The authority's history of the fire frame refused it.
            Refused
        };

        // Sets the beam's target. True only for a continuous weapon's lock,
        // as the projectile's spawn result has always reported it.
        static bool Acquire(Entities::BeamProjectileEntity& beam, const EquipInfo& equip, Scene& scene);
        // The outcome of the acquisition just made, once; NotAttempted after.
        [[nodiscard]] static Outcome ConsumeOutcome() noexcept;

    private:
        [[nodiscard]] static bool Finish(Outcome outcome, bool result) noexcept;

        inline static Outcome _outcome = Outcome::NotAttempted;
    };

    [[nodiscard]] std::string ToString(HomingTargetAcquisition::Outcome value);
}
