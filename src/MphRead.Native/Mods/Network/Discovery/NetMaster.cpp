#include "Mods/Network/Discovery/NetMaster.hpp"
#include "Mods/Network/Discovery/NetStatus.hpp"
#include "NativeRuntime/System/Random.hpp"

#include <atomic>
#include <set>

#include "Entities/Players/PlayerEntity.hpp"
#include "Formats/Formats.hpp"
#include "NativeRuntime/System/Console.hpp"
#include "NativeRuntime/System/DateTime.hpp"
#include "NativeRuntime/System/Exceptions.hpp"
#include "NativeRuntime/System/Globalization.hpp"
#include "NativeRuntime/System/Net.hpp"
#include "NativeRuntime/System/Tasks.hpp"
#include "Mods/Update/ServerUpdate.hpp"

#include "Mods/Network/Server/Core/DedicatedServer.hpp"
#include "Mods/Network/Server/Match/MapRotation.hpp"
#include "NativeRuntime/System/Managed.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

using ::MphRead::NativeRuntime::UncheckedIncrement;

namespace
{
    constexpr std::int64_t TicksPerMillisecond = 10'000;

    std::string IPv4ToString(const std::array<std::uint8_t, 4>& address)
    {
        return std::to_string(address[0]) + "." + std::to_string(address[1]) + "."
            + std::to_string(address[2]) + "." + std::to_string(address[3]);
    }

    std::int32_t UncheckedSlotCount(std::int32_t first, std::int32_t last) noexcept
    {
        const std::uint32_t bits = static_cast<std::uint32_t>(last)
            - static_cast<std::uint32_t>(first) + 1U;
        return std::bit_cast<std::int32_t>(bits);
    }

    template <typename TBody>
    auto WithUdpClient(::MphRead::NativeRuntime::SocketHandle socket, TBody&& body)
        -> decltype(body())
    {
        using TResult = decltype(body());
        std::optional<TResult> result;
        std::exception_ptr pending;
        try
        {
            result.emplace(body());
        }
        catch (...)
        {
            pending = std::current_exception();
        }

        // C# using-var disposal runs on both return and exception. If Dispose
        // throws, it replaces the pending completion.
        ::MphRead::NativeRuntime::UdpClientDispose(socket);
        if (pending != nullptr)
        {
            std::rethrow_exception(pending);
        }
        return std::move(*result);
    }
}

namespace
{
    // The IPv4 addresses Dns.GetHostAddresses returns, in its own order.
    [[nodiscard]] std::vector<std::array<std::uint8_t, 4>> DnsGetIPv4Addresses(
        const std::string& host)
    {
        std::vector<std::array<std::uint8_t, 4>> result;
        for (const ::MphRead::NativeRuntime::Address& address
            : ::MphRead::NativeRuntime::DnsGetHostAddresses(host))
        {
            if (address.Family == ::MphRead::NativeRuntime::AddressFamily::InterNetwork)
            {
                result.push_back(address.Bytes);
            }
        }
        return result;
    }
}

namespace MphRead::Mods::Network
{
    class MasterServer::Entry final
    {
    public:
        std::shared_ptr<System::Net::IPEndPoint> Key{};
        std::uint32_t Address = 0;
        std::uint16_t Port = 0;
        std::uint8_t Players = 0;
        std::uint8_t MaxPlayers = 0;
        std::uint8_t Mode = 0;
        std::uint8_t Protocol = 0;
        std::string ServerName{};
        std::string RoomKey{};
        double LastSeen = 0.0;
    };

    class MasterServer::Hosted final
    {
    public:
        std::shared_ptr<DedicatedServer> Server{};
        std::shared_ptr<std::stop_source> Cancel{};
        std::int32_t Port = 0;
        std::string Name{};
        std::array<std::uint8_t, 4> Asker{};
        double StartedAt = 0.0;
        double LastOccupied = 0.0;
    };

    MasterReporter::MasterReporter(std::string host, std::int32_t port)
        : _host(std::move(host)), _port(port)
    {
    }

    void MasterReporter::Beat(double now, const std::string& serverName,
        std::uint16_t port, std::uint8_t players, std::uint8_t maxPlayers,
        std::uint8_t mode, const std::string& roomKey)
    {
        if (now - _lastBeat < NetMasterConfig::HeartbeatSeconds)
        {
            return;
        }
        _lastBeat = now;
        try
        {
            if (!Resolve(now))
            {
                return;
            }
            MasterHeartbeatPacket beat{};
            beat.Protocol = static_cast<std::uint8_t>(NetConfig::ProtocolVersion);
            beat.Port = port;
            beat.Players = players;
            beat.MaxPlayers = maxPlayers;
            beat.Mode = mode;
            beat.ServerName = serverName;
            beat.RoomKey = roomKey;
            beat.Write(_scratch);

            std::array<std::uint8_t, 1 + MasterHeartbeatPacket::Size> datagram{};
            datagram[0] = static_cast<std::uint8_t>(PacketType::MasterHeartbeat);
            std::copy(_scratch.begin(), _scratch.end(), datagram.begin() + 1);
            if (!_socket.has_value())
            {
                throw ::System::NullReferenceException();
            }
            ::MphRead::NativeRuntime::UdpClientSend(*_socket, datagram, _endPoint);
        }
        catch (const std::exception& ex)
        {
            Complain(ex.what());
        }
    }

