#include "Mods/Network/Gameplay/Weapons/NetShotEvents.hpp"

#include "Mods/Network/Integration/Hooks/NetHooks.hpp"
#include "Mods/Network/Bots/ServerBots.hpp"
#include "Mods/Network/Session/NetSession.hpp"
#include "Mods/Network/Gameplay/Combat/NetUnlagged.hpp"
#include "Mods/Network/Gameplay/Player/NetPlayerLifecycle.hpp"
#include "Entities/Players/PlayerEntity.hpp"
#include "Metadata/Weapons.hpp"
#include "Mods/Network/Gameplay/Weapons/FiringVariant.hpp"
#include "NativeRuntime/System/Managed.hpp"

#include <algorithm>
#include <optional>
#include <utility>

namespace MphRead::Mods::Network
{
    static_assert(NetShotEvents::Slots == Entities::PlayerEntity::SlotCapacity);
    static_assert(LocalShotLog::RepeatFrames == RemoteShotQueue::FreshFrames,
        "a shot is repeated exactly as long as a receiver may still fire it");
    static_assert(ShotEventEligibilityTimeline::Capacity
        >= static_cast<std::size_t>(NetUnlagged::HistoryFrames),
        "shot eligibility history must cover the hit-claim rewind window");
    static_assert(ShotEventEligibilityTimeline::Capacity > RemoteShotQueue::FreshFrames,
        "shot eligibility history must cover the remote firing queue");

    namespace
    {
        [[nodiscard]] bool Continuous(const Entities::PlayerEntity& shooter) noexcept
        {
            const auto& weapon = NativeRuntime::RequireReference(shooter.EquipInfo()).Weapon;
            return weapon != nullptr && ::MphRead::TestFlag(weapon->Flags, ::MphRead::WeaponFlags::Continuous);
        }

    }

    bool NetShotEvents::Drives(const Entities::PlayerEntity& shooter) noexcept
    {
        const std::int32_t slot = shooter.SlotIndex();
        return NetSession::Active() && slot >= 0 && slot < Slots && slot != NetHooks::LocalSlot()
            && !shooter.IsBot() && _remote[static_cast<std::size_t>(slot)].Active();
    }

    LocalShotLog* NetShotEvents::LogFor(const Entities::PlayerEntity& shooter) noexcept
    {
        const std::int32_t slot = shooter.SlotIndex();
        if (!NetSession::Active() || slot < 0 || slot >= Slots)
        {
            return nullptr;
        }
        if (NetSession::IsAuthority())
        {
            // The authority's own shots are resolved where they are fired;
            // a bot's are also told to every client, which draws them.
            return shooter.IsBot() && ServerBots::Holds(slot) ? &_bots[static_cast<std::size_t>(slot)] : nullptr;
        }
        return slot == NetHooks::LocalSlot() ? &_local : nullptr;
    }

    bool NetShotEvents::Records(const Entities::PlayerEntity& shooter) noexcept
    {
        return LogFor(shooter) != nullptr;
    }

