#include "Mods/Network/Integration/Launch/NetLaunch.hpp"
#include "Mods/Network/Server/Lobby/LobbyRules.hpp"
#include "GameState.hpp"

#include "Features.hpp"
#include "NativeRuntime/System/Globalization.hpp"
#include "Program.hpp"
#include "Renderer.hpp"
#include "Scene.hpp"
#include "Mods/Launcher/Portable/MatchStart.hpp"
#include "Mods/Launcher/Portable/LaunchPlan.hpp"
#include "Mods/Launcher/Portable/LauncherPrefs.hpp"
#include "Mods/RespawnChoice.hpp"
#include "Mods/Network/Diagnostics/NetLog.hpp"
#include "Mods/Network/Session/NetSession.hpp"
#include "Mods/Network/Discovery/NetStatus.hpp"
#include "Mods/Network/Gameplay/Player/PlayerColors.hpp"
#include "Entities/Players/PlayerEntity.hpp"
#include "Formats/Formats.hpp"
#include "NativeRuntime/System/Console.hpp"
#include "Mods/Network/Protocol/NetProtocol.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace
{
    using Clock = std::chrono::steady_clock;

    std::int64_t ElapsedMilliseconds(const Clock::time_point& start)
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            Clock::now() - start).count();
    }

    double ElapsedSeconds(const Clock::time_point& start)
    {
        return std::chrono::duration<double>(Clock::now() - start).count();
    }

    std::string Endpoint(const std::string& address, std::int32_t port)
    {
        return address + ":" + std::to_string(port);
    }

    std::string JoinNames(const std::vector<std::string>& names)
    {
        std::string result;
        for (std::size_t index = 0; index < names.size(); ++index)
        {
            if (index != 0)
            {
                result += ", ";
            }
            result += names[index];
        }
        return result;
    }
}

namespace MphRead::Mods::Network
{
    std::string NetLaunch::_lastJoinError;
    bool NetLaunch::_terminalLobby = false;
    std::string NetLaunch::_terminalInput{};

