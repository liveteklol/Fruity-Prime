#include "AndroidMatch.hpp"

#if !defined(__ANDROID__)
#error "AndroidMatch is only valid for the Android native target."
#endif

#include "AndroidInput.hpp"
#include "AndroidMaps.hpp"

#include "../MphRead.Native/Entities/Players/PlayerEntity.hpp"
#include "../MphRead.Native/Formats/Enums.hpp"
#include "../MphRead.Native/Formats/Types.hpp"
#include "../MphRead.Native/GameState.hpp"
#include "../MphRead.Native/Menu.hpp"
#include "../MphRead.Native/Program.hpp"
#include "../MphRead.Native/Renderer.hpp"
#include "../MphRead.Native/Scene.hpp"
#include "../MphRead.Native/Mods/Launcher/Portable/AdventureSave.hpp"
#include "../MphRead.Native/Mods/Launcher/Portable/GameFiles.hpp"
#include "../MphRead.Native/Mods/Launcher/Portable/LaunchPlan.hpp"
#include "../MphRead.Native/Mods/Launcher/Portable/MatchStart.hpp"
#include "../MphRead.Native/Mods/MapGen/MapCatalog.hpp"
#include "Mods/Network/Replay/DemoPlayback.hpp"
#include "Mods/Network/Integration/Launch/NetLaunch.hpp"
#include "Mods/Network/Session/NetSession.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <string>

namespace MphRead::Droid
{
    std::unique_ptr<MphRead::Scene> AndroidMatch::Build(
        AndroidInput& input,
        OpenTK::Mathematics::Vector2i size,
        MphRead::Mods::Launcher::LaunchPlan plan,
        std::function<void()> close)
    {
        using MphRead::Mods::Launcher::GameFiles;
        using MphRead::Mods::Launcher::LaunchKind;
        using MphRead::Mods::Network::NetLaunch;
        using MphRead::Mods::Network::NetSession;

        GameFiles::ApplyPaths();
        AndroidMaps::EnsureBuilt();

        if (plan.Kind() == LaunchKind::Demo)
        {
            return BuildDemo(input, size, plan, close);
        }
        if (plan.Kind() == LaunchKind::Adventure)
        {
            return BuildAdventure(input, size, plan, close);
        }

        MphRead::Menu::SaveSlot = 0;
        auto scene = std::make_unique<MphRead::Scene>(
            size,
            input.Keyboard(),
            input.Mouse(),
            [](std::string)
            {
            },
            close);

        if (NetSession::Active())
        {
            NetLaunch::DisableCheatsForMatch();
            BuildNetworkedMatch(*scene, plan);
        }
        else
        {
            MphRead::Mods::Launcher::MatchStart::ApplyLocalRules(plan);
            AddLocalPlayers(
                *scene,
                plan,
                MphRead::GameState::IsTeamMode(plan.Mode()));

            if (!plan.RoomKey().has_value())
            {
                throw System::NullReferenceException();
            }
            scene->AddRoom(plan.RoomKey().value(), plan.Mode());
        }
        return scene;
    }

    std::unique_ptr<MphRead::Scene> AndroidMatch::BuildDemo(
        AndroidInput& input,
        OpenTK::Mathematics::Vector2i size,
        MphRead::Mods::Launcher::LaunchPlan plan,
        std::function<void()> close)
    {
        using MphRead::Entities::PlayerEntity;
        using MphRead::Mods::Network::DemoPlayback;
        using MphRead::Mods::Network::NetLaunch;

        PlayerEntity::SetMaxPlayers(PlayerEntity::SlotCapacity);

        if (!plan.DemoPath().has_value())
        {
            throw System::ArgumentNullException("path");
        }
        if (!DemoPlayback::Join(plan.DemoPath().value()))
        {
            std::optional<std::string> error = DemoPlayback::LastError();
            throw MphRead::ProgramException(
                error.value_or("That file could not be read as a demo."));
        }

        std::optional<MphRead::Mods::Network::NetLaunchServerRoom> room
            = NetLaunch::ServerRoom();
        if (!room.has_value())
        {
            DemoPlayback::Stop();
            throw MphRead::ProgramException("The demo has no match info in it.");
        }

        MphRead::Menu::SaveSlot = 0;
        auto scene = std::make_unique<MphRead::Scene>(
            size,
            input.Keyboard(),
            input.Mouse(),
            [](std::string)
            {
            },
            close);

        NetLaunch::BuildPlayers(
            *scene,
            MphRead::Hunter::Samus,
            0,
            MphRead::GameState::IsTeamMode(room->Mode),
            std::optional<std::int32_t>{-1});

        scene->AddRoom(
            room->RoomKey,
            room->Mode,
            NetLaunch::RoomPlayerCount());

        std::cout
            << "[match] demo, "
            << room->RoomKey
            << std::endl;
        return scene;
    }

