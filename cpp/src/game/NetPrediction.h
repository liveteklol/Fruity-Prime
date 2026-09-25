#pragma once

#include "Player.h"
#include "net/NetClient.h"

#include <array>
#include <functional>
#include <span>
#include <string>

namespace fp {

// NetHitPrediction and the client half of NetHitClaims, for a client of a
// server that runs the match.
//
// This machine's own shots on somebody else are resolved here, the frame they
// land: the flinch, the knockback, the mark over the crosshair and the health
// bar do not wait a round trip for the server. Each such hit is declared to
// the server (a hit claim), which answers it by id; the snapshot's damage
// history confirms the rest. Until the authority accounts for a hit, the
// victim's health is held at the authority's number less what is still
// outstanding. A prediction never kills somebody else (a killing blow is
// clamped to leave them standing on one point, and the authority's word puts
// them down); this machine's own fall, splash and recoil kill at once, since
// the server runs the same arithmetic on the same inputs.
class HitPrediction final : public NetDamageHooks {
public:
    static constexpr int PendingFrames = 120, PendingCapacity = 24, HealCapacity = 48;
    // The server slot `player` stands for, -1 for none.
    using SlotOf = std::function<int(const Player* player)>;
    HitPrediction(net::Client& client, SlotOf slotOf);

    // NetDamageHooks
    bool predicts(const Player& victim, const DamageSource& source, uint32_t flags) override;
    void noteHit(Player& victim, uint32_t& flags, int& damage, const DamageSource& source) override;
    bool suppress(const Player&, const DamageSource&) override { return false; }
    void noteDrain(const Player& healer, int gained) override;

    // NetDamage.Replay: the authority credits this machine's player with
    // `landed` hits on `slot`; true when they retire predictions (already shown).
    bool confirm(int slot, int landed, bool authorityHeadshot);
    // What ApplyState assigns: the authority's health less the outstanding
    // predictions (and never a rise the snapshot is not itself reporting).
    int healthFor(int slot, int authorityHealth);
    // This machine's player: the authority's number plus the Shock Coil's
    // outstanding drain, less its own outstanding self-damage.
    int localHealthFor(const Player& player, int authorityHealth);
    bool heldDead(int slot);
    void noteRespawn(int slot);
    void noteDeath(int slot);
    void forgetSlot(int slot);
    // The slot changed hands: its claims go too.
    void forgetClaims(int slot);
    // A new room or a new match: everything.
    void forgetPending();
    // Once per simulation frame: the pending hits and the claims age.
    void tick();
    // The claims due this frame, as a HitClaim payload; 0 bytes when none.
    size_t composeClaims(std::span<uint8_t> dest);
    void applyVerdicts(std::span<const uint8_t> payload);
    std::string describe() const;

private:
    struct Pending {
        uint32_t frame = 0;
        int damage = 0;
        bool lethal = false, held = false, headshot = false, spent = false, travelled = false, self = false;
        uint16_t claim = 0, life = 0, generation = 0;
    };
    struct SlotState {
        std::array<Pending, PendingCapacity> pending{};
        int count = 0, head = 0;
        int settledCredit = 0;
        uint32_t settledFrame = 0;
        int shownHealth = 0;
        uint32_t predictedFrame = 0;
        int lastAuthorityHealth = 0;
        uint16_t life = 0, generation = 0;
    };
    struct Outgoing {
        net::HitClaim claim;
        int age = 0, sends = 0;
        bool live = false;
    };

    int localSlot() const { return m_client.localSlot(); }
    uint32_t now() const { return m_client.frame(); }
    int holdFrames() const;
    int resendInterval() const;
    bool claiming() const { return m_client.localSlot() >= 0; }
    bool lifeMatches(int slot, uint16_t generation, uint16_t life) const { return m_client.lifeMatches(slot, generation, life); }
    void ensureLife(int slot);
    int debit(int slot);
    int push(int slot, int damage, bool lethal, bool headshot, bool self);
    int retireHead(int slot, bool confirmed);
    void resolveHeld(Pending& p, bool confirmed);
    void settle(int slot, uint16_t claimId, bool confirmed);
    uint16_t declare(int victimSlot, const Player& victim, const DamageSource& source, int damage, uint32_t flags, bool lethal);
    void tickOutbox();

    net::Client& m_client;
    SlotOf m_slotOf;
    std::array<SlotState, net::SlotCapacity> m_slots{};
    std::array<uint32_t, HealCapacity> m_healFrame{};
    std::array<int, HealCapacity> m_healAmount{};
    int m_healCount = 0, m_healHead = 0;
    std::array<Outgoing, 32> m_outbox{};
    uint16_t m_nextClaimId = 1;
    // The tally the log prints.
    long long m_predicted = 0, m_confirmed = 0, m_denied = 0, m_unpredicted = 0, m_lethalHeld = 0, m_lethalConfirmed = 0,
              m_lethalDenied = 0, m_selfPredicted = 0, m_selfConfirmed = 0, m_selfDeaths = 0, m_drained = 0, m_headshots = 0,
              m_headshotsAgreed = 0, m_headshotsDowngraded = 0;
    long long m_declared = 0, m_applied = 0, m_duplicate = 0, m_deadShooter = 0, m_deadVictim = 0, m_refused = 0, m_unanswered = 0,
              m_resends = 0;
};

} // namespace fp
