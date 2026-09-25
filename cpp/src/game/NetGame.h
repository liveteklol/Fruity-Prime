#pragma once

#include "NetPrediction.h"
#include "Player.h"
#include "formats/Rooms.h"
#include "net/NetClient.h"

#include <array>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace fp {

class World;
struct Scene;

// A match played on a server (the C#'s NetHooks, NetPlayerBridge and
// NetDamage.Replay, for a client of a dedicated server that runs the match).
//
// The server is the authority. This machine moves its own player and says
// where it is; everything else it is told: when anybody spawns or dies, their
// health and score, and where the other players are -- drawn off a playout
// clock a few frames behind the newest snapshot. Their relayed input only
// makes them do what a position cannot say: fire, morph, bomb, lunge. Damage
// resolved here hurts nobody; the authority's hits are replayed from the
// snapshots, which is what plays the flinch, the knockback, the arrows and
// the kill messages.
//
// World slot 0 is always this machine's player, whichever slot the server
// gave it; the others are bound to the server's slots as the roster fills them.
class NetGame {
public:
    explicit NetGame(std::unique_ptr<net::Client> client);
    ~NetGame();

    net::Client& client() { return *m_client; }
    // The room the server is playing (its key) and the mode, in this port's numbering.
    std::string serverRoom() const;
    GameMode serverMode() const;
    // The world for the room being played; call again after a room change.
    void attach(World& world, const std::string& roomKey);
    // Loads every hunter's models into `scene`, so a player joining with any
    // of them needs nothing the renderer does not already hold.
    static void preloadModels(Scene& scene, const std::filesystem::path& root);

    // One 60 Hz tick in place of World::tick: the network, then the world.
    void tick(const PlayerInput& input, bool walking);
    // The server moved to another room: the caller rebuilds the scene and the world, then attach()es.
    bool roomChanged() const;
    // Between matches in a persistent lobby: nothing plays until the owner starts one.
    bool waitingInLobby() const;
    // A mid-match map vote's two HUD lines, empty when none is running.
    std::vector<std::string> voteLines() const;
    // F1 / F2: this player's answer to the vote.
    void answerVote(bool yes) { m_client->sendVote(yes ? net::Vote::KindYes : net::Vote::KindNo); }

    // The status line (connection, slot, ping) for the log or the HUD.
    std::string status() const;
    // For the HUD: the chat lines of the last ten seconds, and what is wrong with the connection.
    std::vector<std::string> recentChat() const;
    // A line this player typed: to the server, which passes it on, and shown here at once.
    void say(const std::string& text);
    std::string trouble() const;
    // A recorded match played back: nobody is this machine's player.
    bool playback() const { return m_client->playback(); }
    // F9: this match into a demo file, or stop.
    bool toggleRecording(const std::filesystem::path& directory);
    bool recording() const { return m_client->recording(); }
    // The hit prediction and claims tally, for the log.
    std::string predictionReport() const { return m_prediction->describe(); }

private:
    struct Remote {
        int worldSlot = -1; // -1: not bound
        int hunter = -1;
        // NetPlayerBridge / NetDamage bookkeeping for this server slot.
        bool lifeApplied = false;
        uint16_t appliedLife = 0;
        bool damageSeen = false;
        uint16_t damageLife = 0, damageGeneration = 0, damageLastSeen = 0;
        bool pressSeen = false;
        uint32_t lastPressFrame = 0;
        // FormReconciliation
        uint32_t mismatchSince = 0, attemptSince = 0, transitionSince = 0, transitionLastSeen = 0;
        bool mismatching = false, attempted = false, transitionSeen = false, transitionActive = false, transitionTarget = false;
    };

    void syncSlots();
    void bindSlot(int serverSlot, int hunter);
    void releaseSlot(int serverSlot);
    Player* worldPlayer(int serverSlot);
    int worldSlotOf(int serverSlot) const { return serverSlot == m_client->localSlot() ? 0 : m_remotes[serverSlot].worldSlot; }
    void applyStates();
    void applyState(int serverSlot, const net::PlayerState& state);
    void beginLife(int serverSlot, const net::PlayerState& state);
    void replayDamage(int serverSlot, const net::PlayerState& state);
    void netDie(Player& player, Player* attacker);
    void pinPuppets();
    PlayerInput puppetInput(int serverSlot);
    int reconcileForm(Remote& r, bool desiredAlt, const Player& p, int ping);
    void applyMatchClock();
    void applyHealthSpawns();
    void recordLocalPresses(const PlayerInput& input);
    void sendLocalIntent(const PlayerInput& input);
    void resetTrackers(int serverSlot);

    int serverSlotOfPlayer(const Player* player) const;
    // NetPlayerBridge.Diverged: the authority's copy of this machine's player
    // is somewhere the round trip cannot explain, for a second on end.
    bool diverged(const Player& player, const net::PlayerState& state);
    std::array<Vec3, 128> m_ownPositions{};
    std::array<uint32_t, 128> m_ownFrames{};
    int m_divergedFrames = 0;

    std::unique_ptr<net::Client> m_client;
    std::unique_ptr<HitPrediction> m_prediction;
    World* m_world = nullptr;
    std::string m_roomKey;
    std::array<Remote, net::SlotCapacity> m_remotes{};
    std::vector<int> m_freeWorldSlots; // bound to nobody, kept for a hunter coming back
    std::vector<PlayerInput> m_puppetInput; // by world slot
    unsigned m_lastMatchState = 0;
    // The local player's presses, newest first, and the charge latched at a release.
    std::array<uint32_t, net::Intent::PressHistory> m_pressHistory{};
    uint32_t m_heldLast = 0;
    bool m_boostHeldLast = false, m_shootHeldLast = false;
    bool m_hasLatch = false;
    int m_latchedCharge = 0, m_latchedBoost = 0;
    long long m_ticks = 0;
    bool m_placed = false;
    struct ChatLine {
        std::string text;
        long long tick;
    };
    std::vector<ChatLine> m_chat;
};

} // namespace fp
