#include "Mods/Network/Server/Core/HostPool.hpp"

#include "Mods/Network/Server/Core/LocalServer.hpp"
#include "Mods/Network/Server/Core/DedicatedServerProcess.hpp"
#include "Mods/Network/Server/Match/MapRotation.hpp"
#include "Mods/Network/Discovery/NetMaster.hpp"
#include "Entities/Players/PlayerEntity.hpp"
#include "Formats/Formats.hpp"
#include "NativeRuntime/System/ExceptionText.hpp"
#include "NativeRuntime/System/Net.hpp"
#include "NativeRuntime/System/Random.hpp"
#include "NativeRuntime/System/Tasks.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <exception>
#include <thread>

namespace MphRead::Mods::Network
{
    namespace Runtime = ::MphRead::NativeRuntime;

    namespace
    {
        [[nodiscard]] std::string AddressText(const std::array<std::uint8_t, 4>& bytes)
        {
            return std::to_string(bytes[0]) + "." + std::to_string(bytes[1]) + "."
                + std::to_string(bytes[2]) + "." + std::to_string(bytes[3]);
        }
    }

    void HostPool::SetPorts(std::int32_t first, std::int32_t last) noexcept
    {
        _first = first;
        _last = last;
    }

    HostPool::~HostPool()
    {
        StopAll("host pool closed");
    }

    std::string HostPool::Describe() const
    {
        return CanHost()
            ? "can start games on ports " + std::to_string(_first) + "-" + std::to_string(_last)
                + " for players who cannot open one of their own"
            : std::string("not starting games for anybody (no host port range)");
    }

    HostReplyPacket HostPool::Start(const HostRequestPacket& request,
        const std::shared_ptr<System::Net::IPEndPoint>& asker, double now)
    {
        const std::array<std::uint8_t, 4> askerAddress = asker->AddressBytes();
        for (std::int32_t i = static_cast<std::int32_t>(_hosted.size()) - 1; i >= 0; i--)
        {
            const std::shared_ptr<Hosted> previous = _hosted[static_cast<std::size_t>(i)];
            if (previous->Asker == askerAddress && previous->Server->PlayerCount() == 0)
            {
                Stop(previous, "the same player asked for another game");
            }
        }
        const std::int32_t port = FreePort(now);
        if (port < 0)
        {
            HostReplyPacket busy{};
            busy.Reason = "all " + std::to_string(_last - _first + 1) + " game slots are busy";
            return busy;
        }
        const GameMode mode = ::MphRead::IsDefinedGameMode(request.Mode)
            ? static_cast<GameMode>(request.Mode)
            : GameMode::Battle;
        const std::string requestedName = request.ServerName.value_or("");
        const std::string name = !requestedName.empty() ? requestedName : "Hosted game";
        const std::shared_ptr<MapRotation> rotation = request.Rotation.has_value() && !request.Rotation->empty()
            ? MapRotation::FromList(*request.Rotation, request.TimeLimit, request.PointGoal)
            : MapRotation::SingleMatch(request.RoomKey.value_or(""), mode, request.TimeLimit, request.PointGoal);
        Runtime::Guid ownerToken{};
        if (request.Policy == ServerSessionPolicy::Lobby)
        {
            std::array<std::uint8_t, 16> bytes{};
            Runtime::RandomNumberGeneratorFill(bytes.data(), bytes.size());
            ownerToken = Runtime::Guid(bytes);
        }
        DedicatedServerProcessOptions options{};
        options.Port = port;
        options.MaxPlayers = std::clamp(static_cast<std::int32_t>(request.MaxPlayers), 2,
            Entities::PlayerEntity::SlotCapacity);
        options.Name = name;
        for (const auto& map : rotation->Entries()) { options.Rotation.emplace_back(map->RoomKey, map->Mode); }
        options.TimeLimit = request.TimeLimit;
        options.PointGoal = request.PointGoal;
        options.Policy = request.Policy;
        options.Format = request.Format;
        options.OwnerToken = ownerToken;
        options.Reporter = ReporterFactory ? ReporterFactory() : nullptr;
        options.RequireReady = request.RequireReady;
        options.AllowJoinInProgress = request.AllowJoinInProgress;
        std::string error;
        const auto server = DedicatedServerProcess::Start(options, error);
        if (server == nullptr)
        {
            HostReplyPacket failed{};
            failed.Reason = error;
            return failed;
        }
        const auto entry = std::make_shared<Hosted>();
        entry->Server = server;
        entry->Port = port;
        entry->Name = name;
        entry->Asker = askerAddress;
        entry->StartedAt = now;
        entry->LastOccupied = now;
        _hosted.push_back(entry);
        Log("started \"" + name + "\" on port " + std::to_string(port) + " for " + AddressText(askerAddress)
            + " (" + request.RoomKey.value_or("") + ", " + ::MphRead::ToString(mode) + ", "
            + std::to_string(rotation->Entries().size()) + " map(s))");
        HostReplyPacket result{};
        result.Started = true;
        result.Port = static_cast<std::uint16_t>(port);
        result.Reason = "";
        result.OwnerToken = ownerToken;
        return result;
    }

    std::int32_t HostPool::FreePort(double now)
    {
        for (std::int32_t port = _first; port <= _last; port++)
        {
            const bool taken = std::any_of(_hosted.begin(), _hosted.end(),
                [port](const std::shared_ptr<Hosted>& entry) { return entry->Port == port; });
            if (taken)
            {
                continue;
            }
            if (!LocalServer::CanBind(port)) { continue; }
            const auto cooling = _cooling.find(port);
            if (cooling != _cooling.end())
            {
                if (now - cooling->second < PortCooldownSeconds)
                {
                    continue;
                }
                _cooling.erase(cooling);
            }
            return port;
        }
        return -1;
    }

    void HostPool::Reap(double now)
    {
        for (std::int32_t i = static_cast<std::int32_t>(_hosted.size()) - 1; i >= 0; i--)
        {
            const std::shared_ptr<Hosted> entry = _hosted[static_cast<std::size_t>(i)];
            if (!entry->Server->Alive())
            {
                Stop(entry, "server process exited", now);
                continue;
            }
            if (now - entry->LastProbe < 1.0) { continue; }
            entry->LastProbe = now;
            const auto count = entry->Server->PlayerCount();
            if (!count.has_value() || *count > 0)
            {
                entry->LastOccupied = now;
                continue;
            }
            const bool played = entry->Server->EverOccupied();
            const double grace = played ? EmptySeconds : StartupSeconds;
            if (now - entry->LastOccupied > grace)
            {
                Stop(entry, played ? "everyone left" : "nobody joined", now);
            }
        }
    }

    void HostPool::StopAll(const std::string& why)
    {
        for (std::int32_t i = static_cast<std::int32_t>(_hosted.size()) - 1; i >= 0; i--)
        {
            Stop(_hosted[static_cast<std::size_t>(i)], why);
        }
    }

    void HostPool::Stop(const std::shared_ptr<Hosted>& entry, const std::string& why, double now)
    {
        Log("stopping \"" + entry->Name + "\" on port " + std::to_string(entry->Port) + ": " + why);
        entry->Server->Stop();
        const auto found = std::find(_hosted.begin(), _hosted.end(), entry);
        if (found != _hosted.end())
        {
            _hosted.erase(found);
        }
        _cooling[entry->Port] = now;
        if (OnStopped)
        {
            OnStopped(entry->Port);
        }
    }
}
