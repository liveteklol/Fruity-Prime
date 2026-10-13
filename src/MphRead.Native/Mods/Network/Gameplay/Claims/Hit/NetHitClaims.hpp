#pragma once

#include "Mods/Network/Protocol/NetProtocol.hpp"
#include "Mods/Network/Gameplay/Weapons/MuzzleObstructionHistory.hpp"
#include "Mods/Network/Gameplay/Claims/Hit/RescuedHitLedger.hpp"
#include "Mods/Network/Gameplay/Claims/Hit/HitClaimOutbox.hpp"
#include "Mods/Network/Gameplay/Claims/Hit/HitClaimIntentHistory.hpp"
#include "Mods/Network/Gameplay/Claims/Hit/HitClaimDeathState.hpp"
#include "Mods/Network/Gameplay/Claims/Hit/HitClaimMetrics.hpp"
#include "Mods/Network/Gameplay/Claims/Hit/HitClaimRateLimiter.hpp"
#include "Mods/Network/Gameplay/Claims/Hit/HitVerdictOutbox.hpp"
#include "Mods/Network/Gameplay/Claims/Hit/AuthorityHitLedger.hpp"
#include "Mods/Network/Gameplay/Claims/Hit/AuthorityHitInbox.hpp"
#include "Mods/Network/Gameplay/Claims/Hit/HitClaimReplayLedger.hpp"
#include "Mods/Network/Domain/ShotKey.hpp"

#include "Formats/Enums.hpp"
#include "NativeRuntime/OpenTK/Mathematics.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>

namespace MphRead::Entities
{
    class PlayerEntity;
    enum class DamageFlags : std::int32_t;
}

namespace MphRead
{
    class Scene;
    class WeaponInfo;
}

namespace MphRead::Mods::Network
{
    // A shot the authority cannot find is declared, checked and arbitrated.
    // See .claude/multiplayer/NETWORK-HITCLAIMS.md.
    class NetHitClaimsFixture;
    struct ClaimDamageProfile;

    class NetHitClaims final
    {
        // Narrow white-box access for the combat diagnostic's authority edge cases.
        friend class NetHitClaimsFixture;

    public:
        NetHitClaims() = delete;

        [[nodiscard]] static bool Enabled() noexcept { return _enabled; }
        static void Enabled(bool value) noexcept { _enabled = value; }

        // How far the victim the shooter drew may be from where this
        // authority's history had them at that frame. Measured (-hitlog, 2581
        // claims at 250 ms +-40): p99 0.26, p99.9 0.67, worst 0.95 -- the
        // puppet is read off the same frame the authority rewinds to. It was
        // 2.0 when the authority still checked every hit itself.
        static constexpr float ClaimRadius = 1.25F;
        static constexpr float MeleeRadius = 4.0F;

        [[nodiscard]] static std::int32_t GraceFor(std::int32_t slot);

        static constexpr std::int32_t MinGraceFrames = 24;
        static constexpr std::int32_t MaxGraceFrames = 72;

        [[nodiscard]] static std::int32_t GraceFrames();

        [[nodiscard]] static std::int64_t Declared() noexcept { return _outbox.Declared(); }
        [[nodiscard]] static std::int64_t Applied() noexcept
        { return _metrics.Value(HitClaimMetrics::Counter::Applied); }
        [[nodiscard]] static std::int64_t Duplicate() noexcept
        { return _metrics.Value(HitClaimMetrics::Counter::Duplicate); }
        [[nodiscard]] static std::int64_t RefusedDeadShooter() noexcept
        { return _metrics.Value(HitClaimMetrics::Counter::RefusedDeadShooter); }
        [[nodiscard]] static std::int64_t RefusedDeadVictim() noexcept
        { return _metrics.Value(HitClaimMetrics::Counter::RefusedDeadVictim); }
        [[nodiscard]] static std::int64_t RefusedOther() noexcept
        { return _metrics.Value(HitClaimMetrics::Counter::RefusedOther); }
        [[nodiscard]] static std::int64_t Unanswered() noexcept { return _outbox.Unanswered(); }
        [[nodiscard]] static std::int64_t Resends() noexcept { return _outbox.Resends(); }

        [[nodiscard]] static bool Claiming();

