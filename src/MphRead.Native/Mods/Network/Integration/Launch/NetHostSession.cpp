#include "Mods/Network/Integration/Launch/NetHostSession.hpp"
#include "Mods/Network/Integration/Launch/NetLaunch.hpp"
#include "Mods/Network/Server/Core/DedicatedServerProcess.hpp"
#include "Mods/Network/Discovery/NetMaster.hpp"
#include "GameState.hpp"

namespace MphRead::Mods::Network
{
    std::shared_ptr<DedicatedServerProcess> NetHostSession::_server{};
    std::optional<std::string> NetHostSession::_lastError{};
    std::mutex NetHostSession::_lastErrorMutex{};

    bool NetHostSession::Running() noexcept
    {
        return _server != nullptr && _server->Alive();
    }

    std::optional<std::string> NetHostSession::LastError()
    {
        const std::lock_guard lock(_lastErrorMutex);
        return _lastError;
    }

    void NetHostSession::SetLastError(std::optional<std::string> value)
    {
        const std::lock_guard lock(_lastErrorMutex);
        _lastError = std::move(value);
    }

    bool NetHostSession::StartAndJoin(std::int32_t port,
        const std::string& playerName, Hunter hunter, const std::string& roomKey,
        GameMode mode, float timeLimit, std::int32_t pointGoal,
        std::int32_t maxPlayers,
        std::optional<std::tuple<std::string, std::int32_t, std::string>> listing)
    {
        Stop();
        SetLastError(std::nullopt);
        DedicatedServerProcessOptions options{};
        options.Port = port;
        options.MaxPlayers = maxPlayers;
        options.Name = listing.has_value() ? std::get<2>(*listing) : playerName;
        options.Rotation.emplace_back(roomKey, mode);
        options.TimeLimit = timeLimit;
        options.PointGoal = pointGoal;
        options.FriendlyFire = GameState::FriendlyFire();
        options.ShadowFreeze = GameState::ShadowFreeze();
        options.AffinityWeapons = GameState::AffinityWeapons();
        options.Instagib = GameState::Instagib();
        if (listing.has_value())
        {
            options.Reporter = std::make_shared<MasterReporter>(std::get<0>(*listing), std::get<1>(*listing));
        }
        std::string error;
        _server = DedicatedServerProcess::Start(options, error);
        if (_server == nullptr)
        {
            SetLastError(error);
            return false;
        }
        if (!NetLaunch::Join("127.0.0.1", port, playerName, hunter))
        {
            SetLastError(NetLaunch::LastJoinError());
            _server->Stop();
            _server.reset();
            return false;
        }
        return true;
    }

    void NetHostSession::Stop()
    {
        if (_server != nullptr) { _server->Detach(); }
        _server.reset();
    }
}
