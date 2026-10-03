#pragma once

#include <algorithm>
#include <compare>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace MphRead::NativeRuntime::Rhi
{
    // One ordered queue's successful submissions. This is independent of
    // simulation frames, presentation frames, and reusable command slots.
    struct SubmissionSerial final
    {
        std::uint64_t Value = 0;
        auto operator<=>(const SubmissionSerial&) const = default;
    };

    class SubmissionProgress final
    {
    public:
        [[nodiscard]] SubmissionSerial Next() const
        {
            if (_submitted.Value == std::numeric_limits<std::uint64_t>::max())
                throw std::overflow_error("Submission serial exhausted.");
            return {_submitted.Value + 1};
        }

        // Commit only after the native queue accepted the submission.
        void Submitted(SubmissionSerial serial)
        {
            if (serial != Next()) throw std::logic_error("Unordered submission serial.");
            _submitted = serial;
        }

        void Complete(SubmissionSerial serial)
        {
            if (serial > _submitted) throw std::logic_error("Completion exceeds submitted work.");
            _completed = std::max(_completed, serial);
        }

        [[nodiscard]] SubmissionSerial Submitted() const noexcept { return _submitted; }
        [[nodiscard]] SubmissionSerial Completed() const noexcept { return _completed; }

    private:
        SubmissionSerial _submitted{};
        SubmissionSerial _completed{};
    };
}
