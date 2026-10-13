#include "Mods/Network/Session/NetSession.hpp"
#include "Mods/Network/Session/SessionLifecycle.hpp"
#include "Mods/Network/Session/SessionConnection.hpp"
#include "Mods/Network/Session/SessionPacketDispatcher.hpp"
#include "Mods/Network/Session/SessionAdmissionChannel.hpp"
#include "Mods/Network/Session/SessionChatChannel.hpp"
#include "Mods/Network/Session/SessionVoteChannel.hpp"
#include "Mods/Network/Session/SessionMapUpload.hpp"
#include "Mods/Network/Session/SessionSnapshotPublisher.hpp"
#include "Mods/Network/Session/SessionSnapshotReceiver.hpp"
#include "Mods/Network/Session/SessionIntentPublisher.hpp"
#include "Mods/Network/Session/SessionHostRosterPublisher.hpp"
#include "Mods/Network/Session/SessionMatchTransition.hpp"
#include "Mods/Network/Session/SessionRuntimePump.hpp"
#include "Mods/Network/Session/SessionPlaybackReset.hpp"
#include "Mods/Network/Session/SessionClientHandshake.hpp"
#include "Mods/Network/Session/SessionMatchEndChannel.hpp"
#include "Mods/Network/Session/SessionPeerMaintenance.hpp"
#include "Mods/Network/Session/SessionSlotCleanup.hpp"
#include "Mods/Network/Replay/DemoPlayback.hpp"
#include "Mods/Network/Replay/DemoRecorder.hpp"
#include "Entities/Players/PlayerEntity.hpp"
#include "GameState.hpp"
#include "Mods/Network/Session/SessionPeerHandshake.hpp"
#include "Mods/Network/Session/RemoteIntentChannel.hpp"
#include "Mods/Network/Session/SessionRosterSynchronizer.hpp"
#include "Mods/Network/Session/PeerRegistry.hpp"
#include "Mods/Network/Gameplay/Weapons/ContinuousWeaponPhase.hpp"

#include "Mods/Network/Session/NetMatchTimeSync.hpp"
#include "Mods/Network/Gameplay/Player/NetPlayerLifecycle.hpp"
#include "Formats/Types.hpp"

#include <cstddef>
#include <cstdint>
#include <utility>

namespace
{
    namespace Runtime = ::MphRead::NativeRuntime;
}

namespace MphRead::Mods::Network
{
    static_assert(PeerRegistry::SlotCapacity == Entities::PlayerEntity::SlotCapacity);
    static_assert(PeerRegistry::SlotCapacity == RemotePlayerChannel::SlotCapacity);
    static_assert(PeerRegistry::SlotCapacity == SessionRoster::SlotCapacity);
    static_assert(PeerRegistry::SlotCapacity == ClientReplicaStream::SlotCapacity);

    // Eight player states, the header and the objective clocks in one
    // datagram (pickup state takes what is left): Weavel's turret state and
    // the confirmed-impact bytes both live in a player state.
    static_assert(SnapshotHeader::Size + 8 * PlayerState::Size + NetMatchTimeSync::Size
        <= NetConfig::MaxPacketSize - 1, "a full snapshot no longer fits one datagram");

    SessionContext NetSession::_context{};
    const std::uint32_t NetSession::ClientId = SessionClientHandshake::NewClientId();
    const ConnectionSecret NetSession::Secret = SessionClientHandshake::NewSecret();

    SessionLifecycle::Ports NetSession::LifecyclePorts()
    {
        return SessionLifecycle::Ports{
            _context._runtime,
            _context._connection,
            _context._simulationCallbacks,
            _context._authorityState,
            _context._clientLobby,
            _context._connectionHealth,
            _context._matchIdentity,
            _context._playerIdentity,
            _context._replicaStream,
            _context._traffic,
            &SessionConnection::ResolveIPv4,
            &Clock,
            &SendHello,
            &SendIdentify,
            &ResetLobbySession
        };
    }

