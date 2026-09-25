#include "NetPrediction.h"

#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace fp {

namespace {

constexpr float TravelFlight = 3 / 60.0f;
constexpr int MaxSends = 6, MaxAge = 60, MinResendInterval = 8;

// NetHitClaims.GraceFor: how long the server holds a claim for its own answer.
int graceFor(int ping) { return std::clamp(static_cast<int>(ping * 0.06f) + 20, 24, 72); }

} // namespace

HitPrediction::HitPrediction(net::Client& client, SlotOf slotOf)
    : m_client(client)
    , m_slotOf(std::move(slotOf))
{
}

int HitPrediction::holdFrames() const
{
    const int slot = localSlot();
    const int ping = slot >= 0 ? m_client.slotInfo()[slot].ping : 0;
    return std::clamp(static_cast<int>(ping * 0.06f) + 12 + (claiming() ? graceFor(ping) : 0), 15, 120);
}

int HitPrediction::resendInterval() const
{
    const int slot = localSlot();
    const int ping = slot >= 0 ? m_client.slotInfo()[slot].ping : 0;
    return std::clamp(static_cast<int>(ping * 0.06f) + 6, MinResendInterval, 40);
}

void HitPrediction::ensureLife(int slot)
{
    if (slot < 0 || slot >= net::SlotCapacity) {
        return;
    }
    const net::LifecycleTracker& life = m_client.life(slot);
    SlotState& s = m_slots[slot];
    if (s.life != life.lifeId() || s.generation != life.generation()) {
        forgetSlot(slot);
        s.life = life.lifeId();
        s.generation = life.generation();
    }
}

bool HitPrediction::predicts(const Player& victim, const DamageSource& source, uint32_t flags)
{
    // NetHitPrediction.Predicts: this machine's own shot, splash or fall.
    const int local = localSlot();
    if (victim.health() <= 0 || local < 0) {
        return false;
    }
    if (source.attacker == nullptr) {
        return m_slotOf(&victim) == local && (flags & DamageFlags::Death);
    }
    return m_slotOf(source.attacker) == local;
}

void HitPrediction::noteHit(Player& victim, uint32_t& flags, int& damage, const DamageSource& source)
{
    const int local = localSlot();
    const int victimSlot = m_slotOf(&victim);
    if (local < 0 || victimSlot < 0) {
        return;
    }
    bool self;
    if (source.attacker == nullptr) {
        if (victimSlot != local) {
            return;
        }
        self = true;
    } else {
        if (m_slotOf(source.attacker) != local) {
            return;
        }
        self = source.attacker == &victim;
    }
    bool lethal = victim.health() > 0 && (damage >= victim.health() || (flags & DamageFlags::Death));
    const int claimedDamage = damage;
    const bool claimedLethal = lethal;
    if (lethal && !self) {
        // A prediction never kills somebody else: the authority's word puts them down.
        damage = std::max(0, victim.health() - 1);
        flags &= ~DamageFlags::Death;
        m_lethalHeld++;
        lethal = false;
    }
    const bool headshot = flags & DamageFlags::Headshot;
    const int at = push(victimSlot, damage, lethal, headshot, self);
    if (at >= 0) {
        Pending& p = m_slots[victimSlot].pending[at];
        p.held = claimedLethal && !self;
        p.travelled = !self && source.flight > TravelFlight;
        if (!self) {
            p.claim = declare(victimSlot, victim, source, claimedDamage, flags, claimedLethal);
        }
    }
    if (self) {
        m_selfPredicted++;
        m_selfDeaths += lethal;
    } else {
        m_predicted++;
        m_headshots += headshot;
    }
}

void HitPrediction::noteDrain(const Player& healer, int gained)
{
    const int local = localSlot();
    if (gained <= 0 || local < 0 || m_slotOf(&healer) != local) {
        return;
    }
    if (m_healCount == HealCapacity) {
        m_healHead = (m_healHead + 1) % HealCapacity;
        m_healCount--;
    }
    const int tail = (m_healHead + m_healCount) % HealCapacity;
    m_healFrame[tail] = now();
    m_healAmount[tail] = gained;
    m_healCount++;
    m_drained += gained;
}

