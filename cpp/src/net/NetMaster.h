#pragma once

// The server directory (the C#'s NetMaster.cs): servers announce themselves
// to it every 15 s, the browser asks it who is up, and it starts matches for
// players who cannot open a port of their own. Wire-compatible with the C#'s
// directory, servers and launchers.

#include "NetProtocol.h"
#include "NetTransport.h"

#include <atomic>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace fp::net {

// NetMasterConfig
constexpr const char* MasterDefaultHost = "net.livetek.fr";
constexpr uint16_t MasterDefaultPort = 27889;
constexpr double MasterHeartbeatSeconds = 15.0, MasterExpirySeconds = 50.0;
constexpr uint8_t PacketMasterHeartbeat = 17, PacketMasterQuery = 18, PacketMasterList = 19, PacketHostRequest = 20,
                  PacketHostReply = 21;

// MasterEntryPacket: one server in the directory's answer.
struct MasterEntry {
    static constexpr int MaxNameBytes = 32, MaxRoomBytes = 40;
    static constexpr int Size = 4 + 2 + 1 + 1 + 1 + 1 + MaxNameBytes + MaxRoomBytes;
    uint32_t address = 0; // host order
    uint16_t port = 0;
    uint8_t players = 0, maxPlayers = 0, mode = 0, protocol = 0; // mode: the wire's GameMode
    std::string name, roomKey;
    void write(std::span<uint8_t> dest) const;
    static MasterEntry read(std::span<const uint8_t> src);
};

// MasterHeartbeatPacket: a server saying it is up and what it runs.
struct MasterHeartbeat {
    static constexpr int Size = 1 + 2 + 1 + 1 + 1 + MasterEntry::MaxNameBytes + MasterEntry::MaxRoomBytes;
    uint8_t protocol = ProtocolVersion;
    uint16_t port = 0;
    uint8_t players = 0, maxPlayers = 0, mode = 0;
    std::string name, roomKey;
    void write(std::span<uint8_t> dest) const;
    static MasterHeartbeat read(std::span<const uint8_t> src);
};

// HostRequestPacket: "run a game for me" -- a map cycle and the session's shape.
struct HostRequest {
    static constexpr int MaxRoomBytes = 40, MaxNameBytes = 32;
    static constexpr int Size = 1 + 1 + 1 + 2 + 2 + MaxRoomBytes + MaxNameBytes;
    static constexpr int MaxRotation = 16, RotationEntrySize = MaxRoomBytes + 1;
    uint8_t protocol = ProtocolVersion, maxPlayers = 4, mode = 3; // mode: the wire's GameMode
    uint16_t timeLimit = 7 * 60, pointGoal = 7;                  // seconds
    std::string roomKey, serverName;
    std::vector<std::pair<std::string, uint8_t>> rotation; // entry 0 is roomKey
    uint8_t policy = SessionState::PolicyContinuous, format = SessionState::FormatAuto;
    bool allowJoinInProgress = true, requireReady = true;
    size_t length() const { return Size + 1 + std::min<size_t>(rotation.size(), MaxRotation) * RotationEntrySize + 4; }
    void write(std::span<uint8_t> dest) const;
    static std::optional<HostRequest> read(std::span<const uint8_t> src);
};

// HostReplyPacket: the port the game runs on, or why it does not.
struct HostReply {
    static constexpr int MaxReasonBytes = 96;
    static constexpr int Size = 1 + 2 + MaxReasonBytes + 16;
    bool started = false;
    uint16_t port = 0;
    std::string reason;
    std::array<uint8_t, 16> ownerToken{}; // a lobby's: the asker presents it and owns the lobby
    void write(std::span<uint8_t> dest) const;
    static HostReply read(std::span<const uint8_t> src);
};

// One row of the directory's list, and the list.
struct MasterListing {
    std::string address;
    uint16_t port = 0;
    std::string name, roomKey;
    int mode = 3, players = 0, maxPlayers = 0, protocol = 0;
};
struct MasterListResult {
    std::vector<MasterListing> servers;
    bool answered = false;
    std::optional<bool> canHost; // null: too old a directory to say
};
// NetMasterClient.Query: who is up, as the directory knows it.
MasterListResult queryMaster(const std::string& host, uint16_t port = MasterDefaultPort, int timeoutMs = 1500);
// NetMasterClient.RequestGame: ask a directory (or a server that hosts) to start a game.
HostReply requestGame(const std::string& host, uint16_t port, const HostRequest& request, int timeoutMs = 5000);

// MasterReporter: a server's heartbeat to the directory. Every failure is
// swallowed and retried: a directory outage never touches a match.
class MasterReporter {
public:
    MasterReporter(std::string host, uint16_t port);
    void beat(double now, const MasterHeartbeat& heartbeat);
    void farewell(uint16_t gamePort);
    const std::string& host() const { return m_host; }

private:
    bool resolve(double now);
    std::string m_host;
    uint16_t m_port;
    Transport m_transport;
    std::optional<Endpoint> m_endpoint;
    double m_lastBeat = -1e9, m_lastResolve = -1e9;
    bool m_complained = false;
};

// HostPool: matches started on this machine for other players, each an
// ordinary dedicated server in a process of its own that runs the match
// itself (never a client's), on a port from a range, reaped once idle.
class HostPool {
public:
    HostPool(std::filesystem::path gameFiles, uint16_t first, uint16_t last, std::string masterHost, uint16_t masterPort);
    ~HostPool();
    bool enabled() const { return m_first > 0 && m_last >= m_first; }
    HostReply start(const HostRequest& request, const Endpoint& asker);
    void reap();
    size_t count() const { return m_games.size(); }
    void stopAll();

private:
    struct Game;
    std::filesystem::path m_files;
    uint16_t m_first, m_last;
    std::string m_masterHost;
    uint16_t m_masterPort;
    std::vector<std::unique_ptr<Game>> m_games;
};

// MasterServer: the directory. Servers announce every 15 s and are
// forgotten after 50 s of silence; a MasterQuery is answered in as many
// datagrams as the list takes; a HostRequest starts a game from the pool.
class MasterServer {
public:
    struct Config {
        uint16_t port = MasterDefaultPort;
        std::string publicHost; // published for servers registering from loopback or a private range
        std::filesystem::path gameFiles;
        uint16_t hostFirst = 27900, hostLast = 27919; // 0: starts no games
    };
    explicit MasterServer(Config config);
    bool run(const std::atomic<bool>* stop);

private:
    struct Entry {
        Endpoint key;
        MasterEntry wire;
        double lastSeen = 0;
    };
    void handle(const ReceivedPacket& packet, double now);
    void sendList(const Endpoint& to);
    Config m_config;
    Transport m_transport;
    std::vector<Entry> m_entries;
    std::unique_ptr<HostPool> m_pool;
    uint32_t m_publicAddress = 0;
};

// A private or loopback IPv4 address (host order).
bool isLocalAddress(uint32_t address);

} // namespace fp::net
