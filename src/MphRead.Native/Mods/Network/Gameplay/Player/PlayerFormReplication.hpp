#pragma once

#include "Mods/Network/Domain/NetworkCapacity.hpp"
#include "Mods/Network/Gameplay/Replication/FormReconciliation.hpp"
#include "Mods/Network/Gameplay/Replication/OwnerFormPredictionPolicy.hpp"
#include "Mods/Network/Gameplay/Replication/ReplicatedFormState.hpp"
#include "Mods/Network/Gameplay/Replication/FormTransitionPlaybackPolicy.hpp"

#include <array>
#include <cstdint>
#include <string>

namespace MphRead::Entities
{
    class PlayerEntity;
}

namespace MphRead::Mods::Network
{
    struct IntentPacket;
    struct PlayerState;

    // Owns the per-slot authority form observations and reconciles entity
    // form transitions using session-provided frame and ping values.
    class PlayerFormReplication final
    {
    public:
        PlayerFormReplication() = delete;

        [[nodiscard]] static std::string FormSaidByAuthority();
        static void RecordAuthorityForm(std::int32_t slot, bool altForm) noexcept;
        static void Apply(Entities::PlayerEntity& player, bool desiredAlt,
            std::uint32_t frame, std::int32_t pingMilliseconds);
        static void Apply(Entities::PlayerEntity& player, const PlayerState& state,
            std::uint32_t frame, std::int32_t pingMilliseconds);
        static void Apply(Entities::PlayerEntity& player, const ReplicatedFormState& state,
            std::uint32_t frame, std::int32_t pingMilliseconds);
        // Reconciles the local owner's form state without seeking or pausing
        // the input-predicted animation on authority snapshot progress.
        static void ApplyOwnerPrediction(Entities::PlayerEntity& player, const PlayerState& state,
            std::uint32_t frame, std::int32_t pingMilliseconds);
        // Applies a remote replica's form animation at the shared presentation
        // clock.
        static void ApplyPresentation(Entities::PlayerEntity& player, const ReplicatedFormState& state,
            std::uint32_t frame, std::int32_t pingMilliseconds);
        [[nodiscard]] static bool AdvanceForAltFormAction(Entities::PlayerEntity& player,
            const IntentPacket* ownerIntent, const PlayerState* authorityState, std::uint32_t frame);
        [[nodiscard]] static FormCorrection Reconcile(std::int32_t slot, std::uint32_t frame,
            bool desiredAlt, bool actualAlt, bool morphing, bool unmorphing,
            std::int32_t pingMilliseconds);

        static void NoteRoomChanged() noexcept;
        static void Reset() noexcept;
        static void ForgetSlot(std::int32_t slot) noexcept;

    private:
        enum class ProgressApplication : std::uint8_t
        {
            NativePlayback,
            OwnerPrediction,
            PresentationClock
        };

        static void ApplyReplicatedState(Entities::PlayerEntity& player, const ReplicatedFormState& state,
            std::uint32_t frame, std::int32_t pingMilliseconds, ProgressApplication progressApplication);
        static std::array<FormReconciliation, NetworkSlotCapacity> _reconciliation;
        static std::array<FormTransitionPlaybackPolicy, NetworkSlotCapacity> _transitionPlayback;
        static std::array<std::uint8_t, NetworkSlotCapacity> _authoritySaid;
        static std::array<bool, NetworkSlotCapacity> _ownerStableCorrectionDeferred;
    };
}
