// The Morph Ball touch block of IntentPacket: what an owner reports so the
// authority can run the same touch roll and touch/shoulder boost branches.
#include "Mods/Network/Protocol/NetProtocol.hpp"
#include "Mods/Network/Gameplay/Weapons/LocalShotLog.hpp"
#include "Mods/Network/Gameplay/Claims/Common/ClaimResend.hpp"
#include "Mods/Network/Protocol/ConnectionSecret.hpp"
#include "Mods/Network/Gameplay/Claims/Hit/ClaimDamageBound.hpp"
#include "Mods/Network/Gameplay/StatusEffects/TimedEffectSync.hpp"
#include "Mods/Network/Gameplay/Pickups/ItemAuthority.hpp"
#include "Mods/Network/Gameplay/Pickups/PickupReach.hpp"
#include "Mods/Network/Gameplay/Pickups/PowerUpLedger.hpp"
#include "Mods/Network/Gameplay/Pickups/PredictionBook.hpp"
#include "Mods/Network/Protocol/PowerUpSync.hpp"
#include "Mods/Network/Transport/InboxShedding.hpp"
#include "Mods/Network/Gameplay/Pickups/PredictionMatch.hpp"
#include "Mods/Network/Gameplay/Bombs/RemoteBombState.hpp"
#include "Mods/Network/Gameplay/Bombs/NetBombs.hpp"
#include "Mods/Network/Gameplay/Weapons/RemoteShotQueue.hpp"
#include "Mods/Network/Gameplay/Weapons/ShotEventLedger.hpp"
#include "../Mods/Input/HostTouch.hpp"
#include "../Mods/Input/TouchInputAdapter.hpp"
#include "../Entities/Players/MorphBallBoostStateMachine.hpp"
#include "../Entities/Players/MorphBallTouchRules.hpp"

#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <limits>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <vector>

