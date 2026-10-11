// The Morph Ball touch block of IntentPacket: what an owner reports so the
// authority can run the same touch roll and touch/shoulder boost branches.
#include "../Mods/Network/NetProtocol.hpp"
#include "../Mods/Network/LocalShotLog.hpp"
#include "../Mods/Network/DeathaltWitness.hpp"
#include "../Mods/Network/RemoteBombState.hpp"
#include "../Mods/Network/RemoteShotQueue.hpp"
#include "../Mods/Network/ShotEventLedger.hpp"
#include "../Mods/Input/HostTouch.hpp"
#include "../Mods/Input/TouchInputAdapter.hpp"
#include "../Entities/Players/MorphBallBoostStateMachine.hpp"
#include "../Entities/Players/MorphBallTouchRules.hpp"

#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <limits>
#include <stdexcept>
#include <tuple>
#include <vector>

using MphRead::Mods::Network::IntentPacket;
using namespace MphRead::Mods::Input;
namespace Boost = MphRead::Entities::MorphBallBoostStateMachine;
namespace Rules = MphRead::Entities::MorphBallTouchRules;

namespace
{
    void Expect(bool value, const char* reason)
    {
        if (!value) throw std::runtime_error(reason);
    }

    bool Near(float a, float b) { return std::fabs(a - b) <= 1e-6F; }

    using ShotEvent = IntentPacket::ShotEvent;
    using MphRead::BeamType;
    using MphRead::Mods::Network::LocalShotLog;
    using MphRead::Mods::Network::RemoteShotQueue;
    using MphRead::Mods::Network::ShotEventLedger;
    using MphRead::Mods::Network::HitClaimPacket;

    IntentPacket WithShots(std::uint32_t frame, std::initializer_list<ShotEvent> events)
    {
        IntentPacket packet{};
        packet.Frame = frame;
        for (const ShotEvent& event : events)
        {
            packet.ShotHistory[packet.ShotHistoryLength++] = event;
        }
        return packet;
    }

    std::uint8_t Id(BeamType beam) { return static_cast<std::uint8_t>(beam); }

    std::uint8_t NextWeapon(RemoteShotQueue& shots)
    {
        const auto event = shots.Next();
        return event.has_value() ? event->WeaponId : IntentPacket::NoWeapon;
    }

    // Protocol 20 on the wire: the ray, this frame's shot, and the history.
    void ShotEventWire()
    {
        IntentPacket shot{};
        shot.ShotHistory[0] = {41, 100, Id(BeamType::Missile)};
        shot.ShotHistory[1] = {42, 104, Id(BeamType::OmegaCannon), 37, 99,
            OpenTK::Mathematics::Vector3(4, 5, 6), OpenTK::Mathematics::Vector3(0, 1, 0)};
        shot.ShotHistoryLength = 2;
        std::vector<std::uint8_t> bytes(IntentPacket::ShotFullSize);
        shot.Write(bytes);
        const IntentPacket noRay = IntentPacket::Read(bytes);
        Expect(!noRay.HasShot, "a frame that fired nothing carries no ray of its own");
        Expect(noRay.ShotHistoryLength == 2 && noRay.ShotHistory[1].Sequence == 42
            && noRay.ShotHistory[1].Frame == 104 && noRay.ShotHistory[1].WeaponId == Id(BeamType::OmegaCannon)
            && noRay.ShotHistory[1].Charge == 37,
            "but still carries the history");
        Expect(noRay.ShotHistory[1].AckFrame == 99 && Near(noRay.ShotHistory[1].Origin.Z, 6.0F)
            && Near(noRay.ShotHistory[1].Direction.Y, 1.0F) && noRay.ShotHistory[1].HasRay(),
            "each event carries the world it was aimed in and its own ray");
        Expect(!noRay.ShotHistory[0].HasRay(), "an event recorded without a ray says so");

        HitClaimPacket claim{};
        claim.ShotSequence = 0xA1B2C3D4U;
        claim.Beam = Id(BeamType::Missile);
        claim.Damage = 9;
        claim.TurretDamage = 10;
        std::vector<std::uint8_t> claimBytes(HitClaimPacket::Size);
        claim.Write(claimBytes);
        Expect(HitClaimPacket::Read(claimBytes).ShotSequence == 0xA1B2C3D4U, "a claim names its shot");
        Expect(HitClaimPacket::Read(claimBytes).Damage == 9 && HitClaimPacket::Read(claimBytes).TurretDamage == 10,
            "and splits a hit on a turret into the body's share and the turret's");
        claim.Flags = HitClaimPacket::FlagSplash;
        claim.Cause = HitClaimPacket::CauseDeathalt;
        claim.Write(claimBytes);
        Expect(HitClaimPacket::Read(claimBytes).Cause == HitClaimPacket::CauseDeathalt
            && HitClaimPacket::Read(claimBytes).Flags == HitClaimPacket::FlagSplash,
            "a claim says what did the damage, beside its flags");
        claimBytes[70] = 0x7F;
        Expect(HitClaimPacket::Read(claimBytes).Cause == HitClaimPacket::CauseHit, "a cause nobody knows is a plain hit");

        // A shooter's Death Alt is taken from what the authority saw of it.
        MphRead::Mods::Network::DeathaltWitness witness;
        Expect(!witness.Vouches(2, 100), "nobody seen with one: not vouched for");
        witness.Observe(2, true, 100);
        witness.Observe(2, false, 140);
        Expect(witness.Vouches(2, 100 + MphRead::Mods::Network::DeathaltWitness::SlackFrames),
            "a claim a trip behind the power-up running out is still its");
        Expect(!witness.Vouches(2, 101 + MphRead::Mods::Network::DeathaltWitness::SlackFrames),
            "past the slack it is not");
        Expect(!witness.Vouches(3, 100), "and one player's Death Alt is not another's");
        witness.Forget(2);
        Expect(!witness.Vouches(2, 100), "a new life starts unseen");

        shot.HasShot = true;
        shot.ShotOrigin = OpenTK::Mathematics::Vector3(1, 2, 3);
        shot.ShotDirection = OpenTK::Mathematics::Vector3(0, 0, -1);
        shot.Write(bytes);
        const IntentPacket withRay = IntentPacket::Read(bytes);
        Expect(withRay.HasShot && Near(withRay.ShotOrigin.Y, 2.0F) && Near(withRay.ShotDirection.Z, -1.0F)
            && withRay.ShotHistory[1].Sequence == 42,
            "this frame's ray and the history share the payload");

        // A Shock Coil frame: a ray with no event.
        IntentPacket coil{};
        coil.HasShot = true;
        coil.ShotOrigin = OpenTK::Mathematics::Vector3(1, 2, 3);
        coil.ShotDirection = OpenTK::Mathematics::Vector3(1, 0, 0);
        coil.Write(bytes);
        const IntentPacket coilRead = IntentPacket::Read(bytes);
        Expect(coilRead.HasShot && coilRead.ShotHistoryLength == 0, "continuous fire keeps its ray without an event");

        bytes[static_cast<std::size_t>(IntentPacket::FullSize + IntentPacket::ShotSize)] = 0xFF;
        Expect(IntentPacket::Read(bytes).ShotHistoryLength == IntentPacket::ShotHistoryCount,
            "a corrupt history count is clamped");
        Expect(IntentPacket::Read(std::span(bytes).first(static_cast<std::size_t>(IntentPacket::FullSize))).ShotHistoryLength == 0,
            "an intent without the block has no history");
    }

