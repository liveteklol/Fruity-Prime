#include "Mods/Network/Server/Match/ServerSnapshotStream.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <limits>
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
    using MphRead::Mods::Network::ServerSnapshotStream;

    bool passed = true;
    ServerSnapshotStream stream;
    constexpr std::array<std::uint8_t, 4> first{ 1, 2, 3, 4 };
    constexpr std::array<std::uint8_t, 3> second{ 9, 8, 7 };
    passed &= Check(!stream.HasLatest() && stream.Latest().empty()
        && stream.CanAcceptIncoming(1), "initial cache and frame gate");
    passed &= Check(stream.CacheLatest(first) && stream.HasLatest()
        && std::equal(stream.Latest().begin(), stream.Latest().end(), first.begin(), first.end())
        && stream.CanAcceptIncoming(1), "outbound snapshot cache does not consume incoming sequence");

    passed &= Check(stream.AcceptIncoming(std::numeric_limits<std::uint32_t>::max() - 1, first)
        && !stream.CanAcceptIncoming(std::numeric_limits<std::uint32_t>::max() - 1)
        && !stream.AcceptIncoming(std::numeric_limits<std::uint32_t>::max() - 1, second),
        "incoming authority snapshots reject duplicates");
    passed &= Check(stream.AcceptIncoming(0, second)
        && std::equal(stream.Latest().begin(), stream.Latest().end(), second.begin(), second.end()),
        "incoming sequence wraps and refreshes the cached bytes");

    stream.ResetIncomingSequence();
    passed &= Check(stream.CanAcceptIncoming(1) && stream.HasLatest()
        && std::equal(stream.Latest().begin(), stream.Latest().end(), second.begin(), second.end()),
        "authority transition resets ordering while retaining the current cache");
    stream.ClearLatest();
    passed &= Check(!stream.HasLatest() && stream.Latest().empty()
        && stream.CanAcceptIncoming(0), "new match clears cache and sequence independently");
    return passed ? 0 : 1;
}
