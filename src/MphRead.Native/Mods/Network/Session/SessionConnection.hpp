#pragma once

#include "Mods/Network/Protocol/NetProtocol.hpp"
#include "Mods/Network/Transport/NetTransport.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>

namespace MphRead::Mods::Network
{
    // Owns the socket, its peer endpoint, and the session's reusable datagram
    // buffer. Rebinding replaces only the socket so the authenticated server
    // endpoint remains stable across reconnect attempts.
    class SessionConnection final
    {
    public:
        SessionConnection() = default;
        SessionConnection(const SessionConnection&) = delete;
        SessionConnection(SessionConnection&&) = delete;
        SessionConnection& operator=(const SessionConnection&) = delete;
        SessionConnection& operator=(SessionConnection&&) = delete;

        [[nodiscard]] static std::array<std::uint8_t, 4> ResolveIPv4(const std::string& address);

        void Open(std::int32_t port);
        [[nodiscard]] std::int32_t Rebind();
        void Close();

        [[nodiscard]] bool IsOpen() const noexcept { return _transport != nullptr; }
        [[nodiscard]] bool HasHostEndpoint() const noexcept { return _hostEndPoint != nullptr; }
        [[nodiscard]] std::int32_t LocalPort() const noexcept;
        [[nodiscard]] bool IsHost(const std::shared_ptr<System::Net::IPEndPoint>& sender) const;

        void SetHostEndpoint(std::shared_ptr<System::Net::IPEndPoint> endpoint) noexcept;
        void AnswerPingsImmediately() noexcept;
        [[nodiscard]] NetTransport::DrainEnumerable Drain();
        void EnqueueForPlayback(
            const std::shared_ptr<std::vector<std::uint8_t>>& data, std::int32_t length);
        void Send(const std::shared_ptr<System::Net::IPEndPoint>& target,
            PacketType type, std::span<const std::uint8_t> payload);
        void SendToHost(PacketType type, std::span<const std::uint8_t> payload);
        void SendPong(std::span<const std::uint8_t> payload);

        [[nodiscard]] std::array<std::uint8_t, NetConfig::MaxPacketSize>& Buffer() noexcept
        {
            return _buffer;
        }

    private:
        std::unique_ptr<NetTransport> _transport{};
        std::shared_ptr<System::Net::IPEndPoint> _hostEndPoint{};
        std::array<std::uint8_t, NetConfig::MaxPacketSize> _buffer{};
    };
}
