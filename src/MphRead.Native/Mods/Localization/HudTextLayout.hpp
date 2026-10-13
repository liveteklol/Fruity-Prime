#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace MphRead::Mods::Localization::HudTextLayout
{
    struct Glyph
    {
        std::int32_t Value;
        std::int32_t Original;
    };

    // HUD string tables store packed two-byte glyphs as two UTF-16 code units.
    // The caller's end is the current line boundary, so a lead byte at the end
    // of a line cannot consume the first code unit of the next line.
    [[nodiscard]] inline Glyph ReadForwardGlyph(
        std::span<const char16_t> text,
        std::int32_t& index,
        std::int32_t end) noexcept
    {
        const std::int32_t first = text[static_cast<std::size_t>(index)];
        std::int32_t value = first;
        if ((first & 0x80) != 0 && index + 1 < end)
        {
            ++index;
            value = static_cast<std::int32_t>(text[static_cast<std::size_t>(index)]) & 0x3F
                | ((first & 0x1F) << 6);
        }
        return Glyph{value, first};
    }
}
