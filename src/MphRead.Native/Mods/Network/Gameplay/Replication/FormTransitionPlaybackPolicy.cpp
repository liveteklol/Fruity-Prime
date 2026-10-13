#include "Mods/Network/Gameplay/Replication/FormTransitionPlaybackPolicy.hpp"

namespace MphRead::Mods::Network
{
    FormTransitionPlaybackDecision FormTransitionPlaybackPolicy::Step(
        ReplicatedFormState::Transition direction,
        std::uint8_t progress,
        bool actualAlt,
        bool morphing,
        bool unmorphing,
        bool matchingAnimationEnded) noexcept
    {
        if (direction == ReplicatedFormState::Transition::None)
        {
            Reset();
            return FormTransitionPlaybackDecision::Apply;
        }

        const bool regressed = _observedTransition && direction == _direction
            && progress < _lastProgress;
        const bool started = !_observedTransition || direction != _direction || regressed;
        if (started)
        {
            _observedTransition = true;
            _direction = direction;
            _lastProgress = progress;
            _sawLocalTransition = false;
            _completed = false;
        }
        else if (progress > _lastProgress)
        {
            _lastProgress = progress;
        }

        const bool matchingLocalTransition = direction == ReplicatedFormState::Transition::Morph
            ? morphing : direction == ReplicatedFormState::Transition::Unmorph && unmorphing;
        if (matchingLocalTransition)
        {
            _sawLocalTransition = true;
        }

        const bool transitionActive = morphing || unmorphing;
        const bool atTarget = direction == ReplicatedFormState::Transition::Morph
            ? actualAlt : direction == ReplicatedFormState::Transition::Unmorph && !actualAlt;
        if (!atTarget)
        {
            _completed = false;
        }
        else if (!_completed && !transitionActive && matchingAnimationEnded
            && (_sawLocalTransition || progress >= 14))
        {
            _completed = true;
            return FormTransitionPlaybackDecision::PreserveCompleted;
        }

        if (_completed && atTarget && !transitionActive)
        {
            return FormTransitionPlaybackDecision::AlreadyPreserved;
        }
        return FormTransitionPlaybackDecision::Apply;
    }

    void FormTransitionPlaybackPolicy::Reset() noexcept
    {
        *this = FormTransitionPlaybackPolicy{};
    }
}
