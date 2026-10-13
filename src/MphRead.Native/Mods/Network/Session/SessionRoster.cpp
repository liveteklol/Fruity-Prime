#include "Mods/Network/Session/SessionRoster.hpp"

namespace MphRead::Mods::Network
{
    bool SessionRoster::IsValidSlot(std::int32_t slot) noexcept
    {
        return slot >= 0 && slot < static_cast<std::int32_t>(SlotCapacity);
    }

    bool SessionRoster::IsOccupied(std::int32_t slot) const noexcept
    {
        return IsValidSlot(slot) && _occupied[static_cast<std::size_t>(slot)];
    }

    void SessionRoster::SetOccupied(std::int32_t slot, bool occupied) noexcept
    {
        if (IsValidSlot(slot))
        {
            _occupied[static_cast<std::size_t>(slot)] = occupied;
        }
    }

    void SessionRoster::ClearOccupied() noexcept
    {
        _occupied.fill(false);
    }

    Hunter SessionRoster::HunterAt(std::size_t slot) const
    {
        return _hunters.at(slot);
    }

    void SessionRoster::SetHunter(std::size_t slot, Hunter hunter)
    {
        _hunters.at(slot) = hunter;
    }

    std::int32_t SessionRoster::PingAt(std::int32_t slot) const noexcept
    {
        return IsValidSlot(slot) ? _pings[static_cast<std::size_t>(slot)] : 0;
    }

    void SessionRoster::SetPing(std::int32_t slot, std::int32_t ping) noexcept
    {
        if (IsValidSlot(slot))
        {
            _pings[static_cast<std::size_t>(slot)] = ping;
        }
    }

    void SessionRoster::ClearPings() noexcept
    {
        _pings.fill(0);
    }

    std::int8_t SessionRoster::TeamAt(std::int32_t slot) const noexcept
    {
        return IsValidSlot(slot) ? _teams[static_cast<std::size_t>(slot)] : -1;
    }

    void SessionRoster::SetTeam(std::size_t slot, std::int8_t team)
    {
        _teams.at(slot) = team;
    }

    bool SessionRoster::LobbyReady(std::int32_t slot) const noexcept
    {
        return IsValidSlot(slot) && _lobbyReady[static_cast<std::size_t>(slot)];
    }

    void SessionRoster::SetLobbyReady(std::size_t slot, bool ready)
    {
        _lobbyReady.at(slot) = ready;
    }

    void SessionRoster::ResetLobby() noexcept
    {
        _teams.fill(-1);
        _lobbyReady.fill(false);
    }

    SessionRoster& SessionSlots() noexcept
    {
        static SessionRoster roster{};
        return roster;
    }
}