    void MasterReporter::Farewell(std::uint16_t port)
    {
        if (!_socket.has_value() || _endPoint == nullptr)
        {
            return;
        }
        try
        {
            std::array<std::uint8_t, 3> datagram{};
            datagram[0] = static_cast<std::uint8_t>(PacketType::Bye);
            datagram[1] = static_cast<std::uint8_t>(port);
            datagram[2] = static_cast<std::uint8_t>(port >> 8);
            ::MphRead::NativeRuntime::UdpClientSend(*_socket, datagram, _endPoint);
        }
        catch (const std::exception&)
        {
        }
    }

    bool MasterReporter::Resolve(double now)
    {
        if (_endPoint != nullptr && now - _lastResolve < 3600.0)
        {
            return true;
        }
        const std::vector<std::array<std::uint8_t, 4>> addresses
            = DnsGetIPv4Addresses(_host);
        if (addresses.empty())
        {
            Complain(_host + " has no IPv4 address");
            return false;
        }
        _lastResolve = now;
        _endPoint = std::make_shared<System::Net::IPEndPoint>(addresses.front(), _port);
        if (!_socket.has_value())
        {
            _socket = ::MphRead::NativeRuntime::UdpClientCreateInterNetwork();
        }
        _complained = false;
        return true;
    }

    void MasterReporter::Complain(const std::string& message)
    {
        if (_complained)
        {
            return;
        }
        _complained = true;
        NativeRuntime::ConsoleWriteLine(("[master] not listed on " + _host + ":"
            + ::MphRead::NativeRuntime::ToString(_port) + " -- " + message));
        NativeRuntime::ConsoleWriteLine(("[master] the server is running normally; "
            "pass -nomaster to stop trying, or -master HOST to point elsewhere"));
    }

    void MasterReporter::Dispose()
    {
        if (_socket.has_value())
        {
            ::MphRead::NativeRuntime::UdpClientDispose((*_socket));
        }
        _socket.reset();
    }

    MasterServer::MasterServer(std::int32_t port)
        : _port(port)
    {
    }

    void MasterServer::SetHostPorts(std::int32_t first, std::int32_t last) noexcept
    {
        _hostPortFirst = first;
        _hostPortLast = last;
    }

    bool MasterServer::CanHost() const noexcept
    {
        return _hostPortLast >= _hostPortFirst && _hostPortFirst > 0;
    }

    bool MasterServer::SetPublicAddress(const std::string& host)
    {
        try
        {
            const std::vector<std::array<std::uint8_t, 4>> resolved
                = DnsGetIPv4Addresses(host);
            if (resolved.empty())
            {
                Log("cannot publish local servers as \"" + host + "\": no IPv4 address");
                return false;
            }
            _publicAddress = ToUInt32(resolved.front());
            _publicName = host + " (" + IPv4ToString(resolved.front()) + ")";
            return true;
        }
        catch (const std::exception& ex)
        {
            Log("cannot publish local servers as \"" + host + "\": " + ex.what());
            return false;
        }
    }

    std::uint32_t MasterServer::ToUInt32(
        const std::array<std::uint8_t, 4>& address) noexcept
    {
        return (static_cast<std::uint32_t>(address[0]) << 24)
            | (static_cast<std::uint32_t>(address[1]) << 16)
            | (static_cast<std::uint32_t>(address[2]) << 8)
            | static_cast<std::uint32_t>(address[3]);
    }

    bool MasterServer::IsLocal(const std::array<std::uint8_t, 4>& address) noexcept
    {
        return address[0] == 127 || address[0] == 10
            || (address[0] == 172 && address[1] >= 16 && address[1] <= 31)
            || (address[0] == 192 && address[1] == 168)
            || (address[0] == 169 && address[1] == 254);
    }

    void MasterServer::Stop() noexcept
    {
        _running.store(false);
    }

