#include "NetHitPrediction.hpp"
#include "NetPlayerBridge.hpp"

#include "../../Entities/BeamProjectileEntity.hpp"
#include "../../Entities/BombEntity.hpp"
#include "../../Entities/Players/HalfturretEntity.hpp"
#include "../../GameState.hpp"
#include "NetDamage.hpp"
#include "NetHitClaims.hpp"
#include "NetHooks.hpp"
#include "NetLog.hpp"
#include "NetPlayerLifecycle.hpp"
#include "NetSession.hpp"
#include "NetShotDiagnostics.hpp"
#include "../../Formats/Types.hpp"
#include "../../NativeRuntime/System/Globalization.hpp"
#include "../../NativeRuntime/System/Managed.hpp"
#include "../../NativeRuntime/System/Number.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

using ::MphRead::NativeRuntime::HasFlag;

namespace MphRead::Mods::Network
{
    namespace
    {
        namespace Runtime = ::MphRead::NativeRuntime;

        [[nodiscard]] constexpr std::size_t Index(std::int32_t value) noexcept
        {
            return static_cast<std::size_t>(value);
        }
    }

    bool NetHitPrediction::Predicting() noexcept
    {
        return NetSession::Active() && !NetSession::IsHost() && !NetSession::IsAuthority() && !NetDamage::Replaying();
    }

    std::int32_t NetHitPrediction::HoldFrames()
    {
        const std::int32_t slot = NetHooks::LocalSlot();
        const std::int32_t ping = slot >= 0 && slot < static_cast<std::int32_t>(NetSession::SlotPing.size())
            ? NetSession::SlotPing[Index(slot)]
            : 0;
        const std::int32_t grace = NetHitClaims::Claiming() ? NetHitClaims::GraceFrames() : 0;
        return std::clamp(static_cast<std::int32_t>(static_cast<float>(ping) * 0.06F) + 12 + grace, 15, 120);
    }

    std::int32_t NetHitPrediction::Bucket(::MphRead::BeamType beam) noexcept
    {
        const auto index = static_cast<std::int32_t>(beam);
        return index < 0 || index >= AltBeam ? AltBeam : index;
    }

    std::string NetHitPrediction::BucketName(std::int32_t bucket)
    {
        return bucket == AltBeam ? std::string("alt/bomb") : ::MphRead::ToString(static_cast<::MphRead::BeamType>(bucket));
    }

    bool NetHitPrediction::MovingNow()
    {
        const std::int32_t local = NetHooks::LocalSlot();
        return local >= 0 && local < static_cast<std::int32_t>(Entities::PlayerEntity::Players().size())
            && OpenTK::Mathematics::LengthSquared(
                Runtime::RequireReference(Entities::PlayerEntity::Players()[Index(local)]).Speed()) > 0.0004F;
    }

    void NetHitPrediction::ResolveHeld(std::int32_t slot, std::int32_t at, bool confirmed)
    {
        bool& held = _pendingHeld[Index(slot)][Index(at)];
        if (!held)
        {
            return;
        }
        held = false;
        if (confirmed)
        {
            _lethalConfirmed++;
        }
        else
        {
            _lethalDenied++;
        }
        if (NetLog::Enabled())
        {
            NetLog::Event(std::string("[predict] lethal ") + (confirmed ? "confirmed" : "denied")
                + " slot=" + std::to_string(slot) + " generation=" + std::to_string(NetPlayerLifecycle::Generation(slot))
                + " life=" + std::to_string(NetPlayerLifecycle::Get(slot))
                + " frame=" + std::to_string(NetSession::NetFrame()) + " " + LifecycleDetails(slot));
        }
    }

    void NetHitPrediction::EnsureLife(std::int32_t slot)
    {
        if (slot < 0 || slot >= Slots)
        {
            return;
        }
        const auto s = Index(slot);
        if (_life[s] != NetPlayerLifecycle::Get(slot) || _generation[s] != NetPlayerLifecycle::Generation(slot))
        {
            ForgetSlot(slot);
            _life[s] = NetPlayerLifecycle::Get(slot);
            _generation[s] = NetPlayerLifecycle::Generation(slot);
        }
    }

    std::string NetHitPrediction::LifecycleDetails(std::int32_t slot)
    {
        return "pending=" + std::to_string(Runtime::ManagedAt(_pendingCount, slot))
            + " debit=" + std::to_string(Debit(slot))
            + " authorityHealth=" + std::to_string(Runtime::ManagedAt(_lastAuthorityHealth, slot))
            + " heldDead=" + (HeldDead(slot) ? "True" : "False");
    }

