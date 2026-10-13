#include "Mods/Network/Session/SessionPorts.hpp"

#include "Mods/Network/Session/NetSession.hpp"

#include <utility>

namespace MphRead::Mods::Network
{
    ServerSimulationSessionPort MakeNetSessionServerSimulationPort()
    {
        ServerSimulationSessionPort port{};
        port.StartAuthority = [](SnapshotSink sink, std::function<void()> matchEnded,
            const std::optional<RosterPacket>& roster,
            const std::optional<SessionStatePacket>& session)
        {
            NetSession::StartServerAuthority(std::move(sink), std::move(matchEnded));
            if (roster.has_value())
            {
                NetSession::ApplyRoster(*roster);
            }
            if (session.has_value())
            {
                NetSession::ApplySessionState(*session);
            }
        };
        port.Stop = []() { NetSession::Stop(); };
        return port;
    }

    DedicatedServerSessionPort MakeNetSessionDedicatedServerPort()
    {
        DedicatedServerSessionPort port{};
        port.Simulation = MakeNetSessionServerSimulationPort();
        port.Clock.ReadFrame = &NetSession::NetFrame;
        port.Clock.ReadClock = &NetSession::Clock;
        port.Identity.ReadMatchId = &NetSession::CurrentMatchId;
        port.Identity.ReadAuthorityEpoch = &NetSession::AuthorityEpoch;
        port.ApplySessionState = &NetSession::ApplySessionState;
        port.ApplyMatchState = &NetSession::ApplyMatchState;
        port.ApplyRoster = &NetSession::ApplyRoster;
        port.AcceptSlotIntent = &NetSession::AcceptSlotIntent;
        return port;
    }
}
