#include "Mods/Diagnostics/HostedAuthorityCheck.hpp"
#include "Mods/Network/Integration/Launch/NetHostSession.hpp"
#include "Mods/Network/Integration/Launch/NetLaunch.hpp"
#include "Mods/Network/Server/Core/DedicatedServerProcess.hpp"
#include "Mods/Network/Server/Core/HostPool.hpp"
#include "Mods/Network/Server/Core/LocalServer.hpp"
#include "Mods/Network/Server/Core/ServerLaunchOptions.hpp"
#include "Mods/Network/Session/NetSession.hpp"
#include "Mods/Network/Transport/NetTransport.hpp"
#include "Mods/Network/Gameplay/Claims/Hit/NetHitClaims.hpp"
#include "Mods/Network/Gameplay/Combat/NetHitPrediction.hpp"
#include "Mods/Network/Discovery/NetStatus.hpp"
#include "Mods/Network/Discovery/NetMaster.hpp"
#include "NativeRuntime/System/Console.hpp"
#include <chrono>
#include <functional>
#include <stdexcept>
#include <thread>

namespace MphRead::Mods::Diagnostics
{
    using namespace Network;
    namespace Runtime = ::MphRead::NativeRuntime;

    namespace
    {
        void Check(bool condition, const std::string& name)
        {
            if (!condition) { throw std::runtime_error(name); }
            Runtime::ConsoleWriteLine("HOST AUTHORITY PASS " + name);
        }

        void PumpUntil(const std::function<bool()>& condition, const std::string& name)
        {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(12);
            do
            {
                NetSession::Pump();
                if (condition()) { return; }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            } while (std::chrono::steady_clock::now() < deadline);
            throw std::runtime_error("timeout: " + name);
        }

        int FreePorts()
        {
            for (int port = 28100; port < 28200; port += 3)
            {
                if (LocalServer::CanBind(port) && LocalServer::CanBind(port + 1)
                    && LocalServer::CanBind(port + 2)) { return port; }
            }
            throw std::runtime_error("no free test ports");
        }
    }

