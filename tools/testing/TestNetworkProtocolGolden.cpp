#include "Mods/Network/Protocol/NetProtocol.hpp"
#include "Mods/Network/Protocol/SessionProtocol.hpp"

#include <array>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    using namespace MphRead::Mods::Network;

    struct GoldenPacket final
    {
        std::string Name;
        std::vector<std::uint8_t> Bytes;
    };

    template <std::size_t Size, typename Writer>
    std::vector<std::uint8_t> Encode(Writer&& write)
    {
        std::array<std::uint8_t, Size> bytes{};
        write(std::span<std::uint8_t>(bytes));
        return { bytes.begin(), bytes.end() };
    }

    std::string Hex(const std::vector<std::uint8_t>& bytes)
    {
        std::ostringstream result;
        result << std::hex << std::setfill('0');
        for (std::uint8_t byte : bytes)
        {
            result << std::setw(2) << static_cast<unsigned int>(byte);
        }
        return result.str();
    }

    std::vector<GoldenPacket> Capture()
    {
        std::vector<GoldenPacket> packets;
        auto add = [&packets](std::string name, std::vector<std::uint8_t> bytes)
        {
            packets.push_back({ std::move(name), std::move(bytes) });
        };

        add("PacketType", {
            static_cast<std::uint8_t>(PacketType::Hello), static_cast<std::uint8_t>(PacketType::Welcome),
            static_cast<std::uint8_t>(PacketType::Intent), static_cast<std::uint8_t>(PacketType::Snapshot),
            static_cast<std::uint8_t>(PacketType::Bye), static_cast<std::uint8_t>(PacketType::Ping),
            static_cast<std::uint8_t>(PacketType::Pong), static_cast<std::uint8_t>(PacketType::MatchState),
            static_cast<std::uint8_t>(PacketType::MapChange), static_cast<std::uint8_t>(PacketType::Roster),
            static_cast<std::uint8_t>(PacketType::Identify), static_cast<std::uint8_t>(PacketType::Authority),
            static_cast<std::uint8_t>(PacketType::SlotIntent), static_cast<std::uint8_t>(PacketType::StatusQuery),
            static_cast<std::uint8_t>(PacketType::StatusReply), static_cast<std::uint8_t>(PacketType::MatchEnd),
            static_cast<std::uint8_t>(PacketType::MasterHeartbeat), static_cast<std::uint8_t>(PacketType::MasterQuery),
            static_cast<std::uint8_t>(PacketType::MasterList), static_cast<std::uint8_t>(PacketType::HostRequest),
            static_cast<std::uint8_t>(PacketType::HostReply), static_cast<std::uint8_t>(PacketType::Refused),
            static_cast<std::uint8_t>(PacketType::Chat), static_cast<std::uint8_t>(PacketType::Vote),
            static_cast<std::uint8_t>(PacketType::VoteState), static_cast<std::uint8_t>(PacketType::MapChoices),
            static_cast<std::uint8_t>(PacketType::MapPick), static_cast<std::uint8_t>(PacketType::HitClaim),
            static_cast<std::uint8_t>(PacketType::HitVerdict), static_cast<std::uint8_t>(PacketType::MapOffer),
            static_cast<std::uint8_t>(PacketType::MapWant), static_cast<std::uint8_t>(PacketType::MapChunk),
            static_cast<std::uint8_t>(PacketType::SessionState), static_cast<std::uint8_t>(PacketType::LobbyCommand),
            static_cast<std::uint8_t>(PacketType::LobbyCommandResult), static_cast<std::uint8_t>(PacketType::MatchLoaded),
            static_cast<std::uint8_t>(PacketType::MatchLoadFailed), static_cast<std::uint8_t>(PacketType::PickupClaim),
            static_cast<std::uint8_t>(PacketType::PickupVerdict), static_cast<std::uint8_t>(PacketType::MapDone) });

        HelloPacket hello;
        hello.Protocol = NetConfig::ProtocolVersion;
        hello.Slot = 3;
        hello.ClientId = 0x12345678U;
        std::array<std::uint8_t, 16> guidBytes{};
        for (std::size_t i = 0; i < guidBytes.size(); i++)
        {
            guidBytes[i] = static_cast<std::uint8_t>(i + 1);
            hello.Secret[i] = static_cast<std::uint8_t>(0xA0U + i);
        }
        hello.OwnerToken = ::MphRead::NativeRuntime::Guid(guidBytes);
        add("Hello", Encode<HelloPacket::Size>([&hello](auto bytes) { hello.Write(bytes); }));

        RefusedPacket refused;
        refused.Reason = RefusedPacket::ReasonIdentity;
        refused.Players = 5;
        refused.MaxPlayers = 8;
        add("Refused", Encode<RefusedPacket::Size>([&refused](auto bytes) { refused.Write(bytes); }));

        HostRequestPacket host;
        host.Protocol = NetConfig::ProtocolVersion;
        host.MaxPlayers = 8;
        host.Mode = 2;
        host.TimeLimit = 600;
        host.PointGoal = 20;
        host.RoomKey = "TEST ARENA";
        host.ServerName = "Golden";
        host.Rotation = std::vector<std::pair<std::string, ::MphRead::GameMode>>{
            { "TEST PADS", ::MphRead::GameMode::Battle }
        };
        host.Policy = ServerSessionPolicy::Lobby;
        host.AllowJoinInProgress = false;
        host.RequireReady = true;
        host.Format = MatchFormat::TwoVsTwo;
        add("HostRequest", Encode<HostRequestPacket::Size + 1 + HostRequestPacket::RotationEntrySize + 4>(
            [&host](auto bytes) { host.Write(bytes); }));

        HostReplyPacket hostReply;
        hostReply.Started = true;
        hostReply.Port = 27888;
        hostReply.Reason = "ready";
        hostReply.OwnerToken = hello.OwnerToken;
        add("HostReply", Encode<HostReplyPacket::Size>([&hostReply](auto bytes) { hostReply.Write(bytes); }));

        MasterEntryPacket master;
        master.Address = 0x7F000001U;
        master.Port = 27888;
        master.Players = 3;
        master.MaxPlayers = 8;
        master.Mode = 2;
        master.Protocol = NetConfig::ProtocolVersion;
        master.ServerName = "Golden";
        master.RoomKey = "TEST ARENA";
        add("MasterEntry", Encode<MasterEntryPacket::Size>([&master](auto bytes) { master.Write(bytes); }));

        MasterHeartbeatPacket heartbeat;
        heartbeat.Protocol = master.Protocol;
        heartbeat.Port = master.Port;
        heartbeat.Players = master.Players;
        heartbeat.MaxPlayers = master.MaxPlayers;
        heartbeat.Mode = master.Mode;
        heartbeat.ServerName = master.ServerName;
        heartbeat.RoomKey = master.RoomKey;
        add("MasterHeartbeat", Encode<MasterHeartbeatPacket::Size>([&heartbeat](auto bytes) { heartbeat.Write(bytes); }));

        MatchStatePacket match;
        match.Mode = 2;
        match.TimeRemaining = 321.5F;
        match.TimeElapsed = 278.5F;
        match.PlayerCount = 3;
        match.Flags = static_cast<std::uint8_t>(MatchStatePacket::RuleFlags(1, true) | MatchStatePacket::FlagInProgress);
        match.PointGoal = 20;
        match.MatchId = 0x1234;
        match.RoomKey = "TEST ARENA";
        match.NextRoomKey = "TEST PADS";
        match.AuthorityEpoch = 0x0102030405060708ULL;
        add("MatchState", Encode<MatchStatePacket::Size>([&match](auto bytes) { match.Write(bytes); }));

        ServerStatusPacket status;
        status.Phase = SessionPhase::InMatch;
        status.Format = MatchFormat::TwoVsTwo;
        status.LobbyEnabled = true;
        status.AllowJoinInProgress = true;
        status.Match = match;
        status.MaxPlayers = 8;
        status.Protocol = NetConfig::ProtocolVersion;
        status.Flags = ServerStatusPacket::FlagCanHost;
        status.ServerName = "Golden";
        add("ServerStatus", Encode<ServerStatusPacket::SizeWithFlags>([&status](auto bytes) { status.Write(bytes); }));

        RosterPacket roster = RosterPacket::Create();
        roster.Count = 2;
        roster.MatchId = match.MatchId;
        roster.AuthorityEpoch = match.AuthorityEpoch;
        roster.Revision = 9;
        roster.SessionRevision = 4;
        (*roster.Slots)[0] = 0;
        (*roster.Slots)[1] = 3;
        (*roster.Hunters)[0] = 1;
        (*roster.Hunters)[1] = 5;
        (*roster.Colors)[0] = 2;
        (*roster.Colors)[1] = 3;
        (*roster.Pings)[0] = 12;
        (*roster.Pings)[1] = 34;
        (*roster.Names)[0] = "Alpha";
        (*roster.Names)[1] = "Bravo";
        (*roster.Teams)[0] = 0;
        (*roster.Teams)[1] = 1;
        (*roster.LobbyReady)[0] = true;
        (*roster.Generations)[0] = 7;
        (*roster.Generations)[1] = 8;
        add("Roster", Encode<RosterPacket::Size>([&roster](auto bytes) { roster.Write(bytes); }));

        ChatPacket chat;
        chat.Slot = 3;
        chat.Kind = ChatPacket::KindTeam;
        chat.Name = "Bravo";
        chat.Text = "hello";
        add("Chat", Encode<ChatPacket::Size>([&chat](auto bytes) { chat.Write(bytes); }));

        IntentPacket intent;
        intent.MatchId = match.MatchId;
        intent.AuthorityEpoch = match.AuthorityEpoch;
        intent.SlotGeneration = 7;
        intent.LifeId = 9;
        intent.Frame = 600;
        intent.Buttons = IntentButtons::Shoot | IntentButtons::Jump;
        intent.Aim = { 0.25F, -0.5F, 0.75F };
        intent.Position = { 1.0F, 2.0F, 3.0F };
        intent.WeaponSelect = 4;
        intent.Presses = std::make_shared<std::vector<std::uint32_t>>(IntentPacket::PressHistory, 0);
        (*intent.Presses)[0] = 598;
        intent.AmmoUa = 11;
        intent.AmmoMissiles = 22;
        intent.AckFrame = 590;
        intent.AckSubFrame = 128;
        intent.ChargeLevel = 15;
        intent.BoostDamage = 2;
        intent.ShotFlags = 3;
        intent.TouchFlags = IntentPacket::TouchPresent | IntentPacket::TouchDown | IntentPacket::TouchSamplePresent;
        intent.TouchDelta4X = -12;
        intent.TouchDelta4Y = 24;
        intent.TouchSampleSequence = 0x10203040U;
        intent.HasShot = true;
        intent.ShotOrigin = { 4.0F, 5.0F, 6.0F };
        intent.ShotDirection = { 0.0F, 0.0F, 1.0F };
        intent.ShotHistoryLength = 1;
        intent.ShotHistory[0] = { 12, 599, 4, 3, 590, { 4.0F, 5.0F, 6.0F }, { 0.0F, 0.0F, 1.0F } };
        intent.ShotHistory[0].WeaponVariantIndex = 13;
        intent.ShotHistory[0].ShotFlags = IntentPacket::ShotFlagPrimeHunter | IntentPacket::ShotFlagTargetResolved;
        intent.ShotHistory[0].Target = { IntentPacket::ShotTargetKind::Player, 0xFF, 3, 7, 9, -1, 0 };
        intent.HasBombs = true;
        intent.BombsLength = 1;
        intent.Bombs[0] = { 2, { 7.0F, 8.0F, 9.0F }, false };
        intent.HasContinuousTarget = true;
        intent.ContinuousTarget = { IntentPacket::ShotTargetKind::SceneEntity,
            static_cast<std::uint8_t>(::MphRead::EntityType::Door), 0xFF, 0, 0, 24, 0 };
        add("Intent", Encode<IntentPacket::MaxWireSize>([&intent](auto bytes) { intent.Write(bytes); }));

        DamageEvent damage;
        damage.EventId = 5;
        damage.AttackerGeneration = 7;
        damage.Damage = 81;
        damage.AttackerSlot = 1;
        damage.Beam = 4;
        damage.Flags = 3;
        damage.Direction = { 0.25F, -0.5F, 1.0F };
        add("DamageEvent", Encode<DamageEvent::Size>([&damage](auto bytes) { damage.Write(bytes); }));

        PlayerState state;
        state.SlotIndex = 3;
        state.Flags = PlayerState::FlagActive | PlayerState::FlagSpawned
            | PlayerState::FlagFrozen | PlayerState::FlagDisrupted | PlayerState::FlagBurning;
        state.Position = { 1.0F, 2.0F, 3.0F };
        state.Speed = { 0.25F, -0.5F, 0.75F };
        state.Facing = { 0.0F, 0.0F, 1.0F };
        state.Health = 77;
        state.CurrentWeapon = 4;
        state.Team = 1;
        state.Points = -2;
        state.Kills = 8;
        state.Deaths = 3;
        state.SlotGeneration = 7;
        state.LifeId = 9;
        state.DamageEventId = 5;
        state.Damage3 = damage;
        state.WeavelFlags = PlayerState::WeavelFlagTurretActive;
        state.HalfturretHealth = 40;
        state.HalfturretPosition = { 2.0F, 3.0F, 4.0F };
        state.ImpactLaunchLow = 0xFE;
        state.Impact = ImpactOffset{ 12, -20, 0 };
        state.PowerUps[0] = { 17, 1200 };
        state.PowerUps[1] = { 2, 300 };
        state.StatusEffects.BurningFrames = 300;
        state.StatusEffects.DisruptedFrames = 120;
        state.StatusEffects.FrozenFrames = 45;
        add("PlayerState", Encode<PlayerState::Size>([&state](auto bytes) { state.Write(bytes); }));

        SnapshotHeader snapshot;
        snapshot.Frame = 600;
        snapshot.Rng1 = 0x10203040U;
        snapshot.Rng2 = 0xA0B0C0D0U;
        snapshot.PlayerCount = 3;
        snapshot.PrimeHunterSlot = 2;
        snapshot.MatchId = match.MatchId;
        snapshot.AuthorityEpoch = match.AuthorityEpoch;
        add("SnapshotHeader", Encode<SnapshotHeader::Size>([&snapshot](auto bytes) { snapshot.Write(bytes); }));

        HitClaimPacket hit;
        hit.ClaimId = 17;
        hit.Frame = 600;
        hit.AckFrame = 590;
        hit.LaunchFrame = 598;
        hit.VictimSlot = 2;
        hit.Beam = 4;
        hit.Damage = 37;
        hit.Flags = HitClaimPacket::FlagHeadshot | HitClaimPacket::FlagDoubled;
        hit.HitPoint = { 1.25F, 2.5F, -3.0F };
        hit.Impulse = { 0.5F, -0.25F, 0.0F };
        hit.Impact = ImpactOffset{ 12, -20, 5 };
        hit.ShotSequence = 0x12345678U;
        hit.TurretDamage = 6;
        hit.Cause = HitClaimPacket::CauseDeathalt;
        hit.MatchId = match.MatchId;
        hit.AuthorityEpoch = match.AuthorityEpoch;
        hit.ShooterGeneration = 7;
        hit.ShooterLifeId = 9;
        hit.VictimGeneration = 11;
        hit.VictimLifeId = 13;
        add("HitClaim", Encode<HitClaimPacket::Size>([&hit](auto bytes) { hit.Write(bytes); }));

        add("HitVerdict", Encode<HitVerdictPacket::HeaderSize + 2 * HitVerdictPacket::EntrySize>([](auto bytes)
        {
            const std::array entries{ std::pair<std::uint16_t, std::uint8_t>{ 17, HitVerdictPacket::ResultApplied },
                std::pair<std::uint16_t, std::uint8_t>{ 18, HitVerdictPacket::ResultWrongLife } };
            HitVerdictPacket::Write(bytes, entries, 0x1234, 0x0102030405060708ULL, 7, 9);
        }));

        PickupClaimPacket pickup;
        pickup.MatchId = match.MatchId;
        pickup.AuthorityEpoch = match.AuthorityEpoch;
        pickup.SlotGeneration = 7;
        pickup.LifeId = 9;
        pickup.ClaimId = 17;
        pickup.SpawnId = -3;
        pickup.SpawnCount = 4;
        pickup.ContactFrame = 600;
        pickup.Position = { 1.0F, 2.0F, 3.0F };
        add("PickupClaim", Encode<PickupClaimPacket::Size>([&pickup](auto bytes) { pickup.Write(bytes); }));

        add("PickupVerdict", Encode<PickupVerdictPacket::HeaderSize + 2 * PickupVerdictPacket::EntrySize>([](auto bytes)
        {
            const std::array entries{ PickupVerdictPacket::Entry{ 17, PickupVerdictPacket::ResultGranted, 5 },
                PickupVerdictPacket::Entry{ 18, PickupVerdictPacket::ResultTaken, 5 } };
            PickupVerdictPacket::Write(bytes, entries, 0x1234, 0x0102030405060708ULL, 7, 9);
        }));

        VotePacket vote;
        vote.Kind = VotePacket::KindPropose;
        vote.RoomKey = "TEST PADS";
        add("Vote", Encode<VotePacket::Size>([&vote](auto bytes) { vote.Write(bytes); }));

        VoteStatePacket voteState;
        voteState.State = VoteStatePacket::StateRunning;
        voteState.RoomKey = "TEST PADS";
        voteState.Proposer = "Bravo";
        voteState.Yes = 3;
        voteState.No = 1;
        voteState.Eligible = 6;
        voteState.Needed = 4;
        voteState.Seconds = 12;
        add("VoteState", Encode<VoteStatePacket::Size>([&voteState](auto bytes) { voteState.Write(bytes); }));

        MapChoicesPacket choices;
        choices.Open = 1;
        choices.Count = 2;
        choices.Eligible = 3;
        choices.RoomKeys = std::make_shared<std::vector<std::optional<std::string>>>(MapChoicesPacket::MaxChoices);
        choices.Votes = std::make_shared<std::vector<std::uint8_t>>(MapChoicesPacket::MaxChoices);
        (*choices.RoomKeys)[0] = "TEST ARENA";
        (*choices.RoomKeys)[1] = "TEST PADS";
        (*choices.Votes)[0] = 2;
        (*choices.Votes)[1] = 1;
        add("MapChoices", Encode<MapChoicesPacket::Size>([&choices](auto bytes) { choices.Write(bytes); }));

        MapPickPacket pick;
        pick.RoomKey = "TEST PADS";
        add("MapPick", Encode<MapPickPacket::Size>([&pick](auto bytes) { pick.Write(bytes); }));

        SessionStatePacket session;
        session.AuthorityEpoch = match.AuthorityEpoch;
        session.Phase = SessionPhase::Starting;
        session.Policy = ServerSessionPolicy::Lobby;
        session.Revision = 4;
        session.MatchId = match.MatchId;
        session.OwnerSlot = 1;
        session.MaxPlayers = 8;
        session.ExpectedParticipants = 3;
        session.LoadedParticipants = 2;
        session.RuleFlags = SessionRules::FriendlyFire | SessionRules::RequireReady;
        session.Match.RoomKey = "TEST ARENA";
        session.Match.Mode = ::MphRead::GameMode::Battle;
        session.Match.TimeLimitSeconds = 600;
        session.Match.PointGoal = 20;
        session.WorldProfile = { 3, ::MphRead::Mods::Multiplayer::ResourceSpawnProfile::Standard };
        add("SessionState", Encode<SessionStatePacket::Size>([&session](auto bytes) { session.Write(bytes); }));

        LobbyCommandPacket command;
        command.CommandId = 0x12345678U;
        command.ExpectedRevision = 4;
        command.Type = LobbyCommandType::SetTeam;
        command.TargetSlot = 3;
        command.TeamIndex = 1;
        command.Ready = true;
        command.Configuration = session;
        add("LobbyCommand", Encode<LobbyCommandPacket::Size>([&command](auto bytes) { command.Write(bytes); }));

        LobbyCommandPacket updateMatch = command;
        updateMatch.CommandId++;
        updateMatch.Type = LobbyCommandType::UpdateMatch;
        add("LobbyCommandUpdateMatch", Encode<LobbyCommandPacket::Size>(
            [&updateMatch](auto bytes) { updateMatch.Write(bytes); }));

        LobbyCommandResultPacket result;
        result.CommandId = command.CommandId;
        result.ResultCode = LobbyResultCode::StaleRevision;
        result.CurrentRevision = 5;
        result.Reason = "stale";
        add("LobbyCommandResult", Encode<LobbyCommandResultPacket::Size>([&result](auto bytes) { result.Write(bytes); }));

        MatchLoadedPacket loaded;
        loaded.MatchId = match.MatchId;
        add("MatchLoaded", Encode<MatchLoadedPacket::Size>([&loaded](auto bytes) { loaded.Write(bytes); }));

        MatchLoadFailedPacket loadFailed;
        loadFailed.MatchId = match.MatchId;
        loadFailed.Reason = "missing map";
        add("MatchLoadFailed", Encode<MatchLoadFailedPacket::Size>([&loadFailed](auto bytes) { loadFailed.Write(bytes); }));

        return packets;
    }

    template <typename T>
    bool RoundTrips(const std::vector<std::uint8_t>& bytes)
    {
        const T decoded = T::Read(std::span<const std::uint8_t>(bytes));
        std::vector<std::uint8_t> encoded(bytes.size());
        decoded.Write(std::span<std::uint8_t>(encoded));
        return encoded == bytes;
    }

    template <typename T>
    bool CheckRoundTrip(const std::vector<GoldenPacket>& packets,
        std::string_view name, std::string_view label)
    {
        for (const GoldenPacket& packet : packets)
        {
            if (packet.Name == name)
            {
                if (!RoundTrips<T>(packet.Bytes))
                {
                    std::cerr << "Protocol 33 decode/encode mismatch: " << label << '\n';
                    return false;
                }
                return true;
            }
        }
        std::cerr << "Missing Protocol 33 round-trip fixture: " << label << '\n';
        return false;
    }

    template <typename T, typename TryRead>
    bool CheckTryReadRoundTrip(const std::vector<GoldenPacket>& packets,
        std::string_view name, std::string_view label, TryRead&& tryRead)
    {
        for (const GoldenPacket& packet : packets)
        {
            if (packet.Name == name)
            {
                T decoded{};
                if (!tryRead(std::span<const std::uint8_t>(packet.Bytes), decoded))
                {
                    std::cerr << "Protocol 33 rejected valid round-trip fixture: " << label << '\n';
                    return false;
                }
                std::vector<std::uint8_t> encoded(packet.Bytes.size());
                decoded.Write(std::span<std::uint8_t>(encoded));
                if (encoded != packet.Bytes)
                {
                    std::cerr << "Protocol 33 decode/encode mismatch: " << label << '\n';
                    return false;
                }
                return true;
            }
        }
        std::cerr << "Missing Protocol 33 round-trip fixture: " << label << '\n';
        return false;
    }
}

