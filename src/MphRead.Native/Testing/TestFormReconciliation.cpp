#include "Mods/Network/Gameplay/Replication/FormReconciliation.hpp"
#include "Mods/Network/Gameplay/Replication/ReplicatedFormState.hpp"
#include "Mods/Network/Gameplay/Replication/FormTransitionPlaybackPolicy.hpp"
#include "Mods/Network/Gameplay/Replication/FormPresentationPolicy.hpp"
#include "Mods/Network/Gameplay/Replication/OwnerFormPredictionPolicy.hpp"

#include <iostream>
#include <string_view>

namespace
{
    bool Check(bool condition, std::string_view name)
    {
        if (!condition)
        {
            std::cerr << "FAIL " << name << '\n';
            return false;
        }
        return true;
    }
}

int main()
{
    using MphRead::Mods::Network::FormCorrection;
    using MphRead::Mods::Network::FormReconciliation;
    using MphRead::Mods::Network::FormTransitionPlaybackPolicy;
    using MphRead::Mods::Network::FormTransitionPlaybackDecision;
    using MphRead::Mods::Network::FormPresentationPolicy;
    using MphRead::Mods::Network::OwnerFormPredictionPolicy;
    using MphRead::Mods::Network::ReplicatedFormState;

    bool passed = true;
    FormReconciliation reconciliation;
    FormCorrection correction = FormCorrection::None;
    for (std::uint32_t frame = 10;
        frame < 10 + FormReconciliation::MaximumDisagreementFrames - 1; frame++)
    {
        correction = reconciliation.Step(frame, true, false, true, false, 300);
    }
    passed &= Check(correction == FormCorrection::None,
        "a morph remains in flight until the complete mismatch deadline");
    correction = reconciliation.Step(10 + FormReconciliation::MaximumDisagreementFrames - 1,
        true, false, true, false, 300);
    passed &= Check(correction == FormCorrection::Force,
        "a morph cannot extend the complete mismatch deadline");

    reconciliation.Reset();
    passed &= Check(reconciliation.Step(100, true, false, false, false, 300) == FormCorrection::None
        && reconciliation.Step(107, true, false, false, false, 300) == FormCorrection::None
        && reconciliation.Step(108, true, false, false, false, 300) == FormCorrection::Start,
        "the initial mismatch grace starts a correction");
    for (std::uint32_t frame = 109;
        frame < 100 + FormReconciliation::MaximumDisagreementFrames - 1; frame++)
    {
        correction = reconciliation.Step(frame, true, false, true, false, 300);
    }
    passed &= Check(correction == FormCorrection::None,
        "the transition can continue within the remaining mismatch budget");
    correction = reconciliation.Step(100 + FormReconciliation::MaximumDisagreementFrames - 1,
        true, false, true, false, 300);
    passed &= Check(correction == FormCorrection::Force,
        "the initial grace is charged against the complete mismatch budget");

    reconciliation.Reset();
    passed &= Check(reconciliation.Step(100, true, false, false, false, 0) == FormCorrection::None
        && reconciliation.Step(107, true, false, false, false, 0) == FormCorrection::None
        && reconciliation.Step(108, true, false, false, false, 0) == FormCorrection::Start,
        "a settled mismatch starts a correction after the normal grace");
    passed &= Check(reconciliation.Step(119, true, false, false, false, 0) == FormCorrection::None
        && reconciliation.Step(120, true, false, false, false, 0) == FormCorrection::Force,
        "a failed switch force-corrects after its retry grace");

    reconciliation.Reset();
    passed &= Check(reconciliation.Step(200, false, false, true, false, 250) == FormCorrection::None
        && reconciliation.Step(201, false, false, true, false, 250) == FormCorrection::None
        && reconciliation.Step(202, false, true, false, false, 250) == FormCorrection::None
        && reconciliation.Step(223, false, true, false, false, 250) == FormCorrection::None,
        "a locally predicted morph may finish before its authority bit arrives within one-way latency");
    passed &= Check(reconciliation.Step(224, true, true, false, false, 250) == FormCorrection::None,
        "the authority form acknowledgement clears the owner's temporary prediction mismatch");

    ReplicatedFormState form{};
    passed &= Check(ReplicatedFormState::TryCreate(false, true, false, 7, form)
        && form.Direction == ReplicatedFormState::Transition::Morph
        && form.Progress == 7 && !form.AltForm,
        "a remote hunter receives the authoritative morph direction and phase");
    passed &= Check(ReplicatedFormState::TryCreate(false, false, true, 12, form)
        && form.Direction == ReplicatedFormState::Transition::Unmorph
        && form.Progress == 12,
        "a remote hunter receives the authoritative unmorph direction and phase");
    passed &= Check(ReplicatedFormState::TryCreate(true, false, false, 0, form)
        && form.Direction == ReplicatedFormState::Transition::None && form.AltForm,
        "a completed alt-form state carries no stale transition phase");
    passed &= Check(FormPresentationPolicy::RequiresStableCorrection(form, false, false, false),
        "a stable sampled Alt form commits when the displayed form is still biped");
    ReplicatedFormState stableBiped{};
    passed &= Check(ReplicatedFormState::TryCreate(false, false, false, 0, stableBiped)
        && FormPresentationPolicy::RequiresStableCorrection(stableBiped, true, false, false),
        "a stable sampled biped form commits when the displayed form is still Alt");
    passed &= Check(FormPresentationPolicy::RequiresStableCorrection(stableBiped, false, true, false)
        && FormPresentationPolicy::RequiresStableCorrection(form, true, false, true),
        "a stable sample clears stale morph or unmorph animation flags");
    passed &= Check(!FormPresentationPolicy::RequiresStableCorrection(stableBiped, false, false, false)
        && !FormPresentationPolicy::RequiresStableCorrection(form, true, false, false),
        "a matching stable sample leaves the displayed form alone");
    ReplicatedFormState activeMorph{};
    passed &= Check(ReplicatedFormState::TryCreate(false, true, false, 15, activeMorph)
        && !FormPresentationPolicy::RequiresStableCorrection(activeMorph, false, true, false),
        "an active transition remains driven by its sampled animation phase");
    passed &= Check(OwnerFormPredictionPolicy::DeferStableCorrection(stableBiped, false, true)
        && OwnerFormPredictionPolicy::DeferStableCorrection(form, true, false),
        "stable server snapshots cannot force the local owner out of an in-flight form animation");
    passed &= Check(!OwnerFormPredictionPolicy::DeferStableCorrection(stableBiped, false, false)
        && !OwnerFormPredictionPolicy::DeferStableCorrection(activeMorph, true, false),
        "owner correction resumes after the local clip and transition snapshots remain predictive");
    passed &= Check(!ReplicatedFormState::TryCreate(false, true, true, 4, form),
        "contradictory morph directions are rejected");
    passed &= Check(!ReplicatedFormState::TryCreate(false, false, false, 1, form),
        "a stable form cannot carry a stale transition phase");
    form.Direction = static_cast<ReplicatedFormState::Transition>(0xFF);
    passed &= Check(!form.IsValid(), "an unknown transition value is rejected");
    form.Direction = ReplicatedFormState::Transition::None;
    form.Progress = 16;
    passed &= Check(!form.IsValid(), "an out-of-range transition phase is rejected");

    FormTransitionPlaybackPolicy playback;
    passed &= Check(playback.Step(ReplicatedFormState::Transition::Morph, 8,
        false, true, false, false) == FormTransitionPlaybackDecision::Apply,
        "an active morph continues to accept authority progress");
    passed &= Check(playback.Step(ReplicatedFormState::Transition::Morph, 14,
        true, false, false, true) == FormTransitionPlaybackDecision::PreserveCompleted,
        "a delayed morph snapshot preserves an already-completed alt-form animation");
    passed &= Check(playback.Step(ReplicatedFormState::Transition::Morph, 14,
        true, false, false, false) == FormTransitionPlaybackDecision::AlreadyPreserved,
        "a later progress snapshot cannot replay a completed morph after its animation becomes idle");
    passed &= Check(playback.Step(ReplicatedFormState::Transition::None, 0,
        true, false, false, false) == FormTransitionPlaybackDecision::Apply
        && playback.Step(ReplicatedFormState::Transition::Morph, 0,
            false, true, false, false) == FormTransitionPlaybackDecision::Apply,
        "a stable snapshot resets completion before the next morph");
    passed &= Check(playback.Step(ReplicatedFormState::Transition::Morph, 14,
        true, false, false, true) == FormTransitionPlaybackDecision::PreserveCompleted,
        "a morph completed after the new transition began is preserved");
    passed &= Check(playback.Step(ReplicatedFormState::Transition::Unmorph, 14,
        false, false, false, true) == FormTransitionPlaybackDecision::PreserveCompleted,
        "a direction change starts and preserves a completed unmorph");
    passed &= Check(playback.Step(ReplicatedFormState::Transition::Unmorph, 14,
        false, false, false, false) == FormTransitionPlaybackDecision::AlreadyPreserved,
        "a later unmorph snapshot cannot restore alt form after the player animation becomes idle");
    passed &= Check(playback.Step(ReplicatedFormState::Transition::Unmorph, 0,
        true, false, false, true) == FormTransitionPlaybackDecision::Apply,
        "a progress regression identifies a new same-direction transition");
    return passed ? 0 : 1;
}
