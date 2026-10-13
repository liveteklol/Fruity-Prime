#include "Mods/Network/Protocol/Codec/WirePrimitives.hpp"
#include "Mods/Network/Protocol/Packets/RealtimePackets.hpp"

namespace MphRead::Mods::Network
{
    using namespace Protocol::Wire;

    void IntentPacket::Write(std::span<std::uint8_t> dest) const
    {
        W16(Slice(dest, 74), MatchId);
        W64(Slice(dest, 76), AuthorityEpoch);
        W16(Slice(dest, 84), SlotGeneration);
        W16(Slice(dest, 86), LifeId);
        W32(Slice(dest, 0), Frame);
        W32(Slice(dest, 4), static_cast<std::uint32_t>(Buttons));
        WF(Slice(dest, 8), Aim.X);
        WF(Slice(dest, 12), Aim.Y);
        WF(Slice(dest, 16), Aim.Z);
        At(dest, 20) = WeaponSelect;
        for (std::size_t index = 0; index < static_cast<std::size_t>(PressHistory); index++)
        {
            std::uint32_t value = 0;
            if (Presses != nullptr && index < Presses->size())
            {
                value = (*Presses)[index];
            }
            W32(Slice(dest, 21 + index * 4), value);
        }
        constexpr std::size_t at = 21 + PressHistory * 4;
        WF(Slice(dest, at), Position.X);
        WF(Slice(dest, at + 4), Position.Y);
        WF(Slice(dest, at + 8), Position.Z);
        W16(Slice(dest, at + 12), AmmoUa);
        W16(Slice(dest, at + 14), AmmoMissiles);
        W32(Slice(dest, at + 16), AckFrame);
        At(dest, at + 20) = AckSubFrame;
        if (dest.size() >= static_cast<std::size_t>(Size + StateSize))
        {
            dest[Size] = ChargeLevel;
            dest[Size + 1] = BoostDamage;
            dest[Size + 2] = ShotFlags;
            dest[Size + 3] = 0;
        }
        if (dest.size() >= static_cast<std::size_t>(Size + StateSize + LegacyTouchSize))
        {
            dest[Size + 3] = static_cast<std::uint8_t>(TouchFlags & ~(TouchSamplePresent | TouchSecondStep));
            W16(Slice(dest, static_cast<std::size_t>(Size + StateSize)), static_cast<std::uint16_t>(TouchDelta4X));
            W16(Slice(dest, static_cast<std::size_t>(Size + StateSize + 2)), static_cast<std::uint16_t>(TouchDelta4Y));
        }
        if (dest.size() >= static_cast<std::size_t>(FullSize))
        {
            dest[Size + 3] = TouchFlags;
            W32(Slice(dest, static_cast<std::size_t>(Size + StateSize + LegacyTouchSize)), TouchSampleSequence);
        }
        if (dest.size() >= WireSize())
        {
            WriteSections(dest);
        }
    }
    void IntentPacket::ReadSections(std::span<const std::uint8_t> src)
    {
        std::size_t at = static_cast<std::size_t>(FullSize);
        const std::uint8_t sections = At(src, at++);
        const auto fits = [&src, &at](std::size_t bytes) { return at + bytes <= src.size(); };
        if ((sections & SectionRay) != 0)
        {
            if (!fits(static_cast<std::size_t>(ShotSize)))
            {
                return;
            }
            ShotOrigin = ::OpenTK::Mathematics::Vector3(RF(Slice(src, at)), RF(Slice(src, at + 4)), RF(Slice(src, at + 8)));
            ShotDirection = ::OpenTK::Mathematics::Vector3(
                RF(Slice(src, at + 12)), RF(Slice(src, at + 16)), RF(Slice(src, at + 20)));
            HasShot = ShotRayValidation::IsFinite(ShotOrigin)
                && ShotRayValidation::IsValidDirection(ShotDirection);
            at += static_cast<std::size_t>(ShotSize);
        }
        if ((sections & SectionShots) != 0)
        {
            if (!fits(1))
            {
                return;
            }
            const std::uint8_t count = std::min<std::uint8_t>(At(src, at), ShotHistoryCount);
            if (!fits(1 + static_cast<std::size_t>(ShotEventSize) * count))
            {
                return;
            }
            ++at;
            for (std::size_t i = 0; i < count; ++i)
            {
                ShotEvent& event = ShotHistory[i];
                event.Sequence = R32(Slice(src, at));
                event.Frame = R32(Slice(src, at + 4));
                event.AckFrame = R32(Slice(src, at + 8));
                event.WeaponId = At(src, at + 12);
                event.Charge = At(src, at + 13);
                event.Origin = ::OpenTK::Mathematics::Vector3(
                    RF(Slice(src, at + 16)), RF(Slice(src, at + 20)), RF(Slice(src, at + 24)));
                event.Direction = ::OpenTK::Mathematics::Vector3(
                    RF(Slice(src, at + 28)), RF(Slice(src, at + 32)), RF(Slice(src, at + 36)));
                if (!ShotRayValidation::IsFinite(event.Origin)
                    || !ShotRayValidation::IsValidDirection(event.Direction))
                {
                    event.Origin = event.Direction = ::OpenTK::Mathematics::Vector3::Zero;
                }
                at += static_cast<std::size_t>(ShotEventSize);
            }
            ShotHistoryLength = count;
        }
        if ((sections & SectionBombs) != 0)
        {
            if (!fits(2))
            {
                return;
            }
            const std::uint8_t count = std::min<std::uint8_t>(At(src, at), BombCount);
            const std::uint8_t gone = At(src, at + 1);
            if (!fits(2 + static_cast<std::size_t>(BombSize) * count))
            {
                return;
            }
            at += 2;
            for (std::size_t i = 0; i < count; ++i)
            {
                Bombs[i].Sequence = R32(Slice(src, at));
                Bombs[i].Position = ::OpenTK::Mathematics::Vector3(
                    RF(Slice(src, at + 4)), RF(Slice(src, at + 8)), RF(Slice(src, at + 12)));
                Bombs[i].Gone = (gone & (1U << i)) != 0;
                at += static_cast<std::size_t>(BombSize);
            }
            BombsLength = count;
            HasBombs = true;
        }
    }

