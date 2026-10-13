#include "Mods/Network/Session/NetSession.hpp"
#include "Mods/Network/Session/SessionPorts.hpp"

#include <iostream>
#include <string_view>

namespace
{
    bool Check(bool condition, std::string_view name)
    {
        if (!condition)
        {
            std::cerr << "FAIL " << name << '\n';
            return false;
        }
        return true;
    }
}

int main()
{
    using namespace MphRead::Mods::Network;

    SessionStatePacket session{};
    session.AuthorityEpoch = 1;
    session.MatchId = 1;
    session.Revision = 1;
    session.MaxPlayers = 4;
    RosterPacket roster = RosterPacket::Create();
    roster.AuthorityEpoch = session.AuthorityEpoch;
    roster.MatchId = session.MatchId;
    roster.Revision = 1;

    DedicatedServerSessionPort port = MakeNetSessionDedicatedServerPort();
    bool passed = Check(port.Clock.ReadFrame != nullptr && port.Clock.ReadClock != nullptr
        && port.Identity.ReadMatchId != nullptr && port.Identity.ReadAuthorityEpoch != nullptr
        && port.ApplySessionState != nullptr && port.ApplyMatchState != nullptr
        && port.ApplyRoster != nullptr && port.AcceptSlotIntent != nullptr,
        "dedicated server receives a complete session port")
        && Check(static_cast<bool>(port.Simulation.StartAuthority)
            && static_cast<bool>(port.Simulation.Stop),
            "dedicated server port exposes simulation lifecycle separately");

    port.Simulation.StartAuthority([](std::span<const std::uint8_t>) {}, [] {}, roster, session);
    passed &= Check(NetSession::Role() == NetRole::Server,
        "simulation adapter starts the authoritative session")
        && Check(NetSession::ServerSession().has_value()
            && NetSession::ServerSession()->MatchId == session.MatchId
            && NetSession::ServerSession()->Revision == session.Revision,
            "simulation adapter applies the initial session projection")
        && Check(port.Identity.ReadMatchId() == session.MatchId
            && port.Identity.ReadAuthorityEpoch() == session.AuthorityEpoch,
            "dedicated server port reads the applied protocol identity")
        && Check(port.Clock.ReadClock() >= 0.0,
            "dedicated server port reads the active session clock");

    SessionStatePacket updatedSession = session;
    updatedSession.Revision++;
    port.ApplySessionState(updatedSession);
    passed &= Check(NetSession::ServerSession().has_value()
        && NetSession::ServerSession()->Revision == updatedSession.Revision,
        "dedicated server port applies session projections");

    port.Simulation.Stop();
    passed &= Check(NetSession::Role() == NetRole::Offline
        && !NetSession::ServerSession().has_value(),
        "simulation adapter stops the session and clears its projection");
    return passed ? 0 : 1;
}
