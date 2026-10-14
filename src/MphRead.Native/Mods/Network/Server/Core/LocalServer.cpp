#include "Mods/Network/Server/Core/LocalServer.hpp"
#include "Mods/Network/Server/Core/DedicatedServerProcess.hpp"
#include "Mods/Network/Discovery/NetMaster.hpp"

#include "Mods/Network/Server/Match/MapRotation.hpp"
#include "Mods/Network/Protocol/NetProtocol.hpp"
#include "Mods/Network/Discovery/NetStatus.hpp"
#include "Mods/Branding.hpp"
#include "Mods/Launcher/Portable/GameFiles.hpp"
#include "Mods/Launcher/Portable/LauncherPrefs.hpp"
#include "Mods/Platform/AppPaths.hpp"
#include "Mods/Update/UpdateCheck.hpp"
#include "Mods/Update/UpdateDownload.hpp"
#include "NativeRuntime/System/ArchiveFile.hpp"
#include "NativeRuntime/System/Console.hpp"
#include "NativeRuntime/System/Globalization.hpp"
#include "NativeRuntime/System/IO.hpp"
#include "NativeRuntime/System/Net.hpp"
#include "NativeRuntime/System/Process.hpp"
#include "NativeRuntime/System/Random.hpp"
#include "NativeRuntime/System/Runtime.hpp"
#include "NativeRuntime/System/ZipArchive.hpp"

#include <array>
#include <chrono>
#include <exception>
#include <filesystem>
#include <thread>
#include <unordered_set>

namespace MphRead::Mods::Network
{
    namespace Runtime = ::MphRead::NativeRuntime;

    std::string ServerBinary::Describe() const
    {
        return Downloaded
            ? "the server package in " + Runtime::PathGetFileName(Runtime::PathGetDirectoryName(Executable).value_or(""))
            : "this build";
    }

    std::string LocalServer::Directory()
    {
        return Runtime::PathCombine(Launcher::LauncherPrefs::Directory(), "server");
    }

    std::optional<ServerBinary> LocalServer::Available()
    {
        if (Runtime::IsWindows())
        {
            const std::string beside = Runtime::PathCombine(Runtime::AppContextBaseDirectory(),
                Update::UpdateCheck::ServerBinaryName());
            if (Runtime::FileExists(beside))
            {
                return ServerBinary(beside, {}, Runtime::AppContextBaseDirectory(), false);
            }
        }
        else
        {
            std::optional<ServerBinary> own = OwnBinary();
            if (own.has_value())
            {
                return own;
            }
        }
        const std::string installed = Runtime::PathCombine(Directory(), Update::UpdateCheck::ServerBinaryName());
        if (Runtime::FileExists(installed))
        {
            return ServerBinary(installed, {}, Directory(), true);
        }
        return std::nullopt;
    }

    bool LocalServer::Ready()
    {
        return Available().has_value();
    }

    std::optional<ServerBinary> LocalServer::HostingBinary()
    {
        const auto canonical = Runtime::PathCombine(Runtime::AppContextBaseDirectory(),
            std::string(Mods::Branding::FileName) + (Runtime::IsWindows() ? ".exe" : ""));
        if (Runtime::FileExists(canonical))
        {
            return ServerBinary(canonical, {}, Platform::AppPaths::UserDataDirectory(), false);
        }
        const auto own = OwnBinary();
        return own.has_value() ? own : Available();
    }

    void LocalServer::PreparePaths(const std::string& directory)
    {
        CopyPaths(directory);
    }

    std::optional<ServerBinary> LocalServer::OwnBinary()
    {
        if (Runtime::IsAndroid())
        {
            return std::nullopt;
        }
        const std::optional<std::string> exe = Runtime::EnvironmentProcessPath();
        if (!exe.has_value() || exe->empty() || !Runtime::FileExists(*exe))
        {
            return std::nullopt;
        }
        const std::string directory = Runtime::AppContextBaseDirectory();
        const std::string name = Runtime::PathGetFileNameWithoutExtension(*exe);
        if (Runtime::StringEqualsOrdinalIgnoreCase(name, "dotnet"))
        {
            const std::string dll = Runtime::PathCombine(directory, std::string(Mods::Branding::FileName) + ".dll");
            if (!Runtime::FileExists(dll))
            {
                return std::nullopt;
            }
            return ServerBinary(*exe, {dll}, Platform::AppPaths::UserDataDirectory(), false);
        }
        return ServerBinary(*exe, {}, Platform::AppPaths::UserDataDirectory(), false);
    }

    bool LocalServer::CanInstall()
    {
        return !Update::UpdateCheck::ServerRid().empty();
    }

