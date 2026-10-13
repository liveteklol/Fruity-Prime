#pragma once

#include "Mods/Network/Protocol/Packets/ClaimPackets.hpp"
#include "Mods/Network/Protocol/Packets/PacketTypes.hpp"
#include "Mods/Network/Server/Core/ServerPeerRegistry.hpp"
#include "Mods/Network/Session/SessionPorts.hpp"

#include <cstdint>
#include <span>
#include <utility>

namespace MphRead::Mods::Network
{
    class NetTransport;
    struct ReceivedPacket;

    // Owns the dedicated server's claim packet boundary and the lifetime of
    // the authority verdict writers bound to its transport.
    class ServerClaimChannel final
    {
    public:
        ServerClaimChannel(ServerPeerRegistry& peers, std::span<std::uint8_t> scratch,
            const ServerSessionIdentityPort& session) noexcept;
        ServerClaimChannel(const ServerClaimChannel&) = delete;
        ServerClaimChannel(ServerClaimChannel&&) = delete;
        ServerClaimChannel& operator=(const ServerClaimChannel&) = delete;
        ServerClaimChannel& operator=(ServerClaimChannel&&) = delete;
        ~ServerClaimChannel();

        void Start(NetTransport& transport);
        void Stop() noexcept;

        void ReceiveHitClaim(const ReceivedPacket& packet, double now, bool inMatch, bool simulating);
        void ReceivePickupClaim(const ReceivedPacket& packet, double now, bool inMatch, bool simulating);

    private:
        void SendPickupVerdicts(std::int32_t slot, std::span<const PickupVerdictPacket::Entry> verdicts);
        void SendHitVerdicts(std::int32_t slot,
            std::span<const std::pair<std::uint16_t, std::uint8_t>> verdicts);

        ServerPeerRegistry& _peers;
        std::span<std::uint8_t> _scratch;
        const ServerSessionIdentityPort& _session;
        NetTransport* _transport = nullptr;
        bool _active = false;
    };
}
