// A .pk3 handed from a player to a server that lacks it, end to end over a
// real loopback UDP socket: the server's ServerMapUpload and the client's
// SessionMapUpload are the production code; only what the engine would do with
// the finished file (read its levels, list the room) is replaced, because that
// needs game files. The cases: a clean transfer, a lossy one (which has to be
// resent), a client that has no such pack, and a transfer that must be refused.

#include "Mods/MapGen/CustomRooms.hpp"
#include "Mods/MapGen/Q3Bsp.hpp"
#include "Mods/Network/Protocol/Packets/MapTransferPackets.hpp"
#include "Mods/Network/Server/Lobby/ServerMapUpload.hpp"
#include "Mods/Network/Session/SessionMapUpload.hpp"
#include "Mods/Network/Transport/NetTransport.hpp"
#include "Metadata/Metadata.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <set>
#include <thread>

namespace fs = std::filesystem;
using namespace MphRead::Mods::Network;

// --- the engine, stood in for -------------------------------------------------

namespace
{
    std::string g_mapDirectory;
    std::set<std::string> g_rooms;
}

namespace MphRead::Mods::MapGen
{
    const std::string& CustomRooms::MapDirectory() { return g_mapDirectory; }
    void CustomRooms::Rescan()
    {
        g_rooms.clear();
        const fs::path uploaded = fs::path(g_mapDirectory) / "uploaded";
        if (fs::is_directory(uploaded))
        {
            for (const auto& entry : fs::directory_iterator(uploaded))
            {
                (void)entry;
                g_rooms.insert("TESTLEVEL");
            }
        }
    }
    // Every .pk3 in this test holds one level, TESTLEVEL, unless its name says otherwise.
    std::vector<std::string> Q3Bsp::ListMaps(const std::string& source)
    {
        if (source.find("bad") != std::string::npos)
        {
            throw std::runtime_error("not a pack");
        }
        return {"TESTLEVEL"};
    }
    std::vector<std::string> Q3Bsp::ListMaps(const std::string* source) { return ListMaps(*source); }

    // Declared by Q3Bsp.hpp for its record types; this test never builds one.
    namespace Q3RecordRuntime
    {
        std::uint32_t TypeHash(const std::type_info&) noexcept { return 0; }
        std::uint32_t StringHash(const Q3String&) noexcept { return 0; }
        std::uint32_t ReferenceHash(const void*) noexcept { return 0; }
        std::string IntString(std::int32_t value) { return std::to_string(value); }
        std::string FloatString(float value) { return std::to_string(value); }
    }
}
namespace MphRead
{
    bool IsDefinedGameMode(std::size_t) { return true; }
}
namespace MphRead::Metadata
{
    std::int32_t AppendLateRooms() { return 0; }
}
namespace MphRead::Mods::Network
{
    std::optional<std::string> ServerMapVoting::ResolveRoomKey(const std::string& roomKey)
    {
        return g_rooms.count(roomKey) != 0 ? std::optional<std::string>(roomKey) : std::nullopt;
    }
}

// --- the server side ----------------------------------------------------------

namespace
{
    int g_failures = 0;
    void Check(bool ok, const char* what)
    {
        std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
        if (!ok)
        {
            ++g_failures;
        }
    }

    class FakeHost final : public ServerMapVotingHost
    {
    public:
        FakeHost(NetTransport& transport) : _transport(transport) {}
        ServerPeerRegistry& VotingPeers() noexcept override { return Peers; }
        bool MapVotesAllowed() const noexcept override { return true; }
        SessionPhase VotingPhase() const noexcept override { return SessionPhase::InMatch; }
        MatchDefinition VotingDefinition() const override { return {}; }
        MphRead::GameMode VotingModeForRoom(const std::string&) const override { return MphRead::GameMode::Battle; }
        void SetPendingMapPick(const std::string&, MphRead::GameMode) override {}
        void ClearPendingMapPick() override {}
        void MarkVotingPeerSeen(const ServerPeerRegistry::PeerHandle&, double) noexcept override {}
        void TellVotingPeer(const ServerPeerRegistry::PeerHandle&, const std::string& text) override
        {
            Told.push_back(text);
        }
        void AnnounceVote(const std::string&) override {}
        void LogVote(const std::string& text) override { std::printf("  [server] %s\n", text.c_str()); }
        void SendVotePacket(const ServerPeerRegistry::PeerHandle& peer, PacketType type,
            std::span<const std::uint8_t> payload) override
        {
            ++Sent;
            _transport.Send(peer->EndPoint, type, payload);
        }
        void ApplyAcceptedMapVote(double, const std::string&, MphRead::GameMode) override {}

