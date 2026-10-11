#include "NetPlayerLifecycle.hpp"
#include "NetBombs.hpp"
#include "NetShotEvents.hpp"

#include "NetDamage.hpp"
#include "NetHitClaims.hpp"
#include "NetHitPrediction.hpp"
#include "NetLog.hpp"
#include "NetPlayerBridge.hpp"
#include "NetProtocol.hpp"
#include "NetRoomChange.hpp"
#include "NetScoreboard.hpp"
#include "NetSession.hpp"
#include "NetShotDiagnostics.hpp"
#include "NetSlotManager.hpp"
#include "NetSmoothing.hpp"
#include "NetTimingDiagnostics.hpp"
#include "NetUnlagged.hpp"

#include "../../Entities/BeamProjectileEntity.hpp"
#include "../../Entities/Players/HalfturretEntity.hpp"
#include "../../Entities/Players/PlayerEntity.hpp"
#include "../../NativeRuntime/System/Managed.hpp"

#include <cmath>
#include <memory>

namespace MphRead::Mods::Network
{
    namespace Runtime = ::MphRead::NativeRuntime;

    namespace
    {
        using ::MphRead::Entities::BeamProjectileEntity;
        using ::MphRead::Entities::HalfturretEntity;
        using ::MphRead::Entities::PlayerEntity;

        // beam.Owner as PlayerEntity ?? (beam.Owner as HalfturretEntity)?.Owner.
        [[nodiscard]] PlayerEntity* OwnerPlayer(const BeamProjectileEntity& beam)
        {
            const std::shared_ptr<::MphRead::Entities::EntityBase> owner = beam.Owner();
            if (auto* player = dynamic_cast<PlayerEntity*>(owner.get()))
            {
                return player;
            }
            if (auto* halfturret = dynamic_cast<HalfturretEntity*>(owner.get()))
            {
                return halfturret->Owner().get();
            }
            return nullptr;
        }

        [[nodiscard]] bool Sane(const OpenTK::Mathematics::Vector3& value) noexcept
        {
            return std::isfinite(value.X) && std::isfinite(value.Y) && std::isfinite(value.Z)
                && std::abs(value.X) < 100000 && std::abs(value.Y) < 100000 && std::abs(value.Z) < 100000;
        }
    }

    std::int64_t NetPlayerLifecycle::StaleLifeStates = 0;
    std::int64_t NetPlayerLifecycle::WrongGeneration = 0;
    std::int64_t NetPlayerLifecycle::InvalidResurrections = 0;
    std::int64_t NetPlayerLifecycle::Transitions = 0;
    std::int64_t NetPlayerLifecycle::Spawns = 0;
    std::int64_t NetPlayerLifecycle::Deaths = 0;
    std::int64_t NetPlayerLifecycle::OldLifeIntents = 0;
    std::int64_t NetPlayerLifecycle::OldLifeClaims = 0;
    std::int64_t NetPlayerLifecycle::OldLifeDamage = 0;
    std::int64_t NetPlayerLifecycle::CrossMatch = 0;
    std::int64_t NetPlayerLifecycle::CrossAuthority = 0;
    std::array<NetLifecycleTracker, 8> NetPlayerLifecycle::_slots{};
    bool NetPlayerLifecycle::_applyingSpawn = false;

    bool NetPlayerLifecycle::CanSpawn()
    {
        return !NetSession::Active() || (NetRoomChange::GameplayReady()
            && (NetSession::IsHost() || NetSession::IsAuthority() || _applyingSpawn));
    }

    std::uint16_t NetPlayerLifecycle::Get(std::int32_t slot) noexcept
    {
        return slot >= 0 && slot < static_cast<std::int32_t>(_slots.size())
            ? _slots[static_cast<std::size_t>(slot)].LifeId() : static_cast<std::uint16_t>(0);
    }