    std::string NetHitPrediction::HealthDetails(std::int32_t slot)
    {
        if (slot < 0 || slot >= Slots)
        {
            return "invalid slot";
        }
        const auto s = Index(slot);
        if (!NetPlayerLifecycle::Matches(slot, _generation[s], _life[s]))
        {
            return "predictedDebit=0 shownFloor=0 pending=0 lastAuthorityHP=0";
        }
        return "predictedDebit=" + std::to_string(Debit(slot)) + " shownFloor=" + std::to_string(_shownHealth[s])
            + " pending=" + std::to_string(_pendingCount[s]) + " lastAuthorityHP=" + std::to_string(_lastAuthorityHealth[s]);
    }

    float NetHitPrediction::MarkerAlpha() noexcept
    {
        if (!_markerEnabled || _markerTimer <= 0)
        {
            return 0.0F;
        }
        constexpr std::int32_t fade = 6;
        return _markerTimer >= fade ? 1.0F : static_cast<float>(_markerTimer) / static_cast<float>(fade);
    }

    void NetHitPrediction::Reset()
    {
        _life.fill(0);
        _generation.fill(0);
        _pendingLife = {};
        _pendingGeneration = {};
        _pendingFrame = {};
        _pendingDamage = {};
        _pendingLethal = {};
        _pendingHeadshot = {};
        _pendingBeam = {};
        _pendingClaim = {};
        _pendingSpent = {};
        _pendingTravelled = {};
        _pendingSelf = {};
        _settledCredit.fill(0);
        _settledFrame.fill(0);
        _beamPredicted.fill(0);
        _beamConfirmed.fill(0);
        _beamDenied.fill(0);
        _beamLethal.fill(0);
        _beamUndone.fill(0);
        _beamDamage.fill(0);
        _pendingCount.fill(0);
        _pendingHead.fill(0);
        _healFrame.fill(0);
        _healAmount.fill(0);
        _healCount = 0;
        _healHead = 0;
        _predicted = 0;
        _confirmed = 0;
        _selfPredicted = 0;
        _selfConfirmed = 0;
        _denied = 0;
        _unpredicted = 0;
        _unpredictedMoving = 0;
        _unpredictedStill = 0;
        _lethalHeld = _lethalConfirmed = _lethalDenied = 0;
        _pendingHeld = {};
        _deathsPredicted = 0;
        _selfDeathsPredicted = 0;
        _deathsUndone = 0;
        _shownHealth.fill(0);
        _predictedFrame.fill(0);
        _healthSamples = 0;
        _healthDisagreed = 0;
        _healthUnderPoints = 0;
        _healthUnderWorst = 0;
        _healthOverPoints = 0;
        _healthOverWorst = 0;
        _floorLifted = 0;
        _lastAuthorityHealth.fill(0);
        _predictedPoints.fill(0);
        _authorityDrop.fill(0);
        _floorHeld = 0;
        _floorHeldPoints = 0;
        _floorWorst = 0;
        _debitPoints = 0;
        _debitWorst = 0;
        _drainPredicted = 0;
        _headshotsPredicted = 0;
        _headshotsAgreed = 0;
        _headshotsDowngraded = 0;
        _headshotsUpgraded = 0;
        _markerTimer = 0;
    }

    bool NetHitPrediction::Predicts(Entities::PlayerEntity& victim, Entities::EntityBase* source, Entities::DamageFlags flags)
    {
        if (!_enabled || victim.Health() <= 0)
        {
            return false;
        }
        const std::int32_t local = NetHooks::LocalSlot();
        if (local < 0)
        {
            return false;
        }
        if (source == nullptr)
        {
            return victim.SlotIndex() == local && HasFlag(flags, Entities::DamageFlags::Death);
        }
        // A burn's damage over time is the authority's, which knows the
        // burner: a tick predicted here only fought the snapshots, which put
        // out this machine's fire until the authority had lit its own, and
        // then nobody's ticks landed at all.
        if (HasFlag(flags, Entities::DamageFlags::Burn))
        {
            return false;
        }
        Entities::PlayerEntity* owner = OwnerOf(source);
        if (owner == nullptr || owner->SlotIndex() != local)
        {
            return false;
        }
        return true;
    }

    Entities::PlayerEntity* NetHitPrediction::OwnerOf(Entities::EntityBase* source)
    {
        if (auto* beam = dynamic_cast<Entities::BeamProjectileEntity*>(source))
        {
            if (std::shared_ptr<Entities::PlayerEntity> player
                = std::dynamic_pointer_cast<Entities::PlayerEntity>(beam->Owner()))
            {
                return player.get();
            }
            if (std::shared_ptr<Entities::HalfturretEntity> turret
                = std::dynamic_pointer_cast<Entities::HalfturretEntity>(beam->Owner()))
            {
                return turret->Owner().get();
            }
            return nullptr;
        }
        if (auto* bomb = dynamic_cast<Entities::BombEntity*>(source))
        {
            return bomb->Owner();
        }
        if (auto* attacker = dynamic_cast<Entities::PlayerEntity*>(source))
        {
            return attacker;
        }
        return nullptr;
    }

