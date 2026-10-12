#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace MphRead::Entities
{
    class PlayerEntity;
}

namespace MphRead::Mods
{
    class WorldEvents final
    {
    public:
        WorldEvents() = delete;
        WorldEvents(const WorldEvents&) = delete;
        WorldEvents& operator=(const WorldEvents&) = delete;

        [[nodiscard]] static bool Watching() noexcept;
        static void Watching(bool value) noexcept;

        static void Reset() noexcept;

        [[nodiscard]] static std::int32_t JumpPadsFor(std::int32_t slot) noexcept;
        [[nodiscard]] static std::int32_t TeleportsFor(std::int32_t slot) noexcept;
        [[nodiscard]] static std::int32_t PickupsFor(std::int32_t slot) noexcept;
        [[nodiscard]] static std::int32_t LastJumpPadId(std::int32_t slot) noexcept;
        [[nodiscard]] static std::int32_t LastTeleporterId(std::int32_t slot) noexcept;

        static void NoteJumpPad(const Entities::PlayerEntity& player, std::int32_t entityId) noexcept;
        static void NoteTeleport(const Entities::PlayerEntity& player, std::int32_t entityId) noexcept;
        static void NotePickup(const Entities::PlayerEntity& player) noexcept;

    private:
        static constexpr std::int32_t SlotCapacity = 8;

        [[nodiscard]] static bool Valid(std::int32_t slot) noexcept;

        static bool _watching;
        static std::array<std::int32_t, static_cast<std::size_t>(SlotCapacity)> _jumpPads;
        static std::array<std::int32_t, static_cast<std::size_t>(SlotCapacity)> _teleports;
        static std::array<std::int32_t, static_cast<std::size_t>(SlotCapacity)> _pickups;
        static std::array<std::int32_t, static_cast<std::size_t>(SlotCapacity)> _lastJumpPadId;
        static std::array<std::int32_t, static_cast<std::size_t>(SlotCapacity)> _lastTeleporterId;
    };
}
