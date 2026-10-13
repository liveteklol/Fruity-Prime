#pragma once

#include "Mods/Network/Gameplay/Replication/ReplicatedFormState.hpp"

namespace MphRead::Mods::Network
{
    // Decides when a sampled stable form owns the displayed form state.
    // Gameplay reconciliation still uses FormReconciliation and its grace.
    class FormPresentationPolicy final
    {
    public:
        FormPresentationPolicy() = delete;

        [[nodiscard]] static bool RequiresStableCorrection(const ReplicatedFormState& sample,
            bool actualAlt, bool morphing, bool unmorphing) noexcept
        {
            return sample.IsValid()
                && sample.Direction == ReplicatedFormState::Transition::None
                && (sample.AltForm != actualAlt || morphing || unmorphing);
        }
    };
}
