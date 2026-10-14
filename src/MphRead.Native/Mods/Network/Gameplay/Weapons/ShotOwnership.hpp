#pragma once

namespace MphRead::Entities
{
    class PlayerEntity;
}

namespace MphRead::Mods::Network
{
    // Whose decisions a player's shots carry on this machine. The owner --
    // the player's own machine, or for a bot the authority that plays it --
    // decides the weapon row and the target; every other copy takes them.
    class ShotOwnership final
    {
    public:
        ShotOwnership() = delete;

        // True offline, and on the machine that owns this player's shots.
        [[nodiscard]] static bool IsOwner(const Entities::PlayerEntity& shooter) noexcept;
        // This machine keeps the score: the authority, or a host.
        [[nodiscard]] static bool Judges() noexcept;
    };
}
