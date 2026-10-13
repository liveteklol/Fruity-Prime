#pragma once

#include "Mods/Network/Domain/NetworkCapacity.hpp"
#include "Mods/Network/Protocol/NetProtocol.hpp"

#include <array>
#include <cstdint>

namespace MphRead::Entities
{
    class PlayerEntity;
}

namespace MphRead::Mods::Network
{
    // Press edges belong to a player slot, regardless of whether that slot is
    // driven by a human client or by the server's AI.
    class NetInputHistory final
    {
    public:
        static constexpr std::int32_t Slots = static_cast<std::int32_t>(NetworkSlotCapacity);

        static void Record(const Entities::PlayerEntity& player) noexcept;
        static void Record(std::int32_t slot, IntentButtons pressed) noexcept;
        static void Attach(IntentPacket& intent, std::int32_t slot);
        static void ForgetSlot(std::int32_t slot) noexcept;
        static void Reset() noexcept;

    private:
        using History = std::array<std::uint32_t, IntentPacket::PressHistory>;
        [[nodiscard]] static IntentButtons Pressed(const Entities::PlayerEntity& player) noexcept;

        static std::array<History, Slots> _history;
    };
}
