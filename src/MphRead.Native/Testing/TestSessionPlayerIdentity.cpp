#include "Mods/Network/Session/SessionPlayerIdentity.hpp"

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
    using MphRead::Hunter;
    using MphRead::Mods::Network::SessionPlayerIdentity;

    bool passed = true;
    SessionPlayerIdentity identity;
    passed &= Check(identity.SelectedHunter() == Hunter::Samus && identity.Color() == 0
        && identity.Name() == "Player", "local identity defaults");

    identity.SetHunter(Hunter::Sylux);
    identity.SetColor(6);
    identity.SetName("Local Player");
    passed &= Check(identity.SelectedHunter() == Hunter::Sylux && identity.Color() == 6
        && identity.Name() == "Local Player", "session identity updates as one owner");
    return passed ? 0 : 1;
}
