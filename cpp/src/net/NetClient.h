#pragma once

#include "NetDemo.h"
#include "NetProtocol.h"
#include "NetTransport.h"

#include <array>
#include <filesystem>
#include <memory>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace fp::net {

// NetLifecycleTracker: one slot's occupant (generation) and life, as the
// authority numbers them. A state or an intent from another occupant or an
// older life is refused.
class LifecycleTracker {
public:
    enum class State : uint8_t { Empty, WaitingToSpawn, Alive, Dead, Spectating };
    uint16_t generation() const { return m_generation; }
    uint16_t lifeId() const { return m_lifeId; }
    State state() const { return m_state; }
    void setOccupant(uint16_t generation);
    void resetLife();
    // True when accepted; `newLife` when it starts one.
    bool accept(uint16_t generation, uint16_t life, State state, bool& newLife);

private:
    uint16_t m_generation = 0, m_lifeId = 0;
    State m_state = State::Empty;
    bool m_dead = false;
};

// NetSmoothing: remote players are read off a playout clock a few frames
// behind the newest snapshot, between two snapshots, so a 60 Hz stream that
// arrives unevenly is drawn evenly. The read point travels in the intent
// (AckFrame, AckSubFrame), so the authority rewinds to exactly that world.
class Smoothing {
public:
    static constexpr int HistoryFrames = 64, MinDelay = 2, MaxDelay = 8;
    void reset();
    void record(uint32_t frame, const std::vector<PlayerState>& states);
    void tick();
    // Where `slot` is at the read point; false when the buffer cannot say.
    bool sample(int slot, const std::array<LifecycleTracker, SlotCapacity>& lives, Vec3& position, bool& altForm) const;
    bool ackPoint(uint32_t& frame, uint8_t& subFrame) const;
    int delay() const { return m_delay; }
    long long starved() const { return m_starved; }

private:
    bool lookup(int slot, uint32_t frame, const std::array<LifecycleTracker, SlotCapacity>& lives, Vec3& position, bool& alt) const;
    struct Cell {
        Vec3 position{};
        bool alt = false, live = false;
        uint16_t life = 0, generation = 0;
    };
    std::array<std::array<Cell, HistoryFrames>, SlotCapacity> m_cells{};
    std::array<uint32_t, HistoryFrames> m_stamp{};
    uint32_t m_newest = 0;
    double m_readFrame = 0;
    bool m_running = false;
    int m_delay = MinDelay, m_sinceStarved = 0, m_sinceGrew = 30;
    long long m_starved = 0;
};

// NetSession for a client of a dedicated server: joins, keeps the slot,
// follows the running match, and holds the newest word from the server on
// everything -- the roster, each slot's state and each slot's input.
class Client {
public:
    struct SlotInfo {
        bool occupied = false;
        int hunter = 0, color = 0, team = -1, ping = 0;
        bool lobbyReady = false;
        std::string name;
    };

    Client();
    ~Client();

    // Opens a socket and says hello; the answer comes through update().
    bool connect(const std::string& host, uint16_t port, const std::string& name, int hunter, int color);
    void disconnect();
    bool connected() const { return m_transport.isOpen() || m_playback != nullptr; }
    // Demos: every packet this client receives (and its own input) goes into
    // the file while recording. Playback feeds a recording back in, frame by
    // frame, with nobody's slot this client's own.
    bool startRecording(const std::filesystem::path& path);
    void stopRecording() { m_recorder.reset(); }
    bool recording() const { return m_recorder != nullptr; }
    // False (and why) when the file cannot be played.
    bool openDemo(const std::filesystem::path& path, std::string& error);
    bool playback() const { return m_playback != nullptr; }
    bool playbackEnded() const { return m_playback != nullptr && !m_playbackPending; }
    // One simulation frame: drains the socket and keeps the connection alive.
    void update();

    // Admission
    int localSlot() const { return m_localSlot; }
    bool refused() const { return m_refused.has_value(); }
    std::string refusedReason() const;
    bool connectionLost() const { return m_connectionLost; }
    const std::string& serverName() const { return m_serverDescription; }

    // The running match
    const std::optional<MatchState>& match() const { return m_match; }
    uint16_t matchId() const { return m_match ? m_match->matchId : 0; }
    uint64_t authorityEpoch() const { return m_match ? m_match->authorityEpoch : 0; }
    // Match state packets received so far (the clock is re-read from each).
    unsigned matchStateCount() const { return m_matchStateCount; }
    int entityLayerPlayers() const { return m_entityLayerPlayers; }
    const std::array<SlotInfo, SlotCapacity>& slotInfo() const { return m_slots; }
    uint32_t rosterRevision() const { return m_rosterRevision; }

