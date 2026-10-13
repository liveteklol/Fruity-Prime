#pragma once

#include "Formats/Enums.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace MphRead::Mods::Network
{
    // The three timed power-ups a battle map hands out. In a networked match
    // the authority grants them, records when each ran (PowerUpLedger) and
    // says so in every snapshot; a client only predicts its own.
    enum class PowerUp : std::uint8_t
    {
        DoubleDamage = 0,
        Cloak = 1,
        Deathalt = 2
    };

    inline constexpr std::size_t PowerUpCount = 3;

    // How long a picked-up power-up runs: the game's 900 frames at 30 Hz.
    inline constexpr std::uint16_t PowerUpFrames = 900 * 2;

    [[nodiscard]] constexpr std::optional<PowerUp> PowerUpOf(::MphRead::ItemType type) noexcept
    {
        switch (type)
        {
        case ::MphRead::ItemType::DoubleDamage:
            return PowerUp::DoubleDamage;
        case ::MphRead::ItemType::Cloak:
            return PowerUp::Cloak;
        case ::MphRead::ItemType::Deathalt:
            return PowerUp::Deathalt;
        default:
            return std::nullopt;
        }
    }

    [[nodiscard]] constexpr ::MphRead::ItemType ItemTypeOf(PowerUp kind) noexcept
    {
        switch (kind)
        {
        case PowerUp::DoubleDamage:
            return ::MphRead::ItemType::DoubleDamage;
        case PowerUp::Cloak:
            return ::MphRead::ItemType::Cloak;
        case PowerUp::Deathalt:
        default:
            return ::MphRead::ItemType::Deathalt;
        }
    }

    // A grant's sequence travels in five bits (PowerUpSync): 1 to 31, then 1
    // again. 0 is "nothing granted yet".
    inline constexpr std::uint8_t MaxGrantSequence = 31;

    [[nodiscard]] constexpr std::uint8_t NextGrantSequence(std::uint8_t sequence) noexcept
    {
        return sequence >= MaxGrantSequence ? std::uint8_t{ 1 } : static_cast<std::uint8_t>(sequence + 1U);
    }

    [[nodiscard]] constexpr std::string_view NameOf(PowerUp kind) noexcept
    {
        switch (kind)
        {
        case PowerUp::DoubleDamage:
            return "double damage";
        case PowerUp::Cloak:
            return "cloak";
        case PowerUp::Deathalt:
        default:
            return "Death Alt";
        }
    }

    [[nodiscard]] constexpr std::size_t IndexOf(PowerUp kind) noexcept
    {
        return static_cast<std::size_t>(kind);
    }
}
