#pragma once

#include "Mods/Network/Gameplay/Replication/ReplicatedFormState.hpp"

namespace MphRead::Mods::Network
{
    // A stable authority sample can arrive while the local owner's native
    // morph clip is still running. It may reconcile gameplay after the clip,
    // but it must not replace the owner's predicted animation early.
    class OwnerFormPredictionPolicy final
    {
    public:
        OwnerFormPredictionPolicy() = delete;

        [[nodiscard]] static bool DeferStableCorrection(
            const ReplicatedFormState& authority,
            bool morphing,
            bool unmorphing) noexcept
        {
            return authority.IsValid()
                && authority.Direction == ReplicatedFormState::Transition::None
                && (morphing || unmorphing);
        }
    };
}