    // Snapshots
    uint32_t frame() const { return m_frame; }
    bool hasSnapshot() const { return m_hasSnapshot; }
    uint32_t lastSnapshotFrame() const { return m_lastSnapshotFrame; }
    uint32_t snapshotAge() const { return m_snapshotArrived == 0 ? 0xFFFFFFFF : m_frame - m_snapshotArrived; }
    const PlayerState* state(int slot) const { return m_stateValid[slot] ? &m_states[slot] : nullptr; }
    const std::array<float, SlotCapacity>& slotTime() const { return m_time; }
    const std::array<float, SlotCapacity>& teamTime() const { return m_teamTime; }
    const std::vector<HealthSpawnState>& healthSpawns() const { return m_healthSpawns; }
    uint32_t rng1() const { return m_rng1; }
    uint32_t rng2() const { return m_rng2; }
    long long snapshotsReceived() const { return m_snapshotsReceived; }
    // The snapshot frame the game has applied, for the intent's ack.
    void noteStatesApplied() { m_appliedSnapshotFrame = m_lastSnapshotFrame; }
    uint32_t appliedSnapshotFrame() const { return m_appliedSnapshotFrame; }

    // Lifecycle
    const LifecycleTracker& life(int slot) const { return m_lives[slot]; }
    const std::array<LifecycleTracker, SlotCapacity>& lives() const { return m_lives; }
    bool lifeMatches(int slot, uint16_t generation, uint16_t life) const
    {
        return generation != 0 && m_lives[slot].generation() == generation && m_lives[slot].lifeId() == life;
    }

    // Other players' input, relayed by the server.
    const Intent* intent(int slot) const { return m_intentValid[slot] ? &m_intents[slot] : nullptr; }
    uint32_t intentAge(int slot) const { return m_intentArrived[slot] == 0 ? 0xFFFFFFFF : m_frame - m_intentArrived[slot]; }

    // Smoothing: the playout clock, ticked once per simulation frame.
    Smoothing& smoothing() { return m_smoothing; }
    const Smoothing& smoothing() const { return m_smoothing; }

    // This player's input for the frame: stamped with the stream, the slot's
    // identity, the frame and the ack before it goes.
    void sendIntent(Intent intent);
    void sendChat(const std::string& text);
    // NetHitClaims: a HitClaim payload (a count, then the claims), and the verdicts that come back.
    void sendHitClaims(std::span<const uint8_t> payload) { send(PacketType::HitClaim, payload); }
    std::vector<std::vector<uint8_t>> takeVerdicts() { return std::exchange(m_verdicts, {}); }
    void sendIdentify();
    void setIdentity(int hunter, int color);
    // The lobby owner token a hosted game's server was started with, sent with every Hello.
    void setOwnerToken(const std::array<uint8_t, 16>& token) { m_ownerToken = token; }

    // What happened since the last call: chat lines, and the server's own messages.
    std::vector<Chat> takeChat() { return std::exchange(m_chat, {}); }
    // Set when a new match (or a new authority) begins, until taken.
    bool takeNewMatch() { return std::exchange(m_newMatch, false); }
    // Set when a slot's occupant changed, per slot, until taken.
    bool takeSlotChanged(int slot) { return std::exchange(m_slotChanged[slot], false); }

    // The session (SessionStatePacket): its phase, its policy and the match it
    // plays or is setting up. Absent before the server has said.
    const std::optional<SessionState>& session() const { return m_session; }
    bool persistentLobby() const { return m_session && m_session->policy == SessionState::PolicyLobby; }
    bool inLobby() const { return m_session && m_session->phase == SessionState::PhaseLobby; }
    bool lobbyOwner() const { return m_session && m_localSlot >= 0 && m_session->ownerSlot == m_localSlot; }
    // NetSession.ShouldLoadMatch: the room is loaded when a match is on, or starting with this player in it.
    bool shouldLoadMatch() const;
    // LobbyCommandPacket, repeated until answered; one at a time. The configuration is the session's own when null.
    bool sendLobbyCommand(uint8_t type, uint8_t targetSlot = 0xFF, int8_t team = -1, bool ready = false,
        const SessionState* configuration = nullptr);
    bool lobbyCommandPending() const { return m_pendingLobby.has_value(); }
    const std::string& lobbyMessage() const { return m_lobbyMessage; }
    // The room of a starting match is loaded here (MatchLoadedPacket), or could not be.
    void markMatchLoaded();
    void reportMatchLoadFailed(const std::string& reason);

    // Votes: the mid-match proposal (VotePacket, VoteStatePacket) and the
    // results screen's ballot (MapChoicesPacket, MapPickPacket).
    const std::optional<VoteState>& vote() const { return m_vote; }
    bool voteAnswered() const { return m_voteAnswered; }
    void sendVote(uint8_t kind, const std::string& roomKey = {});
    const std::optional<MapChoices>& mapChoices() const { return m_mapChoices; }
    const std::string& mapPick() const { return m_mapPick; }
    void sendMapPick(const std::string& roomKey);

