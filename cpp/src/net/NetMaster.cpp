#include "NetMaster.h"

#include "NetClient.h"

#include <QCoreApplication>
#include <QDir>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <random>
#include <thread>

namespace fp::net {

namespace {

// How long a started game waits for its first player, and then for anybody at all.
constexpr int HostedStartupSeconds = 180, HostedEmptySeconds = 45;

double clockNow() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

void log(const char* format, auto... args)
{
    const std::time_t t = std::time(nullptr);
    char stamp[16];
    std::strftime(stamp, sizeof stamp, "%H:%M:%S", std::localtime(&t));
    std::printf("[%s] [master] ", stamp);
    if constexpr (sizeof...(args) == 0) {
        std::fputs(format, stdout);
    } else {
        std::printf(format, args...);
    }
    std::putchar('\n');
    std::fflush(stdout);
}

std::string addressText(uint32_t a)
{
    return std::to_string(a >> 24) + "." + std::to_string((a >> 16) & 0xFF) + "." + std::to_string((a >> 8) & 0xFF) + "."
        + std::to_string(a & 0xFF);
}

const char* rotationMode(int wire)
{
    static const char* names[] = {"Battle", "BattleTeams", "Survival", "SurvivalTeams", "Capture", "Bounty", "BountyTeams", "Nodes",
        "NodesTeams", "Defender", "DefenderTeams", "PrimeHunter"};
    const int mode = portMode(wire);
    return mode >= 0 ? names[mode] : "Battle";
}

} // namespace

bool isLocalAddress(uint32_t a)
{
    const uint32_t o0 = a >> 24, o1 = (a >> 16) & 0xFF;
    return o0 == 127 || o0 == 10 || (o0 == 172 && o1 >= 16 && o1 <= 31) || (o0 == 192 && o1 == 168) || (o0 == 169 && o1 == 254);
}

// ---- packets ---------------------------------------------------------------------

void MasterEntry::write(std::span<uint8_t> dest) const
{
    // The address big-endian, as the C# writes it.
    dest[0] = static_cast<uint8_t>(address >> 24);
    dest[1] = static_cast<uint8_t>(address >> 16);
    dest[2] = static_cast<uint8_t>(address >> 8);
    dest[3] = static_cast<uint8_t>(address);
    writeU16(dest, 4, port);
    dest[6] = players;
    dest[7] = maxPlayers;
    dest[8] = mode;
    dest[9] = protocol;
    writeText(dest.subspan(10, MaxNameBytes), name);
    writeText(dest.subspan(10 + MaxNameBytes, MaxRoomBytes), roomKey);
}

MasterEntry MasterEntry::read(std::span<const uint8_t> src)
{
    MasterEntry e;
    e.address = static_cast<uint32_t>(src[0]) << 24 | static_cast<uint32_t>(src[1]) << 16 | static_cast<uint32_t>(src[2]) << 8 | src[3];
    e.port = readU16(src, 4);
    e.players = src[6];
    e.maxPlayers = src[7];
    e.mode = src[8];
    e.protocol = src[9];
    e.name = readText(src.subspan(10, MaxNameBytes));
    e.roomKey = readText(src.subspan(10 + MaxNameBytes, MaxRoomBytes));
    return e;
}

void MasterHeartbeat::write(std::span<uint8_t> dest) const
{
    dest[0] = protocol;
    writeU16(dest, 1, port);
    dest[3] = players;
    dest[4] = maxPlayers;
    dest[5] = mode;
    writeText(dest.subspan(6, MasterEntry::MaxNameBytes), name);
    writeText(dest.subspan(6 + MasterEntry::MaxNameBytes, MasterEntry::MaxRoomBytes), roomKey);
}

MasterHeartbeat MasterHeartbeat::read(std::span<const uint8_t> src)
{
    MasterHeartbeat h;
    h.protocol = src[0];
    h.port = readU16(src, 1);
    h.players = src[3];
    h.maxPlayers = src[4];
    h.mode = src[5];
    h.name = readText(src.subspan(6, MasterEntry::MaxNameBytes));
    h.roomKey = readText(src.subspan(6 + MasterEntry::MaxNameBytes, MasterEntry::MaxRoomBytes));
    return h;
}

void HostRequest::write(std::span<uint8_t> dest) const
{
    std::fill(dest.begin(), dest.begin() + static_cast<long>(length()), 0);
    dest[0] = protocol;
    dest[1] = maxPlayers;
    dest[2] = mode;
    writeU16(dest, 3, timeLimit);
    writeU16(dest, 5, pointGoal);
    writeText(dest.subspan(7, MaxRoomBytes), roomKey);
    writeText(dest.subspan(7 + MaxRoomBytes, MaxNameBytes), serverName);
    const size_t count = std::min<size_t>(rotation.size(), MaxRotation);
    dest[Size] = static_cast<uint8_t>(count);
    for (size_t i = 0; i < count; i++) {
        const size_t at = Size + 1 + i * RotationEntrySize;
        writeText(dest.subspan(at, MaxRoomBytes), rotation[i].first);
        dest[at + MaxRoomBytes] = rotation[i].second;
    }
    const size_t tail = Size + 1 + count * RotationEntrySize;
    dest[tail] = policy;
    dest[tail + 1] = allowJoinInProgress ? 1 : 0;
    dest[tail + 2] = requireReady ? 1 : 0;
    dest[tail + 3] = format;
}

std::optional<HostRequest> HostRequest::read(std::span<const uint8_t> src)
{
    if (src.size() < static_cast<size_t>(Size) + 5 || src[Size] > MaxRotation) {
        return std::nullopt;
    }
    const size_t tail = Size + 1 + static_cast<size_t>(src[Size]) * RotationEntrySize;
    if (src.size() != tail + 4 || src[tail] > 1 || src[tail + 1] > 1 || src[tail + 2] > 1 || src[tail + 3] > 6) {
        return std::nullopt;
    }
    HostRequest r;
    r.protocol = src[0];
    r.maxPlayers = src[1];
    r.mode = src[2];
    r.timeLimit = readU16(src, 3);
    r.pointGoal = readU16(src, 5);
    r.roomKey = readText(src.subspan(7, MaxRoomBytes));
    r.serverName = readText(src.subspan(7 + MaxRoomBytes, MaxNameBytes));
    for (int i = 0; i < src[Size]; i++) {
        const size_t at = Size + 1 + static_cast<size_t>(i) * RotationEntrySize;
        std::string room = readText(src.subspan(at, MaxRoomBytes));
        if (!room.empty()) {
            const uint8_t mode = src[at + MaxRoomBytes];
            r.rotation.emplace_back(room, portMode(mode) >= 0 ? mode : static_cast<uint8_t>(3));
        }
    }
    r.policy = src[tail];
    r.allowJoinInProgress = src[tail + 1] != 0;
    r.requireReady = src[tail + 2] != 0;
    r.format = src[tail + 3];
    return r;
}

void HostReply::write(std::span<uint8_t> dest) const
{
    std::fill(dest.begin(), dest.begin() + Size, 0);
    dest[0] = started ? 1 : 0;
    writeU16(dest, 1, port);
    writeText(dest.subspan(3, MaxReasonBytes), reason);
    std::copy(ownerToken.begin(), ownerToken.end(), dest.begin() + 3 + MaxReasonBytes);
}

HostReply HostReply::read(std::span<const uint8_t> src)
{
    HostReply r;
    r.started = src[0] != 0;
    r.port = readU16(src, 1);
    r.reason = readText(src.subspan(3, MaxReasonBytes));
    std::copy(src.begin() + 3 + MaxReasonBytes, src.begin() + 3 + MaxReasonBytes + 16, r.ownerToken.begin());
    return r;
}

// ---- the client's end -----------------------------------------------------------

MasterListResult queryMaster(const std::string& host, uint16_t port, int timeoutMs)
{
    MasterListResult result;
    const std::optional<Endpoint> master = Endpoint::resolve(host, port);
    Transport transport;
    if (!master || !transport.open(0)) {
        return result;
    }
    const uint8_t version = ProtocolVersion;
    transport.send(*master, PacketMasterQuery, {&version, 1});
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    int total = -1;
    while (std::chrono::steady_clock::now() < deadline && (total < 0 || static_cast<int>(result.servers.size()) < total)) {
        for (const ReceivedPacket& packet : transport.drain()) {
            const std::span<const uint8_t> p(packet.data);
            if (p.size() < 3 || p[0] != PacketMasterList) {
                continue;
            }
            result.answered = true;
            const int count = p[1];
            total = p[2];
            size_t offset = 3;
            for (int i = 0; i < count && offset + MasterEntry::Size <= p.size(); i++) {
                const MasterEntry e = MasterEntry::read(p.subspan(offset, MasterEntry::Size));
                offset += MasterEntry::Size;
                result.servers.push_back({addressText(e.address), e.port, e.name, e.roomKey, e.mode, e.players, e.maxPlayers, e.protocol});
            }
            if (offset < p.size()) {
                result.canHost = (p[offset] & 1) != 0; // MasterFlags.CanHost
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return result;
}

HostReply requestGame(const std::string& host, uint16_t port, const HostRequest& request, int timeoutMs)
{
    HostReply reply;
    const std::optional<Endpoint> master = Endpoint::resolve(host, port);
    Transport transport;
    if (!master || !transport.open(0)) {
        reply.reason = "cannot reach " + host;
        return reply;
    }
    std::vector<uint8_t> buffer(request.length());
    request.write(buffer);
    const auto start = std::chrono::steady_clock::now();
    auto lastSend = start - std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() - start < std::chrono::milliseconds(timeoutMs)) {
        if (std::chrono::steady_clock::now() - lastSend > std::chrono::milliseconds(1000)) {
            lastSend = std::chrono::steady_clock::now();
            transport.send(*master, PacketHostRequest, buffer);
        }
        for (const ReceivedPacket& packet : transport.drain()) {
            if (packet.data.size() >= 1 + static_cast<size_t>(HostReply::Size) && packet.data[0] == PacketHostReply) {
                return HostReply::read(std::span<const uint8_t>(packet.data).subspan(1));
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    reply.reason = host + " did not answer";
    return reply;
}

// ---- MasterReporter --------------------------------------------------------------

MasterReporter::MasterReporter(std::string host, uint16_t port)
    : m_host(std::move(host))
    , m_port(port)
{
}

bool MasterReporter::resolve(double now)
{
    if (m_endpoint && now - m_lastResolve < 3600) {
        return true;
    }
    m_endpoint = Endpoint::resolve(m_host, m_port);
    if (!m_endpoint) {
        if (!m_complained) {
            m_complained = true;
            log("cannot reach the directory %s:%u; pass --no-master to stop trying, or --master HOST to point elsewhere", m_host.c_str(),
                m_port);
        }
        return false;
    }
    m_lastResolve = now;
    if (!m_transport.isOpen()) {
        m_transport.open(0);
    }
    m_complained = false;
    return true;
}

void MasterReporter::beat(double now, const MasterHeartbeat& heartbeat)
{
    if (now - m_lastBeat < MasterHeartbeatSeconds) {
        return;
    }
    m_lastBeat = now;
    if (!resolve(now)) {
        return;
    }
    std::array<uint8_t, MasterHeartbeat::Size> buffer{};
    heartbeat.write(buffer);
    m_transport.send(*m_endpoint, PacketMasterHeartbeat, buffer);
    m_transport.drain(); // nothing is expected back
}

void MasterReporter::farewell(uint16_t gamePort)
{
    if (!m_endpoint || !m_transport.isOpen()) {
        return;
    }
    std::array<uint8_t, 2> buffer{};
    writeU16(buffer, 0, gamePort);
    m_transport.send(*m_endpoint, static_cast<uint8_t>(PacketType::Bye), buffer);
}

// ---- HostPool ----------------------------------------------------------------------

struct HostPool::Game {
    QProcess process;
    uint16_t port = 0;
    uint32_t asker = 0;
    std::string name;
    std::unique_ptr<QTemporaryDir> dir;
};

HostPool::HostPool(std::filesystem::path gameFiles, uint16_t first, uint16_t last, std::string masterHost, uint16_t masterPort)
    : m_files(std::move(gameFiles))
    , m_first(first)
    , m_last(last)
    , m_masterHost(std::move(masterHost))
    , m_masterPort(masterPort)
{
}

HostPool::~HostPool() { stopAll(); }

void HostPool::stopAll()
{
    for (auto& game : m_games) {
        game->process.terminate();
        if (!game->process.waitForFinished(3000)) {
            game->process.kill();
            game->process.waitForFinished(1000);
        }
    }
    m_games.clear();
}

void HostPool::reap()
{
    for (size_t i = m_games.size(); i-- > 0;) {
        Game& game = *m_games[i];
        game.process.waitForFinished(0);
        if (game.process.state() == QProcess::NotRunning) {
            log("game on %u (%s) ended", game.port, game.name.c_str());
            m_games.erase(m_games.begin() + static_cast<long>(i));
        }
    }
}

HostReply HostPool::start(const HostRequest& request, const Endpoint& asker)
{
    HostReply reply;
    reap();
    // The same player asking again: the empty game it asked for before goes.
    for (size_t i = m_games.size(); i-- > 0;) {
        Game& game = *m_games[i];
        int latency = 0;
        const auto status = game.asker == asker.address ? queryStatus("127.0.0.1", game.port, 300, latency) : std::nullopt;
        if (status && status->match.playerCount == 0) {
            log("game on %u stopped: the same player asked for another game", game.port);
            game.process.terminate();
            game.process.waitForFinished(2000);
            m_games.erase(m_games.begin() + static_cast<long>(i));
        }
    }
    uint16_t port = 0;
    for (uint32_t candidate = m_first; candidate <= m_last && port == 0; candidate++) {
        const bool taken = std::any_of(m_games.begin(), m_games.end(), [&](const auto& g) { return g->port == candidate; });
        Transport probe;
        if (!taken && probe.open(static_cast<uint16_t>(candidate))) {
            port = static_cast<uint16_t>(candidate);
        }
    }
    if (port == 0) {
        reply.reason = "all " + std::to_string(m_last - m_first + 1) + " game slots are busy";
        return reply;
    }
    auto game = std::make_unique<Game>();
    game->port = port;
    game->asker = asker.address;
    game->name = request.serverName.empty() ? "Hosted game" : request.serverName;
    game->dir = std::make_unique<QTemporaryDir>();
    // The asker's whole cycle, entry 0 being the map it asked for.
    std::vector<std::pair<std::string, uint8_t>> maps = request.rotation;
    if (maps.empty()) {
        maps.emplace_back(request.roomKey, request.mode);
    }
    const QString rotationPath = game->dir->filePath(QStringLiteral("rotation.txt"));
    if (FILE* f = std::fopen(rotationPath.toLocal8Bit().constData(), "w")) {
        for (const auto& [room, mode] : maps) {
            std::fprintf(f, "%s | %s | %.3f | %u\n", room.c_str(), rotationMode(mode), request.timeLimit / 60.0, request.pointGoal);
        }
        std::fclose(f);
    }
    QStringList args{QStringLiteral("--server"), QStringLiteral("--files"), QString::fromStdString(m_files.string()),
        QStringLiteral("--port"), QString::number(port), QStringLiteral("--max-players"), QString::number(std::clamp<int>(request.maxPlayers, 2, 8)),
        QStringLiteral("--server-name"), QString::fromStdString(game->name), QStringLiteral("--rotation"), rotationPath,
        QStringLiteral("--exit-when-empty"), QString::number(HostedEmptySeconds), QStringLiteral("--startup-grace"),
        QString::number(HostedStartupSeconds)};
    static const char* formats[] = {"auto", "ffa", "1v1", "2v2", "3v3", "4v4", "2v2v2v2"};
    args << QStringLiteral("--format") << QString::fromLatin1(formats[std::min<int>(request.format, 6)]);
    if (m_masterHost.empty()) {
        args << QStringLiteral("--no-master");
    } else {
        args << QStringLiteral("--master") << QString::fromStdString(m_masterHost + ":" + std::to_string(m_masterPort));
    }
    if (request.policy == SessionState::PolicyLobby) {
        std::random_device random;
        for (uint8_t& b : reply.ownerToken) {
            b = static_cast<uint8_t>(random());
        }
        // Back into Guid.ToString("N") order, which --owner-token parses.
        std::array<uint8_t, 16> text = reply.ownerToken;
        std::reverse(text.begin(), text.begin() + 4);
        std::reverse(text.begin() + 4, text.begin() + 6);
        std::reverse(text.begin() + 6, text.begin() + 8);
        QString hex;
        for (uint8_t b : text) {
            hex += QStringLiteral("%1").arg(b, 2, 16, QChar('0'));
        }
        args << QStringLiteral("--lobby") << QStringLiteral("--owner-token") << hex;
    }
    if (!request.allowJoinInProgress) {
        args << QStringLiteral("--no-join-in-progress");
    }
    game->process.setProgram(QCoreApplication::applicationFilePath());
    game->process.setArguments(args);
    game->process.setProcessChannelMode(QProcess::ForwardedChannels);
    game->process.start();
    if (!game->process.waitForStarted(5000)) {
        reply.reason = "could not start a server";
        return reply;
    }
    // Answer once it listens: the asker's Hello is on its way.
    for (int i = 0; i < 100; i++) {
        int latency = 0;
        if (queryStatus("127.0.0.1", port, 100, latency)) {
            break;
        }
        if (game->process.state() == QProcess::NotRunning) {
            reply.reason = "the server stopped at once (the map may not load)";
            return reply;
        }
    }
    log("started a game on %u for %s: \"%s\", %zu map(s) from %s", port, addressText(asker.address).c_str(), game->name.c_str(), maps.size(),
        maps.front().first.c_str());
    m_games.push_back(std::move(game));
    reply.started = true;
    reply.port = port;
    return reply;
}

// ---- MasterServer ------------------------------------------------------------------

MasterServer::MasterServer(Config config)
    : m_config(std::move(config))
{
    if (m_config.hostFirst > 0 && m_config.hostLast >= m_config.hostFirst) {
        m_pool = std::make_unique<HostPool>(m_config.gameFiles, m_config.hostFirst, m_config.hostLast, "127.0.0.1", m_config.port);
    }
}

bool MasterServer::run(const std::atomic<bool>* stop)
{
    if (!m_transport.open(m_config.port)) {
        log("cannot listen on UDP %u: %s", m_config.port, m_transport.error().c_str());
        return false;
    }
    if (!m_config.publicHost.empty()) {
        if (const auto resolved = Endpoint::resolve(m_config.publicHost, 0)) {
            m_publicAddress = resolved->address;
            log("servers on this machine or its network are published as %s (%s)", m_config.publicHost.c_str(),
                addressText(m_publicAddress).c_str());
        } else {
            log("cannot resolve %s: servers on this machine are listed as they come", m_config.publicHost.c_str());
        }
    }
    log("directory listening on UDP %u; %s", m_transport.localPort(),
        m_pool ? ("can start games on ports " + std::to_string(m_config.hostFirst) + "-" + std::to_string(m_config.hostLast)).c_str()
               : "not starting games for anybody");
    double lastReport = clockNow();
    while (stop == nullptr || !*stop) {
        const double now = clockNow();
        for (const ReceivedPacket& packet : m_transport.drain()) {
            handle(packet, now);
        }
        std::erase_if(m_entries, [&](const Entry& e) { return now - e.lastSeen > MasterExpirySeconds; });
        if (m_pool) {
            m_pool->reap();
        }
        if (now - lastReport >= 60) {
            lastReport = now;
            log("%zu server(s) listed%s", m_entries.size(),
                m_pool && m_pool->count() > 0 ? (", " + std::to_string(m_pool->count()) + " started here").c_str() : "");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    log("shutting down");
    if (m_pool) {
        m_pool->stopAll();
    }
    m_transport.close();
    return true;
}

void MasterServer::handle(const ReceivedPacket& packet, double now)
{
    if (packet.data.empty()) {
        return;
    }
    const std::span<const uint8_t> p = std::span<const uint8_t>(packet.data).subspan(1);
    const uint8_t type = packet.data[0];
    if (type == PacketMasterHeartbeat && p.size() >= static_cast<size_t>(MasterHeartbeat::Size)) {
        const MasterHeartbeat beat = MasterHeartbeat::read(p);
        // The address the heartbeat came from, not the one the server believes it has.
        uint32_t address = packet.sender.address;
        if (m_publicAddress != 0 && isLocalAddress(address)) {
            address = m_publicAddress;
        }
        const uint16_t port = beat.port != 0 ? beat.port : packet.sender.port;
        const Endpoint key{packet.sender.address, port};
        auto it = std::find_if(m_entries.begin(), m_entries.end(), [&](const Entry& e) { return e.key == key; });
        if (it == m_entries.end()) {
            m_entries.push_back({key, {}, now});
            it = m_entries.end() - 1;
            log("%s \"%s\" is up", key.toString().c_str(), beat.name.c_str());
        }
        it->wire = {address, port, beat.players, beat.maxPlayers, beat.mode, beat.protocol, beat.name, beat.roomKey};
        it->lastSeen = now;
    } else if (type == PacketMasterQuery) {
        sendList(packet.sender);
    } else if (type == static_cast<uint8_t>(PacketType::Bye) && p.size() >= 2) {
        const Endpoint key{packet.sender.address, readU16(p, 0)};
        std::erase_if(m_entries, [&](const Entry& e) { return e.key == key; });
    } else if (type == PacketHostRequest) {
        HostReply reply;
        const auto request = HostRequest::read(p);
        if (!request) {
            reply.reason = "malformed request";
        } else if (request->protocol != ProtocolVersion) {
            reply.reason = "this directory speaks protocol " + std::to_string(ProtocolVersion) + ", your build speaks "
                + std::to_string(request->protocol);
        } else if (!m_pool) {
            reply.reason = "this directory does not start games";
        } else {
            reply = m_pool->start(*request, packet.sender);
        }
        std::array<uint8_t, HostReply::Size> buffer{};
        reply.write(buffer);
        m_transport.send(packet.sender, PacketHostReply, buffer);
        if (!reply.started) {
            log("refused a game for %s: %s", packet.sender.toString().c_str(), reply.reason.c_str());
        }
    }
}

void MasterServer::sendList(const Endpoint& to)
{
    // As many datagrams as the list takes, each one saying how many there are in all.
    const int perPacket = (MaxPacketSize - 1 - 2) / MasterEntry::Size;
    const int total = std::min<int>(static_cast<int>(m_entries.size()), 255);
    int sent = 0;
    do {
        const int count = std::min(perPacket, total - sent);
        std::vector<uint8_t> buffer(2 + static_cast<size_t>(count) * MasterEntry::Size + 1);
        buffer[0] = static_cast<uint8_t>(count);
        buffer[1] = static_cast<uint8_t>(total);
        for (int i = 0; i < count; i++) {
            m_entries[sent + i].wire.write(std::span<uint8_t>(buffer).subspan(2 + static_cast<size_t>(i) * MasterEntry::Size, MasterEntry::Size));
        }
        buffer.back() = m_pool ? 1 : 0; // MasterFlags.CanHost
        m_transport.send(to, PacketMasterList, buffer);
        sent += count;
    } while (sent < total);
}

} // namespace fp::net
