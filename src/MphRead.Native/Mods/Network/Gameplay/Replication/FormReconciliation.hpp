#pragma once

#include <cstdint>

namespace MphRead::Mods::Network
{
    enum class FormCorrection : std::int32_t { None, Start, Force };

    // Per-slot timing for reconciling a puppet's form with its source.
    struct FormReconciliation final
    {
        // Bounds the complete authority-to-puppet form disagreement, including
        // both the initial mismatch grace and any in-flight morph animation.
        static constexpr std::uint32_t MaximumDisagreementFrames = 90;

        void Reset() noexcept;

        [[nodiscard]] FormCorrection Step(std::uint32_t frame, bool desiredAlt, bool actualAlt,
            bool morphing, bool unmorphing, std::int32_t pingMilliseconds);

    private:
        static constexpr std::uint32_t MismatchGrace = 8;
        static constexpr std::uint32_t FailedSwitchGrace = 12;
        std::uint32_t _mismatchSince = 0;
        std::uint32_t _attemptSince = 0;
        std::uint32_t _transitionLastSeen = 0;
        bool _mismatching = false;
        bool _attempted = false;
        bool _transitionSeen = false;
        bool _transitionTarget = false;
    };
}
