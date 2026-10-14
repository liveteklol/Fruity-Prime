#include "Mods/Network/Server/Core/ServerAuthorityState.hpp"

#include <iostream>
#include <memory>

namespace
{
    using MphRead::Mods::Network::ServerAuthorityState;

    bool Check(bool condition, const char* name)
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
    ServerAuthorityState authority(40);
    auto first = std::make_shared<ServerAuthorityState::PeerHandle::element_type>();
    auto second = std::make_shared<ServerAuthorityState::PeerHandle::element_type>();
    bool passed = true;

    passed &= Check(!authority.HasAuthority() && authority.Epoch() == 40,
        "authority starts unassigned at the supplied epoch");
    passed &= Check(!authority.TryClaim(nullptr) && authority.Epoch() == 40,
        "a null peer cannot claim authority");
    passed &= Check(!authority.TryClaim(first) && !authority.TryClaim(second)
        && !authority.HasAuthority() && authority.Current() == nullptr && authority.Epoch() == 40,
        "neither creator nor later participants may acquire authority");
    authority.TransferTo(second);
    authority.TransferTo(nullptr);
    passed &= Check(!authority.IsAuthority(first) && !authority.IsAuthority(second)
        && !authority.IsAuthority(nullptr) && authority.Epoch() == 40,
        "creator departure cannot migrate authority or change the server epoch");

    if (!passed)
    {
        return 1;
    }
    std::cout << "ServerAuthorityState PASS\n";
    return 0;
}
