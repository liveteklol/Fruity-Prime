#pragma once

// The wire format of the C# build's multiplayer (Mods/Network/NetProtocol.cs),
// byte for byte, so this client joins the same dedicated servers. Every field
// is little-endian; every packet is one datagram, its first byte the type.

#include "game/Collision.h" // Vec3

#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace fp::net {

// NetConfig
constexpr int ProtocolVersion = 14;
constexpr uint16_t DefaultPort = 27888;
constexpr int MaxPacketSize = 1232;
constexpr int SlotCapacity = 8; // PlayerEntity.SlotCapacity
constexpr double TimeoutSeconds = 30.0;

enum class PacketType : uint8_t {
    Hello = 1, Welcome = 2, Intent = 3, Snapshot = 4, Bye = 5, Ping = 6, Pong = 7, MatchState = 8, MapChange = 9,
    Roster = 10, Identify = 11, Authority = 12, SlotIntent = 13, StatusQuery = 14, StatusReply = 15, MatchEnd = 16,
    Refused = 22, Chat = 23, Vote = 26, VoteState = 27, MapChoices = 28, MapPick = 29, HitClaim = 30, HitVerdict = 31,
    MapOffer = 32, MapWant = 33, MapChunk = 34, MapDone = 35, SessionState = 36, LobbyCommand = 37,
    LobbyCommandResult = 38, MatchLoaded = 39, MatchLoadFailed = 40,
};

// Little-endian reads and writes into a byte span.
inline uint16_t readU16(std::span<const uint8_t> s, size_t at) { return static_cast<uint16_t>(s[at] | s[at + 1] << 8); }
inline int16_t readI16(std::span<const uint8_t> s, size_t at) { return static_cast<int16_t>(readU16(s, at)); }
inline uint32_t readU32(std::span<const uint8_t> s, size_t at)
{
    return s[at] | s[at + 1] << 8 | s[at + 2] << 16 | static_cast<uint32_t>(s[at + 3]) << 24;
}
inline uint64_t readU64(std::span<const uint8_t> s, size_t at) { return readU32(s, at) | static_cast<uint64_t>(readU32(s, at + 4)) << 32; }
inline float readF32(std::span<const uint8_t> s, size_t at)
{
    const uint32_t bits = readU32(s, at);
    float value;
    std::memcpy(&value, &bits, 4);
    return value;
}
inline Vec3 readVec(std::span<const uint8_t> s, size_t at) { return {readF32(s, at), readF32(s, at + 4), readF32(s, at + 8)}; }
inline void writeU16(std::span<uint8_t> s, size_t at, uint16_t v)
{
    s[at] = static_cast<uint8_t>(v);
    s[at + 1] = static_cast<uint8_t>(v >> 8);
}
inline void writeU32(std::span<uint8_t> s, size_t at, uint32_t v)
{
    writeU16(s, at, static_cast<uint16_t>(v));
    writeU16(s, at + 2, static_cast<uint16_t>(v >> 16));
}
inline void writeU64(std::span<uint8_t> s, size_t at, uint64_t v)
{
    writeU32(s, at, static_cast<uint32_t>(v));
    writeU32(s, at + 4, static_cast<uint32_t>(v >> 32));
}
inline void writeF32(std::span<uint8_t> s, size_t at, float v)
{
    uint32_t bits;
    std::memcpy(&bits, &v, 4);
    writeU32(s, at, bits);
}
inline void writeVec(std::span<uint8_t> s, size_t at, const Vec3& v)
{
    writeF32(s, at, v[0]);
    writeF32(s, at + 4, v[1]);
    writeF32(s, at + 8, v[2]);
}
// NetText: fixed-width ASCII, a byte the font cannot draw becoming '?'.
void writeText(std::span<uint8_t> dest, const std::string& value);
std::string readText(std::span<const uint8_t> src);

// NetLifecycleTracker's serial-number arithmetic: zero is "unassigned".
inline uint16_t nextSerial(uint16_t value) { return value == 0xFFFF ? 1 : static_cast<uint16_t>(value + 1); }
inline bool newer16(uint16_t value, uint16_t previous) { return static_cast<int16_t>(value - previous) > 0; }
inline bool newer32(uint32_t value, uint32_t previous) { return static_cast<int32_t>(value - previous) > 0; }