    // The receiver: the event's weapon is the shot's, once per sequence.
    void ShotEventReceiver()
    {
        RemoteShotQueue shots;
        Expect(!shots.Next().has_value() && !shots.Active(), "nothing pending, and no events driving, on a new life");
        shots.BeginLife();

        // The Omega shot, then the switch to the Power Beam arriving first:
        // the switch is WeaponSelect's business, the shot keeps its weapon.
        IntentPacket switched = WithShots(205, {{1, 200, Id(BeamType::OmegaCannon)}});
        switched.WeaponSelect = Id(BeamType::PowerBeam);
        shots.Receive(switched);
        Expect(NextWeapon(shots) == Id(BeamType::OmegaCannon), "the shot fires the weapon it left, whatever is held now");

        // The same event again, in a later intent and in an older one.
        shots.Receive(WithShots(206, {{1, 200, Id(BeamType::OmegaCannon)}}));
        shots.Receive(WithShots(201, {{1, 200, Id(BeamType::OmegaCannon)}}));
        shots.Consume();
        Expect(!shots.Next().has_value(), "one event fires once however often it arrives");

        // Rapid fire: each shot keeps its own weapon, in firing order.
        shots.Receive(WithShots(230, {
            {2, 220, Id(BeamType::Missile)}, {3, 224, Id(BeamType::PowerBeam)}, {4, 228, Id(BeamType::Missile)}}));
        Expect(NextWeapon(shots) == Id(BeamType::Missile), "first shot: Missile");
        shots.Consume();
        Expect(NextWeapon(shots) == Id(BeamType::PowerBeam), "second shot: Power Beam");
        shots.Consume();
        Expect(NextWeapon(shots) == Id(BeamType::Missile), "third shot: Missile");
        shots.Consume();

        // A lost intent: the next one still carries the shot.
        shots.Receive(WithShots(242, {{4, 228, Id(BeamType::Missile)}, {5, 240, Id(BeamType::Imperialist)}}));
        Expect(NextWeapon(shots) == Id(BeamType::Imperialist), "a shot whose intent was lost arrives with the next");
        shots.Consume();

        // Out of order: the newer intent first, then an older one carrying a
        // shot the newer skipped. Late, not lost: it is fired, in order.
        shots.Receive(WithShots(260, {{7, 258, Id(BeamType::Judicator)}}));
        shots.Receive(WithShots(250, {{6, 248, Id(BeamType::Magmaul)}}));
        Expect(NextWeapon(shots) == Id(BeamType::Magmaul), "a skipped shot delivered late is fired first");
        shots.Consume();
        Expect(NextWeapon(shots) == Id(BeamType::Judicator), "then the newer one");
        shots.Consume();
        Expect(shots.Stats().Gaps == 1 && shots.Stats().Recovered == 1 && shots.Stats().Lost == 0,
            "and it is counted as recovered, not lost");

        // An event whose trigger never fired here goes stale rather than
        // lending an old weapon to a later shot.
        shots.Receive(WithShots(300, {{8, 300, Id(BeamType::OmegaCannon)}}));
        shots.Receive(WithShots(340, {}));
        Expect(!shots.Next().has_value(), "a stale event is dropped");

        // Death and respawn: a new life starts empty and takes the sender's
        // sequence wherever it is; the old life's shot, repeated by the next
        // intent, is not taken for a new one.
        shots.Receive(WithShots(400, {{9, 399, Id(BeamType::ShockCoil)}}));
        shots.BeginLife();
        Expect(!shots.Next().has_value(), "a new life holds none of the old life's shots");
        shots.Receive(WithShots(410, {{9, 399, Id(BeamType::ShockCoil)}, {10, 409, Id(BeamType::VoltDriver)}}));
        Expect(NextWeapon(shots) == Id(BeamType::VoltDriver), "and takes the new life's");
        Expect(shots.Stats().Abandoned == 1, "the old life's unfired shot is abandoned, once");
        static_cast<void>(shots.Retire());

        // Nonsense from the wire is refused.
        shots.Receive(WithShots(500, {{11, 500, 0x7F}, {0, 500, Id(BeamType::Missile)}}));
        Expect(!shots.Next().has_value(), "an unknown weapon or a zero sequence is ignored");
        Expect(shots.Active(), "a player who sends intents is driven by events, fired or not");
        static_cast<void>(shots.Retire());

        // The charge rides with the shot.
        shots.Receive(WithShots(600, {{12, 600, Id(BeamType::Magmaul), 48}}));
        Expect(shots.Next().has_value() && shots.Next()->Charge == 48, "the event carries the charge it left with");

        shots.Consume();
        Expect(!shots.Next().has_value(), "fired once, gone");
    }

