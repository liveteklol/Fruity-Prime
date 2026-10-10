#pragma once

#include <cstdint>

namespace MphRead::Entities
{
    // How a remote Weavel's copy reaches the form its snapshots report.
    //
    // It plays the morph and the unmorph like every other hunter's copy does
    // -- the form taking effect when the animation ends -- instead of snapping,
    // which showed nobody watching any transformation at all. A transition
    // whose animation never finishes is snapped after StallFrames. Decisions
    // only: PlayerEntity::ModApplyWeavelState carries them out, with the
    // turret lifecycle that stays the authority's.
    class WeavelReplicaTransition final
    {
    public:
        enum class Step
        {
            // Apply the reported form outright (FinalizeWeavelForm).
            Finalize,
            StartMorph,
            StartUnmorph,
            // An animation is under way: leave it alone.
            Wait,
        };

        static constexpr std::uint64_t StallFrames = 90;

        [[nodiscard]] Step Decide(bool desiredAlt, bool altForm, bool morphing, bool unmorphing,
            bool alive, std::uint64_t frame) noexcept
        {
            if (desiredAlt && !altForm && !unmorphing && alive)
            {
                if (!morphing)
                {
                    _started = frame;
                    return Step::StartMorph;
                }
                return frame - _started < StallFrames ? Step::Wait : Step::Finalize;
            }
            if (!desiredAlt && altForm && !morphing && alive)
            {
                _started = frame;
                return Step::StartUnmorph;
            }
            if (!desiredAlt && unmorphing)
            {
                return frame - _started < StallFrames ? Step::Wait : Step::Finalize;
            }
            return Step::Finalize;
        }

        // Whether a transition this class started is still to be closed, and
        // when it began.
        [[nodiscard]] bool Started() const noexcept { return _started != 0; }
        [[nodiscard]] std::uint64_t StartFrame() const noexcept { return _started; }
        void Close() noexcept { _started = 0; }

    private:
        std::uint64_t _started = 0;
    };
}
