#include "NetSession.hpp"
#include "../../Entities/Players/HalfturretEntity.hpp"

#include "../../NativeRuntime/System/Enum.hpp"

#include "../../GameState.hpp"
#include "../../Metadata/Metadata.hpp"
#include "../../Utility/Rng.hpp"
#include "../DebugLog.hpp"
#include "../MapPick.hpp"
#include "../SpectatorMode.hpp"
#include "../Chat/ChatBox.hpp"
#include "../Chat/NetChat.hpp"
#include "DemoPlayback.hpp"
#include "DemoRecorder.hpp"
#include "NetDamage.hpp"
#include "NetHealthSync.hpp"
#include "NetHitClaims.hpp"
#include "NetHitPrediction.hpp"
#include "MapVote.hpp"
#include "NetLifecycleTracker.hpp"
#include "NetLog.hpp"
#include "NetMatchEnd.hpp"
#include "NetMatchSync.hpp"
#include "NetMatchTimeSync.hpp"
#include "NetPlayerBridge.hpp"
#include "NetBombs.hpp"
#include "NetShotEvents.hpp"
#include "NetPlayerLifecycle.hpp"
#include "NetPlayerSetup.hpp"
#include "NetRoomChange.hpp"
#include "NetShotDiagnostics.hpp"
#include "NetSlotManager.hpp"
#include "NetSmoothing.hpp"
#include "NetTimingDiagnostics.hpp"
#include "NetUnlagged.hpp"
#include "PlayerColors.hpp"
#include "../../Formats/Types.hpp"
#include "../../NativeRuntime/System/BinaryPrimitives.hpp"
#include "../../NativeRuntime/System/DateTime.hpp"
#include "../../NativeRuntime/System/IO.hpp"
#include "../../NativeRuntime/System/Console.hpp"
#include "../../NativeRuntime/System/Encoding.hpp"
#include "../../NativeRuntime/System/Globalization.hpp"
#include "../../NativeRuntime/System/Managed.hpp"
#include "../../NativeRuntime/System/Random.hpp"
#include "../../NativeRuntime/System/Stopwatch.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

using ::MphRead::NativeRuntime::ConsoleWriteLine;
using ::MphRead::NativeRuntime::DecodeUtf8Scalar;
using ::MphRead::NativeRuntime::HasFlag;
using ::MphRead::NativeRuntime::IncrementInPlace;
using ::MphRead::NativeRuntime::MathMax;
using ::MphRead::NativeRuntime::RequireReference;
using ::MphRead::NativeRuntime::StringIsNullOrWhiteSpace;
using ::MphRead::NativeRuntime::Utf8Scalar;

namespace
{
    namespace Runtime = ::MphRead::NativeRuntime;

    [[nodiscard]] std::vector<std::uint8_t> AsciiBytes(std::string_view text)
    {
        std::vector<std::uint8_t> result;
        result.reserve(text.size());
        for (std::size_t index = 0; index < text.size();)
        {
            const Utf8Scalar unit = DecodeUtf8Scalar(text, index);
            if (!unit.Valid())
            {
                result.push_back(static_cast<std::uint8_t>('?'));
                index += unit.Length;
                continue;
            }
            if (unit.Value <= 0x7FU)
            {
                result.push_back(static_cast<std::uint8_t>(unit.Value));
            }
            else
            {
                // Encoding.ASCII replaces an unsupported UTF-16 code unit
                // with '?'. A supplementary scalar occupies a surrogate
                // pair, so the C# encoder emits two replacement bytes.
                result.push_back(static_cast<std::uint8_t>('?'));
                if (unit.Value > 0xFFFFU)
                {
                    result.push_back(static_cast<std::uint8_t>('?'));
                }
            }
            index += unit.Length;
        }
        return result;
    }

    template <typename T>
    [[nodiscard]] const std::vector<T>& RequireVector(
        const std::shared_ptr<std::vector<T>>& value)
    {
        if (value == nullptr)
        {
            throw System::NullReferenceException();
        }
        return *value;
    }

    [[nodiscard]] bool HunterDefined(std::uint8_t value) noexcept
    {
        return value <= static_cast<std::uint8_t>(MphRead::Hunter::Random);
    }

    [[nodiscard]] std::span<std::uint8_t> Scratch(std::array<std::uint8_t, MphRead::Mods::Network::NetConfig::MaxPacketSize>& scratch,
        std::size_t start = 0)
    {
        return std::span<std::uint8_t>(scratch).subspan(start);
    }

    [[nodiscard]] std::span<const std::uint8_t> First(
        const std::array<std::uint8_t, MphRead::Mods::Network::NetConfig::MaxPacketSize>& scratch, std::size_t count)
    {
        return std::span<const std::uint8_t>(scratch).first(count);
    }
}

namespace MphRead::Mods::Network
{
    // Eight player states, the header and the objective clocks in one
    // datagram (NetHealthSync takes what is left): Weavel's turret state and
    // the confirmed-impact bytes both live in a player state.
    static_assert(SnapshotHeader::Size + 8 * PlayerState::Size + NetMatchTimeSync::Size
        <= NetConfig::MaxPacketSize - 1, "a full snapshot no longer fits one datagram");

    std::unique_ptr<NetTransport> NetSession::_transport{};
    bool NetSession::_playback = false;
    std::vector<std::shared_ptr<RemotePeer>> NetSession::_peers{};
    std::shared_ptr<System::Net::IPEndPoint> NetSession::_hostEndPoint{};
    std::array<std::uint8_t, NetConfig::MaxPacketSize> NetSession::_scratch{};

    NetRole NetSession::_role = NetRole::Offline;
    std::int32_t NetSession::_localSlot = 0;
    std::uint32_t NetSession::_netFrame = 0;
    std::uint32_t NetSession::_snapshotArrived = 0;
    std::optional<std::string> NetSession::_lastError{};

    std::int64_t NetSession::_snapshotsReceived = 0;
    std::int64_t NetSession::_snapshotsSent = 0;
    std::int64_t NetSession::_statesApplied = 0;
    std::int64_t NetSession::_intentsReceived = 0;
    std::uint32_t NetSession::_appliedSnapshotFrame = 0;

    SnapshotSink NetSession::_snapshotSink{};
    std::function<void()> NetSession::_serverMatchEnded{};

    Hunter NetSession::_localHunter = Hunter::Samus;
    std::int32_t NetSession::_localColor = 0;
    const std::uint32_t NetSession::ClientId = NetSession::NewClientId();

    bool NetSession::_connectionLost = false;
    double NetSession::_lastServerPacket = 0.0;
    std::int32_t NetSession::_reAnnouncements = 0;
    double NetSession::_longestServerSilence = 0.0;
    std::int32_t NetSession::_authorityStandDowns = 0;
    bool NetSession::_reAnnounced = false;
    std::int64_t NetSession::_authorityFrames = 0;
    bool NetSession::_refused = false;
    RefusedPacket NetSession::_refusedReason{};

    std::array<std::uint16_t, Entities::PlayerEntity::SlotCapacity> NetSession::_hostGenerations{};
    std::array<std::uint32_t, Entities::PlayerEntity::SlotCapacity> NetSession::_lastSlotIntentFrame{};
    std::int64_t NetSession::_intentsOutOfOrder = 0;

    std::optional<MatchStatePacket> NetSession::_serverMatch{};
    bool NetSession::_isAuthority = false;
    bool NetSession::_authorityNeedsStateApply = false;
    std::string NetSession::_playerName = "Player";

    std::uint32_t NetSession::_lastSnapshotFrame = 0;
    bool NetSession::_hasSnapshot = false;
    std::uint32_t NetSession::_rosterRevision = 0;
    bool NetSession::_hasRoster = false;
    std::int32_t NetSession::_snapshotStreamResets = 0;
    std::int64_t NetSession::_snapshotsOutOfOrder = 0;
    std::array<PlayerState, Entities::PlayerEntity::SlotCapacity> NetSession::_snapshotScratch{};

    std::uint32_t NetSession::SnapshotAge() noexcept
    {
        return _snapshotArrived == 0
            ? std::numeric_limits<std::uint32_t>::max()
            : _netFrame >= _snapshotArrived ? _netFrame - _snapshotArrived : 0U;
    }

    std::uint32_t NetSession::RemoteIntentAge(std::int32_t slot) noexcept
    {
        if (slot < 0
            || slot >= static_cast<std::int32_t>(RemoteIntentArrived.size())
            || RemoteIntentArrived[static_cast<std::size_t>(slot)] == 0)
        {
            return std::numeric_limits<std::uint32_t>::max();
        }
        const std::uint32_t arrived = RemoteIntentArrived[static_cast<std::size_t>(slot)];
        return _netFrame >= arrived ? _netFrame - arrived : 0U;
    }

    void NetSession::NoteStatesApplied() noexcept
    {
        IncrementInPlace(_statesApplied);
        _appliedSnapshotFrame = _lastSnapshotFrame;
    }

