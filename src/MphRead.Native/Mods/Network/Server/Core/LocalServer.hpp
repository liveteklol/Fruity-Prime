#pragma once

#include "Mods/Update/SyncHttp.hpp"
#include "Formats/Formats.hpp"
#include "NativeRuntime/System/Guid.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

namespace MphRead::NativeRuntime
{
    class Process;
}

namespace MphRead::Mods::Network
{
    // What a server is started from: an executable, the arguments that go
    // before its own, and the directory it runs in.
    struct ServerBinary
    {
        ServerBinary(std::string executable, std::vector<std::string> prefix,
            std::string workingDirectory, bool downloaded)
            : Executable(std::move(executable)), Prefix(std::move(prefix)),
              WorkingDirectory(std::move(workingDirectory)), Downloaded(downloaded)
        {
        }

        std::string Executable;
        std::vector<std::string> Prefix;
        std::string WorkingDirectory;
        bool Downloaded;

        [[nodiscard]] std::string Describe() const;
    };

    // The create-server screen's Dedicated half: a server on this machine, in
    // its own process that outlives the client.
    class LocalServer final
    {
    public:
        LocalServer() = delete;

        [[nodiscard]] static std::string Directory();
        [[nodiscard]] static const std::optional<std::string>& LastError() noexcept { return _lastError; }
        [[nodiscard]] static const NativeRuntime::Guid& OwnerToken() noexcept { return _ownerToken; }
        [[nodiscard]] static const std::string& InstalledTag() noexcept { return _installedTag; }
        [[nodiscard]] static const std::shared_ptr<NativeRuntime::Process>& Running() noexcept { return _running; }

        [[nodiscard]] static std::optional<ServerBinary> Available();
        [[nodiscard]] static std::optional<ServerBinary> HostingBinary();
        [[nodiscard]] static bool CanBind(std::int32_t port);
        static void PreparePaths(const std::string& directory);
        [[nodiscard]] static bool Ready();
        [[nodiscard]] static bool CanInstall();
        static bool Install(const std::function<void(float)>& progress = {},
            Update::CancellationToken cancel = nullptr);
        static std::int32_t Start(const std::string& serverName,
            const std::vector<std::pair<std::string, GameMode>>& rotation,
            std::int32_t maxPlayers, float timeLimit, std::int32_t pointGoal,
            const std::string& masterHost, std::int32_t masterPort, bool listed,
            std::stop_token cancel = {}, bool lobby = false);
        static void Stop();

    private:
        [[nodiscard]] static std::optional<ServerBinary> OwnBinary();
        static void MakeExecutable(const std::string& path);
        static void CopyPaths(const std::string& directory);
        [[nodiscard]] static std::int32_t FreePort();

        inline static std::optional<std::string> _lastError{};
        inline static NativeRuntime::Guid _ownerToken{};
        inline static std::string _installedTag{};
        inline static std::shared_ptr<NativeRuntime::Process> _running{};
    };
}
