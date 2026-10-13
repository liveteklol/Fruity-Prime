#include "Mods/Network/Session/NetSlotManager.hpp"
#include "Mods/Network/Gameplay/Weapons/ContinuousWeaponPhase.hpp"
#include "Mods/Network/Bots/NetBotRoles.hpp"
#include "Mods/Network/Gameplay/Player/NetPlayerLifecycle.hpp"
#include "Mods/Multiplayer/TeamVisuals.hpp"

#include "GameState.hpp"
#include "Metadata/Metadata.hpp"
#include "NativeRuntime/System/Console.hpp"
#include "NativeRuntime/System/Exceptions.hpp"
#include "NativeRuntime/System/Globalization.hpp"
#include "Mods/Network/Gameplay/Combat/NetDamage.hpp"
#include "Mods/Network/Gameplay/Combat/NetHitPrediction.hpp"
#include "Mods/Network/Diagnostics/NetLog.hpp"
#include "Mods/Network/Gameplay/Player/NetPlayerBridge.hpp"
#include "Mods/Network/Gameplay/Player/NetScoreboard.hpp"
#include "Mods/Network/Session/NetSession.hpp"
#include "NativeRuntime/System/Managed.hpp"
#include "Formats/Types.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>

using ::MphRead::NativeRuntime::RequireReference;
using ::MphRead::NativeRuntime::ManagedAt;
using ::MphRead::NativeRuntime::ManagedListAt;
using ::MphRead::TestFlag;

namespace MphRead::Mods::Network
{
    std::array<bool, Entities::PlayerEntity::SlotCapacity> NetSlotManager::_activated{};

    void NetSlotManager::Reset()
    {
        _activated.fill(false);
        SharedContinuousWeaponPhase().Reset();
    }

    void NetSlotManager::Sync()
    {
        if (!NetSession::Active()
            || (NetSession::LocalSlot() < 0
                && !NetSession::IsServer()))
        {
            return;
        }

        for (std::int32_t slot = 0; slot < Entities::PlayerEntity::MaxPlayers(); slot++)
        {
            std::shared_ptr<Entities::PlayerEntity> player
                = slot < static_cast<std::int32_t>(Entities::PlayerEntity::Players().size())
                ? ManagedListAt(Entities::PlayerEntity::Players(), slot)
                : nullptr;
            if (player == nullptr)
            {
                continue;
            }

            const bool occupied = slot == NetSession::LocalSlot()
                || (slot < static_cast<std::int32_t>(SessionRoster::SlotCapacity)
                    && SessionSlots().IsOccupied(slot));

            // The final team roster can arrive after the match starts.
            // Correct active players as well as newly activated slots.
            if (occupied && ManagedAt(_activated, slot))
            {
                SyncTeam(*player, slot);
            }

            if (occupied && !ManagedAt(_activated, slot))
            {
                if ((Weapons::Current == nullptr))
                {
                    continue;
                }
                Activate(*player, slot);
            }
            else if (occupied && slot != NetSession::LocalSlot())
            {
                const MphRead::Hunter rosterHunter
                    = SessionSlots().HunterAt(static_cast<std::size_t>(slot));
                const MphRead::Hunter playerHunter = player->Hunter();
                if (rosterHunter != playerHunter)
                {
                    player->ModSetHunter(SessionSlots().HunterAt(static_cast<std::size_t>(slot)));
                    player->Initialize();

                    std::string consoleMessage = "[net] slot ";
                    consoleMessage += ::MphRead::NativeRuntime::ToString(slot);
                    consoleMessage += " is playing ";
                    consoleMessage += ::MphRead::ToString(player->Hunter());
                    NativeRuntime::ConsoleWriteLine(consoleMessage);

                    std::string logMessage = "slot ";
                    logMessage += ::MphRead::NativeRuntime::ToString(slot);
                    logMessage += " is playing ";
                    logMessage += ::MphRead::ToString(player->Hunter());
                    NetLog::Event(logMessage);
                }
            }
            else if (!occupied
                && ManagedAt(_activated, slot)
                && slot != NetSession::LocalSlot())
            {
                Deactivate(*player, slot);
            }
        }
    }