        ServerPeerRegistry Peers;
        std::vector<std::string> Told;
        int Sent = 0;

    private:
        NetTransport& _transport;
    };

    struct Rig
    {
        std::string Root;
        NetTransport Server{0};
        FakeHost Host{Server};
        ServerMapUpload Upload{Host};
        SessionConnection Client;
        ServerPeerRegistry::PeerHandle Peer;
        std::string ArrivedRoom;
        bool Arrived = false;
        double Now = 100.0;
        int DropEvery = 0;
        int ChunksSeen = 0;

        explicit Rig(const std::string& root) : Root(root)
        {
            fs::remove_all(Root);
            fs::create_directories(fs::path(Root) / "client");
            fs::create_directories(fs::path(Root) / "server");
            Client.Open(0);
            const auto serverEnd = std::make_shared<System::Net::IPEndPoint>(
                std::array<std::uint8_t, 4>{127, 0, 0, 1}, Server.LocalPort());
            Client.SetHostEndpoint(serverEnd);
            // The server knows the client by the address its datagrams come from; send one
            // first so the test can learn it the same way the real server does.
            const std::array<std::uint8_t, 1> hello{0};
            Client.SendToHost(PacketType::Ping, hello);
            for (int i = 0; i < 200 && Peer == nullptr; ++i)
            {
                for (ReceivedPacket packet : Server.Drain())
                {
                    Peer = std::make_shared<ServerPeerRegistry::Peer>();
                    Peer->EndPoint = packet.Sender;
                    Peer->SlotIndex = 0;
                    Peer->ClientId = 7;
                    Host.Peers.Add(Peer);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            Upload.OnArrived([this](const ServerPeerRegistry::PeerHandle&, const std::string& room, double)
            {
                Arrived = true;
                ArrivedRoom = room;
            });
        }

        std::string ClientDir() const { return (fs::path(Root) / "client").string(); }
        std::string ServerDir() const { return (fs::path(Root) / "server").string(); }

        void Pump(double seconds)
        {
            const double end = Now + seconds;
            while (Now < end && Upload.Active())
            {
                Now += 0.01;
                for (ReceivedPacket packet : Server.Drain())
                {
                    g_mapDirectory = ServerDir();
                    if (packet.Type() == PacketType::MapOffer)
                    {
                        Upload.HandleOffer(Host.Peers.Find(packet.Sender), packet.Payload(), Now);
                    }
                    else if (packet.Type() == PacketType::MapChunk)
                    {
                        ++ChunksSeen;
                        if (DropEvery != 0 && ChunksSeen % DropEvery == 0)
                        {
                            continue;
                        }
                        Upload.HandleChunk(Host.Peers.Find(packet.Sender), packet.Payload(), Now);
                    }
                }
                for (ReceivedPacket packet : Client.Drain())
                {
                    if (packet.Type() == PacketType::MapWant)
                    {
                        g_mapDirectory = ClientDir();
                        SessionMapUpload::Answer(Client, NetRole::Client, packet.Payload());
                    }
                }
                g_mapDirectory = ServerDir();
                Upload.Update(Now);
                std::this_thread::sleep_for(std::chrono::milliseconds(3));
            }
        }
    };

    std::vector<std::uint8_t> WritePack(const std::string& path, std::size_t bytes)
    {
        std::vector<std::uint8_t> data(bytes);
        std::mt19937 rng(1234);
        for (auto& b : data)
        {
            b = static_cast<std::uint8_t>(rng());
        }
        fs::create_directories(fs::path(path).parent_path());
        std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(data.data()),
            static_cast<std::streamsize>(data.size()));
        return data;
    }