int HitPrediction::push(int slot, int damage, bool lethal, bool headshot, bool self)
{
    ensureLife(slot);
    if (slot < 0 || slot >= net::SlotCapacity) {
        return -1;
    }
    SlotState& s = m_slots[slot];
    if (s.count == PendingCapacity) {
        retireHead(slot, false);
    }
    s.predictedFrame = now();
    const int tail = (s.head + s.count) % PendingCapacity;
    Pending& p = s.pending[tail];
    p = {};
    p.life = m_client.life(slot).lifeId();
    p.generation = m_client.life(slot).generation();
    p.frame = now();
    p.damage = std::max(0, damage);
    p.lethal = lethal;
    p.headshot = headshot;
    p.self = self;
    s.count++;
    return tail;
}

void HitPrediction::resolveHeld(Pending& p, bool confirmed)
{
    if (!p.held) {
        return;
    }
    p.held = false;
    (confirmed ? m_lethalConfirmed : m_lethalDenied)++;
}

int HitPrediction::retireHead(int slot, bool confirmed)
{
    SlotState& s = m_slots[slot];
    const int head = s.head;
    Pending& p = s.pending[head];
    resolveHeld(p, confirmed);
    const bool answered = p.spent;
    if (!answered && !confirmed && !p.self) {
        m_denied++;
    }
    p.claim = 0;
    p.spent = p.self = false;
    s.head = (head + 1) % PendingCapacity;
    s.count--;
    return answered ? -1 : head;
}

bool HitPrediction::confirm(int slot, int landed, bool authorityHeadshot)
{
    ensureLife(slot);
    if (slot < 0 || slot >= net::SlotCapacity) {
        return false;
    }
    SlotState& s = m_slots[slot];
    if (s.count == 0) {
        // Nothing outstanding: a verdict may already have retired it.
        const int owed = std::max(1, landed);
        if (now() - s.settledFrame >= static_cast<uint32_t>(holdFrames())) {
            s.settledCredit = 0;
        }
        const int fromSettled = std::min(owed, s.settledCredit);
        s.settledCredit -= fromSettled;
        m_unpredicted += owed - fromSettled;
        return fromSettled > 0;
    }
    const bool self = slot == localSlot();
    const int take = std::clamp(landed, 1, s.count);
    if (landed > take) {
        s.settledCredit -= std::min(landed - take, s.settledCredit);
    }
    for (int i = 0; i < take; i++) {
        const bool predictedHeadshot = s.pending[s.head].headshot;
        s.pending[s.head].headshot = false;
        if (retireHead(slot, true) < 0) {
            continue;
        }
        if (self) {
            m_selfConfirmed++;
            continue;
        }
        m_confirmed++;
        if (i == take - 1) {
            if (predictedHeadshot && authorityHeadshot) {
                m_headshotsAgreed++;
            } else if (predictedHeadshot) {
                m_headshotsDowngraded++;
            }
        }
    }
    return true;
}

int HitPrediction::debit(int slot)
{
    ensureLife(slot);
    if (slot < 0 || slot >= net::SlotCapacity) {
        return 0;
    }
    const SlotState& s = m_slots[slot];
    const uint32_t hold = static_cast<uint32_t>(holdFrames());
    int total = 0;
    for (int i = 0; i < s.count; i++) {
        const Pending& p = s.pending[(s.head + i) % PendingCapacity];
        if (lifeMatches(slot, p.generation, p.life) && now() - p.frame < hold && !p.travelled) {
            total += p.damage;
        }
    }
    return total;
}

