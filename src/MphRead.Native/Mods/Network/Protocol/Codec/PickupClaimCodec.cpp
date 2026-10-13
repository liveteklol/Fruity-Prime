#include "Mods/Network/Protocol/Codec/WirePrimitives.hpp"
#include "Mods/Network/Protocol/Packets/ClaimPackets.hpp"

namespace MphRead::Mods::Network
{
    using namespace Protocol::Wire;

    void PickupClaimPacket::Write(std::span<std::uint8_t> dest) const
    {
        W16(Slice(dest, 0), MatchId);
        W64(Slice(dest, 2), AuthorityEpoch);
        W16(Slice(dest, 10), SlotGeneration);
        W16(Slice(dest, 12), LifeId);
        W16(Slice(dest, 14), ClaimId);
        WI16(Slice(dest, 16), SpawnId);
        W16(Slice(dest, 18), SpawnCount);
        W32(Slice(dest, 20), ContactFrame);
        WF(Slice(dest, 24), Position.X);
        WF(Slice(dest, 28), Position.Y);
        WF(Slice(dest, 32), Position.Z);
    }

    PickupClaimPacket PickupClaimPacket::Read(std::span<const std::uint8_t> src)
    {
        PickupClaimPacket packet;
        packet.MatchId = R16(Slice(src, 0));
        packet.AuthorityEpoch = R64(Slice(src, 2));
        packet.SlotGeneration = R16(Slice(src, 10));
        packet.LifeId = R16(Slice(src, 12));
        packet.ClaimId = R16(Slice(src, 14));
        packet.SpawnId = RI16(Slice(src, 16));
        packet.SpawnCount = R16(Slice(src, 18));
        packet.ContactFrame = R32(Slice(src, 20));
        packet.Position = ::OpenTK::Mathematics::Vector3(RF(Slice(src, 24)), RF(Slice(src, 28)), RF(Slice(src, 32)));
        return packet;
    }

    void PickupVerdictPacket::Write(std::span<std::uint8_t> dest, std::span<const Entry> entries,
        std::uint16_t matchId, std::uint64_t epoch, std::uint16_t generation, std::uint16_t lifeId)
    {
        At(dest, 0) = static_cast<std::uint8_t>(entries.size());
        W16(Slice(dest, 1), matchId);
        W64(Slice(dest, 3), epoch);
        W16(Slice(dest, 11), generation);
        W16(Slice(dest, 13), lifeId);
        for (std::size_t i = 0; i < entries.size(); i++)
        {
            const std::size_t at = HeaderSize + i * EntrySize;
            W16(Slice(dest, at), entries[i].ClaimId);
            At(dest, at + 2) = entries[i].Result;
            At(dest, at + 3) = entries[i].Sequence;
        }
    }

    std::string PickupVerdictPacket::Describe(std::uint8_t result)
    {
        switch (result)
        {
        case ResultGranted: return "granted";
        case ResultTaken: return "somebody else had it first";
        case ResultGone: return "not the spawn the claim named";
        case ResultOutOfReach: return "the authority never had the player within reach";
        case ResultTooOld: return "the contact was too long ago";
        case ResultWrongLife: return "the claimant's life is over";
        case ResultNotClaimable: return "not an item a client claims";
        default: return "unknown";
        }
    }
}