    void NetSession::StartServerAuthority(
        SnapshotSink sink, std::function<void()> matchEnded)
    {
        Stop();
        _role = NetRole::Server;
        _snapshotSink = std::move(sink);
        _serverMatchEnded = std::move(matchEnded);
        _isAuthority = true;
        if (Mods::DebugLog::Active())
        {
            NetLog::Open("server");
        }
        _localSlot = -1;
        _netFrame = 0;
        _lastError.reset();
        NetUnlagged::Reset();
        NetHitPrediction::Reset();
        NetHitClaims::Reset();
        NetSmoothing::Reset();
    }

    void NetSession::StartHost(std::int32_t port)
    {
        Stop();
        try
        {
            _transport = std::make_unique<NetTransport>(port);
            _role = NetRole::Host;
            _localSlot = 0;
            _hostGenerations.fill(0);
            _hostGenerations[0] = 1;
            NetPlayerLifecycle::SetOccupant(0, 1);
            SlotOccupied[0] = true;
            MatchStatePacket match{};
            match.MatchId = 1;
            match.AuthorityEpoch = static_cast<std::uint64_t>(Runtime::DateTimeUtcNowTicks());
            _serverMatch = match;
            _netFrame = 0;
            _lastError.reset();
            NetHitClaims::VerdictSink(SendVerdicts);
            ConsoleWriteLine("[net] hosting on UDP " + Runtime::ToString(_transport->LocalPort()));
        }
        catch (const std::exception& ex)
        {
            _lastError = ex.what();
            ConsoleWriteLine("[net] host failed: " + std::string(ex.what()));
            _role = NetRole::Offline;
        }
    }

    void NetSession::StartClient(const std::string& address, std::int32_t port, Runtime::Guid ownerToken)
    {
        Stop();
        try
        {
            _ownerToken = ownerToken;
            _lastServerPacket = Clock();
            _transport = std::make_unique<NetTransport>(0);
            _transport->AnswerPingsImmediately();
            _hostEndPoint = std::make_shared<System::Net::IPEndPoint>(
                ResolveIPv4(address), port);
            _role = NetRole::Client;
            _localSlot = -1;
            _netFrame = 0;
            _lastError.reset();
            NetLog::Open(_playerName);
            NetLog::Event("joining " + address + ":" + Runtime::ToString(port)
                + " as \"" + _playerName + "\"");
            SendHello();
            SendIdentify();
            ConsoleWriteLine("[net] joining " + address + ":" + Runtime::ToString(port)
                + " as \"" + _playerName + "\"");
        }
        catch (const std::exception& ex)
        {
            _lastError = ex.what();
            ConsoleWriteLine("[net] join failed: " + std::string(ex.what()));
            _role = NetRole::Offline;
        }
    }

    void NetSession::StartPlayback()
    {
        Stop();
        _playback = true;
        _transport = std::make_unique<NetTransport>(0);
        _role = NetRole::Client;
        _localSlot = -1;
        _netFrame = 0;
        _lastError.reset();
    }

    void NetSession::RewindPlayback()
    {
        ContinuousPhase.Reset();
        NetPlayerLifecycle::ResetLives();
        _hasRoster = false;
        _rosterRevision = 0;
        NetUnlagged::Reset();
        NetHitPrediction::Reset();
        NetHitClaims::Reset();
        NetSmoothing::Reset();
        _hasSnapshot = false;
        _lastSnapshotFrame = 0;
        _snapshotArrived = 0;
        _appliedSnapshotFrame = 0;
        _lastSlotIntentFrame.fill(0);
        RemoteStateValid.fill(false);
        RemoteIntentValid.fill(false);
        _snapshotsReceived = 0;
        _snapshotsSent = 0;
        _snapshotsOutOfOrder = 0;
        _statesApplied = 0;
        _intentsReceived = 0;
        _intentsOutOfOrder = 0;
        NetPlayerBridge::Reset();
        NetDamage::Reset();
    }

    void NetSession::InjectPlaybackPacket(
        const std::shared_ptr<std::vector<std::uint8_t>>& data, std::int32_t length)
    {
        if (_transport != nullptr)
        {
            _transport->EnqueueForPlayback(data, length);
        }
    }

    std::array<std::uint8_t, 4> NetSession::ResolveIPv4(const std::string& address)
    {
        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_DGRAM;
        addrinfo* result = nullptr;
        const int error = getaddrinfo(address.c_str(), nullptr, &hints, &result);
        if (error != 0)
        {
#if defined(_WIN32)
            throw std::runtime_error(gai_strerrorA(error));
#else
            throw std::runtime_error(gai_strerror(error));
#endif
        }
        for (addrinfo* current = result; current != nullptr; current = current->ai_next)
        {
            if (current->ai_family != AF_INET
                || current->ai_addrlen < static_cast<decltype(current->ai_addrlen)>(
                    sizeof(sockaddr_in)))
            {
                continue;
            }
            const auto* ipv4 = reinterpret_cast<const sockaddr_in*>(current->ai_addr);
            std::array<std::uint8_t, 4> bytes{};
            std::memcpy(bytes.data(), &ipv4->sin_addr.s_addr, bytes.size());
            freeaddrinfo(result);
            return bytes;
        }
        freeaddrinfo(result);
        throw std::runtime_error(address + " has no IPv4 address");
    }

    void NetSession::Stop()
    {
        ResetLobbySession();
        _playback = false;
        NetPlayerSetup::Reset();
        SpectatorMode::Reset();
        DemoRecorder::Stop();
        NetMatchSync::Reset();
        NetSlotManager::Reset();
        NetDamage::Reset();
        NetRoomChange::Reset();
        NetMatchEnd::Reset();
        NetPlayerBridge::Reset();
        Mods::Chat::ChatBox::Clear();
        _isAuthority = false;
        _snapshotSink = {};
        _serverMatchEnded = {};
        if (_transport != nullptr)
        {
            if (_role == NetRole::Client && _hostEndPoint != nullptr)
            {
                _transport->Send(_hostEndPoint, PacketType::Bye, {});
            }
            _transport->Dispose();
            _transport.reset();
        }
        _peers.clear();
        _hostEndPoint.reset();
        _role = NetRole::Offline;
        MapVote::Reset();
        _connectionLost = false;
        _localSlot = 0;
        RemoteStateValid.fill(false);
        RemoteIntentValid.fill(false);
        RemoteIntentArrived.fill(0);
        ContinuousPhase.Reset();
        SlotPing.fill(0);
        _lastSlotIntentFrame.fill(0);
        _lastServerPacket = 0.0;
        _reAnnouncements = 0;
        _longestServerSilence = 0.0;
        _authorityStandDowns = 0;
        _authorityNeedsStateApply = false;
        _authorityFrames = 0;
        _refused = false;
        _snapshotStreamResets = 0;
        _hasSnapshot = false;
        _hasRoster = false;
        NetPlayerLifecycle::Reset();
        _reAnnounced = false;
        SlotOccupied.fill(false);
        _snapshotsReceived = 0;
        _snapshotsSent = 0;
        _snapshotsOutOfOrder = 0;
        _intentsOutOfOrder = 0;
        _hasSnapshot = false;
        _lastSnapshotFrame = 0;
        _snapshotArrived = 0;
        _appliedSnapshotFrame = 0;
        _statesApplied = 0;
        _intentsReceived = 0;
        _serverMatch.reset();
        NetUnlagged::Reset();
        NetHitPrediction::Reset();
        NetHitClaims::Reset();
        NetSmoothing::Reset();
    }

    void NetSession::SendIdentify()
    {
        if (_transport == nullptr || _hostEndPoint == nullptr)
        {
            return;
        }
        const std::vector<std::uint8_t> name = AsciiBytes(_playerName);
        const std::size_t count = std::min(
            name.size(), static_cast<std::size_t>(RosterPacket::MaxNameBytes));
        _scratch[0] = static_cast<std::uint8_t>(_localHunter);
        _scratch[1] = static_cast<std::uint8_t>(PlayerColors::Clamp(_localColor));
        std::copy_n(name.begin(), count, _scratch.begin() + 2);
        _transport->Send(_hostEndPoint, PacketType::Identify, First(_scratch, count + 2));
    }

    std::uint32_t NetSession::NewClientId()
    {
        std::array<std::uint8_t, 4> bytes{};
        Runtime::RandomNumberGeneratorFill(bytes.data(), bytes.size());
        const std::uint32_t id = Runtime::ReadUInt32LittleEndian(std::span<const std::uint8_t>(bytes));
        return id == 0 ? 1U : id;
    }

    void NetSession::SendHello()
    {
        if (_transport == nullptr || _hostEndPoint == nullptr)
        {
            return;
        }
        _scratch[0] = NetConfig::ProtocolVersion;
        _scratch[1] = _localSlot >= 0 && _localSlot < 0xFF
            ? static_cast<std::uint8_t>(_localSlot)
            : 0xFFU;
        Runtime::WriteUInt32LittleEndian(Scratch(_scratch).subspan(2, 4), ClientId);
        static_cast<void>(_ownerToken.TryWriteBytes(Scratch(_scratch).subspan(6, 16)));
        _transport->Send(_hostEndPoint, PacketType::Hello, First(_scratch, 22));
    }