    void NetShotEvents::Fired(Entities::PlayerEntity& shooter, const FiredShot& shot)
    {
        if (!NetSession::Active())
        {
            return;
        }
        if (shot.Continuous)
        {
            // A tick names only the lock it took itself: one that spawns
            // without looking (something at the muzzle) leaves none.
            const std::int32_t slot = shooter.SlotIndex();
            if (Records(shooter) && slot >= 0 && slot < Slots)
            {
                RecordContinuousTarget(slot, {});
                _continuousFire[static_cast<std::size_t>(slot)] = ContinuousFire{true,
                    shot.WeaponVariantIndex != IntentPacket::NoVariant
                        ? shot.PrimeHunter : FiringVariant::For(shooter).PrimeHunter,
                    FiringVariant::RoleFrame()};
            }
            return;
        }
        if (Records(shooter))
        {
            IntentPacket::ShotEvent event{};
            event.Frame = std::max(1U, NetSession::NetFrame());
            event.WeaponId = static_cast<std::uint8_t>(shot.Weapon);
            event.Charge = static_cast<std::uint8_t>(std::min<std::uint16_t>(
                NativeRuntime::RequireReference(shooter.EquipInfo()).ChargeLevel, 0xFF));
            // The same world the projectile was stamped with as it spawned.
            event.AckFrame = NetUnlagged::LaunchFrameFor(shooter);
            event.RoleFrame = FiringVariant::RoleFrame();
            event.Origin = shot.Origin;
            event.Direction = shot.Direction;
            const FiringVariant firing = shot.WeaponVariantIndex != IntentPacket::NoVariant
                ? FiringVariant{shot.WeaponVariantIndex, shot.PrimeHunter} : FiringVariant::For(shooter);
            event.WeaponVariantIndex = firing.Index;
            if (firing.PrimeHunter)
            {
                event.ShotFlags |= IntentPacket::ShotFlagPrimeHunter;
            }
            LocalShotLog* log = LogFor(shooter);
            static_cast<void>(log->Record(event));
            if (shooter.SlotIndex() >= 0 && shooter.SlotIndex() < Slots)
            {
                _activeShots[static_cast<std::size_t>(shooter.SlotIndex())] = *log->Latest();
            }
        }
        else if (Drives(shooter))
        {
            const std::size_t slot = static_cast<std::size_t>(shooter.SlotIndex());
            const std::optional<IntentPacket::ShotEvent> event = FiringEvent(shooter);
            if (event.has_value())
            {
                _activeShots[slot] = *event;
                _remote[slot].Consume();
            }
        }
    }

    void NetShotEvents::FinishShot(const Entities::PlayerEntity& shooter) noexcept
    {
        const std::int32_t slot = shooter.SlotIndex();
        if (slot >= 0 && slot < Slots)
        {
            _activeShots[static_cast<std::size_t>(slot)].reset();
        }
    }

    std::optional<IntentPacket::ShotEvent> NetShotEvents::CurrentShotEvent(
        const Entities::PlayerEntity& shooter) noexcept
    {
        const std::int32_t slot = shooter.SlotIndex();
        if (slot < 0 || slot >= Slots)
        {
            return std::nullopt;
        }
        return _activeShots[static_cast<std::size_t>(slot)];
    }

    void NetShotEvents::RecordHomingTarget(const Entities::PlayerEntity& shooter,
        const IntentPacket::ShotTargetReference& target) noexcept
    {
        LocalShotLog* log = LogFor(shooter);
        const std::optional<IntentPacket::ShotEvent> active = CurrentShotEvent(shooter);
        if (log == nullptr || !active.has_value())
        {
            return;
        }
        log->SetHomingTarget(active->Sequence, target);
        IntentPacket::ShotEvent updated = *active;
        updated.Target = target;
        updated.ShotFlags |= IntentPacket::ShotFlagTargetResolved;
        _activeShots[static_cast<std::size_t>(shooter.SlotIndex())] = updated;
    }

    void NetShotEvents::RecordContinuousTarget(std::int32_t slot,
        const IntentPacket::ShotTargetReference& target) noexcept
    {
        if (slot >= 0 && slot < Slots)
        {
            _continuousTargets[static_cast<std::size_t>(slot)] = target.IsValid()
                ? target : IntentPacket::ShotTargetReference{};
        }
    }