    bool NetLaunch::Connect(const std::string& address, std::int32_t port,
        const std::string& playerName, Hunter hunter,
        std::int32_t timeoutMs, std::int32_t color,
        NativeRuntime::Guid ownerToken, std::stop_token cancellationToken)
    {
        if (cancellationToken.stop_requested())
        {
            _lastJoinError = "Join cancelled.";
            return false;
        }
        NetSession::SetPlayerName(playerName);
        NetSession::SetLocalHunter(Launcher::Hunters::Resolve(hunter));
        NetSession::SetLocalColor(PlayerColors::Clamp(
            color < 0 ? Launcher::LauncherPrefs::LastColor() : color));
        Entities::PlayerEntity::SetMaxPlayers(Entities::PlayerEntity::SlotCapacity);
        NetSession::StartClient(address, port, ownerToken);
        if (!NetSession::Active())
        {
            _lastJoinError = "Could not open a socket for " + Endpoint(address, port) + ".";
            return false;
        }

        const Clock::time_point clock = Clock::now();
        std::int32_t lastIdentify = 0;
        while (ElapsedMilliseconds(clock) < timeoutMs)
        {
            if (cancellationToken.stop_requested())
            {
                NetSession::Stop();
                _lastJoinError = "Join cancelled.";
                return false;
            }
            NetSession::Update(ElapsedSeconds(clock));
            if (NetSession::Refused())
            {
                _lastJoinError = DescribeJoinFailure(address, port);
                NativeRuntime::ConsoleWriteLine(("[net] " + _lastJoinError));
                return false;
            }

            const std::int32_t localSlot = NetSession::LocalSlot();
            if (localSlot >= 0 && NetSession::ServerSession().has_value()
                && SessionSlots().IsOccupied(localSlot))
            {
                NativeRuntime::ConsoleWriteLine("[net] connected in " + ToString(NetSession::CurrentSessionPhase())
                    + ", slot " + std::to_string(localSlot));
                return true;
            }

            if (ElapsedMilliseconds(clock) - lastIdentify > 500)
            {
                lastIdentify = static_cast<std::int32_t>(ElapsedMilliseconds(clock));
                NetSession::SendIdentify();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        if (cancellationToken.stop_requested())
        {
            NetSession::Stop();
            _lastJoinError = "Join cancelled.";
            return false;
        }

        _lastJoinError = DescribeJoinFailure(address, port);
        NativeRuntime::ConsoleWriteLine(("[net] " + _lastJoinError));
        return false;
    }

    bool NetLaunch::Join(const std::string& address, std::int32_t port,
        const std::string& playerName, Hunter hunter,
        std::int32_t timeoutMs, std::int32_t color)
    {
        if (!Connect(address, port, playerName, hunter, timeoutMs, color))
        {
            return false;
        }
        _terminalLobby = NetSession::PersistentLobby();
        if (!NetSession::ShouldLoadMatch())
        {
            NativeRuntime::ConsoleWriteLine(
                "Connected to lobby. Waiting for the lobby owner to start... Commands: ready, start, leave.");
        }
        while (NetSession::Active() && !NetSession::ShouldLoadMatch())
        {
            NetSession::Pump();
            if (NetSession::Refused() || NetSession::SessionTimedOut())
            {
                NetSession::Stop();
                return false;
            }
            PollTerminalInput();
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        DisableCheatsForMatch();
        return NetSession::Active();
    }

    void NetLaunch::PollTerminalInput()
    {
        if (NativeRuntime::ConsoleIsInputRedirected() || !NativeRuntime::ConsoleKeyAvailable())
        {
            return;
        }
        const NativeRuntime::ConsoleKeyInfo key = NativeRuntime::ConsoleReadKeyInfo(true);
        if (key.Key == NativeRuntime::ConsoleKey::Enter)
        {
            const std::string command = NativeRuntime::ToLowerInvariant(NativeRuntime::StringTrim(_terminalInput));
            _terminalInput.clear();
            if (command == "leave")
            {
                NetSession::Stop();
            }
            if (command == "ready")
            {
                static_cast<void>(NetSession::SendLobbyCommand(LobbyCommandType::SetReady, 255, -1, true));
            }
            if (command == "start")
            {
                static_cast<void>(NetSession::SendLobbyCommand(LobbyCommandType::StartMatch));
            }
        }
        else if (key.Key == NativeRuntime::ConsoleKey::Backspace && !_terminalInput.empty())
        {
            // _terminalInput[..^1]: one UTF-16 unit, which for what can be
            // typed here is one character.
            _terminalInput.resize(_terminalInput.size() - NativeRuntime::DecodeLastUtf8Scalar(_terminalInput, _terminalInput.size()).Length);
        }
        else if (!NativeRuntime::CharIsControl(key.KeyChar) && NativeRuntime::Utf16Length(_terminalInput) < 16)
        {
            NativeRuntime::AppendUtf8(_terminalInput, key.KeyChar);
        }
    }

    bool NetLaunch::TickTerminalLobby(RenderWindow& window)
    {
        if (!_terminalLobby)
        {
            return false;
        }
        if (NetSession::PersistentLobby() && NetSession::IsInLobby() && window.HasScene())
        {
            window.EndScene();
            NetSession::ResetMatchState();
            NativeRuntime::ConsoleWriteLine(
                "Returned to lobby. Commands: ready, start, leave.");
        }
        if (window.HasScene())
        {
            return false;
        }
        NetSession::Pump();
        PollTerminalInput();
        if (!NetSession::Active() || NetSession::Refused() || NetSession::SessionTimedOut())
        {
            _terminalLobby = false;
            NetSession::Stop();
            window.Close();
            return true;
        }
        if (NetSession::ShouldLoadMatch())
        {
            const std::optional<MatchDefinition> match = NetSession::ActiveMatchDefinition();
            if (match.has_value())
            {
                try
                {
                    Launcher::LaunchPlan::Init init;
                    init.Kind = Launcher::LaunchKind::Online;
                    init.RoomKey = match->RoomKey;
                    init.Mode = match->Mode;
                    init.Hunter = NetSession::LocalHunter();
                    init.PlayerName = NetSession::PlayerName();
                    Launcher::LaunchPlan plan(init);
                    if (!Launcher::MatchStart::Begin(window,
                        GameState::LoadSettings(), std::move(plan)))
                    {
                        throw ProgramException("The map could not be loaded.");
                    }
                }
                catch (const std::exception& ex)
                {
                    NetSession::ReportMatchLoadFailed(ex.what());
                    NetSession::Stop();
                    window.Close();
                }
            }
        }
        return !window.HasScene();
    }

    const std::string& NetLaunch::LastJoinError()
    {
        return _lastJoinError;
    }

    std::string NetLaunch::DescribeJoinFailure(const std::string& address,
        std::int32_t port)
    {
        ::MphRead::Mods::Network::ServerStatus status
            = NetStatus::Query(address, port, false, 1500);
        const std::string endpoint = Endpoint(address, port);
        if (!status.Online)
        {
            return "No answer from " + endpoint + ". The server may be off, "
                "or UDP may be blocked between here and it.";
        }
        if (status.MaxPlayers > 0 && status.Players >= status.MaxPlayers)
        {
            return endpoint + " is full (" + std::to_string(status.Players) + "/"
                + std::to_string(status.MaxPlayers)
                + " players). Try again when somebody leaves.";
        }
        if (status.Protocol > 0 && status.Protocol != NetConfig::ProtocolVersion)
        {
            return endpoint + " is running protocol " + std::to_string(status.Protocol)
                + " and this build speaks "
                + std::to_string(NetConfig::ProtocolVersion)
                + ". One of you needs updating.";
        }
        return endpoint + " answered, but would not admit this client ("
            + std::to_string(status.Players) + "/" + std::to_string(status.MaxPlayers)
            + " players). It may have filled up while joining.";
    }

    void NetLaunch::DisableCheatsForMatch()
    {
        std::vector<std::string> turnedOff;
        for (const Cheats::BooleanProperty& property : Cheats::BooleanProperties())
        {
            if (!false
                || !false
                || !false)
            {
                continue;
            }
            if (property.Get())
            {
                turnedOff.push_back(property.Name);
                property.Set(false);
            }
        }
        if (!turnedOff.empty())
        {
            const std::string list = JoinNames(turnedOff);
            NativeRuntime::ConsoleWriteLine(("[net] cheats are off while connected (" + list + ")"));
            NetLog::Event(
                "cheats disabled for this session: " + list);
        }
    }

    std::optional<NetLaunchServerRoom> NetLaunch::ServerRoom()
    {
        if (const std::optional<MatchDefinition> definition = NetSession::ActiveMatchDefinition(); definition.has_value())
        {
            return NetLaunchServerRoom{definition->RoomKey.value_or(""), definition->Mode};
        }
        std::optional<MatchStatePacket> state
            = NetSession::ServerMatch();
        if (!state.has_value() || state->RoomKey.value().length() == 0)
        {
            return std::nullopt;
        }
        const GameMode mode = ::MphRead::IsDefinedGameMode(state->Mode)
            ? static_cast<GameMode>(state->Mode)
            : GameMode::Battle;
        return NetLaunchServerRoom{state->RoomKey.value(), mode};
    }

    Multiplayer::MatchWorldProfile NetLaunch::WorldProfile()
    {
        const std::optional<SessionStatePacket>& state = NetSession::ServerSession();
        return state.has_value() && state->WorldProfile.IsValid()
            ? state->WorldProfile : Multiplayer::MatchWorldProfile::Resolve(2);
    }

    std::int32_t NetLaunch::RoomPlayerCount()
    {
        return WorldProfile().EntityLayerPlayers;
    }

    void NetLaunch::BuildPlayers(Scene& scene, Hunter localHunter,
        std::int32_t localRecolor, bool teams,
        std::optional<std::int32_t> localSlot)
    {
        const std::optional<MatchDefinition> match = NetSession::ActiveMatchDefinition();
        GameState::TeamCount(teams && match.has_value() ? LobbyRules::TeamCount(*match) : teams ? 2 : 0);
        std::int32_t resolvedSlot;
        if (localSlot.has_value())
        {
            resolvedSlot = localSlot.value();
        }
        else
        {
            resolvedSlot = std::max(NetSession::LocalSlot(), 0);
        }

        for (std::int32_t slot = 0;
            slot < Entities::PlayerEntity::MaxPlayers(); ++slot)
        {
            Hunter hunter = slot == resolvedSlot
                ? localHunter
                : SessionSlots().HunterAt(slot);
            if (slot == resolvedSlot && slot >= 0
                && slot < static_cast<std::int32_t>(PlayerColors::SlotCount()))
            {
                PlayerColors::SetChoice(slot, PlayerColors::Clamp(localRecolor));
            }
            const std::int32_t recolor = slot == resolvedSlot ? localRecolor : 0;
            const std::int32_t teamIndex = teams
                ? std::max(0, static_cast<std::int32_t>(SessionSlots().TeamAt(static_cast<std::int32_t>(slot)))) : -1;
            scene.AddPlayer(hunter, recolor, teamIndex);
        }

        for (std::int32_t slot = 0;
            slot < Entities::PlayerEntity::MaxPlayers(); ++slot)
        {
            std::optional<std::shared_ptr<Entities::PlayerEntity>> player;
            if (slot < static_cast<std::int32_t>(Entities::PlayerEntity::Players().size()))
            {
                player = Entities::PlayerEntity::Players().at(static_cast<std::size_t>(slot));
            }
            if (!player.has_value())
            {
                continue;
            }
            (player.value())->SetIsBot(false);
            (player.value())->SetBotLevel(0);
            if (slot == resolvedSlot)
            {
                continue;
            }
            bool occupied = false;
            if (slot < static_cast<std::int32_t>(SessionRoster::SlotCapacity))
            {
                occupied = SessionSlots().IsOccupied(slot);
            }
            if (!occupied)
            {
                (player.value())->SetLoadFlags((player.value())->LoadFlags() & ~Entities::LoadFlags::Active);
            }
        }

        Entities::PlayerEntity::SetPlayerCount(1);
        const std::int32_t mainIndex = resolvedSlot >= 0 ? resolvedSlot : 0;
        Entities::PlayerEntity::SetMainPlayerIndex(mainIndex);
        PlayerColors::Resolve();
        Mods::RespawnChoice::Reset();
        NativeRuntime::ConsoleWriteLine(("[net] player slots built, main player = slot " + std::to_string(mainIndex)));
        NetLog::Event(
            "player slots built, main = slot " + std::to_string(mainIndex));
    }
}