int HitPrediction::healthFor(int slot, int authorityHealth)
{
    ensureLife(slot);
    if (slot < 0 || slot >= net::SlotCapacity) {
        return authorityHealth;
    }
    SlotState& s = m_slots[slot];
    // The floor never refuses a rise the authority itself reports (a pickup, a respawn).
    if (authorityHealth > s.lastAuthorityHealth && s.shownHealth > 0) {
        s.shownHealth = 0;
    }
    s.lastAuthorityHealth = authorityHealth;
    const int owed = debit(slot);
    int health = owed > 0 && authorityHealth > 1 ? std::max(1, authorityHealth - owed) : authorityHealth;
    if (authorityHealth > 0 && s.shownHealth > 0 && now() - s.predictedFrame < static_cast<uint32_t>(holdFrames())) {
        health = std::max(1, std::min(health, s.shownHealth));
    }
    s.shownHealth = health;
    return health;
}

int HitPrediction::localHealthFor(const Player& player, int authorityHealth)
{
    const int local = localSlot();
    ensureLife(local);
    if (authorityHealth <= 0) {
        return authorityHealth;
    }
    const uint32_t hold = static_cast<uint32_t>(holdFrames());
    int credit = 0;
    for (int i = 0; i < m_healCount; i++) {
        const int at = (m_healHead + i) % HealCapacity;
        if (now() - m_healFrame[at] < hold) {
            credit += m_healAmount[at];
        }
    }
    credit -= debit(local);
    if (player.health() <= 0 && heldDead(local)) {
        return 0;
    }
    if (credit == 0) {
        return authorityHealth;
    }
    const int max = player.healthMax() > 0 ? player.healthMax() : authorityHealth;
    return std::clamp(authorityHealth + credit, 1, std::max(1, max));
}

bool HitPrediction::heldDead(int slot)
{
    ensureLife(slot);
    if (slot < 0 || slot >= net::SlotCapacity || slot != localSlot()) {
        return false;
    }
    const SlotState& s = m_slots[slot];
    const uint32_t hold = static_cast<uint32_t>(holdFrames());
    for (int i = 0; i < s.count; i++) {
        const Pending& p = s.pending[(s.head + i) % PendingCapacity];
        if (p.lethal && now() - p.frame < hold) {
            return true;
        }
    }
    return false;
}

void HitPrediction::noteRespawn(int slot)
{
    if (slot == localSlot()) {
        forgetPending();
    } else {
        forgetSlot(slot);
    }
}

void HitPrediction::noteDeath(int slot)
{
    // A fall names nobody, so nothing else releases a predicted self-kill.
    if (slot < 0 || slot >= net::SlotCapacity) {
        return;
    }
    for (Pending& p : m_slots[slot].pending) {
        p.lethal = p.held = false;
    }
}

void HitPrediction::forgetSlot(int slot)
{
    if (slot < 0 || slot >= net::SlotCapacity) {
        return;
    }
    SlotState& s = m_slots[slot];
    const uint16_t life = s.life, generation = s.generation;
    s = {};
    s.life = life;
    s.generation = generation;
    if (slot == localSlot()) {
        m_healCount = m_healHead = 0;
    }
}

void HitPrediction::forgetClaims(int slot)
{
    // NetHitClaims.ForgetSlot: somebody else holds the slot now (or nobody).
    for (Outgoing& o : m_outbox) {
        if (o.claim.victimSlot == slot || slot == localSlot()) {
            o.live = false;
        }
    }
}

void HitPrediction::forgetPending()
{
    for (int slot = 0; slot < net::SlotCapacity; slot++) {
        forgetSlot(slot);
    }
    m_healCount = m_healHead = 0;
    for (Outgoing& o : m_outbox) {
        o.live = false;
    }
}

void HitPrediction::tick()
{
    const uint32_t frame = now();
    for (int slot = 0; slot < net::SlotCapacity; slot++) {
        SlotState& s = m_slots[slot];
        while (s.count > 0 && frame - s.pending[s.head].frame >= PendingFrames) {
            retireHead(slot, false);
        }
    }
    while (m_healCount > 0 && frame - m_healFrame[m_healHead] >= PendingFrames) {
        m_healHead = (m_healHead + 1) % HealCapacity;
        m_healCount--;
    }
    if (claiming()) {
        tickOutbox();
    }
}