    void NetShotEvents::AttachContinuousTarget(const Entities::PlayerEntity& player,
        IntentPacket& intent) noexcept
    {
        intent.HasContinuousFire = false;
        intent.ContinuousPrimeHunter = false;
        intent.ContinuousRoleFrame = 0;
        intent.HasContinuousTarget = false;
        intent.ContinuousTarget = {};
        const std::int32_t slot = player.SlotIndex();
        if (slot < 0 || slot >= Slots)
        {
            return;
        }
        const std::size_t s = static_cast<std::size_t>(slot);
        const ContinuousFire fire = std::exchange(_continuousFire[s], ContinuousFire{});
        if (!Continuous(player))
        {
            _continuousTargets[s] = {};
            return;
        }
        // Only what the owner fired this frame: a held trigger that did
        // not fire names no lock and no role.
        intent.HasContinuousFire = fire.Fired;
        intent.ContinuousPrimeHunter = fire.PrimeHunter;
        intent.ContinuousRoleFrame = fire.RoleFrame;
        const IntentPacket::ShotTargetReference& target = _continuousTargets[s];
        intent.HasContinuousTarget = fire.Fired && target.IsValid()
            && target.Kind != IntentPacket::ShotTargetKind::None;
        if (intent.HasContinuousTarget)
        {
            intent.ContinuousTarget = target;
        }
    }

    std::uint32_t NetShotEvents::SequenceOfShotFired(const Entities::PlayerEntity& shooter) noexcept
    {
        if (const auto active = CurrentShotEvent(shooter); active.has_value())
        {
            return active->Sequence;
        }
        const LocalShotLog* log = LogFor(shooter);
        const IntentPacket::ShotEvent* latest = log != nullptr ? log->Latest() : nullptr;
        return latest != nullptr && latest->Frame == std::max(1U, NetSession::NetFrame()) ? latest->Sequence : 0U;
    }

    void NetShotEvents::PrepareShot(Entities::PlayerEntity& shooter)
    {
        const std::optional<IntentPacket::ShotEvent> event = FiringEvent(shooter);
        if (!event.has_value())
        {
            return;
        }
        const auto weapon = static_cast<::MphRead::BeamType>(event->WeaponId);
        if (weapon != shooter.CurrentWeapon())
        {
            shooter.ModSetWeapon(weapon);
        }
        const auto currentWeapons = ::MphRead::Weapons::Current;
        if (event->WeaponVariantIndex < currentWeapons->size())
        {
            NativeRuntime::RequireReference(shooter.EquipInfo()).Weapon
                = NativeRuntime::ManagedAt(currentWeapons, static_cast<std::int32_t>(event->WeaponVariantIndex));
        }
        // After the switch, which resets it: the charge is the shot's.
        NativeRuntime::RequireReference(shooter.EquipInfo()).ChargeLevel = event->Charge;
    }

    std::optional<IntentPacket::ShotEvent> NetShotEvents::FiringEvent(const Entities::PlayerEntity& shooter) noexcept
    {
        if (!Drives(shooter))
        {
            return std::nullopt;
        }
        const std::size_t slot = static_cast<std::size_t>(shooter.SlotIndex());
        RemoteShotQueue& queue = _remote[slot];
        const std::uint16_t generation = NetPlayerLifecycle::Generation(shooter.SlotIndex());
        for (std::size_t i = 0; i < RemoteShotQueue::Capacity; ++i)
        {
            const std::optional<IntentPacket::ShotEvent> event = queue.Next();
            if (!event.has_value())
            {
                return std::nullopt;
            }
            const ShotEventEligibility eligibility = _eligibility[slot].EvaluateFrame(
                generation, event->Frame);
            if (eligibility == ShotEventEligibility::Allowed)
            {
                return event;
            }
            if (eligibility == ShotEventEligibility::Pending)
            {
                return std::nullopt;
            }
            queue.RejectIneligible();
        }
        return std::nullopt;
    }

    std::optional<IntentPacket::ShotEvent> NetShotEvents::FiringRay(const Entities::PlayerEntity& shooter) noexcept
    {
        std::optional<IntentPacket::ShotEvent> event = FiringEvent(shooter);
        if (!event.has_value() || !event->HasRay()
            || !ShotRayValidation::IsPlausibleOrigin(
                event->Origin, static_cast<OpenTK::Mathematics::Vector3>(shooter.Position), MaxRayOffset))
        {
            return std::nullopt;
        }
        return event;
    }

