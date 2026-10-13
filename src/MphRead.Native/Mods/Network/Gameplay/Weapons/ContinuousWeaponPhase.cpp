#include "Mods/Network/Gameplay/Weapons/ContinuousWeaponPhase.hpp"

#include "NativeRuntime/System/Exceptions.hpp"

#include <algorithm>

namespace MphRead::Mods::Network
{
    namespace
    {
        ContinuousWeaponPhase SharedPhase(8);
    }

    ContinuousWeaponPhase::ContinuousWeaponPhase(std::int32_t slots)
    {
        if (slots < 0)
        {
            throw System::OverflowException();
        }
        _clocks.resize(static_cast<std::size_t>(slots));
    }

    std::int32_t ContinuousWeaponPhase::Amount(
        std::int32_t amount, std::uint64_t phase, bool damage) noexcept
    {
        if (phase % 2 != 0)
        {
            return 0;
        }
        const std::uint64_t bits = static_cast<std::uint64_t>(amount & 31);
        const std::uint64_t fraction = (bits * (phase / 2)) & 31;
        return amount / 32
            + (bits != 0 && (damage ? fraction >= 32 - bits : fraction > 32 - bits) ? 1 : 0);
    }

    void ContinuousWeaponPhase::Reset()
    {
        std::fill(_clocks.begin(), _clocks.end(), Clock{});
    }

    void ContinuousWeaponPhase::ResetSlot(std::int32_t slot)
    {
        if (static_cast<std::uint32_t>(slot) < static_cast<std::uint32_t>(_clocks.size()))
        {
            _clocks[static_cast<std::size_t>(slot)] = Clock{};
        }
    }

    void ContinuousWeaponPhase::Observe(std::int32_t slot, std::uint64_t sceneFrame,
        bool continuousHeld, bool intentFresh)
    {
        if (!continuousHeld || !intentFresh)
        {
            ResetSlot(slot);
            return;
        }
        if (static_cast<std::uint32_t>(slot) < static_cast<std::uint32_t>(_clocks.size()))
        {
            Advance(_clocks[static_cast<std::size_t>(slot)], sceneFrame);
        }
    }

    void ContinuousWeaponPhase::Advance(Clock& clock, std::uint64_t sceneFrame) noexcept
    {
        if (!clock.Valid)
        {
            return;
        }
        if (sceneFrame < clock.SceneFrame)
        {
            clock = Clock{};
        }
        else if (sceneFrame > clock.SceneFrame)
        {
            clock.Phase++;
            clock.SceneFrame = sceneFrame;
        }
    }

    std::uint64_t ContinuousWeaponPhase::Resolve(std::int32_t slot, std::uint64_t sceneFrame,
        bool networked, bool localOwner, std::uint32_t netFrame, bool intentValid,
        std::uint32_t intentFrame, std::uint32_t intentAge, bool& shared)
    {
        if (networked && localOwner && netFrame != 0)
        {
            shared = true;
            return netFrame;
        }
        if (networked && !localOwner
            && static_cast<std::uint32_t>(slot) < static_cast<std::uint32_t>(_clocks.size())
            && intentValid && intentFrame != 0 && intentAge <= MaxIntentAge)
        {
            Clock& clock = _clocks[static_cast<std::size_t>(slot)];
            Advance(clock, sceneFrame);
            if (!clock.Valid)
            {
                clock.Phase = static_cast<std::uint64_t>(intentFrame) + intentAge;
                clock.SceneFrame = sceneFrame;
                clock.Valid = true;
            }
            // Never re-anchor to a later packet: its arrival jitter can repeat
            // or skip a parity even when the trigger never lifted.
            shared = true;
            return clock.Phase;
        }
        ResetSlot(slot);
        shared = false;
        return sceneFrame;
    }

    ContinuousWeaponPhase& SharedContinuousWeaponPhase() noexcept
    {
        return SharedPhase;
    }
}