void HitPrediction::settle(int slot, uint16_t claimId, bool confirmed)
{
    // The exact retirement: the prediction a verdict's claim was declared under.
    ensureLife(slot);
    if (slot < 0 || slot >= net::SlotCapacity || claimId == 0) {
        return;
    }
    SlotState& s = m_slots[slot];
    for (int i = 0; i < s.count; i++) {
        Pending& p = s.pending[(s.head + i) % PendingCapacity];
        if (p.claim != claimId) {
            continue;
        }
        resolveHeld(p, confirmed);
        if (confirmed) {
            m_confirmed++;
            s.settledCredit = std::min(s.settledCredit + 1, 8);
            s.settledFrame = now();
        } else {
            m_denied++;
            s.shownHealth = 0;
        }
        p.claim = 0;
        p.spent = true;
        if (confirmed) {
            return; // its damage stays in the debit until the snapshot carrying the health arrives
        }
        p.damage = 0;
        p.lethal = p.headshot = false;
        while (s.count > 0 && s.pending[s.head].spent && s.pending[s.head].damage == 0) {
            s.pending[s.head].spent = false;
            s.head = (s.head + 1) % PendingCapacity;
            s.count--;
        }
        return;
    }
}

uint16_t HitPrediction::declare(int victimSlot, const Player& victim, const DamageSource& source, int damage, uint32_t flags, bool lethal)
{
    // NetHitClaims.Declare
    const int local = localSlot();
    if (!claiming() || damage <= 0 || victimSlot == local || m_client.life(victimSlot).lifeId() == 0 || m_client.life(local).lifeId() == 0) {
        return 0;
    }
    Outgoing* slot = nullptr;
    for (Outgoing& o : m_outbox) {
        if (!o.live) {
            slot = &o;
            break;
        }
    }
    if (slot == nullptr) {
        slot = &*std::max_element(m_outbox.begin(), m_outbox.end(), [](const Outgoing& a, const Outgoing& b) { return a.age < b.age; });
        m_unanswered++;
    }
    net::HitClaim& c = slot->claim;
    c = {};
    c.matchId = m_client.matchId();
    c.authorityEpoch = m_client.authorityEpoch();
    c.shooterGeneration = m_client.life(local).generation();
    c.shooterLifeId = m_client.life(local).lifeId();
    c.victimGeneration = m_client.life(victimSlot).generation();
    c.victimLifeId = m_client.life(victimSlot).lifeId();
    c.claimId = m_nextClaimId;
    c.frame = now();
    uint32_t readFrame;
    uint8_t readSub;
    c.ackFrame = m_client.smoothing().ackPoint(readFrame, readSub) ? readFrame : m_client.appliedSnapshotFrame();
    c.launchFrame = source.launchFrame;
    c.victimSlot = static_cast<uint8_t>(victimSlot);
    c.beam = source.beam < 0 || source.bomb >= 0 ? net::HitClaim::NoBeam : static_cast<uint8_t>(source.beam);
    c.damage = static_cast<uint16_t>(std::min(damage, 0xFFFF));
    c.flags = static_cast<uint8_t>(((flags & DamageFlags::Headshot) ? net::HitClaim::FlagHeadshot : 0) | (lethal ? net::HitClaim::FlagLethal : 0)
        | (victim.frozenTimer() > 0 ? net::HitClaim::FlagFrozen : 0));
    c.hitPoint = victim.position();
    slot->age = slot->sends = 0;
    slot->live = true;
    const uint16_t id = m_nextClaimId++;
    if (m_nextClaimId == 0) {
        m_nextClaimId = 1;
    }
    m_declared++;
    return id;
}

size_t HitPrediction::composeClaims(std::span<uint8_t> dest)
{
    if (!claiming()) {
        return 0;
    }
    int count = 0;
    size_t offset = 1;
    const int interval = resendInterval();
    for (Outgoing& o : m_outbox) {
        if (count >= net::HitClaim::MaxPerPacket) {
            break;
        }
        if (!o.live || (o.sends > 0 && (o.sends >= MaxSends || o.age % interval != 0))) {
            continue;
        }
        o.claim.write(dest.subspan(offset, net::HitClaim::Size));
        offset += net::HitClaim::Size;
        m_resends += o.sends > 0;
        o.sends++;
        count++;
    }
    if (count == 0) {
        return 0;
    }
    dest[0] = static_cast<uint8_t>(count);
    return offset;
}