int main(int argc, char** argv)
{
    using namespace MphRead::Mods::Network;
    static_assert(NetConfig::ProtocolVersion == 33);
    static_assert(HelloPacket::Size == 38);
    static_assert(static_cast<std::uint8_t>(PacketType::Hello) == 1);
    static_assert(static_cast<std::uint8_t>(PacketType::HitClaim) == 30);
    static_assert(static_cast<std::uint8_t>(PacketType::PickupClaim) == 41);
    static_assert(static_cast<std::uint8_t>(PacketType::PickupVerdict) == 42);

    const auto packets = Capture();
    bool roundTrips = true;
    roundTrips &= CheckRoundTrip<HelloPacket>(packets, "Hello", "Hello");
    roundTrips &= CheckRoundTrip<RefusedPacket>(packets, "Refused", "Refused");
    roundTrips &= CheckRoundTrip<HostRequestPacket>(packets, "HostRequest", "HostRequest");
    roundTrips &= CheckRoundTrip<HostReplyPacket>(packets, "HostReply", "HostReply");
    roundTrips &= CheckRoundTrip<MasterEntryPacket>(packets, "MasterEntry", "MasterEntry");
    roundTrips &= CheckRoundTrip<MasterHeartbeatPacket>(packets, "MasterHeartbeat", "MasterHeartbeat");
    roundTrips &= CheckRoundTrip<MatchStatePacket>(packets, "MatchState", "MatchState");
    roundTrips &= CheckRoundTrip<ServerStatusPacket>(packets, "ServerStatus", "ServerStatus");
    roundTrips &= CheckRoundTrip<RosterPacket>(packets, "Roster", "Roster");
    roundTrips &= CheckRoundTrip<ChatPacket>(packets, "Chat", "Chat");
    roundTrips &= CheckRoundTrip<IntentPacket>(packets, "Intent", "Intent");
    roundTrips &= CheckRoundTrip<DamageEvent>(packets, "DamageEvent", "DamageEvent");
    roundTrips &= CheckRoundTrip<PlayerState>(packets, "PlayerState", "PlayerState");
    roundTrips &= CheckRoundTrip<SnapshotHeader>(packets, "SnapshotHeader", "SnapshotHeader");
    roundTrips &= CheckRoundTrip<HitClaimPacket>(packets, "HitClaim", "HitClaim");
    roundTrips &= CheckRoundTrip<PickupClaimPacket>(packets, "PickupClaim", "PickupClaim");
    roundTrips &= CheckRoundTrip<VotePacket>(packets, "Vote", "Vote");
    roundTrips &= CheckRoundTrip<VoteStatePacket>(packets, "VoteState", "VoteState");
    roundTrips &= CheckRoundTrip<MapChoicesPacket>(packets, "MapChoices", "MapChoices");
    roundTrips &= CheckRoundTrip<MapPickPacket>(packets, "MapPick", "MapPick");
    roundTrips &= CheckTryReadRoundTrip<SessionStatePacket>(packets, "SessionState", "SessionState",
        [](auto bytes, auto& packet) { return SessionStatePacket::TryRead(bytes, packet); });
    roundTrips &= CheckTryReadRoundTrip<LobbyCommandPacket>(packets, "LobbyCommandUpdateMatch", "LobbyCommandUpdateMatch",
        [](auto bytes, auto& packet) { return LobbyCommandPacket::TryRead(bytes, packet); });
    roundTrips &= CheckTryReadRoundTrip<LobbyCommandResultPacket>(packets, "LobbyCommandResult", "LobbyCommandResult",
        [](auto bytes, auto& packet) { return LobbyCommandResultPacket::TryRead(bytes, packet); });
    roundTrips &= CheckTryReadRoundTrip<MatchLoadedPacket>(packets, "MatchLoaded", "MatchLoaded",
        [](auto bytes, auto& packet) { return MatchLoadedPacket::TryRead(bytes, packet); });
    roundTrips &= CheckTryReadRoundTrip<MatchLoadFailedPacket>(packets, "MatchLoadFailed", "MatchLoadFailed",
        [](auto bytes, auto& packet) { return MatchLoadFailedPacket::TryRead(bytes, packet); });
    if (!roundTrips)
    {
        return 1;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--capture")
    {
        for (const auto& packet : packets)
        {
            std::cout << packet.Name << '=' << Hex(packet.Bytes) << '\n';
        }
        return 0;
    }
    if (argc != 2)
    {
        std::cerr << "Usage: fruity_network_protocol_golden_tests <golden-file> | --capture\n";
        return 2;
    }

    std::ifstream input(argv[1]);
    if (!input)
    {
        std::cerr << "Could not open Protocol 33 golden file: " << argv[1] << '\n';
        return 2;
    }
    std::map<std::string, std::string> expected;
    std::string line;
    while (std::getline(input, line))
    {
        if (line.empty() || line[0] == '#')
        {
            continue;
        }
        const std::size_t separator = line.find('=');
        if (separator == std::string::npos || separator == 0
            || !expected.emplace(line.substr(0, separator), line.substr(separator + 1)).second)
        {
            std::cerr << "Malformed or duplicate Protocol 33 golden entry.\n";
            return 2;
        }
    }

    bool passed = expected.size() == packets.size();
    for (const auto& packet : packets)
    {
        const auto found = expected.find(packet.Name);
        if (found == expected.end() || found->second != Hex(packet.Bytes))
        {
            std::cerr << "Protocol 33 raw-wire mismatch: " << packet.Name << '\n';
            passed = false;
        }
    }
    if (!passed)
    {
        std::cerr << "Expected " << expected.size() << " golden packets; captured " << packets.size() << ".\n";
        return 1;
    }
    std::cout << "Protocol 33 raw-wire goldens passed: " << packets.size() << " packet layouts.\n";
    return 0;
}
