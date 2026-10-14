#pragma once

#include "Mods/Network/Domain/MatchDefinition.hpp"
#include "NativeRuntime/System/Guid.hpp"
#include "NativeRuntime/System/Process.hpp"

#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

namespace MphRead::Mods::Network
{
    class MasterReporter;
    struct ServerBinary;

    struct DedicatedServerProcessOptions final
    {
        std::int32_t Port = 0;
        std::int32_t MaxPlayers = 8;
        std::string Name{};
        std::vector<std::pair<std::string, GameMode>> Rotation{};
        float TimeLimit = 0;
        std::int32_t PointGoal = 0;
        ServerSessionPolicy Policy = ServerSessionPolicy::Continuous;
        MatchFormat Format = MatchFormat::Auto;
        NativeRuntime::Guid OwnerToken{};
        bool RequireReady = true;
        bool AllowJoinInProgress = true;
        bool FriendlyFire = false;
        bool ShadowFreeze = true;
        bool AffinityWeapons = false;
        bool Instagib = false;
        std::shared_ptr<MasterReporter> Reporter{};
    };

    // One room, one process: global simulation state never crosses room
    // boundaries. Ownership of the lobby does not grant simulation authority.
    class DedicatedServerProcess final
    {
    public:
        [[nodiscard]] static std::shared_ptr<DedicatedServerProcess> Start(
            const DedicatedServerProcessOptions& options, std::string& error,
            const ServerBinary* binary = nullptr, std::stop_token cancel = {});
        [[nodiscard]] const auto& ProcessHandle() const noexcept { return _process; }
        [[nodiscard]] bool Alive();
        [[nodiscard]] std::optional<std::int32_t> PlayerCount();
        [[nodiscard]] bool EverOccupied() const noexcept { return _everOccupied; }
        [[nodiscard]] std::int32_t Port() const noexcept { return _port; }
        void Stop() noexcept;
        // A local room keeps running when its creator leaves the game.
        void Detach() noexcept;
        ~DedicatedServerProcess() { Stop(); }

    private:
        std::shared_ptr<NativeRuntime::Process> _process{};
        std::string _rotationPath{};
        std::int32_t _port = 0;
        bool _everOccupied = false;
    };
}