// RefusedPacket
struct Refused {
    static constexpr uint8_t ReasonFull = 1, ReasonProtocol = 2, ReasonKicked = 3, ReasonInMatch = 4;
    static constexpr int Size = 3;
    uint8_t reason = 0, players = 0, maxPlayers = 0;
    void write(std::span<uint8_t> dest) const;
    static Refused read(std::span<const uint8_t> src);
    std::string describe(const std::string& where) const;
};

// MatchStatePacket: the map, the mode (GameMode as the C# numbers it) and the clock.
struct MatchState {
    static constexpr int MaxNameBytes = 40;
    static constexpr int Size = 1 + 4 + 4 + 1 + 1 + 2 + 2 + MaxNameBytes + MaxNameBytes + 8;
    static constexpr uint8_t FlagInProgress = 1, FlagEnding = 2, FlagFriendlyFire = 4, FlagNoShadowFreeze = 8,
                             FlagDamageShift = 4, FlagDamageMask = 3 << 4, FlagAffinityWeapons = 1 << 6;
    uint64_t authorityEpoch = 0;
    uint8_t mode = 0;
    float timeRemaining = 0, timeElapsed = 0;
    uint8_t playerCount = 0, flags = 0;
    uint16_t pointGoal = 0, matchId = 0;
    std::string roomKey, nextRoomKey;
    bool ending() const { return (flags & FlagEnding) != 0; }
    bool friendlyFire() const { return (flags & FlagFriendlyFire) != 0; }
    // MatchStatePacket.RuleFlags: the damage level (always medium here) and the affinity weapons.
    static uint8_t ruleFlags(int damageLevel, bool affinityWeapons)
    {
        return static_cast<uint8_t>(((damageLevel + 1) << FlagDamageShift) | (affinityWeapons ? FlagAffinityWeapons : 0));
    }
    void write(std::span<uint8_t> dest) const;
    static MatchState read(std::span<const uint8_t> src);
};

// IntentButtons
namespace Buttons {
constexpr uint32_t MoveLeft = 1u << 0, MoveRight = 1u << 1, MoveUp = 1u << 2, MoveDown = 1u << 3, Shoot = 1u << 4,
                   Zoom = 1u << 5, Jump = 1u << 6, Morph = 1u << 7, Boost = 1u << 8, AltAttack = 1u << 9,
                   ScanVisor = 1u << 10, NextWeapon = 1u << 11, PrevWeapon = 1u << 12, RollLeft = 1u << 13,
                   RollRight = 1u << 14, RollUp = 1u << 15, RollDown = 1u << 16,
                   // Not buttons: the sender's state.
                   ZoomedState = 1u << 17, AltFormState = 1u << 18, InPlayState = 1u << 19, SpectatingState = 1u << 20,
                   ReadyState = 1u << 21;
constexpr uint32_t States = ZoomedState | AltFormState | InPlayState | SpectatingState | ReadyState;
} // namespace Buttons

// RosterPacket: who is in which slot, as which hunter, suit and team.
struct Roster {
    static constexpr int MaxNameBytes = 16;
    static constexpr int EntrySize = 1 + 1 + 1 + 2 + MaxNameBytes + 4;
    static constexpr int HeaderSize = 17;
    static constexpr int Size = HeaderSize + SlotCapacity * EntrySize;
    struct Entry {
        uint8_t slot = 0, hunter = 0, color = 0;
        uint16_t ping = 0, generation = 0;
        int8_t team = -1;
        bool lobbyReady = false;
        std::string name;
    };
    uint8_t count = 0;
    uint16_t matchId = 0, sessionRevision = 0;
    uint64_t authorityEpoch = 0;
    uint32_t revision = 0;
    std::array<Entry, SlotCapacity> entries{};
    void write(std::span<uint8_t> dest) const;
    // RosterPacket.TryRead: false for a malformed one.
    static bool tryRead(std::span<const uint8_t> src, Roster& roster);
};

// ChatPacket
struct Chat {
    static constexpr int MaxNameBytes = 16, MaxTextBytes = 96;
    static constexpr int Size = 1 + 1 + MaxNameBytes + MaxTextBytes;
    static constexpr uint8_t KindSay = 0, KindTeam = 1, KindSystem = 2;
    uint8_t slot = 0, kind = 0;
    std::string name, text;
    void write(std::span<uint8_t> dest) const;
    static Chat read(std::span<const uint8_t> src);
};

