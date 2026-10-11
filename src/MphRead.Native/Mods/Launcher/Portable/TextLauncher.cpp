#include "TextLauncher.hpp"

#include "../../../Renderer.hpp"
#include "../../WindowMode.hpp"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "AdventureSave.hpp"
#include "GameFiles.hpp"
#include "LaunchPlan.hpp"
#include "LauncherPrefs.hpp"
#include "MatchStart.hpp"
#include "SetupProgress.hpp"
#include "../../../Formats/Enums.hpp"
#include "../../../Formats/Types.hpp"
#include "../../../GameState.hpp"
#include "../../../Menu.hpp"
#include "../../DebugLog.hpp"
#include "../../Branding.hpp"
#include "../../Credits.hpp"
#include "../../GameSettings.hpp"
#include "../../ThumbnailBatch.hpp"
#include "../../ThumbnailGenerator.hpp"
#include "../../Network/NetHostSession.hpp"
#include "../../Network/NetLaunch.hpp"
#include "../../Network/NetMaster.hpp"
#include "../../Network/NetSession.hpp"
#include "../../Network/NetStatus.hpp"
#include "../../Update/Updater.hpp"
#include "../../WindowMode.hpp"
#include "../../../NativeRuntime/System/Encoding.hpp"
#include "../../../NativeRuntime/System/Globalization.hpp"
#include "../../../NativeRuntime/System/IO.hpp"
#include "NativeRuntime/System/Globalization.hpp"
#include "../../../NativeRuntime/System/Console.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <climits>
#include <cstdio>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <limits>
#include <locale>
#include <memory>
#include <cwchar>
#include <cwctype>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <io.h>
#include <windows.h>
#else
#include <wctype.h>
#include <sys/stat.h>
#if !defined(__ANDROID__)
#include <execinfo.h>
#endif
#include <unistd.h>
#endif

using ::MphRead::NativeRuntime::AppendUtf8;
using ::MphRead::NativeRuntime::DecodeUtf8Scalar;
using ::MphRead::NativeRuntime::FileExists;
using ::MphRead::NativeRuntime::StringTrim;
using ::MphRead::NativeRuntime::Utf16Length;
using ::MphRead::NativeRuntime::Utf8Scalar;

namespace MphRead::GameStateDetail
{
    [[nodiscard]] GameMode GameModeBattle();
    [[nodiscard]] GameMode GameModeBattleTeams();
    [[nodiscard]] GameMode GameModeSurvival();
    [[nodiscard]] GameMode GameModeSurvivalTeams();
    [[nodiscard]] GameMode GameModeCapture();
    [[nodiscard]] GameMode GameModeBounty();
    [[nodiscard]] GameMode GameModeBountyTeams();
    [[nodiscard]] GameMode GameModeNodes();
    [[nodiscard]] GameMode GameModeNodesTeams();
    [[nodiscard]] GameMode GameModeDefender();
    [[nodiscard]] GameMode GameModeDefenderTeams();
    [[nodiscard]] GameMode GameModePrimeHunter();
}

namespace MphRead::Mods::Launcher
{
    struct TextLauncherAccess final
    {
        static void SetInputEnded() noexcept { TextLauncher::_inputEnded = true; }
    };
}

namespace
{
    // Console.IsOutputRedirected.
    [[nodiscard]] bool ConsoleIsOutputRedirected() noexcept
    {
#if defined(_WIN32)
        const HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD mode = 0;
        return handle == nullptr || handle == INVALID_HANDLE_VALUE
            || GetConsoleMode(handle, &mode) == FALSE;
#else
        return ::isatty(STDOUT_FILENO) == 0;
#endif
    }
}

namespace
{
    using MphRead::GameMode;
    using MphRead::Hunter;
    using MphRead::MenuSettings;
    using MphRead::Mods::Launcher::AdventureSave;
    using MphRead::Mods::Launcher::LaunchKind;
    using MphRead::Mods::Launcher::LaunchPlan;
    using MphRead::Mods::Launcher::LauncherPrefs;
    using MphRead::Mods::Network::HostedGame;
    using MphRead::Mods::Network::MasterListResult;
    using MphRead::Mods::Network::MasterListing;
    using MphRead::Mods::Network::ServerStatus;
    using MphRead::Mods::Update::UpdateInfo;

    constexpr std::int32_t PlayerSlotCapacity
        = MphRead::Entities::PlayerEntity::SlotCapacity;

