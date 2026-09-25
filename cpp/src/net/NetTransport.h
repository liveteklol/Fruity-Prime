#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace fp::net {

// An IPv4 address and port, in host order.
struct Endpoint {
    uint32_t address = 0;
    uint16_t port = 0;
    bool operator==(const Endpoint&) const = default;
    std::string toString() const;
    // A literal or a host name (the first IPv4 address it resolves to).
    static std::optional<Endpoint> resolve(const std::string& host, uint16_t port);
};

struct ReceivedPacket {
    Endpoint sender;
    std::vector<uint8_t> data; // type byte first
};

// NetTransport: a UDP socket read on a thread of its own, so a slow frame
// never makes a datagram wait for the socket and a network hiccup never
// stalls a frame. The game drains a bounded queue once per tick; the oldest
// packet goes when it fills, the newest being the one worth having. Pings are
// answered on the receive thread, so the round trip the server measures is
// the network's and not this machine's frame rate.
class Transport {
public:
    Transport();
    ~Transport();
    Transport(const Transport&) = delete;
    Transport& operator=(const Transport&) = delete;

    // Binds to `port` on every interface (0: any free port).
    bool open(uint16_t port = 0);
    void close();
    bool isOpen() const { return m_socket != invalidSocket; }
    uint16_t localPort() const { return m_localPort; }
    const std::string& error() const { return m_error; }

    void send(const Endpoint& to, uint8_t type, std::span<const uint8_t> payload);
    std::vector<ReceivedPacket> drain();
    uint64_t dropped() const { return m_dropped; }

private:
#ifdef _WIN32
    using Socket = uintptr_t;
    static constexpr Socket invalidSocket = ~static_cast<uintptr_t>(0);
#else
    using Socket = int;
    static constexpr Socket invalidSocket = -1;
#endif
    void receiveLoop();
    void sendRaw(const Endpoint& to, const uint8_t* data, size_t length);

    Socket m_socket = invalidSocket;
    uint16_t m_localPort = 0;
    std::string m_error;
    std::thread m_thread;
    std::atomic<bool> m_running{false};
    std::mutex m_lock;
    std::deque<ReceivedPacket> m_inbox;
    std::atomic<uint64_t> m_dropped{0};
};

} // namespace fp::net