        static std::uint16_t Declare(::MphRead::Entities::PlayerEntity& victim,
            ::MphRead::Entities::PlayerEntity& attacker, ::MphRead::BeamType beam, std::uint32_t damage,
            ::MphRead::Entities::DamageFlags flags, bool lethal, OpenTK::Mathematics::Vector3 hitPoint,
            std::uint32_t launchFrame, std::optional<OpenTK::Mathematics::Vector3> impulse = std::nullopt,
            ::MphRead::Affliction afflictions = ::MphRead::Affliction::None,
            std::optional<OpenTK::Mathematics::Vector3> impact = std::nullopt, bool splash = false,
            std::uint32_t shotSequence = 0, std::uint32_t turretDamage = 0, bool doubled = false);
        // While a claim is being applied: where it landed on the victim, for
        // the damage event the victim's machine will read (NetDamage::Note).
        [[nodiscard]] static ImpactOffset CurrentClaimImpact() noexcept { return _applyingImpact; }
        // The authority keeps what each shooter's intents said: the newest
        // ack, and the rays its recent shots were fired along. A claim is
        // checked against them (ShotPlausible).
        static void RecordIntent(std::int32_t slot, const IntentPacket& intent) noexcept;
        static void SetScene(MphRead::Scene* scene) noexcept { _scene = scene; }
        // A claim's ack against the ack the shooter's own intent reported on
        // the frame it declared the claim (resends keep that frame): honest,
        // 0. When that intent was lost, against its newest ack, which a
        // resend can trail by a resend interval or two (21, 43 measured at 2%
        // loss) -- so that fallback only bounds it to the rewind ceiling.
        static constexpr std::int32_t AckSlackFrames = 3;
        static constexpr std::int32_t AckFallbackFrames = 45;
        // How far the impact may sit from the ray the intent says was fired:
        // the impact is placed from the authority's history of the victim, so
        // the claim radius is part of it, plus 0.75 (honest p99 0.11).
        static constexpr float RayTolerance = ClaimRadius + 0.75F;
        // Shooter-authoritative hits: a remote player's hit on another player
        // is the one their own machine resolved, validated here, and the
        // authority's own copy of it is not applied (NetDamage::Suppress).
        // -servershots restores the authority resolving them itself.
        [[nodiscard]] static bool ShooterHits() noexcept { return _shooterHits; }
        static void ShooterHits(bool value) noexcept { _shooterHits = value; }
        [[nodiscard]] static bool ApplyingClaimNow() noexcept { return _applyingClaim; }
        static void NoteServerCopySuppressed() noexcept
        { _metrics.Increment(HitClaimMetrics::Counter::ServerCopiesSuppressed); }
        [[nodiscard]] static std::int32_t Compose(std::span<std::uint8_t> dest);
        static void ApplyVerdicts(std::span<const std::uint8_t> payload);

        [[nodiscard]] static std::uint32_t CurrentClaimLaunch() noexcept;

        [[nodiscard]] static std::int64_t Received() noexcept
        { return _metrics.Value(HitClaimMetrics::Counter::Received); }
        [[nodiscard]] static std::int64_t AppliedHere() noexcept
        { return _metrics.Value(HitClaimMetrics::Counter::AppliedHere); }
        [[nodiscard]] static std::int64_t DuplicateHere() noexcept
        { return _metrics.Value(HitClaimMetrics::Counter::DuplicateHere); }
        [[nodiscard]] static std::int64_t VoidedDeadShooter() noexcept
        { return _metrics.Value(HitClaimMetrics::Counter::VoidedDeadShooter); }
        [[nodiscard]] static std::int64_t VoidedDeadVictim() noexcept
        { return _metrics.Value(HitClaimMetrics::Counter::VoidedDeadVictim); }
        [[nodiscard]] static std::int64_t RefusedHere() noexcept
        { return _metrics.Value(HitClaimMetrics::Counter::RefusedHere); }
        [[nodiscard]] static std::int64_t CappedHere() noexcept
        { return _metrics.Value(HitClaimMetrics::Counter::CappedHere); }
        [[nodiscard]] static std::int64_t TooOldHere() noexcept
        { return _metrics.Value(HitClaimMetrics::Counter::TooOldHere); }
        [[nodiscard]] static std::int64_t RepeatsHere() noexcept
        { return _metrics.Value(HitClaimMetrics::Counter::RepeatsHere); }
        [[nodiscard]] static std::int64_t MatchedByLaunch() noexcept
        { return _metrics.Value(HitClaimMetrics::Counter::MatchedByLaunch); }
        [[nodiscard]] static std::int64_t MatchedByWindow() noexcept
        { return _metrics.Value(HitClaimMetrics::Counter::MatchedByWindow); }
        [[nodiscard]] static std::int64_t RescuedDamage() noexcept
        { return _metrics.Value(HitClaimMetrics::Counter::RescuedDamage); }
        [[nodiscard]] static std::int64_t RescuedKills() noexcept
        { return _metrics.Value(HitClaimMetrics::Counter::RescuedKills); }
        [[nodiscard]] static std::int64_t RescuedHeadshots() noexcept
        { return _metrics.Value(HitClaimMetrics::Counter::RescuedHeadshots); }
        [[nodiscard]] static std::int64_t SuppressedHere() noexcept
        { return _metrics.Value(HitClaimMetrics::Counter::SuppressedHere); }