    [[nodiscard]] GameMode Battle() { return GameMode::Battle; }
    [[nodiscard]] GameMode BattleTeams() { return GameMode::BattleTeams; }
    [[nodiscard]] GameMode Survival() { return GameMode::Survival; }
    [[nodiscard]] GameMode SurvivalTeams() { return GameMode::SurvivalTeams; }
    [[nodiscard]] GameMode Capture() { return GameMode::Capture; }
    [[nodiscard]] GameMode Bounty() { return GameMode::Bounty; }
    [[nodiscard]] GameMode BountyTeams() { return GameMode::BountyTeams; }
    [[nodiscard]] GameMode Nodes() { return GameMode::Nodes; }
    [[nodiscard]] GameMode NodesTeams() { return GameMode::NodesTeams; }
    [[nodiscard]] GameMode Defender() { return GameMode::Defender; }
    [[nodiscard]] GameMode DefenderTeams() { return GameMode::DefenderTeams; }
    [[nodiscard]] GameMode PrimeHunter() { return GameMode::PrimeHunter; }

    [[nodiscard]] std::string TrimQuotes(std::string value)
    {
        std::size_t first = 0;
        while (first < value.size() && (value[first] == '\"' || value[first] == '\''))
        {
            ++first;
        }
        std::size_t last = value.size();
        while (last > first && (value[last - 1] == '\"' || value[last - 1] == '\''))
        {
            --last;
        }
        return value.substr(first, last - first);
    }

    [[nodiscard]] std::string_view TrimNumberWhitespace(std::string_view text) noexcept
    {
        auto isWhite = [](char value) noexcept
        {
            const unsigned char ch = static_cast<unsigned char>(value);
            return ch == 0x20U || (ch >= 0x09U && ch <= 0x0DU);
        };
        while (!text.empty() && isWhite(text.front()))
        {
            text.remove_prefix(1);
        }
        while (!text.empty() && isWhite(text.back()))
        {
            text.remove_suffix(1);
        }
        return text;
    }