    // Counters for the log.
    long long intentsSent() const { return m_intentsSent; }
    long long outOfOrder() const { return m_outOfOrder; }
    long long refusedStates() const { return m_refusedStates; }

private:
    void handle(const ReceivedPacket& packet);
    void handleWelcome(std::span<const uint8_t> p);
    void handleRoster(std::span<const uint8_t> p);
    void handleMatchState(std::span<const uint8_t> p);
    void handleSnapshot(std::span<const uint8_t> p);
    void handleSlotIntent(std::span<const uint8_t> p);
    void applyMatchState(const MatchState& state);
    void applySessionState(const SessionState& state);
    void pumpLobby(double now);
    void sendHello();
    bool matchesStream(uint16_t match, uint64_t epoch) const
    {
        return match != 0 && match == matchId() && epoch != 0 && epoch == authorityEpoch();
    }
    void setOccupant(int slot, uint16_t generation);
    void forgetSlot(int slot);
    bool acceptState(const PlayerState& state);
    double clock() const;
    void send(PacketType type, std::span<const uint8_t> payload)
    {
        if (m_playback == nullptr) {
            m_transport.send(m_server, static_cast<uint8_t>(type), payload);
        }
    }
    void pumpPlayback();
    void record(std::span<const uint8_t> packet);

    Transport m_transport;
    Endpoint m_server;
    std::string m_serverDescription;
    std::string m_name;
    int m_hunter = 0, m_color = 0;
    uint32_t m_clientId = 1;
    int m_localSlot = -1;
    std::optional<Refused> m_refused;
    bool m_connectionLost = false, m_reAnnounced = false;
    double m_lastServerPacket = 0;
    uint32_t m_frame = 0;

    std::optional<MatchState> m_match;
    unsigned m_matchStateCount = 0;
    int m_entityLayerPlayers = 0;
    bool m_newMatch = false;
    std::array<SlotInfo, SlotCapacity> m_slots{};
    std::array<bool, SlotCapacity> m_slotChanged{};
    bool m_hasRoster = false;
    uint32_t m_rosterRevision = 0;
    std::array<LifecycleTracker, SlotCapacity> m_lives{};

    bool m_hasSnapshot = false;
    uint32_t m_lastSnapshotFrame = 0, m_snapshotArrived = 0, m_appliedSnapshotFrame = 0;
    std::array<PlayerState, SlotCapacity> m_states{};
    std::array<bool, SlotCapacity> m_stateValid{};
    std::array<float, SlotCapacity> m_time{}, m_teamTime{};
    std::vector<HealthSpawnState> m_healthSpawns;
    uint32_t m_rng1 = 0, m_rng2 = 0;
    long long m_snapshotsReceived = 0, m_outOfOrder = 0, m_refusedStates = 0, m_intentsSent = 0;

    std::array<Intent, SlotCapacity> m_intents{};
    std::array<bool, SlotCapacity> m_intentValid{};
    std::array<uint32_t, SlotCapacity> m_intentArrived{};
    std::array<uint32_t, SlotCapacity> m_lastIntentFrame{};

    Smoothing m_smoothing;
    std::vector<Chat> m_chat;
    std::vector<std::vector<uint8_t>> m_verdicts;
    // The session and the lobby
    std::optional<SessionState> m_session;
    std::array<uint8_t, 16> m_ownerToken{};
    struct PendingLobby {
        LobbyCommand command;
        double sentAt = 0;
        int attempts = 0;
    };
    std::optional<PendingLobby> m_pendingLobby;
    uint32_t m_nextCommandId = 0;
    std::string m_lobbyMessage;
    uint16_t m_loadedMatch = 0;
    double m_lastLoadAck = 0, m_lastIdentity = 0;
    // Votes
    std::unique_ptr<DemoWriter> m_recorder;
    uint32_t m_recordStart = 0;
    std::unique_ptr<DemoReader> m_playback;
    std::optional<DemoRecord> m_playbackPending;
    uint32_t m_playbackFrame = 0;
    std::optional<VoteState> m_vote;
    bool m_voteAnswered = false;
    std::optional<MapChoices> m_mapChoices;
    std::string m_mapPick;
    double m_lastPickSent = 0;
};

// NetStatus.Query: what a server is running and how many play there, with
// the round trip, without joining it. nullopt when it does not answer.
std::optional<ServerStatus> queryStatus(const std::string& host, uint16_t port, int timeoutMs, int& latencyMs);

} // namespace fp::net
