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
    passed &= Check(authority.TryClaim(first) && authority.HasAuthority() && authority.Epoch() == 41,
        "the first peer claims authority and advances the epoch");
    passed &= Check(authority.IsAuthority(first) && !authority.IsAuthority(second),
        "authority membership compares the selected peer");
    passed &= Check(!authority.TryClaim(second) && authority.Current() == first && authority.Epoch() == 41,
        "a second claim cannot displace the current authority");

    authority.TransferTo(second);
    passed &= Check(authority.Current() == second && authority.IsAuthority(second) && authority.Epoch() == 42,
        "authority migration selects the successor and advances the epoch");
    authority.TransferTo(nullptr);
    passed &= Check(!authority.HasAuthority() && authority.Current() == nullptr && authority.Epoch() == 43,
        "losing the final authority still advances the fencing epoch");
    passed &= Check(!authority.IsAuthority(nullptr), "a missing peer is never an authority");

    if (!passed)
    {
        return 1;
    }
    std::cout << "ServerAuthorityState PASS\n";
    return 0;
}
