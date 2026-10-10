#pragma once

#include "../../Entities/Players/PlayerEntity.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace MphRead::Entities
{
    class EntityBase;
}

namespace MphRead::Mods::Network
{
    class NetHitPrediction final
    {
    private:
        static constexpr std::int32_t Slots = Entities::PlayerEntity::SlotCapacity;
        static constexpr std::int32_t PendingFrames = 120;
        static constexpr std::int32_t PendingCapacity = 24;
        static constexpr std::int32_t SettledCreditMax = 8;
        static constexpr std::int32_t HealCapacity = 48;

    public:
        NetHitPrediction() = delete;

        static constexpr std::int32_t AltBeam = 10;

        [[nodiscard]] static bool Enabled() noexcept { return _enabled; }
        static void SetEnabled(bool value) noexcept { _enabled = value; }

        [[nodiscard]] static bool MarkerEnabled() noexcept { return _markerEnabled; }
        static void SetMarkerEnabled(bool value) noexcept { _markerEnabled = value; }

        [[nodiscard]] static bool Predicting() noexcept;

        [[nodiscard]] static std::int64_t FloorLifted() noexcept { return _floorLifted; }
        [[nodiscard]] static std::int64_t Predicted() noexcept { return _predicted; }
        [[nodiscard]] static std::int64_t Confirmed() noexcept { return _confirmed; }
        [[nodiscard]] static std::int64_t Denied() noexcept { return _denied; }
        [[nodiscard]] static std::int64_t Unpredicted() noexcept { return _unpredicted; }
        [[nodiscard]] static std::int64_t UnpredictedMoving() noexcept { return _unpredictedMoving; }
        [[nodiscard]] static std::int64_t UnpredictedStill() noexcept { return _unpredictedStill; }
        [[nodiscard]] static std::int64_t LethalHeld() noexcept { return _lethalHeld; }
        [[nodiscard]] static std::int64_t DeathsPredicted() noexcept { return _deathsPredicted; }
        [[nodiscard]] static std::int64_t SelfDeathsPredicted() noexcept { return _selfDeathsPredicted; }
        [[nodiscard]] static std::int64_t DeathsUndone() noexcept { return _deathsUndone; }
        [[nodiscard]] static std::int64_t DrainPredicted() noexcept { return _drainPredicted; }
        [[nodiscard]] static std::int64_t HeadshotsPredicted() noexcept { return _headshotsPredicted; }
        [[nodiscard]] static std::int64_t HeadshotsAgreed() noexcept { return _headshotsAgreed; }
        [[nodiscard]] static std::int64_t HeadshotsDowngraded() noexcept { return _headshotsDowngraded; }
        [[nodiscard]] static std::int64_t HeadshotsUpgraded() noexcept { return _headshotsUpgraded; }
        [[nodiscard]] static std::int64_t SelfPredicted() noexcept { return _selfPredicted; }
        [[nodiscard]] static std::int64_t SelfConfirmed() noexcept { return _selfConfirmed; }

        // A kill this machine resolves is shown the frame it happens, like a
        // local match. The authority favours the shooter (NetHitClaims), so
        // the body only stands back up for a claim it genuinely refuses.
        [[nodiscard]] static bool DeathEnabled() noexcept { return _deathEnabled; }
        static void SetDeathEnabled(bool value) noexcept { _deathEnabled = value; }
        // Whether a kill this machine showed on that slot is still waiting
        // for the authority's own death of the same life.
        [[nodiscard]] static bool ShowingKill(std::int32_t slot);
        // Kills shown here the authority has not reported yet: the scoreboard
        // keeps them rather than taking them back for a round trip.
        [[nodiscard]] static std::int32_t KillsShown();
        static void ForgetShownKill(std::int32_t slot) noexcept
        {
            if (slot >= 0 && slot < static_cast<std::int32_t>(_killShownFrame.size())) _killShownFrame[static_cast<std::size_t>(slot)] = 0;
        }

        [[nodiscard]] static std::int64_t LethalConfirmed() noexcept { return _lethalConfirmed; }
        [[nodiscard]] static std::int64_t LethalDenied() noexcept { return _lethalDenied; }

        [[nodiscard]] static std::string LifecycleDetails(std::int32_t slot);
        [[nodiscard]] static std::string HealthDetails(std::int32_t slot);

        [[nodiscard]] static float MarkerAlpha() noexcept;

        static void Reset();

        [[nodiscard]] static bool Predicts(
            Entities::PlayerEntity& victim,
            Entities::EntityBase* source,
            Entities::DamageFlags flags);