    void NetSession::RebindSocket()
    {
        if (_role != NetRole::Client || _transport == nullptr)
        {
            return;
        }
        const std::int32_t wasPort = _transport->LocalPort();
        _transport->Dispose();
        _transport = std::make_unique<NetTransport>(0);
        ConsoleWriteLine("[net] rebound the socket: " + Runtime::ToString(wasPort)
            + " -> " + Runtime::ToString(_transport->LocalPort()));
        SendHello();
        SendIdentify();
    }

    void NetSession::Update(double time)
    {
        if (_role == NetRole::Client && !DemoPlayback::IsActive())
        {
            time = Clock();
        }
        if (_role == NetRole::Server)
        {
            _netFrame++;
            IncrementInPlace(_authorityFrames);
            return;
        }
        if (_transport == nullptr)
        {
            return;
        }

        _netFrame++;
        if (_isAuthority)
        {
            IncrementInPlace(_authorityFrames);
        }

        for (ReceivedPacket packet : _transport->Drain())
        {
            DemoRecorder::Record(packet);
            Handle(packet, time);
        }
        PumpLobby(time);
        if (_role == NetRole::Host)
        {
            DropTimedOutPeers(time);
            if (_netFrame % 60U == 0U)
            {
                BroadcastHostControl();
            }
        }
        else if (_role == NetRole::Client && (_localSlot < 0 || _reAnnounced) && _netFrame % 60U == 0U)
        {
            SendHello();
        }
        else if (_role == NetRole::Client && _netFrame % 60U == 0U
            && time - _lastServerPacket > SilenceBeforeRejoin)
        {
            IncrementInPlace(_reAnnouncements);
            _reAnnounced = true;
            if (!_connectionLost)
            {
                _connectionLost = true;
                Mods::Chat::ChatBox::System("Connection lost, retrying...");
            }
            ConsoleWriteLine("[net] no word from the server; re-announcing (#"
                + Runtime::ToString(_reAnnouncements) + ", silent for "
                + Runtime::ToString(time - _lastServerPacket, "0.0") + " s)");
            NetLog::Event("server silent, re-announcing");
            SendHello();
            SendIdentify();
        }
        else if (_role == NetRole::Client && _netFrame % 120U == 0U
            && _localSlot >= 0 && _localSlot < static_cast<std::int32_t>(GameState::Nicknames().size()))
        {
            const std::optional<std::string> nickname = GameState::Nicknames()[_localSlot];
            const bool different = !nickname.has_value() || nickname.value() != _playerName;
            if (different)
            {
                SendIdentify();
            }
        }
    }

    void NetSession::PumpMapTransfer()
    {
        if (_transport == nullptr)
        {
            return;
        }
        for (ReceivedPacket packet : _transport->Drain())
        {
            Handle(packet, Clock());
        }
    }

    void NetSession::Handle(ReceivedPacket packet, double time)
    {
        const PacketType type = packet.Type();
        if (DemoPlayback::IsActive() && (type == PacketType::Welcome || type == PacketType::Authority
            || type == PacketType::Bye || type == PacketType::Refused))
        {
            return;
        }
        const bool fromHost = _hostEndPoint != nullptr && packet.Sender != nullptr
            && packet.Sender->Equals(*_hostEndPoint);
        if (_role == NetRole::Client && !_playback && !fromHost)
        {
            return;
        }
        if ((type == PacketType::MapOffer || type == PacketType::MapChunk)
            && (_role != NetRole::Client || !fromHost))
        {
            return;
        }
        if (_role == NetRole::Client)
        {
            if (_lastServerPacket > 0.0 && time > _lastServerPacket)
            {
                _longestServerSilence = MathMax(_longestServerSilence, time - _lastServerPacket);
            }
            _lastServerPacket = time;
            if (_connectionLost)
            {
                _connectionLost = false;
                Mods::Chat::ChatBox::System("Reconnected.");
            }
        }

        const std::span<const std::uint8_t> payload = packet.Payload();
        switch (type)
        {
        case PacketType::SessionState:
            if (_role == NetRole::Client)
            {
                SessionStatePacket session{};
                if (SessionStatePacket::TryRead(payload, session))
                {
                    ApplySessionState(session);
                }
            }
            break;
        case PacketType::LobbyCommandResult:
            if (_role == NetRole::Client)
            {
                LobbyCommandResultPacket result{};
                if (LobbyCommandResultPacket::TryRead(payload, result))
                {
                    ApplyLobbyResult(result);
                }
            }
            break;
        case PacketType::Hello:
            if (_role == NetRole::Host)
            {
                HandleHello(packet, time);
            }
            break;
        case PacketType::Welcome:
            if (_role == NetRole::Client)
            {
                if (payload.size() != 17 || payload[0] >= Entities::PlayerEntity::SlotCapacity
                    || Runtime::ReadUInt32LittleEndian(payload.subspan(1)) != ClientId)
                {
                    break;
                }
                if (_serverMatch.has_value() && !MatchesStream(Runtime::ReadUInt16LittleEndian(payload.subspan(5)),
                        Runtime::ReadUInt64LittleEndian(payload.subspan(7))))
                {
                    break;
                }
                const std::uint16_t generation = Runtime::ReadUInt16LittleEndian(payload.subspan(15));
                const std::uint16_t currentGeneration = NetPlayerLifecycle::Generation(payload[0]);
                if (generation == 0 || (currentGeneration != 0 && generation != currentGeneration
                    && !NetLifecycleTracker::Newer(generation, currentGeneration)))
                {
                    break;
                }
                NetPlayerLifecycle::SetOccupant(payload[0], generation);
                if (_reAnnounced)
                {
                    _reAnnounced = false;
                    if (_isAuthority)
                    {
                        _isAuthority = false;
                        IncrementInPlace(_authorityStandDowns);
                        ConsoleWriteLine("[net] re-admitted; standing down as the "
                            "simulation authority until the server says otherwise");
                        NetLog::Event("re-admitted, authority relinquished");
                    }
                }
                if (payload.size() >= 1)
                {
                    const std::int32_t assigned = payload[0];
                    if (assigned >= Entities::PlayerEntity::SlotCapacity)
                    {
                        break;
                    }
                    if (_localSlot >= 0 && assigned != _localSlot)
                    {
                        ConsoleWriteLine("[net] came back as slot " + Runtime::ToString(assigned)
                            + ", was slot " + Runtime::ToString(_localSlot)
                            + "; releasing the old one");
                        NetLog::Event("reconnected into slot " + Runtime::ToString(assigned)
                            + ", was " + Runtime::ToString(_localSlot));
                        NetSlotManager::ReleaseSlot(_localSlot);
                    }
                    _localSlot = assigned;
                    ConsoleWriteLine("[net] joined as slot " + Runtime::ToString(_localSlot));
                    NetLog::Event("server assigned slot " + Runtime::ToString(_localSlot));
                }
            }
            break;
        case PacketType::Intent:
            if (_role == NetRole::Host)
            {
                HandleIntent(packet, time);
            }
            break;
        case PacketType::SlotIntent:
            if (_role == NetRole::Client)
            {
                HandleSlotIntent(packet);
            }
            break;
        case PacketType::Snapshot:
            if (_role == NetRole::Client)
            {
                HandleSnapshot(packet);
            }
            break;
        case PacketType::Authority:
            if (_role == NetRole::Client)
            {
                if (payload.size() != 13 || payload[0] != _localSlot
                    || !MatchesStream(Runtime::ReadUInt16LittleEndian(payload.subspan(1)),
                        Runtime::ReadUInt64LittleEndian(payload.subspan(3)))
                    || Runtime::ReadUInt16LittleEndian(payload.subspan(11)) != NetPlayerLifecycle::Generation(_localSlot))
                {
                    break;
                }
                if (!_isAuthority)
                {
                    _isAuthority = true;
                    _authorityNeedsStateApply = true;
                    ConsoleWriteLine("[net] this client is now the simulation authority");
                    NetLog::Event("became the simulation authority");
                }
            }
            break;
        case PacketType::Refused:
            if (_role == NetRole::Client)
            {
                if (payload.size() >= 1 && (_localSlot < 0 || payload[0] == RefusedPacket::ReasonKicked))
                {
                    _refusedReason = RefusedPacket::Read(payload);
                    _refused = true;
                }
            }
            break;
        case PacketType::Roster:
            if (_role == NetRole::Client)
            {
                HandleRoster(packet);
            }
            break;
        case PacketType::Ping:
            if (_role == NetRole::Client && _hostEndPoint != nullptr && _transport != nullptr)
            {
                _transport->Send(_hostEndPoint, PacketType::Pong, payload);
            }
            break;
        case PacketType::MatchState:
        case PacketType::MapChange:
            if (_role == NetRole::Client)
            {
                HandleMatchState(packet, type == PacketType::MapChange);
            }
            break;
        case PacketType::HitVerdict:
            if (_role == NetRole::Client)
            {
                NetHitClaims::ApplyVerdicts(payload);
            }
            break;
        case PacketType::HitClaim:
            if (_role == NetRole::Host)
            {
                HandleHitClaim(packet);
            }
            break;
        case PacketType::Chat:
            HandleChat(packet, time);
            break;
        case PacketType::VoteState:
            if (_role == NetRole::Client && payload.size() >= VoteStatePacket::Size)
            {
                MapVote::Apply(VoteStatePacket::Read(payload));
            }
            break;
        case PacketType::MapChoices:
            if (_role == NetRole::Client && payload.size() >= MapChoicesPacket::Size)
            {
                Mods::MapPick::Apply(MapChoicesPacket::Read(payload));
            }
            break;
        case PacketType::Bye:
            HandleBye(packet);
            break;
        default:
            break;
        }
    }