    void NetHitPrediction::NoteHit(Entities::PlayerEntity& victim, Entities::PlayerEntity* attacker,
        Entities::DamageFlags& flags, std::uint32_t& damage, ::MphRead::BeamType beam,
        std::uint32_t launchFrame, float flight, std::optional<OpenTK::Mathematics::Vector3> impulse,
        ::MphRead::Affliction afflictions, std::uint32_t shotSequence, std::uint32_t turretDamage)
    {
        const std::int32_t local = NetHooks::LocalSlot();
        if (local < 0)
        {
            return;
        }
        bool self = false;
        if (attacker == nullptr)
        {
            if (victim.SlotIndex() != local)
            {
                return;
            }
            self = true;
        }
        else
        {
            if (attacker->SlotIndex() != local)
            {
                return;
            }
            self = attacker == &victim;
        }
        if (Predicting() && _enabled)
        {
            const std::int32_t victimSlot = victim.SlotIndex();
            bool lethal = victim.Health() > 0
                && (damage >= static_cast<std::uint32_t>(victim.Health()) || HasFlag(flags, Entities::DamageFlags::Death));
            const std::uint32_t claimedDamage = damage;
            const auto weapon = Index(NetShotDiagnostics::Bucket(beam));
            Runtime::IncrementInPlace(NetShotDiagnostics::LocalHits[weapon]);
            Runtime::IncrementInPlace(NetShotDiagnostics::Predictions[weapon]);
            NetShotDiagnostics::PredictedDamage[weapon] = Runtime::UncheckedAdd(
                NetShotDiagnostics::PredictedDamage[weapon], static_cast<std::int64_t>(damage));
            if (HasFlag(flags, Entities::DamageFlags::Headshot))
            {
                Runtime::IncrementInPlace(NetShotDiagnostics::LocalHeadshots[weapon]);
            }
            if (attacker != nullptr && NetLog::Enabled())
            {
                NetShotDiagnostics::Trace("prediction", ShotKey::For(attacker->SlotIndex(), launchFrame), beam,
                    "victim=" + std::to_string(victimSlot) + " damage=" + std::to_string(damage));
            }
            const bool claimedLethal = lethal;
            if (lethal && !self && !DeathEnabled())
            {
                damage = static_cast<std::uint32_t>(std::max(0, victim.Health() - 1));
                flags = static_cast<Entities::DamageFlags>(static_cast<std::int32_t>(flags)
                    & ~static_cast<std::int32_t>(Entities::DamageFlags::Death));
                _lethalHeld++;
                lethal = false;
                if (NetLog::Enabled())
                {
                    NetLog::Event("[predict] lethal held slot=" + std::to_string(victimSlot)
                        + " life=" + std::to_string(NetPlayerLifecycle::Get(victimSlot))
                        + " frame=" + std::to_string(NetSession::NetFrame()));
                }
            }
            const bool headshot = HasFlag(flags, Entities::DamageFlags::Headshot);
            if (!self)
            {
                Runtime::ManagedAt(_predictedPoints, victimSlot) +=
                    std::min(static_cast<std::int32_t>(damage), std::max(0, victim.Health()));
            }
            const std::int32_t at = Push(victimSlot, NetSession::NetFrame(), static_cast<std::int32_t>(damage),
                lethal, headshot, beam, self);
            if (at >= 0)
            {
                _pendingHeld[Index(victimSlot)][Index(at)] = claimedLethal && !self;
                _pendingTravelled[Index(victimSlot)][Index(at)] = !self && flight > TravelFlight;
            }
            // Hits on itself too: its own shot and its own body are both on
            // this machine, exactly where they are -- the authority's copy is
            // a trip behind and walking somewhere else.
            if (attacker != nullptr)
            {
                const std::uint16_t claimId = NetHitClaims::Declare(victim, *attacker, beam, claimedDamage,
                    flags, claimedLethal, victim.Position, launchFrame, impulse, afflictions,
                    _impactKnown ? std::optional<OpenTK::Mathematics::Vector3>(_impact) : std::nullopt,
                    _impactSplash, shotSequence, turretDamage);
                StampClaim(victimSlot, at, claimId);
            }
            if (headshot && !self)
            {
                _headshotsPredicted++;
            }
            if (self)
            {
                _selfPredicted++;
            }
            else
            {
                _predicted++;
            }
            if (lethal)
            {
                if (self)
                {
                    _selfDeathsPredicted++;
                }
                else
                {
                    _deathsPredicted++;
                    if (victimSlot >= 0 && victimSlot < static_cast<std::int32_t>(_killShownFrame.size()))
                    {
                        _killShownFrame[Index(victimSlot)] = std::max(1U, NetSession::NetFrame());
                    }
                }
            }
        }
        if (!GameState::SinglePlayer() && !self)
        {
            _markerTimer = MarkerFrames;
        }
    }

