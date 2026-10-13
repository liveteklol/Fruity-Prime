#include "Mods/Network/Server/Match/ServerIntentValidator.hpp"

#include <cstdint>
#include <iostream>
#include <string_view>
#include <vector>

namespace
{
    using namespace MphRead::Mods::Network;

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
    constexpr std::uint16_t MatchId = 17;
    constexpr std::uint64_t AuthorityEpoch = 23;
    constexpr std::uint16_t SlotGeneration = 31;
    constexpr std::uint16_t LifeId = 4;

    IntentPacket intent{};
    intent.MatchId = MatchId;
    intent.AuthorityEpoch = AuthorityEpoch;
    intent.SlotGeneration = SlotGeneration;
    intent.LifeId = LifeId;
    intent.Frame = 99;
    std::vector<std::uint8_t> payload(IntentPacket::Size);
    intent.Write(payload);

    bool passed = true;
    const auto validated = ServerIntentValidator::Validate(
        payload, MatchId, AuthorityEpoch, SlotGeneration, LifeId);
    passed &= Check(validated && validated->Frame == intent.Frame, "valid intent is decoded");
    passed &= Check(!ServerIntentValidator::Validate(payload, MatchId + 1, AuthorityEpoch,
        SlotGeneration, LifeId), "cross-match intent is rejected");
    passed &= Check(!ServerIntentValidator::Validate(payload, MatchId, AuthorityEpoch + 1,
        SlotGeneration, LifeId), "stale authority epoch is rejected");
    passed &= Check(!ServerIntentValidator::Validate(payload, MatchId, AuthorityEpoch,
        SlotGeneration + 1, LifeId), "stale slot generation is rejected");
    passed &= Check(!ServerIntentValidator::Validate(payload, MatchId, AuthorityEpoch,
        SlotGeneration, LifeId + 1), "stale life is rejected");
    passed &= Check(!ServerIntentValidator::Validate(
        std::vector<std::uint8_t>(IntentPacket::Size - 1), MatchId, AuthorityEpoch,
        SlotGeneration, LifeId), "truncated intent is rejected");
    passed &= Check(!ServerIntentValidator::Validate(
        std::vector<std::uint8_t>(IntentPacket::MaxWireSize + 1), MatchId, AuthorityEpoch,
        SlotGeneration, LifeId), "oversized intent is rejected");
    return passed ? 0 : 1;
}
