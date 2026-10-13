#include "Mods/Network/Gameplay/StatusEffects/StatusEffectSynchronization.hpp"

#include "Entities/Players/PlayerEntity.hpp"
#include "Mods/Network/Gameplay/StatusEffects/BurnStatusEffectPolicy.hpp"
#include "Mods/Network/Gameplay/StatusEffects/DisruptStatusEffectPolicy.hpp"
#include "Mods/Network/Gameplay/StatusEffects/FreezeStatusEffectPolicy.hpp"
#include "Mods/Network/Gameplay/StatusEffects/TimedEffectSync.hpp"
#include "Mods/Network/Protocol/NetProtocol.hpp"

#include <algorithm>

namespace MphRead::Mods::Network
{
    namespace
    {
        struct BurnPolicyAdapter
        {
            Entities::PlayerEntity& Player;
            void Start(std::uint16_t frames) { BurnStatusEffectPolicy::Start(Player, frames); }
            void Update(std::uint16_t frames) { BurnStatusEffectPolicy::Update(Player, frames); }
            void End() { BurnStatusEffectPolicy::End(Player); }
        };

        struct DisruptPolicyAdapter
        {
            Entities::PlayerEntity& Player;
            void Start(std::uint16_t frames) { DisruptStatusEffectPolicy::Start(Player, frames); }
            void Update(std::uint16_t frames) { DisruptStatusEffectPolicy::Update(Player, frames); }
            void End() { DisruptStatusEffectPolicy::End(Player); }
        };

        struct FreezePolicyAdapter
        {
            Entities::PlayerEntity& Player;
            std::int32_t Slot;
            void Start(std::uint16_t frames) { FreezeStatusEffectPolicy::Start(Player, Slot, frames); }
            void Update(std::uint16_t frames) { FreezeStatusEffectPolicy::Update(Player, Slot, frames); }
            void End() { FreezeStatusEffectPolicy::End(Player); }
        };
    }

    void StatusEffectSynchronization::Capture(const Entities::PlayerEntity& player, PlayerState& state)
    {
        constexpr std::uint8_t statusMask = PlayerState::FlagFrozen
            | PlayerState::FlagDisrupted | PlayerState::FlagBurning;
        state.Flags = static_cast<std::uint8_t>(state.Flags & ~statusMask);
        state.StatusEffects.BurningFrames = std::min(BurnStatusEffectPolicy::Capture(player),
            TimedEffectTimers::MaxBurningFrames);
        state.StatusEffects.DisruptedFrames = std::min(DisruptStatusEffectPolicy::Capture(player),
            TimedEffectTimers::MaxDisruptedFrames);
        state.StatusEffects.FrozenFrames = std::min(FreezeStatusEffectPolicy::Capture(player),
            TimedEffectTimers::MaxFrozenFrames);
        if (state.StatusEffects.FrozenFrames > 0)
        {
            state.Flags = static_cast<std::uint8_t>(state.Flags | PlayerState::FlagFrozen);
        }
        if (state.StatusEffects.DisruptedFrames > 0)
        {
            state.Flags = static_cast<std::uint8_t>(state.Flags | PlayerState::FlagDisrupted);
        }
        if (state.StatusEffects.BurningFrames > 0)
        {
            state.Flags = static_cast<std::uint8_t>(state.Flags | PlayerState::FlagBurning);
        }
    }

    void StatusEffectSynchronization::Apply(Entities::PlayerEntity& player, const PlayerState& state,
        std::uint32_t snapshotAgeFrames)
    {
        BurnPolicyAdapter burn{ player };
        ApplyTimedEffect(RemainingAfterSnapshotAge(state.StatusEffects.BurningFrames, snapshotAgeFrames),
            BurnStatusEffectPolicy::Current(player), false, burn);

        DisruptPolicyAdapter disrupt{ player };
        ApplyTimedEffect(RemainingAfterSnapshotAge(state.StatusEffects.DisruptedFrames, snapshotAgeFrames),
            DisruptStatusEffectPolicy::Current(player), false, disrupt);

        const std::int32_t slot = player.SlotIndex();
        FreezePolicyAdapter freeze{ player, slot };
        ApplyTimedEffect(RemainingAfterSnapshotAge(state.StatusEffects.FrozenFrames, snapshotAgeFrames),
            FreezeStatusEffectPolicy::Current(player), FreezeStatusEffectPolicy::AwaitingAuthority(slot), freeze);
    }
}