using MphRead::Mods::Network::IntentPacket;
using MphRead::Mods::Network::ReconcileTimedEffect;
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
        std::vector<std::uint8_t> bytes(IntentPacket::MaxWireSize);
        shot.Write(bytes);
        const IntentPacket noRay = IntentPacket::Read(std::span(bytes).first(shot.WireSize()));
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

        claim.Flags = HitClaimPacket::FlagDoubled;
        claim.Write(claimBytes);
        Expect(HitClaimPacket::Read(claimBytes).Flags == HitClaimPacket::FlagDoubled, "a doubled hit says so");

        shot.HasShot = true;
        shot.ShotOrigin = OpenTK::Mathematics::Vector3(1, 2, 3);
        shot.ShotDirection = OpenTK::Mathematics::Vector3(0, 0, -1);
        shot.Write(bytes);
        const IntentPacket withRay = IntentPacket::Read(std::span(bytes).first(shot.WireSize()));
        Expect(withRay.HasShot && Near(withRay.ShotOrigin.Y, 2.0F) && Near(withRay.ShotDirection.Z, -1.0F)
            && withRay.ShotHistory[1].Sequence == 42,
            "this frame's ray and the history share the payload");

        // A Shock Coil frame: a ray with no event.
        IntentPacket coil{};
        coil.HasShot = true;
        coil.ShotOrigin = OpenTK::Mathematics::Vector3(1, 2, 3);
        coil.ShotDirection = OpenTK::Mathematics::Vector3(1, 0, 0);
        coil.Write(bytes);
        const IntentPacket coilRead = IntentPacket::Read(std::span(bytes).first(coil.WireSize()));
        Expect(coilRead.HasShot && coilRead.ShotHistoryLength == 0, "continuous fire keeps its ray without an event");

        // Only what there is: an idle intent is the fixed part and one byte.
        IntentPacket idle{};
        Expect(idle.WireSize() == static_cast<std::size_t>(IntentPacket::FullSize) + 1, "an idle intent carries no sections");
        idle.HasBombs = true;
        Expect(idle.WireSize() == static_cast<std::size_t>(IntentPacket::FullSize) + 3,
            "a report of no bombs is two bytes");
        Expect(shot.WireSize() == static_cast<std::size_t>(IntentPacket::FullSize) + 1 + IntentPacket::ShotSize
            + 1 + 2 * IntentPacket::ShotEventSize, "a ray and two shots, and nothing else");
        Expect(static_cast<std::size_t>(IntentPacket::MaxWireSize) < 388, "never larger than the fixed layout was");

        // A section cut short is absent, never half read; a corrupt count
        // is clamped, and then only read if every entry it names is there.
        shot.Write(bytes);
        const std::size_t whole = shot.WireSize();
        Expect(IntentPacket::Read(std::span(bytes).first(whole - 1)).ShotHistoryLength == 0,
            "a history missing its last byte is not read");
        Expect(IntentPacket::Read(std::span(bytes).first(static_cast<std::size_t>(IntentPacket::FullSize))).ShotHistoryLength == 0,
            "an intent without the sections has no history");
        bytes[static_cast<std::size_t>(IntentPacket::FullSize + 1 + IntentPacket::ShotSize)] = 0xFF;
        Expect(IntentPacket::Read(bytes).ShotHistoryLength == IntentPacket::ShotHistoryCount,
            "a corrupt history count is clamped");
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
        log.Fill(intent, 106);
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
        log.Fill(respawned, 200);
        Expect(respawned.ShotHistoryLength == 1 && respawned.ShotHistory[0].Sequence == 7,
            "a new life empties the history but never reuses a sequence");
        Expect(intent.ShotHistory[3].Charge == 5 && intent.ShotHistory[3].AckFrame == 95,
            "the charge and the world are recorded with the shot");
        // Repeated only while a receiver could still fire it.
        IntentPacket later{};
        log.Fill(later, 200 + LocalShotLog::RepeatFrames);
        Expect(later.ShotHistoryLength == 1, "a shot is repeated for RepeatFrames");
        log.Fill(later, 201 + LocalShotLog::RepeatFrames);
        Expect(later.ShotHistoryLength == 0, "and then no longer sent");

        // A claim's resend is timed from its last send.
        using MphRead::Mods::Network::ClaimResendDue;
        Expect(ClaimResendDue(0, 6, 0, 0, 10), "first chance: sent");
        Expect(!ClaimResendDue(1, 6, 9, 0, 10) && ClaimResendDue(1, 6, 10, 0, 10), "then once the interval has passed");
        Expect(ClaimResendDue(1, 6, 13, 0, 10), "a send held back by a full packet goes on the next chance");
        Expect(ClaimResendDue(2, 6, 17, 12, 5), "a shorter interval measured since applies at once");
        Expect(!ClaimResendDue(6, 6, 100, 0, 10), "never past the most sends");
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

        RemoteShotQueue future;
        future.Receive(WithShots(1100, {{1, 1101, Id(BeamType::Missile)}}));
        Expect(future.Stats().Received == 0 && !future.Next().has_value(),
            "an event later than its carrying sender frame is rejected");

        RemoteShotQueue ineligible;
        ineligible.Receive(WithShots(1200, {{1, 1200, Id(BeamType::Missile)}}));
        ineligible.RejectIneligible();
        Expect(ineligible.Stats().Ineligible == 1 && ineligible.Stats().Waiting == 0
            && Balanced(ineligible),
            "an authority-rejected shot is counted once and preserves queue accounting");
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
        std::vector<std::uint8_t> bytes(IntentPacket::MaxWireSize);
        sent.Write(bytes);
        const IntentPacket read = IntentPacket::Read(std::span(bytes).first(sent.WireSize()));
        Expect(read.HasBombs && read.BombsLength == 2 && read.Bombs[1].Sequence == 8
            && Near(read.Bombs[1].Position.Y, 5.0F), "the standing bombs ride the intent");
        IntentPacket none{};
        none.HasBombs = true;
        none.Write(bytes);
        Expect(IntentPacket::Read(std::span(bytes).first(none.WireSize())).HasBombs
            && IntentPacket::Read(std::span(bytes).first(none.WireSize())).BombsLength == 0,
            "no bombs is a report too");
        IntentPacket gone = WithBombs(60, {{9, Vector3(1, 1, 1)}, {10, Vector3(2, 2, 2), true}});
        gone.Write(bytes);
        Expect(!IntentPacket::Read(bytes).Bombs[0].Gone && IntentPacket::Read(bytes).Bombs[1].Gone,
            "a bomb just gone is still reported, as gone");
        IntentPacket quiet{};
        quiet.Write(bytes);
        Expect(!IntentPacket::Read(std::span(bytes).first(quiet.WireSize())).HasBombs,
            "an intent without the section reports nothing");

        RemoteBombState state;
        Expect(!state.Active(), "a player who never reported lays its own");
        Expect(!state.Receive(quiet), "an intent without a bomb section is not a complete bomb report");
        Expect(state.Receive(WithBombs(100, {{1, Vector3(1, 0, 0)}, {2, Vector3(2, 0, 0)}})),
            "the first complete bomb report is accepted");
        auto plan = state.Reconcile({});
        Expect(state.Active() && plan.LayCount == 2 && plan.Lay[0].Sequence == 1 && plan.DetonateCount == 0,
            "reported bombs are laid, oldest first");
        state.Laid(1);
        state.Laid(2);
        const std::uint32_t both[] = {1, 2};
        plan = state.Reconcile(both);
        Expect(plan.LayCount == 0 && plan.DetonateCount == 0, "and once");

        // The owner's second bomb went off: the copy's follows.
        Expect(state.Receive(WithBombs(110, {{1, Vector3(1, 0, 0)}})),
            "a newer complete bomb report is accepted");
        plan = state.Reconcile(both);
        Expect(plan.DetonateCount == 1 && plan.Detonate[0] == 2, "a bomb no longer reported is detonated");
        // An older intent still listing it changes nothing.
        Expect(!state.Receive(WithBombs(105, {{1, Vector3(1, 0, 0)}, {2, Vector3(2, 0, 0)}, {3, Vector3(3, 0, 0)}})),
            "a stale bomb report is rejected");
        plan = state.Reconcile(both);
        Expect(plan.DetonateCount == 1 && plan.LayCount == 0, "an out-of-date report is ignored");

        // A bomb detonated here early (it never is, by touch) is not laid again.
        const std::uint32_t one[] = {1};
        Expect(state.Receive(WithBombs(120, {{1, Vector3(1, 0, 0)}, {2, Vector3(2, 0, 0)}})),
            "a newer bomb report remains accepted");
        plan = state.Reconcile(one);
        Expect(plan.LayCount == 0, "a sequence laid once is never laid again");

        // A bomb that cannot be placed yet is planned again until it is.
        Expect(state.Receive(WithBombs(130, {{3, Vector3(3, 0, 0)}})),
            "a later report for a not-yet-placeable bomb is accepted");
        plan = state.Reconcile({});
        Expect(plan.LayCount == 1 && plan.Lay[0].Sequence == 3, "a bomb not yet placed is still planned");
        plan = state.Reconcile({});
        Expect(plan.LayCount == 1, "until Laid records it");

        // A bomb that moved on its owner's machine is where they say.
        state.Receive(WithBombs(135, {{3, Vector3(7, 0, 0)}}));
        Expect(state.StandingAt(3).has_value() && Near(state.StandingAt(3)->X, 7.0F) && !state.StandingAt(2).has_value(),
            "a standing bomb's place is the owner's");

        MphRead::Mods::Network::NetBombs::Forget(1);
        MphRead::Mods::Network::NetBombs::Receive(1, WithBombs(200, {{41, Vector3(4, 5, 6)}}));
        const auto networkPlace = MphRead::Mods::Network::NetBombs::ReportedPosition(1, 41);
        Expect(networkPlace.has_value() && Near(networkPlace->X, 4.0F)
            && Near(networkPlace->Y, 5.0F) && Near(networkPlace->Z, 6.0F)
            && !MphRead::Mods::Network::NetBombs::ReportedPosition(-1, 41).has_value()
            && !MphRead::Mods::Network::NetBombs::ReportedPosition(1, 0).has_value(),
            "runtime bomb replication exposes only a valid standing sequence and its exact reported XYZ");
        MphRead::Mods::Network::NetBombs::Forget(1);

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

    void ConnectionIdentity()
    {
        using MphRead::Mods::Network::ConnectionSecret;
        using MphRead::Mods::Network::HelloPacket;
        using MphRead::Mods::Network::MayRebind;
        ConnectionSecret held{};
        held.fill(0x5A);
        ConnectionSecret other = held;
        other[15] ^= 1U;
        Expect(MayRebind(held, held), "the secret a slot was opened with moves it");
        Expect(!MayRebind(held, other), "one bit wrong does not");
        Expect(!MayRebind(ConnectionSecret{}, ConnectionSecret{}), "an unset secret proves nothing");

        HelloPacket hello{};
        hello.Protocol = 28;
        hello.Slot = 3;
        hello.ClientId = 0xA1B2C3D4U;
        hello.Secret = held;
        std::vector<std::uint8_t> bytes(HelloPacket::Size);
        hello.Write(bytes);
        const HelloPacket read = HelloPacket::Read(bytes);
        Expect(read.Protocol == 28 && read.Slot == 3 && read.ClientId == 0xA1B2C3D4U && read.Secret == held,
            "a hello reads back as written");
        const HelloPacket probe = HelloPacket::Read(std::span(bytes).first(2));
        Expect(probe.Protocol == 28 && probe.ClientId == 0 && probe.Slot == 3
            && !MphRead::Mods::Network::IsSet(probe.Secret), "a probe's hello leaves the rest unset");
    }

    void InboxShedding()
    {
        using MphRead::Mods::Network::PacketType;
        using MphRead::Mods::Network::ShedIndex;
        const auto type = [](PacketType t) { return t; };
        const std::vector<PacketType> mixed{ PacketType::Welcome, PacketType::MatchState, PacketType::Snapshot,
            PacketType::Intent };
        const auto snapshot = ShedIndex(mixed, type);
        Expect(snapshot.has_value() && *snapshot == 2,
            "a full queue selects the oldest replaceable snapshot before a welcome");
        const std::vector<PacketType> control{ PacketType::HitVerdict, PacketType::MapChange };
        Expect(!ShedIndex(control, type), "important packets are not implicit overflow victims");
        Expect(!ShedIndex(std::vector<PacketType>{}, type), "an empty queue has no replacement candidate");
    }

    void PredictionMatching()
    {
        using MphRead::Mods::Network::AnsweredShot;
        using MphRead::Mods::Network::MatchPrediction;
        using MphRead::Mods::Network::PredictedShot;
        // Power Beam (bucket 0) fired at 10 and 12, a Missile (bucket 2) at 11.
        const std::vector<PredictedShot> waiting{ { 0, 10, false }, { 2, 11, false }, { 0, 12, false } };
        Expect(MatchPrediction(waiting, AnsweredShot{ 2, std::nullopt }) == 1,
            "a Missile hit retires the Missile, not the older Power Beam shot");
        Expect(MatchPrediction(waiting, AnsweredShot{ 0, std::uint8_t{ 12 } }) == 2,
            "a named launch frame picks that very shot");
        Expect(MatchPrediction(waiting, AnsweredShot{ 0, std::uint8_t{ 99 } }) == 0,
            "an unknown launch frame falls back to the oldest of the weapon");
        Expect(MatchPrediction(waiting, AnsweredShot{ 5, std::nullopt }) == 0,
            "a weapon with nothing waiting falls back to the oldest");
        const std::vector<PredictedShot> refused{ { 2, 11, true }, { 2, 13, false } };
        Expect(MatchPrediction(refused, AnsweredShot{ 2, std::uint8_t{ 11 } }) == 1,
            "a refused claim is never the answer");
        Expect(MatchPrediction(std::vector<PredictedShot>{}, AnsweredShot{ 0, std::nullopt }) == 0,
            "nothing waiting is offset zero");
    }

    // One client's view of one power-up: the effect it shows, its book of
    // pickups the authority has not ruled on, and what the snapshots say --
    // driven through every order the answers can come in.
    struct PickupClient
    {
        using PowerUp = MphRead::Mods::Network::PowerUp;
        MphRead::Mods::Network::PredictionBook Book{};
        PowerUp Kind = PowerUp::DoubleDamage;
        std::uint16_t Local = 0;
        std::uint8_t SeenSequence = 0;
        std::uint16_t Life = 1;
        bool Alive = true;
        std::uint32_t Now = 1000;

        void Step(std::uint32_t frames)
        {
            Now += frames;
            Local = static_cast<std::uint16_t>(Local > frames ? Local - frames : 0U);
        }
        void Predict(std::uint16_t claim)
        {
            Book.Add(claim, Kind, static_cast<std::int16_t>(claim), 1, Life, Now, Local);
            Local = MphRead::Mods::Network::PowerUpFrames;
        }
        void Answer(std::uint16_t claim, bool granted, std::uint8_t sequence = 0)
        {
            if (const auto rollback = Book.Settle(claim, granted, sequence, Now, Life, Alive))
            {
                Local = rollback->Frames;
            }
        }
        void Snapshot(std::uint8_t sequence, std::uint16_t remaining)
        {
            using namespace MphRead::Mods::Network;
            switch (ReconcilePowerUp(PowerUpReport{ sequence, remaining }, SeenSequence, Local, Book.Awaiting(Kind), Alive))
            {
            case PowerUpStep::Start:
            case PowerUpStep::Retime:
            case PowerUpStep::Resume:
                Local = remaining;
                break;
            case PowerUpStep::End:
                Local = 0;
                break;
            case PowerUpStep::Keep:
                break;
            }
            SeenSequence = sequence;
            Book.Seen(Kind, sequence);
        }
        // A double damage of 900 frames already running under grant 3.
        void Holding()
        {
            Local = 900;
            Snapshot(3, 900);
        }
        [[nodiscard]] bool Near(std::uint16_t expected) const
        {
            return Local + 1 >= expected && Local <= expected + 1;
        }
    };

    void PredictionOrders()
    {
        {
            // A second pickup refused while the first grant still runs.
            PickupClient client;
            client.Holding();
            client.Predict(1);
            client.Step(30);
            client.Answer(1, false);
            Expect(client.Near(870), "a refused pickup puts the running grant back, less the time since");
            client.Snapshot(3, 870);
            Expect(client.Near(870), "and the snapshots agree with it");
        }
        {
            // A then B; A refused, then B refused.
            PickupClient client;
            client.Holding();
            client.Predict(1);
            client.Step(10);
            client.Predict(2);
            client.Step(10);
            client.Answer(1, false);
            Expect(client.Local == 1790, "an earlier refusal leaves a later pickup's effect alone");
            client.Step(10);
            client.Answer(2, false);
            Expect(client.Near(870), "and the later refusal goes back to what ran before either");
        }
        {
            // A then B; B refused, then A refused.
            PickupClient client;
            client.Holding();
            client.Predict(1);
            client.Step(10);
            client.Predict(2);
            client.Step(10);
            client.Answer(2, false);
            Expect(client.Near(1780), "a later refusal goes back to the earlier pickup, still waiting");
            client.Step(10);
            client.Answer(1, false);
            Expect(client.Near(870), "and the earlier refusal to what ran before it");
        }
        for (const bool snapshotFirst : { false, true })
        {
            // A granted, B refused: the verdicts and the snapshot in both orders.
            PickupClient client;
            client.Holding();
            client.Predict(1);
            client.Step(10);
            client.Predict(2);
            client.Step(5);
            client.Answer(1, true, 4);
            if (snapshotFirst)
            {
                client.Snapshot(4, 1785);
                client.Step(5);
                client.Answer(2, false);
            }
            else
            {
                client.Step(5);
                client.Answer(2, false);
                client.Snapshot(4, 1780);
            }
            Expect(client.Near(1780), snapshotFirst
                ? "A granted, its snapshot, then B refused: A's grant shows"
                : "A granted, B refused, then A's snapshot: A's grant shows");
        }
        {
            // A refused, B granted.
            PickupClient client;
            client.Holding();
            client.Predict(1);
            client.Step(10);
            client.Predict(2);
            client.Step(5);
            client.Answer(1, false);
            client.Step(5);
            client.Answer(2, true, 4);
            client.Snapshot(4, 1790);
            Expect(client.Near(1790), "A refused, B granted: B's grant shows");
        }
        {
            // Three pickups, refused middle, first, last.
            PickupClient client;
            client.Holding();
            client.Predict(1);
            client.Step(10);
            client.Predict(2);
            client.Step(10);
            client.Predict(3);
            client.Step(10);
            client.Answer(2, false);
            Expect(client.Local == 1790, "a refusal in the middle leaves the newest alone");
            client.Answer(1, false);
            Expect(client.Local == 1790, "so does the oldest's");
            client.Answer(3, false);
            Expect(client.Near(870), "the last refusal goes back to what ran before all three");
        }
        {
            // Dying with a pickup waiting, refused while dead.
            PickupClient client;
            client.Holding();
            client.Predict(1);
            client.Step(10);
            client.Alive = false;
            client.Local = 0;
            client.Answer(1, false);
            Expect(client.Local == 0, "nothing is put back onto a dead player");
        }
        {
            // A pickup from the last life refused in the next, with one waiting in it.
            PickupClient client;
            client.Holding();
            client.Predict(1);
            client.Step(10);
            client.Local = 0;
            client.Life = 2;
            client.Book.BeginLife(2);
            client.Answer(1, false);
            Expect(client.Local == 0, "a respawn drops the last life's pickups");
            client.Predict(2);
            client.Step(10);
            client.Answer(2, false);
            Expect(client.Local == 0, "and the new life's refusal goes back to the new life's nothing");
        }
        {
            // Both lives' pickups still waiting when the respawn is noticed late.
            PickupClient client;
            client.Holding();
            client.Predict(1);
            client.Step(10);
            client.Local = 0;
            client.Life = 2;
            client.Predict(2);
            client.Step(10);
            client.Answer(1, false);
            Expect(client.Local == 1790, "another life's refusal hands nothing on and rolls nothing back");
        }
    }

    void PowerUpAuthority()
    {
        using namespace MphRead::Mods::Network;
        // The ledger: when each grant ran, by the world frame of the contact.
        PowerUpLedger ledger;
        Expect(!ledger.RanAt(2, PowerUp::Deathalt, 100, 1), "nothing granted: nothing ran");
        const PowerUpLedger::Grant first = ledger.Begin(2, PowerUp::Deathalt, 100, PowerUpFrames, 1);
        Expect(first.Sequence == 1 && first.Expire == 100 + PowerUpFrames, "a grant runs its full time from the contact");
        Expect(ledger.RanAt(2, PowerUp::Deathalt, 100, 1) && !ledger.RanAt(2, PowerUp::Deathalt, 99, 1),
            "from the contact on, never before it");
        Expect(ledger.RanAt(2, PowerUp::Deathalt, 99 + PowerUpFrames, 1) && !ledger.RanAt(2, PowerUp::Deathalt, 100 + PowerUpFrames, 1),
            "up to its last frame and not one past it");
        Expect(!ledger.RanAt(3, PowerUp::Deathalt, 150, 1) && !ledger.RanAt(2, PowerUp::Cloak, 150, 1),
            "one player's power-up is not another's, nor another kind");
        ledger.End(2, PowerUp::Deathalt, 400);
        Expect(ledger.RanAt(2, PowerUp::Deathalt, 399, 1) && !ledger.RanAt(2, PowerUp::Deathalt, 400, 1),
            "a death ends it exactly there");
        const PowerUpLedger::Grant second = ledger.Begin(2, PowerUp::Deathalt, 1000, PowerUpFrames, 1);
        Expect(second.Sequence == 2 && ledger.RanAt(2, PowerUp::Deathalt, 200, 1),
            "a new grant keeps the old one's record for hits judged late");
        ledger.Begin(2, PowerUp::Deathalt, 1100, PowerUpFrames, 1);
        Expect(ledger.Current(2, PowerUp::Deathalt).Start == 1100 && !ledger.RanAt(2, PowerUp::Deathalt, 1100 + PowerUpFrames, 1)
            && ledger.RanAt(2, PowerUp::Deathalt, 1099, 1), "picked up again: the old one ends where the new begins");
        Expect(!ledger.RanAt(2, PowerUp::Deathalt, 1500, 2), "a later life of the same player does not have it");
        ledger.EndAll(2, 1200);
        Expect(ledger.RanAt(2, PowerUp::Deathalt, 1199, 1) && !ledger.RanAt(2, PowerUp::Deathalt, 1200, 1),
            "a new life ends every grant still running");
        ledger.Forget(2);
        Expect(!ledger.RanAt(2, PowerUp::Deathalt, 1100, 1), "a new occupant has nothing");
        std::uint8_t sequence = 0;
        for (int i = 0; i < 40; ++i)
        {
            sequence = NextGrantSequence(sequence);
            Expect(sequence >= 1 && sequence <= MaxGrantSequence, "a sequence fits its five bits and is never 0");
        }

        // The wire: five bits of sequence, eleven of frames left.
        const PowerUpReport report{ 31, PowerUpFrames };
        Expect(UnpackReport(PackReport(report)).Sequence == 31 && UnpackReport(PackReport(report)).Remaining == PowerUpFrames,
            "a report survives the wire");
        Expect(UnpackReport(PackReport(PowerUpReport{ 1, 5000 })).Remaining == RemainingMask, "frames are clamped, not wrapped");

        // A client's copy: corrected, never assigned each snapshot.
        Expect(ReconcilePowerUp({ 4, 1700 }, 3, 0, false, true) == PowerUpStep::Start, "a grant not shown here starts");
        Expect(ReconcilePowerUp({ 4, 1700 }, 3, 0, false, false) == PowerUpStep::Keep, "but not on a dead copy");
        Expect(ReconcilePowerUp({ 4, 1700 }, 3, 1704, false, true) == PowerUpStep::Keep,
            "a predicted one a few frames off is left alone");
        Expect(ReconcilePowerUp({ 4, 1700 }, 3, 1790, false, true) == PowerUpStep::Retime, "one far off is retimed");
        Expect(ReconcilePowerUp({ 4, 1650 }, 4, 1700, false, true) == PowerUpStep::Keep, "the same grant counts down on its own");
        Expect(ReconcilePowerUp({ 4, 0 }, 4, 12, false, true) == PowerUpStep::End, "the authority's has ended: so does this");
        Expect(ReconcilePowerUp({ 3, 0 }, 3, 1790, true, true) == PowerUpStep::Keep,
            "a prediction the snapshots have not caught up with is not ended");
        Expect(ReconcilePowerUp({ 4, 900 }, 4, 0, false, true) == PowerUpStep::Resume,
            "a grant still running that this copy lost is put back");
        Expect(ReconcilePowerUp({ 4, 6 }, 4, 0, false, true) == PowerUpStep::Keep,
            "not its last few frames: those two copies merely ended apart");
        Expect(ReconcilePowerUp({ 4, 900 }, 4, 0, true, true) == PowerUpStep::Keep
            && ReconcilePowerUp({ 4, 900 }, 4, 0, false, false) == PowerUpStep::Keep,
            "nor while a prediction waits, nor on a dead copy");

        // Who settles a pickup.
        PickupContext context{};
        Expect(RouteFor(context) == PickupRoute::Local, "offline: this machine");
        context = PickupContext{ true, false, true, true, true, true };
        Expect(RouteFor(context) == PickupRoute::Claim, "a client's own player and a power-up: predicted and claimed");
        context.OwnPlayer = false;
        Expect(RouteFor(context) == PickupRoute::None, "somebody else's contact: the authority's word");
        context = PickupContext{ true, false, true, true, false, true };
        Expect(RouteFor(context) == PickupRoute::None, "health is not predicted");
        context.Authority = true;
        Expect(RouteFor(context) == PickupRoute::Authority, "the authority hands it out itself");
        context = PickupContext{ true, false, false, true, true, true };
        Expect(RouteFor(context) == PickupRoute::Local, "an item no spawner put out stays local");

        // The common timer lifecycle keeps a locally predicted freeze until
        // the authority acknowledges it and lets an already-thawing copy end.
        using TimedEffectSyncAction = MphRead::Mods::Network::TimedEffectSyncAction;
        Expect(ReconcileTimedEffect(140, 0, false).Action == TimedEffectSyncAction::Start
            && ReconcileTimedEffect(30, 150, true).Action == TimedEffectSyncAction::Update,
            "an authoritative timed effect starts or corrects its remaining frames");
        Expect(ReconcileTimedEffect(0, 120, false).Action == TimedEffectSyncAction::End,
            "the authority ended the effect: this copy begins its end transition");
        Expect(ReconcileTimedEffect(0, 120, true).Action == TimedEffectSyncAction::Keep,
            "a locally predicted effect is kept until its report arrives");
        Expect(ReconcileTimedEffect(0, 1, false).Action == TimedEffectSyncAction::Keep
            && ReconcileTimedEffect(0, 0, false).Action == TimedEffectSyncAction::Keep,
            "an effect already ending or ended is left alone");

        // A contact is looked for a round trip after it, not anywhere in history.
        const FrameWindow window = ContactArrivalWindow(1000, 150, 2000);
        Expect(window.From == 1000 && window.To == 1000 + 9 + ArrivalSlack, "150 ms is nine frames, and the slack");
        Expect(ContactArrivalWindow(1000, 150, 1012).To == 1012, "never past now");

        // Damage: what the shot could have done, not what the claim says.
        const ChargeAmounts beam{ 6, 10, 36 };
        Expect(ChargePercent(true, true, 6, 24, 0) == 0.0F && ChargePercent(true, true, 6, 24, 48) == 1.0F,
            "a partial charge runs from its least to its full");
        Expect(ChargePercent(true, true, 6, 24, 30) == 0.5F, "and halfway is half");
        Expect(ChargePercent(true, false, 6, 24, 47) == 0.0F && ChargePercent(true, false, 6, 24, 48) == 1.0F,
            "an all-or-nothing charge is one or the other");
        Expect(ChargePercent(false, false, 6, 24, 99) == 0.0F, "a weapon that cannot charge is never charged");
        Expect(AmountAt(beam, 0.0F) == 6.0F && AmountAt(beam, 0.5F) == 23.0F && AmountAt(beam, 1.0F) == 36.0F,
            "the amount at a charge is the projectile's");
        Expect(Largest(beam) == 36, "the most at any charge");
        Expect(ScaleBound(6.0F, DamageScale{}) == 7, "an uncharged shot: its own damage and a point");
        Expect(ScaleBound(6.0F, DamageScale{ 1.0F, 1.0F, true, false }) == 13, "doubled when double damage was granted");
        Expect(ScaleBound(6.0F, DamageScale{ 1.0F, 1.0F, false, true }) == 10, "a Prime Hunter's half again");
        Expect(ScaleBound(6.0F, DamageScale{ 1.0F, 1.0F, true, true }) == 13, "never both");
        Expect(ScaleBound(36.0F, DamageScale{ 0.5F, 1.0F, false, false }) == 19, "a resistant victim takes half");

        // Reach: generous, but not across the room.
        using OpenTK::Mathematics::Vector3;
        Expect(WithinPickupReach(Vector3(0.0F, 0.0F, 0.0F), Vector3(0.9F, 0.65F, 0.0F)), "standing on it");
        Expect(!WithinPickupReach(Vector3(0.0F, 0.0F, 0.0F), Vector3(4.0F, 0.65F, 0.0F)), "four units away");
        Expect(!WithinPickupReach(Vector3(0.0F, 0.0F, 0.0F), Vector3(0.0F, 5.0F, 0.0F)), "a floor above");

        // The claim, on the wire.
        PickupClaimPacket claim{};
        claim.MatchId = 7;
        claim.AuthorityEpoch = 0x1122334455667788ULL;
        claim.ClaimId = 9;
        claim.SpawnId = -3;
        claim.SpawnCount = 41;
        claim.ContactFrame = 123456;
        claim.Position = Vector3(1.5F, -2.0F, 3.25F);
        std::vector<std::uint8_t> bytes(PickupClaimPacket::Size);
        claim.Write(bytes);
        const PickupClaimPacket read = PickupClaimPacket::Read(bytes);
        Expect(read.MatchId == 7 && read.AuthorityEpoch == claim.AuthorityEpoch && read.ClaimId == 9
            && read.SpawnId == -3 && read.SpawnCount == 41 && read.ContactFrame == 123456
            && read.Position.Z == 3.25F, "a pickup claim reads back as written");
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
        PredictionMatching();
        InboxShedding();
        ConnectionIdentity();
        PowerUpAuthority();
        PredictionOrders();
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "IntentTouchPayload: %s\n", e.what());
        return 1;
    }
    std::puts("IntentTouchPayload: ok");
    return 0;
}
