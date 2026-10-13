#include "Mods/Network/Gameplay/Player/PlayerFormReplication.hpp"

#include "Entities/Players/PlayerEntity.hpp"
#include "Mods/Network/Diagnostics/NetLog.hpp"
#include "Mods/Network/Gameplay/Player/NetPlayerLifecycle.hpp"
#include "Mods/Network/Gameplay/Replication/FormTransitionPlaybackPolicy.hpp"
#include "Mods/Network/Gameplay/Replication/FormPresentationPolicy.hpp"
#include "Mods/Network/Protocol/Packets/RealtimePackets.hpp"
#include "Mods/Network/Session/NetSession.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace MphRead::Mods::Network
{
    static_assert(NetworkSlotCapacity == Entities::PlayerEntity::SlotCapacity);

    namespace
    {
        [[nodiscard]] bool DecodeFormState(const PlayerState& state, ReplicatedFormState& form)
        {
            const bool altForm = (state.Flags & PlayerState::FlagAltForm) != 0;
            const bool morphing = (state.WeavelFlags & PlayerState::WeavelFlagMorphing) != 0;
            const bool unmorphing = (state.WeavelFlags & PlayerState::WeavelFlagUnmorphing) != 0;
            return ReplicatedFormState::TryCreate(altForm, morphing, unmorphing,
                state.FormTransitionProgress(), form);
        }
    }

    std::array<FormReconciliation, NetworkSlotCapacity> PlayerFormReplication::_reconciliation{};
    std::array<FormTransitionPlaybackPolicy, NetworkSlotCapacity> PlayerFormReplication::_transitionPlayback{};
    std::array<std::uint8_t, NetworkSlotCapacity> PlayerFormReplication::_authoritySaid{};
    std::array<bool, NetworkSlotCapacity> PlayerFormReplication::_ownerStableCorrectionDeferred{};

    std::string PlayerFormReplication::FormSaidByAuthority()
    {
        std::string text;
        text.reserve(Entities::PlayerEntity::SlotCapacity);
        for (std::int32_t i = 0; i < Entities::PlayerEntity::MaxPlayers()
            && i < static_cast<std::int32_t>(_authoritySaid.size()); ++i)
        {
            const std::uint8_t said = _authoritySaid[static_cast<std::size_t>(i)];
            text.push_back(said == 0 ? '-' : said == 2 ? 'A' : 'b');
        }
        return text;
    }

    void PlayerFormReplication::RecordAuthorityForm(std::int32_t slot, bool altForm) noexcept
    {
        if (slot < 0 || slot >= static_cast<std::int32_t>(_authoritySaid.size()))
        {
            return;
        }
        _authoritySaid[static_cast<std::size_t>(slot)] = static_cast<std::uint8_t>(altForm ? 2 : 1);
    }

    void PlayerFormReplication::Apply(Entities::PlayerEntity& player, bool desiredAlt,
        std::uint32_t frame, std::int32_t pingMilliseconds)
    {
        const std::int32_t slot = player.SlotIndex();
        if (slot < 0 || slot >= static_cast<std::int32_t>(_reconciliation.size()))
        {
            return;
        }
        const FormCorrection correction = Reconcile(slot, frame, desiredAlt,
            player.IsAltForm(), player.IsMorphing(), player.IsUnmorphing(), pingMilliseconds);
        if (correction == FormCorrection::Start)
        {
            player.ModStartFormSwitch();
        }
        else if (correction == FormCorrection::Force)
        {
            player.ModForceForm(desiredAlt);
        }
    }

    void PlayerFormReplication::Apply(Entities::PlayerEntity& player, const PlayerState& state,
        std::uint32_t frame, std::int32_t pingMilliseconds)
    {
        const std::int32_t slot = player.SlotIndex();
        if (slot < 0 || slot >= static_cast<std::int32_t>(_reconciliation.size()))
        {
            return;
        }
        ReplicatedFormState form{};
        if (!DecodeFormState(state, form))
        {
            return;
        }
        Apply(player, form, frame, pingMilliseconds);
    }

    void PlayerFormReplication::Apply(Entities::PlayerEntity& player, const ReplicatedFormState& state,
        std::uint32_t frame, std::int32_t pingMilliseconds)
    {
        ApplyReplicatedState(player, state, frame, pingMilliseconds, ProgressApplication::NativePlayback);
    }

    void PlayerFormReplication::ApplyOwnerPrediction(Entities::PlayerEntity& player,
        const PlayerState& state, std::uint32_t frame, std::int32_t pingMilliseconds)
    {
        const std::int32_t slot = player.SlotIndex();
        if (slot < 0 || slot >= static_cast<std::int32_t>(_ownerStableCorrectionDeferred.size()))
        {
            return;
        }
        ReplicatedFormState form{};
        if (!DecodeFormState(state, form))
        {
            return;
        }
        if (OwnerFormPredictionPolicy::DeferStableCorrection(
            form, player.IsMorphing(), player.IsUnmorphing()))
        {
            if (!_ownerStableCorrectionDeferred[static_cast<std::size_t>(slot)]
                && NetLog::Enabled())
            {
                NetLog::Event("[form-owner] deferred stable authority form during local transition slot="
                    + std::to_string(slot)
                    + " frame=" + std::to_string(frame)
                    + " local=" + player.ModFormState()
                    + " authority=" + (form.AltForm ? std::string("alt") : std::string("biped")));
            }
            _ownerStableCorrectionDeferred[static_cast<std::size_t>(slot)] = true;
            return;
        }
        _ownerStableCorrectionDeferred[static_cast<std::size_t>(slot)] = false;
        ApplyReplicatedState(player, form, frame, pingMilliseconds, ProgressApplication::OwnerPrediction);
    }

    void PlayerFormReplication::ApplyPresentation(Entities::PlayerEntity& player,
        const ReplicatedFormState& state, std::uint32_t frame, std::int32_t pingMilliseconds)
    {
        ApplyReplicatedState(player, state, frame, pingMilliseconds, ProgressApplication::PresentationClock);
    }

    void PlayerFormReplication::ApplyReplicatedState(Entities::PlayerEntity& player,
        const ReplicatedFormState& state,
        std::uint32_t frame, std::int32_t pingMilliseconds, ProgressApplication progressApplication)
    {
        const std::int32_t slot = player.SlotIndex();
        if (slot < 0 || slot >= static_cast<std::int32_t>(_reconciliation.size()) || !state.IsValid())
        {
            return;
        }
        if (progressApplication == ProgressApplication::PresentationClock
            && player.Hunter() == Hunter::Weavel)
        {
            // Weavel's turret lifecycle remains specialized, but its displayed
            // form must commit at the same buffered read point as every other
            // hunter. ModApplyWeavelState still applies authoritative turret
            // presence, health, and position after this form-only correction.
            _reconciliation[static_cast<std::size_t>(slot)].Reset();
            _transitionPlayback[static_cast<std::size_t>(slot)].Reset();
            if (state.Direction == ReplicatedFormState::Transition::None)
            {
                player.ModReleaseFormTransitionHold();
            }
            if (FormPresentationPolicy::RequiresStableCorrection(state, player.IsAltForm(),
                player.IsMorphing(), player.IsUnmorphing()))
            {
                player.ModFinalizeWeavelPresentationForm(state.AltForm);
            }
            return;
        }
        if (player.Hunter() != Hunter::Weavel && state.Direction != ReplicatedFormState::Transition::None)
        {
            const bool morphing = state.Direction == ReplicatedFormState::Transition::Morph;
            FormReconciliation& reconciliation = _reconciliation[static_cast<std::size_t>(slot)];
            reconciliation.Reset();
            if (progressApplication == ProgressApplication::PresentationClock)
            {
                // A presentation sample is the form clip's clock, so apply it
                // even if native playback completed ahead of that read point.
                // The latest-snapshot path below keeps its no-replay policy;
                // the buffered presentation path must be able to restore and
                // hold the sampled phase until the clock reaches stable form.
                _transitionPlayback[static_cast<std::size_t>(slot)].Reset();
                player.ModApplyFormTransition(morphing);
                player.ModHoldFormTransitionAtProgress(morphing, state.Progress);
                return;
            }
            const FormTransitionPlaybackDecision playback = _transitionPlayback[static_cast<std::size_t>(slot)].Step(
                state.Direction, state.Progress, player.IsAltForm(), player.IsMorphing(), player.IsUnmorphing(),
                player.ModFormTransitionAnimationEnded(morphing));
            if (playback != FormTransitionPlaybackDecision::Apply)
            {
                if (playback == FormTransitionPlaybackDecision::PreserveCompleted)
                {
                    NetLog::Event("[form-sync] slot " + std::to_string(slot)
                        + " preserved completed " + (morphing ? std::string("morph") : std::string("unmorph"))
                        + " while applying delayed transition snapshot frame=" + std::to_string(frame)
                        + " progress=" + std::to_string(state.Progress)
                        + " local=" + player.ModFormState());
                }
                return;
            }
            player.ModApplyFormTransition(morphing);
            if (progressApplication == ProgressApplication::OwnerPrediction)
            {
                // The owner already began this clip from local input. Its
                // native simulation owns the animation clock; seeking to a
                // delayed snapshot and holding the final phase made the local
                // transform visibly pause before the authority's commit.
                return;
            }
            player.ModSyncFormTransitionProgress(morphing, state.Progress);
            return;
        }
        _transitionPlayback[static_cast<std::size_t>(slot)].Reset();
        if (player.Hunter() != Hunter::Weavel)
        {
            player.ModReleaseFormTransitionHold();
            if (progressApplication == ProgressApplication::PresentationClock)
            {
                // The sample owns displayed form state at the shared read
                // point. Bypass snapshot reconciliation's latency grace here:
                // that grace belongs to latest-state correction and would
                // leave a presentation sample visibly behind its own clock.
                if (FormPresentationPolicy::RequiresStableCorrection(state, player.IsAltForm(),
                    player.IsMorphing(), player.IsUnmorphing()))
                {
                    player.ModForceForm(state.AltForm);
                    return;
                }
            }
        }
        Apply(player, state.AltForm, frame, pingMilliseconds);
    }

    bool PlayerFormReplication::AdvanceForAltFormAction(Entities::PlayerEntity& player,
        const IntentPacket* ownerIntent, const PlayerState* authorityState, std::uint32_t frame)
    {
        const std::int32_t slot = player.SlotIndex();
        const bool ownerInPlay = ownerIntent != nullptr
            && (ownerIntent->Buttons & IntentButtons::InPlayState) != IntentButtons::None;
        const bool ownerReportsAlt = ownerIntent != nullptr
            && (ownerIntent->Buttons & IntentButtons::AltFormState) != IntentButtons::None;
        if (!ownerInPlay || !ownerReportsAlt || !player.ModIsInPlay() || slot < 0
            || slot >= static_cast<std::int32_t>(_reconciliation.size())
            || player.Hunter() == Hunter::Guardian)
        {
            return false;
        }

        const std::string before = player.ModFormState();
        ReplicatedFormState authorityForm{};
        const ReplicatedFormState* authorityFormPtr = nullptr;
        if (authorityState != nullptr
            && authorityState->SlotIndex == slot
            && NetPlayerLifecycle::Matches(slot, authorityState->SlotGeneration, authorityState->LifeId))
        {
            const bool altForm = (authorityState->Flags & PlayerState::FlagAltForm) != 0;
            const bool morphing = (authorityState->WeavelFlags & PlayerState::WeavelFlagMorphing) != 0;
            const bool unmorphing = (authorityState->WeavelFlags & PlayerState::WeavelFlagUnmorphing) != 0;
            if (ReplicatedFormState::TryCreate(altForm, morphing, unmorphing,
                authorityState->FormTransitionProgress(), authorityForm))
            {
                authorityFormPtr = &authorityForm;
            }
        }

        const bool authorityMovesToAlt = authorityFormPtr != nullptr && authorityFormPtr->IsValid()
            && (authorityFormPtr->AltForm
                || authorityFormPtr->Direction == ReplicatedFormState::Transition::Morph);
        if (authorityMovesToAlt)
        {
            // A bomb is evidence that the owner already executed an alt-form
            // action. Apply its form sample now, before this simulation step,
            // so a final-pending morph can finish alongside the bomb spawn.
            _reconciliation[static_cast<std::size_t>(slot)].Reset();
            _transitionPlayback[static_cast<std::size_t>(slot)].Reset();
            Apply(player, *authorityFormPtr, frame, 0);
        }

        const char* reason = nullptr;
        if (player.IsMorphing())
        {
            player.ModReleaseFormTransitionHold();
            reason = "resume-morph";
        }
        else if (player.IsUnmorphing() || !player.IsAltForm())
        {
            // The action report is newer evidence than a biped/unmorphing
            // sample: begin or reverse the native animation without holding
            // the bomb behind it.
            player.ModApplyFormTransition(true);
            player.ModReleaseFormTransitionHold();
            reason = "start-morph";
        }
        if (reason == nullptr)
        {
            return false;
        }

        _reconciliation[static_cast<std::size_t>(slot)].Reset();
        NetLog::Event("[form-sync] alt-action advance slot=" + std::to_string(slot)
            + " frame=" + std::to_string(frame)
            + " reason=" + reason
            + " before=" + before
            + " after=" + player.ModFormState());
        return true;
    }

    FormCorrection PlayerFormReplication::Reconcile(std::int32_t slot, std::uint32_t frame,
        bool desiredAlt, bool actualAlt, bool morphing, bool unmorphing, std::int32_t pingMilliseconds)
    {
        return slot < 0 || slot >= static_cast<std::int32_t>(_reconciliation.size())
            ? FormCorrection::None
            : _reconciliation[static_cast<std::size_t>(slot)].Step(frame, desiredAlt, actualAlt,
                morphing, unmorphing, pingMilliseconds);
    }

    void PlayerFormReplication::NoteRoomChanged() noexcept
    {
        _reconciliation.fill(FormReconciliation{});
        _transitionPlayback.fill(FormTransitionPlaybackPolicy{});
        _ownerStableCorrectionDeferred.fill(false);
    }

    void PlayerFormReplication::Reset() noexcept
    {
        _reconciliation.fill(FormReconciliation{});
        _transitionPlayback.fill(FormTransitionPlaybackPolicy{});
        _authoritySaid.fill(0);
        _ownerStableCorrectionDeferred.fill(false);
    }

    void PlayerFormReplication::ForgetSlot(std::int32_t slot) noexcept
    {
        if (slot >= 0 && slot < static_cast<std::int32_t>(_reconciliation.size()))
        {
            _reconciliation[static_cast<std::size_t>(slot)].Reset();
            _transitionPlayback[static_cast<std::size_t>(slot)].Reset();
            _ownerStableCorrectionDeferred[static_cast<std::size_t>(slot)] = false;
        }
    }
}
