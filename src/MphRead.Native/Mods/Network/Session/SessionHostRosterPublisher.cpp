#include "Mods/Network/Session/SessionHostRosterPublisher.hpp"

#include "Mods/Network/Session/PeerRegistry.hpp"
#include "GameState.hpp"
#include "NativeRuntime/System/Managed.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

using ::MphRead::NativeRuntime::RequireReference;

namespace MphRead::Mods::Network
{
    namespace
    {
        [[nodiscard]] std::span<const std::uint8_t> First(
            const std::array<std::uint8_t, NetConfig::MaxPacketSize>& buffer, std::size_t count)
        {
            return std::span<const std::uint8_t>(buffer).first(count);
        }
    }

    void SessionHostRosterPublisher::Publish(SessionConnection& connection, PeerRegistry& peers,
        const SessionRuntimeState& runtime,
        const SessionMatchIdentity& matchIdentity, ClientReplicaStream& replicaStream,
        const SessionPlayerIdentity& playerIdentity)
    {
        if (!connection.IsOpen() || !matchIdentity.HasMatch())
        {
            return;
        }

        RosterPacket roster = RosterPacket::Create();
        const bool isHost = runtime.Role() == NetRole::Host;
        roster.MatchId = matchIdentity.MatchId(isHost);
        roster.AuthorityEpoch = matchIdentity.AuthorityEpoch(isHost);
        roster.Revision = replicaStream.NextRosterRevision();
        roster.Count = static_cast<std::uint8_t>(peers.Count() + 1);
        auto& slots = RequireReference(roster.Slots);
        auto& generations = RequireReference(roster.Generations);
        auto& names = RequireReference(roster.Names);
        slots[0] = 0;
        generations[0] = peers.HostGeneration(0);
        names[0] = playerIdentity.Name();
        for (std::size_t i = 0; i < peers.Count(); i++)
        {
            const std::int32_t slot = peers.At(i)->SlotIndex();
            slots[i + 1] = static_cast<std::uint8_t>(slot);
            generations[i + 1] = peers.HostGeneration(static_cast<std::size_t>(slot));
            names[i + 1] = GameState::Nicknames()[slot];
        }

        for (const std::shared_ptr<PeerRegistry::Peer>& peer : peers.Peers())
        {
            matchIdentity.State()->Write(connection.Buffer());
            connection.Send(peer->Endpoint(), PacketType::MatchState,
                First(connection.Buffer(), MatchStatePacket::Size));
            roster.Write(connection.Buffer());
            connection.Send(peer->Endpoint(), PacketType::Roster,
                First(connection.Buffer(), RosterPacket::Size));
        }
    }
}
