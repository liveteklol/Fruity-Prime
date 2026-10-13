#include "Mods/Network/Server/Core/LocalServer.hpp"

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
        const ServerBinary& binary = *found;
        const std::optional<std::string> problem = Launcher::GameFiles::Problem();
        if (problem.has_value())
        {
            _lastError = "a server runs the match itself and needs the game files: " + *problem;
            return -1;
        }
        const std::int32_t port = FreePort();
        if (port < 0)
        {
            _lastError = "no free UDP port could be found for a server";
            return -1;
        }
        const std::string rotationPath = Runtime::PathCombine(binary.WorkingDirectory, "maprotation-launcher.txt");
        try
        {
            MapRotation::WriteList(rotationPath, rotation, timeLimit, pointGoal);
            CopyPaths(binary.WorkingDirectory);
        }
        catch (const std::exception& ex)
        {
            _lastError = std::string("the rotation could not be written: ") + ex.what();
            return -1;
        }
        Runtime::ProcessStartInfo start{};
        start.FileName = binary.Executable;
        start.WorkingDirectory = binary.WorkingDirectory;
        start.UseShellExecute = Runtime::IsWindows();
        start.CreateNoWindow = false;
        for (const std::string& argument : binary.Prefix)
        {
            start.ArgumentList.push_back(argument);
        }
        start.ArgumentList.emplace_back("-server");
        if (lobby)
        {
            start.ArgumentList.emplace_back("-lobby");
            start.ArgumentList.emplace_back("-ownertoken");
            start.ArgumentList.push_back(_ownerToken.ToString("N"));
        }
        start.ArgumentList.emplace_back("-port");
        start.ArgumentList.push_back(std::to_string(port));
        start.ArgumentList.emplace_back("-players");
        start.ArgumentList.push_back(std::to_string(maxPlayers));
        start.ArgumentList.emplace_back("-servername");
        start.ArgumentList.push_back(serverName);
        start.ArgumentList.emplace_back("-rotation");
        start.ArgumentList.push_back(rotationPath);
        start.ArgumentList.emplace_back("-master");
        start.ArgumentList.push_back(masterHost);
        start.ArgumentList.emplace_back("-masterport");
        start.ArgumentList.push_back(std::to_string(masterPort));
        if (!listed)
        {
            start.ArgumentList.emplace_back("-nomaster");
        }
        start.ArgumentList.emplace_back("-noautoupdate");
        try
        {
            _running = Runtime::Process::Start(start);
            if (_running == nullptr)
            {
                _lastError = "the server process would not start";
                return -1;
            }
        }
        catch (const std::exception& ex)
        {
            _lastError = std::string("the server could not be started: ") + ex.what();
            return -1;
        }
        for (std::int32_t i = 0; i < 120 && !cancel.stop_requested(); i++)
        {
            if (_running->HasExited() && _running->ExitCode() != 0)
            {
                _lastError = "the server stopped while starting up -- its window says "
                    "why; usually the game files or a port already in use";
                _running = nullptr;
                return -1;
            }
            if (NetStatus::Query("127.0.0.1", port, false).Online)
            {
                return port;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
        _lastError = "the server did not answer in thirty seconds";
        Stop();
        return -1;
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
