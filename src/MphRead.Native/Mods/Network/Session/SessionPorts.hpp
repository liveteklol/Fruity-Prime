#pragma once

#include "Mods/Network/Protocol/NetProtocol.hpp"
#include "Mods/Network/Protocol/SessionProtocol.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <span>

namespace MphRead::Mods::Network
{
    // Snapshot bytes flow from an authoritative simulation into its session.
    // Keeping the callback type in the session boundary avoids making the
    // simulation include the NetSession facade just to publish a snapshot.
    using SnapshotSink = std::function<void(std::span<const std::uint8_t>)>;

    // Lifecycle operations that an authoritative simulation needs from a
    // session. Packet projections remain optional for diagnostic simulations.
    struct ServerSimulationSessionPort final
    {
        std::function<void(SnapshotSink, std::function<void()>,
            const std::optional<RosterPacket>&, const std::optional<SessionStatePacket>&)> StartAuthority;
        std::function<void()> Stop;
    };

    [[nodiscard]] ServerSimulationSessionPort MakeNetSessionServerSimulationPort();

    struct ServerSessionClockPort final
    {
        std::uint32_t (*ReadFrame)() = nullptr;
        double (*ReadClock)() = nullptr;
    };

    struct ServerSessionIdentityPort final
    {
        std::uint16_t (*ReadMatchId)() = nullptr;
        std::uint64_t (*ReadAuthorityEpoch)() = nullptr;
    };

    // The dedicated server's complete boundary to the process-wide session.
    // Session state stays private to NetSession; the server receives only the
    // operations it needs for authority, replication, and protocol identity.
    struct DedicatedServerSessionPort final
    {
        ServerSimulationSessionPort Simulation{};
        ServerSessionClockPort Clock{};
        ServerSessionIdentityPort Identity{};
        void (*ApplySessionState)(SessionStatePacket) = nullptr;
        void (*ApplyMatchState)(MatchStatePacket, bool) = nullptr;
        void (*ApplyRoster)(RosterPacket) = nullptr;
        void (*AcceptSlotIntent)(std::int32_t, IntentPacket) = nullptr;
    };

    [[nodiscard]] DedicatedServerSessionPort MakeNetSessionDedicatedServerPort();
}
