#include "Mods/Network/Session/SessionSnapshotReceiver.hpp"

#include "Mods/Network/Diagnostics/NetTimingDiagnostics.hpp"
#include "Mods/Network/Gameplay/Player/NetPlayerLifecycle.hpp"
#include "Mods/Network/Gameplay/Replication/NetSmoothing.hpp"
#include "Mods/Network/Gameplay/Pickups/NetItemState.hpp"
#include "Mods/Network/Session/NetMatchTimeSync.hpp"
#include "Mods/Network/Session/RemotePlayerChannel.hpp"
#include "Mods/Network/Transport/NetTransport.hpp"
#include "Entities/Players/PlayerEntity.hpp"
#include "Utility/Rng.hpp"
#include "Mods/Network/Gameplay/Weapons/FiringVariant.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace MphRead::Mods::Network
{
    std::array<PlayerState, NetworkSlotCapacity> SessionSnapshotReceiver::_snapshotScratch{};

    void SessionSnapshotReceiver::Receive(ReceivedPacket packet, bool gameplayFrozen,
        const SessionRuntimeState& runtime, const SessionMatchIdentity& matchIdentity,
        ClientReplicaStream& replicaStream, SessionTrafficMetrics& traffic)
    {
        if (gameplayFrozen)
        {
            return;
        }

        const std::span<const std::uint8_t> payload = packet.Payload();
        if (payload.size() < SnapshotHeader::Size)
        {
            return;
        }
        const SnapshotHeader header = SnapshotHeader::Read(payload);
        const std::size_t timeOffset = SnapshotHeader::Size
            + static_cast<std::size_t>(header.PlayerCount) * PlayerState::Size;
        const std::size_t healthOffset = timeOffset + NetMatchTimeSync::Size;
        if (header.PlayerCount > Entities::PlayerEntity::SlotCapacity || healthOffset > payload.size()
            || (header.PrimeHunterSlot != 0xFF && header.PrimeHunterSlot >= NetworkSlotCapacity))
        {
            return;
        }

        const bool isHost = runtime.Role() == NetRole::Host;
        if (header.MatchId == 0 || header.MatchId != matchIdentity.MatchId(isHost))
        {
            NetPlayerLifecycle::RecordCrossMatch();
            return;
        }
        if (header.AuthorityEpoch == 0 || header.AuthorityEpoch != matchIdentity.AuthorityEpoch(isHost))
        {
            NetPlayerLifecycle::RecordCrossAuthority();
            return;
        }

        if (!NetMatchTimeSync::Validate(payload.subspan(timeOffset, NetMatchTimeSync::Size))
            || !NetItemState::Validate(payload.subspan(healthOffset))
            || !NetItemState::IsCurrentMatch(payload.subspan(healthOffset)))
        {
            return;
        }

        std::int32_t occupied = 0;
        for (std::int32_t i = 0; i < header.PlayerCount; i++)
        {
            const std::int32_t slot = payload[SnapshotHeader::Size
                + static_cast<std::size_t>(i) * PlayerState::Size];
            if (slot >= static_cast<std::int32_t>(RemotePlayerChannel::SlotCapacity)
                || (occupied & (1 << slot)) != 0)
            {
                return;
            }
            occupied |= 1 << slot;
        }
        if (!replicaStream.AcceptSnapshot(header.Frame, runtime.Frame()))
        {
            return;
        }

        traffic.RecordSnapshotReceived();
        FiringVariant::ReceiveRole(header.Frame, header.PrimeHunterSlot == 0xFF ? -1 : header.PrimeHunterSlot);
        Rng::SetRng1(header.Rng1);
        Rng::SetRng2(header.Rng2);

        std::size_t offset = SnapshotHeader::Size;
        std::size_t count = 0;
        RemotePlayers().InvalidateStates();
        for (std::int32_t i = 0; i < header.PlayerCount; ++i)
        {
            if (offset + PlayerState::Size > payload.size())
            {
                break;
            }
            const PlayerState state = PlayerState::Read(payload.subspan(offset));
            offset += PlayerState::Size;
            if (state.SlotIndex < RemotePlayerChannel::SlotCapacity
                && NetPlayerLifecycle::AcceptState(state, header.Frame))
            {
                RemotePlayers().StoreState(state);
                if (count < _snapshotScratch.size())
                {
                    _snapshotScratch[count++] = state;
                }
            }
        }
        NetTimingDiagnostics::Snapshot(packet.ArrivedAt);
        NetSmoothing::Record(header.Frame, std::span<const PlayerState>(_snapshotScratch.data(), count));
        NetMatchTimeSync::Receive(payload.subspan(timeOffset, NetMatchTimeSync::Size));
        NetItemState::Receive(payload.subspan(healthOffset));
    }
}
