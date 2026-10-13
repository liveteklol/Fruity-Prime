#include "Mods/Network/Gameplay/Claims/Hit/NetHitClaims.hpp"
#include "Mods/Network/Diagnostics/NetShotDiagnostics.hpp"
#include "Mods/Network/Gameplay/Claims/Common/ClaimResend.hpp"
#include "Mods/Network/Gameplay/Weapons/NetShotEvents.hpp"
#include "Mods/Network/Gameplay/Claims/Hit/ClaimDamagePolicy.hpp"
#include "Mods/Network/Gameplay/Claims/Hit/ClaimImpactRules.hpp"
#include "Mods/Network/Gameplay/StatusEffects/BurnStatusEffectPolicy.hpp"
#include "Mods/Network/Gameplay/StatusEffects/DisruptStatusEffectPolicy.hpp"
#include "Mods/Network/Gameplay/StatusEffects/FreezeStatusEffectPolicy.hpp"
#include "Mods/Network/Gameplay/Pickups/NetPowerUps.hpp"
#include "Mods/Network/Domain/PowerUps.hpp"
#include "Mods/Network/Gameplay/Combat/HitLocation.hpp"
#include "Formats/CollisionDetection.hpp"

#include "Mods/Network/Gameplay/Combat/NetDamage.hpp"
#include "Mods/Combat/SyluxMuzzleGuard.hpp"
#include "Mods/Multiplayer/TeamLayout.hpp"
#include "Mods/Network/Gameplay/Combat/NetHitPrediction.hpp"
#include "Mods/Network/Session/NetLifecycleTracker.hpp"
#include "Mods/Network/Diagnostics/NetLog.hpp"
#include "Mods/Network/Gameplay/Player/NetPlayerLifecycle.hpp"
#include "Mods/Network/Session/NetRoomChange.hpp"
#include "Mods/Network/Session/NetSession.hpp"
#include "Mods/Network/Gameplay/Replication/NetSmoothing.hpp"
#include "Mods/Network/Gameplay/Combat/NetUnlagged.hpp"

