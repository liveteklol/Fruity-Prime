#include "Mods/Network/Gameplay/Replication/PlayerStateSnapshotCapture.hpp"

#include "GameState.hpp"
#include "Entities/Players/HalfturretEntity.hpp"
#include "Entities/Players/PlayerEntity.hpp"
#include "Mods/Network/Gameplay/Combat/NetDamage.hpp"
#include "Mods/Network/Gameplay/Pickups/NetPowerUps.hpp"
#include "Mods/Network/Gameplay/StatusEffects/StatusEffectSynchronization.hpp"
#include "NativeRuntime/System/Managed.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>

namespace MphRead::Mods::Network
{
    using ::MphRead::NativeRuntime::HasFlag;

    PlayerState PlayerStateSnapshotCapture::Capture(Entities::PlayerEntity& player, std::int32_t slot,
        std::uint16_t slotGeneration, std::uint16_t lifeId)
    {
        PlayerState state{};
        state.SlotIndex = static_cast<std::uint8_t>(slot);
        state.SlotGeneration = slotGeneration;
        state.LifeId = lifeId;
        state.Flags = static_cast<std::uint8_t>(
            PlayerState::FlagActive
            | (player.IsAltForm() ? PlayerState::FlagAltForm : 0)
            | (player.ModIsInPlay() ? PlayerState::FlagSpawned : 0)
            | (player.EquipInfo()->Zoomed ? PlayerState::FlagZoomed : 0)
            | (HasFlag(player.Flags2(), Entities::PlayerFlags2::Spectating) ? PlayerState::FlagSpectating : 0));
        state.Position = player.Position;
        state.Speed = player.Speed();
        state.Facing = player.FacingVector();
        state.Health = static_cast<std::uint16_t>(std::clamp(player.Health(), 0,
            static_cast<std::int32_t>(std::numeric_limits<std::uint16_t>::max())));
        state.CurrentWeapon = static_cast<std::uint8_t>(player.CurrentWeapon());
        if (player.Hunter() == Hunter::Weavel && HasFlag(player.Flags2(), Entities::PlayerFlags2::Halfturret)
            && player.Halfturret() && player.Halfturret()->Health() > 0)
        {
            const auto& turret = *player.Halfturret();
            state.WeavelFlags = static_cast<std::uint8_t>(PlayerState::WeavelFlagTurretActive
                | (turret.Grounded() ? PlayerState::WeavelFlagTurretGrounded : 0));
            state.HalfturretHealth = static_cast<std::uint8_t>(std::clamp(turret.Health(), 0, 255));
            state.HalfturretPosition = turret.Position;
        }
        if (player.IsMorphing())
        {
            state.WeavelFlags = static_cast<std::uint8_t>(state.WeavelFlags | PlayerState::WeavelFlagMorphing);
            if (player.Hunter() != Hunter::Weavel)
            {
                state.SetFormTransitionProgress(player.ModFormTransitionProgress());
            }
        }
        else if (player.IsUnmorphing())
        {
            state.WeavelFlags = static_cast<std::uint8_t>(state.WeavelFlags | PlayerState::WeavelFlagUnmorphing);
            if (player.Hunter() != Hunter::Weavel)
            {
                state.SetFormTransitionProgress(player.ModFormTransitionProgress());
            }
        }
        state.Team = static_cast<std::uint8_t>(player.Team());
        state.Points = static_cast<std::int16_t>(std::clamp(GameState::Points()[slot],
            static_cast<std::int32_t>(std::numeric_limits<std::int16_t>::min()),
            static_cast<std::int32_t>(std::numeric_limits<std::int16_t>::max())));
        state.Kills = static_cast<std::uint16_t>(std::clamp(GameState::Kills()[slot], 0,
            static_cast<std::int32_t>(std::numeric_limits<std::uint16_t>::max())));
        state.Deaths = static_cast<std::uint16_t>(std::clamp(GameState::Deaths()[slot], 0,
            static_cast<std::int32_t>(std::numeric_limits<std::uint16_t>::max())));
        NetDamage::Write(slot, state);
        NetPowerUps::Capture(player, state);
        StatusEffectSynchronization::Capture(player, state);
        return state;
    }
}