    bool LocalServer::Install(const std::function<void(float)>& progress, Update::CancellationToken cancel)
    {
        _lastError = std::nullopt;
        if (!CanInstall())
        {
            _lastError = "there is no dedicated-server package for this platform";
            return false;
        }
        const std::optional<Update::UpdateInfo> found = Update::UpdateCheck::ServerAsset(cancel);
        if (!found.has_value())
        {
            _lastError = Update::UpdateCheck::LastReason().value_or("no server package was found");
            return false;
        }
        const Update::UpdateInfo& package = *found;
        try
        {
            Runtime::DirectoryCreateDirectory(Directory());
            const bool zip = Runtime::StringEndsWithOrdinalIgnoreCase(package.AssetName.Get().value_or(""), ".zip");
            const std::string archive = Runtime::PathCombine(Directory(), zip ? "package.zip" : "package.tar.gz");
            if (!Update::UpdateDownload::Fetch(package.AssetUrl.Get().value_or(""), archive, package.AssetSize.Get(),
                progress, cancel))
            {
                _lastError = Update::UpdateDownload::LastError().value_or("the download failed");
                return false;
            }
            if (zip)
            {
                Runtime::ZipFileExtractToDirectory(archive, Directory(), true);
            }
            else
            {
                Runtime::TarFileExtractGZipToDirectory(archive, Directory());
            }
            Runtime::FileDelete(archive);
            const std::string binary = Runtime::PathCombine(Directory(), Update::UpdateCheck::ServerBinaryName());
            if (!Runtime::FileExists(binary))
            {
                _lastError = "the package does not contain " + Update::UpdateCheck::ServerBinaryName();
                return false;
            }
            MakeExecutable(binary);
            _installedTag = package.Tag.Get().value_or("");
            return true;
        }
        catch (const std::exception& ex)
        {
            _lastError = ex.what();
            return false;
        }
    }

    void LocalServer::MakeExecutable(const std::string& path)
    {
        if (Runtime::IsWindows())
        {
            return;
        }
        try
        {
            std::filesystem::permissions(Runtime::PathFromUtf8(path),
                std::filesystem::perms::owner_exec | std::filesystem::perms::group_exec
                    | std::filesystem::perms::others_exec,
                std::filesystem::perm_options::add);
        }
        catch (...)
        {
        }
    }

    std::int32_t LocalServer::Start(const std::string& serverName,
        const std::vector<std::pair<std::string, GameMode>>& rotation,
        std::int32_t maxPlayers, float timeLimit, std::int32_t pointGoal,
        const std::string& masterHost, std::int32_t masterPort, bool listed,
        std::stop_token cancel, bool lobby)
    {
        _lastError = std::nullopt;
        if (lobby)
        {
            std::array<std::uint8_t, 16> bytes{};
            Runtime::RandomNumberGeneratorFill(bytes.data(), bytes.size());
            _ownerToken = Runtime::Guid(bytes);
        }
        else
        {
            _ownerToken = Runtime::Guid::Empty();
        }
        const std::optional<ServerBinary> found = Available();
        if (!found.has_value())
        {
            _lastError = "there is no server binary on this machine yet";
            return -1;
        }
        const std::int32_t port = FreePort();
        if (port < 0)
        {
            _lastError = "no free UDP port could be found for a server";
            return -1;
        }
        DedicatedServerProcessOptions options{};
        options.Port = port;
        options.Name = serverName;
        options.MaxPlayers = maxPlayers;
        options.Rotation = rotation;
        options.TimeLimit = timeLimit;
        options.PointGoal = pointGoal;
        options.Policy = lobby ? ServerSessionPolicy::Lobby : ServerSessionPolicy::Continuous;
        options.OwnerToken = _ownerToken;
        if (listed) { options.Reporter = std::make_shared<MasterReporter>(masterHost, masterPort); }
        std::string error;
        const auto room = DedicatedServerProcess::Start(options, error, &*found, cancel);
        if (room == nullptr)
        {
            _lastError = error;
            return -1;
        }
        _running = room->ProcessHandle();
        room->Detach();
        return port;
    }

    void LocalServer::CopyPaths(const std::string& directory)
    {
        const std::string root = Launcher::GameFiles::Root();
        const std::string source = Runtime::PathCombine(root, "paths.txt");
        const std::string target = Runtime::PathCombine(directory, "paths.txt");
        if (!Runtime::FileExists(source) || Runtime::PathGetFullPath(source) == Runtime::PathGetFullPath(target))
        {
            return;
        }
        std::vector<std::string> lines = Runtime::FileReadAllLines(source);
        for (std::string& line : lines)
        {
            const std::size_t split = line.find('=');
            const std::string value = split == std::string::npos ? "" : Runtime::StringTrim(line.substr(split + 1));
            if (!value.empty())
            {
                line = Runtime::StringTrim(line.substr(0, split)) + "=" + Runtime::PathGetFullPath(value, root);
            }
        }
        std::string text;
        for (std::size_t i = 0; i < lines.size(); i++)
        {
            if (i > 0)
            {
                text += Runtime::EnvironmentNewLine();
            }
            text += lines[i];
        }
        Runtime::FileWriteAllText(target, text);
    }

    void LocalServer::Stop()
    {
        const std::shared_ptr<Runtime::Process> process = _running;
        _running = nullptr;
        if (process == nullptr)
        {
            return;
        }
        try
        {
            if (!process->HasExited())
            {
                process->Kill(true);
            }
            process->Dispose();
        }
        catch (...)
        {
        }
    }

    std::int32_t LocalServer::FreePort()
    {
        std::unordered_set<std::int32_t> taken;
        try
        {
            for (const std::int32_t port : Runtime::ActiveUdpListenerPorts())
            {
                taken.insert(port);
            }
        }
        catch (...)
        {
        }
        for (std::int32_t port = NetConfig::DefaultPort; port < NetConfig::DefaultPort + 40; port++)
        {
            if (taken.contains(port) || !CanBind(port))
            {
                continue;
            }
            return port;
        }
        return -1;
    }

    bool LocalServer::CanBind(std::int32_t port)
    {
        try
        {
            Runtime::UdpSocket probe;
            probe.Bind(port);
            probe.Dispose();
            return true;
        }
        catch (const Runtime::SocketException&)
        {
            return false;
        }
    }
}
