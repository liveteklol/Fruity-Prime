#include "Mods/Network/Session/SessionClaimChannel.hpp"

#include "Mods/Network/Gameplay/Claims/Hit/NetHitClaims.hpp"
#include "Mods/Network/Gameplay/Claims/Pickup/NetPickupClaims.hpp"
#include "Mods/Network/Gameplay/Player/NetPlayerLifecycle.hpp"
#include "Mods/Network/Session/PeerRegistry.hpp"
#include "Mods/Network/Session/SessionConnection.hpp"
#include "Mods/Network/Session/SessionMatchIdentity.hpp"
#include "Mods/Network/Transport/NetTransport.hpp"

#include <algorithm>

namespace MphRead::Mods::Network
{
    void SessionClaimChannel::ReceiveHitClaim(const ReceivedPacket& packet, const PeerRegistry& peers)
    {
        const std::int32_t slot = peers.FindSlotByEndpoint(packet.Sender);
        if (slot < 0)
        {
            return;
        }
        NetHitClaims::Receive(slot, packet.Payload());
    }

    void SessionClaimChannel::ReceivePickupClaim(const ReceivedPacket& packet, const PeerRegistry& peers)
    {
        const std::int32_t slot = peers.FindSlotByEndpoint(packet.Sender);
        if (slot < 0)
        {
            return;
        }
        NetPickupClaims::Receive(slot, packet.Payload());
    }

    void SessionClaimChannel::ApplyHitVerdicts(std::span<const std::uint8_t> payload)
    {
        NetHitClaims::ApplyVerdicts(payload);
    }

    void SessionClaimChannel::ApplyPickupVerdicts(std::span<const std::uint8_t> payload)
    {
        NetPickupClaims::ApplyVerdicts(payload);
    }

    void SessionClaimChannel::SendClientClaims(SessionConnection& connection)
    {
        // Preserve the wire ordering: an item contact that happened before a
        // shot in the same frame must be visible before the hit claim.
        const std::int32_t pickups = NetPickupClaims::Compose(connection.Buffer());
        if (pickups > 0)
        {
            connection.SendToHost(PacketType::PickupClaim,
                std::span<const std::uint8_t>(connection.Buffer().data(), static_cast<std::size_t>(pickups)));
        }
        const std::int32_t hits = NetHitClaims::Compose(connection.Buffer());
        if (hits > 0)
        {
            connection.SendToHost(PacketType::HitClaim,
                std::span<const std::uint8_t>(connection.Buffer().data(), static_cast<std::size_t>(hits)));
        }
    }

    void SessionClaimChannel::Start(SessionConnection& connection, const PeerRegistry& peers,
        const SessionMatchIdentity& matchIdentity)
    {
        Stop();
        _connection = &connection;
        _peers = &peers;
        _matchIdentity = &matchIdentity;
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

    void SessionClaimChannel::Stop() noexcept
    {
        if (!_active)
        {
            return;
        }
        NetHitClaims::VerdictSink(nullptr);
        NetPickupClaims::VerdictSink(nullptr);
        _connection = nullptr;
        _peers = nullptr;
        _matchIdentity = nullptr;
        _active = false;
    }

    void SessionClaimChannel::SendHitVerdicts(std::int32_t slot,
        std::span<const std::pair<std::uint16_t, std::uint8_t>> verdicts)
    {
        if (verdicts.empty() || _connection == nullptr || !_connection->IsOpen()
            || _peers == nullptr || _matchIdentity == nullptr)
        {
            return;
        }
        const std::shared_ptr<System::Net::IPEndPoint> endpoint = _peers->FindEndpointBySlot(slot);
        if (endpoint == nullptr)
        {
            return;
        }
        HitVerdictPacket::Write(_connection->Buffer(), verdicts, _matchIdentity->MatchId(true),
            _matchIdentity->AuthorityEpoch(true), NetPlayerLifecycle::Generation(slot),
            NetPlayerLifecycle::Get(slot));
        const std::size_t length = HitVerdictPacket::HeaderSize + verdicts.size() * HitVerdictPacket::EntrySize;
        _connection->Send(endpoint, PacketType::HitVerdict,
            std::span<const std::uint8_t>(_connection->Buffer().data(), length));
    }

    void SessionClaimChannel::SendPickupVerdicts(std::int32_t slot,
        std::span<const PickupVerdictPacket::Entry> verdicts)
    {
        if (verdicts.empty() || _connection == nullptr || !_connection->IsOpen()
            || _peers == nullptr || _matchIdentity == nullptr)
        {
            return;
        }
        const std::shared_ptr<System::Net::IPEndPoint> endpoint = _peers->FindEndpointBySlot(slot);
        if (endpoint == nullptr)
        {
            return;
        }
        const std::span<const PickupVerdictPacket::Entry> sent = verdicts.first(
            std::min<std::size_t>(verdicts.size(), PickupVerdictPacket::MaxPerPacket));
        PickupVerdictPacket::Write(_connection->Buffer(), sent, _matchIdentity->MatchId(true),
            _matchIdentity->AuthorityEpoch(true), NetPlayerLifecycle::Generation(slot),
            NetPlayerLifecycle::Get(slot));
        const std::size_t length = PickupVerdictPacket::HeaderSize + sent.size() * PickupVerdictPacket::EntrySize;
        _connection->Send(endpoint, PacketType::PickupVerdict,
            std::span<const std::uint8_t>(_connection->Buffer().data(), length));
    }
}