    double MasterServer::ClockElapsedSeconds() const noexcept
    {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - _clockStart).count();
    }

    void MasterServer::Run(std::stop_token cancel)
    {
        _transport = std::make_unique<NetTransport>(_port);
        _running.store(true);
        Log("listening on UDP "
            + ::MphRead::NativeRuntime::ToString((_transport->LocalPort())));
        Log("servers are dropped after 50 s of silence");
        if (_publicAddress != 0)
        {
            Log("servers on this machine are listed as " + _publicName);
        }
        Log(CanHost()
            ? "can start games on ports "
                + ::MphRead::NativeRuntime::ToString(_hostPortFirst) + "-"
                + ::MphRead::NativeRuntime::ToString(_hostPortLast)
                + " for players who cannot open one of their own"
            : "not starting games for anybody (no host port range)");
        _clockStart = std::chrono::steady_clock::now();
        double lastReport = 0.0;
        while (_running.load() && !cancel.stop_requested())
        {
            const double now = ClockElapsedSeconds();
            for (const ReceivedPacket packet : _transport->Drain())
            {
                Handle(packet, now);
            }
            Expire(now);
            ReapHosted(now);
            if (Update::ServerUpdate::ShouldRestart(
                static_cast<std::int32_t>(_hosted.size())))
            {
                Log("shutting down to come back on the new build");
                _running.store(false);
                break;
            }
            if (now - lastReport >= 60.0)
            {
                lastReport = now;
                std::string message = ::MphRead::NativeRuntime::ToString((static_cast<std::int32_t>(_entries.size()))) + " server(s) listed";
                if (!_hosted.empty())
                {
                    message += ", " + ::MphRead::NativeRuntime::ToString((static_cast<std::int32_t>(_hosted.size()))) + " started here";
                }
                Log(message);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        Log("shutting down");
        for (std::int32_t i = static_cast<std::int32_t>(_hosted.size()) - 1; i >= 0; --i)
        {
            StopHosted(_hosted[static_cast<std::size_t>(i)],
                "the directory is shutting down");
        }
        _transport->Dispose();
        _transport.reset();
    }

    void MasterServer::Handle(const ReceivedPacket& packet, double now)
    {
        if (packet.Type() == PacketType::MasterHeartbeat)
        {
            HandleHeartbeat(packet, now);
        }
        else if (packet.Type() == PacketType::MasterQuery)
        {
            SendList(packet.Sender);
        }
        else if (packet.Type() == PacketType::HostRequest)
        {
            HandleHostRequest(packet, now);
        }
        else if (packet.Type() == PacketType::Bye)
        {
            HandleFarewell(packet);
        }
    }

    void MasterServer::HandleFarewell(const ReceivedPacket& packet)
    {
        const std::span<const std::uint8_t> payload = packet.Payload();
        if (payload.size() < 2)
        {
            return;
        }
        const std::uint16_t port = static_cast<std::uint16_t>(payload[0])
            | static_cast<std::uint16_t>(static_cast<std::uint16_t>(payload[1]) << 8);
        const auto key = std::make_shared<System::Net::IPEndPoint>(
            packet.Sender->AddressBytes(), port);
        const auto found = std::find_if(_entries.begin(), _entries.end(),
            [&](const std::shared_ptr<Entry>& entry)
            {
                return entry->Key->Equals(*key);
            });
        if (found != _entries.end())
        {
            const std::shared_ptr<Entry> entry = *found;
            Log("- " + entry->Key->ToString() + " \"" + entry->ServerName
                + "\" (said goodbye)");
            _entries.erase(found);
        }
    }

    void MasterServer::HandleHostRequest(const ReceivedPacket& packet, double now)
    {
        HostReplyPacket reply{};
        const std::span<const std::uint8_t> payload = packet.Payload();
        if (payload.size() < static_cast<std::size_t>(HostRequestPacket::Size))
        {
            reply.Reason = "malformed request";
        }
        else
        {
            const HostRequestPacket request = HostRequestPacket::Read(payload);
            if (request.Protocol != static_cast<std::uint8_t>(NetConfig::ProtocolVersion))
            {
                reply.Reason = "this directory speaks protocol "
                    + ::MphRead::NativeRuntime::ToString(NetConfig::ProtocolVersion)
                    + ", your build speaks "
                    + ::MphRead::NativeRuntime::ToString(request.Protocol);
            }
            else if (!CanHost())
            {
                reply.Reason = "this directory does not start games";
            }
            else
            {
                reply = StartHosted(request, packet.Sender, now);
            }
        }
        reply.Write(_scratch);
        if (_transport != nullptr)
        {
            _transport->Send(packet.Sender, PacketType::HostReply,
                std::span<const std::uint8_t>(_scratch.data(), HostReplyPacket::Size));
        }
        if (!reply.Started)
        {
            Log("refused a game for " + packet.Sender->ToString() + ": "
                + reply.Reason.value_or(std::string{}));
        }
    }

    HostReplyPacket MasterServer::StartHosted(const HostRequestPacket& request,
        const std::shared_ptr<System::Net::IPEndPoint>& asker, double now)
    {
        const std::array<std::uint8_t, 4> askerAddress = asker->AddressBytes();
        for (std::int32_t i = static_cast<std::int32_t>(_hosted.size()) - 1; i >= 0; --i)
        {
            const std::shared_ptr<Hosted> previous = _hosted[static_cast<std::size_t>(i)];
            if (previous->Asker == askerAddress && previous->Server->PeerCount() == 0)
            {
                StopHosted(previous, "the same player asked for another game");
            }
        }
        const std::int32_t port = FreeHostPort(now);
        if (port < 0)
        {
            HostReplyPacket result{};
            result.Reason = "all " + ::MphRead::NativeRuntime::ToString((UncheckedSlotCount(_hostPortFirst, _hostPortLast))) + " game slots are busy";
            return result;
        }
        const GameMode mode = ::MphRead::IsDefinedGameMode(request.Mode)
            ? static_cast<GameMode>(request.Mode)
            : GameMode::Battle;
        const std::string& requestedName = request.ServerName.value();
        const std::string name = !requestedName.empty() ? requestedName : "Hosted game";
        const std::string& roomKey = request.RoomKey.value();
        const std::shared_ptr<MapRotation> rotation = request.Rotation.has_value() && !request.Rotation->empty()
            ? MapRotation::FromList(*request.Rotation, static_cast<float>(request.TimeLimit), request.PointGoal)
            : MapRotation::SingleMatch(roomKey, mode, static_cast<float>(request.TimeLimit), request.PointGoal);
        NativeRuntime::Guid ownerToken{};
        if (request.Policy == ServerSessionPolicy::Lobby)
        {
            std::array<std::uint8_t, 16> bytes{};
            NativeRuntime::RandomNumberGeneratorFill(bytes.data(), bytes.size());
            ownerToken = NativeRuntime::Guid(bytes);
        }
        const auto server = std::make_shared<DedicatedServer>(port,
            std::clamp(static_cast<std::int32_t>(request.MaxPlayers), 2,
                Entities::PlayerEntity::SlotCapacity), rotation);
        server->ServerName(name);
        server->SessionPolicy(request.Policy);
        server->Format(request.Format);
        server->OwnerToken(ownerToken);
        server->Reporter(std::make_shared<MasterReporter>("127.0.0.1", _port));
        server->RunsTheMatch(false);
        server->SetSessionOptions(request.RequireReady, request.AllowJoinInProgress);

        const auto cancel = std::make_shared<std::stop_source>();
        const auto entry = std::make_shared<Hosted>();
        entry->Server = server;
        entry->Cancel = cancel;
        entry->Port = port;
        entry->Name = name;
        entry->Asker = askerAddress;
        entry->StartedAt = now;
        entry->LastOccupied = now;

        std::thread thread([server, cancel, port]()
        {
            NativeRuntime::SetCurrentThreadName("MphRead hosted "
                + ::MphRead::NativeRuntime::ToString(port));
            try
            {
                server->Run(cancel->get_token());
            }
            catch (const std::exception& ex)
            {
                MasterServer::Log("game on "
                    + ::MphRead::NativeRuntime::ToString(port)
                    + " stopped: " + ex.what());
            }
        });
        thread.detach();

        for (std::int32_t i = 0; i < 50 && !server->Listening(); ++i)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if (!server->Listening())
        {
            cancel->request_stop();
            server->Stop();
            HostReplyPacket result{};
            result.Reason = "could not listen on port "
                + ::MphRead::NativeRuntime::ToString(port);
            return result;
        }
        _hosted.push_back(entry);
        Log("started \"" + name + "\" on port "
            + ::MphRead::NativeRuntime::ToString(port) + " for "
            + IPv4ToString(askerAddress) + " (" + roomKey + ", "
            + ::MphRead::ToString(mode) + ", " + std::to_string(rotation->Entries().size()) + " map(s))");
        HostReplyPacket result{};
        result.Started = true;
        result.Port = static_cast<std::uint16_t>(port);
        result.Reason = "";
        result.OwnerToken = ownerToken;
        return result;
    }

    std::int32_t MasterServer::FreeHostPort(double now)
    {
        std::int32_t port = _hostPortFirst;
        while (port <= _hostPortLast)
        {
            bool taken = false;
            for (const std::shared_ptr<Hosted>& entry : _hosted)
            {
                if (entry->Port == port)
                {
                    taken = true;
                    break;
                }
            }
            if (!taken)
            {
                const auto cooling = _cooling.find(port);
                if (cooling != _cooling.end())
                {
                    if (now - cooling->second < PortCooldownSeconds)
                    {
                        port = UncheckedIncrement(port);
                        continue;
                    }
                    _cooling.erase(cooling);
                }
                return port;
            }
            port = UncheckedIncrement(port);
        }
        return -1;
    }

    void MasterServer::ReapHosted(double now)
    {
        for (std::int32_t i = static_cast<std::int32_t>(_hosted.size()) - 1; i >= 0; --i)
        {
            const std::shared_ptr<Hosted> entry = _hosted[static_cast<std::size_t>(i)];
            if (entry->Server->PeerCount() > 0)
            {
                entry->LastOccupied = now;
                continue;
            }
            const bool played = entry->Server->EverOccupied();
            const double grace = played ? HostedEmptySeconds : HostedStartupSeconds;
            if (now - entry->LastOccupied > grace)
            {
                StopHosted(entry, played ? "everyone left" : "nobody joined");
            }
        }
    }

    void MasterServer::StopHosted(const std::shared_ptr<Hosted>& entry,
        const std::string& why)
    {
        Log("stopping \"" + entry->Name + "\" on port "
            + ::MphRead::NativeRuntime::ToString(entry->Port) + ": " + why);
        entry->Cancel->request_stop();
        entry->Server->Stop();
        for (std::int32_t i = 0; i < 100 && entry->Server->Listening(); ++i)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        const auto found = std::find(_hosted.begin(), _hosted.end(), entry);
        if (found != _hosted.end())
        {
            _hosted.erase(found);
        }
        _cooling[entry->Port] = ClockElapsedSeconds();
        Unlist(entry->Port);
    }

    void MasterServer::Unlist(std::int32_t port)
    {
        for (std::int32_t i = static_cast<std::int32_t>(_entries.size()) - 1; i >= 0; --i)
        {
            const std::shared_ptr<Entry>& entry = _entries[static_cast<std::size_t>(i)];
            if (entry->Port == port && entry->Key->AddressBytes()[0] == 127)
            {
                _entries.erase(_entries.begin() + i);
            }
        }
    }

    void MasterServer::HandleHeartbeat(const ReceivedPacket& packet, double now)
    {
        const std::span<const std::uint8_t> payload = packet.Payload();
        if (payload.size() < static_cast<std::size_t>(MasterHeartbeatPacket::Size))
        {
            return;
        }
        const MasterHeartbeatPacket beat = MasterHeartbeatPacket::Read(payload);
        const std::array<std::uint8_t, 4> senderAddress = packet.Sender->AddressBytes();
        std::uint32_t address = ToUInt32(senderAddress);
        if (_publicAddress != 0 && IsLocal(senderAddress))
        {
            address = _publicAddress;
        }
        const std::uint16_t port = beat.Port != 0
            ? beat.Port : static_cast<std::uint16_t>(packet.Sender->Port());
        const auto key = std::make_shared<System::Net::IPEndPoint>(senderAddress, port);
        auto found = std::find_if(_entries.begin(), _entries.end(),
            [&](const std::shared_ptr<Entry>& candidate)
            {
                return candidate->Key->Equals(*key);
            });
        std::shared_ptr<Entry> entry;
        if (found == _entries.end())
        {
            entry = std::make_shared<Entry>();
            entry->Key = key;
            _entries.push_back(entry);
            Log("+ " + key->ToString() + " \"" + beat.ServerName.value_or(std::string{}) + "\"");
        }
        else
        {
            entry = *found;
        }
        entry->Address = address;
        entry->Port = port;
        entry->Players = beat.Players;
        entry->MaxPlayers = beat.MaxPlayers;
        entry->Mode = beat.Mode;
        entry->Protocol = beat.Protocol;
        entry->ServerName = beat.ServerName.value();
        entry->RoomKey = beat.RoomKey.value();
        entry->LastSeen = now;
    }

    void MasterServer::Expire(double now)
    {
        for (std::int32_t i = static_cast<std::int32_t>(_entries.size()) - 1; i >= 0; --i)
        {
            const std::shared_ptr<Entry>& entry = _entries[static_cast<std::size_t>(i)];
            if (now - entry->LastSeen > NetMasterConfig::ExpirySeconds)
            {
                Log("- " + entry->Key->ToString() + " \"" + entry->ServerName + "\"");
                _entries.erase(_entries.begin() + i);
            }
        }
    }

    void MasterServer::SendList(const std::shared_ptr<System::Net::IPEndPoint>& sender)
    {
        const std::int32_t perPacket = NetMasterConfig::EntriesPerPacket();
        const std::int32_t total = std::min(static_cast<std::int32_t>(_entries.size()), 255);
        std::int32_t sent = 0;
        do
        {
            const std::int32_t count = std::min(perPacket, total - sent);
            _scratch[0] = static_cast<std::uint8_t>(count);
            _scratch[1] = static_cast<std::uint8_t>(total);
            std::int32_t offset = 2;
            for (std::int32_t i = 0; i < count; ++i)
            {
                const std::shared_ptr<Entry>& entry
                    = _entries[static_cast<std::size_t>(sent + i)];
                MasterEntryPacket wire{};
                wire.Address = entry->Address;
                wire.Port = entry->Port;
                wire.Players = entry->Players;
                wire.MaxPlayers = entry->MaxPlayers;
                wire.Mode = entry->Mode;
                wire.Protocol = entry->Protocol;
                wire.ServerName = entry->ServerName;
                wire.RoomKey = entry->RoomKey;
                wire.Write(std::span<std::uint8_t>(_scratch.data() + offset,
                    _scratch.size() - static_cast<std::size_t>(offset)));
                offset += MasterEntryPacket::Size;
            }
            _scratch[static_cast<std::size_t>(offset++)] = static_cast<std::uint8_t>(CanHost() ? MasterFlags::CanHost : 0);
            if (_transport != nullptr)
            {
                _transport->Send(sender, PacketType::MasterList,
                    std::span<const std::uint8_t>(_scratch.data(),
                        static_cast<std::size_t>(offset)));
            }
            sent += count;
        }
        while (sent < total);
    }

    void MasterServer::Log(const std::string& message)
    {
        NativeRuntime::ConsoleWriteLine(("[" + NativeRuntime::DateTimeToString(NativeRuntime::DateTimeNow(), "HH:mm:ss")
            + "] [master] " + message));
    }

    std::string MasterListing::Endpoint() const
    {
        return Port == NetConfig::DefaultPort
            ? Address
            : Address + ":" + ::MphRead::NativeRuntime::ToString(Port);
    }

    std::string HostCandidate::Describe() const
    {
        if (!Answered)
        {
            return "not answering";
        }
        const std::string ping = Latency >= 0 ? std::to_string(Latency) + " ms" : std::string("-- ms");
        return CanHost == false ? ping + " -- cannot open new games" : ping;
    }

    void NetMasterClient::FindHosts(const std::string& masterHost, std::int32_t masterPort,
        std::function<void(HostCandidate)> onFound, std::function<void()> onDone, std::int32_t timeoutMs)
    {
        NativeRuntime::TaskRun([masterHost, masterPort, onFound, onDone, timeoutMs]()
        {
            try
            {
                const auto started = std::chrono::steady_clock::now();
                const MasterListResult listing = Query(masterHost, masterPort, timeoutMs);
                const auto elapsed = NativeRuntime::Int64ToInt32(static_cast<std::int64_t>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - started).count()));
                HostCandidate directory{};
                directory.Label = masterHost;
                directory.Host = masterHost;
                directory.Port = masterPort;
                directory.Answered = listing.Answered;
                directory.CanHost = listing.CanHost;
                directory.Latency = listing.Answered ? elapsed : -1;
                onFound(directory);
                std::vector<MasterListing> rows;
                std::set<std::string, NativeRuntime::OrdinalIgnoreCaseLess> seen;
                if (listing.Servers != nullptr)
                {
                    for (const MasterListing& server : *listing.Servers)
                    {
                        if (server.Address.empty()
                            || !seen.insert(server.Address + ":" + std::to_string(server.Port)).second)
                        {
                            continue;
                        }
                        rows.push_back(server);
                    }
                }
                if (rows.empty())
                {
                    Safely(onDone);
                    return;
                }
                // Task.WhenAll(jobs).ContinueWith(onDone): the last job to
                // finish is the one that says so.
                auto remaining = std::make_shared<std::atomic<std::int32_t>>(static_cast<std::int32_t>(rows.size()));
                for (const MasterListing& row : rows)
                {
                    NativeRuntime::TaskRun([row, onFound, onDone, remaining]()
                    {
                        const auto clock = std::chrono::steady_clock::now();
                        ServerStatus status{};
                        try
                        {
                            status = NetStatus::Query(row.Address, row.Port, false);
                        }
                        catch (const std::exception&)
                        {
                            status = ServerStatus{};
                        }
                        const auto took = NativeRuntime::Int64ToInt32(static_cast<std::int64_t>(
                            std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - clock).count()));
                        HostCandidate candidate{};
                        candidate.Label = !row.ServerName.empty() ? row.ServerName : row.Endpoint();
                        candidate.Host = row.Address;
                        candidate.Port = row.Port;
                        candidate.Answered = status.Online;
                        candidate.CanHost = status.Online ? std::optional<bool>(status.CanHost) : std::nullopt;
                        candidate.Latency = status.Online ? (status.Latency >= 0 ? status.Latency : took) : -1;
                        try
                        {
                            onFound(candidate);
                        }
                        catch (...)
                        {
                        }
                        if (remaining->fetch_sub(1) == 1)
                        {
                            Safely(onDone);
                        }
                    });
                }
            }
            catch (const std::exception&)
            {
                Safely(onDone);
            }
        });
    }

    void NetMasterClient::Merge(std::vector<HostCandidate>& into, const HostCandidate& candidate)
    {
        const std::string machine = Resolve(candidate.Host);
        for (HostCandidate& existing : into)
        {
            if (!NativeRuntime::StringEqualsOrdinalIgnoreCase(Resolve(existing.Host), machine))
            {
                continue;
            }
            if (candidate.WillHost() && !existing.WillHost())
            {
                existing = candidate;
            }
            return;
        }
        into.push_back(candidate);
    }

    void NetMasterClient::Safely(const std::function<void()>& action)
    {
        try
        {
            if (action)
            {
                action();
            }
        }
        catch (...)
        {
        }
    }

    std::pair<HostCandidate, MasterListResult> NetMasterClient::Probe(const std::string& host,
        std::int32_t port, std::int32_t timeoutMs, const std::string& label)
    {
        const auto clock = std::chrono::steady_clock::now();
        MasterListResult answer{};
        try
        {
            answer = Query(host, port, timeoutMs);
        }
        catch (const std::exception&)
        {
            answer = MasterListResult{std::make_shared<std::vector<MasterListing>>(), false, std::nullopt};
        }
        const auto elapsed = NativeRuntime::Int64ToInt32(static_cast<std::int64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - clock).count()));
        HostCandidate candidate{};
        candidate.Label = label;
        candidate.Host = host;
        candidate.Port = port;
        candidate.Answered = answer.Answered;
        candidate.CanHost = answer.CanHost;
        candidate.Latency = answer.Answered ? elapsed : -1;
        return {candidate, answer};
    }

    std::string NetMasterClient::Resolve(const std::string& host)
    {
        try
        {
            const std::vector<std::array<std::uint8_t, 4>> found = DnsGetIPv4Addresses(host);
            return !found.empty() ? IPv4ToString(found.front()) : host;
        }
        catch (const std::exception&)
        {
            return host;
        }
    }

    HostedGame NetMasterClient::RequestGame(const std::string& masterHost,
        std::int32_t masterPort, const std::string& roomKey, GameMode mode,
        float timeLimit, std::int32_t pointGoal, std::int32_t maxPlayers,
        const std::string& serverName, std::int32_t timeoutMs,
        const std::optional<std::vector<std::pair<std::string, GameMode>>>& rotation,
        ServerSessionPolicy policy)
    {
        std::shared_ptr<System::Net::IPEndPoint> endPoint;
        try
        {
            const std::vector<std::array<std::uint8_t, 4>> resolved
                = DnsGetIPv4Addresses(masterHost);
            if (resolved.empty())
            {
                HostedGame result{};
                result.Reason = masterHost + " has no IPv4 address";
                return result;
            }
            endPoint = std::make_shared<System::Net::IPEndPoint>(resolved.front(), masterPort);
        }
        catch (const std::exception& ex)
        {
            HostedGame result{};
            result.Reason = "cannot find " + masterHost + ": " + ex.what();
            return result;
        }

        try
        {
            const ::MphRead::NativeRuntime::SocketHandle socket = ::MphRead::NativeRuntime::UdpClientCreateInterNetwork();
            return WithUdpClient(socket, [&]() -> HostedGame
            {
                ::MphRead::NativeRuntime::UdpClientSetReceiveTimeout(socket, timeoutMs);
                HostRequestPacket request{};
                request.Protocol = static_cast<std::uint8_t>(NetConfig::ProtocolVersion);
                request.MaxPlayers = static_cast<std::uint8_t>(std::clamp(maxPlayers, 2,
                    Entities::PlayerEntity::SlotCapacity));
                request.Mode = static_cast<std::uint8_t>(static_cast<std::int32_t>(mode));
                request.TimeLimit = static_cast<std::uint16_t>(std::clamp(
                    NativeRuntime::ConvertToInt32Net9(timeLimit), 0,
                    static_cast<std::int32_t>(std::numeric_limits<std::uint16_t>::max())));
                request.PointGoal = static_cast<std::uint16_t>(std::clamp(pointGoal, 0,
                    static_cast<std::int32_t>(std::numeric_limits<std::uint16_t>::max())));
                request.RoomKey = roomKey;
                request.ServerName = serverName;
                request.Policy = policy;
                request.AllowJoinInProgress = true;
                request.RequireReady = true;
                request.Rotation = rotation;

                std::vector<std::uint8_t> datagram(static_cast<std::size_t>(1 + request.Length()));
                datagram[0] = static_cast<std::uint8_t>(PacketType::HostRequest);
                request.Write(std::span<std::uint8_t>(datagram).subspan(1));
                ::MphRead::NativeRuntime::UdpClientSend(socket, datagram, endPoint);
                std::shared_ptr<System::Net::IPEndPoint> from = System::Net::IPEndPoint::Any();
                const std::int64_t deadline = NativeRuntime::DateTimeUtcNowTicks()
                    + static_cast<std::int64_t>(timeoutMs) * TicksPerMillisecond;
                while (NativeRuntime::DateTimeUtcNowTicks() < deadline)
                {
                    const std::vector<std::uint8_t> reply
                        = ::MphRead::NativeRuntime::UdpClientReceive(socket, from);
                    if (reply.size() < static_cast<std::size_t>(1 + HostReplyPacket::Size)
                        || reply[0] != static_cast<std::uint8_t>(PacketType::HostReply)
                        || from == nullptr || !from->Equals(*endPoint))
                    {
                        continue;
                    }
                    const HostReplyPacket answer = HostReplyPacket::Read(
                        std::span<const std::uint8_t>(reply).subspan(1));
                    HostedGame result{};
                    result.Started = answer.Started;
                    result.OwnerToken = answer.OwnerToken;
                    result.Host = masterHost;
                    result.Port = answer.Port;
                    result.Reason = answer.Reason.value_or(std::string{});
                    return result;
                }
                HostedGame result{};
                result.Reason = masterHost + " did not answer";
                return result;
            });
        }
        catch (const ::MphRead::NativeRuntime::SocketException&)
        {
            HostedGame result{};
            result.Reason = "no answer from " + masterHost + ":"
                + ::MphRead::NativeRuntime::ToString(masterPort)
                + " -- it may be down, or UDP may not reach it";
            return result;
        }
        catch (const std::exception& ex)
        {
            HostedGame result{};
            result.Reason = ex.what();
            return result;
        }
    }

    MasterListResult NetMasterClient::Query(const std::string& host,
        std::int32_t port, std::int32_t timeoutMs)
    {
        auto found = std::make_shared<std::vector<MasterListing>>();
        bool answered = false;
        std::optional<bool> canHost{};
        std::shared_ptr<System::Net::IPEndPoint> endPoint;
        try
        {
            const std::vector<std::array<std::uint8_t, 4>> resolved
                = DnsGetIPv4Addresses(host);
            if (resolved.empty())
            {
                return {found, false};
            }
            endPoint = std::make_shared<System::Net::IPEndPoint>(resolved.front(), port);
        }
        catch (const std::exception&)
        {
            return {found, false};
        }

        try
        {
            const ::MphRead::NativeRuntime::SocketHandle socket = ::MphRead::NativeRuntime::UdpClientCreateInterNetwork();
            return WithUdpClient(socket, [&]() -> MasterListResult
            {
                ::MphRead::NativeRuntime::UdpClientSetReceiveTimeout(socket, timeoutMs);
                const std::array<std::uint8_t, 2> query = {
                    static_cast<std::uint8_t>(PacketType::MasterQuery),
                    static_cast<std::uint8_t>(NetConfig::ProtocolVersion)
                };
                ::MphRead::NativeRuntime::UdpClientSend(socket, query, endPoint);
                std::shared_ptr<System::Net::IPEndPoint> from = System::Net::IPEndPoint::Any();
                const std::int64_t deadline = NativeRuntime::DateTimeUtcNowTicks()
                    + static_cast<std::int64_t>(timeoutMs) * TicksPerMillisecond;
                std::int32_t total = -1;
                while (NativeRuntime::DateTimeUtcNowTicks() < deadline
                    && (total < 0 || static_cast<std::int32_t>(found->size()) < total))
                {
                    const std::vector<std::uint8_t> reply
                        = ::MphRead::NativeRuntime::UdpClientReceive(socket, from);
                    if (reply.size() < 3
                        || reply[0] != static_cast<std::uint8_t>(PacketType::MasterList))
                    {
                        continue;
                    }
                    answered = true;
                    const std::int32_t count = reply[1];
                    total = reply[2];
                    std::int32_t offset = 3;
                    for (std::int32_t i = 0; i < count; ++i)
                    {
                        if (offset + MasterEntryPacket::Size
                            > static_cast<std::int32_t>(reply.size()))
                        {
                            break;
                        }
                        const MasterEntryPacket entry = MasterEntryPacket::Read(
                            std::span<const std::uint8_t>(reply).subspan(
                                static_cast<std::size_t>(offset)));
                        offset += MasterEntryPacket::Size;
                        const std::array<std::uint8_t, 4> address = {
                            static_cast<std::uint8_t>(entry.Address >> 24),
                            static_cast<std::uint8_t>(entry.Address >> 16),
                            static_cast<std::uint8_t>(entry.Address >> 8),
                            static_cast<std::uint8_t>(entry.Address)
                        };
                        MasterListing listing{};
                        listing.Address = IPv4ToString(address);
                        listing.Port = entry.Port;
                        listing.ServerName = entry.ServerName.value();
                        listing.RoomKey = entry.RoomKey.value();
                        listing.Mode = ::MphRead::IsDefinedGameMode(entry.Mode)
                            ? static_cast<GameMode>(entry.Mode)
                            : GameMode::Battle;
                        listing.Players = entry.Players;
                        listing.MaxPlayers = entry.MaxPlayers;
                        listing.Protocol = entry.Protocol;
                        found->push_back(std::move(listing));
                    }
                    if (offset < static_cast<std::int32_t>(reply.size()))
                    {
                        canHost = (reply[static_cast<std::size_t>(offset)] & MasterFlags::CanHost) != 0;
                    }
                    if (total == 0)
                    {
                        break;
                    }
                }
                return MasterListResult{found, answered, canHost};
            });
        }
        catch (const ::MphRead::NativeRuntime::SocketException&)
        {
        }
        catch (const std::exception&)
        {
        }
        return {found, answered};
    }
}
