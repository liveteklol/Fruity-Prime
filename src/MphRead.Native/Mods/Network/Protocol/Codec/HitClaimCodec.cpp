#include "Mods/Network/Protocol/Codec/WirePrimitives.hpp"
#include "Mods/Network/Protocol/Packets/ClaimPackets.hpp"
#include "Mods/Network/Domain/NetworkVectorRules.hpp"

namespace MphRead::Mods::Network
{
    using namespace Protocol::Wire;

    void HitClaimPacket::Write(std::span<std::uint8_t> dest) const
    {
        W16(Slice(dest, 31), MatchId);
        W64(Slice(dest, 33), AuthorityEpoch);
        W16(Slice(dest, 41), ShooterGeneration);
        W16(Slice(dest, 43), ShooterLifeId);
        W16(Slice(dest, 45), VictimGeneration);
        W16(Slice(dest, 47), VictimLifeId);
        W16(Slice(dest, 0), ClaimId);
        W32(Slice(dest, 2), Frame);
        W32(Slice(dest, 6), AckFrame);
        W32(Slice(dest, 10), LaunchFrame);
        At(dest, 14) = VictimSlot;
        At(dest, 15) = Beam;
        W16(Slice(dest, 16), Damage);
        At(dest, 18) = Flags;
        WF(Slice(dest, 19), HitPoint.X);
        WF(Slice(dest, 23), HitPoint.Y);
        WF(Slice(dest, 27), HitPoint.Z);
        WF(Slice(dest, 49), Impulse.X);
        WF(Slice(dest, 53), Impulse.Y);
        WF(Slice(dest, 57), Impulse.Z);
        At(dest, 61) = static_cast<std::uint8_t>(Impact.X);
        At(dest, 62) = static_cast<std::uint8_t>(Impact.Y);
        At(dest, 63) = static_cast<std::uint8_t>(Impact.Z);
        W32(Slice(dest, 64), ShotSequence);
        W16(Slice(dest, 68), TurretDamage);
        At(dest, 70) = Cause;
    }
    HitClaimPacket HitClaimPacket::Read(std::span<const std::uint8_t> src)
    {
        HitClaimPacket packet;
        packet.MatchId = R16(Slice(src, 31));
        packet.AuthorityEpoch = R64(Slice(src, 33));
        packet.ShooterGeneration = R16(Slice(src, 41));
        packet.ShooterLifeId = R16(Slice(src, 43));
        packet.VictimGeneration = R16(Slice(src, 45));
        packet.VictimLifeId = R16(Slice(src, 47));
        packet.ClaimId = R16(Slice(src, 0));
        packet.Frame = R32(Slice(src, 2));
        packet.AckFrame = R32(Slice(src, 6));
        packet.LaunchFrame = R32(Slice(src, 10));
        packet.VictimSlot = At(src, 14);
        packet.Beam = At(src, 15);
        packet.Damage = R16(Slice(src, 16));
        packet.Flags = At(src, 18);
        packet.HitPoint = ::OpenTK::Mathematics::Vector3(
            RF(Slice(src, 19)), RF(Slice(src, 23)), RF(Slice(src, 27)));
        packet.Impulse = ::OpenTK::Mathematics::Vector3(RF(Slice(src, 49)), RF(Slice(src, 53)), RF(Slice(src, 57)));
        packet.Impact.X = static_cast<std::int8_t>(At(src, 61));
        packet.Impact.Y = static_cast<std::int8_t>(At(src, 62));
        packet.Impact.Z = static_cast<std::int8_t>(At(src, 63));
        if (!Domain::IsFiniteVector3(packet.Impulse)
            || packet.Impulse.LengthSquared() > 4.0F)
        {
            packet.Impulse = ::OpenTK::Mathematics::Vector3::Zero;
            packet.Flags = static_cast<std::uint8_t>(packet.Flags & ~FlagImpulse);
        }
        packet.ShotSequence = R32(Slice(src, 64));
        packet.TurretDamage = R16(Slice(src, 68));
        packet.Cause = At(src, 70) <= CauseDeathalt ? At(src, 70) : CauseHit;
        return packet;
    }
    void HitVerdictPacket::Write(std::span<std::uint8_t> dest,
        std::span<const std::pair<std::uint16_t, std::uint8_t>> entries,
        std::uint16_t matchId, std::uint64_t epoch, std::uint16_t generation,
        std::uint16_t lifeId)
    {
        At(dest, 0) = static_cast<std::uint8_t>(entries.size());
        W16(Slice(dest, 1), matchId);
        W64(Slice(dest, 3), epoch);
        W16(Slice(dest, 11), generation);
        W16(Slice(dest, 13), lifeId);
        for (std::size_t i = 0; i < entries.size(); i++)
        {
            const std::size_t at = HeaderSize + i * EntrySize;
            W16(Slice(dest, at), entries[i].first);
            At(dest, at + 2) = entries[i].second;
        }
    }
    std::string HitVerdictPacket::Describe(std::uint8_t result)
    {
        switch (result)
        {
        case ResultWrongLife: return "wrong lifecycle";
        case ResultGeometry: return "hit point outside reconciliation radius";
        case ResultDamageLimit: return "damage exceeds weapon limit";
        case ResultInvalidLaunch: return "launch frame follows hit frame";
        case ResultNoDamage: return "authority damage rules prevented the hit";
        case ResultBusy: return "authority had no room to judge it";
        case ResultApplied: return "applied";
        case ResultDuplicate: return "already resolved";
        case ResultDeadShooter: return "shooter was already dead when it fired";
        case ResultDeadVictim: return "victim was already down";
        case ResultRefused: return "refused";
        case ResultTooOld: return "older than the history";
        default: return "unknown";
        }
    }
}