    void NetSession::HandleHitClaim(ReceivedPacket packet)
    {
        std::shared_ptr<RemotePeer> peer = FindPeer(packet.Sender);
        if (peer == nullptr || peer->SlotIndex < 0)
        {
            return;
        }
        NetHitClaims::Receive(peer->SlotIndex, packet.Payload());
    }

    void NetSession::SendVerdicts(std::int32_t slot,
        std::span<const std::pair<std::uint16_t, std::uint8_t>> verdicts)
    {
        if (verdicts.empty() || _transport == nullptr)
        {
            return;
        }
        for (const std::shared_ptr<RemotePeer>& peer : _peers)
        {
            if (peer->SlotIndex != slot)
            {
                continue;
            }
            HitVerdictPacket::Write(_scratch, verdicts, CurrentMatchId(), AuthorityEpoch(),
                NetPlayerLifecycle::Generation(slot), NetPlayerLifecycle::Get(slot));
            _transport->Send(peer->EndPoint, PacketType::HitVerdict,
                First(_scratch, HitVerdictPacket::HeaderSize + verdicts.size() * HitVerdictPacket::EntrySize));
            return;
        }
    }

    void NetSession::HandleChat(ReceivedPacket packet, double time)
    {
        const std::span<const std::uint8_t> payload = packet.Payload();
        if (payload.size() < ChatPacket::Size)
        {
            return;
        }
        ChatPacket chat = ChatPacket::Read(payload);
        if (RequireReference(chat.Text).empty())
        {
            return;
        }
        if (_role == NetRole::Host)
        {
            std::shared_ptr<RemotePeer> peer = FindPeer(packet.Sender);
            if (peer == nullptr || peer->SlotIndex < 0)
            {
                return;
            }
            peer->LastSeenTime = time;
            chat.Slot = static_cast<std::uint8_t>(peer->SlotIndex);
            if (peer->SlotIndex < static_cast<std::int32_t>(GameState::Nicknames().size()))
            {
                const std::optional<std::string> nickname = GameState::Nicknames()[peer->SlotIndex];
                if (nickname.has_value() && !nickname->empty())
                {
                    chat.Name = nickname;
                }
            }
            const auto team = [](std::int32_t slot) { return Runtime::ManagedAt(SlotTeamIndex, slot); };
            const bool teamOnly = chat.Kind == ChatPacket::KindTeam && GameState::Teams() && team(peer->SlotIndex) >= 0;
            chat.Kind = teamOnly ? ChatPacket::KindTeam : ChatPacket::KindSay;
            chat.Write(_scratch);
            for (const std::shared_ptr<RemotePeer>& other : _peers)
            {
                if (other != peer && (!teamOnly || team(other->SlotIndex) == team(peer->SlotIndex)) && _transport != nullptr)
                {
                    _transport->Send(other->EndPoint, PacketType::Chat, First(_scratch, ChatPacket::Size));
                }
            }
            if (teamOnly && (_localSlot < 0 || team(_localSlot) != team(peer->SlotIndex)))
            {
                return;
            }
        }
        Mods::Chat::NetChat::Receive(chat);
    }

    void NetSession::SendChat(const std::string& value)
    {
        if (_transport == nullptr || StringIsNullOrWhiteSpace(value))
        {
            return;
        }
        std::string text = value;
        bool teamOnly = Runtime::StringStartsWithOrdinalIgnoreCase(text, "/team ");
        if (teamOnly)
        {
            text = Runtime::StringTrim(text.substr(6));
        }
        if (text.empty())
        {
            return;
        }
        const std::optional<MatchDefinition> match = ActiveMatchDefinition();
        teamOnly = teamOnly && match.has_value() && GameState::IsTeamMode(match->Mode);
        ChatPacket chat{};
        chat.Slot = static_cast<std::uint8_t>(std::max(_localSlot, 0));
        chat.Kind = teamOnly ? ChatPacket::KindTeam : ChatPacket::KindSay;
        chat.Name = _playerName;
        chat.Text = text;
        Mods::Chat::NetChat::Remember(chat);
        chat.Write(_scratch);
        if (_role == NetRole::Host)
        {
            for (const std::shared_ptr<RemotePeer>& peer : _peers)
            {
                if (teamOnly && Runtime::ManagedAt(SlotTeamIndex, peer->SlotIndex)
                    != Runtime::ManagedAt(SlotTeamIndex, std::max(_localSlot, 0)))
                {
                    continue;
                }
                _transport->Send(peer->EndPoint, PacketType::Chat, First(_scratch, ChatPacket::Size));
            }
            return;
        }
        if (_hostEndPoint != nullptr)
        {
            _transport->Send(_hostEndPoint, PacketType::Chat, First(_scratch, ChatPacket::Size));
        }
    }

    void NetSession::SendVote(std::uint8_t kind, const std::string& roomKey)
    {
        if (_transport == nullptr || _hostEndPoint == nullptr || _role != NetRole::Client)
        {
            return;
        }
        VotePacket vote{};
        vote.Kind = kind;
        vote.RoomKey = roomKey;
        vote.Write(_scratch);
        _transport->Send(_hostEndPoint, PacketType::Vote, First(_scratch, VotePacket::Size));
    }

    void NetSession::SendMapPick(const std::string& roomKey)
    {
        if (_transport == nullptr || _hostEndPoint == nullptr || _role != NetRole::Client)
        {
            return;
        }
        MapPickPacket pick{};
        pick.RoomKey = roomKey;
        pick.Write(_scratch);
        _transport->Send(_hostEndPoint, PacketType::MapPick, First(_scratch, MapPickPacket::Size));
    }

    void NetSession::BroadcastHostControl()
    {
        if (_transport == nullptr || !_serverMatch.has_value())
        {
            return;
        }
        RosterPacket roster = RosterPacket::Create();
        roster.MatchId = CurrentMatchId();
        roster.AuthorityEpoch = AuthorityEpoch();
        roster.Revision = ++_rosterRevision;
        roster.Count = static_cast<std::uint8_t>(_peers.size() + 1);
        auto& slots = RequireReference(roster.Slots);
        auto& generations = RequireReference(roster.Generations);
        auto& names = RequireReference(roster.Names);
        slots[0] = 0;
        generations[0] = _hostGenerations[0];
        names[0] = _playerName;
        for (std::size_t i = 0; i < _peers.size(); i++)
        {
            const std::int32_t slot = _peers[i]->SlotIndex;
            slots[i + 1] = static_cast<std::uint8_t>(slot);
            generations[i + 1] = Runtime::ManagedAt(_hostGenerations, slot);
            names[i + 1] = GameState::Nicknames()[slot];
        }
        for (const std::shared_ptr<RemotePeer>& peer : _peers)
        {
            _serverMatch->Write(_scratch);
            _transport->Send(peer->EndPoint, PacketType::MatchState, First(_scratch, MatchStatePacket::Size));
            roster.Write(_scratch);
            _transport->Send(peer->EndPoint, PacketType::Roster, First(_scratch, RosterPacket::Size));
        }
    }

