#include "Mods/Network/Server/Core/ServerHandshake.hpp"

#include "Mods/Network/Protocol/ConnectionSecret.hpp"

#include <cstddef>
#include <memory>
#include <utility>

namespace MphRead::Mods::Network
{
    ServerHandshake::Resolution ServerHandshake::Resolve(
        ServerPeerRegistry& peers,
        const std::shared_ptr<System::Net::IPEndPoint>& endpoint,
        std::uint32_t clientId,
        const ConnectionSecret& secret)
    {
        PeerHandle peer = peers.Find(endpoint);
        if (peer != nullptr)
        {
            return Resolution{
                peer->ClientId == clientId ? ResolutionKind::Existing : ResolutionKind::EndpointConflict,
                std::move(peer),
                {}};
        }

        if (clientId == 0)
        {
            return {};
        }

        peer = peers.FindByClientId(clientId);
        if (peer == nullptr)
        {
            return {};
        }
        if (!MayRebind(peer->Secret, secret))
        {
            return Resolution{ResolutionKind::InvalidSecret, std::move(peer), {}};
        }

        std::shared_ptr<System::Net::IPEndPoint> previousEndpoint = peer->EndPoint;
        peer->EndPoint = endpoint;
        return Resolution{ResolutionKind::Rebound, std::move(peer), std::move(previousEndpoint)};
    }

    ServerHandshake::PeerHandle ServerHandshake::Register(
        ServerPeerRegistry& peers,
        ServerSlotIdentity& slotIdentity,
        const std::shared_ptr<System::Net::IPEndPoint>& endpoint,
        std::int32_t slot,
        std::int32_t capacity,
        ReservedSlotCheck isReserved,
        std::uint32_t clientId,
        const ConnectionSecret& secret,
        std::int8_t teamIndex)
    {
        if (slot < 0 || slot >= capacity || !slotIdentity.IsValidSlot(static_cast<std::size_t>(slot))
            || isReserved == nullptr || isReserved(slot)
            || !peers.SlotFree(slot, [](std::int32_t) { return false; }))
        {
            return nullptr;
        }

        const auto slotIndex = static_cast<std::size_t>(slot);
        (void)slotIdentity.AdvanceGeneration(slotIndex);
        auto peer = std::make_shared<ServerPeerRegistry::Peer>();
        peer->EndPoint = endpoint;
        peer->SlotIndex = slot;
        peer->ClientId = clientId;
        peer->Secret = secret;
        peer->Lobby.SetTeamIndex(teamIndex);
        peers.Add(peer);
        return peer;
    }
}
