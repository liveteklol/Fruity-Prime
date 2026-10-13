#pragma once

#include <cstdint>

namespace MphRead::Mods::Network
{
    // The form state of a hunter at one authoritative snapshot frame.
    // Consumers apply it to the entity without changing snapshot playout.
    struct ReplicatedFormState final
    {
        enum class Transition : std::uint8_t
        {
            None,
            Morph,
            Unmorph
        };

        bool AltForm = false;
        Transition Direction = Transition::None;
        std::uint8_t Progress = 0;

        [[nodiscard]] bool HeadingAlt() const noexcept
        {
            return Direction == Transition::Morph
                || (Direction == Transition::None && AltForm);
        }

        [[nodiscard]] bool IsValid() const noexcept
        {
            return Progress <= 15
                && (Direction != Transition::None || Progress == 0)
                && (Direction == Transition::None || Direction == Transition::Morph
                    || Direction == Transition::Unmorph);
        }

        [[nodiscard]] static bool TryCreate(bool altForm, bool morphing, bool unmorphing,
            std::uint8_t progress, ReplicatedFormState& result) noexcept
        {
            if ((morphing && unmorphing) || progress > 15
                || (!morphing && !unmorphing && progress != 0))
            {
                return false;
            }
            result.AltForm = altForm;
            result.Direction = morphing ? Transition::Morph
                : unmorphing ? Transition::Unmorph : Transition::None;
            result.Progress = progress;
            return true;
        }
    };
}