    std::uint16_t NetPlayerLifecycle::Generation(std::int32_t slot) noexcept
    {
        return slot >= 0 && slot < static_cast<std::int32_t>(_slots.size())
            ? _slots[static_cast<std::size_t>(slot)].Generation() : static_cast<std::uint16_t>(0);
    }

    bool NetPlayerLifecycle::Matches(std::int32_t slot, std::uint16_t generation, std::uint16_t life) noexcept
    {
        return generation != 0 && Generation(slot) == generation && Get(slot) == life;
    }

    void NetPlayerLifecycle::StampProjectile(BeamProjectileEntity& beam, BeamProjectileEntity* parent)
    {
        PlayerEntity* owner = OwnerPlayer(beam);
        beam.ModNearestLocal = -1.0F;
        beam.ModTouchedLocal = false;
        beam.ModConfirmedTarget = false;
        beam.ModPassedTarget = false;
        beam.ModHeldUntil = 0;
        beam.ModTargetSlot = -1;
        beam.ModTouchedTarget = false;
        if (parent != nullptr && NetSession::Active() && CurrentProjectile(*parent)
            && owner != nullptr && owner->SlotIndex() == parent->ModLaunchKey().ShooterSlot)
        {
            // A ricochet is the original shot, even if its shooter respawned.
            beam.ModLaunchKey(parent->ModLaunchKey());
            beam.ModLaunchMatch = parent->ModLaunchMatch;
            beam.ModLaunchAuthority = parent->ModLaunchAuthority;
            beam.ModLaunchGeneration = parent->ModLaunchGeneration;
            beam.ModLaunchLife = parent->ModLaunchLife;
            beam.ModLaunchFrame = parent->ModLaunchFrame;
            beam.ModShooterAck = parent->ModShooterAck;
            beam.ModShotSequence = parent->ModShotSequence;
            return;
        }
        beam.ModLaunchMatch = NetSession::CurrentMatchId();
        beam.ModLaunchAuthority = NetSession::AuthorityEpoch();
        beam.ModLaunchGeneration = owner == nullptr ? static_cast<std::uint16_t>(0) : Generation(owner->SlotIndex());
        beam.ModLaunchLife = owner == nullptr ? static_cast<std::uint16_t>(0) : Get(owner->SlotIndex());
        beam.ModLaunchFrame = owner == nullptr ? 0U : NetUnlagged::LaunchFrameFor(*owner);
        // A turret's shot is not its owner's shot event.
        beam.ModShotSequence = owner != nullptr && beam.Owner().get() == owner
            ? NetShotEvents::SequenceOfShotFired(*owner) : 0U;
        beam.ModShooterAck = 0;
        if (owner != nullptr && NetSession::Active() && !NetSession::IsAuthority() && !NetSession::IsHost()
            && owner->SlotIndex() != NetSession::LocalSlot() && !owner->IsBot() && owner->SlotIndex() >= 0
            && static_cast<std::size_t>(owner->SlotIndex()) < NetSession::RemoteIntents.size()
            && NetSession::RemoteIntentValid[static_cast<std::size_t>(owner->SlotIndex())])
        {
            beam.ModShooterAck = NetSession::RemoteIntents[static_cast<std::size_t>(owner->SlotIndex())].AckFrame;
            NetPlayerBridge::OnRemoteShotSpawned(beam);
        }
        beam.ModLaunchKey(ShotKey(beam.ModLaunchAuthority, beam.ModLaunchMatch,
            owner != nullptr ? owner->SlotIndex() : -1, beam.ModLaunchGeneration, beam.ModLaunchLife,
            beam.ModLaunchFrame));
    }

