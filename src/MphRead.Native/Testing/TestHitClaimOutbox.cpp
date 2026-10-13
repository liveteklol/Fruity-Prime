#include "Mods/Network/Gameplay/Claims/Hit/HitClaimOutbox.hpp"

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

    MphRead::Mods::Network::HitClaimOutbox::Entry MakeEntry(std::uint8_t victimSlot)
    {
        MphRead::Mods::Network::HitClaimOutbox::Entry entry;
        entry.MatchId = 17;
        entry.AuthorityEpoch = 0x1020304050607080ULL;
        entry.ShooterGeneration = 3;
        entry.ShooterLifeId = 4;
        entry.VictimGeneration = 5;
        entry.VictimLifeId = 6;
        entry.Frame = 100;
        entry.AckFrame = 90;
        entry.LaunchFrame = 91;
        entry.VictimSlot = victimSlot;
        entry.Beam = 2;
        entry.Damage = 35;
        return entry;
    }
}

int main()
{
    using namespace MphRead::Mods::Network;

    bool passed = true;
    HitClaimOutbox outbox;
    passed &= Check(outbox.Declared() == 0 && outbox.Unanswered() == 0 && outbox.Resends() == 0,
        "initial counters");

    const std::uint16_t firstId = outbox.Enqueue(MakeEntry(2));
    const std::uint16_t secondId = outbox.Enqueue(MakeEntry(3));
    passed &= Check(firstId == 1 && secondId == 2 && outbox.Declared() == 2, "claim ids are assigned by outbox");

    std::array<std::uint8_t, 1 + HitClaimPacket::Size * HitClaimPacket::MaxPerPacket> packetBytes{};
    const auto encoded = outbox.Compose(packetBytes, 100);
    passed &= Check(encoded == 1 + 2 * HitClaimPacket::Size, "compose writes bounded live claims");
    passed &= Check(packetBytes[0] == 2, "compose reports claim count");
    if (encoded > 0)
    {
        const HitClaimPacket first = HitClaimPacket::Read(std::span<const std::uint8_t>(packetBytes).subspan(1));
        const HitClaimPacket second = HitClaimPacket::Read(
            std::span<const std::uint8_t>(packetBytes).subspan(1 + HitClaimPacket::Size));
        passed &= Check(first.ClaimId == firstId && second.ClaimId == secondId, "wire order follows queue order");
        passed &= Check(first.MatchId == 17 && first.AuthorityEpoch == 0x1020304050607080ULL
            && first.AckFrame == 90 && first.LaunchFrame == 91 && first.Damage == 35,
            "outbox preserves hit claim wire fields");
    }
    passed &= Check(outbox.Compose(packetBytes, 100) == 0, "claims are not resent before their interval");

    std::array<std::uint16_t, HitClaimOutbox::Capacity> expiredIds{};
    std::size_t expiredCount = 0;
    const auto onExpired = [&expiredIds, &expiredCount](const HitClaimOutbox::Entry& entry)
    {
        expiredIds[expiredCount++] = entry.Id;
    };
    outbox.Tick(2, onExpired);
    outbox.Tick(2, onExpired);
    outbox.Tick(2, onExpired);
    passed &= Check(expiredCount == 2 && expiredIds[0] == firstId && expiredIds[1] == secondId,
        "claims expire after the configured age");
    passed &= Check(outbox.Unanswered() == 2 && outbox.FindLive(firstId) == nullptr,
        "expired claims leave the outbox and count unanswered");

    HitClaimOutbox overflow;
    for (std::size_t i = 0; i < HitClaimOutbox::Capacity; i++)
    {
        (void)overflow.Enqueue(MakeEntry(1));
    }
    const auto overflowId = overflow.Enqueue(MakeEntry(4));
    passed &= Check(overflowId == HitClaimOutbox::Capacity + 1 && overflow.Unanswered() == 1,
        "full queue replaces the oldest and records an unanswered claim");
    passed &= Check(overflow.FindLive(1) == nullptr && overflow.FindLive(overflowId) != nullptr,
        "queue overflow removes only its selected entry");
    overflow.ForgetSlot(4, 0);
    passed &= Check(overflow.FindLive(overflowId) == nullptr && overflow.FindLive(2) != nullptr,
        "slot cleanup removes claims for the departing victim");
    overflow.ForgetSlot(0, 0);
    passed &= Check(overflow.FindLive(2) == nullptr, "local slot cleanup clears the complete outbox");
    overflow.Reset();
    passed &= Check(overflow.Declared() == 0 && overflow.Unanswered() == 0 && overflow.Resends() == 0,
        "reset clears outbox sequence counters");

    return passed ? 0 : 1;
}