    // The owner's side: a sequence per shot, the last few in every intent.
    void ShotEventSender()
    {
        LocalShotLog log;
        Expect(log.Latest() == nullptr, "nothing fired yet");
        for (std::uint32_t i = 0; i < 6; ++i)
        {
            IntentPacket::ShotEvent shot{};
            shot.Frame = 100 + i;
            shot.WeaponId = Id(i % 2 == 0 ? BeamType::Missile : BeamType::OmegaCannon);
            shot.Charge = static_cast<std::uint8_t>(i);
            shot.AckFrame = 90 + i;
            const std::uint32_t sequence = log.Record(shot);
            Expect(log.Latest() != nullptr && log.Latest()->Sequence == sequence && sequence == i + 1,
                "the projectiles spawned after a shot carry its sequence");
        }
        IntentPacket intent{};
        log.Fill(intent);
        Expect(intent.ShotHistoryLength == IntentPacket::ShotHistoryCount
            && intent.ShotHistory[0].Sequence == 3 && intent.ShotHistory[3].Sequence == 6,
            "the history keeps the newest shots, oldest first");
        Expect(intent.ShotHistory[3].WeaponId == Id(BeamType::OmegaCannon), "newest last");
        log.Reset();
        IntentPacket::ShotEvent after{};
        after.Frame = 200;
        after.WeaponId = Id(BeamType::PowerBeam);
        static_cast<void>(log.Record(after));
        IntentPacket respawned{};
        log.Fill(respawned);
        Expect(respawned.ShotHistoryLength == 1 && respawned.ShotHistory[0].Sequence == 7,
            "a new life empties the history but never reuses a sequence");
        Expect(intent.ShotHistory[3].Charge == 5 && intent.ShotHistory[3].AckFrame == 95,
            "the charge and the world are recorded with the shot");
    }