// IntentPacket: one frame of this player's input, where it is and what it holds.
struct Intent {
    static constexpr int PressHistory = 8;
    static constexpr int Size = 4 + 4 + 12 + 1 + 4 * PressHistory + 12 + 2 + 2 + 4 + 1 + 14;
    static constexpr int FullSize = Size + 4; // with the shot state
    static constexpr uint8_t FlagDoubleDamage = 1, FlagPrimeHunter = 2;
    uint16_t matchId = 0;
    uint64_t authorityEpoch = 0;
    uint16_t slotGeneration = 0, lifeId = 0;
    uint32_t frame = 0;
    uint32_t buttons = 0;
    std::array<uint32_t, PressHistory> presses{}; // rising edges for frame, frame - 1, ...
    Vec3 aim{}, position{};
    uint8_t weaponSelect = 0xFF;
    uint16_t ammoUa = 0, ammoMissiles = 0;
    uint32_t ackFrame = 0;
    uint8_t ackSubFrame = 0;
    bool hasState = false;
    uint8_t chargeLevel = 0, boostDamage = 0, shotFlags = 0;
    void write(std::span<uint8_t> dest) const; // FullSize bytes
    static Intent read(std::span<const uint8_t> src);
};

// DamageEvent: one hit the authority resolved against the player whose state carries it.
struct DamageEvent {
    static constexpr int Size = 15;
    uint16_t eventId = 0, attackerGeneration = 0, damage = 0;
    uint8_t attackerSlot = 0xFF, beam = 0xFF, flags = 0;
    Vec3 direction{};
    void write(std::span<uint8_t> dest) const;
    static DamageEvent read(std::span<const uint8_t> src);
};

// PlayerState: one player in a snapshot.
struct PlayerState {
    static constexpr int DamageHistory = 4;
    static constexpr int Size = 54 + DamageEvent::Size * DamageHistory;
    static constexpr uint8_t FlagActive = 1, FlagAltForm = 2, FlagSpawned = 4, FlagZoomed = 8, FlagSpectating = 16,
                             FlagFrozen = 32, FlagDisrupted = 64, FlagBurning = 128;
    uint8_t slot = 0, flags = 0;
    Vec3 position{}, speed{}, facing{};
    uint16_t health = 0;
    uint8_t currentWeapon = 0, team = 0;
    int16_t points = 0;
    uint16_t kills = 0, deaths = 0, slotGeneration = 0, lifeId = 0, damageEventId = 0;
    std::array<DamageEvent, DamageHistory> damage{};
    void write(std::span<uint8_t> dest) const;
    static PlayerState read(std::span<const uint8_t> src);
};

// SnapshotHeader
struct SnapshotHeader {
    static constexpr int Size = 4 + 4 + 4 + 1 + 10;
    uint32_t frame = 0, rng1 = 0, rng2 = 0;
    uint8_t playerCount = 0;
    uint16_t matchId = 0;
    uint64_t authorityEpoch = 0;
    void write(std::span<uint8_t> dest) const;
    static SnapshotHeader read(std::span<const uint8_t> src);
};

// NetMatchTimeSync: GameState.Time and TeamTime per slot, after the players.
constexpr int MatchTimeSyncSize = SlotCapacity * 4 * 2;
// NetHealthSync: the health spawners, after the time.
struct HealthSpawnState {
    int16_t id = 0;
    bool available = false, active = false;
    uint16_t cooldown = 0, spawnCount = 0;
    int8_t pickerSlot = -1;
};
constexpr int HealthSyncHeaderSize = 3, HealthSyncEntrySize = 7, HealthSyncMaxSpawns = 56;

