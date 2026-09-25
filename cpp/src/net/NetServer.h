#pragma once

#include "NetProtocol.h"
#include "NetTransport.h"

#include <array>
#include <atomic>
#include <map>
#include <memory>
#include <optional>
#include <deque>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace fp::net {

// MapRotation's RotationEntry: one match of the rotation.
struct RotationEntry {
    std::string roomKey = "MP3 PROVING GROUND";
    int mode = 0; // this port's GameMode
    float timeLimit = 7 * 60; // seconds, 0 for none
    int pointGoal = 7;
    int teamCount = 2; // in a team mode: the format's (TeamLayout.TeamCount)
};

// MapRotation: the matches a server plays in turn. The file is the C#'s:
// one match a line, "ROOM KEY | mode | minutes | points", '#' a comment.
class Rotation {
public:
    static Rotation single(RotationEntry entry);
    // False (with the reason) when the file cannot be read or has no match.
    static bool load(const std::string& path, Rotation& rotation, std::string& error);
    const RotationEntry& current() const { return m_override ? *m_override : m_entries[m_index]; }
    // A map the room voted in jumps the queue; the cycle resumes after it.
    const RotationEntry& next() const { return m_pending ? *m_pending : m_entries[(m_index + 1) % m_entries.size()]; }
    const RotationEntry& advance();
    size_t size() const { return m_entries.size(); }
    const std::vector<RotationEntry>& entries() const { return m_entries; }
    // MapRotation.PlayNext: `roomKey` next, in `mode` (-1: the current one's),
    // to the current match's time limit and points. ClearPending takes it back.
    void playNext(const std::string& roomKey, int mode);
    void clearPending() { m_pending.reset(); }
    // The mode the rotation plays `roomKey` in, else `fallback`.
    int modeFor(const std::string& roomKey, int fallback) const;

private:
    std::vector<RotationEntry> m_entries{RotationEntry{}};
    size_t m_index = 0;
    std::optional<RotationEntry> m_pending, m_override;
};

// Who holds a slot, as the simulation needs it.
struct Occupant {
    bool occupied = false;
    int hunter = 0, color = 0, team = -1;
    uint16_t generation = 0;
    std::string name;
};

// The match a server runs (ServerSim): the engine, stepped at 60 Hz, fed the
// clients' intents and composing the snapshot everybody is sent.
class Simulation {
public:
    virtual ~Simulation() = default;
    // A new match on `entry`: loads its room unless it is the one already loaded.
    virtual bool start(const RotationEntry& entry, bool friendlyFire, int layerPlayers) = 0;
    virtual void setOccupants(const std::array<Occupant, SlotCapacity>& occupants) = 0;
    // The life the slot's player is on (0: not spawned yet); an intent from another is refused.
    virtual uint16_t lifeId(int slot) const = 0;
    virtual void intent(int slot, const Intent& intent) = 0;
    // One step; the snapshot it ends on goes into `out` (the payload), its length returned.
    virtual size_t step(uint32_t frame, uint16_t matchId, uint64_t epoch, std::span<uint8_t> out) = 0;
    // The match reached its end (a score, the clock, the last survivor).
    virtual bool over() const = 0;
    // Seconds left on the match clock, negative for none.
    virtual float timeRemaining() const = 0;
    virtual std::string describe() const = 0;
    // NetHitClaims, as the arbiter: a client's HitClaim payload, and the
    // verdicts owed to each slot (id, result), taken once per step.
    virtual void hitClaims(int /*slot*/, std::span<const uint8_t> /*payload*/) {}
    virtual std::vector<std::pair<uint16_t, uint8_t>> takeVerdicts(int /*slot*/) { return {}; }
    // The round trip a slot's client measures, in milliseconds.
    virtual void setPing(int /*slot*/, int /*ms*/) {}
};