    // The authority's record of a shooter's shots, for the claims naming them.
    void ShotLedger()
    {
        ShotEventLedger ledger;
        Expect(!ledger.Find(0).has_value() && !ledger.Find(7).has_value(), "nothing named before it is reported");
        ledger.Note({7, 300, Id(BeamType::Missile), 0, 280});
        Expect(ledger.Find(7).has_value() && ledger.Find(7)->AckFrame == 280, "a reported shot is found by its sequence");
        ledger.Note({7, 300, Id(BeamType::Missile), 0, 280});
        Expect(ledger.Find(7).has_value(), "and stays, however many claims name it");
        ledger.Note({7 + ShotEventLedger::Capacity, 900, Id(BeamType::PowerBeam)});
        Expect(!ledger.Find(7).has_value(), "a shot pushed out by newer ones is no longer named");
        ledger.Reset();
        Expect(!ledger.Find(7 + ShotEventLedger::Capacity).has_value(), "a new life names nothing");

        // A late intent's event a whole ledger older shares the newer shot's
        // entry: it must not take its place.
        ledger.Note({100, 500, Id(BeamType::Missile), 0, 480});
        ledger.Note({100 - ShotEventLedger::Capacity, 100, Id(BeamType::PowerBeam), 0, 80});
        Expect(ledger.Find(100).has_value() && ledger.StaleRefused() == 1, "an older shot cannot overwrite a newer one");
        // The same sequence saying something else is noted, never kept.
        ledger.Note({100, 500, Id(BeamType::Judicator), 0, 480});
        Expect(ledger.Find(100)->WeaponId == Id(BeamType::Missile) && ledger.Conflicts() == 1,
            "a contradicting repeat is counted and ignored");
        // Wrapping sequences still order: the shot after 0xFFFFFFFF is newer.
        ShotEventLedger wrap;
        wrap.Note({0xFFFFFFFFU, 1, Id(BeamType::Missile)});
        constexpr auto wrapped = static_cast<std::uint32_t>(0xFFFFFFFFULL + ShotEventLedger::Capacity);
        wrap.Note({wrapped, 2, Id(BeamType::PowerBeam)});
        Expect(wrap.Find(wrapped).has_value(), "a wrapped sequence is newer");
    }

    // Every event received ends fired, stale, pushed out, abandoned or still
    // waiting; every sequence skipped ends recovered, lost or still awaited.
    bool Balanced(const RemoteShotQueue& shots)
    {
        const auto stats = shots.Stats();
        return stats.Received == stats.Fired + stats.Unfired() + stats.Waiting
            && stats.Gaps == stats.Recovered + stats.Lost + stats.Pending;
    }

    // What became of the shots: skipped and lost, late, stale, no room.
    void ShotQueueMetrics()
    {
        // A jump of 19: 18 skipped, counted once. The 16 newest are still
        // awaited, the 2 before them are lost.
        RemoteShotQueue jump;
        jump.Receive(WithShots(100, {{1, 100, Id(BeamType::PowerBeam)}}));
        jump.Consume();
        jump.Receive(WithShots(110, {{20, 110, Id(BeamType::PowerBeam)}}));
        Expect(jump.Stats().Gaps == 18 && jump.Stats().Lost == 2 && jump.Stats().Pending == 16,
            "a long skip is counted once, and only past the window as lost");
        Expect(Balanced(jump), "and the books balance");
        // Later sequences push the awaited ones out of the window.
        jump.Receive(WithShots(120, {{20 + RemoteShotQueue::MissingWindow + 1, 120, Id(BeamType::PowerBeam)}}));
        Expect(jump.Stats().Pending == RemoteShotQueue::MissingWindow && jump.Stats().Lost == 2 + 16
            && Balanced(jump), "skipped shots past the window are lost");

        // The sender's sequence wraps past 0, which it never uses.
        RemoteShotQueue wrap;
        wrap.Receive(WithShots(100, {{0xFFFFFFFFU, 100, Id(BeamType::Missile)}}));
        wrap.Receive(WithShots(101, {{1, 101, Id(BeamType::Missile)}}));
        Expect(wrap.Stats().Gaps == 0 && wrap.Stats().Received == 2, "0 is not a shot skipped");
        RemoteShotQueue wrapGap;
        wrapGap.Receive(WithShots(100, {{0xFFFFFFFEU, 100, Id(BeamType::Missile)}}));
        wrapGap.Receive(WithShots(101, {{2, 101, Id(BeamType::Missile)}}));
        Expect(wrapGap.Stats().Gaps == 2 && wrapGap.Stats().Pending == 2, "0xFFFFFFFF and 1 skipped, not 0");

        // Recovered is not fired: late, and then too late to fire.
        RemoteShotQueue late;
        late.Receive(WithShots(200, {{2, 200, Id(BeamType::Missile)}}));
        late.Consume();
        late.Receive(WithShots(260, {{4, 259, Id(BeamType::Missile)}}));
        late.Receive(WithShots(230, {{3, 228, Id(BeamType::Missile)}}));
        Expect(late.Stats().Recovered == 1, "a reordered intent delivers the skipped shot");
        Expect(NextWeapon(late) == Id(BeamType::Missile) && late.Stats().Stale == 1,
            "but past FreshFrames it is dropped, not fired");
        late.Consume();
        Expect(late.Stats().Fired == 2 && late.Stats().Recovered == 1 && Balanced(late),
            "fired counts what the copy fired, not what arrived");

        // Fired after a newer one: counted, never waited for.
        RemoteShotQueue order;
        order.Receive(WithShots(300, {{5, 300, Id(BeamType::Missile)}}));
        order.Consume();
        order.Receive(WithShots(310, {{7, 310, Id(BeamType::Missile)}}));
        order.Consume();
        order.Receive(WithShots(306, {{6, 305, Id(BeamType::Judicator)}}));
        Expect(NextWeapon(order) == Id(BeamType::Judicator), "the late shot still fires");
        order.Consume();
        Expect(order.Stats().OutOfOrder == 1 && order.Stats().Fired == 3 && Balanced(order),
            "and is counted as fired out of order");

        // More waiting than the queue holds: the oldest makes room.
        RemoteShotQueue full;
        for (std::uint32_t i = 1; i <= RemoteShotQueue::Capacity + 1; ++i)
        {
            full.Receive(WithShots(300 + i, {{i, 300 + i, Id(BeamType::Missile)}}));
        }
        Expect(full.Stats().Overflow == 1 && Balanced(full), "a full queue counts what it pushed out");

        // A life: the first one seen keeps what arrived before it (its own
        // shots); a later one abandons what the last left unfired.
        full.BeginLife();
        Expect(full.Stats().Waiting == RemoteShotQueue::Capacity && full.Stats().Abandoned == 0,
            "the first life keeps its shots");
        full.BeginLife();
        Expect(full.Stats().Waiting == 0 && full.Stats().Abandoned == RemoteShotQueue::Capacity && Balanced(full),
            "a new life abandons the old one's");
        const auto retired = full.Retire();
        Expect(retired.Overflow == 1 && retired.Abandoned == RemoteShotQueue::Capacity,
            "an occupant leaving takes their statistics");
        Expect(!full.Next().has_value() && full.Stats().Received == 0 && full.Stats().Overflow == 0,
            "and the next occupant's start from nothing");

        // A shot still queued once FreshFrames have passed is overdue: nothing
        // asked for it, which is not a shot in flight at the end of a run.
        RemoteShotQueue stuck;
        stuck.Receive(WithShots(700, {{1, 700, Id(BeamType::Missile)}}));
        Expect(stuck.Stats().Waiting == 1 && stuck.Stats().Overdue == 0, "a fresh shot waiting is the tail");
        stuck.Receive(WithShots(700 + RemoteShotQueue::FreshFrames + 1, {}));
        Expect(stuck.Stats().Waiting == 1 && stuck.Stats().Overdue == 1, "an old one is overdue");

        // Shots fired before this machine was watching are where the
        // sequence is, not shots to fire or count.
        RemoteShotQueue joined;
        joined.Receive(WithShots(1000, {{40, 900, Id(BeamType::Missile)}, {41, 995, Id(BeamType::PowerBeam)}}));
        Expect(joined.Stats().Received == 1 && NextWeapon(joined) == Id(BeamType::PowerBeam) && joined.Stats().Gaps == 0,
            "a joiner takes only the shot still fresh");
    }