    std::size_t IntentPacket::WireSize() const noexcept
    {
        std::size_t size = static_cast<std::size_t>(FullSize) + 1;
        if (HasShot)
        {
            size += static_cast<std::size_t>(ShotSize);
        }
        if (const std::uint8_t shots = std::min<std::uint8_t>(ShotHistoryLength, ShotHistoryCount); shots > 0)
        {
            size += 1 + static_cast<std::size_t>(ShotEventSize) * shots;
        }
        if (HasBombs)
        {
            size += 2 + static_cast<std::size_t>(BombSize) * std::min<std::uint8_t>(BombsLength, BombCount);
        }
        return size;
    }
    void IntentPacket::WriteSections(std::span<std::uint8_t> dest) const
    {
        const std::uint8_t shots = std::min<std::uint8_t>(ShotHistoryLength, ShotHistoryCount);
        const std::uint8_t bombs = HasBombs ? std::min<std::uint8_t>(BombsLength, BombCount) : 0;
        std::size_t at = static_cast<std::size_t>(FullSize);
        At(dest, at++) = static_cast<std::uint8_t>((HasShot ? SectionRay : 0) | (shots > 0 ? SectionShots : 0)
            | (HasBombs ? SectionBombs : 0));
        if (HasShot)
        {
            WF(Slice(dest, at), ShotOrigin.X);
            WF(Slice(dest, at + 4), ShotOrigin.Y);
            WF(Slice(dest, at + 8), ShotOrigin.Z);
            WF(Slice(dest, at + 12), ShotDirection.X);
            WF(Slice(dest, at + 16), ShotDirection.Y);
            WF(Slice(dest, at + 20), ShotDirection.Z);
            at += static_cast<std::size_t>(ShotSize);
        }
        if (shots > 0)
        {
            At(dest, at++) = shots;
            for (std::size_t i = 0; i < shots; ++i)
            {
                const ShotEvent& event = ShotHistory[i];
                W32(Slice(dest, at), event.Sequence);
                W32(Slice(dest, at + 4), event.Frame);
                W32(Slice(dest, at + 8), event.AckFrame);
                At(dest, at + 12) = event.WeaponId;
                At(dest, at + 13) = event.Charge;
                At(dest, at + 14) = 0;
                At(dest, at + 15) = 0;
                WF(Slice(dest, at + 16), event.Origin.X);
                WF(Slice(dest, at + 20), event.Origin.Y);
                WF(Slice(dest, at + 24), event.Origin.Z);
                WF(Slice(dest, at + 28), event.Direction.X);
                WF(Slice(dest, at + 32), event.Direction.Y);
                WF(Slice(dest, at + 36), event.Direction.Z);
                at += static_cast<std::size_t>(ShotEventSize);
            }
        }
        if (HasBombs)
        {
            std::uint8_t gone = 0;
            for (std::size_t i = 0; i < bombs; ++i)
            {
                gone = static_cast<std::uint8_t>(gone | (Bombs[i].Gone ? 1U << i : 0U));
            }
            At(dest, at) = bombs;
            At(dest, at + 1) = gone;
            at += 2;
            for (std::size_t i = 0; i < bombs; ++i)
            {
                W32(Slice(dest, at), Bombs[i].Sequence);
                WF(Slice(dest, at + 4), Bombs[i].Position.X);
                WF(Slice(dest, at + 8), Bombs[i].Position.Y);
                WF(Slice(dest, at + 12), Bombs[i].Position.Z);
                at += static_cast<std::size_t>(BombSize);
            }
        }
    }
    void IntentPacket::SetTouchReport(const ::MphRead::Mods::Input::NativeTouchState::Reported& touch) noexcept
    {
        TouchFlags = static_cast<std::uint8_t>(TouchPresent | TouchSamplePresent
            | (touch.Down ? TouchDown : 0) | (touch.Continued ? TouchContinued : 0)
            | (touch.SecondStep ? TouchSecondStep : 0));
        TouchDelta4X = touch.Delta4X;
        TouchDelta4Y = touch.Delta4Y;
        TouchSampleSequence = touch.SampleSequence;
    }