// DedicatedServer: a headless server that runs the match itself. Clients
// join, say who they are and send their input every frame; the simulation
// decides everything and a snapshot of it goes to everybody 60 times a
// second. Continuous sessions only (no lobby): the rotation plays on, one
// match after another, and players come and go as they like.
class Server {
public:
    struct Config {
        uint16_t port = DefaultPort;
        int maxPlayers = 4;
        std::string name = "Fruity Prime";
        bool friendlyFire = false;
        Rotation rotation;
        // SessionPolicy.Lobby: players gather in a lobby, its owner sets the
        // match up and starts it, and the lobby comes back after each one.
        bool lobby = false;
        uint8_t format = SessionState::FormatAuto; // MatchFormat
        bool requireReady = true, allowJoinInProgress = true;
        bool allowMapVotes = true;
        // OwnerToken: the first client to present it owns the lobby (a hosted game's host).
        std::array<uint8_t, 16> ownerToken{};
        // MasterReporter: where the heartbeat goes; empty for nowhere.
        std::string masterHost;
        uint16_t masterPort = 27889;
        // HostPool: extra matches opened here for players who cannot open a port (0: none).
        uint16_t hostFirst = 0, hostLast = 0;
        std::string gameFiles;
        // A game started for somebody else: it closes once empty for this long
        // (never before `startupGrace` has passed with nobody), 0 for never.
        double exitWhenEmpty = 0, startupGrace = 180;
    };

    Server(Config config, std::unique_ptr<Simulation> simulation);
    ~Server();
    // Until stop() (or `stop` turns true); false when the socket or the first room would not open.
    bool run(const std::atomic<bool>* stop = nullptr);
    void stop() { m_running = false; }

private:
    struct Peer {
        Endpoint endpoint;
        int slot = -1;
        double lastSeen = 0;
        uint32_t lastIntentFrame = 0;
        std::string name;
        uint8_t hunter = 0, color = 0;
        int ping = 0;
        double pingSentAt = 0;
        uint8_t pingId = 0;
        bool pingPending = false;
        double chatCredit = 3, chatCreditAt = 0;
        int chatDropped = 0;
        bool postMatchReady = false;
        int8_t team = -1;
        uint32_t clientId = 0;
        // Votes: a mid-match ballot (VotePacket kind), the results screen's pick, when it last proposed.
        uint8_t ballot = 0;
        std::string pick;
        double lastProposal = -1e9;
        // The lobby: ready, and the answers to the commands it sent (a resend is answered the same).
        bool lobbyReady = false;
        std::map<uint32_t, LobbyCommandResult> commands;
        std::deque<uint32_t> commandOrder;
    };

    void handle(const ReceivedPacket& packet, double now);
    void handleHello(const Endpoint& from, std::span<const uint8_t> p, double now);
    void handleIdentify(Peer& peer, std::span<const uint8_t> p);
    void handleIntent(Peer& peer, std::span<const uint8_t> p);
    void handleChat(Peer& peer, std::span<const uint8_t> p, double now);
    void handlePong(Peer& peer, std::span<const uint8_t> p, double now);
    // Votes (NetServerLobby.cpp): the mid-match proposal and the results screen's ballot.
    void handleVote(Peer& peer, std::span<const uint8_t> p, double now);
    void handleMapPick(Peer& peer, std::span<const uint8_t> p);
    void startVote(Peer& peer, const std::string& roomKey, double now);
    void tally(double now);
    void resolveVote(double now, bool passed, const std::string& count);
    void reviewVote(double now);
    void countVotes(int& yes, int& no, int& eligible, int& needed) const;
    void broadcastVoteState(double now);
    void openBallot();
    void closeBallot();
    void recount();
    void applyLeader();
    void reviewPicks();
    void broadcastMapChoices();
    void tell(const Peer& peer, const std::string& text);
    // The lobby (NetServerLobby.cpp).
    SessionState currentDefinition() const;
    SessionState definitionFor(const RotationEntry& entry) const;
    TeamLayout currentLayout() const;
    void handleLobbyCommand(Peer& peer, std::span<const uint8_t> p, double now);
    uint8_t executeLobbyCommand(Peer& peer, const LobbyCommand& command, std::string& reason, double now);
    std::string validateStart(const SessionState& match, bool requireReady, uint8_t& code) const;
    void handleMatchLoaded(Peer& peer, std::span<const uint8_t> p, double now);
    void checkLoadBarrier(double now);
    void returnToLobby(double now);
    void lobbyPeerRemoved(const Peer& peer, double now);
    void claimOwner(Peer& peer, std::span<const uint8_t> hello);
    void setPhase(uint8_t phase);
    void invalidateLobbyReady();
    void sendStatus(const Endpoint& to, double now);
    void sendRefusal(const Endpoint& to, uint8_t reason);
    void remove(size_t index, const std::string& reason);
    void dropTimedOut(double now);
    Peer* find(const Endpoint& endpoint);
    bool slotFree(int slot) const;
    int nextFreeSlot() const;
    int8_t chooseTeam(const SessionState& match, const Peer* exclude) const;
    void normalizeTeams();
    bool teamMode() const;

