#include "Mods/Network/Gameplay/Player/PlayerColors.hpp"

#include "GameState.hpp"
#include "NativeRuntime/System/Console.hpp"
#include "NativeRuntime/System/Globalization.hpp"

#include "Entities/Players/HalfturretEntity.hpp"
#include "Mods/Network/Diagnostics/NetLog.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace MphRead::Mods::Network
{
    std::array<std::int32_t, Entities::PlayerEntity::SlotCapacity> PlayerColors::_choices{};

    std::array<std::int32_t, Entities::PlayerEntity::SlotCapacity> PlayerColors::_applied
        = PlayerColors::CreateApplied();

    std::array<std::int32_t, Entities::PlayerEntity::SlotCapacity> PlayerColors::CreateApplied()
    {
        std::array<std::int32_t, Entities::PlayerEntity::SlotCapacity> applied{};
        applied.fill(-1);
        return applied;
    }

    void PlayerColors::Reset()
    {
        _choices.fill(0);
        _applied.fill(-1);
    }

    std::int32_t PlayerColors::ChoiceFor(std::int32_t slot)
    {
        return _choices.at(static_cast<std::size_t>(slot));
    }

    void PlayerColors::SetChoice(std::int32_t slot, std::int32_t color)
    {
        _choices.at(static_cast<std::size_t>(slot)) = color;
    }

    std::int32_t PlayerColors::Clamp(std::int32_t color)
    {
        return color < 0 || color >= Count ? 0 : color;
    }

    void PlayerColors::Resolve()
    {
        if (GameState::Teams())
        {
            return;
        }
        for (std::int32_t slot = 0;
            slot < Entities::PlayerEntity::MaxPlayers()
                && slot < static_cast<std::int32_t>(Entities::PlayerEntity::Players().size());
            slot++)
        {
            std::shared_ptr<Entities::PlayerEntity> player
                = Entities::PlayerEntity::Players()[static_cast<std::size_t>(slot)];
            if (player == nullptr
                || (player->LoadFlags() & Entities::LoadFlags::Active) == Entities::LoadFlags::None)
            {
                continue;
            }
            const std::int32_t want = Clamp(_choices[static_cast<std::size_t>(slot)]);
            std::int32_t color = want;
            for (std::int32_t step = 0; step < Count; step++)
            {
                color = (want + step) % Count;
                if (!TakenBefore(slot, player->Hunter(), color))
                {
                    break;
                }
            }
            player->SetRecolor(color);
            if (_applied[static_cast<std::size_t>(slot)] != color)
            {
                _applied[static_cast<std::size_t>(slot)] = color;

                std::string consoleMessage = "[net] slot ";
                consoleMessage += ::MphRead::NativeRuntime::ToString(slot);
                consoleMessage += " (";
                consoleMessage += ::MphRead::ToString(player->Hunter());
                consoleMessage += ") wears suit ";
                consoleMessage += ::MphRead::NativeRuntime::ToString(color + 1);
                if (color != want)
                {
                    consoleMessage += " -- asked for ";
                    consoleMessage += ::MphRead::NativeRuntime::ToString(want + 1);
                }
                NativeRuntime::ConsoleWriteLine(consoleMessage);

                std::string logMessage = "slot ";
                logMessage += ::MphRead::NativeRuntime::ToString(slot);
                logMessage += " ";
                logMessage += ::MphRead::ToString(player->Hunter());
                logMessage += " suit ";
                logMessage += ::MphRead::NativeRuntime::ToString(color + 1);
                logMessage += " (asked ";
                logMessage += ::MphRead::NativeRuntime::ToString(want + 1);
                logMessage += ")";
                NetLog::Event(logMessage);
            }
            if (player->Halfturret() != nullptr)
            {
                player->Halfturret()->SetRecolor(color);
            }
        }
    }

    bool PlayerColors::TakenBefore(
        std::int32_t slot, MphRead::Hunter hunter, std::int32_t color)
    {
        for (std::int32_t i = 0; i < slot; i++)
        {
            std::shared_ptr<Entities::PlayerEntity> other
                = Entities::PlayerEntity::Players()[static_cast<std::size_t>(i)];
            if (other != nullptr
                && (other->LoadFlags() & Entities::LoadFlags::Active) != Entities::LoadFlags::None
                && other->Hunter() == hunter
                && other->Recolor() == color)
            {
                return true;
            }
        }
        return false;
    }
}
