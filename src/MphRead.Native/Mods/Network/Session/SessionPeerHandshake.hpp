#pragma once

#include <cstdint>

namespace MphRead::Mods::Network
{
    struct ReceivedPacket;
    class SessionConnection;
    class SessionMatchIdentity;

    // Admits/rebinds host peers and writes the Welcome response.
    class SessionPeerHandshake final
    {
    public:
        SessionPeerHandshake() = delete;

        using OccupantWriter = void (*)(std::int32_t slot, std::uint16_t generation);
        using OccupantGenerationReader = std::uint16_t (*)(std::int32_t slot);

        // Returns true after a peer is admitted and its Welcome is sent.
        // Slot/life ownership is updated through the narrow session boundary.
        [[nodiscard]] static bool HandleHello(ReceivedPacket packet, double time,
            SessionConnection& connection, const SessionMatchIdentity& matchIdentity,
            OccupantWriter setOccupant, OccupantGenerationReader occupantGeneration);
    };
}
