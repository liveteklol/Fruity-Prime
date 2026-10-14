#include "Mods/Network/Server/Core/DedicatedServerProcess.hpp"

#include "Mods/Launcher/Portable/GameFiles.hpp"
#include "Mods/Network/Server/Core/LocalServer.hpp"
#include "Mods/Network/Server/Match/MapRotation.hpp"
#include "Mods/Network/Discovery/NetMaster.hpp"
#include "Mods/Network/Discovery/NetStatus.hpp"
#include "Mods/Network/Diagnostics/HomingShotTrace.hpp"
#include "Mods/Platform/AppPaths.hpp"
#include "NativeRuntime/System/IO.hpp"
#include "NativeRuntime/System/Runtime.hpp"

#include <chrono>
#include <array>
#include <thread>

namespace MphRead::Mods::Network
{
    namespace Runtime = ::MphRead::NativeRuntime;

    std::shared_ptr<DedicatedServerProcess> DedicatedServerProcess::Start(
        const DedicatedServerProcessOptions& options, std::string& error,
        const ServerBinary* selectedBinary, std::stop_token cancel)
    {
        error.clear();
        const auto binary = selectedBinary != nullptr
            ? std::optional<ServerBinary>(*selectedBinary) : LocalServer::HostingBinary();
        if (!binary.has_value() || options.Rotation.empty() || options.Port < 1 || options.Port > 65535)
        {
            error = "no server executable or valid room/port was supplied";
            return nullptr;
        }
        if (const auto problem = Launcher::GameFiles::Problem(); problem.has_value())
        {
            error = "a hosted server needs the game files: " + *problem;
            return nullptr;
        }
        if (cancel.stop_requested() || !LocalServer::CanBind(options.Port))
        {
            error = "server startup cancelled or UDP port already in use";
            return nullptr;
        }
        auto room = std::make_shared<DedicatedServerProcess>();
        room->_port = options.Port;
        std::array<std::uint8_t, ServerStatusPacket::MaxNameBytes> nameBytes{};
        NetText::Write(nameBytes, options.Name);
        const auto expectedName = NetText::Read(nameBytes);
        try
        {
            const auto directory = Runtime::PathCombine(Platform::AppPaths::UserDataDirectory(), "hosted");
            Runtime::DirectoryCreateDirectory(directory);
            room->_rotationPath = Runtime::PathCombine(directory,
                std::to_string(options.Port) + "-" + Runtime::Guid::NewGuid().ToString("N") + ".txt");
            MapRotation::WriteList(room->_rotationPath, options.Rotation, options.TimeLimit, options.PointGoal);
            const auto pathsDirectory = Runtime::IsMacOS() ? Platform::AppPaths::UserDataDirectory()
                : Runtime::PathGetDirectoryName(binary->Executable).value_or(binary->WorkingDirectory);
            LocalServer::PreparePaths(pathsDirectory);
            Runtime::ProcessStartInfo start{};
            start.FileName = binary->Executable;
            start.WorkingDirectory = binary->WorkingDirectory;
            start.CreateNoWindow = true;
            start.ArgumentList = binary->Prefix;
            const auto add = [&](const std::string& key, const std::string& value)
            {
                start.ArgumentList.push_back(key);
                start.ArgumentList.push_back(value);
            };
            start.ArgumentList.emplace_back("-server");
            add("-port", std::to_string(options.Port));
            add("-players", std::to_string(options.MaxPlayers));
            add("-servername", options.Name);
            add("-rotation", room->_rotationPath);
            add("-format", ToString(options.Format));
            add("-requireready", options.RequireReady ? "on" : "off");
            add("-joininprogress", options.AllowJoinInProgress ? "on" : "off");
            if (options.Policy == ServerSessionPolicy::Lobby)
            {
                start.ArgumentList.emplace_back("-lobby");
                add("-ownertoken", options.OwnerToken.ToString("N"));
            }
            if (options.FriendlyFire) { start.ArgumentList.emplace_back("-friendlyfire"); }
            if (!options.ShadowFreeze) { start.ArgumentList.emplace_back("-noshadowfreeze"); }
            if (options.AffinityWeapons) { start.ArgumentList.emplace_back("-affinityweapons"); }
            if (options.Instagib) { start.ArgumentList.emplace_back("-instagib"); }
            if (options.Reporter != nullptr)
            {
                add("-master", options.Reporter->Host());
                add("-masterport", std::to_string(options.Reporter->Port()));
            }
            else { start.ArgumentList.emplace_back("-nomaster"); }
            if (HomingShotTrace::Enabled()) { start.ArgumentList.emplace_back("-homingtrace"); }
            start.ArgumentList.emplace_back("-noupdate");
            start.ArgumentList.emplace_back("-noautoupdate");
            room->_process = Runtime::Process::Start(start);
            const auto deadline = std::chrono::steady_clock::now()
                + std::chrono::milliseconds(NetMasterConfig::HostedStartupTimeoutMs);
            while (!cancel.stop_requested() && room->Alive() && std::chrono::steady_clock::now() < deadline)
            {
                const auto status = NetStatus::Query("127.0.0.1", options.Port, false, 150);
                if (status.Online && status.Protocol == NetConfig::ProtocolVersion
                    && status.ServerName == expectedName && room->Alive())
                {
                    return room;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            error = "the dedicated server did not become ready on port " + std::to_string(options.Port);
        }
        catch (const std::exception& exception) { error = exception.what(); }
        room->Stop();
        return nullptr;
    }

    bool DedicatedServerProcess::Alive()
    {
        return _process != nullptr && !_process->HasExited();
    }

    std::optional<std::int32_t> DedicatedServerProcess::PlayerCount()
    {
        if (!Alive()) { return std::nullopt; }
        const auto status = NetStatus::Query("127.0.0.1", _port, false, 150);
        if (!status.Online) { return std::nullopt; }
        _everOccupied |= status.Players > 0;
        return status.Players;
    }

    void DedicatedServerProcess::Stop() noexcept
    {
        try
        {
            if (Alive()) { _process->Kill(true); }
            if (_process != nullptr) { _process->Dispose(); }
            if (!_rotationPath.empty()) { Runtime::FileDelete(_rotationPath); }
        }
        catch (...) {}
        _process.reset();
        _rotationPath.clear();
    }

    void DedicatedServerProcess::Detach() noexcept
    {
        // Process disposal releases the parent's handle, without killing
        // the independently running dedicated server.
        _process.reset();
        _rotationPath.clear();
    }
}
