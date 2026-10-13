#pragma once

#include <cstdint>
#include <span>

namespace MphRead::Mods::Network
{
    class NetMatchTimeSync final
    {
    public:
        NetMatchTimeSync() = delete;

        // PlayerEntity.SlotCapacity * sizeof(float) * 2.
        static constexpr std::int32_t Size = 8 * 4 * 2;

        static void Write(std::span<std::uint8_t> dest);
        [[nodiscard]] static bool Validate(std::span<const std::uint8_t> src);
        static void Receive(std::span<const std::uint8_t> src);
    };
}
