// NetHitClaims, the arbiter's half: the server checks each hit a client says
// it landed against its own history, waits a grace window for its own
// simulation to resolve the same shot, and applies only what survives -- in
// the order the shots were fired, so that of two players who shot each
// other, the one put down by a shot aimed at a strictly earlier world is the
// one whose shot is void.

#include "ServerGame.h"

#include "World.h"

#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <limits>

namespace fp {

namespace {

constexpr float ClaimRadius = 2.0f, MeleeRadius = 4.0f;
constexpr int MinGraceFrames = 24, MaxGraceFrames = 72;
constexpr int AckMatchFrames = 120, LaunchMatchFrames = 4, RescuedFrames = 720;

int graceFor(int ping) { return std::clamp(static_cast<int>(ping * 0.06f) + 20, MinGraceFrames, MaxGraceFrames); }

bool debugNet() { return qEnvironmentVariableIsSet("FP_DEBUG_NET"); }

// The most a weapon can deal with every multiplier stacked at once.
int maxDamageFor(uint8_t beam)
{
    int raw = 200;
    const auto& weapons = weaponsMP();
    if (beam != net::HitClaim::NoBeam && beam < weapons.size()) {
        const WeaponInfo& w = weapons[beam];
        raw = std::max({w.chargedHeadshotDamage, w.headshotDamage, w.minChargeHeadshotDamage, w.chargedDamage, w.unchargedDamage,
            w.minChargeDamage, w.chargedSplashDamage});
    }
    return static_cast<int>(raw * 5.0f) + 1;
}

} // namespace

bool ServerGame::inPlay(int slot)
{
    const Player* p = worldPlayer(slot);
    return p != nullptr && m_slots[slot].worldSlot >= 0 && m_room->world->m_slots[m_slots[slot].worldSlot].active && p->health() > 0;
}

bool ServerGame::positionAt(int slot, uint32_t frame, uint16_t generation, uint16_t life, Vec3& position) const
{
    // NetUnlagged.PositionAt: where the history holds a player at `frame`.
    if (slot < 0 || slot >= net::SlotCapacity || frame == 0) {
        return false;
    }
    const size_t index = frame % HistoryFrames;
    const HistoryCell& cell = m_history[slot][index];
    if (m_stamp[index] != frame || !cell.inPlay || cell.life != life || cell.generation != generation) {
        return false;
    }
    position = cell.position;
    return std::isfinite(position[0]) && std::isfinite(position[1]) && std::isfinite(position[2]);
}

uint32_t ServerGame::fireFrameOf(int slot) const
{
    if (slot < 0 || slot >= net::SlotCapacity || !m_slots[slot].hasIntent) {
        return m_frame;
    }
    const uint32_t ack = m_slots[slot].intent.ackFrame;
    return ack == 0 || ack > m_frame ? m_frame : ack;
}

void ServerGame::noteAuthorityHit(int attacker, int victim, uint32_t launchFrame, int damage)
{
    // NoteAuthorityHit: every hit this machine resolves, for pairing a claim
    // with its own answer and for judging who was down first.
    if (victim < 0 || victim >= net::SlotCapacity) {
        return;
    }
    const uint32_t fire = m_applyingClaim ? m_applyingAck : fireFrameOf(attacker);
    if (m_applyingClaim) {
        launchFrame = m_applyingLaunch;
    }
    m_lastHitFire[victim] = launchFrame != 0 ? launchFrame : fire;
    if (attacker >= 0 && attacker < net::SlotCapacity) {
        int& head = m_ledgerHead[attacker][victim];
        LedgerEntry& e = m_ledger[attacker][victim][head];
        e.at = m_frame;
        e.ack = fire;
        e.launch = launchFrame;
        e.damage = damage;
        e.used = m_applyingClaim;
        head = (head + 1) % LedgerDepth;
    }
}

bool ServerGame::takeLedger(int attacker, int victim, uint32_t ack, uint32_t launch, uint32_t arrived, int window, int& damage)
{
    // The authority's own hit for the same shot: by the shot when both know it, else by the nearest world in the window.
    damage = 0;
    auto& ledger = m_ledger[attacker][victim];
    if (launch != 0) {
        for (LedgerEntry& e : ledger) {
            if (!e.used && e.at != 0 && e.launch != 0 && std::llabs(static_cast<long long>(e.launch) - launch) <= LaunchMatchFrames) {
                e.used = true;
                damage = e.damage;
                return true;
            }
        }
    }
    const uint32_t floor = arrived > static_cast<uint32_t>(window) ? arrived - static_cast<uint32_t>(window) : 0;
    LedgerEntry* best = nullptr;
    long long bestGap = std::numeric_limits<long long>::max();
    for (LedgerEntry& e : ledger) {
        if (e.used || e.at == 0 || e.at < floor || e.at > arrived + static_cast<uint32_t>(window) || (e.launch != 0 && launch != 0)) {
            continue;
        }
        const long long gap = std::llabs(static_cast<long long>(e.ack) - ack);
        if (gap <= AckMatchFrames && gap < bestGap) {
            bestGap = gap;
            best = &e;
        }
    }
    if (best == nullptr) {
        return false;
    }
    best->used = true;
    damage = best->damage;
    return true;
}

void ServerGame::clearLedger(int attacker, int victim)
{
    m_ledger[attacker][victim] = {};
    m_ledgerHead[attacker][victim] = 0;
}

bool ServerGame::seen(int slot, uint16_t id) const
{
    return id == 0 || m_seenIds[slot][id % SeenCapacity] == id
        || (m_newestId[slot] != 0 && !net::newer16(id, m_newestId[slot])
            && static_cast<uint16_t>(m_newestId[slot] - id) >= SeenCapacity);
}

void ServerGame::remember(int slot, uint16_t id, uint8_t result)
{
    if (m_newestId[slot] == 0 || net::newer16(id, m_newestId[slot])) {
        m_newestId[slot] = id;
    }
    m_seenIds[slot][id % SeenCapacity] = id;
    m_seenResults[slot][id % SeenCapacity] = result;
}

void ServerGame::answer(int slot, uint16_t id, uint8_t result, bool rememberIt)
{
    if (slot < 0 || slot >= net::SlotCapacity) {
        return;
    }
    if (rememberIt) {
        remember(slot, id, result);
    }
    if (m_verdicts[slot].size() < static_cast<size_t>(net::HitVerdict::MaxPerPacket)) {
        m_verdicts[slot].emplace_back(id, result);
    }
}

std::vector<std::pair<uint16_t, uint8_t>> ServerGame::takeVerdicts(int slot)
{
    if (slot < 0 || slot >= net::SlotCapacity) {
        return {};
    }
    return std::exchange(m_verdicts[slot], {});
}

void ServerGame::hitClaims(int shooter, std::span<const uint8_t> payload)
{
    // NetHitClaims.Receive
    if (!m_room || shooter < 0 || shooter >= net::SlotCapacity || payload.empty()) {
        return;
    }
    const int count = std::min<int>(payload[0], net::HitClaim::MaxPerPacket);
    for (int i = 0; i < count; i++) {
        const size_t at = 1 + static_cast<size_t>(i) * net::HitClaim::Size;
        if (at + net::HitClaim::Size > payload.size()) {
            break;
        }
        const net::HitClaim claim = net::HitClaim::read(payload.subspan(at, net::HitClaim::Size));
        m_claimsReceived++;
        if (claim.shooterLifeId == 0 || claim.victimLifeId == 0 || claim.matchId != m_matchId || claim.authorityEpoch != m_epoch
            || !lifeMatches(shooter, claim.shooterGeneration, claim.shooterLifeId)) {
            continue; // another life's, or another match's
        }
        if (!lifeMatches(claim.victimSlot, claim.victimGeneration, claim.victimLifeId)) {
            answer(shooter, claim.claimId, net::HitVerdict::WrongLife, false);
            continue;
        }
        if (seen(shooter, claim.claimId)) {
            m_claimsRepeats++;
            const int seenAt = claim.claimId % SeenCapacity;
            const uint8_t result = m_seenIds[shooter][seenAt] == claim.claimId ? m_seenResults[shooter][seenAt] : net::HitVerdict::TooOld;
            if (result != ResultPending) {
                answer(shooter, claim.claimId, result);
            }
            continue;
        }
        remember(shooter, claim.claimId, ResultPending);
        const uint8_t immediate = judge(shooter, claim);
        if (immediate != net::HitVerdict::Applied) {
            answer(shooter, claim.claimId, immediate);
            continue;
        }
        // Parked: its own answer may still come within the grace window.
        for (PendingClaim& p : m_claims) {
            if (!p.live) {
                p.claim = claim;
                p.shooter = static_cast<uint8_t>(shooter);
                p.arrived = m_frame;
                p.grace = graceFor(m_ping[shooter]);
                p.live = true;
                break;
            }
        }
    }
}

uint8_t ServerGame::judge(int shooter, const net::HitClaim& claim)
{
    // NetHitClaims.Judge, cheapest first.
    const int victim = claim.victimSlot;
    if (victim < 0 || victim >= net::SlotCapacity || victim == shooter || worldPlayer(victim) == nullptr) {
        m_claimsRefused++;
        return net::HitVerdict::Refused;
    }
    if (claim.ackFrame == 0 || claim.ackFrame > m_frame || m_frame - claim.ackFrame > HistoryFrames - 4) {
        m_claimsTooOld++;
        return net::HitVerdict::TooOld;
    }
    if (claim.launchFrame > claim.ackFrame) {
        m_claimsRefused++;
        return net::HitVerdict::InvalidLaunch;
    }
    if (claim.damage > maxDamageFor(claim.beam)) {
        m_claimsRefused++;
        return net::HitVerdict::DamageLimit;
    }
    Vec3 was;
    if (!positionAt(victim, claim.ackFrame, claim.victimGeneration, claim.victimLifeId, was)) {
        m_claimsTooOld++;
        return net::HitVerdict::TooOld;
    }
    // Only a hit the history says was there to be had: the two machines
    // agreed about where the victim stood.
    const float reach = claim.beam == net::HitClaim::NoBeam ? MeleeRadius : ClaimRadius;
    const Vec3 offset = claim.hitPoint - was;
    if (!std::isfinite(offset[0]) || !std::isfinite(offset[1]) || !std::isfinite(offset[2]) || dot(offset, offset) > reach * reach) {
        m_claimsRefused++;
        return net::HitVerdict::Geometry;
    }
    if (!inPlay(victim)) {
        m_claimsDeadVictim++;
        return net::HitVerdict::DeadVictim;
    }
    const uint32_t fired = claim.launchFrame != 0 ? claim.launchFrame : claim.ackFrame;
    if (m_dead[shooter] && m_deathFire[shooter] < fired) {
        m_claimsDeadShooter++;
        return net::HitVerdict::DeadShooter;
    }
    return net::HitVerdict::Applied;
}

void ServerGame::trackDeaths()
{
    // A death is stamped with the world of the shot that caused it.
    for (int i = 0; i < net::SlotCapacity; i++) {
        const bool now = inPlay(i);
        if (m_wasInPlay[i] && !now) {
            m_dead[i] = true;
            m_deathFire[i] = m_lastHitFire[i] != 0 ? m_lastHitFire[i] : m_frame;
        } else if (!m_wasInPlay[i] && now) {
            m_dead[i] = false;
            m_deathFire[i] = m_lastHitFire[i] = 0;
            for (int j = 0; j < net::SlotCapacity; j++) {
                clearLedger(j, i);
            }
        }
        m_wasInPlay[i] = now;
    }
}

void ServerGame::tickClaims()
{
    // NetHitClaims.Tick: drop what the authority resolved itself, then apply
    // the rest in fire-frame order once its grace is over.
    trackDeaths();
    while (true) {
        PendingClaim* next = nullptr;
        PendingClaim* overdue = nullptr;
        for (PendingClaim& p : m_claims) {
            if (!p.live) {
                continue;
            }
            const net::HitClaim& c = p.claim;
            if (c.matchId != m_matchId || c.authorityEpoch != m_epoch || !lifeMatches(p.shooter, c.shooterGeneration, c.shooterLifeId)
                || !lifeMatches(c.victimSlot, c.victimGeneration, c.victimLifeId)) {
                p.live = false;
                continue;
            }
            int resolved = 0;
            if (takeLedger(p.shooter, c.victimSlot, c.ackFrame, c.launchFrame, p.arrived, p.grace, resolved)) {
                p.live = false;
                m_claimsDuplicate++;
                answer(p.shooter, c.claimId, net::HitVerdict::Duplicate);
                continue;
            }
            auto fired = [](const PendingClaim& e) { return e.claim.launchFrame != 0 ? e.claim.launchFrame : e.claim.ackFrame; };
            if (next == nullptr || fired(p) < fired(*next)) {
                next = &p;
            }
            if (m_frame - p.arrived >= 2 * MaxGraceFrames && (overdue == nullptr || p.arrived < overdue->arrived)) {
                overdue = &p;
            }
        }
        if (next == nullptr) {
            break;
        }
        if (m_frame - next->arrived < static_cast<uint32_t>(next->grace)) {
            if (overdue == nullptr) {
                break;
            }
            next = overdue;
        }
        applyClaim(*next);
        next->live = false;
        trackDeaths();
    }
}

void ServerGame::applyClaim(PendingClaim& entry)
{
    // NetHitClaims.ApplyOne: through TakeDamage with the shooter as the
    // source, so the damage history, the score and the death are the usual ones.
    const net::HitClaim& c = entry.claim;
    const int victimSlot = c.victimSlot, shooterSlot = entry.shooter;
    Player* victim = worldPlayer(victimSlot);
    Player* shooter = worldPlayer(shooterSlot);
    if (victim == nullptr || shooter == nullptr) {
        answer(shooterSlot, c.claimId, net::HitVerdict::Refused);
        return;
    }
    if (!inPlay(victimSlot)) {
        m_claimsDeadVictim++;
        answer(shooterSlot, c.claimId, net::HitVerdict::DeadVictim);
        return;
    }
    if (m_dead[shooterSlot] && m_deathFire[shooterSlot] < (c.launchFrame != 0 ? c.launchFrame : c.ackFrame)) {
        m_claimsDeadShooter++;
        answer(shooterSlot, c.claimId, net::HitVerdict::DeadShooter);
        return;
    }
    uint32_t flags = DamageFlags::NoDmgInvuln;
    if (c.flags & net::HitClaim::FlagHeadshot) {
        flags |= DamageFlags::Headshot;
    }
    DamageSource source;
    source.attacker = shooter;
    source.beam = c.beam == net::HitClaim::NoBeam ? -1 : c.beam;
    source.claimed = true;
    source.launchFrame = c.launchFrame;
    const int before = victim->health();
    const bool lethal = before <= c.damage;
    m_applyingClaim = true;
    m_applyingAck = c.ackFrame;
    m_applyingLaunch = c.launchFrame;
    victim->takeDamage(c.damage, flags, nullptr, source);
    m_applyingClaim = false;
    if (victim->health() >= before) {
        m_claimsRefused++;
        answer(shooterSlot, c.claimId, net::HitVerdict::NoDamage);
        return;
    }
    if ((c.flags & net::HitClaim::FlagFrozen) && victim->health() > 0) {
        victim->netSetAfflictions(true, victim->disruptedTimer() > 0, victim->burnTimer() > 0);
    }
    m_claimsApplied++;
    m_rescuedDamage += before - std::max(0, victim->health());
    m_rescuedHeadshots += (c.flags & net::HitClaim::FlagHeadshot) != 0;
    m_rescuedKills += lethal;
    if (c.launchFrame != 0) {
        // The authority's own copy of that shot, still in the air, must not land it a second time.
        Rescued* same = nullptr;
        for (Rescued& r : m_rescued) {
            if (r.owed > 0 && r.attacker == shooterSlot && r.launch == c.launchFrame && r.victim == victimSlot
                && lifeMatches(victimSlot, r.victimGeneration, r.victimLife)) {
                same = &r;
            }
        }
        if (same != nullptr) {
            same->owed++;
            same->at = m_frame;
        } else {
            Rescued& r = m_rescued[m_rescuedHead];
            m_rescuedHead = (m_rescuedHead + 1) % RescuedCapacity;
            r = {static_cast<uint8_t>(shooterSlot), static_cast<uint8_t>(victimSlot), c.launchFrame, m_frame,
                m_slots[victimSlot].occupant.generation, m_slots[victimSlot].life, 1};
        }
    }
    if (debugNet()) {
        qInfo("[claims] slot %d's claim on slot %d applied: %d damage%s%s, aimed at frame %u, %u frames ago", shooterSlot, victimSlot,
            before - std::max(0, victim->health()), (c.flags & net::HitClaim::FlagHeadshot) ? " (headshot)" : "",
            lethal ? " and killed them" : "", c.ackFrame, m_frame - c.ackFrame);
    }
    answer(shooterSlot, c.claimId, net::HitVerdict::Applied);
}

bool ServerGame::suppress(const Player& victim, const DamageSource& source)
{
    // NetHitClaims.AlreadyRescued
    if (m_applyingClaim || source.launchFrame == 0 || source.attacker == nullptr || source.beam < 0) {
        return false;
    }
    const int attacker = serverSlotOf(*source.attacker);
    const int victimSlot = serverSlotOf(victim);
    if (attacker < 0 || victimSlot < 0) {
        return false;
    }
    for (Rescued& r : m_rescued) {
        if (r.owed <= 0 || r.attacker != attacker || r.launch != source.launchFrame || r.victim != victimSlot
            || !lifeMatches(victimSlot, r.victimGeneration, r.victimLife)) {
            continue;
        }
        if (m_frame - r.at > RescuedFrames) {
            r.owed = 0;
            continue;
        }
        r.owed--;
        m_suppressed++;
        return true;
    }
    return false;
}

void ServerGame::forgetClaims(int slot)
{
    // NetHitClaims.ForgetSlot: somebody else holds the slot now.
    for (PendingClaim& p : m_claims) {
        if (p.claim.victimSlot == slot || p.shooter == slot) {
            p.live = false;
        }
    }
    m_verdicts[slot].clear();
    m_seenIds[slot] = {};
    m_seenResults[slot] = {};
    for (Rescued& r : m_rescued) {
        if (r.attacker == slot || r.victim == slot) {
            r.owed = 0;
        }
    }
    m_newestId[slot] = 0;
    m_deathFire[slot] = m_lastHitFire[slot] = 0;
    m_dead[slot] = m_wasInPlay[slot] = false;
    for (int i = 0; i < net::SlotCapacity; i++) {
        clearLedger(slot, i);
        clearLedger(i, slot);
    }
}

void ServerGame::resetClaims()
{
    for (int slot = 0; slot < net::SlotCapacity; slot++) {
        forgetClaims(slot);
    }
}

} // namespace fp
