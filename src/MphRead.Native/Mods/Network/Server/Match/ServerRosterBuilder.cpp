#include "Mods/Network/Server/Match/ServerRosterBuilder.hpp"

#include "Mods/Network/Bots/ServerBots.hpp"
#include "NativeRuntime/System/Managed.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>

namespace MphRead::Mods::Network
{
    RosterPacket ServerRosterBuilder::Build(std::uint16_t matchId, std::uint64_t authorityEpoch,
        std::uint16_t sessionRevision, std::int32_t maxPlayers,
        const ServerPeerRegistry& peers, const ServerSlotIdentity& slots)
    {
        RosterPacket roster = RosterPacket::Create();
        roster.MatchId = matchId;
        roster.AuthorityEpoch = authorityEpoch;
        roster.Revision = ++_revision;
        roster.SessionRevision = sessionRevision;
        auto& slotIndices = ::MphRead::NativeRuntime::RequireReference(roster.Slots);
        auto& generations = ::MphRead::NativeRuntime::RequireReference(roster.Generations);
        auto& teams = ::MphRead::NativeRuntime::RequireReference(roster.Teams);
        auto& ready = ::MphRead::NativeRuntime::RequireReference(roster.LobbyReady);
        auto& hunters = ::MphRead::NativeRuntime::RequireReference(roster.Hunters);
        auto& colors = ::MphRead::NativeRuntime::RequireReference(roster.Colors);
        auto& pings = ::MphRead::NativeRuntime::RequireReference(roster.Pings);
        auto& names = ::MphRead::NativeRuntime::RequireReference(roster.Names);
        const auto& connected = peers.All();
        for (std::size_t i = 0; i < connected.size() && i < static_cast<std::size_t>(RosterPacket::MaxSlots); i++)
        {
            const ServerPeerRegistry::Peer& peer = *connected[i];
            const auto at = static_cast<std::size_t>(roster.Count);
            slotIndices[at] = static_cast<std::uint8_t>(peer.SlotIndex);
            generations[at] = slots.Generation(static_cast<std::size_t>(peer.SlotIndex));
            teams[at] = peer.Lobby.TeamIndex();
            ready[at] = peer.Lobby.LobbyReady();
            hunters[at] = peer.Profile.Hunter();
            colors[at] = peer.Profile.Color();
            pings[at] = static_cast<std::uint16_t>(std::clamp(peer.Liveness.PingMilliseconds(), 0, 9999));
            names[at] = !peer.Profile.Name().empty() ? peer.Profile.Name() : "Player" + std::to_string(peer.SlotIndex + 1);
            roster.Count++;
        }
        for (std::int32_t slot = 0; slot < maxPlayers && roster.Count < RosterPacket::MaxSlots; slot++)
        {
            if (!ServerBots::Holds(slot))
            {
                continue;
            }
            const auto at = static_cast<std::size_t>(roster.Count);
            slotIndices[at] = static_cast<std::uint8_t>(slot);
            generations[at] = slots.Generation(static_cast<std::size_t>(slot));
            teams[at] = -1;
            ready[at] = true;
            hunters[at] = static_cast<std::uint8_t>(ServerBots::HunterFor(slot));
            colors[at] = 0;
            pings[at] = 0;
            names[at] = ServerBots::NameFor(slot);
            roster.Count++;
        }
        return roster;
    }
}
