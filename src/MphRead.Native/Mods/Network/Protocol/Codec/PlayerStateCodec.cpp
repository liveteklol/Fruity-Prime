#include "Mods/Network/Protocol/Codec/WirePrimitives.hpp"
#include "Mods/Network/Protocol/Packets/RealtimePackets.hpp"
#include "NativeRuntime/System/Managed.hpp"

namespace MphRead::Mods::Network
{
    using namespace Protocol::Wire;
    std::int16_t DamageEvent::PackDirection(float value) noexcept
    {
        const std::int32_t rounded = ::MphRead::NativeRuntime::ConvertToInt32Net9(
            ::MphRead::NativeRuntime::RoundToEven(value * DirectionScale));
        return static_cast<std::int16_t>(std::clamp<std::int32_t>(rounded, -32768, 32767));
    }
    float DamageEvent::UnpackDirection(std::int16_t value) noexcept
    {
        return static_cast<float>(value) / DirectionScale;
    }
    void DamageEvent::Write(std::span<std::uint8_t> dest) const
    {
        W16(dest, EventId);
        W16(Slice(dest, 2), AttackerGeneration);
        W16(Slice(dest, 4), Damage);
        At(dest, 6) = AttackerSlot;
        At(dest, 7) = Beam;
        At(dest, 8) = Flags;
        WI16(Slice(dest, 9), PackDirection(Direction.X));
        WI16(Slice(dest, 11), PackDirection(Direction.Y));
        WI16(Slice(dest, 13), PackDirection(Direction.Z));
    }
    ImpactOffset ImpactOffset::From(::OpenTK::Mathematics::Vector3 offset) noexcept
    {
        ImpactOffset packed{};
        if (!std::isfinite(offset.X) || !std::isfinite(offset.Y) || !std::isfinite(offset.Z))
        {
            return packed;
        }
        const auto pack = [](float value)
        {
            return static_cast<std::int8_t>(std::clamp(std::lround(value * Scale), -127L, 127L));
        };
        packed.X = pack(offset.X);
        packed.Y = pack(offset.Y);
        packed.Z = pack(offset.Z);
        return packed;
    }
    DamageEvent DamageEvent::Read(std::span<const std::uint8_t> src)
    {
        DamageEvent value;
        value.EventId = R16(src);
        value.AttackerGeneration = R16(Slice(src, 2));
        value.Damage = R16(Slice(src, 4));
        value.AttackerSlot = At(src, 6);
        value.Beam = At(src, 7);
        value.Flags = At(src, 8);
        value.Direction = ::OpenTK::Mathematics::Vector3(
            UnpackDirection(RI16(Slice(src, 9))),
            UnpackDirection(RI16(Slice(src, 11))),
            UnpackDirection(RI16(Slice(src, 13))));
        return value;
    }
    DamageEvent PlayerState::EventAt(std::int32_t index) const
    {
        switch (index)
        {
        case 0: return Damage0;
        case 1: return Damage1;
        case 2: return Damage2;
        case 3: return Damage3;
        default: throw System::ArgumentOutOfRangeException("index");
        }
    }
    void PlayerState::Write(std::span<std::uint8_t> dest) const
    {
        if (dest.size() < Size) throw std::out_of_range("PlayerState protocol 31 requires " + std::to_string(Size) + " bytes");
        At(dest, 0) = SlotIndex;
        At(dest, 1) = Flags;
        WriteVec(Slice(dest, 2), Position);
        WriteVec(Slice(dest, 14), Speed);
        WriteVec(Slice(dest, 26), Facing);
        W16(Slice(dest, 38), Health);
        At(dest, 40) = CurrentWeapon;
        At(dest, 41) = Team;
        WI16(Slice(dest, 42), Points);
        W16(Slice(dest, 44), Kills);
        W16(Slice(dest, 46), Deaths);
        W16(Slice(dest, 48), SlotGeneration);
        W16(Slice(dest, 50), LifeId);
        W16(Slice(dest, 52), DamageEventId);
        for (std::int32_t i = 0; i < DamageHistory; i++)
        {
            EventAt(i).Write(Slice(dest, 54 + static_cast<std::size_t>(i) * DamageEvent::Size));
        }
        At(dest, LegacySize) = WeavelFlags;
        At(dest, LegacySize + 1) = HalfturretHealth;
        WriteVec(Slice(dest, LegacySize + 2), HalfturretPosition);
        At(dest, LegacySize + 14) = ImpactLaunchLow;
        At(dest, LegacySize + 15) = static_cast<std::uint8_t>(Impact.Y);
        std::uint8_t bearing = 0;
        if (Impact.Known() && (Impact.X != 0 || Impact.Z != 0))
        {
            const double angle = std::atan2(static_cast<double>(Impact.Z), static_cast<double>(Impact.X));
            bearing = static_cast<std::uint8_t>(static_cast<std::int32_t>(std::lround(angle * 128.0 / 3.141592653589793)) & 0xFF);
        }
        At(dest, LegacySize + 16) = bearing;
        for (std::size_t i = 0; i < PowerUps.size(); i++)
        {
            W16(Slice(dest, static_cast<std::size_t>(PowerUpOffset) + 2 * i), PackReport(PowerUps[i]));
        }
        At(dest, FrozenFramesOffset) = static_cast<std::uint8_t>(
            TimedEffectTimers::Clamp(StatusEffects.FrozenFrames, TimedEffectTimers::MaxFrozenFrames));
        W16(Slice(dest, BurnDisruptFramesOffset), StatusEffects.PackBurnAndDisrupt());
    }
    PlayerState PlayerState::Read(std::span<const std::uint8_t> src)
    {
        if (src.size() < Size) throw std::out_of_range("PlayerState layout older than protocol 31 is incompatible");
        PlayerState state;
        state.SlotIndex = At(src, 0);
        state.Flags = At(src, 1);
        state.Position = ReadVec(Slice(src, 2));
        state.Speed = ReadVec(Slice(src, 14));
        state.Facing = ReadVec(Slice(src, 26));
        state.Health = R16(Slice(src, 38));
        state.CurrentWeapon = At(src, 40);
        state.Team = At(src, 41);
        state.Points = RI16(Slice(src, 42));
        state.Kills = R16(Slice(src, 44));
        state.Deaths = R16(Slice(src, 46));
        state.SlotGeneration = R16(Slice(src, 48));
        state.LifeId = R16(Slice(src, 50));
        state.DamageEventId = R16(Slice(src, 52));
        state.Damage0 = DamageEvent::Read(Slice(src, 54));
        state.Damage1 = DamageEvent::Read(Slice(src, 54 + DamageEvent::Size));
        state.Damage2 = DamageEvent::Read(Slice(src, 54 + 2 * DamageEvent::Size));
        state.Damage3 = DamageEvent::Read(Slice(src, 54 + 3 * DamageEvent::Size));
        state.WeavelFlags = At(src, LegacySize);
        state.HalfturretHealth = At(src, LegacySize + 1);
        state.HalfturretPosition = ReadVec(Slice(src, LegacySize + 2));
        state.ImpactLaunchLow = At(src, LegacySize + 14);
        const auto height = static_cast<std::int8_t>(At(src, LegacySize + 15));
        if (height != ImpactOffset::None)
        {
            const double angle = static_cast<std::int8_t>(At(src, LegacySize + 16)) * 3.141592653589793 / 128.0;
            state.Impact = ImpactOffset::From(::OpenTK::Mathematics::Vector3(
                static_cast<float>(std::cos(angle)) * ImpactRadius, height / ImpactOffset::Scale,
                static_cast<float>(std::sin(angle)) * ImpactRadius));
        }
        for (std::size_t i = 0; i < state.PowerUps.size(); i++)
        {
            state.PowerUps[i] = UnpackReport(R16(Slice(src, static_cast<std::size_t>(PowerUpOffset) + 2 * i)));
        }
        state.StatusEffects.FrozenFrames = At(src, FrozenFramesOffset);
        state.StatusEffects.UnpackBurnAndDisrupt(R16(Slice(src, BurnDisruptFramesOffset)));

        DamageEvent latest{};
        for (std::int32_t i = DamageHistory - 1; i >= 0; i--)
        {
            const DamageEvent candidate = state.EventAt(i);
            if (candidate.EventId == state.DamageEventId)
            {
                latest = candidate;
                break;
            }
        }
        state.AttackerSlot = latest.EventId == 0 ? static_cast<std::uint8_t>(0xFF) : latest.AttackerSlot;
        state.DamageBeam = latest.EventId == 0 ? static_cast<std::uint8_t>(0xFF) : latest.Beam;
        state.DamageFlags = latest.Flags;
        state.HitDirection = latest.Direction;
        return state;
    }
    void PlayerState::WriteVec(std::span<std::uint8_t> dest, ::OpenTK::Mathematics::Vector3 value)
    {
        WF(Slice(dest, 0), value.X);
        WF(Slice(dest, 4), value.Y);
        WF(Slice(dest, 8), value.Z);
    }
    ::OpenTK::Mathematics::Vector3 PlayerState::ReadVec(std::span<const std::uint8_t> src) { return ::OpenTK::Mathematics::Vector3(RF(Slice(src, 0)), RF(Slice(src, 4)), RF(Slice(src, 8))); }
    void SnapshotHeader::Write(std::span<std::uint8_t> dest) const
    {
        W16(Slice(dest, 13), MatchId);
        W64(Slice(dest, 15), AuthorityEpoch);
        W32(Slice(dest, 0), Frame);
        W32(Slice(dest, 4), Rng1);
        W32(Slice(dest, 8), Rng2);
        At(dest, 12) = PlayerCount;
    }
    SnapshotHeader SnapshotHeader::Read(std::span<const std::uint8_t> src)
    {
        SnapshotHeader packet;
        packet.MatchId = R16(Slice(src, 13));
        packet.AuthorityEpoch = R64(Slice(src, 15));
        packet.Frame = R32(Slice(src, 0));
        packet.Rng1 = R32(Slice(src, 4));
        packet.Rng2 = R32(Slice(src, 8));
        packet.PlayerCount = At(src, 12);
        return packet;
    }
}
