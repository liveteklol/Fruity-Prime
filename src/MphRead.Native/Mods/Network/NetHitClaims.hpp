#pragma once

#include "NetProtocol.hpp"
#include "NetShotDiagnostics.hpp"
#include "MuzzleObstructionHistory.hpp"

#include "../../Formats/Enums.hpp"
#include "../../NativeRuntime/OpenTK/Mathematics.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include "DeathaltWitness.hpp"

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
    class NetCombatCheck;

    // A ray a shooter's intent said it fired (NetHitClaims::RecordIntent).
    struct ClaimShotRay
    {
        std::uint32_t Ack = 0;
        OpenTK::Mathematics::Vector3 Origin{};
        OpenTK::Mathematics::Vector3 Direction{};
    };

    class NetHitClaims final
    {
        // NetCombatCheck calls Judge and NoteRescued the way the C# does by reflection.
        friend class NetCombatCheck;

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

        [[nodiscard]] static std::int64_t Declared() noexcept { return _declared; }
        [[nodiscard]] static std::int64_t Applied() noexcept { return _applied; }
        [[nodiscard]] static std::int64_t Duplicate() noexcept { return _duplicate; }
        [[nodiscard]] static std::int64_t RefusedDeadShooter() noexcept { return _refusedDeadShooter; }
        [[nodiscard]] static std::int64_t RefusedDeadVictim() noexcept { return _refusedDeadVictim; }
        [[nodiscard]] static std::int64_t RefusedOther() noexcept { return _refusedOther; }
        [[nodiscard]] static std::int64_t Unanswered() noexcept { return _unanswered; }
        [[nodiscard]] static std::int64_t Resends() noexcept { return _resends; }

        [[nodiscard]] static bool Claiming();

        static std::uint16_t Declare(::MphRead::Entities::PlayerEntity& victim,
            ::MphRead::Entities::PlayerEntity& attacker, ::MphRead::BeamType beam, std::uint32_t damage,
            ::MphRead::Entities::DamageFlags flags, bool lethal, OpenTK::Mathematics::Vector3 hitPoint,
            std::uint32_t launchFrame, std::optional<OpenTK::Mathematics::Vector3> impulse = std::nullopt,
            ::MphRead::Affliction afflictions = ::MphRead::Affliction::None,
            std::optional<OpenTK::Mathematics::Vector3> impact = std::nullopt, bool splash = false,
            std::uint32_t shotSequence = 0, std::uint32_t turretDamage = 0);
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
        static void NoteServerCopySuppressed() noexcept { _serverCopiesSuppressed++; }
        [[nodiscard]] static std::int32_t Compose(std::span<std::uint8_t> dest);
        static void ApplyVerdicts(std::span<const std::uint8_t> payload);

        [[nodiscard]] static std::uint32_t CurrentClaimLaunch() noexcept;

        [[nodiscard]] static std::int64_t Received() noexcept { return _received; }
        [[nodiscard]] static std::int64_t AppliedHere() noexcept { return _appliedHere; }
        [[nodiscard]] static std::int64_t DuplicateHere() noexcept { return _duplicateHere; }
        [[nodiscard]] static std::int64_t VoidedDeadShooter() noexcept { return _voidedDeadShooter; }
        [[nodiscard]] static std::int64_t VoidedDeadVictim() noexcept { return _voidedDeadVictim; }
        [[nodiscard]] static std::int64_t RefusedHere() noexcept { return _refusedHere; }
        [[nodiscard]] static std::int64_t TooOldHere() noexcept { return _tooOldHere; }
        [[nodiscard]] static std::int64_t RepeatsHere() noexcept { return _repeatsHere; }
        [[nodiscard]] static std::int64_t MatchedByLaunch() noexcept { return _matchedByLaunch; }
        [[nodiscard]] static std::int64_t MatchedByWindow() noexcept { return _matchedByWindow; }
        [[nodiscard]] static std::int64_t RescuedDamage() noexcept { return _rescuedDamage; }
        [[nodiscard]] static std::int64_t RescuedKills() noexcept { return _rescuedKills; }
        [[nodiscard]] static std::int64_t RescuedHeadshots() noexcept { return _rescuedHeadshots; }
        [[nodiscard]] static std::int64_t SuppressedHere() noexcept { return _suppressedHere; }

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

        using VerdictWriter = std::function<void(std::int32_t slot,
            std::span<const std::pair<std::uint16_t, std::uint8_t>> verdicts)>;
        [[nodiscard]] static const VerdictWriter& VerdictSink() noexcept { return _verdictSink; }
        static void VerdictSink(VerdictWriter value) { _verdictSink = std::move(value); }

        static void Reset();
        static void ForgetSlot(std::int32_t slot, bool preserveFlights = false);
        static void ForgetPending();
        [[nodiscard]] static std::optional<std::string> Describe();

    private:
        static constexpr std::int32_t Slots = 8;
        static constexpr std::int32_t OutboxCapacity = 32;
        static constexpr std::int32_t MaxSends = 6;
        static constexpr std::int32_t MaxAge = 60;
        static constexpr std::int32_t MinResendInterval = 8;
        static constexpr std::int32_t PendingCapacity = 64;
        static constexpr std::int32_t SeenCapacity = 128;
        static constexpr std::uint8_t ResultPending = 255;
        static constexpr std::int32_t LedgerDepth = 8;
        static constexpr std::int32_t AltBeamBuckets = 11; // NetHitPrediction.AltBeam + 1
        static constexpr std::int32_t AckMatchFrames = 120;
        static constexpr std::int32_t LaunchMatchFrames = 4;
        static constexpr std::int32_t RescuedCapacity = 32;
        static constexpr std::int32_t RescuedFrames = 720;
        static constexpr std::int32_t VerdictCapacity = HitVerdictPacket::MaxPerPacket;

        struct Outgoing final
        {
            std::uint32_t ShotSequence = 0;
            std::uint16_t MatchId = 0;
            std::uint64_t AuthorityEpoch = 0;
            std::uint16_t ShooterGeneration = 0;
            std::uint16_t ShooterLifeId = 0;
            std::uint16_t VictimGeneration = 0;
            std::uint16_t VictimLifeId = 0;
            std::uint16_t Id = 0;
            std::uint32_t Frame = 0;
            std::uint32_t AckFrame = 0;
            std::uint32_t LaunchFrame = 0;
            std::uint8_t VictimSlot = 0;
            std::uint8_t Beam = 0;
            std::uint16_t Damage = 0;
            std::uint16_t TurretDamage = 0;
            std::uint8_t Cause = HitClaimPacket::CauseHit;
            std::uint8_t Flags = 0;
            OpenTK::Mathematics::Vector3 HitPoint{};
            OpenTK::Mathematics::Vector3 Impulse{};
            ImpactOffset Impact{};
            std::int32_t Age = 0;
            std::int32_t Sends = 0;
            bool Live = false;
        };

        struct Pending final
        {
            std::uint16_t MatchId = 0;
            std::uint64_t AuthorityEpoch = 0;
            std::uint16_t ShooterGeneration = 0;
            std::uint16_t ShooterLifeId = 0;
            std::uint16_t VictimGeneration = 0;
            std::uint16_t VictimLifeId = 0;
            std::uint16_t Id = 0;
            std::uint8_t ShooterSlot = 0;
            std::uint8_t VictimSlot = 0;
            std::uint8_t Beam = 0;
            std::uint16_t Damage = 0;
            std::uint16_t TurretDamage = 0;
            std::uint8_t Cause = HitClaimPacket::CauseHit;
            std::uint8_t Flags = 0;
            std::uint32_t AckFrame = 0;
            std::uint32_t LaunchFrame = 0;
            OpenTK::Mathematics::Vector3 HitPoint{};
            OpenTK::Mathematics::Vector3 Impulse{};
            ImpactOffset Impact{};
            std::uint32_t Arrived = 0;
            std::int32_t Grace = 0;
            bool Live = false;
            bool RequireAuthorityHit = false;
        };

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
        static void Park(std::int32_t shooterSlot, const HitClaimPacket& claim);
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

        template <typename T, std::size_t A, std::size_t B>
        using Grid = std::array<std::array<T, B>, A>;
        template <typename T>
        using Ledger = std::array<std::array<std::array<T, LedgerDepth>, Slots>, Slots>;

        static bool _enabled;
        static std::array<Outgoing, OutboxCapacity> _outbox;
        static std::uint16_t _nextId;
        static std::int64_t _declared;
        static std::int64_t _applied;
        static std::int64_t _duplicate;
        static std::int64_t _refusedDeadShooter;
        static std::int64_t _refusedDeadVictim;
        static std::int64_t _refusedOther;
        static std::int64_t _unanswered;
        static std::int64_t _resends;

        static std::array<Pending, PendingCapacity> _pending;
        static std::array<std::uint16_t, Slots> _newestId;
        static std::array<std::uint8_t, Slots> _lastResult;
        static Grid<std::uint16_t, Slots, SeenCapacity> _seenIds;
        static Grid<std::uint8_t, Slots, SeenCapacity> _seenResults;
        static Grid<::MphRead::BeamType, Slots, SeenCapacity> _seenBeams;
        static Grid<ShotKey, Slots, SeenCapacity> _seenKeys;

        static Ledger<std::uint32_t> _authorityHit;
        static Ledger<std::uint32_t> _authorityHitAck;
        static Ledger<std::uint32_t> _authorityHitLaunch;
        static Ledger<bool> _authorityHitUsed;
        static Ledger<std::int32_t> _authorityHitDamage;
        static Ledger<ShotKey> _authorityHitKeys;
        static Ledger<BeamType> _authorityHitBeam;
        static MuzzleObstructionHistory _muzzleObstructions;
        static Grid<std::int32_t, Slots, Slots> _authorityHitHead;

        static std::array<std::int64_t, AltBeamBuckets> _agreeByBeam;
        static std::array<std::int64_t, AltBeamBuckets> _differByBeam;
        static std::array<std::int64_t, AltBeamBuckets> _claimedByBeam;
        static std::array<std::int64_t, AltBeamBuckets> _resolvedByBeam;
        static std::int32_t _disagreementsLogged;

        static std::array<std::uint32_t, Slots> _deathFire;
        // The authority frame a slot went down on. A shot its owner fired
        // while still displaying an earlier world was taken before that
        // player could know they were dead, and it stands.
        static std::array<std::uint32_t, Slots> _deathFrame;
        [[nodiscard]] static bool FiredAfterOwnDeath(std::size_t shooter, std::uint32_t fired) noexcept;
        static void FinishLethal(const Pending& entry);
        inline static std::int64_t _finishedHere = 0;
        inline static ImpactOffset _applyingImpact{};
        inline static std::int64_t _impactRefused = 0;
        inline static std::int64_t _afflictionsStripped = 0;
        inline static std::int64_t _causesVouched = 0;
        inline static std::int64_t _causesStripped = 0;
        inline static DeathaltWitness _deathalt{};
        inline static MphRead::Scene* _scene = nullptr;
        inline static std::array<std::array<ClaimShotRay, 32>, 8> _shotRays{};
        inline static std::array<std::size_t, 8> _shotRayNext{};
        inline static std::array<std::uint32_t, 8> _latestAck{};
        // (intent frame, ack) the shooter reported, recent ones.
        inline static std::array<std::array<std::uint32_t, 64>, 8> _ackFrameOf{};
        inline static std::array<std::array<std::uint32_t, 64>, 8> _ackValueOf{};
        inline static std::int64_t _ackRefused = 0;
        inline static std::int64_t _rayRefused = 0;
        // Claims naming a shot event whose weapon or launch frame they
        // contradict, and claims checked against the event they named.
        // How far a claim's launch frame may stand from the world its named
        // shot was aimed in: the frame the ack ticks over between the two.
        static constexpr std::uint32_t NamedShotAckSlack = 2;
        [[nodiscard]] static bool NamedShotAgrees(std::int32_t shooterSlot, const HitClaimPacket& claim,
            std::uint32_t launch, std::optional<IntentPacket::ShotEvent>& named);
        inline static std::int64_t _eventRefused = 0;
        inline static std::int64_t _eventMatched = 0;
        inline static std::int64_t _eventNamed = 0;
        inline static std::int64_t _eventUnknown = 0;
        inline static std::int64_t _losRefused = 0;
        inline static std::int64_t _rateRefused = 0;
        // The distinct launch frames recently claimed, per shooter and weapon.
        inline static std::array<std::array<std::array<std::uint32_t, 8>, 9>, 8> _recentLaunches{};
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
        [[nodiscard]] static std::uint8_t CauseOf(::MphRead::Entities::DamageFlags flags) noexcept;
        // Whether the weapon a claim names heals its shooter by the hit.
        [[nodiscard]] static bool Drains(std::int32_t shooterSlot, std::uint8_t beam);
        inline static bool _shooterHits = true;
        inline static std::int64_t _serverCopiesSuppressed = 0;
        static std::array<bool, Slots> _dead;
        static std::array<std::uint32_t, Slots> _lastHitFire;
        static std::array<bool, Slots> _wasInPlay;

        static bool _applyingClaim;
        static std::uint32_t _applyingClaimAck;
        static std::uint32_t _applyingClaimLaunch;

        static std::int64_t _received;
        static std::int64_t _appliedHere;
        static std::int64_t _duplicateHere;
        static std::int64_t _voidedDeadShooter;
        static std::int64_t _voidedDeadVictim;
        static std::int64_t _refusedHere;
        static std::int64_t _tooOldHere;
        static std::int64_t _repeatsHere;
        static std::int64_t _matchedByLaunch;
        static std::int64_t _matchedByWindow;
        static std::int64_t _rescuedDamage;
        static std::int64_t _rescuedKills;
        static std::int64_t _rescuedHeadshots;
        static std::int64_t _suppressedHere;

        static std::array<std::uint8_t, RescuedCapacity> _rescuedAttacker;
        static std::array<std::uint8_t, RescuedCapacity> _rescuedVictim;
        static std::array<ShotKey, RescuedCapacity> _rescuedKeys;
        static std::array<std::uint16_t, RescuedCapacity> _rescuedVictimGeneration;
        static std::array<std::uint16_t, RescuedCapacity> _rescuedVictimLife;
        static std::array<std::uint32_t, RescuedCapacity> _rescuedAt;
        static std::array<std::int32_t, RescuedCapacity> _rescuedOwed;
        static std::int32_t _rescuedHead;

        static VerdictWriter _verdictSink;
        static Grid<std::pair<std::uint16_t, std::uint8_t>, Slots, VerdictCapacity> _verdicts;
        static std::array<std::int32_t, Slots> _verdictCount;
    };
}