    void NetSession::HandleHello(ReceivedPacket packet, double time)
    {
        const std::span<const std::uint8_t> payload = packet.Payload();
        if (payload.size() < 1 || payload[0] != NetConfig::ProtocolVersion)
        {
            return;
        }
        const std::uint32_t clientId = payload.size() >= 6
            ? Runtime::ReadUInt32LittleEndian(payload.subspan(2, 4))
            : 0U;
        std::shared_ptr<RemotePeer> peer = FindPeer(packet.Sender);
        if (peer != nullptr && peer->ClientId != clientId)
        {
            const auto found = std::find(_peers.begin(), _peers.end(), peer);
            if (found != _peers.end())
            {
                _peers.erase(found);
            }
            NetPlayerLifecycle::SetOccupant(peer->SlotIndex, 0);
            peer = nullptr;
        }
        if (peer == nullptr && clientId != 0)
        {
            for (const std::shared_ptr<RemotePeer>& candidate : _peers)
            {
                if (candidate->ClientId == clientId)
                {
                    peer = candidate;
                    ConsoleWriteLine("[net] slot " + Runtime::ToString(peer->SlotIndex)
                        + " came back on " + packet.Sender->ToString()
                        + " (was " + peer->EndPoint->ToString() + ")");
                    peer->EndPoint = packet.Sender;
                    break;
                }
            }
        }
        if (peer == nullptr)
        {
            const std::int32_t slot = NextFreeSlot();
            if (slot < 0)
            {
                return;
            }
            peer = std::make_shared<RemotePeer>();
            peer->EndPoint = packet.Sender;
            peer->SlotIndex = slot;
            _peers.push_back(peer);
            auto& generation = Runtime::ManagedAt(_hostGenerations, slot);
            generation = NetLifecycleTracker::Next(generation);
            NetPlayerLifecycle::SetOccupant(slot, generation);
            ConsoleWriteLine("[net] peer " + packet.Sender->ToString()
                + " -> slot " + Runtime::ToString(slot));
        }
        peer->ClientId = clientId;
        peer->LastSeenTime = time;
        _scratch[0] = static_cast<std::uint8_t>(peer->SlotIndex);
        Runtime::WriteUInt32LittleEndian(Scratch(_scratch, 1), clientId);
        Runtime::WriteUInt16LittleEndian(Scratch(_scratch, 5), CurrentMatchId());
        Runtime::WriteUInt64LittleEndian(Scratch(_scratch, 7), AuthorityEpoch());
        Runtime::WriteUInt16LittleEndian(Scratch(_scratch, 15), NetPlayerLifecycle::Generation(peer->SlotIndex));
        if (_transport == nullptr)
        {
            throw System::NullReferenceException();
        }
        _transport->Send(peer->EndPoint, PacketType::Welcome, First(_scratch, 17));
        BroadcastHostControl();
    }

    void NetSession::HandleIntent(ReceivedPacket packet, double time)
    {
        const std::span<const std::uint8_t> payload = packet.Payload();
        if (payload.size() < IntentPacket::Size)
        {
            return;
        }
        std::shared_ptr<RemotePeer> peer = FindPeer(packet.Sender);
        if (peer == nullptr || peer->SlotIndex < 0)
        {
            return;
        }
        const IntentPacket intent = IntentPacket::Read(payload);
        if (!NetPlayerLifecycle::AcceptIntent(peer->SlotIndex, intent))
        {
            return;
        }
        NetShotEvents::Receive(peer->SlotIndex, intent);
        NetBombs::Receive(peer->SlotIndex, intent);
        if (peer->LastIntentFrame != 0 && !NetLifecycleTracker::Newer(intent.Frame, peer->LastIntentFrame))
        {
            return;
        }
        peer->LastIntentFrame = intent.Frame;
        peer->LatestIntent = intent;
        peer->LastSeenTime = time;
        const auto index = static_cast<std::size_t>(peer->SlotIndex);
        RemoteIntents.at(index) = intent;
        NetHitClaims::RecordIntent(peer->SlotIndex, intent);
        RemoteIntentValid.at(index) = true;
        RemoteIntentArrived.at(index) = std::max(_netFrame, 1U);
    }

    void NetSession::HandleSlotIntent(ReceivedPacket packet)
    {
        const std::span<const std::uint8_t> payload = packet.Payload();
        if (payload.size() < 1 + IntentPacket::Size)
        {
            return;
        }
        const std::int32_t slot = payload[0];
        if (slot < 0 || slot >= static_cast<std::int32_t>(RemoteIntents.size()) || slot == _localSlot)
        {
            return;
        }
        AcceptSlotIntent(slot, IntentPacket::Read(payload.subspan(1)));
    }

    void NetSession::AcceptSlotIntent(std::int32_t slot, IntentPacket intent)
    {
        if (slot < 0 || slot >= static_cast<std::int32_t>(RemoteIntents.size()) || slot == _localSlot)
        {
            return;
        }
        if (!NetPlayerLifecycle::AcceptIntent(slot, intent))
        {
            return;
        }
        NetShotEvents::Receive(slot, intent);
        NetBombs::Receive(slot, intent);
        const auto index = static_cast<std::size_t>(slot);
        if (_lastSlotIntentFrame[index] != 0 && !NetLifecycleTracker::Newer(intent.Frame, _lastSlotIntentFrame[index]))
        {
            IncrementInPlace(_intentsOutOfOrder);
            return;
        }
        _lastSlotIntentFrame[index] = intent.Frame;
        RemoteIntents[index] = intent;
        NetHitClaims::RecordIntent(slot, intent);
        RemoteIntentValid[index] = true;
        RemoteIntentArrived[index] = std::max(_netFrame, 1U);
        IncrementInPlace(_intentsReceived);
        if (NetLog::Enabled() && HasFlag(intent.Buttons, IntentButtons::Shoot))
        {
            NetShotDiagnostics::Trace("intent", ShotKey::For(slot, intent.AckFrame),
                static_cast<BeamType>(intent.WeaponSelect),
                "intentFrame=" + std::to_string(intent.Frame) + " intentLife=" + std::to_string(intent.LifeId)
                + " inPlay=" + (HasFlag(intent.Buttons, IntentButtons::InPlayState) ? "True" : "False")
                + " shoot=true");
        }
    }

    void NetSession::ForgetSlot(std::int32_t slot)
    {
        if (slot < 0 || slot >= Entities::PlayerEntity::SlotCapacity)
        {
            return;
        }
        ContinuousPhase.ResetSlot(slot);
        const auto index = static_cast<std::size_t>(slot);
        _lastSlotIntentFrame[index] = 0;
        RemoteIntentArrived[index] = 0;
        RemoteIntentValid[index] = false;
        RemoteIntents[index] = IntentPacket{};
        RemoteIntentArrived[index] = 0;
        RemoteStateValid[index] = false;
        RemoteStates[index] = PlayerState{};
        for (const std::shared_ptr<RemotePeer>& peer : _peers)
        {
            if (peer->SlotIndex == slot)
            {
                peer->LastIntentFrame = 0;
            }
        }
    }

    bool NetSession::ConsumeAuthorityStateSync() noexcept
    {
        if (!_authorityNeedsStateApply)
        {
            return false;
        }
        _authorityNeedsStateApply = false;
        return true;
    }

    std::int32_t NetSession::ServerPlayerCount() noexcept
    {
        return _serverMatch.has_value() ? static_cast<std::int32_t>(_serverMatch->PlayerCount) : 0;
    }

    void NetSession::HandleRoster(ReceivedPacket packet)
    {
        const std::span<const std::uint8_t> payload = packet.Payload();
        if (payload.size() < RosterPacket::Size)
        {
            return;
        }
        RosterPacket roster{};
        if (!RosterPacket::TryRead(payload, roster))
        {
            return;
        }
        ApplyRoster(roster);
    }

    void NetSession::ApplyRoster(RosterPacket roster)
    {
        if (!MatchesStream(roster.MatchId, roster.AuthorityEpoch)
            || (_hasRoster && !NetLifecycleTracker::Newer(roster.Revision, _rosterRevision)))
        {
            return;
        }
        if (roster.Count > SlotOccupied.size())
        {
            return;
        }
        const auto& slots = RequireVector(roster.Slots);
        const auto& generations = RequireVector(roster.Generations);
        std::int32_t occupied = 0;
        for (std::int32_t i = 0; i < roster.Count; i++)
        {
            const std::int32_t slot = Runtime::ManagedAt(slots, i);
            if (slot >= static_cast<std::int32_t>(SlotOccupied.size()) || (occupied & (1 << slot)) != 0)
            {
                return;
            }
            occupied |= 1 << slot;
            const std::uint16_t generation = Runtime::ManagedAt(generations, i);
            const std::uint16_t previous = NetPlayerLifecycle::Generation(slot);
            if (generation == 0 || (previous != 0 && generation != previous
                && !NetLifecycleTracker::Newer(generation, previous)))
            {
                IncrementInPlace(NetPlayerLifecycle::WrongGeneration);
                return;
            }
        }
        _hasRoster = true;
        _rosterRevision = roster.Revision;
        _rosterSessionRevision = roster.SessionRevision;
        SlotOccupied.fill(false);
        SlotLobbyReady.fill(false);
        SlotTeamIndex.fill(-1);
        const auto& teams = RequireVector(roster.Teams);
        const auto& ready = RequireVector(roster.LobbyReady);
        const auto& names = RequireVector(roster.Names);
        const auto& hunters = RequireVector(roster.Hunters);
        const auto& colors = RequireVector(roster.Colors);
        const auto& pings = RequireVector(roster.Pings);
        for (std::int32_t i = 0; i < roster.Count; i++)
        {
            const std::int32_t slot = Runtime::ManagedAt(slots, i);
            if (slot < 0 || slot >= static_cast<std::int32_t>(SlotOccupied.size()))
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
            SlotOccupied[index] = true;
            SlotTeamIndex[index] = Runtime::ManagedAt(teams, i);
            SlotLobbyReady[index] = Runtime::ManagedAt(ready, i);
            GameState::Nicknames()[slot] = Runtime::ManagedAt(names, i).value_or(std::string());
            if (HunterDefined(Runtime::ManagedAt(hunters, i)))
            {
                SlotHunter[index] = static_cast<Hunter>(Runtime::ManagedAt(hunters, i));
            }
            PlayerColors::Choice[slot] = PlayerColors::Clamp(Runtime::ManagedAt(colors, i));
            SlotPing[index] = Runtime::ManagedAt(pings, i);
        }
        for (std::int32_t slot = 0; slot < static_cast<std::int32_t>(SlotOccupied.size()); slot++)
        {
            if (!SlotOccupied[static_cast<std::size_t>(slot)])
            {
                NetPlayerLifecycle::SetOccupant(slot, 0);
            }
        }
    }