    bool startMatch(double now, const RotationEntry& entry);
    void simulate(double now);
    void sendVerdicts();
    void endMatch(double now, const char* reason);
    double endSequence() const;
    bool advanceMap(double now);
    void touchSession();

    MatchState buildMatchState(double now) const;
    SessionState buildSessionState() const;
    Roster buildRoster();
    void broadcastMatchState(double now, PacketType type = PacketType::MatchState);
    void broadcastSessionState();
    void broadcastRoster();
    void pingPeers(double now);
    void announce(const std::string& text);
    void syncOccupants();
    void sendAll(PacketType type, std::span<const uint8_t> payload, const Peer* except = nullptr);
    void send(const Endpoint& to, PacketType type, std::span<const uint8_t> payload) { m_transport.send(to, static_cast<uint8_t>(type), payload); }
    int layerPlayers() const;
    RotationEntry entryFor(const SessionState& definition) const;
    void report(double now);

    Config m_config;
    std::unique_ptr<Simulation> m_sim;
    std::unique_ptr<class MasterReporter> m_reporter;
    std::unique_ptr<class HostPool> m_hosts;
    double m_lastOccupied = 0;
    bool m_everOccupied = false;
    Transport m_transport;
    std::atomic<bool> m_running{false};
    std::vector<Peer> m_peers;
    std::array<uint16_t, SlotCapacity> m_generations{};
    uint16_t m_matchId = 1, m_sessionRevision = 1;
    uint64_t m_epoch = 0;
    uint32_t m_rosterRevision = 0;
    uint8_t m_phase = SessionState::PhaseInMatch;
    double m_matchStarted = 0, m_matchEndedAt = -1;
    bool m_simStarted = false;
    // The fixed-step clock (ServerSim.Advance).
    double m_lastAdvance = -1, m_accumulator = 0;
    uint32_t m_frame = 0;
    std::vector<uint8_t> m_lastSnapshot;
    // Votes
    bool m_voteRunning = false;
    std::string m_voteRoom, m_voteProposer;
    int m_voteMode = 0;
    double m_voteStartedAt = 0, m_voteResolvedAt = -1e9;
    uint8_t m_voteResult = VoteState::StateIdle;
    bool m_ballotOpen = false;
    std::vector<std::pair<std::string, int>> m_tally;
    // The lobby
    SessionState m_lobbyMatch, m_frozenMatch;
    bool m_lockTeams = false;
    uint32_t m_ownerClientId = 0;
    uint8_t m_expectedLoaded = 0, m_loaded = 0;
    double m_startDeadline = 0;
    double m_now = 0;
    // For the report: steps, their cost, the worst one.
    long long m_steps = 0, m_overruns = 0, m_dropped = 0;
    double m_stepSeconds = 0, m_worstStep = 0;
};

} // namespace fp::net
