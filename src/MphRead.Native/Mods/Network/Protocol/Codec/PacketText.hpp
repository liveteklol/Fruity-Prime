#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace MphRead::Mods::Network
{
    class PacketText final
    {
    public:
        static void Write(std::span<std::uint8_t> dest, const std::optional<std::string>& value);
        [[nodiscard]] static std::string Read(std::span<const std::uint8_t> src);

        PacketText() = delete;
    };
}
