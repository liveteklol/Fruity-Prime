#include "Mods/Network/Session/SessionRosterSynchronizer.hpp"

#include "GameState.hpp"
#include "Mods/Network/Gameplay/Player/NetPlayerLifecycle.hpp"
#include "Mods/Network/Gameplay/Player/PlayerColors.hpp"
#include "Mods/Network/Session/ClientLobbyState.hpp"
#include "Mods/Network/Session/ClientReplicaStream.hpp"
#include "Mods/Network/Session/NetLifecycleTracker.hpp"
#include "Mods/Network/Session/SessionRoster.hpp"
#include "Mods/Network/Session/SessionMatchIdentity.hpp"
#include "Mods/Network/Session/SessionMatchTransition.hpp"
#include "NativeRuntime/System/Managed.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace MphRead::Mods::Network
{
    namespace Runtime = ::MphRead::NativeRuntime;

    namespace
    {
        [[nodiscard]] bool HunterDefined(std::uint8_t value) noexcept
        {
            return value <= static_cast<std::uint8_t>(Hunter::Random);
        }

        template <typename T>
        [[nodiscard]] const std::vector<T>& RequireVector(const std::shared_ptr<std::vector<T>>& value)
        {
            if (value == nullptr)
            {
                throw System::NullReferenceException();
            }
            return *value;
        }
    }

    void SessionRosterSynchronizer::ApplyReceived(const RosterPacket& roster, bool host,
        SessionMatchIdentity& matchIdentity, ClientReplicaStream& replicaStream,
        ClientLobbyState& clientLobby)
    {
        if (!SessionMatchTransition::MatchesStream(roster.MatchId, roster.AuthorityEpoch,
                host, matchIdentity)
            || !replicaStream.AcceptRosterRevision(roster.Revision))
        {
            return;
        }
        ApplyAccepted(roster, replicaStream, clientLobby);
    }

    void SessionRosterSynchronizer::ApplyAccepted(const RosterPacket& roster,
        ClientReplicaStream& replicaStream, ClientLobbyState& clientLobby)
    {
        if (roster.Count > SessionRoster::SlotCapacity)
        {
            return;
        }
        const auto& slots = RequireVector(roster.Slots);
        const auto& generations = RequireVector(roster.Generations);
        std::int32_t occupied = 0;
        for (std::int32_t i = 0; i < roster.Count; i++)
        {
            const std::int32_t slot = Runtime::ManagedAt(slots, i);
            if (slot >= static_cast<std::int32_t>(SessionRoster::SlotCapacity) || (occupied & (1 << slot)) != 0)
            {
                return;
            }
            occupied |= 1 << slot;
            const std::uint16_t generation = Runtime::ManagedAt(generations, i);
            const std::uint16_t previous = NetPlayerLifecycle::Generation(slot);
            if (generation == 0 || (previous != 0 && generation != previous
                && !NetLifecycleTracker::Newer(generation, previous)))
            {
                NetPlayerLifecycle::RecordWrongGeneration();
                return;
            }
        }
        replicaStream.CommitRosterRevision(roster.Revision);
        clientLobby.SetRosterSessionRevision(roster.SessionRevision);
        SessionSlots().ClearOccupied();
        SessionSlots().ResetLobby();
        const auto& teams = RequireVector(roster.Teams);
        const auto& ready = RequireVector(roster.LobbyReady);
        const auto& names = RequireVector(roster.Names);
        const auto& hunters = RequireVector(roster.Hunters);
        const auto& colors = RequireVector(roster.Colors);
        const auto& pings = RequireVector(roster.Pings);
        for (std::int32_t i = 0; i < roster.Count; i++)
        {
            const std::int32_t slot = Runtime::ManagedAt(slots, i);
            if (slot < 0 || slot >= static_cast<std::int32_t>(SessionRoster::SlotCapacity))
            {
                continue;
            }
            if (Runtime::ManagedAt(generations, i) == 0)
            {
                continue;
            }
            const std::uint16_t previousGeneration = NetPlayerLifecycle::Generation(slot);
            if (previousGeneration != 0 && Runtime::ManagedAt(generations, i) != previousGeneration
                && !NetLifecycleTracker::Newer(Runtime::ManagedAt(generations, i), previousGeneration))
            {
                continue;
            }
            const auto index = static_cast<std::size_t>(slot);
            NetPlayerLifecycle::SetOccupant(slot, Runtime::ManagedAt(generations, i));
            SessionSlots().SetOccupied(slot, true);
            SessionSlots().SetTeam(index, Runtime::ManagedAt(teams, i));
            SessionSlots().SetLobbyReady(index, Runtime::ManagedAt(ready, i));
            GameState::Nicknames()[slot] = Runtime::ManagedAt(names, i).value_or(std::string());
            if (HunterDefined(Runtime::ManagedAt(hunters, i)))
            {
                SessionSlots().SetHunter(index, static_cast<Hunter>(Runtime::ManagedAt(hunters, i)));
            }
            PlayerColors::SetChoice(slot, PlayerColors::Clamp(Runtime::ManagedAt(colors, i)));
            SessionSlots().SetPing(slot, Runtime::ManagedAt(pings, i));
        }
        for (std::int32_t slot = 0; slot < static_cast<std::int32_t>(SessionRoster::SlotCapacity); slot++)
        {
            if (!SessionSlots().IsOccupied(slot))
            {
                NetPlayerLifecycle::SetOccupant(slot, 0);
            }
        }
    }
}