    std::vector<std::uint8_t> ReadFile(const fs::path& path)
    {
        std::ifstream in(path, std::ios::binary);
        return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    }

    fs::path Uploaded(const Rig& rig)
    {
        const fs::path folder = fs::path(rig.ServerDir()) / "uploaded";
        if (fs::is_directory(folder))
        {
            for (const auto& entry : fs::directory_iterator(folder))
            {
                return entry.path();
            }
        }
        return {};
    }
}

int main(int, char** argv)
{
    const std::string base = (fs::temp_directory_path() / "fp-mapupload-test").string();
    (void)argv;

    {   // A clean transfer: a size that is not a multiple of a chunk.
        Rig rig(base + "/clean");
        const auto data = WritePack(rig.ClientDir() + "/packs/arena.pk3", 700001);
        g_mapDirectory = rig.ServerDir();
        Check(rig.Upload.Begin(rig.Peer, "TESTLEVEL", rig.Now), "clean: server asks for the map");
        rig.Pump(20);
        Check(rig.Arrived && rig.ArrivedRoom == "TESTLEVEL", "clean: the room is listed and the vote can start");
        Check(!Uploaded(rig).empty() && ReadFile(Uploaded(rig)) == data, "clean: stored file is byte-identical");
        Check(!rig.Upload.Active(), "clean: transfer is finished");
    }

    {   // One chunk in five lost on the way to the server.
        Rig rig(base + "/lossy");
        const auto data = WritePack(rig.ClientDir() + "/arena.pk3", 400000);
        rig.DropEvery = 5;
        g_mapDirectory = rig.ServerDir();
        Check(rig.Upload.Begin(rig.Peer, "TESTLEVEL", rig.Now), "lossy: server asks for the map");
        rig.Pump(60);
        Check(rig.Arrived && ReadFile(Uploaded(rig)) == data, "lossy: arrives intact despite 20% loss");
    }

    {   // The client has no pack with that level.
        Rig rig(base + "/absent");
        g_mapDirectory = rig.ServerDir();
        Check(rig.Upload.Begin(rig.Peer, "TESTLEVEL", rig.Now), "absent: server asks");
        rig.Pump(ServerMapUpload::OfferSeconds + 1);
        Check(!rig.Arrived && !rig.Upload.Active(), "absent: gives up when nothing is offered");
        Check(Uploaded(rig).empty(), "absent: nothing was written");
    }

    {   // A pack the importer cannot open is deleted again.
        Rig rig(base + "/bad");
        WritePack(rig.ClientDir() + "/bad.pk3", 5000);
        g_mapDirectory = rig.ServerDir();
        Check(rig.Upload.Begin(rig.Peer, "TESTLEVEL", rig.Now), "unreadable: server asks");
        rig.Pump(20);
        // The client's own check (ListMaps) already refuses an unreadable pack, so
        // the server hears nothing; either way nothing may be kept.
        Check(!rig.Arrived && Uploaded(rig).empty(), "unreadable: nothing kept, no room listed");
    }

    {   // Switched off.
        Rig rig(base + "/off");
        ServerMapUpload::Enabled(false);
        Check(!rig.Upload.Begin(rig.Peer, "TESTLEVEL", rig.Now), "off: -nomapupload refuses");
        ServerMapUpload::Enabled(true);
        Check(rig.Host.Sent == 0, "off: nothing was sent");
    }

    {   // A packet that lies about its length is dropped, not trusted.
        std::array<std::uint8_t, 6> lying{0, 0, 0, 0, 0xE8, 0x03};
        MapChunkPacket chunk{};
        Check(!MapChunkPacket::TryRead(lying, chunk), "packet: chunk longer than its datagram is refused");
    }

    fs::remove_all(base);
    std::printf(g_failures == 0 ? "ALL PASS\n" : "%d FAILED\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
