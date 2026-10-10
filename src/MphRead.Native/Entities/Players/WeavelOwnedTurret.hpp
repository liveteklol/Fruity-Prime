#pragma once

namespace MphRead::Entities
{
    // What the owner's machine does with the authority's word on its own
    // turret.
    //
    // The owner predicts its turret's life -- it places it the moment it
    // morphs, without waiting for the authority -- but only the authority
    // takes other players' hits on it. So a lower health the authority
    // reports is damage taken there (a turret never heals, so a higher one is
    // only an older report), and a turret the authority has destroyed is
    // destroyed here too: otherwise it went on firing from the owner's
    // machine, its hits claimed as the owner's, invisible to everybody who
    // had seen it go. Its position and footing stay the owner's.
    //
    // A turret the authority has not placed yet is not a turret it has
    // destroyed: Destroy needs the authority to have reported this alt life's
    // turret standing first, and to report the player still heading for alt
    // form (not unmorphing) without it now. Decisions only:
    // PlayerEntity::ModApplyOwnWeavelTurret carries them out.
    class WeavelOwnedTurret final
    {
    public:
        enum class Step
        {
            Keep,
            // Take the authority's turret health, if it is lower.
            AdoptHealth,
            Destroy,
        };

        [[nodiscard]] Step Decide(bool localAlt, bool localTurretAlive,
            bool authorityHeadingAlt, bool authorityTurretActive) noexcept
        {
            if (!localAlt)
            {
                _confirmed = false;
                return Step::Keep;
            }
            if (authorityTurretActive)
            {
                _confirmed = true;
                return localTurretAlive ? Step::AdoptHealth : Step::Keep;
            }
            if (_confirmed && authorityHeadingAlt && localTurretAlive)
            {
                _confirmed = false;
                return Step::Destroy;
            }
            return Step::Keep;
        }

    private:
        bool _confirmed = false;
    };
}
