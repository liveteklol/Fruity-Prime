#include "Mods/Network/Session/RemotePlayerChannel.hpp"

#include "Mods/Network/Protocol/Packets/SessionPackets.hpp"

namespace MphRead::Mods::Network
{
    static_assert(RemotePlayerChannel::SlotCapacity == static_cast<std::size_t>(RosterPacket::MaxSlots));

    bool RemotePlayerChannel::IsValidSlot(std::int32_t slot) noexcept
    {
        return slot >= 0 && slot < static_cast<std::int32_t>(SlotCapacity);
    }

    bool RemotePlayerChannel::HasState(std::int32_t slot) const noexcept
    {
        return IsValidSlot(slot) && _stateValid[static_cast<std::size_t>(slot)];
    }

    const PlayerState& RemotePlayerChannel::StateAt(std::size_t slot) const noexcept
    {
        return _states[slot];
    }

    void RemotePlayerChannel::StoreState(const PlayerState& state) noexcept
    {
        if (!IsValidSlot(state.SlotIndex))
        {
            return;
        }
        const std::size_t slot = state.SlotIndex;
        _states[slot] = state;
        _stateValid[slot] = true;
    }

    void RemotePlayerChannel::InvalidateStates() noexcept
    {
        _stateValid.fill(false);
    }

    bool RemotePlayerChannel::HasIntent(std::int32_t slot) const noexcept
    {
        return IsValidSlot(slot) && _intentValid[static_cast<std::size_t>(slot)];
    }

    const IntentPacket& RemotePlayerChannel::IntentAt(std::size_t slot) const noexcept
    {
        return _intents[slot];
    }

    void RemotePlayerChannel::StoreIntent(std::int32_t slot, const IntentPacket& intent,
        std::uint32_t arrivalFrame) noexcept
    {
        if (!IsValidSlot(slot))
        {
            return;
        }
        const std::size_t index = static_cast<std::size_t>(slot);
        _intents[index] = intent;
        _intentValid[index] = true;
        _intentArrived[index] = arrivalFrame;
    }

    void RemotePlayerChannel::InvalidateIntents() noexcept
    {
        _intentValid.fill(false);
    }

    void RemotePlayerChannel::InvalidateIntent(std::int32_t slot) noexcept
    {
        if (IsValidSlot(slot))
        {
            _intentValid[static_cast<std::size_t>(slot)] = false;
        }
    }

    void RemotePlayerChannel::ClearIntentArrival() noexcept
    {
        _intentArrived.fill(0);
    }

    std::uint32_t RemotePlayerChannel::IntentAge(std::int32_t slot,
        std::uint32_t currentFrame) const noexcept
    {
        if (!IsValidSlot(slot))
        {
            return UINT32_MAX;
        }
        const std::uint32_t arrived = _intentArrived[static_cast<std::size_t>(slot)];
        return arrived == 0 ? UINT32_MAX : currentFrame >= arrived ? currentFrame - arrived : 0U;
    }

    void RemotePlayerChannel::Forget(std::int32_t slot) noexcept
    {
        if (!IsValidSlot(slot))
        {
            return;
        }
        const std::size_t index = static_cast<std::size_t>(slot);
        _states[index] = PlayerState{};
        _stateValid[index] = false;
        _intents[index] = IntentPacket{};
        _intentValid[index] = false;
        _intentArrived[index] = 0;
    }

    void RemotePlayerChannel::Reset() noexcept
    {
        for (std::size_t slot = 0; slot < SlotCapacity; slot++)
        {
            _states[slot] = PlayerState{};
            _intents[slot] = IntentPacket{};
        }
        _stateValid.fill(false);
        _intentValid.fill(false);
        _intentArrived.fill(0);
    }

    RemotePlayerChannel& RemotePlayers() noexcept
    {
        static RemotePlayerChannel channel{};
        return channel;
    }
}