    bool NetHitPrediction::Confirm(std::int32_t slot, std::int32_t landed, bool authorityHeadshot)
    {
        EnsureLife(slot);
        if (slot < 0 || slot >= Slots)
        {
            return false;
        }
        const auto s = Index(slot);
        if (_pendingCount[s] == 0)
        {
            const std::int32_t owed = std::max(1, landed);
            if (NetSession::NetFrame() - _settledFrame[s] >= static_cast<std::uint32_t>(HoldFrames()))
            {
                _settledCredit[s] = 0;
            }
            const std::int32_t fromSettled = std::min(owed, _settledCredit[s]);
            _settledCredit[s] -= fromSettled;
            const std::int32_t rest = owed - fromSettled;
            if (rest > 0)
            {
                _unpredicted += rest;
                if (MovingNow())
                {
                    _unpredictedMoving += rest;
                }
                else
                {
                    _unpredictedStill += rest;
                }
            }
            return fromSettled > 0;
        }
        const bool self = slot == NetHooks::LocalSlot();
        const std::int32_t take = std::clamp(landed, 1, _pendingCount[s]);
        if (landed > take)
        {
            const std::int32_t spare = landed - take;
            _settledCredit[s] -= std::min(spare, _settledCredit[s]);
        }
        for (std::int32_t i = 0; i < take; i++)
        {
            const std::int32_t head = _pendingHead[s];
            const bool predictedHeadshot = _pendingHeadshot[s][Index(head)];
            _pendingHeadshot[s][Index(head)] = false;
            if (RetireHead(slot, true) < 0)
            {
                continue;
            }
            if (self)
            {
                _selfConfirmed++;
                continue;
            }
            _confirmed++;
            if (i != take - 1)
            {
                continue;
            }
            if (predictedHeadshot && authorityHeadshot)
            {
                _headshotsAgreed++;
            }
            else if (predictedHeadshot)
            {
                _headshotsDowngraded++;
            }
            else if (authorityHeadshot)
            {
                _headshotsUpgraded++;
            }
        }
        return true;
    }

    void NetHitPrediction::ForgetSlot(std::int32_t slot)
    {
        if (slot < 0 || slot >= Slots)
        {
            return;
        }
        const auto s = Index(slot);
        for (std::size_t i = 0; i < PendingCapacity; i++)
        {
            _pendingFrame[s][i] = 0;
            _pendingDamage[s][i] = 0;
            _pendingLethal[s][i] = false;
            _pendingHeld[s][i] = false;
            _pendingHeadshot[s][i] = false;
            _pendingBeam[s][i] = AltBeam;
            _pendingClaim[s][i] = 0;
            _pendingSpent[s][i] = false;
            _pendingTravelled[s][i] = false;
            _pendingSelf[s][i] = false;
        }
        if (slot == NetHooks::LocalSlot())
        {
            _healCount = 0;
            _healHead = 0;
            _drainBaseline = 0;
        }
        _pendingCount[s] = 0;
        _pendingHead[s] = 0;
        _settledCredit[s] = 0;
        _shownHealth[s] = 0;
        _predictedFrame[s] = 0;
        _lastAuthorityHealth[s] = 0;
        _settledCredit[s] = 0;
        _settledFrame[s] = 0;
    }

    void NetHitPrediction::NoteRespawn(std::int32_t slot)
    {
        if (slot < 0 || slot >= Slots)
        {
            return;
        }
        _killShownFrame[Index(slot)] = 0;
        if (slot == NetSession::LocalSlot())
        {
            ForgetPending();
        }
        else
        {
            ForgetSlot(slot);
        }
    }

    void NetHitPrediction::NoteDeath(std::int32_t slot)
    {
        if (slot < 0 || slot >= Slots)
        {
            return;
        }
        // The authority has the death now: nothing shown here is waiting.
        _killShownFrame[Index(slot)] = 0;
        for (std::size_t i = 0; i < PendingCapacity; i++)
        {
            _pendingLethal[Index(slot)][i] = false;
            _pendingHeld[Index(slot)][i] = false;
        }
    }

    void NetHitPrediction::ForgetPending()
    {
        for (std::int32_t slot = 0; slot < Slots; slot++)
        {
            ForgetSlot(slot);
        }
        _healCount = 0;
        _healHead = 0;
    }

    void NetHitPrediction::NoteDrain(Entities::PlayerEntity& healer, std::int32_t amount)
    {
        if (!_enabled || !Predicting() || amount <= 0)
        {
            return;
        }
        const std::int32_t local = NetHooks::LocalSlot();
        if (local < 0 || healer.SlotIndex() != local)
        {
            return;
        }
        if (_healCount == HealCapacity)
        {
            _healHead = (_healHead + 1) % HealCapacity;
            _healCount--;
        }
        const std::int32_t tail = (_healHead + _healCount) % HealCapacity;
        _healFrame[Index(tail)] = NetSession::NetFrame();
        _healAmount[Index(tail)] = amount;
        _healCount++;
        _drainPredicted += amount;
        auto& drainCredit = NetShotDiagnostics::DrainCredit[
            Index(NetShotDiagnostics::Bucket(::MphRead::BeamType::ShockCoil))];
        drainCredit = Runtime::UncheckedAdd(drainCredit, static_cast<std::int64_t>(amount));
    }