        [[nodiscard]] static bool Arbitrating();

        static void NoteAuthorityHit(std::int32_t attackerSlot, std::int32_t victimSlot,
            std::uint32_t launchFrame = 0, std::int32_t damage = 0,
            BeamType beam = BeamType::None, std::optional<ShotKey> launchKey = std::nullopt);
        static void NoteMuzzleObstruction(const ShotKey& key, BeamType beam,
            const Combat::BeamObstacleHit& hit);
        static void NoteMuzzleDescendant(const ShotKey& key, BeamType beam);
        static void Receive(std::int32_t shooterSlot, std::span<const std::uint8_t> payload);
        [[nodiscard]] static std::string DescribeAgreement();
        static void Tick();
        [[nodiscard]] static bool AlreadyRescued(std::int32_t attacker, std::int32_t victim,
            std::uint32_t launch, std::optional<ShotKey> launchKey = std::nullopt);

        using VerdictWriter = HitVerdictOutbox::Writer;
        [[nodiscard]] static const VerdictWriter& VerdictSink() noexcept { return _verdictOutbox.Sink(); }
        static void VerdictSink(VerdictWriter value) { _verdictOutbox.Sink(std::move(value)); }

        static void Reset();
        static void ForgetSlot(std::int32_t slot, bool preserveFlights = false);
        static void ForgetPending();
        [[nodiscard]] static std::optional<std::string> Describe();

    private:
        static constexpr std::int32_t Slots = 8;
        static_assert(Slots == static_cast<std::int32_t>(HitClaimRateLimiter::SlotCapacity));
        static_assert(Slots == static_cast<std::int32_t>(AuthorityHitLedger::SlotCapacity));
        static_assert(Slots == static_cast<std::int32_t>(HitClaimReplayLedger::SlotCapacity));
        static constexpr std::uint8_t ResultPending = 255;
        static constexpr std::size_t AltBeamBuckets = HitClaimMetrics::DamageBucketCount;
        static constexpr std::int32_t PendingCapacity
            = static_cast<std::int32_t>(AuthorityHitInbox::Capacity);
        using Pending = AuthorityHitInbox::Entry;

        [[nodiscard]] static std::int32_t MaxClaimAge();
        [[nodiscard]] static std::int32_t ResendInterval();
        static void TickOutbox();
        static void NoteLedger(std::int32_t attacker, std::int32_t victim, std::uint32_t ack,
            std::uint32_t launch, std::int32_t damage, bool used = false,
            BeamType beam = BeamType::None, std::optional<ShotKey> launchKey = std::nullopt);
        [[nodiscard]] static std::int32_t NearestLedgerOffset(
            std::int32_t attacker, std::int32_t victim, std::uint32_t arrived);
        [[nodiscard]] static bool TakeLedger(std::int32_t attacker, std::int32_t victim,
            std::uint32_t claimAck, std::uint32_t claimLaunch, std::uint32_t arrived, std::int32_t window,
            std::int32_t& authorityDamage, std::optional<ShotKey> strictKey = std::nullopt,
            BeamType strictBeam = BeamType::None);
        [[nodiscard]] static bool MuzzleObstructed(const Pending& entry);
        static void ClearLedger(std::int32_t attacker, std::int32_t victim);
        [[nodiscard]] static std::uint32_t FireFrameOf(std::int32_t slot);
        [[nodiscard]] static bool Seen(std::int32_t slot, std::uint16_t id);
        static void Remember(std::int32_t slot, std::uint16_t id, std::uint8_t result);
        [[nodiscard]] static std::uint8_t Judge(std::int32_t shooterSlot, const HitClaimPacket& claim);
        static void NoteAgreement(std::int32_t shooter, std::int32_t victim, std::uint8_t beam,
            std::int32_t claimed, std::int32_t resolved);
        // False when every pending entry is taken: the claim has not been parked.
        [[nodiscard]] static bool Park(std::int32_t shooterSlot, const HitClaimPacket& claim);
        [[nodiscard]] static std::int32_t MaxDamageFor(std::int32_t shooterSlot, std::uint8_t beam);
        // The weapon the shooter actually fires for this beam: a hunter's
        // affinity weapon is its own row of the table (index + 9), with its
        // own homing, cooldown, damage and afflictions. Null for no beam.
        [[nodiscard]] static const ::MphRead::WeaponInfo* FiredWeapon(std::int32_t shooterSlot, std::uint8_t beam);
        static void TrackDeaths();
        static void NoteRescued(std::int32_t attacker, std::int32_t victim, std::uint32_t launch);
        static void ApplyOne(Pending& entry);
        static void Answer(std::int32_t slot, std::uint16_t id, std::uint8_t result, bool remember = true);
        static void FlushVerdicts();