    std::unique_ptr<MphRead::Scene> AndroidMatch::BuildAdventure(
        AndroidInput& input,
        OpenTK::Mathematics::Vector2i size,
        MphRead::Mods::Launcher::LaunchPlan plan,
        std::function<void()> close)
    {
        using MphRead::Entities::PlayerEntity;
        using MphRead::Mods::Launcher::AdventureSave;

        std::string roomKey
            = AdventureSave::Begin(plan.SaveSlot(), plan.NewGame());
        if (roomKey.empty())
        {
            throw MphRead::ProgramException(
                "That save slot does not name a room to load.");
        }

        MphRead::GameState::Mode(MphRead::GameMode::SinglePlayer);
        PlayerEntity::SetMaxPlayers(4);

        auto scene = std::make_unique<MphRead::Scene>(
            size,
            input.Keyboard(),
            input.Mouse(),
            [](std::string)
            {
            },
            close);

        scene->AddPlayer(plan.Hunter(), 0, -1);
        scene->AddRoom(roomKey, MphRead::GameMode::SinglePlayer);

        std::cout
            << "[match] adventure, slot "
            << static_cast<std::int32_t>(plan.SaveSlot())
            << ", "
            << (plan.NewGame() ? "new game" : "continued")
            << ", room "
            << roomKey
            << std::endl;
        return scene;
    }

    void AndroidMatch::Finish()
    {
        MphRead::Mods::Launcher::MatchStart::CommitAdventureSave();
    }

    void AndroidMatch::BuildNetworkedMatch(
        MphRead::Scene& scene,
        MphRead::Mods::Launcher::LaunchPlan plan)
    {
        using MphRead::Mods::Network::NetLaunch;
        using MphRead::Mods::Network::NetLaunchServerRoom;

        std::optional<NetLaunchServerRoom> room = NetLaunch::ServerRoom();

        std::optional<std::string> roomKey;
        if (room.has_value())
        {
            roomKey = room->RoomKey;
        }
        else
        {
            roomKey = plan.RoomKey();
        }

        MphRead::GameMode mode
            = room.has_value() ? room->Mode : plan.Mode();

        if (!roomKey.has_value())
        {
            throw System::NullReferenceException();
        }
        if (roomKey->empty())
        {
            throw MphRead::ProgramException(
                "The server did not say which map it is running.");
        }

        // The server plays a map this device does not have: fetched now.
        if (!MphRead::Mods::MapGen::MapCatalog::EnsureRoom(roomKey.value()))
        {
            throw MphRead::ProgramException(
                "The server plays " + roomKey.value()
                + ", which this device does not have and the map server could not give.");
        }

        NetLaunch::BuildPlayers(
            scene,
            plan.Hunter(),
            0,
            MphRead::GameState::IsTeamMode(mode));

        scene.AddRoom(
            roomKey.value(),
            mode,
            NetLaunch::RoomPlayerCount());
    }

    void AndroidMatch::AddLocalPlayers(
        MphRead::Scene& scene,
        MphRead::Mods::Launcher::LaunchPlan plan,
        bool teamPlay)
    {
        using MphRead::Entities::PlayerEntity;

        const std::int32_t bots = std::clamp(
            plan.Bots(),
            0,
            PlayerEntity::SlotCapacity - 1);

        PlayerEntity::SetMaxPlayers(std::max(4, bots + 1));

        scene.AddPlayer(
            plan.Hunter(),
            0,
            teamPlay ? 0 : -1);

        for (std::int32_t i = 1; i <= bots; ++i)
        {
            const MphRead::Hunter hunter = static_cast<MphRead::Hunter>(
                (static_cast<std::int32_t>(plan.Hunter()) + i) % 7);

            scene.AddPlayer(
                hunter,
                0,
                teamPlay ? i % 2 : -1);
        }

        const std::int32_t level = std::clamp(
            plan.BotLevel(),
            0,
            3);

        const auto& players = PlayerEntity::Players();
        for (std::size_t i = 0; i < players.size(); ++i)
        {
            const std::shared_ptr<PlayerEntity>& player = players[i];
            if (!player)
            {
                throw System::NullReferenceException();
            }
            if (player->IsBot())
            {
                player->SetBotLevel(level);
            }
        }

        const std::int32_t mainPlayerIndex
            = PlayerEntity::MainPlayerIndex();
        const std::shared_ptr<PlayerEntity> main = PlayerEntity::Main();
        if (!main)
        {
            throw System::NullReferenceException();
        }

        std::cout
            << "[match] local match, main player = slot "
            << mainPlayerIndex
            << ", bot = "
            << (main->IsBot() ? "True" : "False")
            << std::endl;
    }
}
