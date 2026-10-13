#include "Mods/Network/Session/NetMatchSync.hpp"
#include "Mods/Network/Domain/MatchDefinition.hpp"

#include "GameState.hpp"
#include "NativeRuntime/System/Console.hpp"
#include "NativeRuntime/System/Globalization.hpp"
#include "Mods/Network/Session/NetMatchEnd.hpp"
#include "Mods/Network/Protocol/NetProtocol.hpp"
#include "Mods/Network/Session/NetSession.hpp"

#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace MphRead::Mods::Network
{
    std::string NetMatchSync::_lastRoom = "";
    bool NetMatchSync::_everSynced = false;
    float NetMatchSync::_lastDrift = 0.0f;

    void NetMatchSync::Reset()
    {
        _lastRoom = "";
        _everSynced = false;
    }

    bool NetMatchSync::Synced()
    {
        return _everSynced;
    }

    float NetMatchSync::LastDrift()
    {
        return _lastDrift;
    }

    void NetMatchSync::Apply()
    {
        if (!NetSession::Active() || !NetSession::ServerMatch().has_value())
        {
            return;
        }

        const MatchStatePacket state = NetSession::ServerMatch().value();
        // No room yet (a server simulation before its first match state, as
        // -simcheck runs it) is the same as an empty one, not an exception.
        if (!state.RoomKey.has_value() || state.RoomKey->empty())
        {
            return;
        }
        const std::string& roomKey = *state.RoomKey;

        const GameMode mode = ::MphRead::IsDefinedGameMode(state.Mode)
            ? static_cast<GameMode>(state.Mode)
            : GameState::Mode();
        if (MatchGoalRules::UsesTimeTarget(mode))
        {
            if (GameState::TimeGoal() != static_cast<std::int32_t>(state.PointGoal))
            {
                GameState::TimeGoal(static_cast<std::int32_t>(state.PointGoal));
            }
        }
        else if (GameState::PointGoal() != static_cast<std::int32_t>(state.PointGoal))
        {
            GameState::PointGoal(static_cast<std::int32_t>(state.PointGoal));
        }

        GameState::FriendlyFire(state.FriendlyFire());
        GameState::ShadowFreeze(state.ShadowFreeze());
        // And whether weapon pickups are the picking hunter's affinity
        // variant, which is a different row of the damage table. Silence is
        // not a no: a server built before this says nothing, and nothing
        // means "keep playing by the local setting". The damage level is
        // pinned to medium on every machine. GameState.DamageLevel.
        if (state.StatesRules())
        {
            GameState::AffinityWeapons(state.AffinityWeapons());
        }

        if (NetMatchEnd::InIntermission())
        {
            _lastRoom = roomKey;
            return;
        }

        // Negative is the engine/HUD's no-timer sentinel. Infinity cannot
        // be converted to TimeSpan by the music and HUD timer paths.
        if (const std::optional<MatchDefinition> definition = NetSession::ActiveMatchDefinition();
            definition.has_value() && definition->TimeLimitSeconds == 0 && !state.Ending())
        {
            GameState::MatchTime(-1.0F);
            return;
        }
        if (state.TimeRemaining <= 0.0f && state.TimeElapsed <= 0.0f)
        {
            return;
        }

        const bool newMatch = roomKey != _lastRoom;
        _lastRoom = roomKey;

        _lastDrift = GameState::MatchTime() - state.TimeRemaining;
        if (newMatch || !_everSynced || std::fabs(_lastDrift) > 1.5f)
        {
            GameState::MatchTime(state.TimeRemaining);
            if (!_everSynced || newMatch)
            {
                std::string message = "[net] match clock synced to server: ";
                message += ::MphRead::NativeRuntime::ToString(state.TimeRemaining, "0");
                message += " s remaining on ";
                message += roomKey;
                NativeRuntime::ConsoleWriteLine(message);
            }
            _everSynced = true;
        }
    }
}
