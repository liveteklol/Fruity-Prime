#pragma once

#include "Mods/Network/Protocol/NetProtocol.hpp"
#include "Mods/Network/Protocol/SessionProtocol.hpp"
#include "Mods/Multiplayer/MatchWorldProfile.hpp"
#include "NativeRuntime/System/Number.hpp"

#include <cstdint>
#include <string>

namespace MphRead::Mods::Network
{
    // The match a simulating authority is started on by the item tests
    // (HealthSimulationTest, PowerUpSimulationTest): eight players in one
    // battle that nobody wins, ten minutes long.
    struct SimulationFixture final
    {
        static constexpr std::uint8_t Players = 8;

        [[nodiscard]] static SessionStatePacket Session(const std::string& room)
        {
            SessionStatePacket session{};
            session.Phase = SessionPhase::InMatch;
            session.Policy = ServerSessionPolicy::Lobby;
            session.Revision = 1;
            session.MatchId = 1;
            session.AuthorityEpoch = 1;
            session.MaxPlayers = Players;
            session.OwnerSlot = 0;
            session.Match.RoomKey = room;
            session.Match.Mode = GameMode::Battle;
            session.Match.Format = MatchFormat::FreeForAll;
            session.Match.PointGoal = 999;
            session.Match.TimeLimitSeconds = 600;
            session.WorldProfile = Multiplayer::MatchWorldProfile::Resolve(Players);
            return session;
        }

        [[nodiscard]] static RosterPacket Roster(const std::string& namePrefix)
        {
            RosterPacket roster = RosterPacket::Create();
            roster.MatchId = 1;
            roster.AuthorityEpoch = 1;
            roster.Revision = 1;
            roster.SessionRevision = 1;
            for (std::uint8_t i = 0; i < Players; i++)
            {
                (*roster.Slots)[i] = i;
                (*roster.Generations)[i] = 1;
                (*roster.Teams)[i] = -1;
                (*roster.Names)[i] = namePrefix + ::MphRead::NativeRuntime::ToString(i);
                roster.Count++;
            }
            return roster;
        }
    };
}