    std::int32_t HostedAuthorityCheck::Run(const std::string& room)
    {
        std::shared_ptr<DedicatedServerProcess> local{};
        std::shared_ptr<Runtime::Process> localHandle{};
        HostPool pool{};
        int result = 1;
        try
        {
            const int port = FreePorts();
            {
                NetTransport delayedDirectory(0);
                std::jthread replyThread([&](std::stop_token cancel)
                {
                    while (!cancel.stop_requested())
                    {
                        for (const auto& packet : delayedDirectory.Drain())
                        {
                            if (packet.Type() != PacketType::HostRequest) { continue; }
                            std::this_thread::sleep_for(std::chrono::seconds(7));
                            HostReplyPacket reply{};
                            reply.Started = true;
                            reply.Port = static_cast<std::uint16_t>(port);
                            std::array<std::uint8_t, HostReplyPacket::Size> bytes{};
                            reply.Write(bytes);
                            delayedDirectory.Send(packet.Sender, PacketType::HostReply, bytes);
                            return;
                        }
                        std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    }
                });
                const auto delayed = NetMasterClient::RequestGame("127.0.0.1", delayedDirectory.LocalPort(),
                    room, GameMode::Battle, 0, 999, 8, "DelayedDirectory");
                Check(delayed.Started && delayed.Port == port,
                    "production room requests allow startup replies later than six seconds");
            }
            const auto token = Runtime::Guid::NewGuid();
            Runtime::Guid parsed{};
            Check(ServerLaunchOptions::ParseOwnerToken(token.ToString("N"), parsed) && parsed == token,
                "owner token survives process arguments with its exact wire bytes");
            Check(!ServerLaunchOptions::ParseOwnerToken(std::string(32, 'z'), parsed),
                "malformed owner token is rejected");
            Check(NetHostSession::StartAndJoin(port, "LocalCreator", Hunter::Samus, room,
                GameMode::Battle, 0, 999), "local hosting starts a separate server");
            local = NetHostSession::_server;
            localHandle = local->ProcessHandle();
            PumpUntil([] { return NetSession::SnapshotsReceived() > 0; }, "local server snapshots");
            Check(NetSession::IsClient() && !NetSession::IsAuthority() && NetSession::AuthorityFrames() == 0
                && NetHitClaims::Claiming() && NetHitPrediction::Predicting(),
                "creator is a predicted client with shooter claims enabled");
            const auto epoch = NetSession::ServerMatch()->AuthorityEpoch;
            const auto frame = NetSession::LastSnapshotFrame();
            NetSession::Stop();
            NetHostSession::Stop();
            Check(localHandle != nullptr && !localHandle->HasExited(), "creator departure leaves the server running");
            Check(NetLaunch::Connect("127.0.0.1", port, "RemainingPlayer", Hunter::Kanden),
                "another player joins after the creator has left");
            PumpUntil([frame] { return NetSession::LastSnapshotFrame() > frame; }, "continued simulation");
            Check(NetSession::ServerMatch()->AuthorityEpoch == epoch && !NetSession::IsAuthority()
                && NetHitClaims::Claiming(), "departure preserves the server epoch and client claim path");
            NetSession::Stop();
            localHandle->Kill(true);
            localHandle->Dispose();
            localHandle.reset();

            pool.SetPorts(port + 1, port + 2);
            HostRequestPacket request{};
            request.Protocol = NetConfig::ProtocolVersion;
            request.MaxPlayers = 8;
            request.RoomKey = room;
            request.Mode = static_cast<std::uint8_t>(GameMode::Battle);
            request.ServerName = "OwnerLobby";
            request.Policy = ServerSessionPolicy::Lobby;
            request.Format = MatchFormat::FreeForAll;
            request.RequireReady = false;
            request.AllowJoinInProgress = true;
            const auto lobby = pool.Start(request, System::Net::IPEndPoint::Loopback(12345), 0);
            Check(lobby.Started && lobby.OwnerToken != Runtime::Guid::Empty(), "online lobby starts in a child process");
            Check(NetLaunch::Connect("127.0.0.1", lobby.Port, "LobbyCreator", Hunter::Samus,
                8000, -1, lobby.OwnerToken), "lobby creator connects using its owner token");
            PumpUntil([] { return NetSession::LocalIsLobbyOwner(); }, "lobby owner claim");
            Check(NetSession::IsInLobby() && NetSession::CanEditLobby() && !NetSession::IsAuthority(),
                "lobby control does not grant simulation authority");
            const auto status = NetStatus::Query("127.0.0.1", lobby.Port, false);
            Check(status.LobbyEnabled && status.Format == MatchFormat::FreeForAll && status.AllowJoinInProgress,
                "lobby policy, format and join settings survive process launch");
            NetTransport guest(0);
            guest.AnswerPingsImmediately();
            HelloPacket hello{};
            hello.Protocol = NetConfig::ProtocolVersion;
            hello.Slot = HelloPacket::AnySlot;
            hello.ClientId = 399991;
            hello.Secret.fill(23);
            std::array<std::uint8_t, HelloPacket::Size> helloBytes{};
            hello.Write(helloBytes);
            const auto lobbyEndpoint = System::Net::IPEndPoint::Loopback(lobby.Port);
            guest.Send(lobbyEndpoint, PacketType::Hello, helloBytes);
            PumpUntil([] { return NetSession::LobbyRoster().Count == 2; }, "second lobby participant");

            request.Policy = ServerSessionPolicy::Continuous;
            request.Mode = static_cast<std::uint8_t>(GameMode::PrimeHunter);
            request.ServerName = "IndependentPrimeRoom-With-A-Long-Name-For-Readiness";
            const auto second = pool.Start(request, std::make_shared<System::Net::IPEndPoint>(
                std::array<std::uint8_t, 4>{127, 0, 0, 2}, 12346), 1);
            Check(second.Started && pool.Count() == 2 && second.Port != lobby.Port,
                "two rooms run concurrently in separate processes");
            Check(NetStatus::Query("127.0.0.1", second.Port, false).Mode == GameMode::PrimeHunter
                && NetStatus::Query("127.0.0.1", lobby.Port, false).Mode == GameMode::Battle,
                "room modes and world state stay isolated");

            Check(NetSession::SendLobbyCommand(LobbyCommandType::StartMatch), "owner starts the match");
            PumpUntil([] { return NetSession::IsStarting(); }, "load barrier");
            NetSession::MarkMatchLoaded();
            std::array<std::uint8_t, MatchLoadedPacket::Size> loaded{};
            MatchLoadedPacket{NetSession::ServerSession()->MatchId}.Write(loaded);
            guest.Send(lobbyEndpoint, PacketType::MatchLoaded, loaded);
            PumpUntil([] { return NetSession::IsPlaying() && NetSession::SnapshotsReceived() > 0; },
                "server starts the hosted simulation");
            Check(!NetSession::IsAuthority() && NetSession::AuthorityFrames() == 0 && NetHitClaims::Claiming(),
                "lobby creator remains a client while the server runs the match");
            const auto lobbyEpoch = NetSession::ServerMatch()->AuthorityEpoch;
            const auto lobbyFrame = NetSession::LastSnapshotFrame();
            NetSession::Stop();
            Check(NetLaunch::Connect("127.0.0.1", lobby.Port, "AfterOwnerLeft", Hunter::Sylux),
                "hosted lobby accepts a player after its owner leaves");
            PumpUntil([lobbyFrame] { return NetSession::LastSnapshotFrame() > lobbyFrame; }, "hosted simulation continues");
            Check(NetSession::ServerMatch()->AuthorityEpoch == lobbyEpoch && NetSession::IsPlaying()
                && !NetSession::IsAuthority() && NetHitClaims::Claiming(),
                "owner departure does not stop or migrate simulation and claims");
            result = 0;
        }
        catch (const std::exception& error)
        {
            Runtime::ConsoleWriteLine("HOST AUTHORITY FAIL " + std::string(error.what()));
        }
        NetSession::Stop();
        NetHostSession::Stop();
        if (local != nullptr) { local->Stop(); }
        if (localHandle != nullptr)
        {
            try
            {
                if (!localHandle->HasExited()) { localHandle->Kill(true); }
                localHandle->Dispose();
            }
            catch (...) {}
        }
        pool.StopAll("authority check finished");
        return result;
    }
}