    void NetSession::HandleMatchState(ReceivedPacket packet, bool rotated)
    {
        const std::span<const std::uint8_t> payload = packet.Payload();
        if (payload.size() < MatchStatePacket::Size)
        {
            return;
        }
        const MatchStatePacket state = MatchStatePacket::Read(payload);
        if (PersistentLobby() && (IsInLobby()
            || !_serverSession.has_value() || state.MatchId != _serverSession->MatchId))
        {
            return;
        }
        ApplyMatchState(state, rotated);
    }

    void NetSession::ApplyMatchState(MatchStatePacket state, bool rotated)
    {
        static_cast<void>(rotated);
        if (state.MatchId == 0 || state.AuthorityEpoch == 0)
        {
            return;
        }
        const std::optional<MatchStatePacket> previous = _serverMatch;
        if (previous.has_value())
        {
            if (state.AuthorityEpoch != previous->AuthorityEpoch
                && !NetLifecycleTracker::Newer(state.AuthorityEpoch, previous->AuthorityEpoch))
            {
                IncrementInPlace(NetPlayerLifecycle::CrossAuthority);
                return;
            }
            if (state.AuthorityEpoch == previous->AuthorityEpoch
                && state.MatchId != previous->MatchId
                && !NetLifecycleTracker::Newer(state.MatchId, previous->MatchId))
            {
                IncrementInPlace(NetPlayerLifecycle::CrossMatch);
                return;
            }
            if (state.AuthorityEpoch == previous->AuthorityEpoch
                && state.MatchId == previous->MatchId && previous->Ending() && !state.Ending())
            {
                return;
            }
        }
        const bool newMatch = !previous.has_value() || state.MatchId != previous->MatchId;
        const bool newEpoch = !previous.has_value() || state.AuthorityEpoch != previous->AuthorityEpoch;
        _serverMatch = state;
        if (newMatch || newEpoch)
        {
            _hasSnapshot = false;
            _lastSnapshotFrame = _snapshotArrived = _appliedSnapshotFrame = 0;
            _hasRoster = false;
            _lastSlotIntentFrame.fill(0);
            RemoteIntentValid.fill(false);
            RemoteStateValid.fill(false);
            NetSmoothing::NoteRoomChanged();
            NetUnlagged::Reset();
            NetHitPrediction::ForgetPending();
            NetHitClaims::ForgetPending();
            if (newEpoch && previous.has_value())
            {
                for (std::int32_t slot = 0; slot < static_cast<std::int32_t>(SlotOccupied.size()); slot++)
                {
                    NetPlayerLifecycle::SetOccupant(slot, 0);
                }
                SlotOccupied.fill(false);
                _isAuthority = false;
                if (!_playback && _role == NetRole::Client)
                {
                    _reAnnounced = true;
                    SendHello();
                }
            }
            else if (newMatch)
            {
                NetPlayerLifecycle::ResetLives();
            }
            IncrementInPlace(_snapshotStreamResets);
        }
        if (newMatch || !previous.has_value() || previous->RoomKey != state.RoomKey)
        {
            const std::string room = state.RoomKey.value_or(std::string{});
            ConsoleWriteLine("[net] server map: " + room + " ("
                + ::MphRead::ToString(static_cast<GameMode>(state.Mode)) + ", "
                + Runtime::ToString(state.TimeRemaining, "0") + " s left)");
            const std::vector<MapChangedHandler> handlers = MapChanged;
            for (const MapChangedHandler& handler : handlers)
            {
                handler(state);
            }
        }
    }

    std::uint16_t NetSession::CurrentMatchId() noexcept
    {
        return _serverMatch.has_value() ? _serverMatch->MatchId : (IsHost() ? std::uint16_t{1} : std::uint16_t{0});
    }

    std::uint64_t NetSession::AuthorityEpoch() noexcept
    {
        return _serverMatch.has_value() ? _serverMatch->AuthorityEpoch : (IsHost() ? 1ULL : 0ULL);
    }

    bool NetSession::MatchesStream(std::uint16_t match, std::uint64_t epoch)
    {
        if (match == 0 || match != CurrentMatchId())
        {
            IncrementInPlace(NetPlayerLifecycle::CrossMatch);
            return false;
        }
        if (epoch == 0 || epoch != AuthorityEpoch())
        {
            IncrementInPlace(NetPlayerLifecycle::CrossAuthority);
            return false;
        }
        return true;
    }

    void NetSession::HandleSnapshot(ReceivedPacket packet)
    {
        if (FreezeGameplay())
        {
            return;
        }
        const std::span<const std::uint8_t> payload = packet.Payload();
        if (payload.size() < SnapshotHeader::Size)
        {
            return;
        }
        const SnapshotHeader header = SnapshotHeader::Read(payload);
        const std::size_t timeOffset = SnapshotHeader::Size + static_cast<std::size_t>(header.PlayerCount) * PlayerState::Size;
        const std::size_t healthOffset = timeOffset + NetMatchTimeSync::Size;
        if (header.PlayerCount > Entities::PlayerEntity::SlotCapacity || healthOffset > payload.size()
            || !MatchesStream(header.MatchId, header.AuthorityEpoch))
        {
            return;
        }
        if (!NetMatchTimeSync::Validate(payload.subspan(timeOffset, NetMatchTimeSync::Size))
            || !NetHealthSync::Validate(payload.subspan(healthOffset))
            || !NetHealthSync::IsCurrentMatch(payload.subspan(healthOffset)))
        {
            return;
        }
        std::int32_t occupied = 0;
        for (std::int32_t i = 0; i < header.PlayerCount; i++)
        {
            const std::int32_t slot = payload[SnapshotHeader::Size + static_cast<std::size_t>(i) * PlayerState::Size];
            if (slot >= static_cast<std::int32_t>(RemoteStates.size()) || (occupied & (1 << slot)) != 0)
            {
                return;
            }
            occupied |= 1 << slot;
        }
        if (_hasSnapshot && !NetLifecycleTracker::Newer(header.Frame, _lastSnapshotFrame))
        {
            IncrementInPlace(_snapshotsOutOfOrder);
            return;
        }
        _hasSnapshot = true;
        _lastSnapshotFrame = header.Frame;
        _snapshotArrived = std::max(_netFrame, 1U);
        IncrementInPlace(_snapshotsReceived);
        Rng::SetRng1(header.Rng1);
        Rng::SetRng2(header.Rng2);

        std::size_t offset = SnapshotHeader::Size;
        std::size_t count = 0;
        RemoteStateValid.fill(false);
        for (std::int32_t i = 0; i < header.PlayerCount; ++i)
        {
            if (offset + PlayerState::Size > payload.size())
            {
                break;
            }
            const PlayerState state = PlayerState::Read(payload.subspan(offset));
            offset += PlayerState::Size;
            if (state.SlotIndex < RemoteStates.size() && NetPlayerLifecycle::AcceptState(state, header.Frame))
            {
                const std::size_t slot = state.SlotIndex;
                RemoteStates[slot] = state;
                RemoteStateValid[slot] = true;
                if (count < _snapshotScratch.size())
                {
                    _snapshotScratch[count++] = state;
                }
            }
        }
        NetTimingDiagnostics::Snapshot(packet.ArrivedAt);
        NetSmoothing::Record(header.Frame, std::span<const PlayerState>(_snapshotScratch.data(), count));
        NetMatchTimeSync::Receive(payload.subspan(timeOffset, NetMatchTimeSync::Size));
        NetHealthSync::Receive(payload.subspan(healthOffset));
    }