    bool NetPlayerLifecycle::CurrentProjectile(const BeamProjectileEntity& beam)
    {
        PlayerEntity* owner = OwnerPlayer(beam);
        return owner == nullptr || (beam.ModLaunchMatch == NetSession::CurrentMatchId()
            && beam.ModLaunchAuthority == NetSession::AuthorityEpoch()
            && beam.ModLaunchLife != 0
            && Generation(owner->SlotIndex()) == beam.ModLaunchGeneration
            && beam.ModLaunchKey() == ShotKey(beam.ModLaunchAuthority, beam.ModLaunchMatch,
                owner->SlotIndex(), beam.ModLaunchGeneration, beam.ModLaunchLife, beam.ModLaunchFrame));
    }

    void NetPlayerLifecycle::SetOccupant(std::int32_t slot, std::uint16_t generation)
    {
        if (slot < 0 || slot >= static_cast<std::int32_t>(_slots.size()) || Generation(slot) == generation)
        {
            return;
        }
        NetSlotManager::ReleaseSlot(slot);
        OnSlotChanged(slot);
        _slots[static_cast<std::size_t>(slot)].SetOccupant(generation);
    }

    void NetPlayerLifecycle::OnSlotChanged(std::int32_t slot)
    {
        NetPlayerBridge::ForgetSlot(slot);
        NetShotEvents::Forget(slot);
        NetBombs::Forget(slot);
        NetDamage::ForgetSlot(slot);
        NetHitPrediction::ForgetSlot(slot);
        NetHitClaims::ForgetSlot(slot);
        NetSmoothing::ResetSlot(slot);
        NetUnlagged::ResetSlot(slot);
        NetSession::ForgetSlot(slot);
        NetScoreboard::ForgetSlot(slot);
        NetTimingDiagnostics::ForgetSlot(slot);
    }

    void NetPlayerLifecycle::OnSpawn(PlayerEntity& player)
    {
        if (!NetSession::Active() || _applyingSpawn || (!NetSession::IsHost() && !NetSession::IsAuthority()))
        {
            return;
        }
        const std::int32_t slot = player.SlotIndex();
        if (Generation(slot) == 0)
        {
            SetOccupant(slot, 1);
        }
        (void)_slots[static_cast<std::size_t>(slot)].BeginLife();
        NetPlayerBridge::ForgetSlot(slot);
        NetDamage::ForgetSlot(slot);
        NetHitPrediction::NoteRespawn(slot);
        NetHitClaims::ForgetSlot(slot, true);
        NetSmoothing::ResetSlot(slot);
        NetUnlagged::ResetSlot(slot);
        NetSession::ForgetSlot(slot);
        player.ModResetNetworkHistory();
        player.Controls().ClearAll();
        Runtime::IncrementInPlace(Spawns);
        Runtime::IncrementInPlace(Transitions);
        Log(slot, "SPAWN", 0, player.Health(), true);
    }

    NetworkPlayerState NetPlayerLifecycle::StateOf(const PlayerState& state) noexcept
    {
        return (state.Flags & PlayerState::FlagSpectating) != 0 ? NetworkPlayerState::Spectating
            : state.LifeId == 0 ? NetworkPlayerState::WaitingToSpawn
            : state.Health == 0 ? NetworkPlayerState::Dead : NetworkPlayerState::Alive;
    }

