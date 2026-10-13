#include "Mods/Network/Server/Core/ServerPeerProfile.hpp"

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
    using MphRead::Mods::Network::ServerPeerProfile;
    using MphRead::Mods::Network::ServerPlayerIdentity;

    bool passed = true;
    ServerPeerProfile profile;
    passed &= Check(profile.Name().empty() && profile.Hunter() == 0 && profile.Color() == 0,
        "profile starts without a reported identity");

    ServerPlayerIdentity first{};
    first.Name = "Player One";
    first.Hunter = 2;
    first.Color = 1;
    const ServerPeerProfile::Update initial = profile.Apply(first);
    passed &= Check(initial.Changed && initial.FirstName && initial.AppearanceChanged
        && profile.Name() == "Player One" && profile.Hunter() == 2 && profile.Color() == 1,
        "first validated identity is stored and reported as an appearance change");

    const ServerPeerProfile::Update duplicate = profile.Apply(first);
    passed &= Check(!duplicate.Changed && !duplicate.FirstName && !duplicate.AppearanceChanged,
        "repeated identity payload does not create a false change");

    ServerPlayerIdentity rename = first;
    rename.Name = "Player Two";
    const ServerPeerProfile::Update renamed = profile.Apply(rename);
    passed &= Check(renamed.Changed && !renamed.FirstName && !renamed.AppearanceChanged
        && profile.Name() == "Player Two",
        "renaming retains appearance and does not repeat the join announcement");

    ServerPlayerIdentity appearance = rename;
    appearance.Color = 3;
    const ServerPeerProfile::Update changedAppearance = profile.Apply(appearance);
    passed &= Check(changedAppearance.Changed && !changedAppearance.FirstName
        && changedAppearance.AppearanceChanged && profile.Color() == 3,
        "hunter or color change is distinguishable for ready invalidation");

    return passed ? 0 : 1;
}
