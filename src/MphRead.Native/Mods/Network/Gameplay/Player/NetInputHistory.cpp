#include "Mods/Network/Gameplay/Player/NetInputHistory.hpp"

#include "Entities/Players/PlayerEntity.hpp"

#include <algorithm>
#include <memory>
#include <vector>

namespace MphRead::Mods::Network
{
    std::array<NetInputHistory::History, NetInputHistory::Slots> NetInputHistory::_history{};

    IntentButtons NetInputHistory::Pressed(const Entities::PlayerEntity& player) noexcept
    {
        const Entities::PlayerControls& controls = player.Controls();
        IntentButtons pressed = IntentButtons::None;
        if (controls.MoveLeft().IsPressed()) pressed |= IntentButtons::MoveLeft;
        if (controls.MoveRight().IsPressed()) pressed |= IntentButtons::MoveRight;
        if (controls.MoveUp().IsPressed()) pressed |= IntentButtons::MoveUp;
        if (controls.MoveDown().IsPressed()) pressed |= IntentButtons::MoveDown;
        if (controls.Shoot().IsPressed()) pressed |= IntentButtons::Shoot;
        if (controls.Zoom().IsPressed()) pressed |= IntentButtons::Zoom;
        if (controls.Jump().IsPressed()) pressed |= IntentButtons::Jump;
        if (controls.Morph().IsPressed()) pressed |= IntentButtons::Morph;
        if (controls.Boost().IsPressed()) pressed |= IntentButtons::Boost;
        if (controls.AltAttack().IsPressed()) pressed |= IntentButtons::AltAttack;
        if (controls.ScanVisor().IsPressed()) pressed |= IntentButtons::ScanVisor;
        if (controls.NextWeapon().IsPressed()) pressed |= IntentButtons::NextWeapon;
        if (controls.PrevWeapon().IsPressed()) pressed |= IntentButtons::PrevWeapon;
        if (controls.RolltLeft().IsPressed()) pressed |= IntentButtons::RollLeft;
        if (controls.RollRight().IsPressed()) pressed |= IntentButtons::RollRight;
        if (controls.RollUp().IsPressed()) pressed |= IntentButtons::RollUp;
        if (controls.RollDown().IsPressed()) pressed |= IntentButtons::RollDown;
        return pressed;
    }

    void NetInputHistory::Record(const Entities::PlayerEntity& player) noexcept
    {
        const std::int32_t slot = player.SlotIndex();
        if (slot < 0 || slot >= Slots)
        {
            return;
        }
        if (!player.ModIsInPlay())
        {
            ForgetSlot(slot);
            return;
        }
        Record(slot, Pressed(player));
    }

    void NetInputHistory::Record(std::int32_t slot, IntentButtons pressed) noexcept
    {
        if (slot < 0 || slot >= Slots)
        {
            return;
        }
        History& history = _history[static_cast<std::size_t>(slot)];
        std::move_backward(history.begin(), history.end() - 1, history.end());
        history[0] = static_cast<std::uint32_t>(pressed);
    }

    void NetInputHistory::Attach(IntentPacket& intent, std::int32_t slot)
    {
        if (intent.Presses == nullptr)
        {
            intent.Presses = std::make_shared<std::vector<std::uint32_t>>(IntentPacket::PressHistory, 0);
        }
        intent.Presses->assign(IntentPacket::PressHistory, 0);
        if (slot < 0 || slot >= Slots)
        {
            return;
        }
        const History& history = _history[static_cast<std::size_t>(slot)];
        std::copy(history.begin(), history.end(), intent.Presses->begin());
    }

    void NetInputHistory::ForgetSlot(std::int32_t slot) noexcept
    {
        if (slot < 0 || slot >= Slots)
        {
            return;
        }
        _history[static_cast<std::size_t>(slot)] = History{};
    }

    void NetInputHistory::Reset() noexcept
    {
        _history = {};
    }
}
