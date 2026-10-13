#pragma once

#include "Mods/Network/Protocol/ConnectionSecret.hpp"
#include "Mods/Network/Server/Core/ServerPeerRegistry.hpp"
#include "Mods/Network/Server/Core/ServerSlotIdentity.hpp"

#include <cstdint>
#include <memory>

namespace System::Net
{
    class IPEndPoint;
}

namespace MphRead::Mods::Network
{
    // Owns connection identity resolution and registration of a human peer.
    // Match policy, authority effects and packet I/O remain with the server.
    class ServerHandshake final
    {
    public:
        using PeerHandle = ServerPeerRegistry::PeerHandle;
        using ReservedSlotCheck = bool(*)(std::int32_t);

        enum class ResolutionKind : std::uint8_t
        {
            NotFound,
            Existing,
            Rebound,
            EndpointConflict,
            InvalidSecret
        };

        struct Resolution final
        {
            ResolutionKind Kind = ResolutionKind::NotFound;
            PeerHandle Peer{};
            std::shared_ptr<System::Net::IPEndPoint> PreviousEndpoint{};
        };

        [[nodiscard]] static Resolution Resolve(
            ServerPeerRegistry& peers,
            const std::shared_ptr<System::Net::IPEndPoint>& endpoint,
            std::uint32_t clientId,
            const ConnectionSecret& secret);

        [[nodiscard]] static PeerHandle Register(
            ServerPeerRegistry& peers,
            ServerSlotIdentity& slotIdentity,
            const std::shared_ptr<System::Net::IPEndPoint>& endpoint,
            std::int32_t slot,
            std::int32_t capacity,
            ReservedSlotCheck isReserved,
            std::uint32_t clientId,
            const ConnectionSecret& secret,
            std::int8_t teamIndex);
    };
}