    IntentPacket WithBombs(std::uint32_t frame, std::initializer_list<IntentPacket::Bomb> bombs)
    {
        IntentPacket packet{};
        packet.Frame = frame;
        packet.HasBombs = true;
        for (const IntentPacket::Bomb& bomb : bombs)
        {
            packet.Bombs[packet.BombsLength++] = bomb;
        }
        return packet;
    }

    // A copy's bombs are the owner's reported ones: laid once, detonated
    // when no longer reported, never from an out-of-date report.
    void BombState()
    {
        using MphRead::Mods::Network::RemoteBombState;
        using OpenTK::Mathematics::Vector3;

        IntentPacket sent = WithBombs(50, {{7, Vector3(1, 2, 3)}, {8, Vector3(4, 5, 6)}});
        std::vector<std::uint8_t> bytes(IntentPacket::BombFullSize);
        sent.Write(bytes);
        const IntentPacket read = IntentPacket::Read(bytes);
        Expect(read.HasBombs && read.BombsLength == 2 && read.Bombs[1].Sequence == 8
            && Near(read.Bombs[1].Position.Y, 5.0F), "the standing bombs ride the intent");
        IntentPacket none{};
        none.HasBombs = true;
        none.Write(bytes);
        Expect(IntentPacket::Read(bytes).HasBombs && IntentPacket::Read(bytes).BombsLength == 0,
            "no bombs is a report too");
        IntentPacket gone = WithBombs(60, {{9, Vector3(1, 1, 1)}, {10, Vector3(2, 2, 2), true}});
        gone.Write(bytes);
        Expect(!IntentPacket::Read(bytes).Bombs[0].Gone && IntentPacket::Read(bytes).Bombs[1].Gone,
            "a bomb just gone is still reported, as gone");
        std::vector<std::uint8_t> older(IntentPacket::ShotFullSize);
        sent.Write(older);
        Expect(!IntentPacket::Read(older).HasBombs, "a payload without the block reports nothing");

        RemoteBombState state;
        Expect(!state.Active(), "a player who never reported lays its own");
        state.Receive(WithBombs(100, {{1, Vector3(1, 0, 0)}, {2, Vector3(2, 0, 0)}}));
        auto plan = state.Reconcile({});
        Expect(state.Active() && plan.LayCount == 2 && plan.Lay[0].Sequence == 1 && plan.DetonateCount == 0,
            "reported bombs are laid, oldest first");
        state.Laid(1);
        state.Laid(2);
        const std::uint32_t both[] = {1, 2};
        plan = state.Reconcile(both);
        Expect(plan.LayCount == 0 && plan.DetonateCount == 0, "and once");

        // The owner's second bomb went off: the copy's follows.
        state.Receive(WithBombs(110, {{1, Vector3(1, 0, 0)}}));
        plan = state.Reconcile(both);
        Expect(plan.DetonateCount == 1 && plan.Detonate[0] == 2, "a bomb no longer reported is detonated");
        // An older intent still listing it changes nothing.
        state.Receive(WithBombs(105, {{1, Vector3(1, 0, 0)}, {2, Vector3(2, 0, 0)}, {3, Vector3(3, 0, 0)}}));
        plan = state.Reconcile(both);
        Expect(plan.DetonateCount == 1 && plan.LayCount == 0, "an out-of-date report is ignored");

        // A bomb detonated here early (it never is, by touch) is not laid again.
        const std::uint32_t one[] = {1};
        state.Receive(WithBombs(120, {{1, Vector3(1, 0, 0)}, {2, Vector3(2, 0, 0)}}));
        plan = state.Reconcile(one);
        Expect(plan.LayCount == 0, "a sequence laid once is never laid again");

        // A bomb that cannot be placed yet is planned again until it is.
        state.Receive(WithBombs(130, {{3, Vector3(3, 0, 0)}}));
        plan = state.Reconcile({});
        Expect(plan.LayCount == 1 && plan.Lay[0].Sequence == 3, "a bomb not yet placed is still planned");
        plan = state.Reconcile({});
        Expect(plan.LayCount == 1, "until Laid records it");

        // A bomb that moved on its owner's machine is where they say.
        state.Receive(WithBombs(135, {{3, Vector3(7, 0, 0)}}));
        Expect(state.StandingAt(3).has_value() && Near(state.StandingAt(3)->X, 7.0F) && !state.StandingAt(2).has_value(),
            "a standing bomb's place is the owner's");

        // A bomb that stood for a frame: laid here as it goes off.
        state.Laid(3);
        state.Receive(WithBombs(140, {{4, Vector3(4, 0, 0), true}}));
        plan = state.Reconcile({});
        Expect(plan.LayCount == 1 && plan.Lay[0].Gone, "a bomb seen only gone is still laid, to go off");
        const std::uint32_t four[] = {4};
        state.Laid(4);
        plan = state.Reconcile(four);
        Expect(plan.DetonateCount == 1 && plan.LayCount == 0, "and a gone one held here is detonated");

        state.Reset();
        Expect(!state.Active(), "a new occupant starts over");
    }

