#include "Mods/Network/Gameplay/Claims/Pickup/PickupClaimOutbox.hpp"

#include <array>
#include <cstdint>
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

    MphRead::Mods::Network::PickupClaimPacket MakeClaim(std::int16_t spawnId)
    {
        MphRead::Mods::Network::PickupClaimPacket claim{};
        claim.MatchId = 17;
        claim.AuthorityEpoch = 0x1020304050607080ULL;
        claim.SlotGeneration = 3;
        claim.LifeId = 4;
        claim.SpawnId = spawnId;
        claim.SpawnCount = 5;
        claim.ContactFrame = 900;
        claim.Position = { 1.25F, 2.5F, -3.75F };
        return claim;
    }
}

int main()
{
    using namespace MphRead::Mods::Network;
    bool passed = true;
    PickupClaimOutbox outbox;
    const std::uint16_t first = outbox.Enqueue(MakeClaim(10));
    const std::uint16_t second = outbox.Enqueue(MakeClaim(11));
    passed &= Check(first == 1 && second == 2 && outbox.Declared() == 2 && outbox.PendingCount() == 2,
        "assigns claim ids and owns pending client claims");

    std::array<std::uint8_t, 1 + PickupClaimPacket::Size * PickupClaimPacket::MaxPerPacket> payload{};
    const std::int32_t encoded = outbox.Compose(payload, 8);
    passed &= Check(encoded == 1 + 2 * PickupClaimPacket::Size && payload[0] == 2,
        "composes a bounded batch with its count prefix");
    if (encoded > 0)
    {
        const PickupClaimPacket decodedFirst = PickupClaimPacket::Read(
            std::span<const std::uint8_t>(payload).subspan(1, PickupClaimPacket::Size));
        const PickupClaimPacket decodedSecond = PickupClaimPacket::Read(
            std::span<const std::uint8_t>(payload).subspan(1 + PickupClaimPacket::Size, PickupClaimPacket::Size));
        passed &= Check(decodedFirst.ClaimId == first && decodedSecond.ClaimId == second
            && decodedFirst.SpawnId == 10 && decodedSecond.SpawnId == 11
            && decodedFirst.ContactFrame == 900 && decodedFirst.Position.X == 1.25F,
            "preserves packet values and insertion order");
    }
    passed &= Check(outbox.Compose(payload, 8) == 0, "does not resend before the interval");
    outbox.Tick([](std::uint16_t) {});
    passed &= Check(outbox.Compose(payload, 1) == encoded,
        "resends after elapsed age even when the interval changes");

    passed &= Check(outbox.Resolve(first) && !outbox.Resolve(first) && outbox.PendingCount() == 1,
        "resolves an answer once");
    for (std::int32_t age = 1; age < PickupClaimOutbox::MaxAge; age++)
    {
        outbox.Tick([](std::uint16_t) {});
    }
    passed &= Check(outbox.PendingCount() == 1 && outbox.Unanswered() == 0,
        "claim remains live at the age boundary");
    std::uint16_t expired = 0;
    std::size_t expireCount = 0;
    outbox.Tick([&expired, &expireCount](std::uint16_t claimId)
    {
        expired = claimId;
        expireCount++;
    });
    passed &= Check(expireCount == 1 && expired == second
        && outbox.PendingCount() == 0 && outbox.Unanswered() == 1,
        "expires and reports unanswered claim ids at the configured boundary");

    outbox.Reset();
    passed &= Check(outbox.Declared() == 0 && outbox.Unanswered() == 0 && outbox.PendingCount() == 0,
        "clears pending claims and counters");
    passed &= Check(outbox.Enqueue(MakeClaim(12)) == 3,
        "preserves the claim sequence across a reset");

    PickupClaimOutbox full;
    for (std::size_t i = 0; i < PickupClaimOutbox::Capacity; i++)
    {
        passed &= Check(full.Enqueue(MakeClaim(static_cast<std::int16_t>(i))) != 0,
            "accepts claims up to fixed capacity");
    }
    passed &= Check(full.Enqueue(MakeClaim(99)) == 0 && full.Declared() == PickupClaimOutbox::Capacity,
        "refuses overflow without advancing declared count");
    return passed ? 0 : 1;
}
