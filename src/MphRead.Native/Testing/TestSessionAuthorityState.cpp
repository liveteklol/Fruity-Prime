#include "Mods/Network/Session/SessionAuthorityState.hpp"

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
    using MphRead::Mods::Network::SessionAuthorityState;

    bool passed = true;
    SessionAuthorityState authority;
    passed &= Check(!authority.IsAuthority() && authority.Frames() == 0 && authority.StandDowns() == 0,
        "initial state");

    authority.BecomeServerAuthority();
    authority.RecordServerFrame();
    authority.RecordAuthorityFrame();
    passed &= Check(authority.IsAuthority() && authority.Frames() == 2,
        "server authority records every authoritative frame");
    passed &= Check(!authority.ConsumeStateSync(), "server authority does not request a client state sync");
    passed &= Check(!authority.BecomeClientAuthority(), "already-authoritative state is idempotent");

    passed &= Check(authority.StandDown() && !authority.IsAuthority() && authority.StandDowns() == 1,
        "stand-down records an active lease transition");
    passed &= Check(!authority.StandDown() && authority.StandDowns() == 1,
        "stand-down without a lease has no effect");

    passed &= Check(authority.BecomeClientAuthority() && authority.IsAuthority(), "client accepts a new lease");
    passed &= Check(authority.ConsumeStateSync() && !authority.ConsumeStateSync(),
        "client lease requests one state synchronization");
    authority.RecordAuthorityFrame();
    passed &= Check(authority.Frames() == 3, "client authority records an authoritative frame");

    authority.Revoke();
    passed &= Check(!authority.IsAuthority() && authority.StandDowns() == 1,
        "epoch revoke does not count as reconnect stand-down");
    authority.Reset();
    passed &= Check(!authority.IsAuthority() && authority.Frames() == 0 && authority.StandDowns() == 0
        && !authority.ConsumeStateSync(), "session reset clears lease and measurements");
    return passed ? 0 : 1;
}