    std::int32_t NetHitPrediction::Debit(std::int32_t slot)
    {
        EnsureLife(slot);
        if (!_enabled || slot < 0 || slot >= Slots || _pendingCount[Index(slot)] == 0)
        {
            return 0;
        }
        const auto s = Index(slot);
        const std::uint32_t now = NetSession::NetFrame();
        const std::int32_t hold = HoldFrames();
        std::int32_t debit = 0;
        for (std::int32_t i = 0; i < _pendingCount[s]; i++)
        {
            const auto at = Index((_pendingHead[s] + i) % PendingCapacity);
            if (NetPlayerLifecycle::Matches(slot, _pendingGeneration[s][at], _pendingLife[s][at])
                && now - _pendingFrame[s][at] < static_cast<std::uint32_t>(hold))
            {
                if (_pendingTravelled[s][at])
                {
                    continue;
                }
                debit += _pendingDamage[s][at];
            }
        }
        return debit;
    }

    std::int32_t NetHitPrediction::HealthFor(std::int32_t slot, std::int32_t authorityHealth)
    {
        EnsureLife(slot);
        if (!_enabled || slot < 0 || slot >= Slots)
        {
            return authorityHealth;
        }
        const auto s = Index(slot);
        if (authorityHealth > _lastAuthorityHealth[s] && _shownHealth[s] > 0)
        {
            _shownHealth[s] = 0;
            _floorLifted++;
        }
        if (_lastAuthorityHealth[s] > authorityHealth && authorityHealth > 0)
        {
            _authorityDrop[s] += _lastAuthorityHealth[s] - authorityHealth;
        }
        _lastAuthorityHealth[s] = authorityHealth;
        const std::int32_t debit = Debit(slot);
        std::int32_t health = debit > 0 && authorityHealth > 1 ? std::max(1, authorityHealth - debit) : authorityHealth;
        const std::int32_t owed = health;
        if (authorityHealth > 0 && _shownHealth[s] > 0
            && NetSession::NetFrame() - _predictedFrame[s] < static_cast<std::uint32_t>(HoldFrames()))
        {
            health = std::min(health, _shownHealth[s]);
            health = std::max(1, health);
        }
        if (authorityHealth > 0)
        {
            _healthSamples++;
            if (health < owed)
            {
                _floorHeld++;
                _floorHeldPoints += owed - health;
                _floorWorst = std::max(_floorWorst, owed - health);
            }
            if (debit == 0 && health != authorityHealth)
            {
                _healthDisagreed++;
                const std::int32_t gap = authorityHealth - health;
                if (gap > 0)
                {
                    _healthUnderPoints += gap;
                    _healthUnderWorst = std::max(_healthUnderWorst, gap);
                }
                else
                {
                    _healthOverPoints += -gap;
                    _healthOverWorst = std::max(_healthOverWorst, -gap);
                }
            }
            _debitPoints += debit;
            _debitWorst = std::max(_debitWorst, debit);
        }
        _shownHealth[s] = health;
        return health;
    }

    std::string NetHitPrediction::DescribeDamageLedger()
    {
        std::string text = "damage ledger (predicted / authority removed):";
        bool any = false;
        for (std::size_t slot = 0; slot < Slots; slot++)
        {
            if (_predictedPoints[slot] == 0 && _authorityDrop[slot] == 0)
            {
                continue;
            }
            any = true;
            const std::string ratio = _authorityDrop[slot] > 0
                ? " x" + Runtime::ToString(static_cast<double>(_predictedPoints[slot])
                    / static_cast<double>(_authorityDrop[slot]), "F2")
                : std::string();
            text += " slot " + std::to_string(slot) + " " + std::to_string(_predictedPoints[slot]) + "/"
                + std::to_string(_authorityDrop[slot]) + ratio + ";";
        }
        return any ? text : std::string("damage ledger: nothing predicted onto anybody");
    }

    std::string NetHitPrediction::DescribeHealth()
    {
        if (_healthSamples == 0)
        {
            return "health bars: nothing drawn for anybody else";
        }
        return "health bars: " + std::to_string(_healthSamples) + " sample(s); debit mean "
            + Runtime::ToString(static_cast<double>(_debitPoints) / static_cast<double>(_healthSamples), "F2")
            + " worst " + std::to_string(_debitWorst) + "; floor held " + std::to_string(_floorHeld)
            + " (" + std::to_string(_floorHeldPoints) + " point(s), worst " + std::to_string(_floorWorst)
            + "), lifted " + std::to_string(_floorLifted) + "; disagreed with nothing outstanding "
            + std::to_string(_healthDisagreed) + " -- drawn low by " + std::to_string(_healthUnderPoints)
            + " point(s) (worst " + std::to_string(_healthUnderWorst) + "), high by "
            + std::to_string(_healthOverPoints) + " (worst " + std::to_string(_healthOverWorst) + ")";
    }

