#pragma once

#include "Mods/Network/Domain/NetworkCapacity.hpp"
#include "Mods/Network/Protocol/Packets/RealtimePackets.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace MphRead::Mods::Network
{
    // Owns the latest accepted state and intent for remote player slots.
    // NetSession controls when these values are published or invalidated.
    class RemotePlayerChannel final
    {
    public:
        static constexpr std::size_t SlotCapacity = NetworkSlotCapacity;

        [[nodiscard]] bool HasState(std::int32_t slot) const noexcept;
        [[nodiscard]] const PlayerState& StateAt(std::size_t slot) const noexcept;
        void StoreState(const PlayerState& state) noexcept;
        void InvalidateStates() noexcept;

        [[nodiscard]] bool HasIntent(std::int32_t slot) const noexcept;
        [[nodiscard]] const IntentPacket& IntentAt(std::size_t slot) const noexcept;
        void StoreIntent(std::int32_t slot, const IntentPacket& intent,
            std::uint32_t arrivalFrame) noexcept;
        void InvalidateIntents() noexcept;
        void InvalidateIntent(std::int32_t slot) noexcept;
        void ClearIntentArrival() noexcept;
        [[nodiscard]] std::uint32_t IntentAge(std::int32_t slot,
            std::uint32_t currentFrame) const noexcept;

        void Forget(std::int32_t slot) noexcept;
        void Reset() noexcept;

    private:
        [[nodiscard]] static bool IsValidSlot(std::int32_t slot) noexcept;

        std::array<PlayerState, SlotCapacity> _states{};
        std::array<bool, SlotCapacity> _stateValid{};
        std::array<IntentPacket, SlotCapacity> _intents{};
        std::array<bool, SlotCapacity> _intentValid{};
        std::array<std::uint32_t, SlotCapacity> _intentArrived{};
    };

    [[nodiscard]] RemotePlayerChannel& RemotePlayers() noexcept;
}
