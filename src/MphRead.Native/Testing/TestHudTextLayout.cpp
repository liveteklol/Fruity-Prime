#include "Mods/Localization/HudTextLayout.hpp"
#include "Mods/Localization/HudMessageLanguage.hpp"

#include <array>
#include <iostream>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace
{
    using MphRead::Mods::Localization::HudTextLayout::Glyph;
    using MphRead::Mods::Localization::HudMessageLanguage::CaptureSourceLanguage;
    using MphRead::Mods::Localization::HudMessageLanguage::IsStaleLocalizedMessage;
    using MphRead::Mods::Localization::HudTextLayout::ReadForwardGlyph;

    bool Check(bool condition, std::string_view name)
    {
        if (!condition)
        {
            std::cerr << "FAIL " << name << '\n';
            return false;
        }
        return true;
    }
}

int main()
{
    bool passed = true;

    constexpr std::array<char16_t, 4> text{u'A', 0x0082, 0x00A0, u'B'};
    std::vector<Glyph> glyphs;
    for (std::int32_t i = 0; i < static_cast<std::int32_t>(text.size()); ++i)
    {
        glyphs.push_back(ReadForwardGlyph(text, i, static_cast<std::int32_t>(text.size())));
    }
    passed &= Check(glyphs.size() == 3
        && glyphs[0].Value == u'A'
        && glyphs[1].Value == 0x00A0
        && glyphs[1].Original == 0x0082
        && glyphs[2].Value == u'B',
        "forward traversal decodes a Japanese packed glyph and advances past both bytes");

    constexpr std::array<char16_t, 3> line{0x0082, u'\n', 0x00A0};
    std::int32_t index = 0;
    const Glyph lineEnd = ReadForwardGlyph(line, index, 1);
    passed &= Check(lineEnd.Value == 0x0082 && index == 0,
        "a glyph lead at the line boundary does not consume the next line");

    constexpr std::array<char16_t, 1> trailingLead{0x0082};
    index = 0;
    const Glyph malformed = ReadForwardGlyph(trailingLead, index, 1);
    passed &= Check(malformed.Value == 0x0082 && index == 0,
        "a trailing multibyte lead remains a bounded single-unit glyph");

    passed &= Check(IsStaleLocalizedMessage(MphRead::Language::Japanese, MphRead::Language::English)
        && !IsStaleLocalizedMessage(MphRead::Language::Japanese, MphRead::Language::Japanese)
        && !IsStaleLocalizedMessage(std::nullopt, MphRead::Language::English),
        "only a queued localized message from the previous language is discarded");
    passed &= Check(
        CaptureSourceLanguage(std::nullopt, MphRead::Language::Japanese) == MphRead::Language::Japanese
        && CaptureSourceLanguage(MphRead::Language::English, MphRead::Language::Japanese) == MphRead::Language::English,
        "queued text without an explicit locale captures the current language");

    return passed ? 0 : 1;
}