    std::uint32_t NetSession::SnapshotAge() noexcept
    {
        return _context._replicaStream.SnapshotAge(_context._runtime.Frame());
    }

    std::uint32_t NetSession::RemoteIntentAge(std::int32_t slot) noexcept
    {
        return RemotePlayers().IntentAge(slot, _context._runtime.Frame());
    }

    bool NetSession::HasRemoteState(std::int32_t slot) noexcept
    {
        return RemotePlayers().HasState(slot);
    }

    const PlayerState& NetSession::RemoteState(std::size_t slot) noexcept
    {
        return RemotePlayers().StateAt(slot);
    }

    bool NetSession::HasRemoteIntent(std::int32_t slot) noexcept
    {
        return RemotePlayers().HasIntent(slot);
    }

    const IntentPacket& NetSession::RemoteIntent(std::size_t slot) noexcept
    {
        return RemotePlayers().IntentAt(slot);
    }

    void NetSession::NoteStatesApplied() noexcept
    {
        _context._traffic.RecordStateApplied();
        _context._replicaStream.NoteStatesApplied();
    }

    void NetSession::StartServerAuthority(
        SnapshotSink sink, std::function<void()> matchEnded)
    {
        SessionLifecycle::Ports ports = LifecyclePorts();
        SessionLifecycle::StartServerAuthority(ports, std::move(sink), std::move(matchEnded));
    }

    void NetSession::StartHost(std::int32_t port)
    {
        SessionLifecycle::Ports ports = LifecyclePorts();
        SessionLifecycle::StartHost(ports, port);
    }

    void NetSession::StartClient(const std::string& address, std::int32_t port, Runtime::Guid ownerToken)
    {
        SessionLifecycle::Ports ports = LifecyclePorts();
        SessionLifecycle::StartClient(ports, address, port, ownerToken);
    }

    void NetSession::StartPlayback()
    {
        SessionLifecycle::Ports ports = LifecyclePorts();
        SessionLifecycle::StartPlayback(ports);
    }

    void NetSession::RewindPlayback()
    {
        SessionPlaybackReset::Apply(_context._runtime, _context._replicaStream, RemotePlayers(), _context._traffic);
    }

    void NetSession::InjectPlaybackPacket(
        const std::shared_ptr<std::vector<std::uint8_t>>& data, std::int32_t length)
    {
        if (_context._connection.IsOpen())
        {
            _context._connection.EnqueueForPlayback(data, length);
        }
    }

    void NetSession::Stop()
    {
        SessionLifecycle::Ports ports = LifecyclePorts();
        SessionLifecycle::Stop(ports);
    }

    void NetSession::SendIdentify()
    {
        SessionClientHandshake::SendIdentify(_context._connection, _context._playerIdentity);
    }

    void NetSession::SendHello()
    {
        SessionClientHandshake::SendHello(_context._connection, _context._runtime, _context._clientLobby, ClientId, Secret);
    }

    void NetSession::RebindSocket()
    {
        SessionLifecycle::Ports ports = LifecyclePorts();
        SessionLifecycle::RebindSocket(ports);
    }

    void NetSession::Update(double time)
    {
        SessionRuntimePump::Ports ports{
            _context._runtime,
            _context._connection,
            _context._authorityState,
            _context._connectionHealth,
            DemoPlayback::IsActive(),
            SilenceBeforeRejoin,
            &DemoRecorder::Record,
            &Handle,
            &PumpLobby,
            &DropTimedOutPeers,
            &BroadcastHostControl,
            &SendHello,
            &SendIdentify,
            &IdentityNeedsRefresh,
            &Clock
        };
        SessionRuntimePump::Update(ports, time);
    }

    void NetSession::PumpMapTransfer()
    {
        SessionRuntimePump::MapTransferPorts ports{
            _context._connection,
            &Handle,
            &Clock
        };
        SessionRuntimePump::PumpMapTransfer(ports);
    }

