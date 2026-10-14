#include "Mods/Network/Gameplay/Player/NetPlayerIntent.hpp"

#include "Entities/Players/PlayerEntity.hpp"
#include "Mods/Network/Gameplay/Player/PlayerCombatPolicy.hpp"
#include "NativeRuntime/System/Managed.hpp"
#include "Formats/Enums.hpp"

#include <algorithm>
#include <memory>
#include <vector>

namespace MphRead::Mods::Network
{
    using ::MphRead::NativeRuntime::HasFlag;

    IntentPacket NetPlayerIntent::CaptureState(Entities::PlayerEntity& player,
        std::optional<bool> localSpectatorRequest)
    {
        Entities::PlayerControls& controls = player.Controls();
        const bool spectating = localSpectatorRequest.value_or(player.ModIsSpectating());
        const bool inPlay = localSpectatorRequest.has_value()
            ? PlayerCombatPolicy::IsAliveAndSpawned(
                HasFlag(player.LoadFlags(), Entities::LoadFlags::Active),
                HasFlag(player.LoadFlags(), Entities::LoadFlags::Spawned), player.Health()) && !spectating
            : player.ModIsInPlay();
        IntentButtons buttons = IntentButtons::None;
        if (!spectating)
        {
            if (controls.MoveLeft().IsDown()) buttons |= IntentButtons::MoveLeft;
            if (controls.MoveRight().IsDown()) buttons |= IntentButtons::MoveRight;
            if (controls.MoveUp().IsDown()) buttons |= IntentButtons::MoveUp;
            if (controls.MoveDown().IsDown()) buttons |= IntentButtons::MoveDown;
            if (controls.Shoot().IsDown()) buttons |= IntentButtons::Shoot;
            if (controls.Zoom().IsDown()) buttons |= IntentButtons::Zoom;
            if (controls.Jump().IsDown()) buttons |= IntentButtons::Jump;
            if (controls.Morph().IsDown()) buttons |= IntentButtons::Morph;
            if (controls.Boost().IsDown()) buttons |= IntentButtons::Boost;
            if (controls.AltAttack().IsDown()) buttons |= IntentButtons::AltAttack;
            if (controls.ScanVisor().IsDown()) buttons |= IntentButtons::ScanVisor;
            if (controls.NextWeapon().IsDown()) buttons |= IntentButtons::NextWeapon;
            if (controls.PrevWeapon().IsDown()) buttons |= IntentButtons::PrevWeapon;
            if (controls.RolltLeft().IsDown()) buttons |= IntentButtons::RollLeft;
            if (controls.RollRight().IsDown()) buttons |= IntentButtons::RollRight;
            if (controls.RollUp().IsDown()) buttons |= IntentButtons::RollUp;
            if (controls.RollDown().IsDown()) buttons |= IntentButtons::RollDown;
            if (player.EquipInfo()->Zoomed) buttons |= IntentButtons::ZoomedState;
            if (player.IsAltForm()) buttons |= IntentButtons::AltFormState;
        }
        if (inPlay)
        {
            buttons |= IntentButtons::InPlayState;
        }
        if (spectating)
        {
            buttons |= IntentButtons::SpectatingState;
        }

        IntentPacket intent{};
        intent.Buttons = buttons;
        intent.Aim = player.ModGunVector();
        intent.Position = player.Position;
        intent.WeaponSelect = static_cast<std::uint8_t>(player.CurrentWeapon());
        intent.AmmoUa = static_cast<std::uint16_t>(std::clamp(player.ModAmmo().first, 0, 0xFFFF));
        intent.AmmoMissiles = static_cast<std::uint16_t>(std::clamp(player.ModAmmo().second, 0, 0xFFFF));
        intent.Presses = std::make_shared<std::vector<std::uint32_t>>(IntentPacket::PressHistory);
        intent.ChargeLevel = static_cast<std::uint8_t>(std::clamp(player.ModChargeLevel(), 0, 255));
        intent.BoostDamage = static_cast<std::uint8_t>(std::clamp(player.ModBoostDamage(), 0, 255));
        intent.ShotFlags = static_cast<std::uint8_t>(player.IsPrimeHunter() ? IntentPacket::FlagPrimeHunter : 0);
        intent.HasState = true;
        intent.SetTouchReport(player.ModTouchReport());
        return intent;
    }
}
