#pragma once

#include "Mods/Network/Gameplay/Replication/ReplicatedFormState.hpp"

#include <cstdint>

namespace MphRead::Mods::Network
{
    enum class FormTransitionPlaybackDecision : std::uint8_t
    {
        Apply,
        PreserveCompleted,
        AlreadyPreserved
    };

    // Tracks one authoritative transition so late progress snapshots cannot
    // replay a local animation that has already completed.
    class FormTransitionPlaybackPolicy final
    {
    public:
        [[nodiscard]] FormTransitionPlaybackDecision Step(
            ReplicatedFormState::Transition direction,
            std::uint8_t progress,
            bool actualAlt,
            bool morphing,
            bool unmorphing,
            bool matchingAnimationEnded) noexcept;

        void Reset() noexcept;

    private:
        ReplicatedFormState::Transition _direction = ReplicatedFormState::Transition::None;
        std::uint8_t _lastProgress = 0;
        bool _observedTransition = false;
        bool _sawLocalTransition = false;
        bool _completed = false;
    };
}
