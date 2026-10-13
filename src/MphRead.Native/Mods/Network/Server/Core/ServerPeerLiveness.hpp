#pragma once

#include <cstdint>

namespace MphRead::Mods::Network
{
    // Owns one peer's keepalive, timeout and smoothed round-trip state.
    class ServerPeerLiveness final
    {
    public:
        static constexpr double PingRetrySeconds = 5.0;

        void MarkSeen(double now) noexcept;
        [[nodiscard]] bool TimedOut(double now, double timeoutSeconds) const noexcept;
        [[nodiscard]] bool CanSendPing(double now) const noexcept;
        [[nodiscard]] std::uint8_t BeginPing(double now) noexcept;
        [[nodiscard]] bool AcceptPong(std::uint8_t pingId, double now) noexcept;
        [[nodiscard]] std::int32_t PingMilliseconds() const noexcept;

    private:
        double _lastSeen = 0.0;
        double _pingSentAt = 0.0;
        std::uint8_t _pingId = 0;
        bool _pingPending = false;
        std::int32_t _pingMilliseconds = 0;
    };
}
