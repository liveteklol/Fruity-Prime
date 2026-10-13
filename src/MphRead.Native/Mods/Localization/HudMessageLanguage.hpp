#pragma once

#include "Formats/Enums.hpp"

#include <optional>

namespace MphRead::Mods::Localization::HudMessageLanguage
{
    [[nodiscard]] inline ::MphRead::Language CaptureSourceLanguage(
        std::optional<::MphRead::Language> sourceLanguage,
        ::MphRead::Language currentLanguage) noexcept
    {
        return sourceLanguage.value_or(currentLanguage);
    }

    [[nodiscard]] inline bool IsStaleLocalizedMessage(
        std::optional<::MphRead::Language> sourceLanguage,
        ::MphRead::Language currentLanguage) noexcept
    {
        return sourceLanguage.has_value() && *sourceLanguage != currentLanguage;
    }
}