    void NetSession::Handle(ReceivedPacket packet, double time)
    {
        SessionPacketDispatcher::Dispatch(std::move(packet), time);
    }

    bool NetSession::IsPlayback() noexcept
    {
        return _context._runtime.IsPlayback();
    }

    bool NetSession::IsHostEndpoint(
        const std::shared_ptr<System::Net::IPEndPoint>& endpoint)
    {
        return _context._connection.IsHost(endpoint);
    }

    bool NetSession::RecordServerPacket(double time) noexcept
    {
        return _context._connectionHealth.RecordServerPacket(time);
    }

    void NetSession::HandlePing(std::span<const std::uint8_t> payload)
    {
        _context._connection.SendPong(payload);
    }

    void NetSession::HandleWelcome(std::span<const std::uint8_t> payload)
    {
        SessionAdmissionChannel::HandleWelcome(payload, ClientId, _context._runtime, _context._matchIdentity,
            _context._connectionHealth, _context._authorityState, &NetSession::MatchesStream);
    }

    void NetSession::HandleAuthority(std::span<const std::uint8_t> payload)
    {
        SessionAdmissionChannel::HandleAuthority(payload, _context._runtime, _context._authorityState,
            &NetSession::MatchesStream);
    }

    void NetSession::HandleRefused(std::span<const std::uint8_t> payload)
    {
        SessionAdmissionChannel::HandleRefused(payload, _context._runtime.LocalSlot(), _context._connectionHealth);
    }

    void NetSession::HandleChat(ReceivedPacket packet, double time)
    {
        SessionChatChannel::Receive(std::move(packet), time, _context._connection, _context._runtime);
    }

    void NetSession::SendChat(const std::string& value)
    {
        SessionChatChannel::Send(value, _context._connection, _context._runtime.Role(), _context._runtime.LocalSlot(),
            _context._playerIdentity.Name(), &NetSession::ActiveMatchDefinition);
    }

    void NetSession::SendVote(std::uint8_t kind, const std::string& roomKey)
    {
        SessionVoteChannel::SendVote(_context._connection, _context._runtime.Role(), kind, roomKey);
    }

    void NetSession::AnswerMapWant(std::span<const std::uint8_t> payload)
    {
        SessionMapUpload::Answer(_context._connection, _context._runtime.Role(), payload);
    }

    void NetSession::SendMapPick(const std::string& roomKey)
    {
        SessionVoteChannel::SendMapPick(_context._connection, _context._runtime.Role(), roomKey);
    }

    void NetSession::BroadcastHostControl()
    {
        SessionHostRosterPublisher::Publish(_context._connection, SessionPeers(),
            _context._runtime, _context._matchIdentity, _context._replicaStream, _context._playerIdentity);
    }

    void NetSession::HandleHello(ReceivedPacket packet, double time)
    {
        constexpr SessionPeerHandshake::OccupantWriter setOccupant =
            +[](std::int32_t slot, std::uint16_t generation)
            {
                NetPlayerLifecycle::SetOccupant(slot, generation);
            };
        constexpr SessionPeerHandshake::OccupantGenerationReader occupantGeneration =
            +[](std::int32_t slot)
            {
                return NetPlayerLifecycle::Generation(slot);
            };
        if (SessionPeerHandshake::HandleHello(packet, time, _context._connection, _context._matchIdentity,
            setOccupant, occupantGeneration))
        {
            BroadcastHostControl();
        }
    }

    void NetSession::HandleIntent(const ReceivedPacket& packet, double time)
    {
        RemoteIntentChannel::ReceiveHostIntent(packet, time, _context._runtime.Frame());
    }

    void NetSession::HandleSlotIntent(const ReceivedPacket& packet)
    {
        RemoteIntentChannel::ReceiveAuthorityIntent(packet, _context._runtime.LocalSlot(), _context._runtime.Frame(),
            _context._replicaStream, _context._traffic);
    }