    IntentPacket Transport(const NativeTouchState::Reported& report)
    {
        IntentPacket packet{};
        packet.SlotGeneration = 2;
        packet.LifeId = 3;
        packet.SetTouchReport(report); // same conversion CaptureIntent uses
        std::vector<std::uint8_t> bytes(IntentPacket::FullSize);
        packet.Write(bytes);
        return IntentPacket::Read(bytes);
    }

    void Receive(TouchInputAdapter& remote, const IntentPacket& packet)
    {
        // The conversion and adapter called by ApplyIntent/ModSetReportedTouch.
        remote.BeginStep();
        remote.ApplyReported(packet.TouchReport(), packet.SlotGeneration, packet.LifeId);
    }

    Boost::Result Advance(const TouchInputAdapter& input, Boost::State& state,
        Boost::SampleLatch& latch, bool held)
    {
        const auto& touch = input.State();
        return Boost::AdvanceSample(state, {touch.Down, touch.Continued, touch.Delta4X, touch.Delta4Y, held},
            {4, 20, false}, latch, input.Sample().Identity());
    }

    Rules::PlanarDelta Roll(const TouchInputAdapter& input)
    {
        const auto& touch = input.State();
        return Rules::TouchRollStep(touch.Delta4X, touch.Delta4Y, Rules::TouchRollPerDsPixel,
            0.6F, 0.8F, 0.8F, -0.6F, input.Sample().RollShare());
    }

