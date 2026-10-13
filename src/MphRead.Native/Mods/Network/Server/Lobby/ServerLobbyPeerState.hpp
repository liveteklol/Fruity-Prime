#pragma once

#include <cstdint>

namespace MphRead::Mods::Network
{
    // Owns lobby-only flags and team assignment for one connected peer.
    class ServerLobbyPeerState final
    {
    public:
        [[nodiscard]] bool LobbyReady() const noexcept { return _lobbyReady; }
        [[nodiscard]] bool PostMatchReady() const noexcept { return _postMatchReady; }
        [[nodiscard]] std::int8_t TeamIndex() const noexcept { return _teamIndex; }

        void SetLobbyReady(bool value) noexcept { _lobbyReady = value; }
        void InvalidateLobbyReady() noexcept { _lobbyReady = false; }
        void SetPostMatchReady(bool value) noexcept { _postMatchReady = value; }
        void ClearPostMatchReady() noexcept { _postMatchReady = false; }
        void SetTeamIndex(std::int8_t value) noexcept { _teamIndex = value; }

        void MoveToTeam(std::int8_t value) noexcept
        {
            _teamIndex = value;
            _lobbyReady = false;
        }

    private:
        bool _postMatchReady = false;
        bool _lobbyReady = false;
        std::int8_t _teamIndex = -1;
    };
}