    void NetHitPrediction::SettleDrain(std::int32_t authorityHealth)
    {
        const std::uint32_t frame = NetSession::AppliedSnapshotFrame();
        if (frame == _drainSnapshotFrame)
        {
            return;
        }
        _drainSnapshotFrame = frame;
        std::int32_t landed = _drainBaseline > 0 && authorityHealth > _drainBaseline
            ? authorityHealth - _drainBaseline : 0;
        _drainBaseline = authorityHealth;
        while (landed > 0 && _healCount > 0)
        {
            std::int32_t& owed = _healAmount[Index(_healHead)];
            const std::int32_t taken = std::min(owed, landed);
            owed -= taken;
            landed -= taken;
            if (owed == 0)
            {
                _healHead = (_healHead + 1) % HealCapacity;
                _healCount--;
            }
        }
    }

    std::int32_t NetHitPrediction::LocalHealthFor(Entities::PlayerEntity& player, std::int32_t authorityHealth)
    {
        EnsureLife(player.SlotIndex());
        SettleDrain(authorityHealth);
        if (!_enabled || authorityHealth <= 0)
        {
            return authorityHealth;
        }
        const std::uint32_t now = NetSession::NetFrame();
        const std::int32_t hold = HoldFrames();
        std::int32_t credit = 0;
        for (std::int32_t i = 0; i < _healCount; i++)
        {
            const auto at = Index((_healHead + i) % HealCapacity);
            if (now - _healFrame[at] < static_cast<std::uint32_t>(hold))
            {
                credit += _healAmount[at];
            }
        }
        credit -= Debit(NetHooks::LocalSlot());
        if (player.Health() <= 0 && HeldDead(NetHooks::LocalSlot()))
        {
            return 0;
        }
        if (credit == 0)
        {
            return authorityHealth;
        }
        const std::int32_t max = player.HealthMax() > 0 ? player.HealthMax() : authorityHealth;
        return std::clamp(authorityHealth + credit, 1, max);
    }

    bool NetHitPrediction::ShowingKill(std::int32_t slot)
    {
        if (slot < 0 || slot >= static_cast<std::int32_t>(_killShownFrame.size()))
        {
            return false;
        }
        const std::uint32_t shown = _killShownFrame[Index(slot)];
        // The authority's death comes back a round trip plus the claim's
        // grace window later; past that, it is not coming.
        return shown != 0 && NetSession::NetFrame() - shown < static_cast<std::uint32_t>(HoldFrames() + 30);
    }

    std::int32_t NetHitPrediction::KillsShown()
    {
        std::int32_t count = 0;
        for (std::int32_t slot = 0; slot < static_cast<std::int32_t>(_killShownFrame.size()); slot++)
        {
            count += ShowingKill(slot) ? 1 : 0;
        }
        return count;
    }

    bool NetHitPrediction::HeldDead(std::int32_t slot)
    {
        EnsureLife(slot);
        if (!_enabled || slot < 0 || slot >= Slots)
        {
            return false;
        }
        if (slot != NetHooks::LocalSlot())
        {
            return false;
        }
        const auto s = Index(slot);
        const std::uint32_t now = NetSession::NetFrame();
        const std::int32_t hold = HoldFrames();
        for (std::int32_t i = 0; i < _pendingCount[s]; i++)
        {
            const auto at = Index((_pendingHead[s] + i) % PendingCapacity);
            if (_pendingLethal[s][at] && now - _pendingFrame[s][at] < static_cast<std::uint32_t>(hold))
            {
                return true;
            }
        }
        return false;
    }

    void NetHitPrediction::Tick()
    {
        if (_markerTimer > 0)
        {
            _markerTimer--;
        }
        if (!NetSession::Active())
        {
            return;
        }
        const std::uint32_t now = NetSession::NetFrame();
        for (std::int32_t slot = 0; slot < Slots; slot++)
        {
            const auto s = Index(slot);
            while (_pendingCount[s] > 0)
            {
                const std::uint32_t frame = _pendingFrame[s][Index(_pendingHead[s])];
                if (now - frame < PendingFrames)
                {
                    break;
                }
                static_cast<void>(RetireHead(slot, false));
            }
        }
        while (_healCount > 0 && now - _healFrame[Index(_healHead)] >= PendingFrames)
        {
            _healHead = (_healHead + 1) % HealCapacity;
            _healCount--;
        }
    }

