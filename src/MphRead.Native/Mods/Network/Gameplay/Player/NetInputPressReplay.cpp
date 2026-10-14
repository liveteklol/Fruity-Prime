#include "Mods/Network/Gameplay/Player/NetInputPressReplay.hpp"
#include "Mods/Network/Session/NetLifecycleTracker.hpp"

#include <algorithm>
#include <cstddef>
#include <vector>

namespace MphRead::Mods::Network
{
    std::array<NetInputPressReplay::Cursor, NetworkSlotCapacity> NetInputPressReplay::_cursors{};

    NetInputPressReplayResult NetInputPressReplay::Consume(std::int32_t slot, const IntentPacket& intent) noexcept
    {
        NetInputPressReplayResult result{};
        if (slot < 0 || slot >= static_cast<std::int32_t>(_cursors.size()) || intent.Presses == nullptr)
        {
            return result;
        }

        const std::vector<std::uint32_t>& presses = *intent.Presses;
        if (presses.empty())
        {
            return result;
        }

        Cursor& cursor = _cursors[static_cast<std::size_t>(slot)];
        const std::uint32_t historyLength = static_cast<std::uint32_t>(std::min(
            presses.size(), static_cast<std::size_t>(IntentPacket::PressHistory)));
        if (!cursor.Initialized
            || cursor.SlotGeneration != intent.SlotGeneration
            || cursor.LifeId != intent.LifeId)
        {
            // The caller has already validated this intent's slot generation
            // and life. Its bounded history is therefore the unconsumed input
            // window for this life; establish the cursor just before it so the
            // first packet cannot silently lose a press edge.
            cursor.LastFrame = intent.Frame - historyLength;
            cursor.SlotGeneration = intent.SlotGeneration;
            cursor.LifeId = intent.LifeId;
            cursor.Initialized = true;
        }

        bool hasShootAge = false;
        for (std::uint32_t age = historyLength; age > 0; --age)
        {
            const std::uint32_t index = age - 1;
            const std::uint32_t frame = intent.Frame - index;
            if (!NetLifecycleTracker::Newer(frame, cursor.LastFrame))
            {
                continue;
            }

            const IntentButtons press = static_cast<IntentButtons>(presses[static_cast<std::size_t>(index)]);
            result.Pressed |= press;
            if (!hasShootAge && (press & IntentButtons::Shoot) != IntentButtons::None)
            {
                // Preserve the previous recovery policy: when several shot
                // edges arrive together, resolve from the oldest missed edge.
                result.ShootAge = static_cast<std::int32_t>(index);
                hasShootAge = true;
            }
            if ((press & IntentButtons::AltAttack) != IntentButtons::None)
            {
                result.AltAttackAge = static_cast<std::int32_t>(index);
            }
        }

        if (NetLifecycleTracker::Newer(intent.Frame, cursor.LastFrame))
        {
            cursor.LastFrame = intent.Frame;
        }
        return result;
    }

    void NetInputPressReplay::Discard(std::int32_t slot, const IntentPacket& intent) noexcept
    {
        if (slot < 0 || slot >= static_cast<std::int32_t>(_cursors.size()))
        {
            return;
        }

        Cursor& cursor = _cursors[static_cast<std::size_t>(slot)];
        if (!cursor.Initialized
            || cursor.SlotGeneration != intent.SlotGeneration
            || cursor.LifeId != intent.LifeId)
        {
            cursor = Cursor{intent.Frame, intent.SlotGeneration, intent.LifeId, true};
            return;
        }
        if (NetLifecycleTracker::Newer(intent.Frame, cursor.LastFrame))
        {
            cursor.LastFrame = intent.Frame;
        }
    }

    void NetInputPressReplay::ForgetSlot(std::int32_t slot) noexcept
    {
        if (slot < 0 || slot >= static_cast<std::int32_t>(_cursors.size()))
        {
            return;
        }
        _cursors[static_cast<std::size_t>(slot)] = Cursor{};
    }

    void NetInputPressReplay::Reset() noexcept
    {
        _cursors = {};
    }
}