    void NetSession::AcceptSlotIntent(std::int32_t slot, IntentPacket intent)
    {
        RemoteIntentChannel::AcceptAuthorityIntent(slot, std::move(intent), _context._runtime.LocalSlot(), _context._runtime.Frame(),
            _context._replicaStream, _context._traffic);
    }

    void NetSession::ForgetSlot(std::int32_t slot)
    {
        SessionSlotCleanup::Forget(slot, SessionPeers(), _context._replicaStream, RemotePlayers());
    }

    bool NetSession::ConsumeAuthorityStateSync() noexcept
    {
        return _context._authorityState.ConsumeStateSync();
    }

    std::int32_t NetSession::ServerPlayerCount() noexcept
    {
        return _context._matchIdentity.HasMatch()
            ? static_cast<std::int32_t>(_context._matchIdentity.State()->PlayerCount) : 0;
    }

    void NetSession::ApplyRoster(RosterPacket roster)
    {
        SessionRosterSynchronizer::ApplyReceived(roster, IsHost(), _context._matchIdentity,
            _context._replicaStream, _context._clientLobby);
    }

    void NetSession::ReceiveMatchState(const MatchStatePacket& state, bool rotated)
    {
        if (!_context._clientLobby.AcceptsMatchState(state))
        {
            return;
        }
        ApplyMatchState(state, rotated);
    }

    void NetSession::ApplyMatchState(MatchStatePacket state, bool rotated)
    {
        SessionMatchTransition::Apply(state, rotated, _context._runtime, _context._matchIdentity,
            _context._replicaStream, RemotePlayers(), _context._authorityState, _context._connectionHealth,
            SessionSlots(), &NetSession::SendHello);
    }

    std::uint16_t NetSession::CurrentMatchId() noexcept
    {
        return _context._matchIdentity.MatchId(IsHost());
    }

    std::uint64_t NetSession::AuthorityEpoch() noexcept
    {
        return _context._matchIdentity.AuthorityEpoch(IsHost());
    }

    bool NetSession::MatchesStream(std::uint16_t match, std::uint64_t epoch)
    {
        return SessionMatchTransition::MatchesStream(match, epoch, IsHost(), _context._matchIdentity);
    }

    void NetSession::HandleSnapshot(ReceivedPacket packet)
    {
        SessionSnapshotReceiver::Receive(std::move(packet), FreezeGameplay(), _context._runtime,
            _context._matchIdentity, _context._replicaStream, _context._traffic);
    }

    void NetSession::HandleBye(ReceivedPacket packet)
    {
        const SessionPeerMaintenance::Ports ports{Role(), &BroadcastHostControl, &Stop};
        SessionPeerMaintenance::HandleBye(std::move(packet), ports);
    }

    void NetSession::DropTimedOutPeers(double time)
    {
        const SessionPeerMaintenance::Ports ports{Role(), &BroadcastHostControl, &Stop};
        SessionPeerMaintenance::DropTimedOut(time, ports);
    }

    bool NetSession::IdentityNeedsRefresh() noexcept
    {
        return _context._playerIdentity.NeedsRefresh(GameState::Nicknames(), _context._runtime.LocalSlot());
    }

    void NetSession::SendIntent(IntentPacket intent)
    {
        SessionIntentPublisher::Send(std::move(intent), FreezeGameplay(),
            _context._connection, _context._runtime, _context._matchIdentity, _context._traffic);
    }

    void NetSession::SendMatchEnd()
    {
        SessionMatchEndChannel::Send(_context._connection, _context._runtime, _context._matchIdentity, _context._simulationCallbacks);
    }

    void NetSession::BroadcastSnapshot()
    {
        SessionSnapshotPublisher::Publish(_context._connection, _context._runtime, _context._authorityState, _context._matchIdentity,
            _context._simulationCallbacks, _context._traffic);
    }
}
