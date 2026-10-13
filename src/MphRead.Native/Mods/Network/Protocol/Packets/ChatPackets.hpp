#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace MphRead::Mods::Network
{
    struct ChatPacket
    {
        static constexpr std::int32_t MaxNameBytes = 16;
        static constexpr std::int32_t MaxTextBytes = 96;
        static constexpr std::int32_t Size = 1 + 1 + MaxNameBytes + MaxTextBytes;

        static constexpr std::uint8_t KindSay = 0;
        static constexpr std::uint8_t KindTeam = 1;
        static constexpr std::uint8_t KindSystem = 2;

        std::uint8_t Slot = 0;
        std::uint8_t Kind = 0;
        std::optional<std::string> Name{};
        std::optional<std::string> Text{};

        void Write(std::span<std::uint8_t> dest) const;
        [[nodiscard]] static ChatPacket Read(std::span<const std::uint8_t> src);

        static void WriteAscii(std::span<std::uint8_t> dest,
            const std::optional<std::string>& value);
        [[nodiscard]] static std::string ReadAscii(std::span<const std::uint8_t> src);
    };

}
