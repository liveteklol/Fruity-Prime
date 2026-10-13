#include "Mods/Network/Server/Core/ServerPeerIdentity.hpp"
#include "Mods/Network/Protocol/Packets/SessionPackets.hpp"

#include <cstdint>
#include <iostream>
#include <span>
#include <vector>

namespace
{
    using MphRead::Mods::Network::RosterPacket;
    using MphRead::Mods::Network::ServerPeerIdentity;

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
    bool passed = true;
    const std::vector<std::uint8_t> valid{6, 3, ' ', 'A', 'l', 'i', 'c', 'e', ' ', ' ', 0, 0};
    const auto identity = ServerPeerIdentity::Parse(valid);
    passed &= Check(identity.has_value(), "valid identity is accepted");
    if (identity.has_value())
    {
        passed &= Check(identity->Name == "Alice" && identity->Hunter == 6 && identity->Color == 3,
            "identity trims the wire name and preserves hunter and color");
    }

    passed &= Check(!ServerPeerIdentity::Parse(std::span<const std::uint8_t>{}).has_value(),
        "short payload is rejected");
    const std::vector<std::uint8_t> invalidHunter{7, 0, 'A'};
    passed &= Check(!ServerPeerIdentity::Parse(invalidHunter).has_value(), "non-playable hunter is rejected");
    const std::vector<std::uint8_t> invalidColor{0, 4, 'A'};
    passed &= Check(!ServerPeerIdentity::Parse(invalidColor).has_value(), "out-of-range color is rejected");
    const std::vector<std::uint8_t> emptyName{0, 0, ' ', ' ', 0};
    passed &= Check(!ServerPeerIdentity::Parse(emptyName).has_value(), "blank identity is rejected");

    std::vector<std::uint8_t> longName{0, 0};
    longName.insert(longName.end(), RosterPacket::MaxNameBytes + 5, static_cast<std::uint8_t>('x'));
    const auto truncated = ServerPeerIdentity::Parse(longName);
    passed &= Check(truncated.has_value() && truncated->Name.size() == RosterPacket::MaxNameBytes,
        "accepted name is capped at the roster wire limit");

    if (!passed)
    {
        return 1;
    }
    std::cout << "ServerPeerIdentity PASS\n";
    return 0;
}