    void NetSession::HandleBye(ReceivedPacket packet)
    {
        if (_role == NetRole::Host)
        {
            std::shared_ptr<RemotePeer> peer = FindPeer(packet.Sender);
            if (peer != nullptr)
            {
                ConsoleWriteLine("[net] peer " + peer->EndPoint->ToString()
                    + " left (slot " + Runtime::ToString(peer->SlotIndex) + ")");
                RemoteIntentValid.at(static_cast<std::size_t>(peer->SlotIndex)) = false;
                const auto found = std::find(_peers.begin(), _peers.end(), peer);
                if (found != _peers.end())
                {
                    _peers.erase(found);
                }
                NetPlayerLifecycle::SetOccupant(peer->SlotIndex, 0);
                BroadcastHostControl();
            }
        }
        else
        {
            ConsoleWriteLine("[net] host closed the session");
            Stop();
        }
    }

    void NetSession::DropTimedOutPeers(double time)
    {
        for (std::int32_t i = static_cast<std::int32_t>(_peers.size()) - 1; i >= 0; --i)
        {
            const std::shared_ptr<RemotePeer> peer = _peers[static_cast<std::size_t>(i)];
            if (time - peer->LastSeenTime > NetConfig::TimeoutSeconds)
            {
                ConsoleWriteLine("[net] peer " + peer->EndPoint->ToString()
                    + " timed out (slot " + Runtime::ToString(peer->SlotIndex) + ")");
                RemoteIntentValid.at(static_cast<std::size_t>(peer->SlotIndex)) = false;
                _peers.erase(_peers.begin() + i);
                NetPlayerLifecycle::SetOccupant(peer->SlotIndex, 0);
                BroadcastHostControl();
            }
        }
    }

    std::shared_ptr<RemotePeer> NetSession::FindPeer(
        const std::shared_ptr<System::Net::IPEndPoint>& endPoint)
    {
        for (const std::shared_ptr<RemotePeer>& peer : _peers)
        {
            if (peer->EndPoint != nullptr && endPoint != nullptr && peer->EndPoint->Equals(*endPoint))
            {
                return peer;
            }
        }
        return nullptr;
    }

    std::int32_t NetSession::NextFreeSlot()
    {
        for (std::int32_t slot = 1; slot < Entities::PlayerEntity::MaxPlayers(); ++slot)
        {
            bool used = false;
            for (const std::shared_ptr<RemotePeer>& peer : _peers)
            {
                if (peer->SlotIndex == slot)
                {
                    used = true;
                    break;
                }
            }
            if (!used)
            {
                return slot;
            }
        }
        return -1;
    }

    void NetSession::SendIntent(IntentPacket intent)
    {
        if (FreezeGameplay())
        {
            return;
        }
        if (_transport == nullptr || _role != NetRole::Client || _hostEndPoint == nullptr)
        {
            return;
        }
        // A deferred intent keeps the frame it was captured on (NetHooks sends
        // it after the simulation, to carry the ray the frame really fired).
        if (intent.Frame == 0)
        {
            intent.Frame = _netFrame;
        }
        intent.MatchId = CurrentMatchId();
        intent.AuthorityEpoch = AuthorityEpoch();
        intent.SlotGeneration = NetPlayerLifecycle::Generation(_localSlot);
        intent.LifeId = NetPlayerLifecycle::Get(_localSlot);
        const auto size = static_cast<std::size_t>(IntentPacket::BombFullSize);
        intent.Write(std::span<std::uint8_t>(_scratch.data(), size));
        _transport->Send(_hostEndPoint, PacketType::Intent, First(_scratch, size));
        if (_localSlot >= 0)
        {
            DemoRecorder::RecordOwnIntent(_localSlot, First(_scratch, IntentPacket::FullSize));
        }
        const std::int32_t claims = NetHitClaims::Compose(_scratch);
        if (claims > 0)
        {
            _transport->Send(_hostEndPoint, PacketType::HitClaim, First(_scratch, static_cast<std::size_t>(claims)));
        }
    }

    void NetSession::SendMatchEnd()
    {
        if (_role == NetRole::Server)
        {
            if (_serverMatchEnded)
            {
                _serverMatchEnded();
            }
            return;
        }
        if (_transport == nullptr || _role != NetRole::Client || _hostEndPoint == nullptr)
        {
            return;
        }
        Runtime::WriteUInt16LittleEndian(Scratch(_scratch), CurrentMatchId());
        Runtime::WriteUInt64LittleEndian(Scratch(_scratch, 2), AuthorityEpoch());
        _transport->Send(_hostEndPoint, PacketType::MatchEnd, First(_scratch, 10));
    }

    void NetSession::BroadcastSnapshot()
    {
        if (!NetRoomChange::GameplayReady())
        {
            return;
        }
        const bool asServer = _role == NetRole::Server && static_cast<bool>(_snapshotSink);
        if (_transport == nullptr && !asServer)
        {
            return;
        }
        const bool asHost = _role == NetRole::Host && !_peers.empty();
        const bool asAuthority = _role == NetRole::Client && _isAuthority && _hostEndPoint != nullptr;
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
            PlayerState state{};
            state.SlotIndex = static_cast<std::uint8_t>(i);
            state.SlotGeneration = NetPlayerLifecycle::Generation(slot);
            state.LifeId = NetPlayerLifecycle::Get(slot);
            state.Flags = static_cast<std::uint8_t>(
                PlayerState::FlagActive
                | (player.IsAltForm() ? PlayerState::FlagAltForm : 0)
                | (player.ModIsInPlay() ? PlayerState::FlagSpawned : 0)
                | (player.EquipInfo()->Zoomed ? PlayerState::FlagZoomed : 0)
                | (HasFlag(player.Flags2(), Entities::PlayerFlags2::Spectating) ? PlayerState::FlagSpectating : 0)
                | (player.ModFrozen() ? PlayerState::FlagFrozen : 0)
                | (player.ModDisrupted() ? PlayerState::FlagDisrupted : 0)
                | (player.ModBurning() ? PlayerState::FlagBurning : 0));
            state.Position = player.Position;
            state.Speed = player.Speed();
            state.Facing = player.FacingVector();
            state.Health = static_cast<std::uint16_t>(std::clamp(player.Health(), 0,
                static_cast<std::int32_t>(std::numeric_limits<std::uint16_t>::max())));
            state.CurrentWeapon = static_cast<std::uint8_t>(player.CurrentWeapon());
            if (player.Hunter() == Hunter::Weavel && HasFlag(player.Flags2(), Entities::PlayerFlags2::Halfturret)
                && player.Halfturret() && player.Halfturret()->Health() > 0)
            {
                const auto& turret = *player.Halfturret();
                state.WeavelFlags = static_cast<std::uint8_t>(PlayerState::WeavelFlagTurretActive
                    | (turret.Grounded() ? PlayerState::WeavelFlagTurretGrounded : 0));
                state.HalfturretHealth = static_cast<std::uint8_t>(std::clamp(turret.Health(), 0, 255));
                state.HalfturretPosition = turret.Position;
            }
            if (player.Hunter() == Hunter::Weavel)
            {
                state.WeavelFlags = static_cast<std::uint8_t>(state.WeavelFlags
                    | (player.IsMorphing() ? PlayerState::WeavelFlagMorphing : 0)
                    | (player.IsUnmorphing() ? PlayerState::WeavelFlagUnmorphing : 0));
            }
            state.Team = static_cast<std::uint8_t>(player.Team());
            state.Points = static_cast<std::int16_t>(std::clamp(GameState::Points()[slot],
                static_cast<std::int32_t>(std::numeric_limits<std::int16_t>::min()),
                static_cast<std::int32_t>(std::numeric_limits<std::int16_t>::max())));
            state.Kills = static_cast<std::uint16_t>(std::clamp(GameState::Kills()[slot], 0,
                static_cast<std::int32_t>(std::numeric_limits<std::uint16_t>::max())));
            state.Deaths = static_cast<std::uint16_t>(std::clamp(GameState::Deaths()[slot], 0,
                static_cast<std::int32_t>(std::numeric_limits<std::uint16_t>::max())));
            NetDamage::Write(slot, state);
            static_cast<void>(NetPlayerLifecycle::AcceptState(state, _netFrame));
            state.Write(Scratch(_scratch, offset));
            offset += PlayerState::Size;
            ++count;
        }
        NetMatchTimeSync::Write(Scratch(_scratch, offset));
        offset += NetMatchTimeSync::Size;
        offset += static_cast<std::size_t>(NetHealthSync::Write(
            Scratch(_scratch, offset).first(NetConfig::MaxPacketSize - 1 - offset)));

        SnapshotHeader header{};
        header.MatchId = CurrentMatchId();
        header.AuthorityEpoch = AuthorityEpoch();
        header.Frame = _netFrame;
        header.Rng1 = Rng::Rng1();
        header.Rng2 = Rng::Rng2();
        header.PlayerCount = static_cast<std::uint8_t>(count);
        header.Write(_scratch);
        IncrementInPlace(_snapshotsSent);
        NetUnlagged::Record(header.Frame);
        DemoRecorder::RecordOwnSnapshot(First(_scratch, offset));
        if (asServer)
        {
            _snapshotSink(First(_scratch, offset));
            return;
        }

