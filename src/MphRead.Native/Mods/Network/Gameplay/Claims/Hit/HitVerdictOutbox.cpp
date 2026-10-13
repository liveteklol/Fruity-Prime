#include "Mods/Network/Gameplay/Claims/Hit/HitVerdictOutbox.hpp"

#include <algorithm>

namespace MphRead::Mods::Network
{
    bool HitVerdictOutbox::Enqueue(std::int32_t slot, std::uint16_t id, std::uint8_t result) noexcept
    {
        if (slot < 0 || slot >= static_cast<std::int32_t>(SlotCapacity))
        {
            return false;
        }

        const std::size_t index = static_cast<std::size_t>(slot);
        std::size_t& count = _counts[index];
        if (count >= BatchCapacity)
        {
            return false;
        }

        _entries[index][count++] = {id, result};
        return true;
    }

    void HitVerdictOutbox::Flush()
    {
        if (!_sink)
        {
            Clear();
            return;
        }

        std::array<Entry, BatchCapacity> scratch{};
        for (std::size_t slot = 0; slot < SlotCapacity; slot++)
        {
            const std::size_t count = _counts[slot];
            if (count == 0)
            {
                continue;
            }

            std::copy_n(_entries[slot].begin(), count, scratch.begin());
            _counts[slot] = 0;
            _sink(static_cast<std::int32_t>(slot), std::span<const Entry>(scratch.data(), count));
        }
    }

    void HitVerdictOutbox::Clear() noexcept
    {
        _counts.fill(0);
    }

    void HitVerdictOutbox::ClearSlot(std::int32_t slot) noexcept
    {
        if (slot >= 0 && slot < static_cast<std::int32_t>(SlotCapacity))
        {
            _counts[static_cast<std::size_t>(slot)] = 0;
        }
    }

    std::size_t HitVerdictOutbox::PendingCount(std::int32_t slot) const noexcept
    {
        if (slot < 0 || slot >= static_cast<std::int32_t>(SlotCapacity))
        {
            return 0;
        }
        return _counts[static_cast<std::size_t>(slot)];
    }
}