    void NetSlotManager::Activate(Entities::PlayerEntity& player, std::int32_t slot)
    {
        ManagedAt(_activated, slot) = true;

        player.SetLoadFlags(player.LoadFlags() | Entities::LoadFlags::SlotActive);
        player.SetLoadFlags(player.LoadFlags() | Entities::LoadFlags::Active);
        player.SetLoadFlags(player.LoadFlags() | Entities::LoadFlags::Initial);
        NetBotRoles::Assign(player);

        SyncTeam(player, slot);

        if (slot != NetSession::LocalSlot())
        {
            const MphRead::Hunter rosterHunter = SessionSlots().HunterAt(static_cast<std::size_t>(slot));
            const MphRead::Hunter playerHunter = player.Hunter();
            if (rosterHunter != playerHunter)
            {
                player.ModSetHunter(SessionSlots().HunterAt(static_cast<std::size_t>(slot)));
            }
        }

        player.Initialize();
        Entities::PlayerEntity::SetPlayerCount(CountActive());

        std::string consoleMessage = "[net] slot ";
        consoleMessage += ::MphRead::NativeRuntime::ToString(slot);
        consoleMessage += " activated (";
        consoleMessage += ManagedAt(GameState::Nicknames(), slot);
        consoleMessage += ") -- ";
        consoleMessage += ::MphRead::NativeRuntime::ToString(Entities::PlayerEntity::PlayerCount());
        consoleMessage += " player(s) in scene";
        NativeRuntime::ConsoleWriteLine(consoleMessage);

        std::string logMessage = "slot ";
        logMessage += ::MphRead::NativeRuntime::ToString(slot);
        logMessage += " activated (";
        logMessage += ManagedAt(GameState::Nicknames(), slot);
        logMessage += "), ";
        logMessage += ::MphRead::NativeRuntime::ToString(Entities::PlayerEntity::PlayerCount());
        logMessage += " player(s) in scene";
        NetLog::Event(logMessage);
    }

    std::int32_t NetSlotManager::CountActive()
    {
        std::int32_t count = 0;
        for (std::int32_t i = 0; i < Entities::PlayerEntity::MaxPlayers(); i++)
        {
            std::shared_ptr<Entities::PlayerEntity> player
                = i < static_cast<std::int32_t>(Entities::PlayerEntity::Players().size())
                ? ManagedListAt(Entities::PlayerEntity::Players(), i)
                : nullptr;
            if (player != nullptr
                && TestFlag(player->LoadFlags(), Entities::LoadFlags::Active))
            {
                count++;
            }
        }
        return count;
    }

    void NetSlotManager::ReleaseSlot(std::int32_t slot)
    {
        if (slot < 0
            || slot >= Entities::PlayerEntity::SlotCapacity
            || slot >= static_cast<std::int32_t>(Entities::PlayerEntity::Players().size())
            || !ManagedAt(_activated, slot))
        {
            return;
        }
        Deactivate(RequireReference(
            ManagedListAt(Entities::PlayerEntity::Players(), slot)), slot);
    }

    void NetSlotManager::SyncTeam(Entities::PlayerEntity& player, std::int32_t slot)
    {
        const std::int32_t wanted = GameState::Teams()
            ? SessionSlots().TeamAt(slot)
            : slot;
        if (wanted < 0 || (GameState::Teams() && wanted >= GameState::TeamCount())
            || player.TeamIndex() == wanted)
        {
            return;
        }
        player.SetTeamIndex(wanted);
        if (GameState::Teams())
        {
            Mods::Multiplayer::TeamVisuals::Apply(player);
        }
        else
        {
            player.SetTeam(Team::None);
        }
    }

    bool NetSlotManager::TeamIndexTaken(std::int32_t teamIndex, std::int32_t slot)
    {
        for (std::int32_t i = 0;
            i < Entities::PlayerEntity::MaxPlayers()
                && i < static_cast<std::int32_t>(Entities::PlayerEntity::Players().size());
            i++)
        {
            if (i == slot || !ManagedAt(_activated, i))
            {
                continue;
            }
            if (RequireReference(
                ManagedListAt(Entities::PlayerEntity::Players(), i)).TeamIndex()
                == teamIndex)
            {
                return true;
            }
        }
        return false;
    }

    void NetSlotManager::Deactivate(Entities::PlayerEntity& player, std::int32_t slot)
    {
        ManagedAt(_activated, slot) = false;

        NetPlayerLifecycle::OnSlotChanged(slot);
        NetScoreboard::ForgetSlot(slot);

        player.SetLoadFlags(player.LoadFlags() & ~Entities::LoadFlags::Active);
        player.SetLoadFlags(player.LoadFlags() & ~Entities::LoadFlags::Spawned);
        player.SetHealth(0);
        Entities::PlayerEntity::SetPlayerCount(std::max(CountActive(), 1));

        std::string consoleMessage = "[net] slot ";
        consoleMessage += ::MphRead::NativeRuntime::ToString(slot);
        consoleMessage += " deactivated -- player left";
        NativeRuntime::ConsoleWriteLine(consoleMessage);

        std::string logMessage = "slot ";
        logMessage += ::MphRead::NativeRuntime::ToString(slot);
        logMessage += " deactivated";
        NetLog::Event(logMessage);
    }
}