// SessionStatePacket: the session's phase, the match it plays and the world profile.
struct SessionState {
    static constexpr int Size = 35 + 40;
    static constexpr uint8_t PhaseLobby = 0, PhaseStarting = 1, PhaseInMatch = 2, PhasePostMatch = 3;
    // SessionRules
    static constexpr uint16_t RuleFriendlyFire = 1, RuleAffinityWeapons = 2, RuleShadowFreeze = 4, RuleRequireReady = 8,
                              RuleAllowJoinInProgress = 16, RuleLockTeams = 32;
    static constexpr uint16_t RuleHideOpponentHealth = 64;
    static constexpr uint8_t PolicyContinuous = 0, PolicyLobby = 1;
    // MatchFormat
    static constexpr uint8_t FormatAuto = 0, FormatFreeForAll = 1, FormatOneVsOne = 2, FormatTwoVsTwo = 3, FormatThreeVsThree = 4,
                             FormatFourVsFour = 5, FormatTwoVsTwoVsTwoVsTwo = 6, FormatCustom = 7;
    uint8_t phase = 0, policy = 0, entityLayerPlayers = 0;
    uint16_t revision = 0, matchId = 0;
    uint64_t authorityEpoch = 0;
    uint8_t ownerSlot = 0xFF, maxPlayers = 0, format = 0, mode = 0, resources = 0; // mode: the wire's GameMode
    uint16_t timeLimit = 0, pointGoal = 0, rules = 0;                            // timeLimit in seconds
    uint8_t expectedParticipants = 0, loadedParticipants = 0;                    // slot masks while Starting
    std::array<uint8_t, 5> customTeams{};                                        // TeamLayout: count, then A-D
    std::string roomKey;
    bool rule(uint16_t flag) const { return (rules & flag) != 0; }
    void write(std::span<uint8_t> dest) const;
    static bool tryRead(std::span<const uint8_t> src, SessionState& state);
};

// TeamLayout: how many teams and how many players each holds.
struct TeamLayout {
    uint8_t teamCount = 0;
    std::array<uint8_t, 4> capacity{};
    int totalPlayers() const { return capacity[0] + capacity[1] + capacity[2] + capacity[3]; }
    bool valid() const;
    bool operator==(const TeamLayout&) const = default;
    std::string describe() const; // "2v2", "FFA"
};
// TeamRules.ChooseTeam: the emptiest team for its size, -1 when all are full.
int chooseTeam(const TeamLayout& layout, const std::array<int, 4>& counts);
// LobbyRules: the teams a format means for a mode (the wire's GameMode).
bool isTeamModeWire(int wireMode);
TeamLayout resolveTeamLayout(int wireMode, uint8_t format, const std::array<uint8_t, 5>& custom);
// LobbyRules.ValidateDefinition: empty when the map, mode and format agree.
std::string validateDefinition(const SessionState& definition);

// VotePacket: propose a map mid-match, or answer a proposal.
struct Vote {
    static constexpr int Size = 1 + MatchState::MaxNameBytes;
    static constexpr uint8_t KindPropose = 0, KindYes = 1, KindNo = 2;
    uint8_t kind = 0;
    std::string roomKey;
    void write(std::span<uint8_t> dest) const;
    static Vote read(std::span<const uint8_t> src);
};

// VoteStatePacket: the vote in progress, or how long before another.
struct VoteState {
    static constexpr int Size = 1 + MatchState::MaxNameBytes + Chat::MaxNameBytes + 1 + 1 + 1 + 1 + 2;
    static constexpr uint8_t StateIdle = 0, StateRunning = 1, StatePassed = 2, StateFailed = 3;
    uint8_t state = StateIdle;
    std::string roomKey, proposer;
    uint8_t yes = 0, no = 0, eligible = 0, needed = 0;
    uint16_t seconds = 0; // 0xFFFF: voting is off
    void write(std::span<uint8_t> dest) const;
    static VoteState read(std::span<const uint8_t> src);
};

// MapChoicesPacket: the results screen's ballot -- the maps somebody picked, most-wanted first.
struct MapChoices {
    static constexpr int MaxChoices = 8;
    static constexpr int Size = 4 + MaxChoices * (MatchState::MaxNameBytes + 1);
    bool open = false;
    uint8_t eligible = 0;
    std::vector<std::pair<std::string, uint8_t>> choices;
    void write(std::span<uint8_t> dest) const;
    static MapChoices read(std::span<const uint8_t> src);
};

// MapPickPacket: this player's pick for the next map, or empty to take it back.
constexpr int MapPickSize = MatchState::MaxNameBytes;