    std::int32_t NetHitPrediction::Push(std::int32_t slot, std::uint32_t frame, std::int32_t damage, bool lethal,
        bool headshot, ::MphRead::BeamType beam, bool self)
    {
        EnsureLife(slot);
        if (slot < 0 || slot >= Slots)
        {
            return -1;
        }
        const auto s = Index(slot);
        if (_pendingCount[s] == PendingCapacity)
        {
            static_cast<void>(RetireHead(slot, false));
        }
        _predictedFrame[s] = frame;
        const std::int32_t tail = (_pendingHead[s] + _pendingCount[s]) % PendingCapacity;
        const auto t = Index(tail);
        _pendingLife[s][t] = NetPlayerLifecycle::Get(slot);
        _pendingGeneration[s][t] = NetPlayerLifecycle::Generation(slot);
        _pendingFrame[s][t] = frame;
        _pendingDamage[s][t] = std::max(0, damage);
        _pendingLethal[s][t] = lethal;
        _pendingHeadshot[s][t] = headshot;
        _pendingBeam[s][t] = static_cast<std::uint8_t>(Bucket(beam));
        _pendingClaim[s][t] = 0;
        _pendingSpent[s][t] = false;
        _pendingTravelled[s][t] = false;
        _pendingSelf[s][t] = self;
        _pendingCount[s]++;
        if (!self)
        {
            const auto bucket = Index(Bucket(beam));
            _beamPredicted[bucket]++;
            _beamDamage[bucket] += std::max(0, damage);
            if (lethal)
            {
                _beamLethal[bucket]++;
            }
        }
        return tail;
    }

    std::int32_t NetHitPrediction::RetireHead(std::int32_t slot, bool confirmed)
    {
        const auto s = Index(slot);
        const std::int32_t head = _pendingHead[s];
        const auto h = Index(head);
        ResolveHeld(slot, head, confirmed);
        const bool spent = _pendingSpent[s][h] || _pendingSelf[s][h];
        const std::int32_t bucket = _pendingBeam[s][h];
        if (!spent && bucket >= 0 && bucket < BeamBuckets)
        {
            if (confirmed)
            {
                _beamConfirmed[Index(bucket)]++;
            }
            else
            {
                _beamDenied[Index(bucket)]++;
            }
        }
        const bool answered = _pendingSpent[s][h];
        if (!answered && !confirmed && !_pendingSelf[s][h])
        {
            _denied++;
            if (_pendingLethal[s][h])
            {
                // Never answered: the next snapshot decides, not this guess.
                NetPlayerBridge::Resync(slot);
                _deathsUndone++;
            }
        }
        _pendingClaim[s][h] = 0;
        _pendingSpent[s][h] = false;
        _pendingSelf[s][h] = false;
        _pendingHead[s] = (head + 1) % PendingCapacity;
        _pendingCount[s]--;
        return answered ? -1 : head;
    }

    void NetHitPrediction::Settle(std::int32_t slot, std::uint16_t claimId, bool confirmed, bool victimDown)
    {
        EnsureLife(slot);
        if (!_enabled || slot < 0 || slot >= Slots || claimId == 0 || _pendingCount[Index(slot)] == 0)
        {
            return;
        }
        const auto s = Index(slot);
        for (std::int32_t i = 0; i < _pendingCount[s]; i++)
        {
            const std::int32_t at = (_pendingHead[s] + i) % PendingCapacity;
            const auto a = Index(at);
            if (_pendingClaim[s][a] != claimId)
            {
                continue;
            }
            ResolveHeld(slot, at, confirmed);
            const std::int32_t bucket = _pendingSelf[s][a] ? -1 : _pendingBeam[s][a];
            if (bucket >= 0 && bucket < BeamBuckets)
            {
                if (confirmed)
                {
                    _beamConfirmed[Index(bucket)]++;
                }
                else
                {
                    _beamDenied[Index(bucket)]++;
                }
            }
            if (confirmed && _pendingSelf[s][a])
            {
                // A hit on itself, claimed and applied.
                _selfConfirmed++;
            }
            else if (confirmed)
            {
                _confirmed++;
                if (_settledCredit[s] < SettledCreditMax)
                {
                    _settledCredit[s]++;
                }
                _settledFrame[s] = NetSession::NetFrame();
            }
            else
            {
                _denied++;
                if (_pendingLethal[s][a] && !victimDown)
                {
                    // A kill shown here that the authority refused: put the
                    // player back as the authority has them.
                    NetPlayerBridge::Resync(slot);
                    _deathsUndone++;
                    if (bucket >= 0 && bucket < BeamBuckets)
                    {
                        _beamUndone[Index(bucket)]++;
                    }
                }
                _shownHealth[s] = 0;
            }
            _pendingClaim[s][a] = 0;
            _pendingSpent[s][a] = true;
            if (confirmed)
            {
                return;
            }
            _pendingDamage[s][a] = 0;
            _pendingLethal[s][a] = false;
            _pendingHeadshot[s][a] = false;
            while (_pendingCount[s] > 0 && _pendingSpent[s][Index(_pendingHead[s])]
                && _pendingDamage[s][Index(_pendingHead[s])] == 0)
            {
                _pendingSpent[s][Index(_pendingHead[s])] = false;
                _pendingHead[s] = (_pendingHead[s] + 1) % PendingCapacity;
                _pendingCount[s]--;
            }
            return;
        }
    }

