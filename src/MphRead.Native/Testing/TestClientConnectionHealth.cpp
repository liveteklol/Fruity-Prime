#include "Mods/Network/Session/ClientConnectionHealth.hpp"

#include <cmath>
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
    using MphRead::Mods::Network::ClientConnectionHealth;

    bool passed = true;
    ClientConnectionHealth health;
    passed &= Check(!health.ConnectionLost() && health.LastServerPacket() == 0.0
        && health.Reannouncements() == 0 && health.LongestServerSilence() == 0.0
        && !health.Reannounced() && !health.Refused(), "initial connection state");

    health.Begin(10.0);
    passed &= Check(!health.TimedOut(12.0, 2.0) && health.TimedOut(12.001, 2.0),
        "timeout threshold remains strict");
    passed &= Check(!health.ReannouncementDue(15.0, 5.0)
        && health.ReannouncementDue(15.001, 5.0), "rejoin threshold remains strict");

    const ClientConnectionHealth::Reannouncement first = health.StartReannouncement(15.001, 5.0);
    passed &= Check(first.Started && first.BecameLost && health.ConnectionLost()
        && health.Reannounced() && health.Reannouncements() == 1
        && std::abs(first.SilentSeconds - 5.001) < 0.000001,
        "first silent interval begins one reannouncement and reports its duration");
    const ClientConnectionHealth::Reannouncement duplicate = health.StartReannouncement(20.0, 5.0);
    passed &= Check(!duplicate.Started && health.Reannouncements() == 1,
        "one outage is counted once until admission");

    passed &= Check(health.RecordServerPacket(16.0) && !health.ConnectionLost()
        && health.LastServerPacket() == 16.0 && std::abs(health.LongestServerSilence() - 6.0) < 0.000001,
        "server packet records the silence and reports recovery");
    passed &= Check(!health.RecordServerPacket(17.0) && health.LongestServerSilence() == 6.0,
        "ordinary server packets do not report another recovery");
    health.RequestReannouncement();
    passed &= Check(health.Reannounced() && health.Admit() && !health.Reannounced() && !health.Admit(),
        "welcome admission clears a requested reannouncement marker once");

    MphRead::Mods::Network::RefusedPacket refusal{};
    refusal.Reason = MphRead::Mods::Network::RefusedPacket::ReasonFull;
    refusal.Players = 8;
    health.Refuse(refusal);
    passed &= Check(health.Refused() && health.RefusedReason().Reason == refusal.Reason
        && health.RefusedReason().Players == refusal.Players,
        "admission refusal is owned by connection health");
    health.ClearRefusal();
    passed &= Check(!health.Refused(), "clearing a refusal leaves other health state intact");

    health.Reset();
    passed &= Check(!health.ConnectionLost() && health.LastServerPacket() == 0.0
        && health.Reannouncements() == 0 && health.LongestServerSilence() == 0.0
        && !health.Reannounced() && !health.Refused(), "session stop resets all owned connection state");
    return passed ? 0 : 1;
}
