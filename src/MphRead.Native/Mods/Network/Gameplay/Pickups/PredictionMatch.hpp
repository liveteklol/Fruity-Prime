#pragma once

#include <cstdint>
#include <optional>
#include <span>

namespace MphRead::Mods::Network
{
    // One hit this machine predicted on a victim and is still waiting to hear
    // about: the weapon's bucket and the low byte of the frame its shot left
    // the gun. Refused is a claim the authority already turned down -- no hit
    // of the authority's can be the answer to it.
    struct PredictedShot
    {
        std::int32_t Bucket = 0;
        std::uint8_t LaunchLow = 0;
        bool Refused = false;
    };

    // A hit the authority reports on that victim. Its launch frame is known
    // only for the newest hit a snapshot carries.
    struct AnsweredShot
    {
        std::int32_t Bucket = 0;
        std::optional<std::uint8_t> LaunchLow;
    };

    // Which waiting prediction an authority hit answers, as an offset from
    // the oldest: the same shot (weapon and launch frame) if it is waiting,
    // else the oldest from the same weapon, else the oldest of all. Taking
    // the oldest of all regardless retired a Missile's prediction on the
    // Power Beam's confirmation when the two were in flight together, and
    // left the Missile's debit standing against a hit already counted.
    [[nodiscard]] constexpr std::int32_t MatchPrediction(std::span<const PredictedShot> waiting,
        const AnsweredShot& answered) noexcept
    {
        std::int32_t sameWeapon = -1;
        for (std::size_t i = 0; i < waiting.size(); i++)
        {
            const PredictedShot& shot = waiting[i];
            if (shot.Refused || shot.Bucket != answered.Bucket)
            {
                continue;
            }
            if (answered.LaunchLow.has_value() && shot.LaunchLow == *answered.LaunchLow)
            {
                return static_cast<std::int32_t>(i);
            }
            if (sameWeapon < 0)
            {
                sameWeapon = static_cast<std::int32_t>(i);
            }
        }
        return sameWeapon >= 0 ? sameWeapon : 0;
    }
}