    bool NetPlayerLifecycle::AcceptState(const PlayerState& state, std::uint32_t frame)
    {
        const std::int32_t slot = state.SlotIndex;
        if (slot >= static_cast<std::int32_t>(_slots.size()) || !Sane(state.Position) || !Sane(state.Speed)
            || !Sane(state.Facing))
        {
            return false;
        }
        const NetworkPlayerState next = StateOf(state);
        if (state.Health > 0 && ((state.Flags & PlayerState::FlagSpawned) == 0 || state.LifeId == 0))
        {
            return false;
        }
        NetLifecycleTracker& tracker = _slots[static_cast<std::size_t>(slot)];
        const NetworkPlayerState before = tracker.State();
        bool fresh = false;
        const LifecycleRejection rejection = tracker.Accept(state.SlotGeneration, state.LifeId, next, fresh);
        if (rejection != LifecycleRejection::None)
        {
            if (rejection == LifecycleRejection::WrongGeneration)
            {
                Runtime::IncrementInPlace(WrongGeneration);
            }
            else if (rejection == LifecycleRejection::InvalidResurrection)
            {
                Runtime::IncrementInPlace(InvalidResurrections);
            }
            else
            {
                Runtime::IncrementInPlace(StaleLifeStates);
            }
            Log(slot, "DROP " + ToString(rejection), frame, state.Health,
                (state.Flags & PlayerState::FlagSpawned) != 0);
            return false;
        }
        if (fresh)
        {
            NetSmoothing::ResetSlot(slot);
        }
        if (fresh || before != next)
        {
            Runtime::IncrementInPlace(Transitions);
            if (fresh && next == NetworkPlayerState::Alive)
            {
                Runtime::IncrementInPlace(Spawns);
            }
            if (next == NetworkPlayerState::Dead)
            {
                Runtime::IncrementInPlace(Deaths);
            }
            Log(slot, fresh ? std::string("SPAWN") : ToString(next), frame, state.Health,
                (state.Flags & PlayerState::FlagSpawned) != 0);
        }
        return true;
    }

    bool NetPlayerLifecycle::AcceptIntent(std::int32_t slot, const IntentPacket& intent)
    {
        if (!NetSession::MatchesStream(intent.MatchId, intent.AuthorityEpoch))
        {
            return false;
        }
        if (intent.SlotGeneration != Generation(slot))
        {
            Runtime::IncrementInPlace(WrongGeneration);
            return false;
        }
        if (intent.LifeId == 0 || !Matches(slot, intent.SlotGeneration, intent.LifeId))
        {
            Runtime::IncrementInPlace(OldLifeIntents);
            return false;
        }
        return true;
    }

    void NetPlayerLifecycle::ResetLives()
    {
        for (std::int32_t i = 0; i < static_cast<std::int32_t>(_slots.size()); i++)
        {
            OnSlotChanged(i);
            _slots[static_cast<std::size_t>(i)].ResetLife();
        }
    }

    void NetPlayerLifecycle::Reset()
    {
        for (NetLifecycleTracker& slot : _slots)
        {
            slot.SetOccupant(0);
        }
        _applyingSpawn = false;
        StaleLifeStates = WrongGeneration = InvalidResurrections = Transitions = Spawns = Deaths = 0;
        OldLifeIntents = OldLifeClaims = OldLifeDamage = CrossMatch = CrossAuthority = 0;
    }

    void NetPlayerLifecycle::Log(std::int32_t slot, const std::string& action, std::uint32_t frame,
        std::int32_t health, bool spawned)
    {
        if (NetLog::Enabled())
        {
            NetLog::Event("[life] slot=" + std::to_string(slot) + " generation="
                + std::to_string(Generation(slot)) + " life=" + std::to_string(Get(slot)) + " " + action
                + " frame=" + std::to_string(NetSession::NetFrame()) + " authorityFrame=" + std::to_string(frame)
                + " health=" + std::to_string(health) + " spawned=" + (spawned ? "True" : "False") + " "
                + NetHitPrediction::LifecycleDetails(slot));
        }
    }

    std::string NetPlayerLifecycle::Describe()
    {
        return "life: transitions=" + std::to_string(Transitions) + " spawns=" + std::to_string(Spawns)
            + " deaths=" + std::to_string(Deaths) + " invalid resurrection=" + std::to_string(InvalidResurrections)
            + " stale life=" + std::to_string(StaleLifeStates) + " wrong generation=" + std::to_string(WrongGeneration)
            + " cross match=" + std::to_string(CrossMatch) + " cross authority=" + std::to_string(CrossAuthority)
            + " old intents=" + std::to_string(OldLifeIntents) + " old claims=" + std::to_string(OldLifeClaims)
            + " old damage=" + std::to_string(OldLifeDamage);
    }
}