        static void NoteHit(
            Entities::PlayerEntity& victim,
            Entities::PlayerEntity* attacker,
            Entities::DamageFlags& flags,
            std::uint32_t& damage,
            ::MphRead::BeamType beam = ::MphRead::BeamType::None,
            std::uint32_t launchFrame = 0,
            float flight = 0,
            std::optional<OpenTK::Mathematics::Vector3> impulse = std::nullopt,
            ::MphRead::Affliction afflictions = ::MphRead::Affliction::None,
            std::uint32_t shotSequence = 0,
            std::uint32_t turretDamage = 0);

        [[nodiscard]] static bool Confirm(std::int32_t slot, std::int32_t landed = 1, bool authorityHeadshot = false);
        [[nodiscard]] static Entities::PlayerEntity* OwnerOf(Entities::EntityBase* source);
        // Where on the victim the hit about to be dealt landed, relative to
        // their Position: set by the projectile around its TakeDamage call,
        // read by NoteHit for the claim (HitClaimPacket::Impact).
        static void SetImpact(OpenTK::Mathematics::Vector3 offset, bool splash) noexcept
        {
            _impact = offset;
            _impactKnown = true;
            _impactSplash = splash;
        }
        static void ClearImpact() noexcept { _impactKnown = false; _impactSplash = false; }
        static void ForgetSlot(std::int32_t slot);
        static void NoteRespawn(std::int32_t slot);
        static void NoteDeath(std::int32_t slot);
        static void ForgetPending();
        static void NoteDrain(Entities::PlayerEntity& healer, std::int32_t amount);

        [[nodiscard]] static std::int32_t HealthFor(std::int32_t slot, std::int32_t authorityHealth);

        [[nodiscard]] static std::int64_t HealthSamples() noexcept { return _healthSamples; }
        [[nodiscard]] static std::int64_t HealthDisagreed() noexcept { return _healthDisagreed; }
        [[nodiscard]] static std::int64_t HealthUnderPoints() noexcept { return _healthUnderPoints; }
        [[nodiscard]] static std::int32_t HealthUnderWorst() noexcept { return _healthUnderWorst; }
        [[nodiscard]] static std::int64_t HealthOverPoints() noexcept { return _healthOverPoints; }
        [[nodiscard]] static std::int32_t HealthOverWorst() noexcept { return _healthOverWorst; }
        [[nodiscard]] static std::int64_t FloorHeld() noexcept { return _floorHeld; }
        [[nodiscard]] static std::int64_t FloorHeldPoints() noexcept { return _floorHeldPoints; }
        [[nodiscard]] static std::int32_t FloorWorst() noexcept { return _floorWorst; }
        [[nodiscard]] static std::int64_t DebitPoints() noexcept { return _debitPoints; }
        [[nodiscard]] static std::int32_t DebitWorst() noexcept { return _debitWorst; }

        [[nodiscard]] static std::string DescribeDamageLedger();
        [[nodiscard]] static std::string DescribeHealth();
        [[nodiscard]] static std::int32_t LocalHealthFor(Entities::PlayerEntity& player, std::int32_t authorityHealth);
        [[nodiscard]] static bool HeldDead(std::int32_t slot);

        static void Tick();
        static void Settle(std::int32_t slot, std::uint16_t claimId, bool confirmed, bool victimDown = false);

        [[nodiscard]] static std::string Describe();
        [[nodiscard]] static std::string DescribeByWeapon();
        [[nodiscard]] static std::string DescribeHeadshots();

    private:
        static constexpr std::int32_t BeamBuckets = AltBeam + 1;
        static constexpr float TravelFlight = 3.0F / 60.0F;
        static constexpr std::int32_t MarkerFrames = 12;

        template <typename T>
        using Pending = std::array<std::array<T, PendingCapacity>, Slots>;

        [[nodiscard]] static std::int32_t HoldFrames();
        [[nodiscard]] static std::int32_t Bucket(::MphRead::BeamType beam) noexcept;
        [[nodiscard]] static std::string BucketName(std::int32_t bucket);
        [[nodiscard]] static bool MovingNow();
        static void ResolveHeld(std::int32_t slot, std::int32_t at, bool confirmed);
        static void EnsureLife(std::int32_t slot);
        [[nodiscard]] static std::int32_t Debit(std::int32_t slot);
        [[nodiscard]] static std::int32_t Push(std::int32_t slot, std::uint32_t frame, std::int32_t damage,
            bool lethal, bool headshot, ::MphRead::BeamType beam, bool self);
        static std::int32_t RetireHead(std::int32_t slot, bool confirmed);
        static void StampClaim(std::int32_t slot, std::int32_t at, std::uint16_t claimId);
        // The authority's drain arrives as the health it reports for this
        // machine's player rising: that much of the drain credited here has
        // landed and is owed no longer, oldest first. Without it the credit
        // stood on top of the drain it predicted until it expired -- the
        // health shown rose twice and then fell back.
        static void SettleDrain(std::int32_t authorityHealth);