        static bool _enabled;
        static HitClaimOutbox _outbox;
        inline static HitClaimMetrics _metrics{};

        static AuthorityHitInbox _inbox;
        static HitClaimReplayLedger _replayLedger;

        static AuthorityHitLedger _authorityHitLedger;
        static MuzzleObstructionHistory _muzzleObstructions;

        static HitClaimDeathState _deathState;
        static void FinishLethal(const Pending& entry);
        inline static ImpactOffset _applyingImpact{};
        inline static MphRead::Scene* _scene = nullptr;
        // Claims naming a shot event whose weapon or launch frame they
        // contradict, and claims checked against the event they named.
        // How far a claim's launch frame may stand from the world its named
        // shot was aimed in: the frame the ack ticks over between the two.
        static constexpr std::uint32_t NamedShotAckSlack = 2;
        [[nodiscard]] static bool NamedShotAgrees(std::int32_t shooterSlot, const HitClaimPacket& claim,
            std::uint32_t launch, std::optional<IntentPacket::ShotEvent>& named);
        // The distinct launch frames recently claimed, per shooter and weapon.
        static HitClaimRateLimiter _rateLimiter;
        [[nodiscard]] static bool RatePlausible(std::int32_t shooterSlot, std::uint8_t beam, std::uint32_t launch);
        // Measured for every claim (ShotPlausible), for -hitlog.
        inline static std::int32_t _lastAckGap = 0;
        inline static float _lastRayDistance = -1.0F;
        inline static bool _lastBlocked = false;
        [[nodiscard]] static bool ShotPlausible(std::int32_t shooterSlot, const HitClaimPacket& claim,
            OpenTK::Mathematics::Vector3 was);
        [[nodiscard]] static bool StraightWeapon(std::int32_t shooterSlot, std::uint8_t beam);
        [[nodiscard]] static bool ImpactPlausible(const HitClaimPacket& claim, std::int32_t victimSlot);
        // The damage flags a claim's hit carries when applied here: its
        // headshot, and its cause when the authority can vouch for it.
        [[nodiscard]] static ::MphRead::Entities::DamageFlags CauseFlags(const Pending& entry);
        [[nodiscard]] static bool CauseVouched(const Pending& entry);
        // The frame the claimed shot was fired in (the world it was aimed in).
        [[nodiscard]] static std::uint32_t FiredFrame(const Pending& entry) noexcept;
        // A doubled claim waits for the double damage it was fired under:
        // the pickup claim behind it may still be on its way.
        [[nodiscard]] static bool DoublingSettled(const Pending& entry);
        [[nodiscard]] static std::uint32_t DoublingHold(const Pending& entry);
        // What the hit does, decided here as it is applied: only authority-
        // granted power-ups can change the damage policy's bound.
        static void SettleDamage(Pending& entry);
        [[nodiscard]] static ClaimDamageProfile DamageProfile(const Pending& entry);
        [[nodiscard]] static std::uint8_t CauseOf(::MphRead::Entities::DamageFlags flags) noexcept;
        // Whether the weapon a claim names heals its shooter by the hit.
        [[nodiscard]] static bool Drains(std::int32_t shooterSlot, std::uint8_t beam);
        inline static bool _shooterHits = true;
        static HitClaimIntentHistory _intentHistory;

        static bool _applyingClaim;
        static std::uint32_t _applyingClaimAck;
        static std::uint32_t _applyingClaimLaunch;

        static RescuedHitLedger _rescuedLedger;

        static HitVerdictOutbox _verdictOutbox;
    };
}
