#include "Mods/Network/Gameplay/Replication/FormReconciliation.hpp"

#include "NativeRuntime/System/Managed.hpp"

namespace MphRead::Mods::Network
{
    void FormReconciliation::Reset() noexcept
    {
        *this = FormReconciliation{};
    }

    FormCorrection FormReconciliation::Step(std::uint32_t frame, bool desiredAlt, bool actualAlt,
        bool morphing, bool unmorphing, std::int32_t pingMilliseconds)
    {
        const bool transitioning = morphing || unmorphing;
        if (transitioning)
        {
            const bool target = morphing;
            _transitionSeen = true;
            _transitionTarget = target;
            _transitionLastSeen = frame;
        }

        if (actualAlt == desiredAlt)
        {
            _mismatching = false;
            _attempted = false;
            return FormCorrection::None;
        }

        if (!_mismatching)
        {
            _mismatchSince = frame;
            _mismatching = true;
        }
        // The observed mismatch budget includes MismatchGrace. Otherwise a
        // puppet could spend the full grace window starting a transition and
        // then another full transition window out of sync with the authority.
        if (frame - _mismatchSince >= MaximumDisagreementFrames - 1)
        {
            Reset();
            return FormCorrection::Force;
        }

        // A morph only changes IsAltForm at animation end; an unmorph changes
        // it immediately. In both cases older snapshots/intents can still
        // describe the form from before the relayed press. The overall
        // disagreement deadline above continues to run while an animation is
        // in flight, so the initial grace cannot extend that deadline.
        if (transitioning)
        {
            return FormCorrection::None;
        }
        const std::int32_t latencyGraceValue = ::MphRead::NativeRuntime::UncheckedAdd(
            ::MphRead::NativeRuntime::UncheckedMultiply(pingMilliseconds, 60) / 1000, 8);
        const std::uint32_t latencyGrace = static_cast<std::uint32_t>(
            ::MphRead::NativeRuntime::MathClamp(latencyGraceValue, 8, 32));
        if (_transitionSeen && !_attempted && _transitionTarget == actualAlt
            && frame - _transitionLastSeen <= latencyGrace)
        {
            return FormCorrection::None;
        }

        if (_attempted)
        {
            if (frame - _attemptSince < FailedSwitchGrace)
            {
                return FormCorrection::None;
            }
            Reset();
            return FormCorrection::Force;
        }
        if (frame - _mismatchSince < MismatchGrace)
        {
            return FormCorrection::None;
        }
        _attempted = true;
        _attemptSince = frame;
        return FormCorrection::Start;
    }
}