    void OwnerAuthorityParity()
    {
        TouchInputAdapter owner{}, authority{};
        Boost::State local{false, true, 7}, remote = local;
        Boost::SampleLatch localLatch{}, remoteLatch{};
        int localBoosts = 0;
        int remoteBoosts = 0;
        // SPACE charge -> swipe -> keep holding -> release; lift and re-arm.
        const std::vector<std::tuple<bool, int, int, bool>> trace{
            {true, 10, 10, true}, {true, 120, 10, true}, {true, 125, 15, true},
            {true, 130, 15, false}, {false, 0, 0, true}, {true, 10, 10, true},
            {true, 115, 10, false}};
        NativeTouchState reference{};
        for (auto [down, x, y, held] : trace)
        {
            reference.Update(down, x, y);
            Rules::PlanarDelta localSum{}, remoteSum{};
            for (int step = 0; step < 2; ++step)
            {
                HostTouch::Publish(down, x, y);
                owner.Step({}); // real host producer/adapter, not a toy clock
                const auto packet = Transport(owner.Sample().Report());
                Receive(authority, packet);
                Expect(authority.State().Report() == owner.State().Report(), "authority sees the owner's native state");
                Expect(authority.Sample().NewNativeSampleThisStep() == owner.Sample().NewNativeSampleThisStep(),
                    "wire sample boundaries match the owner on both substeps");
                auto a = Advance(owner, local, localLatch, held);
                auto b = Advance(authority, remote, remoteLatch, held);
                Expect(a.Branch == b.Branch && a.Boost == b.Boost && a.ChargeSpent == b.ChargeSpent,
                    "owner and authority produce the same branch/event sequence");
                Expect(local.Boosting == remote.Boosting && local.CanTouchBoost == remote.CanTouchBoost && local.Charge == remote.Charge,
                    "owner and authority preserve the same boost state and charge");
                localBoosts += a.Boost == Boost::Fired::TouchBoost;
                remoteBoosts += b.Boost == Boost::Fired::TouchBoost;
                const auto lr = Roll(owner);
                const auto rr = Roll(authority);
                localSum.X += lr.X; localSum.Z += lr.Z;
                remoteSum.X += rr.X; remoteSum.Z += rr.Z;
            }
            const auto native = Rules::TouchRoll(reference.Delta4X, reference.Delta4Y,
                Rules::TouchRollPerDsPixel, 0.6F, 0.8F, 0.8F, -0.6F);
            Expect(Near(localSum.X, native.X) && Near(localSum.Z, native.Z)
                && Near(remoteSum.X, native.X) && Near(remoteSum.Z, native.Z),
                "both owner and authority integrate one native roll per sample pair");
            if (!down) local.Boosting = remote.Boosting = false; // cooldown ended before next gesture
        }
        Expect(localBoosts == 2 && remoteBoosts == 2, "both gestures fire one TouchBoost each on each machine");
        HostTouch::Withdraw();
    }

    void PacketFaults()
    {
        TouchInputAdapter remote{};
        Boost::State state{false, true, 7};
        Boost::SampleLatch latch{};
        auto first = Transport({true, true, 120, -20, 10, false});
        Receive(remote, first);
        auto r = Advance(remote, state, latch, true);
        const auto half = Roll(remote);
        Expect(r.Boost == Boost::Fired::TouchBoost && state.Charge == 7, "first report fires TouchBoost");
        Receive(remote, first);
        r = Advance(remote, state, latch, false);
        Expect(remote.Sample().RollShare() == 0 && r.Boost == Boost::Fired::None && state.Charge == 7,
            "duplicate report adds no roll or boost and cannot release Shoulder");
        for (int heldSteps = 0; heldSteps < 8; ++heldSteps)
        {
            remote.BeginStep(); // no new intent available this simulation step
            r = Advance(remote, state, latch, true);
            Expect(remote.Sample().RollShare() == 0 && r.Boost == Boost::Fired::None && state.Charge == 7,
                "packet loss cannot reuse an impulse or change a latched touch branch");
        }
        auto second = Transport({true, true, 120, -20, 10, true});
        Receive(remote, second);
        r = Advance(remote, state, latch, false);
        const auto otherHalf = Roll(remote);
        const auto native = Rules::TouchRoll(120, -20, Rules::TouchRollPerDsPixel, 0.6F, 0.8F, 0.8F, -0.6F);
        Expect(Near(half.X + otherHalf.X, native.X) && Near(half.Z + otherHalf.Z, native.Z)
            && r.Boost == Boost::Fired::None && state.Charge == 7, "delayed sibling completes exactly one impulse and stays latched");
        for (const auto& stale : {second, first, Transport({true, true, -120, 0, 9, true})})
        {
            Receive(remote, stale);
            r = Advance(remote, state, latch, false);
            Expect(remote.Sample().RollShare() == 0 && remote.State().Delta4X == 120
                && r.Boost == Boost::Fired::None && state.Charge == 7,
                "duplicate/stale/reordered reports cannot replace or reconsume the sample");
        }
        auto next = Transport({true, true, 120, -20, 11, false});
        Receive(remote, next); // same deltas, different sample
        r = Advance(remote, state, latch, true);
        Expect(remote.Sample().NewNativeSampleThisStep() && r.Branch == Rules::BoostBranch::Shoulder && state.Charge == 8,
            "identical deltas with a new identity re-arbitrate and resume Shoulder");
        // Drop sample 11's sibling, then accept a lift and a new swipe.
        Receive(remote, Transport({false, false, 0, 0, 12, false}));
        (void)Advance(remote, state, latch, true);
        Expect(state.CanTouchBoost, "a dropped sibling does not suppress the next sample's re-arm");
        state.Boosting = false;
        // First substep lost: the second is still a new sample and may boost.
        Receive(remote, Transport({true, true, 120, 0, 13, true}));
        r = Advance(remote, state, latch, true);
        Expect(r.Boost == Boost::Fired::TouchBoost && remote.Sample().RollShare() == 0.5F,
            "losing the first substep delays a qualifying boost by at most one delivered sibling");
        Receive(remote, Transport({true, true, -120, 0, 13, false}));
        r = Advance(remote, state, latch, true);
        Expect(remote.Sample().RollShare() == 0 && remote.State().Delta4X == 120 && r.Boost == Boost::Fired::None,
            "a late first substep cannot rewind the sibling or fire again");

        TouchInputAdapter wrap{};
        Receive(wrap, Transport({true, true, 1, 0, std::numeric_limits<std::uint32_t>::max(), false}));
        Receive(wrap, Transport({true, true, 2, 0, 0, false}));
        Expect(wrap.Sample().NewNativeSampleThisStep() && wrap.State().Delta4X == 2, "sample sequence wraps forward");
        Receive(wrap, Transport({true, true, 1, 0, std::numeric_limits<std::uint32_t>::max(), true}));
        Expect(wrap.Sample().RollShare() == 0 && wrap.State().Delta4X == 2, "pre-wrap sample is stale");
        auto respawn = Transport({true, true, 120, 0, 0, false});
        respawn.LifeId = 4;
        Receive(remote, respawn);
        state = {false, true, 7};
        r = Advance(remote, state, latch, true);
        Expect(remote.Sample().NewNativeSampleThisStep() && r.Boost == Boost::Fired::TouchBoost,
            "a new life gets a new identity even if the sequence resets");
        respawn.SlotGeneration = 3;
        Receive(remote, respawn);
        Expect(remote.Sample().NewNativeSampleThisStep(), "a reused slot starts a new sample stream");
    }
}

