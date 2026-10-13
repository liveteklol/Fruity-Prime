#pragma once

#include "Mods/Network/Protocol/Packets/ClaimPackets.hpp"

#include <span>
#include <cstdint>
#include <utility>

namespace MphRead::Mods::Network
{
    class PeerRegistry;
    class SessionConnection;
    class SessionMatchIdentity;
    struct ReceivedPacket;

    // Bridges claim packets between the session peer registry and gameplay claim owners.
    class SessionClaimChannel final
    {
    public:
        SessionClaimChannel() = default;
        SessionClaimChannel(const SessionClaimChannel&) = delete;
        SessionClaimChannel(SessionClaimChannel&&) = delete;
        SessionClaimChannel& operator=(const SessionClaimChannel&) = delete;
        SessionClaimChannel& operator=(SessionClaimChannel&&) = delete;

        static void ReceiveHitClaim(const ReceivedPacket& packet, const PeerRegistry& peers);
        static void ReceivePickupClaim(const ReceivedPacket& packet, const PeerRegistry& peers);
        static void ApplyHitVerdicts(std::span<const std::uint8_t> payload);
        static void ApplyPickupVerdicts(std::span<const std::uint8_t> payload);
        static void SendClientClaims(SessionConnection& connection);

        void Start(SessionConnection& connection, const PeerRegistry& peers,
            const SessionMatchIdentity& matchIdentity);
        void Stop() noexcept;

    private:
        void SendHitVerdicts(std::int32_t slot,
            std::span<const std::pair<std::uint16_t, std::uint8_t>> verdicts);
        void SendPickupVerdicts(std::int32_t slot,
            std::span<const PickupVerdictPacket::Entry> verdicts);

        SessionConnection* _connection = nullptr;
        const PeerRegistry* _peers = nullptr;
        const SessionMatchIdentity* _matchIdentity = nullptr;
        bool _active = false;
    };
}
