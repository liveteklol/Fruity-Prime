#include "MatchStart.hpp"

#include "AdventureSave.hpp"
#include "GameFiles.hpp"
#include "LaunchPlan.hpp"
#include "LauncherPrefs.hpp"
#include "../../../Entities/Players/PlayerEntity.hpp"
#include "../../../Formats/Types.hpp"
#include "../../../GameState.hpp"
#include "../../../Menu.hpp"
#include "../../../Renderer.hpp"
#include "../../MapGen/CustomRooms.hpp"
#include "../../MapGen/MapCatalog.hpp"
#include "Mods/Network/Replay/DemoPlayback.hpp"
#include "Mods/Network/Integration/Launch/NetLaunch.hpp"
#include "Mods/Network/Session/NetSession.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace MphRead::Mods::Launcher
{
    void MatchStart::Launch(
        std::shared_ptr<MphRead::MenuSettings> settings,
        LaunchPlan plan)
    {
        MphRead::RenderWindow::LogCreatingWindow();
        MphRead::RenderWindow renderer;
        if (!Begin(renderer, std::move(settings), std::move(plan)))
        {
            return;
        }
        renderer.Run();
        AfterMatch();
    }

    bool MatchStart::Begin(
        MphRead::RenderWindow& window,
        std::shared_ptr<MphRead::MenuSettings> settings,
        LaunchPlan plan)
    {
        if (!GameFiles::Ready())
        {
            std::cout << "[launcher] no game files; nothing to load" << std::endl;
            return false;
        }

        GameFiles::ApplyPaths();

        if (plan.Kind() == LaunchKind::Adventure)
        {
            return BeginAdventure(window, std::move(plan));
        }
        if (plan.Kind() == LaunchKind::Demo)
        {
            return BeginDemo(window, std::move(plan));
        }

        MphRead::Menu::SaveSlot = 0;
        if (plan.Kind() == LaunchKind::Online || plan.Kind() == LaunchKind::Host)
        {
            std::optional<MphRead::Mods::Network::NetLaunchServerRoom> room
                = MphRead::Mods::Network::NetLaunch::ServerRoom();
            if (room.has_value())
            {
                if (!settings)
                {
                    throw System::NullReferenceException();
                }
                settings->RoomKey = room->RoomKey;
            }
            else
            {
                std::cout << "[net] no map reported by the server; loading the selected map instead"
                          << std::endl;
            }
        }

        std::string roomKey;
        if (plan.Kind() == LaunchKind::Offline)
        {
            if (!plan.RoomKey().has_value())
            {
                throw System::NullReferenceException();
            }
            roomKey = plan.RoomKey().value();
        }
        else
        {
            if (!settings)
            {
                throw System::NullReferenceException();
            }
            roomKey = settings->RoomKey;
        }

        if (roomKey.empty() || roomKey == "none")
        {
            return false;
        }

        // A server plays a map this device does not have: fetched now, from
        // FruityPrime's map server.
        if (MphRead::Mods::Network::NetSession::Active()
            && !MphRead::Mods::MapGen::MapCatalog::EnsureRoom(roomKey))
        {
            std::cout << "[launcher] the server plays " << roomKey
                      << ", which this device does not have and the map server could not give"
                      << std::endl;
            return false;
        }

        // Only the map about to be played, ahead of the others the background
        // is still building: a cartridge room, or a map built already, starts
        // at once.
        MphRead::Mods::MapGen::CustomRooms::EnsureReady(roomKey);
        std::optional<std::string> unplayable
            = MphRead::Mods::MapGen::CustomRooms::WhyUnplayable(roomKey);
        if (unplayable.has_value())
        {
            std::cout << "[launcher] " << unplayable.value() << std::endl;
            return false;
        }

        EnsureScene(window);
        MphRead::GameMode mode = plan.Mode();
        if (MphRead::Mods::Network::NetSession::Active())
        {
            std::optional<MphRead::Mods::Network::NetLaunchServerRoom> serverRoom
                = MphRead::Mods::Network::NetLaunch::ServerRoom();
            if (serverRoom.has_value())
            {
                mode = serverRoom->Mode;
            }
        }

        bool teamPlay;
        if (MphRead::Mods::Network::NetSession::Active())
        {
            teamPlay = MphRead::GameState::IsTeamMode(mode);
        }
        else
        {
            if (!settings)
            {
                throw System::NullReferenceException();
            }
            teamPlay = settings->TeamPlay == "on" || MphRead::GameState::IsTeamMode(plan.Mode());
        }

        if (MphRead::Mods::Network::NetSession::Active())
        {
            MphRead::Mods::Network::NetLaunch::DisableCheatsForMatch();
            MphRead::Mods::Network::NetLaunch::BuildPlayers(
                window.Scene(), plan.Hunter(), LauncherPrefs::LastColor(), teamPlay);
        }
        else
        {
            ApplyLocalRules(plan);
            AddLocalPlayers(window, std::move(plan), teamPlay);
        }

        window.AddRoom(roomKey, mode,
            MphRead::Mods::Network::NetSession::Active()
                ? MphRead::Mods::Network::NetLaunch::RoomPlayerCount()
                : 0);
        window.LoadScene();
        MphRead::Mods::Network::NetSession::MarkMatchLoaded();
        return true;
    }

    void MatchStart::ApplyLocalRules(const LaunchPlan& plan)
    {
        // Instagib is the server's to say online; offline it is the Play
        // screen's, and set every time so it never carries over from the last
        // match.
        if (plan.Kind() == LaunchKind::Offline)
        {
            MphRead::GameState::Instagib(plan.Instagib());
        }
    }

    void MatchStart::EnsureScene(MphRead::RenderWindow& window)
    {
        if (!window.HasScene())
        {
            (void)window.BeginScene();
        }
    }

    void MatchStart::AfterMatch()
    {
        if (MphRead::Mods::Network::DemoPlayback::IsActive())
        {
            MphRead::Mods::Network::DemoPlayback::Stop();
        }
        CommitAdventureSave();
    }

    bool MatchStart::BeginAdventure(MphRead::RenderWindow& window, LaunchPlan plan)
    {
        std::string roomKey = AdventureSave::Begin(plan.SaveSlot(), plan.NewGame());
        if (roomKey.empty())
        {
            std::cout << "[launcher] no adventure room to load" << std::endl;
            return false;
        }

        MphRead::GameState::Mode(MphRead::GameMode::SinglePlayer);
        EnsureScene(window);
        MphRead::Entities::PlayerEntity::SetMaxPlayers(4);
        window.AddPlayer(plan.Hunter(), LauncherPrefs::LastColor(), -1);
        window.AddRoom(roomKey, MphRead::GameMode::SinglePlayer);
        window.LoadScene();
        return true;
    }

    bool MatchStart::BeginDemo(MphRead::RenderWindow& window, LaunchPlan plan)
    {
        MphRead::Entities::PlayerEntity::SetMaxPlayers(
            MphRead::Entities::PlayerEntity::SlotCapacity);
        if (!plan.DemoPath().has_value())
        {
            throw System::ArgumentNullException("path");
        }
        if (!MphRead::Mods::Network::DemoPlayback::Join(plan.DemoPath().value()))
        {
            std::cout << "[demo] could not open or read the demo file" << std::endl;
            return false;
        }

        std::optional<MphRead::Mods::Network::NetLaunchServerRoom> room
            = MphRead::Mods::Network::NetLaunch::ServerRoom();
        if (!room.has_value())
        {
            std::cout << "[demo] the demo has no match info" << std::endl;
            MphRead::Mods::Network::DemoPlayback::Stop();
            return false;
        }

        MphRead::Menu::SaveSlot = 0;
        EnsureScene(window);
        MphRead::Mods::Network::NetLaunch::BuildPlayers(
            window.Scene(), MphRead::Hunter::Samus, 0,
            MphRead::GameState::IsTeamMode(room->Mode), -1);
        window.AddRoom(room->RoomKey, room->Mode,
            MphRead::Mods::Network::NetLaunch::RoomPlayerCount());
        window.LoadScene();
        return true;
    }

    void MatchStart::CommitAdventureSave()
    {
        if (MphRead::Menu::NeededSave != MphRead::SaveWhen::Never
            && MphRead::Menu::SaveSlot != 0)
        {
            MphRead::GameState::CommitSave();
        }
        MphRead::Menu::NeededSave = MphRead::SaveWhen::Never;
    }

    void MatchStart::AddLocalPlayers(
        MphRead::RenderWindow& renderer,
        LaunchPlan plan,
        bool teamPlay)
    {
        const std::int32_t bots = std::clamp(
            plan.Bots(), 0, MphRead::Entities::PlayerEntity::SlotCapacity - 1);
        MphRead::Entities::PlayerEntity::SetMaxPlayers(std::max(4, bots + 1));
        renderer.AddPlayer(plan.Hunter(), LauncherPrefs::LastColor(), teamPlay ? 0 : -1);
        for (std::int32_t i = 1; i <= bots; ++i)
        {
            const MphRead::Hunter hunter = static_cast<MphRead::Hunter>(
                (static_cast<std::int32_t>(plan.Hunter()) + i) % 7);
            renderer.AddPlayer(hunter, 0, teamPlay ? i % 2 : -1);
        }

        const std::int32_t level = std::clamp(plan.BotLevel(), 0, 3);
        const auto& players = MphRead::Entities::PlayerEntity::Players();
        for (std::size_t i = 0; i < players.size(); ++i)
        {
            const std::shared_ptr<MphRead::Entities::PlayerEntity>& player = players[i];
            if (!player)
            {
                throw System::NullReferenceException();
            }
            if (player->IsBot())
            {
                player->SetBotLevel(level);
            }
        }
    }
}