    [[nodiscard]] bool TryParseInt32Invariant(std::string_view text, std::int32_t& value) noexcept
    {
        // System.Int32 parsing ignores terminating U+0000 characters before
        // applying NumberStyles.Integer whitespace/sign handling.
        while (!text.empty() && text.back() == '\0')
        {
            text.remove_suffix(1);
        }
        text = TrimNumberWhitespace(text);
        if (text.empty())
        {
            return false;
        }
        bool negative = false;
        std::size_t position = 0;
        if (text[position] == '+' || text[position] == '-')
        {
            negative = text[position] == '-';
            ++position;
            if (position == text.size())
            {
                return false;
            }
        }

        const std::uint64_t limit = negative
            ? static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max()) + 1U
            : static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max());
        std::uint64_t magnitude = 0;
        for (; position < text.size(); ++position)
        {
            const char ch = text[position];
            if (ch < '0' || ch > '9')
            {
                return false;
            }
            const std::uint64_t digit = static_cast<unsigned int>(ch - '0');
            if (magnitude > (limit - digit) / 10U)
            {
                return false;
            }
            magnitude = magnitude * 10U + digit;
        }

        if (negative)
        {
            if (magnitude == static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max()) + 1U)
            {
                value = std::numeric_limits<std::int32_t>::min();
            }
            else
            {
                value = -static_cast<std::int32_t>(magnitude);
            }
        }
        else
        {
            value = static_cast<std::int32_t>(magnitude);
        }
        return true;
    }

    [[nodiscard]] std::string PadRight(std::string value, std::size_t width)
    {
        const std::size_t length = Utf16Length(value);
        if (length < width)
        {
            value.append(width - length, ' ');
        }
        return value;
    }

    [[nodiscard]] std::string PadLeftInt(std::int32_t value, std::size_t width)
    {
        std::string text = std::to_string(value);
        if (text.size() < width)
        {
            text.insert(0, width - text.size(), ' ');
        }
        return text;
    }

    [[nodiscard]] bool OutputRedirected() noexcept
    {
        return ConsoleIsOutputRedirected();
    }

    void WriteExceptionStackTrace(const std::exception& exception)
    {
        ::MphRead::NativeRuntime::ConsoleWriteLineNullable(
            ::MphRead::Mods::DebugLog::ExceptionStackTrace(exception));
    }

    [[nodiscard]] UpdateInfo NullableValue(std::optional<UpdateInfo> value)
    {
        if (!value.has_value())
        {
            throw MphRead::Mods::Update::InvalidOperationException(
                "Nullable object must have a value.");
        }
        return *value;
    }

    [[nodiscard]] const std::vector<MasterListing>& Servers(const MasterListResult& result)
    {
        if (!result.Servers)
        {
            throw System::NullReferenceException();
        }
        return *result.Servers;
    }

    [[nodiscard]] std::string Ask(const std::string& prompt, const std::string& fallback)
    {
        using ::MphRead::Mods::Launcher::TextLauncherAccess;
        if (!fallback.empty())
        {
            std::cout << prompt << " [" << fallback << "]: ";
        }
        else
        {
            std::cout << prompt << ": ";
        }
        std::cout.flush();
        const std::optional<std::string> line
            = MphRead::NativeRuntime::ConsoleReadLine();
        if (!line.has_value())
        {
            TextLauncherAccess::SetInputEnded();
            std::cout << '\n';
            return fallback;
        }
        const std::string trimmed = StringTrim(*line);
        return trimmed.empty() ? fallback : trimmed;
    }

    [[nodiscard]] bool ParseEndpoint(std::string text, std::string& host, std::int32_t& port)
    {
        text = StringTrim(text);
        if (text.empty())
        {
            return false;
        }
        const std::size_t colon = text.rfind(':');
        if (colon == std::string::npos || colon == 0)
        {
            host = std::move(text);
            return true;
        }
        std::int32_t parsed = 0;
        if (!TryParseInt32Invariant(std::string_view(text).substr(colon + 1), parsed)
            || parsed < 1 || parsed > 65535)
        {
            return false;
        }
        host = text.substr(0, colon);
        port = parsed;
        return true;
    }

    [[nodiscard]] std::string Describe(ServerStatus status)
    {
        const std::string players = status.MaxPlayers > 0
            ? std::to_string(status.Players) + "/" + std::to_string(status.MaxPlayers)
            : std::to_string(status.Players);
        const std::string ping = status.Latency >= 0
            ? std::to_string(status.Latency) + " ms"
            : "-- ms";
        return status.RoomKey + " ("
            + MphRead::Mods::Network::NetStatus::ModeName(status.Mode)
            + ") " + players + " " + ping;
    }

    [[nodiscard]] Hunter AskHunter()
    {
        std::vector<Hunter> hunters{
            Hunter::Samus, Hunter::Kanden, Hunter::Trace, Hunter::Sylux,
            Hunter::Noxus, Hunter::Spire, Hunter::Weavel, Hunter::Random
        };

        const Hunter remembered = LauncherPrefs::LastHunter();
        auto found = std::find(hunters.begin(), hunters.end(), remembered);
        const std::int32_t current = found == hunters.end()
            ? 0
            : static_cast<std::int32_t>(std::distance(hunters.begin(), found));

        std::cout << '\n' << "  ";
        for (std::size_t index = 0; index < hunters.size(); ++index)
        {
            if (index != 0)
            {
                std::cout << "  ";
            }
            std::cout << '[' << index + 1 << "] " << ::MphRead::ToString(hunters[index]);
        }
        std::cout << '\n';

        const std::string answer = Ask("  Hunter", std::to_string(current + 1));
        std::int32_t index = 0;
        const Hunter hunter = TryParseInt32Invariant(answer, index)
                && index >= 1 && index <= static_cast<std::int32_t>(hunters.size())
            ? hunters[static_cast<std::size_t>(index - 1)]
            : LauncherPrefs::LastHunter();
        LauncherPrefs::LastHunter(hunter);
        return hunter;
    }

    [[nodiscard]] std::string AskName()
    {
        std::string name = Ask("  Your name", LauncherPrefs::PlayerName());
        if (name.empty())
        {
            name = LauncherPrefs::PlayerName();
        }
        LauncherPrefs::PlayerName(name);
        return name;
    }

    [[nodiscard]] bool AskYesNo(const std::string& prompt, bool current)
    {
        const std::string answer = ::MphRead::NativeRuntime::ToLowerInvariant(Ask(prompt + " (y/n)", current ? "y" : "n"));
        return !answer.empty() ? answer[0] == 'y' : current;
    }

    [[nodiscard]] std::int32_t AskInt(
        const std::string& prompt, std::int32_t current,
        std::int32_t minimum, std::int32_t maximum)
    {
        const std::string answer = Ask(prompt, std::to_string(current));
        std::int32_t value = 0;
        return TryParseInt32Invariant(answer, value)
            ? std::clamp(value, minimum, maximum)
            : current;
    }

    [[nodiscard]] GameMode AskMode()
    {
        const std::array<GameMode, 12> modes{
            Battle(), BattleTeams(), Survival(), SurvivalTeams(), Capture(), Bounty(),
            BountyTeams(), Defender(), DefenderTeams(), Nodes(), NodesTeams(), PrimeHunter()
        };
        std::cout << '\n';
        for (std::size_t index = 0; index < modes.size(); ++index)
        {
            std::cout << "  [" << PadLeftInt(static_cast<std::int32_t>(index + 1), 2)
                << "] " << MphRead::Mods::Network::NetStatus::ModeName(modes[index]) << '\n';
        }
        std::cout << '\n';
        const std::string answer = Ask("  Mode", "1");
        std::int32_t index = 0;
        return TryParseInt32Invariant(answer, index)
                && index >= 1 && index <= static_cast<std::int32_t>(modes.size())
            ? modes[static_cast<std::size_t>(index - 1)]
            : Battle();
    }

    [[nodiscard]] bool AskRoom(
        const std::shared_ptr<MenuSettings>& settings,
        const std::vector<std::string>& rooms,
        std::string& roomKey)
    {
        if (!settings)
        {
            throw System::NullReferenceException();
        }
        roomKey = settings->RoomKey;
        if (rooms.empty())
        {
            std::cout << "  No multiplayer rooms were found." << '\n';
            return false;
        }

        std::size_t current = 0;
        for (std::size_t index = 0; index < rooms.size(); ++index)
        {
            if (rooms[index] == roomKey)
            {
                current = index;
                break;
            }
        }
        std::cout << '\n';
        for (std::size_t index = 0; index < rooms.size(); ++index)
        {
            std::cout << "  [" << PadLeftInt(static_cast<std::int32_t>(index + 1), 2)
                << "] " << rooms[index] << '\n';
        }
        std::cout << '\n';
        const std::string answer = Ask("  Map", std::to_string(current + 1));
        std::int32_t index = 0;
        if (!TryParseInt32Invariant(answer, index)
            || index < 1 || index > static_cast<std::int32_t>(rooms.size()))
        {
            return false;
        }
        roomKey = rooms[static_cast<std::size_t>(index - 1)];
        settings->RoomKey = roomKey;
        return true;
    }

    void GenerateMissingThumbnails()
    {
        const std::vector<std::string> missing = MphRead::Mods::ThumbnailGenerator::MissingThumbnails();
        if (missing.empty())
        {
            return;
        }
        std::cout << "  Rendering map previews..." << '\n';
        const bool redraw = !OutputRedirected();
        (void)MphRead::Mods::ThumbnailBatch::Run(
            missing,
            MphRead::Mods::ThumbnailBatch::DefaultParallelism(),
            MphRead::Mods::ThumbnailGenerator::ThumbnailWidth,
            MphRead::Mods::ThumbnailGenerator::ThumbnailHeight,
            [redraw](const std::string& line)
            {
                if (redraw)
                {
                    std::cout << "\r  " << PadRight(line, 70) << std::flush;
                }
                else
                {
                    std::cout << "  " << line << '\n';
                }
            });
        if (redraw)
        {
            std::cout << '\n';
        }
    }

    void SetUpGameFiles()
    {
        std::cout << '\n';
        std::cout << "  " << MphRead::Mods::Branding::Name
            << " needs your own Metroid Prime Hunters cartridge" << '\n';
        std::cout << "  dump. It unpacks what it needs next to this program and" << '\n';
        std::cout << "  leaves the file alone. No game data is included or" << '\n';
        std::cout << "  downloaded." << '\n';
        std::cout << '\n';
        std::string path = Ask("  Path to the .nds file (blank to cancel)", "");
        if (path.empty())
        {
            return;
        }
        path = TrimQuotes(StringTrim(path));
        if (!FileExists(path))
        {
            std::cout << "  There is no file at " << path << '\n';
            return;
        }
        std::cout << '\n';
        MphRead::Mods::Launcher::SetupProgress progress;
        const bool redraw = !OutputRedirected();
        const bool ok = MphRead::Mods::Launcher::GameFiles::RunSetup(
            path,
            [&progress, redraw](const std::string& line)
            {
                if (!progress.Observe(line))
                {
                    return;
                }
                if (redraw)
                {
                    std::cout << "\r  " << progress.Bar() << "  "
                        << PadRight(progress.Stage(), 22) << std::flush;
                }
                else
                {
                    std::cout << "  " << progress.Bar() << "  "
                        << progress.Stage() << '\n';
                }
            });
        progress.Finish(ok);
        if (redraw)
        {
            std::cout << "\r  " << progress.Bar() << "  "
                << PadRight(progress.Stage(), 22) << std::flush;
        }
        std::cout << '\n';
        std::cout << '\n';
        std::cout << (ok ? "  Ready to play." : "  Setup did not finish.") << '\n';
        if (ok)
        {
            GenerateMissingThumbnails();
        }
    }

    void Settings()
    {
        std::cout << '\n';
        LauncherPrefs::PlayerName(AskName());
        LauncherPrefs::LastHunter(AskHunter());
        const MphRead::Mods::WindowStartMode current = LauncherPrefs::WindowMode();
        const bool fullscreen = current != MphRead::Mods::WindowStartMode::Windowed;
        // A yes is the fullscreen kind already chosen; this prompt only asks
        // whether, not which.
        LauncherPrefs::WindowMode(
            AskYesNo("  Start fullscreen", fullscreen)
                ? LauncherPrefs::FullscreenKind()
                : MphRead::Mods::WindowStartMode::Windowed);

        const std::string endpoint = Ask("  Default server",
            LauncherPrefs::ServerAddress() + ":" + std::to_string(LauncherPrefs::ServerPort()));
        std::string address = LauncherPrefs::ServerAddress();
        std::int32_t port = LauncherPrefs::ServerPort();
        if (ParseEndpoint(endpoint, address, port))
        {
            LauncherPrefs::ServerAddress(address);
            LauncherPrefs::ServerPort(port);
        }

        const std::string master = Ask("  Server directory",
            LauncherPrefs::MasterHost() + ":" + std::to_string(LauncherPrefs::MasterPort()));
        std::string masterHost = LauncherPrefs::MasterHost();
        std::int32_t masterPort = LauncherPrefs::MasterPort();
        if (ParseEndpoint(master, masterHost, masterPort))
        {
            LauncherPrefs::MasterHost(masterHost);
            LauncherPrefs::MasterPort(masterPort);
        }
        LauncherPrefs::Save();
        std::cout << "  Saved." << '\n';
        std::cout << "  Volumes, controls and match rules are in -menu." << '\n';
    }

    [[nodiscard]] bool Adventure(LaunchPlan& plan)
    {
        plan = LaunchPlan{};
        while (true)
        {
            const std::vector<AdventureSave::SlotInfo> slots = AdventureSave::ReadAll();
            std::cout << '\n';
            std::cout << "  Adventure" << '\n';
            std::cout << "  --------------------------------------------" << '\n';
            for (std::size_t index = 0; index < slots.size(); ++index)
            {
                std::cout << "  [" << index + 1 << "] Slot "
                    << static_cast<unsigned int>(slots[index].Slot)
                    << "           " << slots[index].Describe() << '\n';
            }
            std::cout << "  [b] Back" << '\n';
            std::cout << '\n';
            const std::string choice = ::MphRead::NativeRuntime::ToLowerInvariant(Ask("  Choose a slot", "1"));
            if (choice == "b" || choice == "back" || ::MphRead::Mods::Launcher::TextLauncher::InputEnded())
            {
                return false;
            }
            std::int32_t index = 0;
            if (!::MphRead::NativeRuntime::Int32TryParseCurrentCulture(choice, index)
                || index < 1 || index > static_cast<std::int32_t>(slots.size()))
            {
                continue;
            }
            const AdventureSave::SlotInfo slot = slots[static_cast<std::size_t>(index - 1)];
            bool newGame = !slot.Used;
            if (slot.Used)
            {
                std::cout << '\n';
                std::cout << "  Slot " << static_cast<unsigned int>(slot.Slot)
                    << ": " << slot.Describe() << '\n';
                std::cout << "  [1] Continue" << '\n';
                std::cout << "  [2] New game    (overwrites this slot once you save)" << '\n';
                std::cout << "  [b] Back" << '\n';
                std::cout << '\n';
                const std::string what = ::MphRead::NativeRuntime::ToLowerInvariant(Ask("  Choose", "1"));
                if (what == "b" || what == "back")
                {
                    continue;
                }
                if (what == "2")
                {
                    newGame = true;
                }
                else if (what != "1")
                {
                    continue;
                }
            }

            const Hunter hunter = AskHunter();
            LauncherPrefs::LastKind(static_cast<std::int32_t>(LaunchKind::Adventure));
            LauncherPrefs::Save();
            LaunchPlan::Init init;
            init.Kind = LaunchKind::Adventure;
            init.Hunter = hunter;
            init.PlayerName = LauncherPrefs::PlayerName();
            init.SaveSlot = slot.Slot;
            init.NewGame = newGame;
            plan = LaunchPlan(init);
            return true;
        }
    }

    [[nodiscard]] bool Browse(std::string& address, std::int32_t& port)
    {
        std::cout << "  Asking " << LauncherPrefs::MasterHost() << ':'
            << LauncherPrefs::MasterPort() << "..." << '\n';
        const MasterListResult result = MphRead::Mods::Network::NetMasterClient::Query(
            LauncherPrefs::MasterHost(), LauncherPrefs::MasterPort());
        if (!result.Answered)
        {
            std::cout << "  The directory did not answer; it may be down, "
                << "or UDP may not reach it." << '\n';
            return false;
        }
        const std::vector<MasterListing>& servers = Servers(result);
        if (servers.empty())
        {
            std::cout << "  The directory is up and has nobody listed." << '\n';
            return false;
        }
        const std::vector<MasterListing> listed(servers.begin(), servers.end());
        std::cout << '\n';
        for (std::size_t index = 0; index < listed.size(); ++index)
        {
            const MasterListing listing = listed[index];
            const ServerStatus status = MphRead::Mods::Network::NetStatus::Query(
                listing.Address, listing.Port, false);
            const std::string name = !status.ServerName.empty()
                ? status.ServerName
                : !listing.ServerName.empty() ? listing.ServerName : listing.Endpoint();
            std::cout << "  [" << index + 1 << "] " << PadRight(name, 24) << ' '
                << PadRight(listing.Endpoint(), 24) << ' '
                << (status.Online ? Describe(status) : "did not answer") << '\n';
        }
        std::cout << '\n';
        const std::string answer = Ask("  Which one", "1");
        std::int32_t index = 0;
        if (!TryParseInt32Invariant(answer, index)
            || index < 1 || index > static_cast<std::int32_t>(listed.size()))
        {
            return false;
        }
        address = listed[static_cast<std::size_t>(index - 1)].Address;
        port = listed[static_cast<std::size_t>(index - 1)].Port;
        return true;
    }

    [[nodiscard]] bool PlayOnline(LaunchPlan& plan)
    {
        plan = LaunchPlan{};
        std::string address = LauncherPrefs::ServerAddress();
        std::int32_t port = LauncherPrefs::ServerPort();
        std::cout << '\n';
        std::cout << "  [b] browse the servers on " << LauncherPrefs::MasterHost() << '\n';
        std::cout << "  [enter] use " << address << ':' << port << '\n';
        std::cout << "  [c] cancel" << '\n';
        const std::string answer = ::MphRead::NativeRuntime::ToLowerInvariant(Ask("  Server", ""));
        if (answer == "c")
        {
            return false;
        }
        if (answer == "b")
        {
            if (!Browse(address, port))
            {
                return false;
            }
        }
        else if (!answer.empty())
        {
            if (!ParseEndpoint(answer, address, port))
            {
                std::cout << "  That is not a host or host:port." << '\n';
                return false;
            }
        }

        const ServerStatus status = MphRead::Mods::Network::NetStatus::Query(address, port, true);
        if (status.Online)
        {
            std::cout << "  " << Describe(status) << '\n';
        }
        else
        {
            std::cout << "  That server did not answer. Joining anyway." << '\n';
        }
        const std::string name = AskName();
        const Hunter hunter = AskHunter();
        LauncherPrefs::ServerAddress(address);
        LauncherPrefs::ServerPort(port);
        LauncherPrefs::LastKind(static_cast<std::int32_t>(LaunchKind::Online));
        LauncherPrefs::Save();
        std::cout << "  Connecting to " << address << ':' << port << "..." << '\n';
        if (!MphRead::Mods::Network::NetLaunch::Join(address, port, name, hunter))
        {
            std::cout << "  " << MphRead::Mods::Network::NetLaunch::LastJoinError() << '\n';
            MphRead::Mods::Network::NetSession::Stop();
            return false;
        }

        LaunchPlan::Init init;
        init.Kind = LaunchKind::Online;
        init.Hunter = hunter;
        init.PlayerName = name;
        init.RoomKey = std::string();
        init.Mode = Battle();
        init.Port = port;
        plan = LaunchPlan(init);
        return true;
    }

    [[nodiscard]] bool PlayOffline(
        const std::shared_ptr<MenuSettings>& settings,
        const std::vector<std::string>& rooms,
        LaunchPlan& plan)
    {
        plan = LaunchPlan{};
        std::string roomKey;
        if (!AskRoom(settings, rooms, roomKey))
        {
            return false;
        }
        const GameMode mode = AskMode();
        const std::int32_t bots = AskInt(
            "  Bots (0-7)", LauncherPrefs::Bots(), 0, PlayerSlotCapacity - 1);
        const std::int32_t level = AskInt(
            "  Bot skill (0 easy, 1 normal, 2 hard, 3 insane)", LauncherPrefs::BotLevel(), 0, 3);
        const Hunter hunter = AskHunter();
        LauncherPrefs::Bots(bots);
        LauncherPrefs::BotLevel(level);
        LauncherPrefs::LastKind(static_cast<std::int32_t>(LaunchKind::Offline));
        LauncherPrefs::Save();

        LaunchPlan::Init init;
        init.Kind = LaunchKind::Offline;
        init.Hunter = hunter;
        init.PlayerName = LauncherPrefs::PlayerName();
        init.RoomKey = roomKey;
        init.Mode = mode;
        init.Bots = bots;
        init.BotLevel = level;
        plan = LaunchPlan(init);
        return true;
    }

    [[nodiscard]] bool HostGame(
        const std::shared_ptr<MenuSettings>& settings,
        const std::vector<std::string>& rooms,
        LaunchPlan& plan)
    {
        plan = LaunchPlan{};
        std::string roomKey;
        if (!AskRoom(settings, rooms, roomKey))
        {
            return false;
        }
        const GameMode mode = AskMode();
        const std::string name = AskName();
        const Hunter hunter = AskHunter();
        const bool onMaster = AskYesNo(
            "  Let the directory run it (no port forwarding)", LauncherPrefs::HostOnMaster());
        LauncherPrefs::HostOnMaster(onMaster);
        LauncherPrefs::LastKind(static_cast<std::int32_t>(LaunchKind::Host));

        if (onMaster)
        {
            std::cout << "  Asking " << LauncherPrefs::MasterHost() << ':'
                << LauncherPrefs::MasterPort() << " to run " << roomKey << "..." << '\n';
            const HostedGame game = MphRead::Mods::Network::NetMasterClient::RequestGame(
                LauncherPrefs::MasterHost(), LauncherPrefs::MasterPort(), roomKey, mode,
                7 * 60, 7, PlayerSlotCapacity, name + "'s game");
            if (!game.Started)
            {
                std::cout << "  It would not: " << game.Reason << '\n';
                return false;
            }
            LauncherPrefs::Save();
            std::cout << "  Running on " << game.Host << ':' << game.Port << "; joining it." << '\n';
            if (!MphRead::Mods::Network::NetLaunch::Join(game.Host, game.Port, name, hunter))
            {
                std::cout << "  The game started but could not be joined." << '\n';
                MphRead::Mods::Network::NetSession::Stop();
                return false;
            }
        }
        else
        {
            const std::int32_t port = AskInt("  Port", LauncherPrefs::HostPort(), 1, 65535);
            const bool listed = AskYesNo(
                "  List it so others can find it", LauncherPrefs::ListHostedGame());
            LauncherPrefs::HostPort(port);
            LauncherPrefs::ListHostedGame(listed);
            LauncherPrefs::Save();
            std::cout << "  Starting a server on port " << port << "..." << '\n';
            const std::optional<std::tuple<std::string, std::int32_t, std::string>> listing = listed
                ? std::optional<std::tuple<std::string, std::int32_t, std::string>>(
                    std::in_place,
                    LauncherPrefs::MasterHost(), LauncherPrefs::MasterPort(), name + "'s game")
                : std::nullopt;
            if (!MphRead::Mods::Network::NetHostSession::StartAndJoin(
                port, name, hunter, roomKey, mode, 7 * 60, 7, PlayerSlotCapacity, listing))
            {
                const std::optional<std::string> error = MphRead::Mods::Network::NetHostSession::LastError();
                std::cout << "  The server would not start: "
                    << (error.has_value() ? *error : "the port may be in use") << '\n';
                return false;
            }
            std::cout << "  Hosting on port " << port << ". Friends join with:" << '\n';
            std::cout << "    " << MphRead::Mods::Branding::Executable()
                << " -connect <your address> -port " << port << '\n';
        }

        LaunchPlan::Init init;
        init.Kind = LaunchKind::Host;
        init.Hunter = hunter;
        init.PlayerName = name;
        init.RoomKey = roomKey;
        init.Mode = mode;
        init.Port = LauncherPrefs::HostPort();
        plan = LaunchPlan(init);
        return true;
    }

    void UpdateNow(UpdateInfo update)
    {
        std::cout << '\n';
        std::cout << "  " << MphRead::Mods::Update::Updater::Describe(update) << '\n';
        const std::optional<std::string>& pageUrl = update.PageUrl.Get();
        std::cout << "  " << (pageUrl.has_value() ? *pageUrl : std::string()) << '\n';
        if (MphRead::Mods::Update::Updater::OpenPage(update))
        {
            std::cout << "  Opened in your browser." << '\n';
        }
        std::cout << "  Download it there and unpack it over this one." << '\n';
        std::cout << '\n';
    }

    [[nodiscard]] bool Home(
        const std::shared_ptr<MenuSettings>& settings,
        const std::vector<std::string>& rooms,
        LaunchPlan& plan)
    {
        plan = LaunchPlan{};
        std::optional<std::string> problem = MphRead::Mods::Launcher::GameFiles::Problem();
        while (true)
        {
            std::cout << '\n';
            std::cout << "  " << MphRead::Mods::Branding::NameAndVersion() << '\n';
            std::cout << "  --------------------------------------------" << '\n';
            std::cout << "  Game files : " << MphRead::Mods::Launcher::GameFiles::Describe() << '\n';
            std::cout << "  Player     : " << LauncherPrefs::PlayerName()
                << " as " << ::MphRead::ToString(LauncherPrefs::LastHunter()) << '\n';
            std::cout << '\n';

            if (problem.has_value())
            {
                std::cout << "  " << *problem << '.' << '\n';
                std::cout << '\n';
                std::cout << "  [1] Game files       point this at your .nds dump" << '\n';
                std::cout << "  [q] Quit" << '\n';
                std::cout << '\n';
                std::cout << "  " << MphRead::Mods::Credits::Summary() << '\n';
                std::cout << '\n';
                const std::string only = ::MphRead::NativeRuntime::ToLowerInvariant(Ask("  Choose", "1"));
                if (only == "q" || only == "quit" || ::MphRead::Mods::Launcher::TextLauncher::InputEnded())
                {
                    return false;
                }
                SetUpGameFiles();
                problem = MphRead::Mods::Launcher::GameFiles::Problem();
                if (!problem.has_value())
                {
                    return true;
                }
                continue;
            }

            if (MphRead::Mods::Update::Updater::Available().has_value())
            {
                std::cout << "  " << MphRead::Mods::Update::Updater::Describe(
                    NullableValue(MphRead::Mods::Update::Updater::Available())) << '\n';
                std::cout << '\n';
            }
            std::cout << "  [1] Adventure        the story, from a save slot" << '\n';
            std::cout << "  [2] Play online      join a server" << '\n';
            std::cout << "  [3] Play offline     a match against bots" << '\n';
            std::cout << "  [4] Host a game      run a server and play on it" << '\n';
            std::cout << "  [5] Settings         name, hunter, window, addresses" << '\n';
            std::cout << "  [6] Game files       point this at your .nds dump" << '\n';
            if (MphRead::Mods::Update::Updater::Available().has_value())
            {
                std::cout << "  [u] Update now       open the download page" << '\n';
            }
            std::cout << "  [q] Quit" << '\n';
            std::cout << '\n';
            std::cout << "  " << MphRead::Mods::Credits::Summary()
                << " -credits for the full list." << '\n';
            std::cout << '\n';

            const std::string choice = ::MphRead::NativeRuntime::ToLowerInvariant(Ask("  Choose", "1"));
            if (choice == "q" || choice == "quit" || ::MphRead::Mods::Launcher::TextLauncher::InputEnded())
            {
                return false;
            }
            if (choice == "5")
            {
                Settings();
                continue;
            }
            if (choice == "6")
            {
                SetUpGameFiles();
                problem = MphRead::Mods::Launcher::GameFiles::Problem();
                return true;
            }
            if (choice == "u" && MphRead::Mods::Update::Updater::Available().has_value())
            {
                UpdateNow(NullableValue(MphRead::Mods::Update::Updater::Available()));
                continue;
            }
            if (problem.has_value())
            {
                std::cout << '\n';
                std::cout << "  " << *problem << ". Use [6] first." << '\n';
                continue;
            }

            if (choice == "1")
            {
                if (Adventure(plan))
                {
                    return true;
                }
                continue;
            }
            if (choice == "2")
            {
                if (PlayOnline(plan))
                {
                    return true;
                }
                continue;
            }
            if (choice == "3")
            {
                if (PlayOffline(settings, rooms, plan))
                {
                    return true;
                }
                continue;
            }
            if (choice == "4")
            {
                if (HostGame(settings, rooms, plan))
                {
                    return true;
                }
                continue;
            }
        }
    }
}

