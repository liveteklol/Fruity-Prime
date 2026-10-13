#include "Mods/Network/Session/SessionSnapshotPublisher.hpp"
#include "Mods/Network/Session/PeerRegistry.hpp"
#include "Mods/Network/Gameplay/Replication/PlayerStateSnapshotCapture.hpp"
#include "Mods/Network/Gameplay/Combat/NetUnlagged.hpp"
#include "Mods/Network/Gameplay/Pickups/NetItemState.hpp"
#include "Mods/Network/Replay/DemoRecorder.hpp"
#include "Mods/Network/Session/NetMatchTimeSync.hpp"
#include "Mods/Network/Session/NetRoomChange.hpp"
#include "Mods/Network/Gameplay/Player/NetPlayerLifecycle.hpp"
#include "Mods/Network/Diagnostics/NetLog.hpp"
#include "Entities/Players/PlayerEntity.hpp"
#include "NativeRuntime/System/Globalization.hpp"
#include "NativeRuntime/System/Managed.hpp"
#include "Utility/Rng.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>

using ::MphRead::NativeRuntime::HasFlag;
using ::MphRead::NativeRuntime::RequireReference;

namespace MphRead::Mods::Network
{
    namespace Runtime = ::MphRead::NativeRuntime;

    namespace
    {
        [[nodiscard]] std::span<std::uint8_t> Scratch(
            std::array<std::uint8_t, NetConfig::MaxPacketSize>& buffer, std::size_t start = 0)
        {
            return std::span<std::uint8_t>(buffer).subspan(start);
        }

        [[nodiscard]] std::span<const std::uint8_t> First(
            const std::array<std::uint8_t, NetConfig::MaxPacketSize>& buffer, std::size_t count)
        {
            return std::span<const std::uint8_t>(buffer).first(count);
        }
    }

    void SessionSnapshotPublisher::Publish(SessionConnection& connection, const SessionRuntimeState& runtime,
        const SessionAuthorityState& authority, const SessionMatchIdentity& matchIdentity,
        const SessionSimulationCallbacks& simulationCallbacks,
        SessionTrafficMetrics& traffic)
    {
        if (!NetRoomChange::GameplayReady())
        {
            return;
        }
        const bool asServer = runtime.Role() == NetRole::Server && simulationCallbacks.HasSnapshotSink();
        if (!connection.IsOpen() && !asServer)
        {
            return;
        }
        const bool asHost = runtime.Role() == NetRole::Host && !SessionPeers().Empty();
        const bool asAuthority = runtime.Role() == NetRole::Client && authority.IsAuthority() && connection.HasHostEndpoint();
        if (!asHost && !asAuthority && !asServer)
        {
            return;
        }

        std::int32_t count = 0;
        std::size_t offset = SnapshotHeader::Size;
        const auto& players = Entities::PlayerEntity::Players();
        for (std::size_t i = 0; i < players.size(); ++i)
        {
            Entities::PlayerEntity& player = RequireReference(players[i]);
            if (!HasFlag(player.LoadFlags(), Entities::LoadFlags::Active))
            {
                continue;
            }
            if (offset + PlayerState::Size > NetConfig::MaxPacketSize - 1)
            {
                break;
            }
            if (!std::isfinite(player.Position.X) || !std::isfinite(player.Position.Y)
                || !std::isfinite(player.Position.Z))
            {
                NetLog::Event("slot " + Runtime::ToString(static_cast<std::int32_t>(i))
                    + " not published: position is "
                    + static_cast<OpenTK::Mathematics::Vector3>(player.Position).ToString());
                continue;
            }
            const auto slot = static_cast<std::int32_t>(i);
            PlayerState state = PlayerStateSnapshotCapture::Capture(player, slot,
                NetPlayerLifecycle::Generation(slot), NetPlayerLifecycle::Get(slot));
            static_cast<void>(NetPlayerLifecycle::AcceptState(state, runtime.Frame()));
            state.Write(Scratch(connection.Buffer(), offset));
            offset += PlayerState::Size;
            ++count;
        }
        NetMatchTimeSync::Write(Scratch(connection.Buffer(), offset));
        offset += NetMatchTimeSync::Size;
        offset += static_cast<std::size_t>(NetItemState::Write(
            Scratch(connection.Buffer(), offset).first(NetConfig::MaxPacketSize - 1 - offset)));

        SnapshotHeader header{};
        header.MatchId = matchIdentity.MatchId(runtime.Role() == NetRole::Host);
        header.AuthorityEpoch = matchIdentity.AuthorityEpoch(runtime.Role() == NetRole::Host);
        header.Frame = runtime.Frame();
        header.Rng1 = Rng::Rng1();
        header.Rng2 = Rng::Rng2();
        header.PlayerCount = static_cast<std::uint8_t>(count);
        header.Write(connection.Buffer());
        traffic.RecordSnapshotSent();
        NetUnlagged::Record(header.Frame);
        DemoRecorder::RecordOwnSnapshot(First(connection.Buffer(), offset));
        if (asServer)
        {
            simulationCallbacks.PublishSnapshot(First(connection.Buffer(), offset));
            return;
        }

        if (asAuthority)
        {
            connection.SendToHost(PacketType::Snapshot, First(connection.Buffer(), offset));
            return;
        }
        for (const std::shared_ptr<PeerRegistry::Peer>& peer : SessionPeers().Peers())
        {
            connection.Send(peer->Endpoint(), PacketType::Snapshot, First(connection.Buffer(), offset));
        }
    }
}
