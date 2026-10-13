#include "Mods/Network/Gameplay/Claims/Hit/HitClaimReplayLedger.hpp"

#include <bit>

namespace MphRead::Mods::Network
{
    bool HitClaimReplayLedger::Newer(std::uint16_t value, std::uint16_t previous) noexcept
    {
        return std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(value - previous)) > 0;
    }

    bool HitClaimReplayLedger::Seen(std::int32_t slot, std::uint16_t id) const noexcept
    {
        if (slot < 0 || slot >= static_cast<std::int32_t>(SlotCapacity))
        {
            return true;
        }
        const auto s = static_cast<std::size_t>(slot);
        const std::uint16_t newest = _newestId[s];
        return id == 0
            || _ids[s][static_cast<std::size_t>(id % Capacity)] == id
            || (newest != 0 && !Newer(id, newest)
                && static_cast<std::uint16_t>(newest - id) >= Capacity);
    }

    std::optional<std::uint8_t> HitClaimReplayLedger::Result(std::int32_t slot, std::uint16_t id) const noexcept
    {
        if (slot < 0 || slot >= static_cast<std::int32_t>(SlotCapacity))
        {
            return std::nullopt;
        }
        const auto s = static_cast<std::size_t>(slot);
        const auto at = static_cast<std::size_t>(id % Capacity);
        if (_ids[s][at] != id)
        {
            return std::nullopt;
        }
        return _results[s][at];
    }

    ::MphRead::BeamType HitClaimReplayLedger::Beam(std::int32_t slot, std::uint16_t id) const noexcept
    {
        if (slot < 0 || slot >= static_cast<std::int32_t>(SlotCapacity))
        {
            return ::MphRead::BeamType::None;
        }
        return _beams[static_cast<std::size_t>(slot)][static_cast<std::size_t>(id % Capacity)];
    }

    ShotKey HitClaimReplayLedger::Key(std::int32_t slot, std::uint16_t id) const noexcept
    {
        if (slot < 0 || slot >= static_cast<std::int32_t>(SlotCapacity))
        {
            return {};
        }
        return _keys[static_cast<std::size_t>(slot)][static_cast<std::size_t>(id % Capacity)];
    }

    void HitClaimReplayLedger::Begin(std::int32_t slot, std::uint16_t id, std::uint8_t result,
        ::MphRead::BeamType beam, ShotKey key) noexcept
    {
        if (slot < 0 || slot >= static_cast<std::int32_t>(SlotCapacity))
        {
            return;
        }
        const auto s = static_cast<std::size_t>(slot);
        const auto at = static_cast<std::size_t>(id % Capacity);
        _beams[s][at] = beam;
        _keys[s][at] = key;
        Remember(slot, id, result);
    }

    void HitClaimReplayLedger::Remember(std::int32_t slot, std::uint16_t id, std::uint8_t result) noexcept
    {
        if (slot < 0 || slot >= static_cast<std::int32_t>(SlotCapacity))
        {
            return;
        }
        const auto s = static_cast<std::size_t>(slot);
        if (_newestId[s] == 0 || Newer(id, _newestId[s]))
        {
            _newestId[s] = id;
        }
        const auto at = static_cast<std::size_t>(id % Capacity);
        _ids[s][at] = id;
        _results[s][at] = result;
    }

    void HitClaimReplayLedger::ForgetSlot(std::int32_t slot) noexcept
    {
        if (slot < 0 || slot >= static_cast<std::int32_t>(SlotCapacity))
        {
            return;
        }
        const auto s = static_cast<std::size_t>(slot);
        _ids[s].fill(0);
        _results[s].fill(0);
        _newestId[s] = 0;
    }

    void HitClaimReplayLedger::Reset() noexcept
    {
        _ids = {};
        _results = {};
        _newestId = {};
    }
}
