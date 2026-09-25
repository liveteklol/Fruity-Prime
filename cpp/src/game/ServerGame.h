#pragma once

#include "MatchRoom.h"
#include "net/NetServer.h"

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace fp {

// ServerSim: the match a dedicated server runs, in the engine itself. Every
// player is a client's input -- where it says it is, where it aims, what it
// presses -- and the engine decides the rest: spawns, hits, deaths, scores,
// the end of the match. What it decided goes out in a snapshot every step,
// with each player's newest hits (NetDamage) for the clients to replay.
//
// Shots are resolved in the world their shooter saw (NetUnlagged): the other
// players are put back where they were at the frame the shooter's intent
// says it was looking at, for the moment the shot spawns and for the steps
// it has to catch up.
class ServerGame final : public net::Simulation, public NetDamageHooks {
public:
    explicit ServerGame(std::filesystem::path root);
    ~ServerGame() override;

    bool start(const net::RotationEntry& entry, bool friendlyFire, int layerPlayers) override;
    void setOccupants(const std::array<net::Occupant, net::SlotCapacity>& occupants) override;
    uint16_t lifeId(int slot) const override { return slot >= 0 && slot < net::SlotCapacity ? m_slots[slot].life : 0; }
    void intent(int slot, const net::Intent& intent) override;
    size_t step(uint32_t frame, uint16_t matchId, uint64_t epoch, std::span<uint8_t> out) override;
    bool over() const override;
    float timeRemaining() const override;
    std::string describe() const override;
    // NetHitClaims, the arbiter's half (HitArbiter.cpp).
    void hitClaims(int slot, std::span<const uint8_t> payload) override;
    std::vector<std::pair<uint16_t, uint8_t>> takeVerdicts(int slot) override;
    void setPing(int slot, int ms) override
    {
        if (slot >= 0 && slot < net::SlotCapacity) {
            m_ping[slot] = ms;
        }
    }

    // NetDamageHooks: nothing is predicted on the machine that runs the match;
    // it only refuses its own copy of a shot a claim already made real.
    bool predicts(const Player&, const DamageSource&, uint32_t) override { return false; }
    void noteHit(Player&, uint32_t&, int&, const DamageSource&) override {}
    bool suppress(const Player& victim, const DamageSource& source) override;
    void noteDrain(const Player&, int) override {}

private:
    // One server slot: its occupant, its world player, its input and its hits.
    struct Slot {
        net::Occupant occupant;
        int worldSlot = -1;
        uint16_t life = 0;
        int spawnCount = 0; // the world player's, when the life was numbered
        uint32_t spawnFrame = 0;
        bool aimHeld = false; // NetPlayerBridge._aimHeld: a new life's aim is the spawn's until the owner has seen it
        // The newest intent of the current life.
        bool hasIntent = false;
        net::Intent intent;
        uint32_t intentFrame = 0; // the server frame it arrived on
        bool pressSeen = false;
        uint32_t lastPressFrame = 0;
        // NetPlayerBridge.NoteReportedVelocity
        bool reportSeen = false;
        Vec3 lastReport{};
        uint32_t lastReportFrame = 0;
        Vec3 reportedSpeed{};
        // FormReconciliation
        uint32_t mismatchSince = 0, attemptSince = 0;
        bool mismatching = false, attempted = false;
        // NetDamage
        uint16_t damageSequence = 0;
        std::array<net::DamageEvent, net::PlayerState::DamageHistory> damage{};
    };
    // NetUnlagged's history: where everybody was, by snapshot frame.
    static constexpr int HistoryFrames = 128, MaxRewindFrames = 45;
    struct HistoryCell {
        Vec3 position{};
        bool alt = false, inPlay = false;
        uint16_t life = 0, generation = 0;
    };

    Player* worldPlayer(int serverSlot);
    int serverSlotOf(const Player& player) const;
    void bind(int serverSlot);
    void release(int serverSlot);
    void syncSlots();
    bool intentCurrent(const Slot& s) const;
    PlayerInput puppetInput(int serverSlot);
    void applyIntentState(int serverSlot);
    void placeReported(int serverSlot, bool noteVelocity);
    int reconcileForm(Slot& s, bool desiredAlt, const Player& p);
    void noteDamage(Player& victim, const DamageSource& source, uint32_t flags);
    void numberLives();
    size_t composeSnapshot(uint32_t frame, uint16_t matchId, uint64_t epoch, std::span<uint8_t> out);
    // NetUnlagged
    void record(uint32_t frame);
    bool reconcile(int exceptSlot, double targetFrame);
    void restore();
    void beginShot(Player& shooter);
    void endShot(Player& shooter);

    // NetHitClaims (HitArbiter.cpp)
    struct PendingClaim {
        net::HitClaim claim;
        uint8_t shooter = 0;
        uint32_t arrived = 0;
        int grace = 0;
        bool live = false;
    };
    struct LedgerEntry {
        uint32_t at = 0, ack = 0, launch = 0;
        int damage = 0;
        bool used = false;
    };
    struct Rescued {
        uint8_t attacker = 0, victim = 0;
        uint32_t launch = 0, at = 0;
        uint16_t victimGeneration = 0, victimLife = 0;
        int owed = 0;
    };
    static constexpr int LedgerDepth = 8, SeenCapacity = 128, RescuedCapacity = 32;
    static constexpr uint8_t ResultPending = 255;
    bool lifeMatches(int slot, uint16_t generation, uint16_t life) const
    {
        return slot >= 0 && slot < net::SlotCapacity && generation != 0 && m_slots[slot].occupant.generation == generation
            && m_slots[slot].life == life;
    }
    bool inPlay(int slot);
    bool positionAt(int slot, uint32_t frame, uint16_t generation, uint16_t life, Vec3& position) const;
    uint32_t fireFrameOf(int slot) const;
    void noteAuthorityHit(int attacker, int victim, uint32_t launchFrame, int damage);
    bool takeLedger(int attacker, int victim, uint32_t ack, uint32_t launch, uint32_t arrived, int window, int& damage);
    void clearLedger(int attacker, int victim);
    bool seen(int slot, uint16_t id) const;
    void remember(int slot, uint16_t id, uint8_t result);
    void answer(int slot, uint16_t id, uint8_t result, bool rememberIt = true);
    uint8_t judge(int shooter, const net::HitClaim& claim);
    void trackDeaths();
    void applyClaim(PendingClaim& entry);
    void tickClaims();
    void forgetClaims(int slot);
    void resetClaims();
    std::array<PendingClaim, 64> m_claims{};
    std::array<std::array<std::array<LedgerEntry, LedgerDepth>, net::SlotCapacity>, net::SlotCapacity> m_ledger{};
    std::array<std::array<int, net::SlotCapacity>, net::SlotCapacity> m_ledgerHead{};
    std::array<std::array<uint16_t, SeenCapacity>, net::SlotCapacity> m_seenIds{};
    std::array<std::array<uint8_t, SeenCapacity>, net::SlotCapacity> m_seenResults{};
    std::array<uint16_t, net::SlotCapacity> m_newestId{};
    std::array<uint32_t, net::SlotCapacity> m_deathFire{}, m_lastHitFire{};
    std::array<bool, net::SlotCapacity> m_dead{}, m_wasInPlay{};
    std::array<Rescued, RescuedCapacity> m_rescued{};
    int m_rescuedHead = 0;
    std::array<std::vector<std::pair<uint16_t, uint8_t>>, net::SlotCapacity> m_verdicts;
    std::array<int, net::SlotCapacity> m_ping{};
    bool m_applyingClaim = false;
    uint32_t m_applyingAck = 0, m_applyingLaunch = 0;
    uint16_t m_matchId = 0;
    uint64_t m_epoch = 0;
    long long m_claimsReceived = 0, m_claimsApplied = 0, m_claimsDuplicate = 0, m_claimsDeadShooter = 0, m_claimsDeadVictim = 0,
              m_claimsRefused = 0, m_claimsTooOld = 0, m_claimsRepeats = 0, m_rescuedDamage = 0, m_rescuedKills = 0,
              m_rescuedHeadshots = 0, m_suppressed = 0;

    std::filesystem::path m_root;
    std::optional<MatchRoom> m_room;
    std::string m_roomKey;
    int m_mode = -1, m_layerPlayers = 0;
    std::array<Slot, net::SlotCapacity> m_slots{};
    std::vector<int> m_freeWorldSlots;
    std::vector<int> m_serverSlotOfWorld; // by world slot, -1 for none
    std::vector<PlayerInput> m_input;     // by world slot, this step's
    uint32_t m_frame = 0;
    // NetUnlagged
    std::array<std::array<HistoryCell, HistoryFrames>, net::SlotCapacity> m_history{};
    std::array<uint32_t, HistoryFrames> m_stamp{};
    std::array<Vec3, net::SlotCapacity> m_restore{};
    std::array<bool, net::SlotCapacity> m_moved{};
    bool m_reconciled = false, m_shotInProgress = false;
    int m_shooter = -1, m_rewind = 0;
    std::array<bool, 16> m_beamsBefore{};
    long long m_shotsCompensated = 0, m_framesRewound = 0, m_historyMisses = 0, m_catchUpHits = 0;
    int m_worstRewind = 0;
};

} // namespace fp
