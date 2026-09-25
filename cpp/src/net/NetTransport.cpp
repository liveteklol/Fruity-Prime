#include "NetTransport.h"

#include "NetProtocol.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <cerrno>
#endif

#include <cstring>

namespace fp::net {

namespace {

// How many received packets may wait for the game loop (NetTransport.MaxQueuedPackets).
constexpr size_t MaxQueuedPackets = 2048;
constexpr int SocketBufferBytes = 1 << 20;

#ifdef _WIN32
struct WinsockInit {
    bool ok = false;
    WinsockInit()
    {
        WSADATA data;
        ok = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }
    ~WinsockInit()
    {
        if (ok) {
            WSACleanup();
        }
    }
};
bool ensureWinsock()
{
    static WinsockInit init;
    return init.ok;
}
#else
bool ensureWinsock() { return true; }
#endif

sockaddr_in toSockaddr(const Endpoint& e)
{
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(e.address);
    addr.sin_port = htons(e.port);
    return addr;
}

} // namespace

std::string Endpoint::toString() const
{
    return std::to_string(address >> 24) + "." + std::to_string((address >> 16) & 0xFF) + "." + std::to_string((address >> 8) & 0xFF)
        + "." + std::to_string(address & 0xFF) + ":" + std::to_string(port);
}

std::optional<Endpoint> Endpoint::resolve(const std::string& host, uint16_t port)
{
    if (!ensureWinsock()) {
        return std::nullopt;
    }
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* result = nullptr;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &result) != 0 || result == nullptr) {
        return std::nullopt;
    }
    Endpoint e;
    e.address = ntohl(reinterpret_cast<const sockaddr_in*>(result->ai_addr)->sin_addr.s_addr);
    e.port = port;
    freeaddrinfo(result);
    return e;
}

Transport::Transport() = default;

Transport::~Transport() { close(); }

bool Transport::open(uint16_t port)
{
    close();
    if (!ensureWinsock()) {
        m_error = "Winsock could not start";
        return false;
    }
    m_socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (m_socket == invalidSocket) {
        m_error = "cannot create a UDP socket";
        return false;
    }
#ifdef _WIN32
    // SIO_UDP_CONNRESET off: a peer that vanished must not fail the next receive.
    DWORD off = 0, bytes = 0;
    WSAIoctl(m_socket, 0x9800000C, &off, sizeof(off), nullptr, 0, &bytes, nullptr, nullptr);
    DWORD timeout = 500;
    setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
#else
    timeval timeout{0, 500000};
    setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
#endif
    // Only so the thread notices shutdown; nothing waits on the timeout otherwise.
    const int buffer = SocketBufferBytes;
    setsockopt(m_socket, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&buffer), sizeof(buffer));
    setsockopt(m_socket, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&buffer), sizeof(buffer));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (::bind(m_socket, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0) {
        m_error = "cannot bind UDP port " + std::to_string(port);
        close();
        return false;
    }
    socklen_t length = sizeof(addr);
    getsockname(m_socket, reinterpret_cast<sockaddr*>(&addr), &length);
    m_localPort = ntohs(addr.sin_port);
    m_running = true;
    m_thread = std::thread([this] { receiveLoop(); });
    return true;
}

void Transport::close()
{
    m_running = false;
    if (m_socket != invalidSocket) {
#ifdef _WIN32
        closesocket(m_socket);
#else
        ::shutdown(m_socket, SHUT_RDWR);
        ::close(m_socket);
#endif
        m_socket = invalidSocket;
    }
    if (m_thread.joinable()) {
        m_thread.join();
    }
    std::lock_guard lock(m_lock);
    m_inbox.clear();
}

void Transport::receiveLoop()
{
    std::vector<uint8_t> buffer(MaxPacketSize + 1);
    while (m_running) {
        sockaddr_in from{};
        socklen_t fromLength = sizeof(from);
        const auto received = ::recvfrom(m_socket, reinterpret_cast<char*>(buffer.data()), static_cast<int>(buffer.size()), 0,
            reinterpret_cast<sockaddr*>(&from), &fromLength);
        if (received <= 0 || received > MaxPacketSize) {
            // A timeout, an ICMP unreachable from a peer that left, or shutdown.
            continue;
        }
        Endpoint sender{ntohl(from.sin_addr.s_addr), ntohs(from.sin_port)};
        if (buffer[0] == static_cast<uint8_t>(PacketType::Ping)) {
            // The id echoed back so the server can match the reply to its ping.
            std::vector<uint8_t> pong(buffer.begin(), buffer.begin() + received);
            pong[0] = static_cast<uint8_t>(PacketType::Pong);
            sendRaw(sender, pong.data(), pong.size());
            continue;
        }
        std::lock_guard lock(m_lock);
        if (m_inbox.size() >= MaxQueuedPackets) {
            m_inbox.pop_front();
            m_dropped++;
        }
        m_inbox.push_back({sender, std::vector<uint8_t>(buffer.begin(), buffer.begin() + received)});
    }
}

std::vector<ReceivedPacket> Transport::drain()
{
    std::lock_guard lock(m_lock);
    std::vector<ReceivedPacket> packets(std::make_move_iterator(m_inbox.begin()), std::make_move_iterator(m_inbox.end()));
    m_inbox.clear();
    return packets;
}

void Transport::send(const Endpoint& to, uint8_t type, std::span<const uint8_t> payload)
{
    uint8_t datagram[MaxPacketSize];
    if (payload.size() + 1 > sizeof(datagram)) {
        return;
    }
    datagram[0] = type;
    std::memcpy(datagram + 1, payload.data(), payload.size());
    sendRaw(to, datagram, payload.size() + 1);
}

void Transport::sendRaw(const Endpoint& to, const uint8_t* data, size_t length)
{
    if (m_socket == invalidSocket || data == nullptr || length == 0) {
        return;
    }
    const sockaddr_in addr = toSockaddr(to);
    ::sendto(m_socket, reinterpret_cast<const char*>(data), static_cast<int>(length), 0, reinterpret_cast<const sockaddr*>(&addr),
        sizeof(addr));
}

} // namespace fp::net
