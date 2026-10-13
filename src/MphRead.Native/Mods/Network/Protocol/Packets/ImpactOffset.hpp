#pragma once

#include "Formats/Types.hpp"

#include <cstdint>

namespace MphRead::Mods::Network
{
    struct ImpactOffset
    {
        static constexpr float Scale = 64.0F;
        static constexpr std::int8_t None = -128;
        std::int8_t X = None;
        std::int8_t Y = None;
        std::int8_t Z = None;
        [[nodiscard]] bool Known() const noexcept { return X != None;}
        [[nodiscard]] ::OpenTK::Mathematics::Vector3 Value() const noexcept
        {
            return ::OpenTK::Mathematics::Vector3(X / Scale, Y / Scale, Z / Scale);
       }
        [[nodiscard]] static ImpactOffset From(::OpenTK::Mathematics::Vector3 offset) noexcept;
    };

}