#include "Entities/Players/HalfturretEntity.hpp"
#include "Entities/Players/PlayerEntity.hpp"
#include "Metadata/Enemies.hpp"
#include "Metadata/Metadata.hpp"
#include "Metadata/Weapons.hpp"
#include "NativeRuntime/System/BinaryPrimitives.hpp"
#include "NativeRuntime/System/Globalization.hpp"
#include "NativeRuntime/System/Managed.hpp"
#include "NativeRuntime/System/Number.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace MphRead::Mods::Network
{
    namespace
    {
        namespace Runtime = ::MphRead::NativeRuntime;
        using ::MphRead::Entities::DamageFlags;
        using ::MphRead::Entities::LoadFlags;
        using ::MphRead::Entities::PlayerEntity;

        // Where a victim's turret stands, if they have one still standing:
        // what a claim's turret share is checked against and applied to.
        [[nodiscard]] std::optional<OpenTK::Mathematics::Vector3> LiveTurretPosition(const PlayerEntity& victim)
        {
            const std::shared_ptr<::MphRead::Entities::HalfturretEntity> turret = victim.Halfturret();
            if (!turret || !::MphRead::TestFlag(victim.Flags2(), ::MphRead::Entities::PlayerFlags2::Halfturret)
                || turret->Health() <= 0)
            {
                return std::nullopt;
            }
            return static_cast<OpenTK::Mathematics::Vector3>(turret->Position);
        }

        [[nodiscard]] bool HasDamageFlag(DamageFlags value, DamageFlags flag) noexcept
        {
            return (static_cast<std::int32_t>(value) & static_cast<std::int32_t>(flag))
                == static_cast<std::int32_t>(flag);
        }

        [[nodiscard]] bool HasLoadFlag(LoadFlags value, LoadFlags flag) noexcept
        {
            using U = std::underlying_type_t<LoadFlags>;
            return (static_cast<U>(value) & static_cast<U>(flag)) == static_cast<U>(flag);
        }

        [[nodiscard]] std::int32_t Ping(std::int32_t slot)
        {
            return SessionSlots().PingAt(slot);
        }

        [[nodiscard]] PlayerEntity& PlayerAt(std::int32_t slot)
        {
            return Runtime::RequireReference(Runtime::ManagedAt(PlayerEntity::Players(), slot));
        }
    }

    bool NetHitClaims::_enabled = true;
    HitClaimOutbox NetHitClaims::_outbox{};
    AuthorityHitInbox NetHitClaims::_inbox{};
    HitVerdictOutbox NetHitClaims::_verdictOutbox{};
    HitClaimReplayLedger NetHitClaims::_replayLedger{};
    AuthorityHitLedger NetHitClaims::_authorityHitLedger{};
    MuzzleObstructionHistory NetHitClaims::_muzzleObstructions{};
    HitClaimDeathState NetHitClaims::_deathState{};
    HitClaimRateLimiter NetHitClaims::_rateLimiter{};
    bool NetHitClaims::_applyingClaim = false;
    std::uint32_t NetHitClaims::_applyingClaimAck = 0;
    std::uint32_t NetHitClaims::_applyingClaimLaunch = 0;
    HitClaimIntentHistory NetHitClaims::_intentHistory{};
    RescuedHitLedger NetHitClaims::_rescuedLedger{};
    std::int32_t NetHitClaims::GraceFor(std::int32_t slot)
    {
        const std::int32_t ping = Ping(slot);
        return Runtime::MathClamp(static_cast<std::int32_t>(static_cast<float>(ping) * 0.06F) + 20,
            MinGraceFrames, MaxGraceFrames);
    }

    std::int32_t NetHitClaims::GraceFrames()
    {
        return GraceFor(NetSession::LocalSlot());
    }

    std::int32_t NetHitClaims::MaxClaimAge()
    {
        return NetUnlagged::HistoryFrames - 4;
    }

    std::int32_t NetHitClaims::ResendInterval()
    {
        return ClaimResendInterval(Ping(NetSession::LocalSlot()));
    }

    bool NetHitClaims::Claiming()
    {
        return _enabled && NetSession::Active() && !NetSession::IsAuthority() && !NetSession::IsHost()
            && NetSession::LocalSlot() >= 0;
    }

    std::uint16_t NetHitClaims::Declare(PlayerEntity& victim, PlayerEntity& attacker, ::MphRead::BeamType beam,
        std::uint32_t damage, DamageFlags flags, bool lethal, OpenTK::Mathematics::Vector3 hitPoint,
        std::uint32_t launchFrame, std::optional<OpenTK::Mathematics::Vector3> impulse, ::MphRead::Affliction afflictions,
        std::optional<OpenTK::Mathematics::Vector3> impact, bool splash, std::uint32_t shotSequence,
        std::uint32_t turretDamage, bool doubled)
    {
        if (!Claiming() || (damage == 0 && turretDamage == 0)
            || NetPlayerLifecycle::Get(victim.SlotIndex()) == 0 || NetPlayerLifecycle::Get(attacker.SlotIndex()) == 0)
        {
            return 0;
        }
        const std::int32_t slot = victim.SlotIndex();
        if (slot < 0 || slot >= Slots)
        {
            return 0;
        }
        std::uint8_t claimFlags = 0;
        if (HasDamageFlag(flags, DamageFlags::Headshot))
        {
            claimFlags = static_cast<std::uint8_t>(claimFlags | HitClaimPacket::FlagHeadshot);
        }
        if (lethal)
        {
            claimFlags = static_cast<std::uint8_t>(claimFlags | HitClaimPacket::FlagLethal);
        }
        if (victim.ModFrozen() || ::MphRead::TestFlag(afflictions, ::MphRead::Affliction::Freeze))
        {
            claimFlags = static_cast<std::uint8_t>(claimFlags | HitClaimPacket::FlagFrozen);
        }
        if (::MphRead::TestFlag(afflictions, ::MphRead::Affliction::Burn))
        {
            claimFlags = static_cast<std::uint8_t>(claimFlags | HitClaimPacket::FlagBurning);
        }
        if (::MphRead::TestFlag(afflictions, ::MphRead::Affliction::Disrupt))
        {
            claimFlags = static_cast<std::uint8_t>(claimFlags | HitClaimPacket::FlagDisrupted);
        }
        if (impulse.has_value())
        {
            claimFlags = static_cast<std::uint8_t>(claimFlags | HitClaimPacket::FlagImpulse);
        }
        if (splash)
        {
            claimFlags = static_cast<std::uint8_t>(claimFlags | HitClaimPacket::FlagSplash);
        }
        if (doubled)
        {
            claimFlags = static_cast<std::uint8_t>(claimFlags | HitClaimPacket::FlagDoubled);
        }

        std::uint32_t readFrame = 0;
        std::uint8_t subFrame = 0;
        const std::uint32_t ack = NetSmoothing::AckPoint(readFrame, subFrame)
            ? readFrame
            : NetSession::AppliedSnapshotFrame();
        HitClaimOutbox::Entry entry;
        entry.MatchId = NetSession::CurrentMatchId();
        entry.AuthorityEpoch = NetSession::AuthorityEpoch();
        entry.ShooterGeneration = NetPlayerLifecycle::Generation(attacker.SlotIndex());
        entry.ShooterLifeId = NetPlayerLifecycle::Get(attacker.SlotIndex());
        entry.VictimGeneration = NetPlayerLifecycle::Generation(slot);
        entry.VictimLifeId = NetPlayerLifecycle::Get(slot);
        entry.Frame = NetSession::NetFrame();
        entry.AckFrame = ack;
        entry.LaunchFrame = launchFrame;
        entry.ShotSequence = shotSequence;
        entry.VictimSlot = static_cast<std::uint8_t>(slot);
        entry.Beam = beam == ::MphRead::BeamType::None ? HitClaimPacket::NoBeam : static_cast<std::uint8_t>(beam);
        entry.Damage = static_cast<std::uint16_t>(std::min<std::uint32_t>(damage, 0xFFFFU));
        entry.TurretDamage = static_cast<std::uint16_t>(std::min<std::uint32_t>(turretDamage, 0xFFFFU));
        entry.Cause = CauseOf(flags);
        entry.Flags = claimFlags;
        entry.HitPoint = hitPoint;
        entry.Impulse = impulse.value_or(OpenTK::Mathematics::Vector3::Zero);
        entry.Impact = impact.has_value() ? ImpactOffset::From(*impact) : ImpactOffset{};
        const std::uint16_t id = _outbox.Enqueue(entry);
        NetShotDiagnostics::RecordClaim(beam);
        if (NetLog::Enabled())
        {
            NetShotDiagnostics::Trace("claim", ShotKey::For(attacker.SlotIndex(), launchFrame), beam,
                "id=" + std::to_string(id) + " victim=" + std::to_string(slot) + " ack=" + std::to_string(ack)
                + " damage=" + std::to_string(damage));
        }
        return id;
    }

    std::int32_t NetHitClaims::Compose(std::span<std::uint8_t> dest)
    {
        if (!Claiming())
        {
            return 0;
        }
        return _outbox.Compose(dest, ResendInterval());
    }

    void NetHitClaims::TickOutbox()
    {
        _outbox.Tick(HitClaimOutbox::MaxAge, [](const HitClaimOutbox::Entry& entry)
        {
            if (NetLog::Enabled())
            {
                NetShotDiagnostics::Trace("unanswered",
                    ShotKey(entry.AuthorityEpoch, entry.MatchId, NetSession::LocalSlot(),
                        entry.ShooterGeneration, entry.ShooterLifeId, entry.LaunchFrame),
                    static_cast<::MphRead::BeamType>(entry.Beam),
                    "id=" + std::to_string(entry.Id) + " reason=verdict-timeout sends=" + std::to_string(entry.Sends));
            }
            NetHitPrediction::Settle(entry.VictimSlot, entry.Id, false);
        });
    }

    void NetHitClaims::ApplyVerdicts(std::span<const std::uint8_t> payload)
    {
        if (payload.empty())
        {
            return;
        }
        if (payload.size() < static_cast<std::size_t>(HitVerdictPacket::HeaderSize))
        {
            return;
        }
        if (!NetSession::MatchesStream(Runtime::ReadUInt16LittleEndian(Runtime::SpanSlice(payload, 1)),
                Runtime::ReadUInt64LittleEndian(Runtime::SpanSlice(payload, 3)))
            || !NetPlayerLifecycle::Matches(NetSession::LocalSlot(),
                Runtime::ReadUInt16LittleEndian(Runtime::SpanSlice(payload, 11)),
                Runtime::ReadUInt16LittleEndian(Runtime::SpanSlice(payload, 13))))
        {
            return;
        }
        const std::int32_t count = std::min(static_cast<std::int32_t>(payload[0]), HitVerdictPacket::MaxPerPacket);
        for (std::int32_t i = 0; i < count; i++)
        {
            const std::size_t at = static_cast<std::size_t>(HitVerdictPacket::HeaderSize + i * HitVerdictPacket::EntrySize);
            if (at + HitVerdictPacket::EntrySize > payload.size())
            {
                break;
            }
            const auto id = static_cast<std::uint16_t>(payload[at] | (payload[at + 1] << 8));
            const std::uint8_t result = payload[at + 2];
            HitClaimOutbox::Entry* entry = _outbox.FindLive(id);
            if (entry != nullptr
                && NetPlayerLifecycle::Matches(entry->VictimSlot, entry->VictimGeneration, entry->VictimLifeId))
            {
                const HitClaimOutbox::Entry resolved = *entry;
                _outbox.Complete(id);
                if (result == HitVerdictPacket::ResultApplied)
                {
                    NetShotDiagnostics::RecordRescue(static_cast<::MphRead::BeamType>(resolved.Beam));
                }
                else if (result != HitVerdictPacket::ResultDuplicate)
                {
                    NetShotDiagnostics::RecordRefusal(static_cast<::MphRead::BeamType>(resolved.Beam));
                }
                if (NetLog::Enabled())
                {
                    NetShotDiagnostics::Trace("verdict",
                        ShotKey(resolved.AuthorityEpoch, resolved.MatchId, NetSession::LocalSlot(),
                            resolved.ShooterGeneration, resolved.ShooterLifeId, resolved.LaunchFrame),
                        static_cast<::MphRead::BeamType>(resolved.Beam),
                        "id=" + std::to_string(id) + " result=" + HitVerdictPacket::Describe(result));
                }
                switch (result)
                {
                case HitVerdictPacket::ResultApplied:
                    _metrics.Increment(HitClaimMetrics::Counter::Applied);
                    NetHitPrediction::Settle(resolved.VictimSlot, id, true);
                    break;
                case HitVerdictPacket::ResultDuplicate:
                    _metrics.Increment(HitClaimMetrics::Counter::Duplicate);
                    NetHitPrediction::Settle(resolved.VictimSlot, id, true);
                    break;
                case HitVerdictPacket::ResultDeadShooter:
                    _metrics.Increment(HitClaimMetrics::Counter::RefusedDeadShooter);
                    NetHitPrediction::Settle(resolved.VictimSlot, id, false);
                    break;
                case HitVerdictPacket::ResultDeadVictim:
                    _metrics.Increment(HitClaimMetrics::Counter::RefusedDeadVictim);
                    // The victim is down on the authority as well: whatever
                    // this machine drew, a body on the floor is the truth.
                    NetHitPrediction::Settle(resolved.VictimSlot, id, false, true);
                    break;
                default:
                    _metrics.Increment(HitClaimMetrics::Counter::RefusedOther);
                    NetHitPrediction::Settle(resolved.VictimSlot, id, false);
                    NetLog::Event("claim " + std::to_string(id) + " on slot " + std::to_string(resolved.VictimSlot)
                        + " " + HitVerdictPacket::Describe(result));
                    break;
                }
                break;
            }
        }
    }

    void NetHitClaims::NoteLedger(std::int32_t attacker, std::int32_t victim, std::uint32_t ack,
        std::uint32_t launch, std::int32_t damage, bool used, BeamType beam, std::optional<ShotKey> launchKey)
    {
        _authorityHitLedger.Record(attacker, victim, NetSession::NetFrame(), ack, launch, damage, used, beam, launchKey);
    }

    std::int32_t NetHitClaims::NearestLedgerOffset(std::int32_t attacker, std::int32_t victim, std::uint32_t arrived)
    {
        return _authorityHitLedger.NearestOffset(attacker, victim, arrived);
    }

    bool NetHitClaims::TakeLedger(std::int32_t attacker, std::int32_t victim, std::uint32_t claimAck,
        std::uint32_t claimLaunch, std::uint32_t arrived, std::int32_t window, std::int32_t& authorityDamage,
        std::optional<ShotKey> strictKey, BeamType strictBeam)
    {
        const AuthorityHitLedger::Match match = _authorityHitLedger.Take(attacker, victim,
            claimAck, claimLaunch, arrived, window, strictKey, strictBeam);
        authorityDamage = match.Damage;
        if (match.Kind == AuthorityHitLedger::MatchKind::Launch)
        {
            _metrics.Increment(HitClaimMetrics::Counter::MatchedByLaunch);
        }
        else if (match.Kind == AuthorityHitLedger::MatchKind::AckWindow)
        {
            _metrics.Increment(HitClaimMetrics::Counter::MatchedByWindow);
        }
        return static_cast<bool>(match);
    }

    void NetHitClaims::ClearLedger(std::int32_t attacker, std::int32_t victim)
    {
        _authorityHitLedger.ClearPair(attacker, victim);
    }

    std::uint32_t NetHitClaims::CurrentClaimLaunch() noexcept
    {
        return _applyingClaim ? _applyingClaimLaunch : 0U;
    }

    bool NetHitClaims::Arbitrating()
    {
        return _enabled && NetSession::Active()
            && (NetSession::Role() == NetRole::Host || NetSession::IsAuthority());
    }

    void NetHitClaims::NoteAuthorityHit(std::int32_t attackerSlot, std::int32_t victimSlot,
        std::uint32_t launchFrame, std::int32_t damage, BeamType beam, std::optional<ShotKey> launchKey)
    {
        if (!Arbitrating() || victimSlot < 0 || victimSlot >= Slots)
        {
            return;
        }
        const std::uint32_t fire = _applyingClaim ? _applyingClaimAck : FireFrameOf(attackerSlot);
        if (_applyingClaim)
        {
            launchFrame = _applyingClaimLaunch;
        }
        if (attackerSlot >= 0 && attackerSlot < Slots)
        {
            NoteLedger(attackerSlot, victimSlot,
                _applyingClaim ? _applyingClaimAck : fire,
                _applyingClaim ? _applyingClaimLaunch : launchFrame,
                damage, _applyingClaim, beam, launchKey);
        }
    }

    void NetHitClaims::NoteMuzzleObstruction(const ShotKey& key, BeamType beam,
        const Combat::BeamObstacleHit& hit)
    {
        if (Combat::SyluxMuzzleGuard::Enabled && Arbitrating()) _muzzleObstructions.Record(key, beam, hit, NetSession::NetFrame());
    }

    bool NetHitClaims::MuzzleObstructed(const Pending& entry)
    {
        return Combat::SyluxMuzzleGuard::Enabled && (entry.RequireAuthorityHit || _muzzleObstructions.Contains(
            ShotKey(entry.AuthorityEpoch, entry.MatchId, entry.ShooterSlot,
                entry.ShooterGeneration, entry.ShooterLifeId, entry.LaunchFrame),
            static_cast<BeamType>(entry.Beam), NetSession::NetFrame()));
    }

    void NetHitClaims::NoteMuzzleDescendant(const ShotKey& key, BeamType beam)
    {
        if (Combat::SyluxMuzzleGuard::Enabled && Arbitrating())
            _muzzleObstructions.RecordDescendant(key, beam, NetSession::NetFrame());
    }

    std::uint32_t NetHitClaims::FireFrameOf(std::int32_t slot)
    {
        if (slot < 0 || slot >= Slots || slot == NetSession::LocalSlot()
            || !NetSession::HasRemoteIntent(slot))
        {
            return NetSession::NetFrame();
        }
        const std::uint32_t ack = NetSession::RemoteIntent(static_cast<std::size_t>(slot)).AckFrame;
        return ack == 0 || ack > NetSession::NetFrame() ? NetSession::NetFrame() : ack;
    }

    void NetHitClaims::Receive(std::int32_t shooterSlot, std::span<const std::uint8_t> payload)
    {
        if (!Arbitrating() || shooterSlot < 0 || shooterSlot >= Slots || payload.empty())
        {
            return;
        }
        const std::int32_t count = std::min(static_cast<std::int32_t>(payload[0]), HitClaimPacket::MaxPerPacket);
        for (std::int32_t i = 0; i < count; i++)
        {
            const std::size_t at = static_cast<std::size_t>(1 + i * HitClaimPacket::Size);
            if (at + HitClaimPacket::Size > payload.size())
            {
                break;
            }
            const HitClaimPacket claim = HitClaimPacket::Read(Runtime::SpanSlice(payload, at));
            _metrics.Increment(HitClaimMetrics::Counter::Received);
            if (claim.ShooterLifeId == 0 || claim.VictimLifeId == 0
                || !NetSession::MatchesStream(claim.MatchId, claim.AuthorityEpoch)
                || !NetPlayerLifecycle::Matches(shooterSlot, claim.ShooterGeneration, claim.ShooterLifeId))
            {
                NetPlayerLifecycle::RecordOldLifeClaim();
                if (HitLocation::Enabled() && claim.VictimSlot < PlayerEntity::Players().size())
                {
                    // 201: the shooter's own life or the match is not the current one.
                    HitLocation::Claim(shooterSlot, PlayerAt(claim.VictimSlot), claim.Beam,
                        claim.LaunchFrame != 0 ? claim.LaunchFrame : claim.AckFrame, claim.Damage, claim.Flags,
                        claim.HitPoint, claim.HitPoint, false, 201);
                }
                continue;
            }
            if (!NetPlayerLifecycle::Matches(claim.VictimSlot, claim.VictimGeneration, claim.VictimLifeId))
            {
                NetPlayerLifecycle::RecordOldLifeClaim();
                if (HitLocation::Enabled() && claim.VictimSlot < PlayerEntity::Players().size())
                {
                    // 202: the victim the shooter drew is a life this authority has already ended.
                    HitLocation::Claim(shooterSlot, PlayerAt(claim.VictimSlot), claim.Beam,
                        claim.LaunchFrame != 0 ? claim.LaunchFrame : claim.AckFrame, claim.Damage, claim.Flags,
                        claim.HitPoint, claim.HitPoint, false, 202);
                }
                Answer(shooterSlot, claim.ClaimId, HitVerdictPacket::ResultWrongLife, false);
                continue;
            }
            if (Seen(shooterSlot, claim.ClaimId))
            {
                _metrics.Increment(HitClaimMetrics::Counter::RepeatsHere);
                const std::uint8_t result = _replayLedger.Result(shooterSlot, claim.ClaimId)
                    .value_or(HitVerdictPacket::ResultTooOld);
                if (result != ResultPending)
                {
                    Answer(shooterSlot, claim.ClaimId, result);
                }
                continue;
            }
            _replayLedger.Begin(shooterSlot, claim.ClaimId, ResultPending,
                static_cast<::MphRead::BeamType>(claim.Beam), ShotKey::For(shooterSlot, claim.LaunchFrame));
            NetShotDiagnostics::RecordClaim(static_cast<::MphRead::BeamType>(claim.Beam));
            const std::uint8_t immediate = Judge(shooterSlot, claim);
            if (HitLocation::Enabled() && claim.VictimSlot < PlayerEntity::Players().size())
            {
                OpenTK::Mathematics::Vector3 history{};
                const bool known = NetUnlagged::PositionAt(claim.VictimSlot, claim.AckFrame,
                    claim.VictimGeneration, claim.VictimLifeId, history);
                // ack gap; distance off the fired ray (-1: not judged); blocked
                HitLocation::Claim(shooterSlot, PlayerAt(claim.VictimSlot), claim.Beam,
                    claim.LaunchFrame != 0 ? claim.LaunchFrame : claim.AckFrame, claim.Damage, claim.Flags,
                    claim.HitPoint, history, known, immediate,
                    std::to_string(_lastAckGap) + ";" + Runtime::ToString(_lastRayDistance, "F3") + ";"
                        + (_lastBlocked ? "1" : "0"));
            }
            if (immediate != HitVerdictPacket::ResultApplied)
            {
                Answer(shooterSlot, claim.ClaimId, immediate);
                continue;
            }
            if (!Park(shooterSlot, claim))
            {
                // Every pending entry taken: say so, and remember it, so that a
                // resend of this claim is answered the same way instead of
                // waiting on a verdict that will never come.
                _metrics.Increment(HitClaimMetrics::Counter::BusyHere);
                Remember(shooterSlot, claim.ClaimId, HitVerdictPacket::ResultBusy);
                Answer(shooterSlot, claim.ClaimId, HitVerdictPacket::ResultBusy);
            }
        }
    }

    bool NetHitClaims::Seen(std::int32_t slot, std::uint16_t id)
    {
        return _replayLedger.Seen(slot, id);
    }

    void NetHitClaims::Remember(std::int32_t slot, std::uint16_t id, std::uint8_t result)
    {
        _replayLedger.Remember(slot, id, result);
    }

    std::uint8_t NetHitClaims::Judge(std::int32_t shooterSlot, const HitClaimPacket& claim)
    {
        const std::int32_t victimSlot = claim.VictimSlot;
        if (victimSlot < 0 || victimSlot >= Slots
            || victimSlot >= static_cast<std::int32_t>(PlayerEntity::Players().size()))
        {
            _metrics.Increment(HitClaimMetrics::Counter::RefusedHere);
            return HitVerdictPacket::ResultRefused;
        }
        const std::uint32_t now = NetSession::NetFrame();
        if (claim.AckFrame == 0 || claim.AckFrame > now
            || now - claim.AckFrame > static_cast<std::uint32_t>(MaxClaimAge()))
        {
            _metrics.Increment(HitClaimMetrics::Counter::TooOldHere);
            return HitVerdictPacket::ResultTooOld;
        }
        if (claim.LaunchFrame > claim.AckFrame)
        {
            _metrics.Increment(HitClaimMetrics::Counter::RefusedHere);
            return HitVerdictPacket::ResultInvalidLaunch;
        }
        if (Combat::SyluxMuzzleGuard::Enabled && claim.LaunchFrame == 0 && claim.Beam < 9
            && PlayerAt(shooterSlot).Hunter() == Hunter::Sylux)
        {
            _metrics.Increment(HitClaimMetrics::Counter::RefusedHere);
            return HitVerdictPacket::ResultInvalidLaunch;
        }
        if (claim.Damage + claim.TurretDamage > MaxDamageFor(shooterSlot, claim.Beam))
        {
            _metrics.Increment(HitClaimMetrics::Counter::RefusedHere);
            NetLog::Event("slot " + std::to_string(shooterSlot) + " claimed " + std::to_string(claim.Damage + claim.TurretDamage)
                + " damage with beam " + std::to_string(claim.Beam) + ", which cannot deal more than "
                + std::to_string(MaxDamageFor(shooterSlot, claim.Beam)));
            return HitVerdictPacket::ResultDamageLimit;
        }
        // A hit on another player is checked against where the shooter saw
        // them, the world it was looking at; one on itself against where it
        // is now, as its intents have it -- its own screen was never a trip
        // behind itself, and walking it would be units from that world.
        OpenTK::Mathematics::Vector3 was{};
        if (victimSlot == shooterSlot)
        {
            was = static_cast<OpenTK::Mathematics::Vector3>(PlayerAt(victimSlot).Position);
        }
        else if (!NetUnlagged::PositionAt(victimSlot, claim.AckFrame, claim.VictimGeneration, claim.VictimLifeId, was))
        {
            _metrics.Increment(HitClaimMetrics::Counter::TooOldHere);
            return HitVerdictPacket::ResultTooOld;
        }
        const float reach = claim.Beam == HitClaimPacket::NoBeam ? MeleeRadius : ClaimRadius;
        const OpenTK::Mathematics::Vector3 offset = claim.HitPoint - was;
        if (!std::isfinite(offset.X) || !std::isfinite(offset.Y) || !std::isfinite(offset.Z)
            || offset.LengthSquared() > reach * reach)
        {
            _metrics.Increment(HitClaimMetrics::Counter::RefusedHere);
            NetLog::Event("slot " + std::to_string(shooterSlot) + " claimed a hit on slot " + std::to_string(victimSlot)
                + " at " + claim.HitPoint.ToString() + ", "
                + Runtime::ToString(OpenTK::Mathematics::Length(offset), "F2") + " units from where frame "
                + std::to_string(claim.AckFrame) + " put them");
            return HitVerdictPacket::ResultGeometry;
        }
        if (!RatePlausible(shooterSlot, claim.Beam, claim.LaunchFrame != 0 ? claim.LaunchFrame : claim.AckFrame))
        {
            _metrics.Increment(HitClaimMetrics::Counter::RefusedHere);
            NetLog::Event("slot " + std::to_string(shooterSlot) + " claimed a shot of beam " + std::to_string(claim.Beam)
                + " launched closer to another than the weapon can fire");
            return HitVerdictPacket::ResultGeometry;
        }
        if (!ShotPlausible(shooterSlot, claim, was))
        {
            _metrics.Increment(HitClaimMetrics::Counter::RefusedHere);
            NetLog::Event("slot " + std::to_string(shooterSlot) + " claimed a hit on slot " + std::to_string(victimSlot)
                + " that its own shots cannot have made (ack gap " + std::to_string(_lastAckGap) + ", "
                + Runtime::ToString(_lastRayDistance, "F2") + " off its ray, " + (_lastBlocked ? "blocked" : "clear") + ")");
            return HitVerdictPacket::ResultGeometry;
        }
        if (!ImpactPlausible(claim, victimSlot))
        {
            _metrics.Increment(HitClaimMetrics::Counter::RefusedHere);
            _metrics.Increment(HitClaimMetrics::Counter::ImpactRefused);
            NetLog::Event("slot " + std::to_string(shooterSlot) + " claimed a hit on slot " + std::to_string(victimSlot)
                + " landing at " + claim.Impact.Value().ToString() + " from its Position, flags "
                + std::to_string(claim.Flags) + ": not on the body, or not on the head it claims");
            return HitVerdictPacket::ResultGeometry;
        }
        PlayerEntity& victim = PlayerAt(victimSlot);
        if (!HasLoadFlag(victim.LoadFlags(), LoadFlags::Active) || !victim.ModIsInPlay())
        {
            _metrics.Increment(HitClaimMetrics::Counter::VoidedDeadVictim);
            return HitVerdictPacket::ResultDeadVictim;
        }
        const std::uint32_t fired = claim.LaunchFrame != 0 ? claim.LaunchFrame : claim.AckFrame;
        if (_deathState.FiredAfterOwnDeath(shooterSlot, fired))
        {
            _metrics.Increment(HitClaimMetrics::Counter::VoidedDeadShooter);
            return HitVerdictPacket::ResultDeadShooter;
        }
        return HitVerdictPacket::ResultApplied;
    }

    void NetHitClaims::NoteAgreement(std::int32_t shooter, std::int32_t victim, std::uint8_t beam,
        std::int32_t claimed, std::int32_t resolved)
    {
        const std::int32_t bucket = beam == HitClaimPacket::NoBeam || beam >= NetHitPrediction::AltBeam
            ? NetHitPrediction::AltBeam
            : beam;
        if (!_metrics.RecordDamageAgreement(static_cast<std::size_t>(bucket), claimed, resolved))
        {
            return;
        }
        if (_metrics.TakeDisagreementLogSlot())
        {
            NetLog::Event("slot " + std::to_string(shooter) + " predicted " + std::to_string(claimed)
                + " damage on slot " + std::to_string(victim) + " with beam " + std::to_string(beam)
                + " and this machine resolved " + std::to_string(resolved));
        }
    }

    std::string NetHitClaims::DescribeAgreement()
    {
        std::string text = "predicted vs resolved damage:";
        bool any = false;
        for (std::size_t i = 0; i < AltBeamBuckets; i++)
        {
            const HitClaimMetrics::DamageAgreement agreement = _metrics.AgreementAt(i);
            const std::int64_t paired = agreement.Agreed + agreement.Differed;
            if (paired == 0)
            {
                continue;
            }
            any = true;
            const std::string name = static_cast<std::int32_t>(i) == NetHitPrediction::AltBeam
                ? std::string("alt/bomb")
                : ::MphRead::ToString(static_cast<::MphRead::BeamType>(i));
            text += "\n  " + Runtime::StringPadRight(name, 13) + " "
                + Runtime::StringPadLeft(std::to_string(paired), 5) + " paired, "
                + Runtime::StringPadLeft(std::to_string(agreement.Agreed), 5) + " agreed ("
                + Runtime::ToString(static_cast<double>(agreement.Agreed) * 100.0 / static_cast<double>(paired), "F0")
                + "%), " + Runtime::StringPadLeft(std::to_string(agreement.Differed), 5) + " differed -- "
                + std::to_string(agreement.Claimed) + " claimed against " + std::to_string(agreement.Resolved)
                + " resolved";
        }
        return any ? text : std::string("predicted vs resolved damage: nothing paired");
    }

    bool NetHitClaims::Park(std::int32_t shooterSlot, const HitClaimPacket& claim)
    {
        Pending* entry = _inbox.TryAllocate();
        if (entry == nullptr)
        {
            return false;
        }
        entry->MatchId = claim.MatchId;
        entry->AuthorityEpoch = claim.AuthorityEpoch;
        entry->ShooterGeneration = claim.ShooterGeneration;
        entry->ShooterLifeId = claim.ShooterLifeId;
        entry->VictimGeneration = claim.VictimGeneration;
        entry->VictimLifeId = claim.VictimLifeId;
        entry->Id = claim.ClaimId;
        entry->ShooterSlot = static_cast<std::uint8_t>(shooterSlot);
        entry->VictimSlot = claim.VictimSlot;
        entry->Beam = claim.Beam;
        entry->Damage = claim.Damage;
        entry->TurretDamage = claim.TurretDamage;
        entry->Cause = claim.Cause;
        entry->Flags = claim.Flags;
        entry->AckFrame = claim.AckFrame;
        entry->LaunchFrame = claim.LaunchFrame;
        entry->ShotSequence = claim.ShotSequence;
        entry->HitPoint = claim.HitPoint;
        entry->Impulse = claim.Impulse;
        entry->Impact = claim.Impact;
        entry->Arrived = NetSession::NetFrame();
        // With shooter-authoritative hits there is no copy of the authority's
        // own to wait for: the claim is the hit, applied on the next tick in
        // fire-frame order.
        // A bot victim is still resolved here as well, so its claims keep the
        // window that pairs them with the authority's own copy.
        const bool botVictim = claim.VictimSlot < PlayerEntity::Players().size()
            && PlayerAt(claim.VictimSlot).IsBot();
        entry->Grace = _shooterHits && !botVictim ? 0 : GraceFor(shooterSlot);
        entry->RequireAuthorityHit = MuzzleObstructed(*entry);
        entry->Live = true;
        return true;
    }

    const ::MphRead::WeaponInfo* NetHitClaims::FiredWeapon(std::int32_t shooterSlot, std::uint8_t beam)
    {
        const auto& current = ::MphRead::Weapons::Current;
        if (beam == HitClaimPacket::NoBeam || current == nullptr || beam >= 9 || beam >= current->size())
        {
            return nullptr;
        }
        std::size_t index = beam;
        // As PlayerEntity::TryEquipWeapon picks it in a match.
        if (shooterSlot >= 0 && static_cast<std::size_t>(shooterSlot) < PlayerEntity::Players().size()
            && static_cast<std::uint8_t>(::MphRead::Weapons::GetAffinityBeam(PlayerAt(shooterSlot).Hunter())) == beam
            && beam + 9U < current->size())
        {
            index = beam + 9U;
        }
        return &Runtime::RequireReference((*current)[index]);
    }

    bool NetHitClaims::Drains(std::int32_t shooterSlot, std::uint8_t beam)
    {
        // Continuous fire is never charged, and the Shock Coil is the one
        // weapon that drains; its claim carries no charge, so either flag.
        const ::MphRead::WeaponInfo* fired = FiredWeapon(shooterSlot, beam);
        return fired != nullptr
            && (::MphRead::TestFlag(fired->Flags, ::MphRead::WeaponFlags::LifeDrainUncharged)
                || ::MphRead::TestFlag(fired->Flags, ::MphRead::WeaponFlags::LifeDrainCharged));
    }

    std::int32_t NetHitClaims::MaxDamageFor(std::int32_t shooterSlot, std::uint8_t beam)
    {
        std::int32_t raw = 200;
        if (const ::MphRead::WeaponInfo* fired = FiredWeapon(shooterSlot, beam); fired != nullptr)
        {
            const ::MphRead::WeaponInfo& info = *fired;
            raw = std::max<std::int32_t>(info.ChargedHeadshotDamage,
                std::max<std::int32_t>(info.HeadshotDamage,
                std::max<std::int32_t>(info.MinChargeHeadshotDamage,
                std::max<std::int32_t>(info.ChargedDamage,
                std::max<std::int32_t>(info.UnchargedDamage,
                std::max<std::int32_t>(info.MinChargeDamage, info.ChargedSplashDamage))))));
        }
        return static_cast<std::int32_t>(static_cast<float>(raw) * 5.0F) + 1;
    }

    void NetHitClaims::Tick()
    {
        if (!NetRoomChange::GameplayReady())
        {
            return;
        }
        if (Claiming())
        {
            TickOutbox();
        }
        if (!Arbitrating())
        {
            return;
        }
        TrackDeaths();
        const std::uint32_t now = NetSession::NetFrame();
        const auto pending = _inbox.Entries();
        while (true)
        {
            std::int32_t next = -1;
            std::int32_t overdue = -1;
            for (std::int32_t i = 0; i < PendingCapacity; i++)
            {
                Pending& entry = pending[static_cast<std::size_t>(i)];
                if (!entry.Live)
                {
                    continue;
                }
                if (!NetSession::MatchesStream(entry.MatchId, entry.AuthorityEpoch)
                    || !NetPlayerLifecycle::Matches(entry.ShooterSlot, entry.ShooterGeneration, entry.ShooterLifeId)
                    || !NetPlayerLifecycle::Matches(entry.VictimSlot, entry.VictimGeneration, entry.VictimLifeId))
                {
                    entry.Live = false;
                    NetPlayerLifecycle::RecordOldLifeClaim();
                    continue;
                }
                entry.RequireAuthorityHit = MuzzleObstructed(entry);
                const auto strictKey = entry.RequireAuthorityHit
                    ? std::optional(ShotKey(entry.AuthorityEpoch, entry.MatchId, entry.ShooterSlot,
                        entry.ShooterGeneration, entry.ShooterLifeId, entry.LaunchFrame)) : std::nullopt;
                std::int32_t resolved = 0;
                // A doubled claim still waiting on its pickup is not judged on
                // any path until it has its answer or has waited long enough.
                const bool settled = DoublingSettled(entry) || now - entry.Arrived >= DoublingHold(entry);
                if (settled && TakeLedger(entry.ShooterSlot, entry.VictimSlot, entry.AckFrame,
                        entry.LaunchFrame, entry.Arrived, entry.Grace, resolved, strictKey, static_cast<BeamType>(entry.Beam)))
                {
                    entry.Live = false;
                    _metrics.Increment(HitClaimMetrics::Counter::DuplicateHere);
                    // A kill finished from a claim is finished at the damage
                    // the authority allows it, never the number it claimed.
                    SettleDamage(entry);
                    NoteAgreement(entry.ShooterSlot, entry.VictimSlot, entry.Beam, entry.Damage, resolved);
                    FinishLethal(entry);
                    Answer(entry.ShooterSlot, entry.Id, HitVerdictPacket::ResultDuplicate);
                    continue;
                }
                if (next < 0)
                {
                    next = i;
                }
                else
                {
                    const Pending& best = pending[static_cast<std::size_t>(next)];
                    if ((entry.LaunchFrame != 0 ? entry.LaunchFrame : entry.AckFrame)
                        < (best.LaunchFrame != 0 ? best.LaunchFrame : best.AckFrame))
                    {
                        next = i;
                    }
                }
                if (now - entry.Arrived >= 2 * MaxGraceFrames
                    && (overdue < 0 || entry.Arrived < pending[static_cast<std::size_t>(overdue)].Arrived))
                {
                    overdue = i;
                }
            }
            if (next < 0)
            {
                break;
            }
            const Pending& due = pending[static_cast<std::size_t>(next)];
            const std::uint32_t wait = DoublingSettled(due)
                ? static_cast<std::uint32_t>(due.Grace)
                : std::max(static_cast<std::uint32_t>(due.Grace), DoublingHold(due));
            if (now - due.Arrived < wait)
            {
                if (overdue < 0)
                {
                    break;
                }
                next = overdue;
            }
            ApplyOne(pending[static_cast<std::size_t>(next)]);
            pending[static_cast<std::size_t>(next)].Live = false;
            TrackDeaths();
        }
        FlushVerdicts();
    }

    // Favour the shooter, all the way. The old test voided a shot whenever its
    // shooter had been hit by something aimed at an earlier world than the one
    // the shot was aimed at -- the honest order on the machine keeping score,
    // and exactly the kill players watched stand back up: on the shooter's
    // screen they were alive and their target fell. The only shot that is
    // really late is one fired while its owner was already displaying their
    // own death, which a client cannot do. Everything else stands, so a trade
    // inside a round trip is two kills and nobody's screen is taken back.
    // Where the shooter's machine says the shot met the body has to be on the
    // body: inside the capsule the beam test uses (plus the widest beam and a
    // little slack), and in the head band if it claims a headshot. A splash
    // reaches a player from its blast's centre, which can be anywhere within
    // the blast. A claim with no impact (an older path, a melee hit) is left
    // to the history check alone.
    bool NetHitClaims::ImpactPlausible(const HitClaimPacket& claim, std::int32_t victimSlot)
    {
        ClaimImpactProfile profile{};
        if (!claim.Impact.Known() || victimSlot < 0
            || victimSlot >= static_cast<std::int32_t>(PlayerEntity::Players().size()))
        {
            return ClaimImpactRules::Plausible(profile);
        }
        const PlayerEntity& victim = PlayerAt(victimSlot);
        profile.Known = true;
        // A hit on a turret did not land on the body, which can stand metres
        // away: ShotPlausible judges a straight one at the turret instead.
        if (claim.TurretDamage > 0 && LiveTurretPosition(victim).has_value())
        {
            profile.TurretHit = true;
            return ClaimImpactRules::Plausible(profile);
        }
        profile.Offset = claim.Impact.Value();
        profile.Splash = (claim.Flags & HitClaimPacket::FlagSplash) != 0;
        profile.AltForm = victim.IsAltForm();
        profile.Headshot = (claim.Flags & HitClaimPacket::FlagHeadshot) != 0;
        if (profile.Splash || profile.AltForm)
        {
            return ClaimImpactRules::Plausible(profile);
        }
        profile.MinY = Fixed::ToFloat(victim.Values().MinPickupHeight);
        profile.MaxY = Fixed::ToFloat(victim.Values().MaxPickupHeight);
        profile.Radius = Fixed::ToFloat(victim.Values().BipedColRadius);
        return ClaimImpactRules::Plausible(profile);
    }

    void NetHitClaims::RecordIntent(std::int32_t slot, const IntentPacket& intent) noexcept
    {
        _intentHistory.Record(slot, intent);
    }

    // Two different shots of one weapon cannot be launched closer together
    // than its cooldown allows (in 30 Hz frames, so twice that here); half of
    // that is the floor, for acks that jump with jitter. Several hits of one
    // shot -- splash, ricochets, several victims -- share a launch frame.
    bool NetHitClaims::RatePlausible(std::int32_t shooterSlot, std::uint8_t beam, std::uint32_t launch)
    {
        const ::MphRead::WeaponInfo* fired = FiredWeapon(shooterSlot, beam);
        if (shooterSlot < 0 || shooterSlot >= static_cast<std::int32_t>(HitClaimRateLimiter::SlotCapacity) || beam >= 9
            || launch == 0 || fired == nullptr)
        {
            return true;
        }
        const ::MphRead::WeaponInfo& w = *fired;
        const std::int32_t cooldown = std::min<std::int32_t>(w.ShotCooldown, w.AutofireCooldown);
        if (cooldown <= 0)
        {
            return true;
        }
        if (!_rateLimiter.Allows(shooterSlot, beam, launch, cooldown))
        {
            _metrics.Increment(HitClaimMetrics::Counter::RateRefused);
            return false;
        }
        return true;
    }

    bool NetHitClaims::StraightWeapon(std::int32_t shooterSlot, std::uint8_t beam)
    {
        // The shooter's own variant: Samus's charged missile and Kanden's
        // charged Volt Driver home, where everybody else's fly straight.
        // Straight means no gravity, no homing and no bounce of any kind.
        const ::MphRead::WeaponInfo* fired = FiredWeapon(shooterSlot, beam);
        if (fired == nullptr)
        {
            return false;
        }
        const ::MphRead::WeaponInfo& w = *fired;
        return w.UnchargedGravity == 0 && w.MinChargeGravity == 0 && w.ChargedGravity == 0
            && w.UnchargedHoming == 0 && w.MinChargeHoming == 0 && w.ChargedHoming == 0
            && !::MphRead::TestFlag(w.Flags, ::MphRead::WeaponFlags::RicochetUncharged)
            && !::MphRead::TestFlag(w.Flags, ::MphRead::WeaponFlags::RicochetCharged)
            // The Judicator carries neither flag, but a shot of it that meets
            // a wall spawns a ricochet weapon's round off it, in a new direction.
            && w.UnchargedRicochetWeapon() == nullptr && w.ChargedRicochetWeapon() == nullptr;
    }

    // The claim is the hit, so it has to be one the shooter's own shots can
    // have made: resolved against a world it was still drawing (its newest
    // ack), on the ray its intent says it fired, with nothing solid between
    // the gun and the body. Weapons that curve, home or bounce are held to
    // the first only; a claim with no impact or no recorded ray is not judged.
    bool NetHitClaims::ShotPlausible(std::int32_t shooterSlot, const HitClaimPacket& claim,
        OpenTK::Mathematics::Vector3 was)
    {
        _lastAckGap = 0;
        _lastRayDistance = -1.0F;
        _lastBlocked = false;
        if (!_intentHistory.HasSlot(shooterSlot))
        {
            return true;
        }
        const std::optional<std::uint32_t> frameAck = claim.Frame != 0
            ? _intentHistory.AckForFrame(shooterSlot, claim.Frame) : std::nullopt;
        if (frameAck.has_value())
        {
            _lastAckGap = static_cast<std::int32_t>(*frameAck - claim.AckFrame);
            if (std::abs(_lastAckGap) > AckSlackFrames)
            {
                _metrics.Increment(HitClaimMetrics::Counter::AckRefused);
                return false;
            }
        }
        else if (const std::uint32_t latestAck = _intentHistory.LatestAck(shooterSlot); latestAck != 0)
        {
            _lastAckGap = static_cast<std::int32_t>(latestAck - claim.AckFrame);
            if (_lastAckGap > AckFallbackFrames || _lastAckGap < -2)
            {
                _metrics.Increment(HitClaimMetrics::Counter::AckRefused);
                return false;
            }
        }
        const std::uint32_t launch = claim.LaunchFrame != 0 ? claim.LaunchFrame : claim.AckFrame;
        std::optional<IntentPacket::ShotEvent> event{};
        if (!NamedShotAgrees(shooterSlot, claim, launch, event))
        {
            return false;
        }
        // A hit on a turret is judged at the turret, which does not move once
        // placed: the impact is an offset around the body, and a turret
        // standing metres from it put every such hit off the ray.
        const std::optional<OpenTK::Mathematics::Vector3> turret = claim.TurretDamage > 0
            ? LiveTurretPosition(PlayerAt(claim.VictimSlot)) : std::nullopt;
        if ((!claim.Impact.Known() && !turret.has_value()) || (claim.Flags & HitClaimPacket::FlagSplash) != 0
            || !StraightWeapon(shooterSlot, claim.Beam))
        {
            return true;
        }
        const ClaimShotRay* ray = nullptr;
        // The shot event the claim names: the exact ray of that shot.
        ClaimShotRay named{};
        if (event.has_value() && event->HasRay())
        {
            named = ClaimShotRay{event->AckFrame, event->Origin, event->Direction.Normalized()};
            ray = &named;
            _metrics.Increment(HitClaimMetrics::Counter::EventMatched);
        }
        if (ray == nullptr)
        {
            ray = _intentHistory.NearestRay(shooterSlot, launch, 3U);
        }
        if (ray == nullptr)
        {
            return true;
        }
        const OpenTK::Mathematics::Vector3 point = turret.value_or(was + claim.Impact.Value());
        const OpenTK::Mathematics::Vector3 toPoint = point - ray->Origin;
        const float along = std::max(0.0F, OpenTK::Mathematics::Vector3::Dot(toPoint, ray->Direction));
        _lastRayDistance = OpenTK::Mathematics::Length(toPoint - OpenTK::Mathematics::Scale(ray->Direction, along));
        if (_lastRayDistance > RayTolerance)
        {
            _metrics.Increment(HitClaimMetrics::Counter::RayRefused);
            return false;
        }
        if (_scene != nullptr)
        {
            Formats::CollisionResult blocked{};
            _lastBlocked = Formats::CollisionDetection::CheckBetweenPoints(ray->Origin, point,
                Formats::TestFlags::Beams, _scene, blocked);
            if (_lastBlocked)
            {
                _metrics.Increment(HitClaimMetrics::Counter::LineOfSightRefused);
                return false;
            }
        }
        return true;
    }

    // A claim naming a shot event must be that shot: the weapon it left with
    // and the world it was aimed in, whatever kind of hit -- splash too. A
    // shot not reported yet (its intent lost, the next not arrived) is judged
    // the old way.
    bool NetHitClaims::NamedShotAgrees(std::int32_t shooterSlot, const HitClaimPacket& claim, std::uint32_t launch,
        std::optional<IntentPacket::ShotEvent>& named)
    {
        named = NetShotEvents::Find(shooterSlot, claim.ShotSequence);
        if (!named.has_value())
        {
            if (claim.ShotSequence != 0)
            {
                _metrics.Increment(HitClaimMetrics::Counter::EventUnknown);
            }
            return true;
        }
        _metrics.Increment(HitClaimMetrics::Counter::EventNamed);
        const std::uint32_t gap = named->AckFrame > launch ? named->AckFrame - launch : launch - named->AckFrame;
        if (named->WeaponId != claim.Beam || (named->AckFrame != 0 && gap > NamedShotAckSlack))
        {
            _metrics.Increment(HitClaimMetrics::Counter::EventRefused);
            return false;
        }
        return true;
    }

    void NetHitClaims::TrackDeaths()
    {
        for (std::int32_t i = 0; i < Slots; i++)
        {
            const bool inPlay = i < static_cast<std::int32_t>(PlayerEntity::Players().size())
                && HasLoadFlag(PlayerAt(i).LoadFlags(), LoadFlags::Active)
                && PlayerAt(i).ModIsInPlay();
            if (_deathState.UpdateInPlay(i, inPlay, NetSession::NetFrame())
                == HitClaimSlotTransition::Respawned)
            {
                for (std::int32_t j = 0; j < Slots; j++)
                {
                    ClearLedger(j, i);
                }
            }
        }
    }

    void NetHitClaims::NoteRescued(std::int32_t attacker, std::int32_t victim, std::uint32_t launch)
    {
        if (launch == 0)
        {
            return;
        }
        _rescuedLedger.Record(static_cast<std::uint8_t>(attacker), static_cast<std::uint8_t>(victim),
            ShotKey::For(attacker, launch), NetPlayerLifecycle::Generation(victim),
            NetPlayerLifecycle::Get(victim), NetSession::NetFrame());
    }

    bool NetHitClaims::AlreadyRescued(std::int32_t attacker, std::int32_t victim, std::uint32_t launch,
        std::optional<ShotKey> launchKey)
    {
        if (!Arbitrating() || launch == 0 || _applyingClaim
            || attacker < 0 || attacker >= Slots || victim < 0 || victim >= Slots)
        {
            return false;
        }
        const std::uint32_t now = NetSession::NetFrame();
        if (!_rescuedLedger.Consume(launchKey.value_or(ShotKey::For(attacker, launch)),
            static_cast<std::uint8_t>(victim), NetPlayerLifecycle::Generation(victim),
            NetPlayerLifecycle::Get(victim), now))
        {
            return false;
        }
        _metrics.Increment(HitClaimMetrics::Counter::SuppressedHere);
        NetLog::Event("refused the authority's own copy of slot " + std::to_string(attacker)
            + "'s shot (launch " + std::to_string(launch) + ") on slot " + std::to_string(victim)
            + ": a claim already made it real");
        return true;
    }

    // The authority resolved the same shot itself, but left the victim
    // standing on a sliver of health the shooter's machine did not have --
    // a hit one side counted and the other did not, a tick of a continuous
    // weapon. The shooter already watched this player die, and the claim has
    // passed every test a rescue has, so the kill is made real rather than
    // taken back: what the shooter saw is what happened.
    std::uint8_t NetHitClaims::CauseOf(DamageFlags flags) noexcept
    {
        return HasDamageFlag(flags, DamageFlags::Deathalt) ? HitClaimPacket::CauseDeathalt : HitClaimPacket::CauseHit;
    }

    DamageFlags NetHitClaims::CauseFlags(const Pending& entry)
    {
        auto flags = static_cast<std::int32_t>(DamageFlags::NoDmgInvuln);
        if ((entry.Flags & HitClaimPacket::FlagHeadshot) != 0)
        {
            flags |= static_cast<std::int32_t>(DamageFlags::Headshot);
        }
        if (entry.Cause == HitClaimPacket::CauseDeathalt && CauseVouched(entry))
        {
            flags |= static_cast<std::int32_t>(DamageFlags::Deathalt);
        }
        return static_cast<DamageFlags>(flags);
    }

    std::uint32_t NetHitClaims::FiredFrame(const Pending& entry) noexcept
    {
        return entry.LaunchFrame != 0 ? entry.LaunchFrame : entry.AckFrame;
    }

    bool NetHitClaims::DoublingSettled(const Pending& entry)
    {
        return (entry.Flags & HitClaimPacket::FlagDoubled) == 0
            || NetPowerUps::RanAt(entry.ShooterSlot, PowerUp::DoubleDamage, FiredFrame(entry), entry.ShooterLifeId);
    }

    std::uint32_t NetHitClaims::DoublingHold(const Pending& entry)
    {
        // Two of the shooter's own resend intervals: a pickup claim lost once
        // still arrives inside it.
        return static_cast<std::uint32_t>(2 * ClaimResendInterval(Ping(entry.ShooterSlot)));
    }

    ClaimDamageProfile NetHitClaims::DamageProfile(const Pending& entry)
    {
        ClaimDamageProfile profile{};
        profile.Level = Metadata::DamageLevels.at(static_cast<std::size_t>(GameState::DamageLevel()));
        profile.PrimeHunter = GameState::Mode() == GameMode::PrimeHunter;
        const ::MphRead::WeaponInfo* weapon = FiredWeapon(entry.ShooterSlot, entry.Beam);
        if (weapon == nullptr)
        {
            return profile;
        }
        profile.HasWeapon = true;
        if (entry.VictimSlot < PlayerEntity::Players().size() && entry.Beam < 9)
        {
            const auto effectiveness = PlayerAt(entry.VictimSlot).BeamEffectiveness[entry.Beam];
            profile.Effectiveness = Metadata::DamageMultipliers.at(static_cast<std::size_t>(effectiveness));
        }
        profile.Direct = { weapon->UnchargedDamage, weapon->MinChargeDamage, weapon->ChargedDamage };
        profile.Splash = { weapon->SplashDamage, weapon->MinChargeSplashDamage, weapon->ChargedSplashDamage };
        profile.Headshot = { weapon->HeadshotDamage, weapon->MinChargeHeadshotDamage, weapon->ChargedHeadshotDamage };
        profile.CanCharge = ::MphRead::TestFlag(weapon->Flags, ::MphRead::WeaponFlags::CanCharge);
        profile.PartialCharge = ::MphRead::TestFlag(weapon->Flags, ::MphRead::WeaponFlags::PartialCharge);
        profile.MinCharge = weapon->MinCharge;
        profile.FullCharge = weapon->FullCharge;
        if (entry.ShotSequence != 0)
        {
            const std::optional<IntentPacket::ShotEvent> shot = NetShotEvents::Find(entry.ShooterSlot, entry.ShotSequence);
            if (shot.has_value() && shot->WeaponId == entry.Beam && shot->Charge < 0xFF)
            {
                profile.HasShotCharge = true;
                profile.ShotCharge = shot->Charge;
            }
        }
        return profile;
    }

    void NetHitClaims::SettleDamage(Pending& entry)
    {
        const bool granted = NetPowerUps::RanAt(entry.ShooterSlot, PowerUp::DoubleDamage, FiredFrame(entry),
            entry.ShooterLifeId);
        const bool splash = (entry.Flags & HitClaimPacket::FlagSplash) != 0;
        const bool headshot = (entry.Flags & HitClaimPacket::FlagHeadshot) != 0;
        const ClaimDamageKind kind = splash ? ClaimDamageKind::Splash
            : headshot ? ClaimDamageKind::Headshot : ClaimDamageKind::Direct;
        const ClaimDamageSettlement settlement = ClaimDamagePolicy::Settle(DamageProfile(entry), kind,
            entry.Damage, entry.TurretDamage, entry.Flags, granted);
        entry.Damage = settlement.Damage;
        entry.TurretDamage = settlement.TurretDamage;
        entry.Flags = settlement.Flags;
        if (settlement.Undoubled)
        {
            _metrics.Increment(HitClaimMetrics::Counter::UndoubledHere);
            if (NetLog::Enabled())
            {
                NetLog::Event("[powerup] slot " + std::to_string(entry.ShooterSlot) + " claim " + std::to_string(entry.Id)
                    + " doubled at frame " + std::to_string(FiredFrame(entry)) + " without a grant: halved");
            }
        }
        if (!settlement.Capped)
        {
            return;
        }
        _metrics.Increment(HitClaimMetrics::Counter::CappedHere);
        if (NetLog::Enabled())
        {
            NetLog::Event("[claim] slot " + std::to_string(entry.ShooterSlot) + " claim " + std::to_string(entry.Id)
                + " beam " + std::to_string(entry.Beam) + " shot " + std::to_string(entry.ShotSequence)
                + " flags " + std::to_string(entry.Flags) + " claimed " + std::to_string(settlement.Claimed)
                + ", held to " + std::to_string(settlement.Allowed));
        }
    }

    bool NetHitClaims::CauseVouched(const Pending& entry)
    {
        // The Death Alt the authority granted, running in the world the
        // shooter's screen showed when it hit.
        return entry.Cause == HitClaimPacket::CauseDeathalt
            && NetPowerUps::RanAt(entry.ShooterSlot, PowerUp::Deathalt, entry.AckFrame, entry.ShooterLifeId);
    }

    void NetHitClaims::FinishLethal(const Pending& entry)
    {
        if ((entry.Flags & HitClaimPacket::FlagLethal) == 0
            || entry.VictimSlot >= static_cast<std::int32_t>(PlayerEntity::Players().size())
            || entry.ShooterSlot >= static_cast<std::int32_t>(PlayerEntity::Players().size()))
        {
            return;
        }
        PlayerEntity& victim = PlayerAt(entry.VictimSlot);
        if (victim.Health() <= 0 || entry.Damage < static_cast<std::int32_t>(victim.Health())
            || !HasLoadFlag(victim.LoadFlags(), LoadFlags::Active) || !victim.ModIsInPlay()
            || _deathState.FiredAfterOwnDeath(entry.ShooterSlot,
                entry.LaunchFrame != 0 ? entry.LaunchFrame : entry.AckFrame))
        {
            return;
        }
        PlayerEntity& shooter = PlayerAt(entry.ShooterSlot);
        const std::int32_t left = victim.Health();
        _applyingClaim = true;
        _applyingClaimAck = entry.AckFrame;
        _applyingClaimLaunch = entry.LaunchFrame;
        _applyingImpact = entry.Impact;
        try
        {
            const NetDamage::ClaimScope scope(entry.Beam == HitClaimPacket::NoBeam
                ? ::MphRead::BeamType::None : static_cast<::MphRead::BeamType>(entry.Beam));
            victim.TakeDamage(static_cast<std::uint32_t>(left), CauseFlags(entry), std::nullopt, &shooter);
        }
        catch (...)
        {
            _applyingClaim = false;
            _applyingImpact = ImpactOffset{};
            throw;
        }
        _applyingClaim = false;
        _applyingImpact = ImpactOffset{};
        if (victim.Health() <= 0)
        {
            _metrics.Increment(HitClaimMetrics::Counter::FinishedHere);
            NetLog::Event("finished slot " + std::to_string(entry.VictimSlot) + " (" + std::to_string(left)
                + " health left) for slot " + std::to_string(entry.ShooterSlot)
                + ": its lethal claim matched a hit this authority resolved short of a kill");
        }
    }

    void NetHitClaims::ApplyOne(Pending& entry)
    {
        const std::int32_t victimSlot = entry.VictimSlot;
        const std::int32_t shooterSlot = entry.ShooterSlot;
        if (!NetSession::MatchesStream(entry.MatchId, entry.AuthorityEpoch)
            || !NetPlayerLifecycle::Matches(shooterSlot, entry.ShooterGeneration, entry.ShooterLifeId)
            || !NetPlayerLifecycle::Matches(victimSlot, entry.VictimGeneration, entry.VictimLifeId))
        {
            NetPlayerLifecycle::RecordOldLifeClaim();
            return;
        }
        if (victimSlot >= static_cast<std::int32_t>(PlayerEntity::Players().size())
            || shooterSlot >= static_cast<std::int32_t>(PlayerEntity::Players().size()))
        {
            Answer(shooterSlot, entry.Id, HitVerdictPacket::ResultRefused);
            return;
        }
        PlayerEntity& victim = PlayerAt(victimSlot);
        PlayerEntity& shooter = PlayerAt(shooterSlot);
        if (!HasLoadFlag(victim.LoadFlags(), LoadFlags::Active) || !victim.ModIsInPlay())
        {
            _metrics.Increment(HitClaimMetrics::Counter::VoidedDeadVictim);
            Answer(shooterSlot, entry.Id, HitVerdictPacket::ResultDeadVictim);
            return;
        }
        if (_deathState.FiredAfterOwnDeath(shooterSlot,
            entry.LaunchFrame != 0 ? entry.LaunchFrame : entry.AckFrame))
        {
            _metrics.Increment(HitClaimMetrics::Counter::VoidedDeadShooter);
            Answer(shooterSlot, entry.Id, HitVerdictPacket::ResultDeadShooter);
            return;
        }
        SettleDamage(entry);
        if (MuzzleObstructed(entry))
        {
            const ShotKey key(entry.AuthorityEpoch, entry.MatchId, entry.ShooterSlot,
                entry.ShooterGeneration, entry.ShooterLifeId, entry.LaunchFrame);
            std::int32_t resolved = 0;
            if (TakeLedger(shooterSlot, victimSlot, entry.AckFrame, entry.LaunchFrame,
                entry.Arrived, entry.Grace, resolved, key, static_cast<BeamType>(entry.Beam)))
            {
                _metrics.Increment(HitClaimMetrics::Counter::DuplicateHere);
                NoteAgreement(shooterSlot, victimSlot, entry.Beam, entry.Damage, resolved);
                Answer(shooterSlot, entry.Id, HitVerdictPacket::ResultDuplicate);
                return;
            }
            // Tick matched legal splash/ricochet damage first. No direct/splash
            // proof exists on the wire, so unmatched obstructed damage is unsafe.
            _metrics.Increment(HitClaimMetrics::Counter::RefusedHere);
            if (Combat::SyluxMuzzleGuardMetrics::Enabled)
                ++Combat::SyluxMuzzleGuardMetrics::Counters.NetClaimRejected;
            Answer(shooterSlot, entry.Id, HitVerdictPacket::ResultGeometry);
            return;
        }
        DamageFlags flags = CauseFlags(entry);
        if (entry.Cause != HitClaimPacket::CauseHit)
        {
            // A cause this machine cannot vouch for: the hit stands, its
            // cause does not.
            _metrics.Increment(CauseVouched(entry) ? HitClaimMetrics::Counter::CausesVouched : HitClaimMetrics::Counter::CausesStripped);
        }
        _applyingClaim = true;
        _applyingClaimAck = entry.AckFrame;
        _applyingClaimLaunch = entry.LaunchFrame;
        _applyingImpact = entry.Impact;
        const bool lethal = victim.Health() <= entry.Damage;
        const auto before = static_cast<std::uint32_t>(victim.Health());
        // A hit on a turret is applied whole, as the turret hit it was: the
        // turret reacts to all of it (its target, its fire rate), the split
        // follows this machine's health, and the turret still keeps its owner
        // alive. A turret gone here leaves the body's share alone.
        const bool turretHit = entry.TurretDamage > 0 && LiveTurretPosition(victim).has_value();
        const std::uint32_t applied = turretHit
            ? static_cast<std::uint32_t>(entry.Damage) + entry.TurretDamage : entry.Damage;
        if (turretHit)
        {
            flags = static_cast<DamageFlags>(static_cast<std::int32_t>(flags)
                | static_cast<std::int32_t>(DamageFlags::Halfturret));
        }
        try
        {
            const NetDamage::ClaimScope scope(entry.Beam == HitClaimPacket::NoBeam
                ? ::MphRead::BeamType::None : static_cast<::MphRead::BeamType>(entry.Beam));
            if (applied > 0)
            {
                victim.TakeDamage(applied, flags,
                    (entry.Flags & HitClaimPacket::FlagImpulse) != 0
                        ? std::optional<OpenTK::Mathematics::Vector3>(entry.Impulse) : std::nullopt,
                    &shooter);
                // A Shock Coil's drain comes with its hit, by the same rules as
                // the projectile's: the shooter's machine credited it as the
                // hit landed, and only this makes it last.
                if (Drains(shooterSlot, entry.Beam) && &shooter != &victim && !shooter.IsPrimeHunter()
                    && !Mods::Multiplayer::TeamRules::AreAllies(shooter.TeamIndex(), victim.TeamIndex()))
                {
                    shooter.GainDrainedHealth(applied);
                }
            }
        }
        catch (...)
        {
            _applyingClaim = false;
            _applyingImpact = ImpactOffset{};
            throw;
        }
        _applyingClaim = false;
        _applyingImpact = ImpactOffset{};
        if (static_cast<std::uint32_t>(victim.Health()) >= before && !turretHit)
        {
            _metrics.Increment(HitClaimMetrics::Counter::RefusedHere);
            Answer(shooterSlot, entry.Id, HitVerdictPacket::ResultNoDamage);
            return;
        }
        // An affliction the weapon cannot inflict (either charge) is dropped,
        // the damage kept: freezing with a Power Beam is not a hit to refuse
        // but a flag nobody honest sends. Frozen also rides on a victim that
        // was already frozen on the shooter's screen.
        if (const ::MphRead::WeaponInfo* fired = FiredWeapon(shooterSlot, entry.Beam); fired != nullptr)
        {
            const ::MphRead::WeaponInfo& weapon = *fired;
            const auto& table = Runtime::RequireReference(weapon.Afflictions);
            ::MphRead::Affliction allowed = ::MphRead::Affliction::None;
            for (const ::MphRead::Affliction affliction : table)
            {
                allowed = static_cast<::MphRead::Affliction>(static_cast<std::int32_t>(allowed) | static_cast<std::int32_t>(affliction));
            }
            const std::uint8_t before = entry.Flags;
            if (!::MphRead::TestFlag(allowed, ::MphRead::Affliction::Burn))
            {
                entry.Flags = static_cast<std::uint8_t>(entry.Flags & ~HitClaimPacket::FlagBurning);
            }
            if (!::MphRead::TestFlag(allowed, ::MphRead::Affliction::Disrupt))
            {
                entry.Flags = static_cast<std::uint8_t>(entry.Flags & ~HitClaimPacket::FlagDisrupted);
            }
            if (!::MphRead::TestFlag(allowed, ::MphRead::Affliction::Freeze) && !victim.ModFrozen())
            {
                entry.Flags = static_cast<std::uint8_t>(entry.Flags & ~HitClaimPacket::FlagFrozen);
            }
            if (entry.Flags != before)
            {
                _metrics.Increment(HitClaimMetrics::Counter::AfflictionsStripped);
                NetLog::Event("claim " + std::to_string(entry.Id) + " from slot " + std::to_string(shooterSlot)
                    + " carried afflictions its weapon cannot inflict (flags " + std::to_string(before) + "): dropped");
            }
        }
        if ((entry.Flags & HitClaimPacket::FlagBurning) != 0 && victim.Health() > 0)
        {
            BurnStatusEffectPolicy::ApplyAuthoritativeHit(victim, shooter, entry.LaunchFrame);
        }
        if ((entry.Flags & HitClaimPacket::FlagDisrupted) != 0 && victim.Health() > 0)
        {
            DisruptStatusEffectPolicy::ApplyAuthoritativeHit(victim);
        }
        if ((entry.Flags & HitClaimPacket::FlagFrozen) != 0 && victim.Health() > 0)
        {
            FreezeStatusEffectPolicy::ApplyAuthoritativeHit(victim);
        }
        _metrics.Increment(HitClaimMetrics::Counter::AppliedHere);
        if (HitLocation::Enabled())
        {
            // 100: applied on the tick it was parked for, with what it dealt.
            // The damage column is what was claimed; what it took off a
            // victim with less health left than that is the lethal clamp.
            HitLocation::Claim(shooterSlot, victim, entry.Beam,
                entry.LaunchFrame != 0 ? entry.LaunchFrame : entry.AckFrame,
                entry.Damage, entry.Flags, entry.HitPoint, entry.HitPoint, false, 100);
        }
        NoteRescued(shooterSlot, victimSlot, entry.LaunchFrame);
        const std::uint32_t dealt = before - static_cast<std::uint32_t>(std::max(0, victim.Health()));
        _metrics.Add(HitClaimMetrics::Counter::RescuedDamage, dealt);
        if ((entry.Flags & HitClaimPacket::FlagHeadshot) != 0)
        {
            _metrics.Increment(HitClaimMetrics::Counter::RescuedHeadshots);
        }
        if (lethal)
        {
            _metrics.Increment(HitClaimMetrics::Counter::RescuedKills);
        }
        NetLog::Event("rescue " + std::to_string(entry.Id) + " beam="
            + (entry.Beam == HitClaimPacket::NoBeam ? std::string("none")
                : ::MphRead::ToString(static_cast<::MphRead::BeamType>(entry.Beam)))
            + " nearest=" + std::to_string(NearestLedgerOffset(shooterSlot, victimSlot, entry.Arrived)) + "f"
            + " window=" + std::to_string(entry.Grace) + "f ack=" + std::to_string(entry.AckFrame)
            + " arrived=" + std::to_string(entry.Arrived));
        NetLog::Event("claim " + std::to_string(entry.Id) + ": slot " + std::to_string(shooterSlot) + " hit slot "
            + std::to_string(victimSlot) + " for " + std::to_string(dealt)
            + ((entry.Flags & HitClaimPacket::FlagHeadshot) != 0 ? " (headshot)" : "")
            + (lethal ? " and killed them" : "")
            + ", aimed at frame " + std::to_string(entry.AckFrame) + ", "
            + std::to_string(NetSession::NetFrame() - entry.AckFrame) + " frames ago"
            + (_deathState.WasDead(shooterSlot) ? " -- and was dead by the time it arrived" : ""));
        Answer(shooterSlot, entry.Id, HitVerdictPacket::ResultApplied);
    }

    void NetHitClaims::Answer(std::int32_t slot, std::uint16_t id, std::uint8_t result, bool remember)
    {
        if (slot < 0 || slot >= Slots)
        {
            return;
        }
        const std::optional<std::uint8_t> previous = _replayLedger.Result(slot, id);
        if (remember && previous.has_value() && *previous == ResultPending)
        {
            const auto beam = _replayLedger.Beam(slot, id);
            const ShotKey key = _replayLedger.Key(slot, id);
            if (result == HitVerdictPacket::ResultApplied)
            {
                NetShotDiagnostics::RecordRescue(beam);
            }
            else if (result != HitVerdictPacket::ResultDuplicate)
            {
                NetShotDiagnostics::RecordRefusal(beam);
            }
            if (NetLog::Enabled())
            {
                NetShotDiagnostics::Trace("authority-verdict", key, beam,
                    "claim=" + std::to_string(id) + " result=" + HitVerdictPacket::Describe(result));
            }
        }
        if (remember)
        {
            Remember(slot, id, result);
        }
        (void)_verdictOutbox.Enqueue(slot, id, result);
    }

    void NetHitClaims::FlushVerdicts()
    {
        _verdictOutbox.Flush();
    }

    void NetHitClaims::Reset()
    {
        _muzzleObstructions.Reset();
        _outbox.Reset();
        _inbox.Clear();
        _replayLedger.Reset();
        _authorityHitLedger.Reset();
        _deathState.Reset();
        _verdictOutbox.Clear();
        _rescuedLedger.Reset();
        _metrics.Reset();
        _rateLimiter.Reset();
        _intentHistory.Reset();
    }

    void NetHitClaims::ForgetSlot(std::int32_t slot, bool preserveFlights)
    {
        if (slot < 0 || slot >= Slots)
        {
            return;
        }
        _muzzleObstructions.ForgetSlot(slot);
        _rateLimiter.ForgetSlot(slot);
        _outbox.ForgetSlot(slot, NetSession::LocalSlot());
        _inbox.ForgetSlot(slot);
        _verdictOutbox.ClearSlot(slot);
        _replayLedger.ForgetSlot(slot);
        _rescuedLedger.ForgetSlot(static_cast<std::uint8_t>(slot), preserveFlights);
        _deathState.ForgetSlot(slot);
        _authorityHitLedger.ClearSlot(slot);
    }

    void NetHitClaims::ForgetPending()
    {
        _muzzleObstructions.Reset();
        _replayLedger.Reset();
        _verdictOutbox.Clear();
        _rescuedLedger.Clear();
        _outbox.ClearPending();
        _inbox.Clear();
        _authorityHitLedger.Reset();
        _deathState.Reset();
        _rateLimiter.Reset();
    }

    std::optional<std::string> NetHitClaims::Describe()
    {
        if (Declared() == 0 && _metrics.Value(HitClaimMetrics::Counter::Received) == 0)
        {
            return std::nullopt;
        }
        if (_metrics.Value(HitClaimMetrics::Counter::Received) > 0)
        {
            return "hit claims (as authority): " + std::to_string(_metrics.Value(HitClaimMetrics::Counter::Received)) + " received, "
                + std::to_string(_metrics.Value(HitClaimMetrics::Counter::AppliedHere)) + " applied (" + std::to_string(_metrics.Value(HitClaimMetrics::Counter::RescuedDamage)) + " damage, "
                + std::to_string(_metrics.Value(HitClaimMetrics::Counter::RescuedKills)) + " kills, " + std::to_string(_metrics.Value(HitClaimMetrics::Counter::RescuedHeadshots))
                + " headshots rescued), " + std::to_string(_metrics.Value(HitClaimMetrics::Counter::DuplicateHere)) + " already resolved (" + std::to_string(_metrics.Value(HitClaimMetrics::Counter::FinishedHere)) + " finished as kills), "
                + std::to_string(_metrics.Value(HitClaimMetrics::Counter::ImpactRefused)) + " landing off the body, "
                + std::to_string(_metrics.Value(HitClaimMetrics::Counter::AfflictionsStripped)) + " afflictions dropped, "
                + std::to_string(_metrics.Value(HitClaimMetrics::Counter::CausesVouched)) + " Death Alts vouched for ("
                + std::to_string(_metrics.Value(HitClaimMetrics::Counter::CausesStripped)) + " not seen here), " + std::to_string(_metrics.Value(HitClaimMetrics::Counter::AckRefused))
                + " resolved against a stale ack, " + std::to_string(_metrics.Value(HitClaimMetrics::Counter::RayRefused)) + " off the fired ray, "
                + std::to_string(_metrics.Value(HitClaimMetrics::Counter::EventNamed)) + " naming a reported shot ("
                + std::to_string(_metrics.Value(HitClaimMetrics::Counter::EventMatched)) + " ray-checked against it, "
                + std::to_string(_metrics.Value(HitClaimMetrics::Counter::EventRefused)) + " contradicting it), "
                + std::to_string(_metrics.Value(HitClaimMetrics::Counter::EventUnknown)) + " naming one not yet reported, "
                + std::to_string(_metrics.Value(HitClaimMetrics::Counter::LineOfSightRefused)) + " through a wall, " + std::to_string(_metrics.Value(HitClaimMetrics::Counter::RateRefused))
                + " faster than the weapon fires, " + (_shooterHits ? "shooter-authoritative (" + std::to_string(_metrics.Value(HitClaimMetrics::Counter::ServerCopiesSuppressed)) + " server copies not applied), " : std::string())
                + std::to_string(_metrics.Value(HitClaimMetrics::Counter::VoidedDeadShooter)) + " from a shooter already dead, "
                + std::to_string(_metrics.Value(HitClaimMetrics::Counter::VoidedDeadVictim)) + " on a victim already down, "
                + std::to_string(_metrics.Value(HitClaimMetrics::Counter::RefusedHere)) + " refused, " + std::to_string(_metrics.Value(HitClaimMetrics::Counter::BusyHere)) + " with no room to judge, "
                + std::to_string(_metrics.Value(HitClaimMetrics::Counter::UndoubledHere)) + " doubled without a grant, "
                + std::to_string(_metrics.Value(HitClaimMetrics::Counter::CappedHere)) + " held to the most the shot could do, "
                + std::to_string(_metrics.Value(HitClaimMetrics::Counter::TooOldHere)) + " too old, "
                + std::to_string(_metrics.Value(HitClaimMetrics::Counter::RepeatsHere)) + " repeats "
                + "(matched " + std::to_string(_metrics.Value(HitClaimMetrics::Counter::MatchedByLaunch)) + " by shot, " + std::to_string(_metrics.Value(HitClaimMetrics::Counter::MatchedByWindow))
                + " by window, " + std::to_string(_metrics.Value(HitClaimMetrics::Counter::SuppressedHere)) + " of its own refused as already rescued)";
        }
        const std::int64_t answered = _metrics.Value(HitClaimMetrics::Counter::Applied) + _metrics.Value(HitClaimMetrics::Counter::Duplicate) + _metrics.Value(HitClaimMetrics::Counter::RefusedDeadShooter) + _metrics.Value(HitClaimMetrics::Counter::RefusedDeadVictim) + _metrics.Value(HitClaimMetrics::Counter::RefusedOther);
        const double pct = answered > 0
            ? 100.0 * static_cast<double>(_metrics.Value(HitClaimMetrics::Counter::Applied) + _metrics.Value(HitClaimMetrics::Counter::Duplicate)) / static_cast<double>(answered) : 0.0;
        return "hit claims: " + std::to_string(Declared()) + " declared, " + std::to_string(_metrics.Value(HitClaimMetrics::Counter::Applied)) + " applied, "
            + std::to_string(_metrics.Value(HitClaimMetrics::Counter::Duplicate)) + " already resolved (" + Runtime::ToString(pct, "F1") + "% stood), "
            + std::to_string(_metrics.Value(HitClaimMetrics::Counter::RefusedDeadShooter)) + " void (dead shooter), "
            + std::to_string(_metrics.Value(HitClaimMetrics::Counter::RefusedDeadVictim)) + " void (victim down), " + std::to_string(_metrics.Value(HitClaimMetrics::Counter::RefusedOther))
            + " refused, " + std::to_string(Unanswered()) + " unanswered, " + std::to_string(Resends()) + " repeats";
    }
}
