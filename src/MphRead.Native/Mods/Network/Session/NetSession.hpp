#pragma once

#include "Mods/Network/Domain/NetworkCapacity.hpp"
#include "Mods/Network/Domain/MatchDefinition.hpp"
#include "Mods/Network/Protocol/NetProtocol.hpp"
#include "Mods/Network/Transport/NetTransport.hpp"
#include "Mods/Network/Protocol/SessionProtocol.hpp"
#include "Mods/Network/Session/RemotePlayerChannel.hpp"
#include "Mods/Network/Session/SessionRoster.hpp"
#include "Mods/Network/Session/SessionPorts.hpp"
#include "Mods/Network/Session/SessionLifecycle.hpp"
#include "Mods/Network/Session/SessionContext.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace MphRead::Mods::Network
{
    class SessionPacketDispatcher;
    class SessionRuntimePump;
    class SessionPeerMaintenance;
    class RemoteIntentChannel;
    class NetSessionFixture;

    // NetSession.cs and NetSessionLobby.cs: one partial class, one header.
    class NetSession final
    {
        friend class NetSessionFixture;
    public:
        NetSession() = delete;
        NetSession(const NetSession&) = delete;
        NetSession(NetSession&&) = delete;
        NetSession& operator=(const NetSession&) = delete;
        NetSession& operator=(NetSession&&) = delete;

        [[nodiscard]] static NetRole Role() noexcept { return _context._runtime.Role(); }
        [[nodiscard]] static bool Active() noexcept { return _context._runtime.Role() != NetRole::Offline; }
        [[nodiscard]] static bool IsHost() noexcept { return _context._runtime.Role() == NetRole::Host; }
        [[nodiscard]] static bool IsClient() noexcept { return _context._runtime.Role() == NetRole::Client; }
        [[nodiscard]] static bool IsServer() noexcept { return _context._runtime.Role() == NetRole::Server; }

        [[nodiscard]] static std::int32_t LocalSlot() noexcept { return _context._runtime.LocalSlot(); }
        [[nodiscard]] static std::uint32_t NetFrame() noexcept { return _context._runtime.Frame(); }
        [[nodiscard]] static std::uint32_t LastSnapshotFrame() noexcept { return _context._replicaStream.LastSnapshotFrame(); }
        [[nodiscard]] static std::uint32_t SnapshotArrived() noexcept { return _context._replicaStream.SnapshotArrived(); }
        [[nodiscard]] static std::uint32_t SnapshotAge() noexcept;
        [[nodiscard]] static const std::optional<std::string>& LastError() noexcept { return _context._runtime.LastError(); }

        static constexpr std::size_t RemotePlayerSlots = RemotePlayerChannel::SlotCapacity;
        [[nodiscard]] static bool HasRemoteState(std::int32_t slot) noexcept;
        [[nodiscard]] static const PlayerState& RemoteState(std::size_t slot) noexcept;
        [[nodiscard]] static bool HasRemoteIntent(std::int32_t slot) noexcept;
        [[nodiscard]] static const IntentPacket& RemoteIntent(std::size_t slot) noexcept;

        [[nodiscard]] static std::uint32_t RemoteIntentAge(std::int32_t slot) noexcept;

        [[nodiscard]] static std::int64_t SnapshotsReceived() noexcept { return _context._traffic.SnapshotsReceived(); }
        [[nodiscard]] static std::int64_t SnapshotsSent() noexcept { return _context._traffic.SnapshotsSent(); }
        [[nodiscard]] static std::int64_t StatesApplied() noexcept { return _context._traffic.StatesApplied(); }
        [[nodiscard]] static std::int64_t IntentsReceived() noexcept { return _context._traffic.IntentsReceived(); }
        // This machine's intents as they went on the wire: how many, and their
        // bytes (protocol 27's sections make the size vary with what is in them).
        [[nodiscard]] static std::int64_t IntentsSent() noexcept { return _context._traffic.IntentsSent(); }
        [[nodiscard]] static std::int64_t IntentBytesSent() noexcept { return _context._traffic.IntentBytesSent(); }
        static void NoteStatesApplied() noexcept;
        [[nodiscard]] static std::uint32_t AppliedSnapshotFrame() noexcept { return _context._replicaStream.AppliedSnapshotFrame(); }

        static void StartServerAuthority(SnapshotSink sink, std::function<void()> matchEnded);
        static void StartHost(std::int32_t port = NetConfig::DefaultPort);
        static void StartClient(const std::string& address,
            std::int32_t port = NetConfig::DefaultPort,
            ::MphRead::NativeRuntime::Guid ownerToken = {});
        static void StartPlayback();
        static void RewindPlayback();
        static void InjectPlaybackPacket(
            const std::shared_ptr<std::vector<std::uint8_t>>& data, std::int32_t length);
        static void Stop();

        static void SendIdentify();

        [[nodiscard]] static Hunter LocalHunter() noexcept { return _context._playerIdentity.SelectedHunter(); }
        static void SetLocalHunter(Hunter value) noexcept { _context._playerIdentity.SetHunter(value); }
        [[nodiscard]] static std::int32_t LocalColor() noexcept { return _context._playerIdentity.Color(); }
        static void SetLocalColor(std::int32_t value) noexcept { _context._playerIdentity.SetColor(value); }

        static constexpr std::size_t SlotCapacity = SessionRoster::SlotCapacity;

        static const std::uint32_t ClientId;
        // Proves this process opened ClientId's connection (ConnectionSecret).
        static const ConnectionSecret Secret;

        static void RebindSocket();
        static void Update(double time);

        [[nodiscard]] static bool ConnectionLost() noexcept { return _context._connectionHealth.ConnectionLost(); }
        static void PumpMapTransfer();
        [[nodiscard]] static std::int32_t ReAnnouncements() noexcept { return _context._connectionHealth.Reannouncements(); }
        [[nodiscard]] static double LongestServerSilence() noexcept { return _context._connectionHealth.LongestServerSilence(); }
        [[nodiscard]] static std::int32_t AuthorityStandDowns() noexcept { return _context._authorityState.StandDowns(); }
        [[nodiscard]] static std::int64_t AuthorityFrames() noexcept { return _context._authorityState.Frames(); }
        [[nodiscard]] static bool Refused() noexcept { return _context._connectionHealth.Refused(); }
        [[nodiscard]] static RefusedPacket RefusedReason() { return _context._connectionHealth.RefusedReason(); }

        static void SendChat(const std::string& text);
        static void SendVote(std::uint8_t kind, const std::string& roomKey);
        static void SendMapPick(const std::string& roomKey);
        // The server asked for a map pack this player holds (see SessionMapUpload).
        static void AnswerMapWant(std::span<const std::uint8_t> payload);

        static void AcceptSlotIntent(std::int32_t slot, IntentPacket intent);
        [[nodiscard]] static std::int64_t IntentsOutOfOrder() noexcept { return _context._replicaStream.IntentsOutOfOrder(); }
        static void ForgetSlot(std::int32_t slot);

        [[nodiscard]] static std::optional<MatchStatePacket> ServerMatch() { return _context._matchIdentity.State(); }
        [[nodiscard]] static bool IsAuthority() noexcept
        {
            return IsServer() && _context._authorityState.IsAuthority();
        }
        [[nodiscard]] static bool ConsumeAuthorityStateSync() noexcept;
        [[nodiscard]] static std::int32_t ServerPlayerCount() noexcept;

        [[nodiscard]] static const std::string& PlayerName() noexcept { return _context._playerIdentity.Name(); }
        static void SetPlayerName(std::string value) { _context._playerIdentity.SetName(std::move(value)); }

        static void ApplyRoster(RosterPacket roster);
        static void ApplyMatchState(MatchStatePacket state, bool rotated);

        [[nodiscard]] static std::uint16_t CurrentMatchId() noexcept;
        [[nodiscard]] static std::uint64_t AuthorityEpoch() noexcept;
        [[nodiscard]] static std::int32_t SnapshotStreamResets() noexcept { return _context._replicaStream.StreamResets(); }
        [[nodiscard]] static bool MatchesStream(std::uint16_t match, std::uint64_t epoch);
        [[nodiscard]] static std::int64_t SnapshotsOutOfOrder() noexcept { return _context._replicaStream.SnapshotsOutOfOrder(); }

        static void SendIntent(IntentPacket intent);
        static void SendMatchEnd();
        static void BroadcastSnapshot(MphRead::Scene& scene);

        // NetSessionLobby.cs
        [[nodiscard]] static const std::optional<SessionStatePacket>& ServerSession() noexcept
        {
            return _context._clientLobby.Session();
        }
        [[nodiscard]] static SessionPhase CurrentSessionPhase() noexcept;
        [[nodiscard]] static std::uint16_t SessionRevision() noexcept;
        [[nodiscard]] static std::optional<MatchDefinition> ActiveMatchDefinition();
        [[nodiscard]] static bool LocalIsLobbyOwner() noexcept;
        [[nodiscard]] static bool IsInLobby() noexcept;
        [[nodiscard]] static bool IsStarting() noexcept;
        [[nodiscard]] static bool IsPlaying() noexcept;
        [[nodiscard]] static bool IsPostMatch() noexcept;
        [[nodiscard]] static bool CanEditLobby() noexcept;
        [[nodiscard]] static bool PersistentLobby() noexcept;
        [[nodiscard]] static bool FreezeGameplay() noexcept;
        [[nodiscard]] static bool ShouldLoadMatch() noexcept;
        [[nodiscard]] static const std::string& LobbyMessage() noexcept { return _context._clientLobby.Message(); }
        [[nodiscard]] static bool LobbyCommandPending() noexcept { return _context._clientLobby.HasPendingCommand(); }
        [[nodiscard]] static std::int32_t ConnectionPort();
        [[nodiscard]] static bool SessionTimedOut();
        [[nodiscard]] static double Clock();

        static void Pump(double time = 0);
        static bool SendLobbyCommand(LobbyCommandType type, std::uint8_t targetSlot = 255,
            std::int8_t team = -1, bool ready = false,
            std::optional<SessionStatePacket> configuration = std::nullopt);
        static void ApplySessionState(SessionStatePacket state);
        static void MarkMatchLoaded();
        static void ReportMatchLoadFailed(const std::string& reason);
        static void ResetMatchState();
        [[nodiscard]] static RosterPacket LobbyRoster();

    private:
        friend class SessionPacketDispatcher;

        [[nodiscard]] static SessionLifecycle::Ports LifecyclePorts();
        static void SendHello();
        static void Handle(ReceivedPacket packet, double time);
        [[nodiscard]] static bool IsPlayback() noexcept;
        [[nodiscard]] static bool IsHostEndpoint(
            const std::shared_ptr<System::Net::IPEndPoint>& endpoint);
        [[nodiscard]] static bool RecordServerPacket(double time) noexcept;
        static void HandlePing(std::span<const std::uint8_t> payload);
        static void HandleWelcome(std::span<const std::uint8_t> payload);
        static void HandleAuthority(std::span<const std::uint8_t> payload);
        static void HandleRefused(std::span<const std::uint8_t> payload);
        static void HandleChat(ReceivedPacket packet, double time);
        static void BroadcastHostControl();
        static void HandleHello(ReceivedPacket packet, double time);
        static void HandleIntent(const ReceivedPacket& packet, double time);
        static void HandleSlotIntent(const ReceivedPacket& packet);
        static void ReceiveMatchState(const MatchStatePacket& state, bool rotated);
        static void HandleSnapshot(ReceivedPacket packet);
        static void HandleBye(ReceivedPacket packet);
        static void DropTimedOutPeers(double time);
        [[nodiscard]] static bool IdentityNeedsRefresh() noexcept;

        // NetSessionLobby.cs
        static void SendLobbyPacket(const LobbyCommandPacket& command);
        static void PumpLobby(double now);
        static void ApplyLobbyResult(const LobbyCommandResultPacket& result);
        static void ResetLobbySession();

        static constexpr double SilenceBeforeRejoin = 5.0;

        static SessionContext _context;
    };
}

namespace MphRead
{
    class Scene;
}
