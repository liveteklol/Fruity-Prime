#pragma once

namespace MphRead::Entities
{
    class PlayerEntity;
}

namespace MphRead::Mods::Network
{
    struct PickupTestSteering final
    {
        bool HasTarget = false;
        bool MoveForward = false;
        float AimDeltaX = 0.0F;
        float AimDeltaY = 0.0F;
    };

    // Test-only steering toward currently authoritative power-up spawns.
    class PickupTestDriver final
    {
    public:
        PickupTestDriver() = delete;

        [[nodiscard]] static PickupTestSteering Steer(Entities::PlayerEntity& player);
    };
}
