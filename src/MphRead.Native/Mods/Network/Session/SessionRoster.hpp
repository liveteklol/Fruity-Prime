#pragma once

#include "Formats/Enums.hpp"
#include "Mods/Network/Protocol/Packets/SessionPackets.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace MphRead::Mods::Network
{
    // Owns the session-visible identity and lobby metadata for each player slot.
    class SessionRoster final
    {
    public:
        static constexpr std::size_t SlotCapacity = static_cast<std::size_t>(RosterPacket::MaxSlots);

        [[nodiscard]] bool IsOccupied(std::int32_t slot) const noexcept;
        [[nodiscard]] Hunter HunterAt(std::size_t slot) const;
        [[nodiscard]] std::int32_t PingAt(std::int32_t slot) const noexcept;
        [[nodiscard]] std::int8_t TeamAt(std::int32_t slot) const noexcept;
        [[nodiscard]] bool LobbyReady(std::int32_t slot) const noexcept;

        void SetOccupied(std::int32_t slot, bool occupied) noexcept;
        void ClearOccupied() noexcept;
        void SetHunter(std::size_t slot, Hunter hunter);
        void SetPing(std::int32_t slot, std::int32_t ping) noexcept;
        void ClearPings() noexcept;
        void SetTeam(std::size_t slot, std::int8_t team);
        void SetLobbyReady(std::size_t slot, bool ready);
        void ResetLobby() noexcept;

    private:
        [[nodiscard]] static bool IsValidSlot(std::int32_t slot) noexcept;

        std::array<Hunter, SlotCapacity> _hunters{};
        std::array<std::int32_t, SlotCapacity> _pings{};
        std::array<bool, SlotCapacity> _occupied{};
        std::array<std::int8_t, SlotCapacity> _teams{};
        std::array<bool, SlotCapacity> _lobbyReady{};
    };

    [[nodiscard]] SessionRoster& SessionSlots() noexcept;
}