    bool NetShotEvents::MayFire(const Entities::PlayerEntity& shooter) noexcept
    {
        if (!Drives(shooter))
        {
            return true;
        }
        const std::int32_t slot = shooter.SlotIndex();
        const std::size_t index = static_cast<std::size_t>(slot);
        if (Continuous(shooter))
        {
            return shooter.ModIsInPlay() && _eligibility[index].LatestAllowsContinuousFire(
                NetPlayerLifecycle::Generation(slot), NetPlayerLifecycle::Get(slot));
        }
        return FiringEvent(shooter).has_value();
    }

    std::int32_t NetShotEvents::FireReady(Entities::PlayerEntity& shooter)
    {
        // A beam weapon is fired standing: an event met in the ball waits for
        // the copy to stand, or goes stale.
        if (!Drives(shooter) || shooter.IsAltForm() || shooter.IsMorphing())
        {
            return 0;
        }
        std::int32_t fired = 0;
        for (std::size_t i = 0; i < RemoteShotQueue::Capacity && FiringEvent(shooter).has_value(); ++i)
        {
            if (!shooter.ModFireShotEvent())
            {
                break;
            }
            ++fired;
        }
        return fired;
    }

    void NetShotEvents::Attach(IntentPacket& intent, std::int32_t slot) noexcept
    {
        _local.Fill(intent, std::max(1U, NetSession::NetFrame()));
        if (slot >= 0 && slot < Slots && static_cast<std::size_t>(slot) < Entities::PlayerEntity::Players().size())
        {
            const Entities::PlayerEntity& player
                = NativeRuntime::RequireReference(NativeRuntime::ManagedAt(Entities::PlayerEntity::Players(), slot));
            AttachContinuousTarget(player, intent);
        }
    }

    void NetShotEvents::AttachFor(std::int32_t slot, IntentPacket& intent) noexcept
    {
        if (slot >= 0 && slot < Slots)
        {
            _bots[static_cast<std::size_t>(slot)].Fill(intent, std::max(1U, NetSession::NetFrame()));
            if (static_cast<std::size_t>(slot) < Entities::PlayerEntity::Players().size())
            {
                const Entities::PlayerEntity& player
                    = NativeRuntime::RequireReference(NativeRuntime::ManagedAt(Entities::PlayerEntity::Players(), slot));
                AttachContinuousTarget(player, intent);
            }
        }
    }

    void NetShotEvents::Receive(std::int32_t slot, const IntentPacket& intent) noexcept
    {
        if (slot < 0 || slot >= Slots)
        {
            return;
        }
        const auto s = static_cast<std::size_t>(slot);
        if (intent.ShotHistoryLength > 0)
        {
            _lastDeparted[s] = {};
        }
        _remote[s].Receive(intent);
        for (std::size_t i = 0; i < intent.ShotHistoryLength && i < intent.ShotHistory.size(); ++i)
        {
            const IntentPacket::ShotEvent& event = intent.ShotHistory[i];
            if (event.IsValid() && IsValidShotEventFrame(event.Frame, intent.Frame))
            {
                _ledger[s].Note(event);
            }
        }
    }

    void NetShotEvents::RecordAcceptedIntent(std::int32_t slot, std::uint16_t generation,
        std::uint16_t lifeId, std::uint32_t frame, bool inPlay) noexcept
    {
        if (slot < 0 || slot >= Slots)
        {
            return;
        }
        _eligibility[static_cast<std::size_t>(slot)].Record(
            generation, lifeId, frame, inPlay);
    }

    std::optional<IntentPacket::ShotEvent> NetShotEvents::Find(std::int32_t slot, std::uint32_t sequence) noexcept
    {
        if (slot < 0 || slot >= Slots)
        {
            return std::nullopt;
        }
        return _ledger[static_cast<std::size_t>(slot)].Find(sequence);
    }