int main()
{
    try
    {
        IntentPacket touch{};
        touch.ChargeLevel = 7;
        touch.BoostDamage = 30;
        touch.SetTouchReport({true, true, -300, 91, 0xFEDCBA98U, true});
        std::vector<std::uint8_t> bytes(IntentPacket::FullSize);
        touch.Write(bytes);
        const IntentPacket full = IntentPacket::Read(bytes);
        Expect(full.HasState && full.ChargeLevel == 7 && full.BoostDamage == 30, "the state block survives");
        Expect(full.HasTouch() && full.TouchFlags == touch.TouchFlags
            && full.TouchDelta4X == -300 && full.TouchDelta4Y == 91, "the touch block round-trips, signed");
        Expect(full.HasTouchSample() && full.TouchReport() == touch.TouchReport(), "32-bit sample identity and sibling bit round-trip");

        // Protocol 15's touch payload and partial sequence tails retain touch
        // but must never claim to contain a complete sample identity.
        for (int size = IntentPacket::Size + IntentPacket::StateSize + IntentPacket::LegacyTouchSize;
            size < IntentPacket::FullSize; ++size)
        {
            const auto legacy = IntentPacket::Read(std::span(bytes).first(static_cast<std::size_t>(size)));
            Expect(legacy.HasTouch() && !legacy.HasTouchSample() && legacy.TouchDelta4X == -300,
                "legacy or truncated identity keeps signed touch without inventing a sequence");
        }

        std::vector<std::uint8_t> stateOnly(IntentPacket::Size + IntentPacket::StateSize, 0xFF);
        touch.Write(stateOnly);
        Expect(IntentPacket::Read(stateOnly).ChargeLevel == 7 && !IntentPacket::Read(stateOnly).HasTouch(),
            "writing a state-only intent omits touch flags");

        // What a demo recorded before the touch block holds: the state block
        // with its fourth byte zero, and nothing after it.
        bytes.resize(static_cast<std::size_t>(IntentPacket::Size + IntentPacket::StateSize));
        bytes[static_cast<std::size_t>(IntentPacket::Size + 3)] = 0;
        const IntentPacket older = IntentPacket::Read(bytes);
        Expect(older.HasState && older.ChargeLevel == 7, "a state-only payload still carries the state");
        Expect(!older.HasTouch() && older.TouchFlags == 0 && older.TouchDelta4X == 0 && older.TouchDelta4Y == 0,
            "and reads as no contact");

        bytes.resize(static_cast<std::size_t>(IntentPacket::Size));
        const IntentPacket bare = IntentPacket::Read(bytes);
        Expect(!bare.HasState && !bare.HasTouch(), "a bare intent has neither block");
        ShotEventWire();
        ShotEventReceiver();
        ShotEventSender();
        ShotLedger();
        ShotQueueMetrics();
        BombState();
        OwnerAuthorityParity();
        PacketFaults();
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "IntentTouchPayload: %s\n", e.what());
        return 1;
    }
    std::puts("IntentTouchPayload: ok");
    return 0;
}