        NetTransport& transport = *_transport;
        if (asAuthority)
        {
            transport.Send(_hostEndPoint, PacketType::Snapshot, First(_scratch, offset));
            return;
        }
        for (const std::shared_ptr<RemotePeer>& peer : _peers)
        {
            transport.Send(peer->EndPoint, PacketType::Snapshot, First(_scratch, offset));
        }
    }
}

namespace MphRead::Mods::Network::Detail
{
    struct NetLaunchMatchState
    {
        std::optional<std::string> RoomKey;
        std::int32_t Mode;
        double TimeRemaining;
    };

    struct NetMatchSyncMatchStatePacket
    {
        float TimeRemaining = 0.0f;
        float TimeElapsed = 0.0f;
        std::uint8_t Flags = 0;
        std::uint16_t PointGoal = 0;
        std::optional<std::string> RoomKey{};

        [[nodiscard]] bool FriendlyFire() const
        {
            return (Flags & (1u << 2)) != 0;
        }

        [[nodiscard]] bool ShadowFreeze() const
        {
            return (Flags & (1u << 3)) == 0;
        }
    };

    void NetConnectCommandNetSessionStop()
    {
        NetSession::Stop();
    }

    void NetLaunchSetSessionPlayerName(const std::string& value)
    {
        NetSession::SetPlayerName(value);
    }

    void NetLaunchSetSessionLocalHunter(Hunter value)
    {
        NetSession::SetLocalHunter(value);
    }

    void NetLaunchSetSessionLocalColor(std::int32_t value)
    {
        NetSession::SetLocalColor(value);
    }

    void NetLaunchStartClient(const std::string& address, std::int32_t port)
    {
        NetSession::StartClient(address, port);
    }

    bool NetLaunchSessionActive()
    {
        return NetSession::Active();
    }

    void NetLaunchSessionUpdate(double elapsedSeconds)
    {
        NetSession::Update(elapsedSeconds);
    }

    bool NetLaunchSessionRefused()
    {
        return NetSession::Refused();
    }

    std::string NetLaunchDescribeRefusedReason(const std::string& where)
    {
        return NetSession::RefusedReason().Describe(std::optional<std::string>(where));
    }

    std::int32_t NetLaunchSessionLocalSlot()
    {
        return NetSession::LocalSlot();
    }

    std::optional<NetLaunchMatchState> NetLaunchSessionServerMatch()
    {
        const std::optional<MatchStatePacket> state = NetSession::ServerMatch();
        if (!state.has_value())
        {
            return std::nullopt;
        }
        return NetLaunchMatchState{
            state->RoomKey,
            static_cast<std::int32_t>(state->Mode),
            static_cast<double>(state->TimeRemaining)
        };
    }

    void NetLaunchSessionSendIdentify()
    {
        NetSession::SendIdentify();
    }

    bool NetMatchEndNetSessionActive()
    {
        return NetSession::Active();
    }

    bool NetMatchEndNetSessionIsAuthority()
    {
        return NetSession::IsAuthority();
    }

    bool NetMatchEndNetSessionIsHost()
    {
        return NetSession::IsHost();
    }

    std::optional<MatchStatePacket> NetMatchEndNetSessionServerMatch()
    {
        return NetSession::ServerMatch();
    }

    std::uint32_t NetMatchEndNetSessionNetFrame()
    {
        return NetSession::NetFrame();
    }

    void NetMatchEndNetSessionSendMatchEnd()
    {
        NetSession::SendMatchEnd();
    }

    bool NetMatchSyncNetSessionActive()
    {
        return NetSession::Active();
    }

    bool NetMatchSyncNetSessionServerMatchHasValue()
    {
        return NetSession::ServerMatch().has_value();
    }

    NetMatchSyncMatchStatePacket NetMatchSyncNetSessionServerMatchValue()
    {
        const MatchStatePacket state = NetSession::ServerMatch().value();
        return NetMatchSyncMatchStatePacket{
            state.TimeRemaining,
            state.TimeElapsed,
            state.Flags,
            state.PointGoal,
            state.RoomKey
        };
    }

    std::uint32_t NetPlayerBridgeLastSnapshotFrame()
    {
        return NetSession::LastSnapshotFrame();
    }

    bool NetPlayerBridgeIsAuthority()
    {
        return NetSession::IsAuthority();
    }

    std::int32_t NetPlayerBridgeSlotPingLength()
    {
        return static_cast<std::int32_t>(NetSession::SlotPing.size());
    }

    std::int32_t NetPlayerBridgeSlotPing(std::int32_t slot)
    {
        return NetSession::SlotPing.at(static_cast<std::size_t>(slot));
    }

    std::uint32_t NetPlayerBridgeNetFrame()
    {
        return NetSession::NetFrame();
    }

    bool NetPlayerSetupSessionActive()
    {
        return NetSession::Active();
    }

    std::int32_t NetPlayerSetupLocalSlot()
    {
        return NetSession::LocalSlot();
    }

    bool NetPlayerSetupIsServer()
    {
        return NetSession::IsServer();
    }

    bool NetSlotManagerSessionActive()
    {
        return NetSession::Active();
    }

    std::int32_t NetSlotManagerSessionLocalSlot()
    {
        return NetSession::LocalSlot();
    }

    bool NetSlotManagerSessionIsServer()
    {
        return NetSession::IsServer();
    }

    std::int32_t NetSlotManagerSlotOccupiedLength()
    {
        return static_cast<std::int32_t>(NetSession::SlotOccupied.size());
    }

    bool NetSlotManagerSlotOccupied(std::int32_t slot)
    {
        return NetSession::SlotOccupied.at(static_cast<std::size_t>(slot));
    }

    Hunter NetSlotManagerSlotHunter(std::int32_t slot)
    {
        return NetSession::SlotHunter.at(static_cast<std::size_t>(slot));
    }

    void NetSlotManagerSessionForgetSlot(std::int32_t slot)
    {
        NetSession::ForgetSlot(slot);
    }

    bool NetRoomChangeSessionActive()
    {
        return NetSession::Active();
    }

    std::uint32_t NetRoomChangeSessionNetFrame()
    {
        return NetSession::NetFrame();
    }

    std::optional<MatchStatePacket> NetRoomChangeSessionServerMatch()
    {
        return NetSession::ServerMatch();
    }

    std::int32_t NetRoomChangeSessionLocalSlot()
    {
        return NetSession::LocalSlot();
    }

    Hunter NetRoomChangeSessionSlotHunter(std::int32_t slot)
    {
        return NetSession::SlotHunter.at(static_cast<std::size_t>(slot));
    }

    std::int32_t NetRoomChangeSessionSlotOccupiedLength()
    {
        return static_cast<std::int32_t>(NetSession::SlotOccupied.size());
    }

    bool NetRoomChangeSessionSlotOccupied(std::int32_t slot)
    {
        return NetSession::SlotOccupied.at(static_cast<std::size_t>(slot));
    }

    std::string NetLogRoleText()
    {
        return ToString(NetSession::Role());
    }

    std::int32_t NetLogLocalSlot()
    {
        return NetSession::LocalSlot();
    }

    bool NetLogIsAuthority()
    {
        return NetSession::IsAuthority();
    }

    std::optional<MatchStatePacket> NetLogServerMatch()
    {
        return NetSession::ServerMatch();
    }

    std::int32_t NetLogSlotOccupiedLength()
    {
        return static_cast<std::int32_t>(NetSession::SlotOccupied.size());
    }

    bool NetLogSlotOccupied(std::int32_t slot)
    {
        return NetSession::SlotOccupied.at(static_cast<std::size_t>(slot));
    }

    bool NetLogRemoteStateValid(std::int32_t slot)
    {
        return NetSession::RemoteStateValid.at(static_cast<std::size_t>(slot));
    }

    bool NetLogRemoteIntentValid(std::int32_t slot)
    {
        return NetSession::RemoteIntentValid.at(static_cast<std::size_t>(slot));
    }

    void DedicatedServerApplyMatchState(const MatchStatePacket& state, bool rotated)
    {
        NetSession::ApplyMatchState(state, rotated);
    }

    void DedicatedServerApplyRoster(const RosterPacket& roster)
    {
        NetSession::ApplyRoster(roster);
    }

    void DedicatedServerAcceptSlotIntent(std::int32_t slotIndex, const IntentPacket& intent)
    {
        NetSession::AcceptSlotIntent(slotIndex, intent);
    }
}

namespace MphRead::Mods::Network
{
    namespace
    {
        // NetSession.cs NetRole : int
        constexpr ::MphRead::NativeRuntime::EnumNameEntry NetRoleNames[] = {
            {0x0ULL, "Offline"},
            {0x1ULL, "Host"},
            {0x2ULL, "Client"},
            {0x3ULL, "Server"},
        };
    }

    std::string ToString(NetRole value)
    {
        return ::MphRead::NativeRuntime::ManagedEnumToString(
            value, NetRoleNames, std::size(NetRoleNames), false);
    }
}
