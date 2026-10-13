#include "Mods/Network/Integration/Launch/NetConnectCommand.hpp"

#include "GameState.hpp"
#include "NativeRuntime/System/Console.hpp"
#include "Renderer.hpp"
#include "Scene.hpp"
#include "Mods/Network/Integration/Launch/NetLaunch.hpp"
#include "Mods/Network/Diagnostics/NetLog.hpp"
#include "Mods/Network/Session/NetSession.hpp"
#if defined(MPHREAD_SHELL)
#include "Mods/Launcher/GuiLauncher.hpp"
#include "Mods/Launcher/Shell.hpp"
#endif
#include "NativeRuntime/System/Managed.hpp"

#include <optional>
#include <string>

using ::MphRead::NativeRuntime::CSharpTryFinally;

namespace
{
}

namespace MphRead::Mods::Network
{
    void NetConnectCommand::Run(const std::string& host, std::int32_t port,
        const std::string& playerName, Hunter hunter, std::int32_t recolor)
    {
#if defined(MPHREAD_SHELL)
        // The launcher's own JOIN, typed: the match is played in the one
        // window, so Escape opens the pause menu there as it does in any match.
        if (!NetLaunch::Connect(host, port, playerName, hunter, 8000, recolor))
        {
            NativeRuntime::ConsoleWriteLine("[net] could not join: " + NetLaunch::LastJoinError());
            NetSession::Stop();
            return;
        }
        Launcher::LaunchPlan::Init init;
        init.Kind = Launcher::LaunchKind::Online;
        init.Hunter = hunter;
        init.PlayerName = playerName;
        init.RoomKey = "";
        init.Mode = GameMode::Battle;
        init.Port = port;
        Launcher::Gui::Shell::LaunchOnStart(Launcher::LaunchPlan(init));
        static_cast<void>(Launcher::Gui::GuiLauncher::TryRun());
        NetLog::Close();
#else
        if (!NetLaunch::Join(host, port, playerName, hunter, 8000, recolor))
        {
            NativeRuntime::ConsoleWriteLine("[net] could not join; giving up");
            NetSession::Stop();
            return;
        }

        CSharpTryFinally(
            [&]()
            {
                const NetLaunchServerRoom room = NetLaunch::ServerRoom().value();
                // `using var renderer = new RenderWindow();` -- the object is
                // disposed when this scope ends, which is before the outer
                // finally runs, exactly as C# orders them.
                RenderWindow renderer;
                NetLaunch::BuildPlayers(renderer.Scene(), hunter, recolor,
                    GameState::IsTeamMode(room.Mode));
                renderer.AddRoom(room.RoomKey, room.Mode, NetLaunch::RoomPlayerCount());
                NativeRuntime::ConsoleWriteLine(
                    "[net] loading " + room.RoomKey
                    + " (" + ::MphRead::ToString(room.Mode) + ")");
                renderer.Run();
            },
            []()
            {
                // The session owns a worker thread and a bound socket; a crash
                // in the game must not leave either behind.
                NetSession::Stop();
                NetLog::Close();
            });
#endif
    }
}
