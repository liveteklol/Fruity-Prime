#pragma once

#include <cstdint>

namespace MphRead::Entities
{
    class PlayerEntity;
}

namespace MphRead::Mods::Network
{
    // The weapon row a shot leaves the gun with. A copy firing somebody
    // else's recorded shot takes the row and the Prime Hunter role that shot
    // was recorded with; the shooter's own machine resolves them now.
    struct FiringVariant final
    {
        std::uint8_t Index = 0xFF;
        bool PrimeHunter = false;

        [[nodiscard]] static FiringVariant For(const Entities::PlayerEntity& shooter);
        static void ReceiveRole(std::uint32_t frame, std::int32_t slot) noexcept;
        static void ResetRole() noexcept;
        [[nodiscard]] static std::uint32_t RoleFrame() noexcept;
    };
}
