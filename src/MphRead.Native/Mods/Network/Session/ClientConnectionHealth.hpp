#pragma once

#include "Mods/Network/Protocol/Packets/HandshakePackets.hpp"
#include "Mods/Network/Protocol/Packets/SessionPackets.hpp"

#include <cstdint>

namespace MphRead::Mods::Network
{
    // Owns the measurements and state transitions for a client's server
    // connection. Rejoin cadence and network sends stay with NetSession.
    class ClientConnectionHealth final
    {
    public:
        struct Reannouncement final
        {
            bool Started = false;
            bool BecameLost = false;
            double SilentSeconds = 0.0;
        };

        [[nodiscard]] bool ConnectionLost() const noexcept { return _connectionLost; }
        [[nodiscard]] double LastServerPacket() const noexcept { return _lastServerPacket; }
        [[nodiscard]] std::int32_t Reannouncements() const noexcept { return _reannouncements; }
        [[nodiscard]] double LongestServerSilence() const noexcept { return _longestServerSilence; }
        [[nodiscard]] bool Reannounced() const noexcept { return _reannounced; }
        [[nodiscard]] bool Refused() const noexcept { return _refused; }
        [[nodiscard]] const RefusedPacket& RefusedReason() const noexcept { return _refusedReason; }

        void Begin(double now) noexcept;
        void RequestReannouncement() noexcept { _reannounced = true; }
        [[nodiscard]] bool RecordServerPacket(double now) noexcept;
        [[nodiscard]] bool TimedOut(double now, double timeoutSeconds) const noexcept;
        [[nodiscard]] bool ReannouncementDue(double now, double silenceBeforeRejoin) const noexcept;
        [[nodiscard]] Reannouncement StartReannouncement(
            double now, double silenceBeforeRejoin) noexcept;
        [[nodiscard]] bool Admit() noexcept;
        void Refuse(RefusedPacket reason) noexcept;
        void ClearRefusal() noexcept;
        void Reset() noexcept;

    private:
        bool _connectionLost = false;
        double _lastServerPacket = 0.0;
        std::int32_t _reannouncements = 0;
        double _longestServerSilence = 0.0;
        bool _reannounced = false;
        bool _refused = false;
        RefusedPacket _refusedReason{};
    };
}
