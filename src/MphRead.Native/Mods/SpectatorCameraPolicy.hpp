#pragma once

namespace MphRead::Mods
{
    class SpectatorCameraPolicy final
    {
    public:
        SpectatorCameraPolicy() = delete;

        [[nodiscard]] static constexpr bool CanFollowPlayer(bool spectating,
            bool hasMainPlayer, bool hasTarget, bool targetIsCurrent) noexcept
        {
            return hasMainPlayer && (!spectating || (hasTarget && targetIsCurrent));
        }

        [[nodiscard]] static constexpr bool CanBorrowLocalPose(bool spectating,
            bool hasTarget, bool mainIsLocal, bool hasLocalPlayer) noexcept
        {
            return spectating && !hasTarget && mainIsLocal && hasLocalPlayer;
        }
    };
}
