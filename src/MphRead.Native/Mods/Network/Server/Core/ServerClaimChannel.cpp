#include "Mods/Network/Server/Core/ServerClaimChannel.hpp"

#include "Mods/Network/Gameplay/Claims/Hit/NetHitClaims.hpp"
#include "Mods/Network/Gameplay/Claims/Pickup/NetPickupClaims.hpp"
#include "Mods/Network/Gameplay/Player/NetPlayerLifecycle.hpp"
#include "Mods/Network/Transport/NetTransport.hpp"

#include <algorithm>
#include <memory>

namespace MphRead::Mods::Network
{
    ServerClaimChannel::ServerClaimChannel(ServerPeerRegistry& peers, std::span<std::uint8_t> scratch,
        const ServerSessionIdentityPort& session) noexcept
        : _peers(peers), _scratch(scratch), _session(session)
    {
    }

    ServerClaimChannel::~ServerClaimChannel()
    {
        Stop();
    }

    void ServerClaimChannel::Start(NetTransport& transport)
    {
        Stop();
        _transport = &transport;
        _active = true;
        try
        {
            NetHitClaims::VerdictSink([this](std::int32_t slot,
                std::span<const std::pair<std::uint16_t, std::uint8_t>> verdicts)
            {
                SendHitVerdicts(slot, verdicts);
            });
            NetPickupClaims::VerdictSink([this](std::int32_t slot,
                std::span<const PickupVerdictPacket::Entry> verdicts)
            {
                SendPickupVerdicts(slot, verdicts);
            });
        }
        catch (...)
        {
            Stop();
            throw;
        }
    }

    void ServerClaimChannel::Stop() noexcept
    {
        if (!_active)
        {
            return;
        }
        NetHitClaims::VerdictSink(nullptr);
        NetPickupClaims::VerdictSink(nullptr);
        _transport = nullptr;
        _active = false;
    }

    void ServerClaimChannel::ReceiveHitClaim(
        const ReceivedPacket& packet, double now, bool inMatch, bool simulating)
    {
        if (!inMatch)
        {
            return;
        }
        const ServerPeerRegistry::PeerHandle peer = _peers.Find(packet.Sender);
        if (peer == nullptr || peer->SlotIndex < 0 || !simulating)
        {
            return;
        }
        peer->Liveness.MarkSeen(now);
        NetHitClaims::Receive(peer->SlotIndex, packet.Payload());
    }

    void ServerClaimChannel::ReceivePickupClaim(
        const ReceivedPacket& packet, double now, bool inMatch, bool simulating)
    {
        if (!inMatch)
        {
            return;
        }
        const ServerPeerRegistry::PeerHandle peer = _peers.Find(packet.Sender);
        if (peer == nullptr || peer->SlotIndex < 0 || !simulating)
        {
            return;
        }
        peer->Liveness.MarkSeen(now);
        NetPickupClaims::Receive(peer->SlotIndex, packet.Payload());
    }

    void ServerClaimChannel::SendPickupVerdicts(
        std::int32_t slot, std::span<const PickupVerdictPacket::Entry> verdicts)
    {
        if (verdicts.empty() || _transport == nullptr)
        {
            return;
        }
        for (const ServerPeerRegistry::PeerHandle& peer : _peers.All())
        {
            if (peer->SlotIndex != slot)
            {
                continue;
            }
            const std::span<const PickupVerdictPacket::Entry> sent = verdicts.first(
                std::min<std::size_t>(verdicts.size(), PickupVerdictPacket::MaxPerPacket));
            PickupVerdictPacket::Write(_scratch, sent, _session.ReadMatchId(), _session.ReadAuthorityEpoch(),
                NetPlayerLifecycle::Generation(slot), NetPlayerLifecycle::Get(slot));
            const std::size_t length = PickupVerdictPacket::HeaderSize + sent.size() * PickupVerdictPacket::EntrySize;
            _transport->Send(peer->EndPoint, PacketType::PickupVerdict,
                std::span<const std::uint8_t>(_scratch.data(), length));
            return;
        }
    }

    void ServerClaimChannel::SendHitVerdicts(
        std::int32_t slot, std::span<const std::pair<std::uint16_t, std::uint8_t>> verdicts)
    {
        if (verdicts.empty() || _transport == nullptr)
        {
            return;
        }
        for (const ServerPeerRegistry::PeerHandle& peer : _peers.All())
        {
            if (peer->SlotIndex != slot)
            {
                continue;
            }
            HitVerdictPacket::Write(_scratch, verdicts, _session.ReadMatchId(), _session.ReadAuthorityEpoch(),
                NetPlayerLifecycle::Generation(slot), NetPlayerLifecycle::Get(slot));
            const std::size_t length = HitVerdictPacket::HeaderSize + verdicts.size() * HitVerdictPacket::EntrySize;
            _transport->Send(peer->EndPoint, PacketType::HitVerdict,
                std::span<const std::uint8_t>(_scratch.data(), length));
            return;
        }
    }
}
