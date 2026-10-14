#include "Mods/Network/Server/Core/DedicatedServer.hpp"

#include "Mods/Network/Server/Match/MapRotation.hpp"

#include <algorithm>
#include <cmath>

namespace MphRead::Mods::Network
{
    MatchDefinition DedicatedServer::CurrentDefinition() const
    {
        return _lobbyState.Policy() == ServerSessionPolicy::Lobby
            ? (_lobbyState.Phase() == SessionPhase::Lobby ? _lobbyState.LobbyMatch() : _lobbyState.FrozenMatch())
            : DefinitionFor(*_rotation->Current());
    }

    MatchDefinition DedicatedServer::DefinitionFor(const RotationEntry& entry) const
    {
        MatchDefinition definition{};
        definition.RoomKey = entry.RoomKey;
        definition.Mode = entry.Mode;
        definition.Format = _lobbyState.Format();
        const float clampedTimeLimit = std::clamp(entry.TimeLimit, 0.0F, 65535.0F);
        // Math.Clamp preserves NaN, and C#'s unchecked float-to-integer
        // conversion leaves that case unspecified. Pick a stable value here
        // instead of invoking C++'s undefined float-to-integer conversion.
        definition.TimeLimitSeconds = std::isnan(clampedTimeLimit)
            ? 0 : static_cast<std::uint16_t>(clampedTimeLimit);
        definition.PointGoal = static_cast<std::uint16_t>(std::clamp(entry.PointGoal, 0, 0xFFFF));
        definition.FriendlyFire = _friendlyFire;
        definition.AffinityWeapons = _affinityWeapons;
        definition.ShadowFreeze = _shadowFreeze;
        definition.Instagib = _instagib;
        return definition;
    }
}
