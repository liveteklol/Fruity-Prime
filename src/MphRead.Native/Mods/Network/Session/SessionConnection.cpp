#include "Mods/Network/Session/SessionConnection.hpp"

#include <cstring>
#include <stdexcept>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

namespace MphRead::Mods::Network
{
    std::array<std::uint8_t, 4> SessionConnection::ResolveIPv4(const std::string& address)
    {
        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_DGRAM;
        addrinfo* result = nullptr;
        const int error = getaddrinfo(address.c_str(), nullptr, &hints, &result);
        if (error != 0)
        {
#if defined(_WIN32)
            throw std::runtime_error(gai_strerrorA(error));
#else
            throw std::runtime_error(gai_strerror(error));
#endif
        }
        for (addrinfo* current = result; current != nullptr; current = current->ai_next)
        {
            if (current->ai_family != AF_INET
                || current->ai_addrlen < static_cast<decltype(current->ai_addrlen)>(sizeof(sockaddr_in)))
            {
                continue;
            }
            const auto* ipv4 = reinterpret_cast<const sockaddr_in*>(current->ai_addr);
            std::array<std::uint8_t, 4> bytes{};
            std::memcpy(bytes.data(), &ipv4->sin_addr.s_addr, bytes.size());
            freeaddrinfo(result);
            return bytes;
        }
        freeaddrinfo(result);
        throw std::runtime_error(address + " has no IPv4 address");
    }

    void SessionConnection::Open(std::int32_t port)
    {
        Close();
        _transport = std::make_unique<NetTransport>(port);
    }

    std::int32_t SessionConnection::Rebind()
    {
        if (_transport == nullptr)
        {
            return -1;
        }

        const std::int32_t previousPort = _transport->LocalPort();
        _transport->Dispose();
        _transport = std::make_unique<NetTransport>(0);
        return previousPort;
    }

    void SessionConnection::Close()
    {
        if (_transport != nullptr)
        {
            _transport->Dispose();
            _transport.reset();
        }
        _hostEndPoint.reset();
    }

    std::int32_t SessionConnection::LocalPort() const noexcept
    {
        return _transport != nullptr ? _transport->LocalPort() : -1;
    }

    bool SessionConnection::IsHost(
        const std::shared_ptr<System::Net::IPEndPoint>& sender) const
    {
        return _hostEndPoint != nullptr && sender != nullptr
            && sender->Equals(*_hostEndPoint);
    }

    void SessionConnection::SetHostEndpoint(
        std::shared_ptr<System::Net::IPEndPoint> endpoint) noexcept
    {
        _hostEndPoint = std::move(endpoint);
    }

    void SessionConnection::AnswerPingsImmediately() noexcept
    {
        if (_transport != nullptr)
        {
            _transport->AnswerPingsImmediately();
        }
    }

    NetTransport::DrainEnumerable SessionConnection::Drain()
    {
        return _transport->Drain();
    }

    void SessionConnection::EnqueueForPlayback(
        const std::shared_ptr<std::vector<std::uint8_t>>& data, std::int32_t length)
    {
        if (_transport != nullptr)
        {
            _transport->EnqueueForPlayback(data, length);
        }
    }

    void SessionConnection::Send(
        const std::shared_ptr<System::Net::IPEndPoint>& target,
        PacketType type, std::span<const std::uint8_t> payload)
    {
        if (_transport != nullptr)
        {
            _transport->Send(target, type, payload);
        }
    }

    void SessionConnection::SendToHost(PacketType type, std::span<const std::uint8_t> payload)
    {
        if (_transport != nullptr && _hostEndPoint != nullptr)
        {
            _transport->Send(_hostEndPoint, type, payload);
        }
    }

    void SessionConnection::SendPong(std::span<const std::uint8_t> payload)
    {
        SendToHost(PacketType::Pong, payload);
    }
}