        inline static bool _deathEnabled = true;
        inline static OpenTK::Mathematics::Vector3 _impact{};
        inline static bool _impactKnown = false;
        inline static bool _impactSplash = false;
        inline static std::array<std::uint32_t, 8> _killShownFrame{};

        inline static bool _enabled = true;
        inline static bool _markerEnabled = true;

        inline static Pending<std::uint32_t> _pendingFrame{};
        inline static Pending<std::int32_t> _pendingDamage{};
        inline static Pending<bool> _pendingLethal{};
        inline static Pending<bool> _pendingHeadshot{};
        inline static Pending<std::uint8_t> _pendingBeam{};
        inline static Pending<std::uint16_t> _pendingClaim{};
        inline static Pending<bool> _pendingSpent{};
        inline static Pending<bool> _pendingTravelled{};
        inline static Pending<bool> _pendingSelf{};
        inline static std::array<std::int32_t, Slots> _settledCredit{};
        inline static std::array<std::uint32_t, Slots> _settledFrame{};
        inline static std::array<std::int32_t, Slots> _pendingCount{};
        inline static std::array<std::int32_t, Slots> _pendingHead{};
        inline static std::array<std::int32_t, Slots> _shownHealth{};
        inline static std::array<std::uint32_t, Slots> _predictedFrame{};
        inline static std::array<std::int32_t, Slots> _lastAuthorityHealth{};
        inline static std::int64_t _floorLifted = 0;
        inline static std::array<std::int64_t, Slots> _predictedPoints{};
        inline static std::array<std::int64_t, Slots> _authorityDrop{};

        inline static std::array<std::uint32_t, HealCapacity> _healFrame{};
        inline static std::array<std::int32_t, HealCapacity> _healAmount{};
        inline static std::int32_t _healCount = 0;
        inline static std::int32_t _healHead = 0;
        // The authority's health for this machine's player at the last
        // snapshot SettleDrain read (0: none yet this life), and that frame.
        inline static std::int32_t _drainBaseline = 0;
        inline static std::uint32_t _drainSnapshotFrame = 0;

        inline static std::array<std::int64_t, BeamBuckets> _beamPredicted{};
        inline static std::array<std::int64_t, BeamBuckets> _beamConfirmed{};
        inline static std::array<std::int64_t, BeamBuckets> _beamDenied{};
        inline static std::array<std::int64_t, BeamBuckets> _beamLethal{};
        inline static std::array<std::int64_t, BeamBuckets> _beamUndone{};
        inline static std::array<std::int64_t, BeamBuckets> _beamDamage{};

        inline static std::int64_t _predicted = 0;
        inline static std::int64_t _confirmed = 0;
        inline static std::int64_t _denied = 0;
        inline static std::int64_t _unpredicted = 0;
        inline static std::int64_t _unpredictedMoving = 0;
        inline static std::int64_t _unpredictedStill = 0;
        inline static std::int64_t _lethalHeld = 0;
        inline static std::int64_t _deathsPredicted = 0;
        inline static std::int64_t _selfDeathsPredicted = 0;
        inline static std::int64_t _deathsUndone = 0;
        inline static std::int64_t _drainPredicted = 0;
        inline static std::int64_t _headshotsPredicted = 0;
        inline static std::int64_t _headshotsAgreed = 0;
        inline static std::int64_t _headshotsDowngraded = 0;
        inline static std::int64_t _headshotsUpgraded = 0;
        inline static std::int64_t _selfPredicted = 0;
        inline static std::int64_t _selfConfirmed = 0;

        inline static Pending<bool> _pendingHeld{};
        inline static std::int64_t _lethalConfirmed = 0;
        inline static std::int64_t _lethalDenied = 0;

        inline static std::array<std::uint16_t, Slots> _life{};
        inline static std::array<std::uint16_t, Slots> _generation{};
        inline static Pending<std::uint16_t> _pendingLife{};
        inline static Pending<std::uint16_t> _pendingGeneration{};

        inline static std::int32_t _markerTimer = 0;

        inline static std::int64_t _healthSamples = 0;
        inline static std::int64_t _healthDisagreed = 0;
        inline static std::int64_t _healthUnderPoints = 0;
        inline static std::int32_t _healthUnderWorst = 0;
        inline static std::int64_t _healthOverPoints = 0;
        inline static std::int32_t _healthOverWorst = 0;
        inline static std::int64_t _floorHeld = 0;
        inline static std::int64_t _floorHeldPoints = 0;
        inline static std::int32_t _floorWorst = 0;
        inline static std::int64_t _debitPoints = 0;
        inline static std::int32_t _debitWorst = 0;
    };
}