    ShotEventEligibility NetShotEvents::EligibilityFor(std::int32_t slot,
        std::uint32_t sequence, std::uint16_t generation, std::uint16_t lifeId) noexcept
    {
        if (slot < 0 || slot >= Slots)
        {
            return ShotEventEligibility::Pending;
        }
        const std::size_t index = static_cast<std::size_t>(slot);
        const std::optional<IntentPacket::ShotEvent> event = _ledger[index].Find(sequence);
        if (!event.has_value())
        {
            return ShotEventEligibility::Pending;
        }
        return _eligibility[index].Evaluate(generation, lifeId, event->Frame);
    }

    ShotQueueStats NetShotEvents::Stats(std::int32_t slot) noexcept
    {
        if (slot < 0 || slot >= Slots)
        {
            return {};
        }
        const auto s = static_cast<std::size_t>(slot);
        const ShotQueueStats current = _remote[s].Stats();
        return current.Received == 0 && current.Gaps == 0 ? _lastDeparted[s] : current;
    }

    std::optional<std::string> NetShotEvents::Describe()
    {
        ShotQueueStats total = _departed;
        std::uint64_t staleRefused = 0;
        std::uint64_t conflicts = 0;
        for (std::size_t s = 0; s < _remote.size(); ++s)
        {
            total += _remote[s].Stats();
            staleRefused += _ledger[s].StaleRefused();
            conflicts += _ledger[s].Conflicts();
        }
        if (total.Received == 0 && total.Gaps == 0)
        {
            return std::nullopt;
        }
        return "shot events (received): " + std::to_string(total.Received) + " received -- "
            + std::to_string(total.Fired) + " fired (" + std::to_string(total.OutOfOrder) + " after a newer one), "
            + std::to_string(total.Stale) + " stale, "
            + std::to_string(total.Overflow) + " pushed out, "
            + std::to_string(total.Abandoned) + " abandoned with a life, "
            + std::to_string(total.Ineligible) + " ineligible, "
            + std::to_string(total.Waiting) + " waiting (" + std::to_string(total.Overdue) + " overdue); "
            + std::to_string(total.Gaps) + " skipped by a newer one -- "
            + std::to_string(total.Recovered) + " recovered late, "
            + std::to_string(total.Lost) + " never arrived, "
            + std::to_string(total.Pending) + " still awaited; ledger: "
            + std::to_string(staleRefused) + " older shots refused, "
            + std::to_string(conflicts) + " contradicting a kept shot";
    }

    void NetShotEvents::BeginLife(std::int32_t slot) noexcept
    {
        if (slot < 0 || slot >= Slots)
        {
            return;
        }
        // This machine's own log is kept: a shot fired just before the death
        // still has to reach everyone, and Fill already stops repeating it
        // once it is too old to be fired. Only a new occupant (Forget) clears it.
        _remote[static_cast<std::size_t>(slot)].BeginLife();
        _continuousTargets[static_cast<std::size_t>(slot)] = {};
    }

    void NetShotEvents::Forget(std::int32_t slot) noexcept
    {
        if (slot < 0 || slot >= Slots)
        {
            return;
        }
        const ShotQueueStats departed = _remote[static_cast<std::size_t>(slot)].Retire();
        if (departed.Received != 0 || departed.Gaps != 0)
        {
            _departed += departed;
            _lastDeparted[static_cast<std::size_t>(slot)] = departed;
        }
        _ledger[static_cast<std::size_t>(slot)].Reset();
        _eligibility[static_cast<std::size_t>(slot)].Reset();
        _bots[static_cast<std::size_t>(slot)].Reset();
        if (slot == NetHooks::LocalSlot())
        {
            _local.Reset();
        }
        _activeShots[static_cast<std::size_t>(slot)].reset();
        _continuousTargets[static_cast<std::size_t>(slot)] = {};
        _continuousFire[static_cast<std::size_t>(slot)] = {};
    }
}