namespace MphRead::Mods::Launcher
{
    void TextLauncher::Run()
    {
        LauncherPrefs::Load();
        if (LauncherPrefs::AutoUpdate())
        {
            MphRead::Mods::Update::Updater::CheckInBackground([](UpdateInfo) {});
            MphRead::Mods::Update::Updater::WaitForCheck(
                MphRead::Mods::Update::Updater::TimeSpan{20'000'000});
        }
        if (GameFiles::Ready())
        {
            GameFiles::ApplyPaths();
            MphRead::Mods::ThumbnailGenerator::EnsureCustomPreviews();
        }
        std::vector<std::string> rooms;

        while (true)
        {
            std::shared_ptr<MenuSettings> settings = MphRead::GameState::LoadSettings();
            MphRead::Mods::GameSettings::Apply(settings);
            LauncherPrefs::Load();
            MphRead::Mods::WindowMode::FullscreenKind(LauncherPrefs::FullscreenKind());
            if (!MphRead::Mods::WindowMode::StartupForced())
            {
                MphRead::Mods::WindowMode::Startup(LauncherPrefs::WindowMode());
            }
            if (rooms.empty() && GameFiles::Ready())
            {
                rooms = MphRead::Mods::ThumbnailGenerator::MultiplayerRooms();
            }
            Hunters::Reroll();

            LaunchPlan plan;
            if (!Home(settings, rooms, plan))
            {
                return;
            }
            if (plan.Kind() == LaunchKind::None)
            {
                continue;
            }

            bool returnAfterFinally = false;
            std::exception_ptr pending;
            try
            {
                try
                {
                    MatchStart::Launch(settings, plan);
                }
                catch (const std::exception& ex)
                {
                    std::cout << '\n';
                    std::cout << "The game could not start: "
                        << std::string((ex).what()) << '\n';
                    WriteExceptionStackTrace(ex);
                    returnAfterFinally = true;
                }
            }
            catch (...)
            {
                pending = std::current_exception();
            }

            // C# finally statement order is observable. A failure in the first
            // stop prevents the second and replaces any pending body result or
            // exception, exactly as the managed finally does.
            MphRead::Mods::Network::NetSession::Stop();
            MphRead::Mods::Network::NetHostSession::Stop();

            if (pending)
            {
                std::rethrow_exception(pending);
            }
            if (returnAfterFinally)
            {
                return;
            }
        }
    }
}