    void NetHitPrediction::StampClaim(std::int32_t slot, std::int32_t at, std::uint16_t claimId)
    {
        if (claimId != 0 && slot >= 0 && slot < Slots && at >= 0 && at < PendingCapacity)
        {
            _pendingClaim[Index(slot)][Index(at)] = claimId;
        }
    }

    std::string NetHitPrediction::Describe()
    {
        if (!_enabled)
        {
            return "hit prediction: off";
        }
        if (_predicted == 0)
        {
            const std::string own = _selfPredicted > 0
                ? ", " + std::to_string(_selfPredicted) + " self-hits predicted (" + std::to_string(_selfConfirmed)
                    + " confirmed, " + std::to_string(_selfDeathsPredicted) + " of them lethal)"
                : std::string();
            return "hit prediction: on, nothing predicted here (" + std::to_string(_unpredicted)
                + " hits arrived from the authority)" + own;
        }
        const double agreed = static_cast<double>(_confirmed) * 100.0 / static_cast<double>(_predicted);
        std::string deaths = DeathEnabled()
            ? std::to_string(_deathsPredicted) + " kills predicted, " + std::to_string(_deathsUndone) + " undone, " + std::to_string(NetPlayerBridge::KillsResynced()) + " stood back up unconfirmed"
            : std::to_string(_lethalHeld) + " lethal hits held (" + std::to_string(_lethalConfirmed) + " confirmed, "
                + std::to_string(_lethalDenied) + " denied)";
        if (_selfDeathsPredicted > 0)
        {
            deaths += ", " + std::to_string(_selfDeathsPredicted) + " self-kills predicted";
        }
        const std::string drain = _drainPredicted > 0
            ? ", " + std::to_string(_drainPredicted) + " health drained ahead" : std::string();
        const std::string self = _selfPredicted > 0
            ? ", " + std::to_string(_selfPredicted) + " self-hits predicted (" + std::to_string(_selfConfirmed) + " confirmed)"
            : std::string();
        return "hit prediction: " + std::to_string(_predicted) + " predicted, " + std::to_string(_confirmed)
            + " confirmed (" + Runtime::ToString(agreed, "F1") + "%), " + std::to_string(_denied) + " denied, "
            + std::to_string(_unpredicted) + " unpredicted, " + deaths + drain + self;
    }

    std::string NetHitPrediction::DescribeByWeapon()
    {
        std::string text = "hit prediction by weapon:";
        bool any = false;
        for (std::size_t i = 0; i < BeamBuckets; i++)
        {
            if (_beamPredicted[i] == 0)
            {
                continue;
            }
            any = true;
            const std::int64_t answered = _beamConfirmed[i] + _beamDenied[i];
            const std::string rate = answered > 0
                ? Runtime::ToString(static_cast<double>(_beamConfirmed[i]) * 100.0 / static_cast<double>(answered), "F0") + "%"
                : std::string("unanswered");
            text += "\n  " + Runtime::StringPadRight(BucketName(static_cast<std::int32_t>(i)), 13) + " "
                + Runtime::StringPadLeft(std::to_string(_beamPredicted[i]), 5) + " predicted, "
                + Runtime::StringPadLeft(std::to_string(_beamConfirmed[i]), 5) + " confirmed (" + rate + "), "
                + Runtime::StringPadLeft(std::to_string(_beamDenied[i]), 5) + " denied, "
                + Runtime::StringPadLeft(std::to_string(_beamDamage[i]), 6) + " damage";
            if (_beamLethal[i] > 0 || _beamUndone[i] > 0)
            {
                text += ", " + std::to_string(_beamLethal[i]) + " kills (" + std::to_string(_beamUndone[i]) + " undone)";
            }
        }
        if (!any)
        {
            return "hit prediction by weapon: nothing predicted here";
        }
        return text;
    }

    std::string NetHitPrediction::DescribeHeadshots()
    {
        const std::int64_t answered = _headshotsAgreed + _headshotsDowngraded;
        if (_headshotsPredicted == 0 && _headshotsUpgraded == 0)
        {
            return "headshots: none predicted here";
        }
        std::string text = "headshots: " + std::to_string(_headshotsPredicted) + " predicted";
        if (answered > 0)
        {
            text += ", " + std::to_string(_headshotsAgreed) + " agreed by the authority ("
                + Runtime::ToString(static_cast<double>(_headshotsAgreed) * 100.0 / static_cast<double>(answered), "F1")
                + "%), " + std::to_string(_headshotsDowngraded) + " downgraded to body shots";
        }
        else
        {
            text += ", none answered yet";
        }
        if (_headshotsUpgraded > 0)
        {
            text += ", " + std::to_string(_headshotsUpgraded)
                + " the authority called a headshot and this machine did not";
        }
        return text;
    }
}