void HitPrediction::tickOutbox()
{
    for (Outgoing& o : m_outbox) {
        if (!o.live) {
            continue;
        }
        if (++o.age > MaxAge) {
            o.live = false;
            m_unanswered++;
            settle(o.claim.victimSlot, o.claim.claimId, false);
        }
    }
}

void HitPrediction::applyVerdicts(std::span<const uint8_t> payload)
{
    // NetHitClaims.ApplyVerdicts
    if (payload.size() < static_cast<size_t>(net::HitVerdict::HeaderSize)) {
        return;
    }
    const int local = localSlot();
    if (local < 0 || net::readU16(payload, 1) != m_client.matchId() || net::readU64(payload, 3) != m_client.authorityEpoch()
        || !lifeMatches(local, net::readU16(payload, 11), net::readU16(payload, 13))) {
        return;
    }
    const int count = std::min<int>(payload[0], net::HitVerdict::MaxPerPacket);
    for (int i = 0; i < count; i++) {
        const size_t at = net::HitVerdict::HeaderSize + static_cast<size_t>(i) * net::HitVerdict::EntrySize;
        if (at + net::HitVerdict::EntrySize > payload.size()) {
            break;
        }
        const uint16_t id = net::readU16(payload, at);
        const uint8_t result = payload[at + 2];
        for (Outgoing& o : m_outbox) {
            if (!o.live || o.claim.claimId != id || !lifeMatches(o.claim.victimSlot, o.claim.victimGeneration, o.claim.victimLifeId)) {
                continue;
            }
            o.live = false;
            switch (result) {
            case net::HitVerdict::Applied:
                m_applied++;
                settle(o.claim.victimSlot, id, true);
                break;
            case net::HitVerdict::Duplicate:
                m_duplicate++;
                settle(o.claim.victimSlot, id, true);
                break;
            case net::HitVerdict::DeadShooter:
                m_deadShooter++;
                settle(o.claim.victimSlot, id, false);
                break;
            case net::HitVerdict::DeadVictim:
                m_deadVictim++;
                settle(o.claim.victimSlot, id, false);
                break;
            default:
                m_refused++;
                settle(o.claim.victimSlot, id, false);
                if (qEnvironmentVariableIsSet("FP_DEBUG_NET")) {
                    qInfo("[claims] claim %u refused: %s", id, net::HitVerdict::describe(result));
                }
                break;
            }
            break;
        }
    }
}

std::string HitPrediction::describe() const
{
    char line[512];
    const double agreed = m_predicted > 0 ? m_confirmed * 100.0 / m_predicted : 0;
    std::snprintf(line, sizeof line,
        "hit prediction: %lld predicted, %lld confirmed (%.1f%%), %lld denied, %lld unpredicted, %lld lethal hits held (%lld confirmed, "
        "%lld denied), %lld self-hits (%lld confirmed, %lld lethal), %lld health drained ahead; headshots %lld (%lld agreed, %lld "
        "downgraded)\nhit claims: %lld declared, %lld applied, %lld already resolved, %lld void (dead shooter), %lld void (victim down), "
        "%lld refused, %lld unanswered, %lld repeats",
        m_predicted, m_confirmed, agreed, m_denied, m_unpredicted, m_lethalHeld, m_lethalConfirmed, m_lethalDenied, m_selfPredicted,
        m_selfConfirmed, m_selfDeaths, m_drained, m_headshots, m_headshotsAgreed, m_headshotsDowngraded, m_declared, m_applied, m_duplicate,
        m_deadShooter, m_deadVictim, m_refused, m_unanswered, m_resends);
    return line;
}

} // namespace fp