    ::MphRead::Mods::Input::NativeTouchState::Reported IntentPacket::TouchReport() const noexcept
    {
        return {HasTouch() && (TouchFlags & TouchDown) != 0,
            HasTouch() && (TouchFlags & TouchContinued) != 0, TouchDelta4X, TouchDelta4Y,
            HasTouchSample() ? TouchSampleSequence : Frame / 2,
            HasTouchSample() ? (TouchFlags & TouchSecondStep) != 0 : (Frame & 1U) != 0};
    }

    IntentPacket IntentPacket::Read(std::span<const std::uint8_t> src)
    {
        auto presses = std::make_shared<std::vector<std::uint32_t>>(PressHistory);
        for (std::size_t index = 0; index < static_cast<std::size_t>(PressHistory); index++)
        {
            (*presses)[index] = R32(Slice(src, 21 + index * 4));
        }
        IntentPacket packet;
        packet.MatchId = R16(Slice(src, 74));
        packet.AuthorityEpoch = R64(Slice(src, 76));
        packet.SlotGeneration = R16(Slice(src, 84));
        packet.LifeId = R16(Slice(src, 86));
        packet.Frame = R32(Slice(src, 0));
        packet.Buttons = static_cast<IntentButtons>(R32(Slice(src, 4)));
        packet.Aim = ::OpenTK::Mathematics::Vector3(RF(Slice(src, 8)), RF(Slice(src, 12)), RF(Slice(src, 16)));
        packet.WeaponSelect = At(src, 20);
        packet.Presses = std::move(presses);
        packet.Position = ::OpenTK::Mathematics::Vector3(RF(Slice(src, 21 + PressHistory * 4)), RF(Slice(src, 25 + PressHistory * 4)), RF(Slice(src, 29 + PressHistory * 4)));
        packet.AmmoUa = R16(Slice(src, 33 + PressHistory * 4));
        packet.AmmoMissiles = R16(Slice(src, 35 + PressHistory * 4));
        packet.AckFrame = R32(Slice(src, 37 + PressHistory * 4));
        packet.AckSubFrame = At(src, 41 + PressHistory * 4);
        const bool full = src.size() >= static_cast<std::size_t>(Size + StateSize);
        packet.HasState = full;
        if (src.size() >= static_cast<std::size_t>(Size + StateSize + LegacyTouchSize))
        {
            packet.TouchFlags = static_cast<std::uint8_t>(src[Size + 3] & ~(TouchSamplePresent | TouchSecondStep));
            packet.TouchDelta4X = static_cast<std::int16_t>(R16(Slice(src, static_cast<std::size_t>(Size + StateSize))));
            packet.TouchDelta4Y = static_cast<std::int16_t>(R16(Slice(src, static_cast<std::size_t>(Size + StateSize + 2))));
        }
        if (src.size() >= static_cast<std::size_t>(FullSize))
        {
            packet.TouchFlags = src[Size + 3];
            packet.TouchSampleSequence = R32(Slice(src, static_cast<std::size_t>(Size + StateSize + LegacyTouchSize)));
        }
        packet.ChargeLevel = full ? src[Size] : static_cast<std::uint8_t>(0);
        packet.BoostDamage = full ? src[Size + 1] : static_cast<std::uint8_t>(0);
        packet.ShotFlags = full ? src[Size + 2] : static_cast<std::uint8_t>(0);
        if (src.size() > static_cast<std::size_t>(FullSize))
        {
            packet.ReadSections(src);
        }
        return packet;
    }
}