// LobbyCommandPacket and its answer.
struct LobbyCommand {
    static constexpr int Size = 10 + SessionState::Size;
    static constexpr uint8_t SetReady = 0, SetTeam = 1, UpdateMatch = 2, StartMatch = 3, KickPlayer = 4, TransferOwner = 5;
    uint32_t commandId = 0;
    uint16_t expectedRevision = 0;
    uint8_t type = 0, targetSlot = 0xFF;
    int8_t teamIndex = -1;
    bool ready = false;
    SessionState configuration;
    void write(std::span<uint8_t> dest) const;
    static bool tryRead(std::span<const uint8_t> src, LobbyCommand& command);
};
struct LobbyCommandResult {
    static constexpr int Size = 7 + 96;
    // LobbyResultCode
    static constexpr uint8_t Ok = 0, NotOwner = 1, InvalidPhase = 2, StaleRevision = 3, InvalidConfiguration = 4, InvalidTeam = 5,
                             TeamFull = 6, PlayersNotReady = 7, NotEnoughPlayers = 8, TargetNotFound = 9, ServerBusy = 10,
                             MapUnavailable = 11;
    uint32_t commandId = 0;
    uint8_t result = 0;
    uint16_t currentRevision = 0;
    std::string reason;
    void write(std::span<uint8_t> dest) const;
    static bool tryRead(std::span<const uint8_t> src, LobbyCommandResult& result);
};
// MatchLoadedPacket (the match id) and MatchLoadFailedPacket (the id and why).
constexpr int MatchLoadedSize = 2, MatchLoadFailedSize = 98;

// ServerStatusPacket: the answer to a StatusQuery, which claims no slot.
struct ServerStatus {
    static constexpr int MaxNameBytes = 32;
    static constexpr int Size = MatchState::Size + 2 + MaxNameBytes;
    static constexpr int SizeWithFlags = Size + 5;
    MatchState match;
    uint8_t maxPlayers = 0, protocol = 0, flags = 0;
    uint8_t phase = SessionState::PhaseInMatch, format = 0;
    bool lobbyEnabled = false, allowJoinInProgress = true;
    std::string name;
    void write(std::span<uint8_t> dest) const; // SizeWithFlags bytes
    static ServerStatus read(std::span<const uint8_t> src);
};

// HitClaimPacket: one hit a client resolved for its own player on somebody
// else, declared to the server so a shot its own rewind cannot find (the
// ceiling, a recovered press, a shooter killed during the round trip) is
// arbitrated rather than lost. A packet carries a count byte, then entries.
struct HitClaim {
    static constexpr int Size = 49, MaxPerPacket = 6;
    static constexpr uint8_t NoBeam = 0xFF;
    static constexpr uint8_t FlagHeadshot = 1, FlagLethal = 2, FlagFrozen = 4, FlagBurning = 8, FlagDisrupted = 16;
    uint16_t matchId = 0;
    uint64_t authorityEpoch = 0;
    uint16_t shooterGeneration = 0, shooterLifeId = 0, victimGeneration = 0, victimLifeId = 0;
    uint16_t claimId = 0;
    uint32_t frame = 0, ackFrame = 0, launchFrame = 0;
    uint8_t victimSlot = 0, beam = NoBeam;
    uint16_t damage = 0;
    uint8_t flags = 0;
    Vec3 hitPoint{};
    void write(std::span<uint8_t> dest) const;
    static HitClaim read(std::span<const uint8_t> src);
};

// HitVerdictPacket: the server's answers to a client's claims, by id.
namespace HitVerdict {
constexpr int HeaderSize = 15, EntrySize = 3, MaxPerPacket = 16;
constexpr uint8_t Applied = 0, Duplicate = 1, DeadShooter = 2, DeadVictim = 3, Refused = 4, TooOld = 5, WrongLife = 6,
                  Geometry = 7, DamageLimit = 8, InvalidLaunch = 9, NoDamage = 10;
const char* describe(uint8_t result);
} // namespace HitVerdict

// System.Guid: 32 hex digits ("N") to the 16 bytes Guid.ToByteArray gives,
// its first three fields little-endian; false for anything else.
bool parseGuid(const std::string& text, std::array<uint8_t, 16>& bytes);

// GameMode as the wire numbers it (the C# enum) and as this port does.
int wireMode(int portMode);
int portMode(int wireMode); // -1 for none of the multiplayer modes

} // namespace fp::net
