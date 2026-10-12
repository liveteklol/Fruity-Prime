#include "WorldEvents.hpp"

#include "../Entities/Players/PlayerEntity.hpp"
#include "../NativeRuntime/System/Managed.hpp"

#include <bit>

using ::MphRead::NativeRuntime::IncrementInPlace;

namespace
{
}

namespace MphRead::Mods
{
    bool WorldEvents::_watching = false;
    std::array<std::int32_t, static_cast<std::size_t>(WorldEvents::SlotCapacity)> WorldEvents::_jumpPads{};
    std::array<std::int32_t, static_cast<std::size_t>(WorldEvents::SlotCapacity)> WorldEvents::_teleports{};
    std::array<std::int32_t, static_cast<std::size_t>(WorldEvents::SlotCapacity)> WorldEvents::_pickups{};
    std::array<std::int32_t, static_cast<std::size_t>(WorldEvents::SlotCapacity)> WorldEvents::_lastJumpPadId{};
    std::array<std::int32_t, static_cast<std::size_t>(WorldEvents::SlotCapacity)> WorldEvents::_lastTeleporterId{};

    bool WorldEvents::Watching() noexcept
    {
        return _watching;
    }

    void WorldEvents::Watching(bool value) noexcept
    {
        _watching = value;
    }

    void WorldEvents::Reset() noexcept
    {
        for (std::int32_t i = 0; i < SlotCapacity; i++)
        {
            const std::size_t index = static_cast<std::size_t>(i);
            _jumpPads[index] = 0;
            _teleports[index] = 0;
            _pickups[index] = 0;
            _lastJumpPadId[index] = -1;
            _lastTeleporterId[index] = -1;
        }
    }

    std::int32_t WorldEvents::JumpPadsFor(std::int32_t slot) noexcept
    {
        return Valid(slot) ? _jumpPads[static_cast<std::size_t>(slot)] : 0;
    }

    std::int32_t WorldEvents::TeleportsFor(std::int32_t slot) noexcept
    {
        return Valid(slot) ? _teleports[static_cast<std::size_t>(slot)] : 0;
    }

    std::int32_t WorldEvents::PickupsFor(std::int32_t slot) noexcept
    {
        return Valid(slot) ? _pickups[static_cast<std::size_t>(slot)] : 0;
    }

    void WorldEvents::NotePickup(const Entities::PlayerEntity& player) noexcept
    {
        if (Watching() && Valid(player.SlotIndex()))
        {
            IncrementInPlace(_pickups[static_cast<std::size_t>(player.SlotIndex())]);
        }
    }

    std::int32_t WorldEvents::LastJumpPadId(std::int32_t slot) noexcept
    {
        return Valid(slot) ? _lastJumpPadId[static_cast<std::size_t>(slot)] : -1;
    }

    std::int32_t WorldEvents::LastTeleporterId(std::int32_t slot) noexcept
    {
        return Valid(slot) ? _lastTeleporterId[static_cast<std::size_t>(slot)] : -1;
    }

    void WorldEvents::NoteJumpPad(const Entities::PlayerEntity& player, std::int32_t entityId) noexcept
    {
        if (!Watching() || !Valid((player).SlotIndex()))
        {
            return;
        }
        const std::size_t countIndex = static_cast<std::size_t>((player).SlotIndex());
        IncrementInPlace(_jumpPads[countIndex]);
        const std::size_t idIndex = static_cast<std::size_t>((player).SlotIndex());
        _lastJumpPadId[idIndex] = entityId;
    }

    void WorldEvents::NoteTeleport(const Entities::PlayerEntity& player, std::int32_t entityId) noexcept
    {
        if (!Watching() || !Valid((player).SlotIndex()))
        {
            return;
        }
        const std::size_t countIndex = static_cast<std::size_t>((player).SlotIndex());
        IncrementInPlace(_teleports[countIndex]);
        const std::size_t idIndex = static_cast<std::size_t>((player).SlotIndex());
        _lastTeleporterId[idIndex] = entityId;
    }

    bool WorldEvents::Valid(std::int32_t slot) noexcept
    {
        return slot >= 0 && slot < SlotCapacity;
    }
}
