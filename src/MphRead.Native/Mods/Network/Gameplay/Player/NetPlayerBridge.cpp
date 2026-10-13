#include "Mods/Network/Gameplay/Player/NetPlayerBridge.hpp"
#include "Mods/Network/Gameplay/Player/PlayerFormReplication.hpp"
#include "Mods/Network/Gameplay/Player/NetInputHistory.hpp"
#include "Mods/Network/Gameplay/Player/NetInputPressReplay.hpp"
#include "Mods/Network/Gameplay/Player/NetPlayerIntent.hpp"
#include "Mods/Network/Gameplay/Pickups/NetPowerUps.hpp"
#include "Mods/Network/Gameplay/StatusEffects/StatusEffectSynchronization.hpp"

#include "GameState.hpp"
#include "Mods/EndScreen.hpp"
#include "Mods/Network/Gameplay/Combat/NetDamage.hpp"
#include "Mods/Network/Gameplay/Claims/Hit/NetHitClaims.hpp"
#include "Mods/Network/Gameplay/Combat/NetHitPrediction.hpp"
#include "Mods/Network/Integration/Hooks/NetHooks.hpp"
#include "Mods/Network/Diagnostics/NetLog.hpp"
#include "Mods/Network/Gameplay/Player/NetPlayerLifecycle.hpp"
#include "Mods/Network/Gameplay/Weapons/NetShotEvents.hpp"
#include "Mods/Network/Session/NetRoomChange.hpp"
#include "Mods/Network/Session/NetSession.hpp"
#include "Mods/Network/Diagnostics/NetShotDiagnostics.hpp"
#include "Mods/Network/Gameplay/Replication/NetSmoothing.hpp"
#include "Mods/Network/Gameplay/Combat/NetUnlagged.hpp"
#include "Mods/Network/Gameplay/Combat/HitLocation.hpp"
#include "NativeRuntime/System/Managed.hpp"
#include "NativeRuntime/System/Globalization.hpp"
#include "Formats/Types.hpp"
#include "Metadata/Weapons.hpp"
#include "Entities/BeamProjectileEntity.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>

using ::MphRead::NativeRuntime::HasFlag;
using ::OpenTK::Mathematics::Length;
using ::OpenTK::Mathematics::LengthSquared;

namespace MphRead::Mods::Network
{
    void NetPlayerBridge::RecordUnresolvedNodeLookup() noexcept
    {
        ::MphRead::NativeRuntime::IncrementInPlace(NodeLookupsUnresolved);
    }

    std::int32_t NetPlayerBridge::ShootPressAgeFor(std::int32_t slot) noexcept
    {
        return slot >= 0 && slot < static_cast<std::int32_t>(ShootPressAge.size())
            ? ShootPressAge[static_cast<std::size_t>(slot)] : 0;
    }

    std::uint32_t NetPlayerBridge::SpawnFrameFor(std::int32_t slot) noexcept
    {
        return slot >= 0 && slot < static_cast<std::int32_t>(SpawnFrame.size())
            ? SpawnFrame[static_cast<std::size_t>(slot)] : 0;
    }

    namespace
    {
        [[nodiscard]] bool VectorEquals(OpenTK::Mathematics::Vector3 left, OpenTK::Mathematics::Vector3 right) noexcept
        {
            return left.X == right.X && left.Y == right.Y && left.Z == right.Z;
        }

        [[nodiscard]] constexpr std::size_t Index(std::int32_t value) noexcept
        {
            return static_cast<std::size_t>(value);
        }

    }

    std::string NetPlayerBridge::FormSaidByAuthority()
    {
        return PlayerFormReplication::FormSaidByAuthority();
    }

    OpenTK::Mathematics::Vector3 NetPlayerBridge::InFormFor(
        Entities::PlayerEntity& player, OpenTK::Mathematics::Vector3 position, bool measuredInAlt)
    {
        return InForm(player, position, measuredInAlt);
    }

    OpenTK::Mathematics::Vector3 NetPlayerBridge::InForm(
        Entities::PlayerEntity& player, OpenTK::Mathematics::Vector3 position, bool measuredInAlt)
    {
        if (measuredInAlt == player.IsAltForm())
        {
            return position;
        }
        const std::int32_t hunter = static_cast<std::int32_t>(player.Hunter());
        if (hunter < 0 || hunter >= 8)
        {
            return position;
        }
        const auto& volumes = Entities::PlayerEntity::PlayerVolumes[Index(hunter)];
        const OpenTK::Mathematics::Vector3 delta = volumes[0].SpherePosition - volumes[2].SpherePosition;
        return measuredInAlt ? position - delta : position + delta;
    }

    bool NetPlayerBridge::Sane(OpenTK::Mathematics::Vector3 value) noexcept
    {
        return std::isfinite(value.X) && std::isfinite(value.Y) && std::isfinite(value.Z)
            && std::fabs(value.X) < PositionLimit && std::fabs(value.Y) < PositionLimit
            && std::fabs(value.Z) < PositionLimit;
    }

    void NetPlayerBridge::RecordPresses(Entities::PlayerEntity& player)
    {
        NetInputHistory::Record(player);
        if (!player.ModIsInPlay())
        {
            if (player.SlotIndex() == NetSession::LocalSlot())
            {
                _hasLatch = false;
            }
            return;
        }
        if (player.SlotIndex() != NetSession::LocalSlot())
        {
            return;
        }
        Entities::PlayerControls& c = player.Controls();
        if (c.Shoot().IsReleased() || c.Boost().IsReleased() || c.AltAttack().IsPressed())
        {
            _latchedCharge = player.ModChargeLevel();
            _latchedBoostDamage = player.ModBoostDamage();
            _hasLatch = true;
        }
    }

    IntentPacket NetPlayerBridge::StateIntent(Entities::PlayerEntity& player)
    {
        return NetPlayerIntent::CaptureState(player);
    }

    IntentPacket NetPlayerBridge::CaptureIntent(Entities::PlayerEntity& player)
    {
        Entities::PlayerControls& c = player.Controls();
        IntentPacket intent = StateIntent(player);
        if (Mods::EndScreen::Ready())
        {
            intent.Buttons |= IntentButtons::ReadyState;
        }
        intent.Frame = std::max(1U, NetSession::NetFrame());
        NetInputHistory::Attach(intent, player.SlotIndex());
        if (_hasLatch)
        {
            intent.ChargeLevel = static_cast<std::uint8_t>(std::clamp(_latchedCharge, 0, 255));
            intent.BoostDamage = static_cast<std::uint8_t>(std::clamp(_latchedBoostDamage, 0, 255));
        }
        intent.AckFrame = NetHooks::SnapshotOwnsPuppets() && NetSession::AppliedSnapshotFrame() != 0
            ? NetSession::AppliedSnapshotFrame()
            : NetSession::LastSnapshotFrame();
        std::uint32_t readFrame = 0;
        std::uint8_t readSub = 0;
        if (NetSmoothing::AckPoint(readFrame, readSub))
        {
            intent.AckFrame = readFrame;
            intent.AckSubFrame = readSub;
        }
        _hasLatch = false;
        if (NetLog::Enabled() && (HasFlag(intent.Buttons, IntentButtons::Shoot) || c.Shoot().IsReleased()))
        {
            NetShotDiagnostics::Trace("input", ShotKey::For(player.SlotIndex(), intent.AckFrame), player.CurrentWeapon(),
                "intentFrame=" + std::to_string(intent.Frame) + " intentLife=" + std::to_string(intent.LifeId)
                + " inPlay=" + (HasFlag(intent.Buttons, IntentButtons::InPlayState) ? "True" : "False")
                + " shoot=" + (c.Shoot().IsDown() ? "True" : "False")
                + " press=" + (c.Shoot().IsPressed() ? "True" : "False"));
        }
        return intent;
    }

    bool NetPlayerBridge::RespawnRequested(std::int32_t slot)
    {
        return NetSession::Active() && slot != NetSession::LocalSlot()
            && slot >= 0 && slot < static_cast<std::int32_t>(_respawnRequested.size()) && _respawnRequested[Index(slot)];
    }

    void NetPlayerBridge::ApplyIntent(Entities::PlayerEntity& player, const IntentPacket& intent)
    {
        if (intent.LifeId == 0 || !NetPlayerLifecycle::Matches(player.SlotIndex(), intent.SlotGeneration, intent.LifeId))
        {
            return;
        }
        if (!Sane(intent.Aim))
        {
            NativeRuntime::IncrementInPlace(_rejectedUpdates);
            NetLog::Event("slot " + std::to_string(player.SlotIndex()) + " intent rejected: aim=" + intent.Aim.ToString());
            return;
        }
        Entities::PlayerControls& c = player.Controls();
        const std::int32_t aimSlot = player.SlotIndex();
        if (aimSlot >= 0 && aimSlot < static_cast<std::int32_t>(_aimHeld.size()) && _aimHeld[Index(aimSlot)]
            && (intent.AckFrame >= SpawnFrame[Index(aimSlot)]
                || NetSession::NetFrame() - SpawnFrame[Index(aimSlot)] > AimHoldCeiling))
        {
            _aimHeld[Index(aimSlot)] = false;
        }
        const NetInputPressReplayResult pressReplay
            = NetInputPressReplay::Consume(player.SlotIndex(), intent);
        const IntentButtons missed = pressReplay.Pressed;
        if (player.SlotIndex() >= 0 && player.SlotIndex() < static_cast<std::int32_t>(ShootPressAge.size()))
        {
            ShootPressAge[Index(player.SlotIndex())] = pressReplay.ShootAge;
        }
        if (player.IsBot() && HasFlag(missed, IntentButtons::AltAttack) && NetLog::Enabled())
        {
            NetLog::Event("[bot-input] replayed alt-attack slot=" + std::to_string(player.SlotIndex())
                + " frame=" + std::to_string(intent.Frame)
                + " form=" + player.ModFormState());
        }
        NativeRuntime::ManagedAt(_respawnRequested, player.SlotIndex()) = !HasFlag(intent.Buttons, IntentButtons::InPlayState)
            && HasFlag(intent.Buttons, IntentButtons::Shoot);
        if (!HasFlag(intent.Buttons, IntentButtons::InPlayState))
        {
            c.ClearAll();
            NativeRuntime::ManagedAt(ShootPressAge, player.SlotIndex()) = 0;
            player.ModSetSpectating(HasFlag(intent.Buttons, IntentButtons::SpectatingState));
            return;
        }
        Set(c.MoveLeft(), HasFlag(intent.Buttons, IntentButtons::MoveLeft), HasFlag(missed, IntentButtons::MoveLeft));
        Set(c.MoveRight(), HasFlag(intent.Buttons, IntentButtons::MoveRight), HasFlag(missed, IntentButtons::MoveRight));
        Set(c.MoveUp(), HasFlag(intent.Buttons, IntentButtons::MoveUp), HasFlag(missed, IntentButtons::MoveUp));
        Set(c.MoveDown(), HasFlag(intent.Buttons, IntentButtons::MoveDown), HasFlag(missed, IntentButtons::MoveDown));
        Set(c.Shoot(), HasFlag(intent.Buttons, IntentButtons::Shoot), HasFlag(missed, IntentButtons::Shoot));
        Set(c.Zoom(), HasFlag(intent.Buttons, IntentButtons::Zoom), HasFlag(missed, IntentButtons::Zoom));
        Set(c.Jump(), HasFlag(intent.Buttons, IntentButtons::Jump), HasFlag(missed, IntentButtons::Jump));
        // A smoothed remote replica must not start its form transition from
        // the relayed input edge: that edge arrives ahead of the delayed
        // presentation sample and would let the clip finish before the shared
        // playout clock reaches it. PlayerFormReplication applies the sampled
        // transition below; authority and -nointerp keep the input path.
        const bool presentationOwnsForm = NetSmoothing::PresentationOwnsRemoteReplicas();
        Set(c.Morph(), !presentationOwnsForm && HasFlag(intent.Buttons, IntentButtons::Morph),
            !presentationOwnsForm && HasFlag(missed, IntentButtons::Morph));
        if (presentationOwnsForm && HasFlag(missed, IntentButtons::Morph) && NetLog::Enabled())
        {
            NetLog::Event("[form-presentation] ignored relayed morph edge until playout slot="
                + std::to_string(player.SlotIndex()) + " frame=" + std::to_string(intent.Frame));
        }
        else if (c.Morph().IsPressed())
        {
            NetLog::Event("slot " + std::to_string(player.SlotIndex()) + " morph press received, now " + player.ModFormState());
        }
        Set(c.Boost(), HasFlag(intent.Buttons, IntentButtons::Boost), HasFlag(missed, IntentButtons::Boost));
        Set(c.AltAttack(), HasFlag(intent.Buttons, IntentButtons::AltAttack), HasFlag(missed, IntentButtons::AltAttack));
        if (NetLog::Enabled() && HasFlag(missed, IntentButtons::AltAttack))
        {
            ReplicatedFormState presentationForm{};
            const bool presentationOwnsForm = NetSmoothing::PresentationOwnsRemoteReplicas();
            const bool hasPresentationForm = presentationOwnsForm
                && NetSmoothing::SampleForm(player.SlotIndex(), presentationForm);
            const std::uint32_t attackFrame = intent.Frame - static_cast<std::uint32_t>(pressReplay.AltAttackAge);
            NetLog::Event("[altattack-replay] slot=" + std::to_string(player.SlotIndex())
                + " life=" + std::to_string(intent.LifeId)
                + " eventFrame=" + std::to_string(attackFrame)
                + " intentFrame=" + std::to_string(intent.Frame)
                + " localFrame=" + std::to_string(NetSession::NetFrame())
                + " age=" + std::to_string(pressReplay.AltAttackAge)
                + " form=" + player.ModFormState()
                + " presentation=" + (hasPresentationForm
                    ? std::to_string(static_cast<std::int32_t>(presentationForm.Direction))
                        + "/" + std::to_string(presentationForm.Progress)
                        + "/alt=" + (presentationForm.AltForm ? std::string("true") : std::string("false"))
                    : presentationOwnsForm ? std::string("warming") : std::string("immediate"))
                + " pressed=" + (c.AltAttack().IsPressed() ? std::string("true") : std::string("false"))
                + " held=" + (c.AltAttack().IsDown() ? std::string("true") : std::string("false")));
        }
        Set(c.ScanVisor(), HasFlag(intent.Buttons, IntentButtons::ScanVisor), HasFlag(missed, IntentButtons::ScanVisor));
        Set(c.NextWeapon(), HasFlag(intent.Buttons, IntentButtons::NextWeapon), HasFlag(missed, IntentButtons::NextWeapon));
        Set(c.PrevWeapon(), HasFlag(intent.Buttons, IntentButtons::PrevWeapon), HasFlag(missed, IntentButtons::PrevWeapon));
        Set(c.RolltLeft(), HasFlag(intent.Buttons, IntentButtons::RollLeft), HasFlag(missed, IntentButtons::RollLeft));
        Set(c.RollRight(), HasFlag(intent.Buttons, IntentButtons::RollRight), HasFlag(missed, IntentButtons::RollRight));
        Set(c.RollUp(), HasFlag(intent.Buttons, IntentButtons::RollUp), HasFlag(missed, IntentButtons::RollUp));
        Set(c.RollDown(), HasFlag(intent.Buttons, IntentButtons::RollDown), HasFlag(missed, IntentButtons::RollDown));
        if (intent.WeaponSelect != 0xFF)
        {
            player.ModSetWeapon(static_cast<BeamType>(intent.WeaponSelect));
        }
        player.ModSetAmmo(intent.AmmoUa, intent.AmmoMissiles);
        if ((intent.Buttons & PressedButtons) != IntentButtons::None)
        {
            player.ModNoteInput();
        }
        player.ModSetZoom(HasFlag(intent.Buttons, IntentButtons::ZoomedState));
        player.ModSetSpectating(HasFlag(intent.Buttons, IntentButtons::SpectatingState));
        if (NetSession::IsAuthority())
        {
            PlayerFormReplication::Apply(player, HasFlag(intent.Buttons, IntentButtons::AltFormState),
                NetSession::NetFrame(), SessionSlots().PingAt(player.SlotIndex()));
        }
        // Every intent, with or without a touch block: one without (a demo
        // recorded before) is no contact, never the last contact repeated.
        player.ModSetReportedTouch(intent.TouchReport(), intent.SlotGeneration, intent.LifeId);
        if (intent.HasState && (NetSession::IsAuthority() || NetSession::IsHost()))
        {
            player.ModSetShotState(intent.ChargeLevel, intent.BoostDamage);
        }
    }

    void NetPlayerBridge::NoteSpawn(std::int32_t slot)
    {
        if (slot < 0 || slot >= NetInputHistory::Slots)
        {
            return;
        }
        const auto s = Index(slot);
        NetInputPressReplay::ForgetSlot(slot);
        ShootPressAge[s] = 0;
        SpawnFrame[s] = NetSession::NetFrame();
        _aimHeld[s] = true;
    }

    bool NetPlayerBridge::AimAvailable(std::int32_t slot)
    {
        if (slot < 0 || slot >= static_cast<std::int32_t>(NetSession::RemotePlayerSlots)
            || !NetSession::HasRemoteIntent(slot) || !AimTrusted(slot)
            || NetSession::RemoteIntentAge(slot) > NetHooks::StaleIntentFrames)
            return false;
        const auto& intent = NetSession::RemoteIntent(static_cast<std::size_t>(slot));
        return intent.MatchId != 0 && intent.AuthorityEpoch != 0
            && intent.MatchId == NetSession::CurrentMatchId() && intent.AuthorityEpoch == NetSession::AuthorityEpoch()
            && intent.LifeId != 0 && NetPlayerLifecycle::Matches(slot, intent.SlotGeneration, intent.LifeId)
            && HasFlag(intent.Buttons, IntentButtons::InPlayState) && Sane(intent.Aim);
    }

    bool NetPlayerBridge::AimTrusted(std::int32_t slot)
    {
        return slot < 0 || slot >= static_cast<std::int32_t>(_aimHeld.size()) || !_aimHeld[Index(slot)];
    }

    void NetPlayerBridge::Set(Entities::Keybind& bind, bool down, bool pressed)
    {
        const bool wasDown = bind.IsDown();
        bind.SetIsDown(down || pressed);
        bind.SetIsPressed(pressed);
        bind.SetIsReleased(!down && wasDown && !pressed);
    }

    void NetPlayerBridge::BeginRemoteLife(Entities::PlayerEntity& player, const PlayerState& state)
    {
        const std::int32_t slot = player.SlotIndex();
        ForgetSlot(slot);
        NativeRuntime::ManagedAt(_appliedLifeId, slot) = state.LifeId;
        _lifeApplied[Index(slot)] = true;
        NetHitPrediction::NoteRespawn(slot);
        NetDamage::BeginLife(slot, state);
        NetHitClaims::ForgetSlot(slot);
        NetUnlagged::ResetSlot(slot);
        player.ModResetNetworkHistory();
        player.Controls().ClearAll();
        player.ModSetFrozen(false);
        player.ModSetBurning(false);
        player.ModSetDisrupted(false);
        if (state.LifeId != 0)
        {
            NetPlayerLifecycle::ApplyingSpawn(true);
            try
            {
                player.ModNetSpawn(state.Position, state.Facing);
            }
            catch (...)
            {
                NetPlayerLifecycle::ApplyingSpawn(false);
                throw;
            }
            NetPlayerLifecycle::ApplyingSpawn(false);
            // Resolve the final authority pose directly. Spawn applies a
            // temporary vertical lift; walking from that pose can cross into
            // the wrong Part where map levels overlap.
            Move(player, state.Position, true);
            player.ModSetSpawnFacing(state.Facing);
            if (state.Health == 0)
            {
                player.ModNetDie();
            }
        }
        player.SetHealth(state.Health);
    }

    void NetPlayerBridge::ApplyState(Entities::PlayerEntity& player, const PlayerState& state, bool isLocal)
    {
        const std::int32_t slot = player.SlotIndex();
        if (slot != state.SlotIndex || !NetPlayerLifecycle::Matches(slot, state.SlotGeneration, state.LifeId))
        {
            return;
        }
        if (!Sane(state.Position) || !Sane(state.Speed) || !Sane(state.Facing)
            || ((state.WeavelFlags & PlayerState::WeavelFlagMorphing) != 0
                && (state.WeavelFlags & PlayerState::WeavelFlagUnmorphing) != 0)
            || (((state.WeavelFlags & (PlayerState::WeavelFlagMorphing | PlayerState::WeavelFlagUnmorphing)) == 0)
                && state.FormTransitionProgress() != 0)
            || (player.Hunter() != Hunter::Weavel
                && (state.WeavelFlags & (PlayerState::WeavelFlagTurretActive | PlayerState::WeavelFlagTurretGrounded)) != 0)
            || (player.Hunter() == Hunter::Weavel
                && ((state.WeavelFlags & ~PlayerState::WeavelFlagsKnown) != 0
                    || state.FormTransitionProgress() != 0
                    || ((state.WeavelFlags & PlayerState::WeavelFlagTurretActive) != 0
                        && (!Sane(state.HalfturretPosition) || state.HalfturretHealth == 0)))))
        {
            NativeRuntime::IncrementInPlace(_rejectedUpdates);
            return;
        }
        const auto s = Index(slot);
        const bool fresh = !_lifeApplied[s] || _appliedLifeId[s] != state.LifeId;
        if (fresh)
        {
            BeginRemoteLife(player, state);
        }
        const bool spawned = (state.Flags & PlayerState::FlagSpawned) != 0 && state.Health > 0;
        PlayerFormReplication::RecordAuthorityForm(slot, (state.Flags & PlayerState::FlagAltForm) != 0);
        if (!NetRoomChange::Settling())
        {
            // A kill shown here ahead of the authority stays on the board until
            // the authority's own count catches up, instead of flickering off.
            const bool shownDead = !isLocal && NetHitPrediction::ShowingKill(slot);
            const std::int32_t credit = isLocal ? NetHitPrediction::KillsShown() : 0;
            const bool battle = GameState::Mode() == GameMode::Battle || GameState::Mode() == GameMode::BattleTeams;
            GameState::Points()[slot] = state.Points + (battle ? credit : 0);
            GameState::Kills()[slot] = state.Kills + credit;
            GameState::Deaths()[slot] = state.Deaths + (shownDead ? 1 : 0);
        }
        NetDamage::Replay(player, state);
        if (!spawned)
        {
            if (state.Health == 0 && player.Health() > 0)
            {
                player.ModNetDie();
            }
            player.SetHealth(state.Health);
            if (state.Health == 0)
            {
                NetHitPrediction::NoteDeath(slot);
            }
            player.ModSetSpectating((state.Flags & PlayerState::FlagSpectating) != 0);
            return;
        }
        if (!fresh && player.Health() <= 0)
        {
            // Dead here, alive on the authority in the same life: a kill this
            // machine showed is still on its way, or it is never coming. Only
            // the second is worth undoing, and only once it is certain.
            if (isLocal || NetHitPrediction::ShowingKill(slot))
            {
                return;
            }
            NativeRuntime::IncrementInPlace(_killsResynced);
            BeginRemoteLife(player, state);
        }
        // Remote replicas read form animation from the same playout clock as
        // their pose and tracked visuals. The local owner stays on its
        // prediction clock and must not be delayed by remote presentation.
        ReplicatedFormState displayedForm{};
        bool hasDisplayedForm = false;
        if (!isLocal)
        {
            OpenTK::Mathematics::Vector3 displayedPosition = state.Position;
            bool displayedAltForm = (state.Flags & PlayerState::FlagAltForm) != 0;
            OpenTK::Mathematics::Vector3 sampledPosition{};
            bool sampledAltForm = false;
            if (NetSmoothing::Sample(slot, sampledPosition, sampledAltForm))
            {
                displayedPosition = sampledPosition;
                displayedAltForm = sampledAltForm;
            }
            hasDisplayedForm = NetSmoothing::SampleForm(slot, displayedForm);
            if (hasDisplayedForm)
            {
                PlayerFormReplication::ApplyPresentation(player, displayedForm,
                    NetSession::NetFrame(), SessionSlots().PingAt(slot));
            }
            else if (player.Hunter() != Hunter::Weavel)
            {
                PlayerFormReplication::Apply(player, state,
                    NetSession::NetFrame(), SessionSlots().PingAt(slot));
            }
            // Form presentation must be applied before converting the sampled
            // pose into the entity's current form coordinates. At a stable
            // morph/unmorph commit, this keeps the pose and model on the same
            // read frame instead of shifting the player by one form's origin.
            Move(player, InForm(player, displayedPosition, displayedAltForm), true);
            player.SetSpeed(state.Speed);
            player.SetHealth(NetHitPrediction::HealthFor(slot, state.Health));
            player.ModSetFacing(state.Facing);
            player.ModSetWeapon(static_cast<BeamType>(state.CurrentWeapon));
            player.ModSetZoom((state.Flags & PlayerState::FlagZoomed) != 0);
            player.ModSetSpectating((state.Flags & PlayerState::FlagSpectating) != 0);
        }
        else
        {
            if (!fresh && NetRoomChange::GameplayReady() && Diverged(player, state, slot))
            {
                Move(player, state.Position);
                player.SetSpeed(state.Speed);
                _divergedFrames[s] = 0;
            }
            player.SetHealth(NetHitPrediction::LocalHealthFor(player, state.Health));
            // The owner predicts form changes for responsiveness, while the
            // authority remains responsible for the form used by server-side
            // weapons and collision. Reconcile the latest owner state without
            // seeking the locally predicted clip to the delayed snapshot.
            if (!NetSession::IsAuthority())
            {
                PlayerFormReplication::ApplyOwnerPrediction(player, state,
                    NetSession::AppliedSnapshotFrame(), SessionSlots().PingAt(slot));
            }
            // Where the authority had this player, by authority frame: what
            // every other player is drawing of us, a round trip late.
            const std::uint32_t frame = NetSession::AppliedSnapshotFrame();
            if (frame != 0)
            {
                _localFrames[frame % LocalHistory] = frame;
                _localPositions[frame % LocalHistory] = state.Position;
            }
        }
        // Explicit turret reconciliation owns remote replicas. A local owner
        // predicts form and turret placement, and takes the authority's word
        // on the turret's health and destruction (WeavelOwnedTurret).
        if (player.Hunter() == Hunter::Weavel && !isLocal)
        {
            const bool headingAlt = hasDisplayedForm ? displayedForm.HeadingAlt() : state.HeadingAlt();
            player.ModApplyWeavelState(headingAlt,
                (state.WeavelFlags & PlayerState::WeavelFlagTurretActive) != 0, state.HalfturretHealth,
                state.HalfturretPosition, (state.WeavelFlags & PlayerState::WeavelFlagTurretGrounded) != 0);
            if (hasDisplayedForm && displayedForm.Direction != ReplicatedFormState::Transition::None)
            {
                const bool morphing = displayedForm.Direction == ReplicatedFormState::Transition::Morph;
                player.ModHoldFormTransitionAtProgress(morphing, displayedForm.Progress);
            }
            else
            {
                // A stable sample releases the final phase; a missing sample
                // falls back to the latest replica state and native playback.
                player.ModReleaseFormTransitionHold();
            }
        }
        else if (player.Hunter() == Hunter::Weavel && !NetSession::IsAuthority())
        {
            player.ModApplyOwnWeavelTurret(state.HeadingAlt(),
                (state.WeavelFlags & PlayerState::WeavelFlagTurretActive) != 0, state.HalfturretHealth);
        }
        // Common timer lifecycle with freeze, burn and disrupt policies.
        StatusEffectSynchronization::Apply(player, state, NetSession::SnapshotAge());
        NetPowerUps::Apply(player, state, isLocal);
    }

    void NetPlayerBridge::SteerIncoming(std::int32_t attackerSlot)
    {
        using OpenTK::Mathematics::Vector3;
        const std::int32_t local = NetHooks::LocalSlot();
        const auto& players = Entities::PlayerEntity::Players();
        if (!_retargetEnabled || local < 0 || attackerSlot < 0 || attackerSlot == local
            || static_cast<std::size_t>(attackerSlot) >= players.size() || static_cast<std::size_t>(local) >= players.size())
        {
            return;
        }
        Entities::PlayerEntity& shooter = *players[static_cast<std::size_t>(attackerSlot)];
        const Entities::PlayerEntity& me = *players[static_cast<std::size_t>(local)];
        if (shooter.EquipInfo() == nullptr || shooter.EquipInfo()->Beams == nullptr)
        {
            return;
        }
        const Vector3 chest = static_cast<Vector3>(me.Position) + Vector3(0.0F, 0.3F, 0.0F);
        auto& beams = *shooter.EquipInfo()->Beams;
        Entities::BeamProjectileEntity* best = nullptr;
        float bestDistance = 40.0F * 40.0F;
        for (std::int32_t i = 0; i < beams.Length(); ++i)
        {
            Entities::BeamProjectileEntity* beam = beams[i].get();
            if (beam == nullptr || beam->Lifespan() <= 0
                || ::MphRead::TestFlag(beam->Flags(), Entities::BeamFlags::Collided)
                || ::MphRead::TestFlag(beam->Flags(), Entities::BeamFlags::Continuous))
            {
                continue;
            }
            const Vector3 toMe = chest - static_cast<Vector3>(beam->Position);
            const float distance = toMe.LengthSquared();
            if (distance < bestDistance && Vector3::Dot(toMe, beam->Velocity()) > 0.0F)
            {
                bestDistance = distance;
                best = beam;
            }
        }
        if (best == nullptr)
        {
            return;
        }
        const float speed = OpenTK::Mathematics::Length(best->Velocity());
        const Vector3 toMe = chest - static_cast<Vector3>(best->Position);
        const float length = OpenTK::Mathematics::Length(toMe);
        if (!(speed > 0.0001F) || !(length > 0.0001F))
        {
            return;
        }
        best->SetVelocity(OpenTK::Mathematics::Scale(toMe, speed / length));
        NativeRuntime::IncrementInPlace(_shotsSteered);
    }

    namespace
    {
        Entities::PlayerEntity* LocalPlayer() noexcept
        {
            const std::int32_t local = NetHooks::LocalSlot();
            const auto& players = Entities::PlayerEntity::Players();
            if (local < 0 || static_cast<std::size_t>(local) >= players.size())
            {
                return nullptr;
            }
            return players[static_cast<std::size_t>(local)].get();
        }


    }

    void NetPlayerBridge::ShooterRay(const Entities::PlayerEntity& shooter, OpenTK::Mathematics::Vector3 drawnMuzzle,
        OpenTK::Mathematics::Vector3& from, OpenTK::Mathematics::Vector3& direction, std::uint32_t& ackFrame)
    {
        const std::int32_t slot = shooter.SlotIndex();
        from = drawnMuzzle;
        direction = OpenTK::Mathematics::Vector3::Zero;
        ackFrame = 0;
        if (slot < 0 || static_cast<std::size_t>(slot) >= NetSession::RemotePlayerSlots
            || !NetSession::HasRemoteIntent(slot))
        {
            return;
        }
        // The shot event being fired: its own ray and the world it was aimed
        // in, however late it arrived.
        if (const auto event = NetShotEvents::FiringRay(shooter); event.has_value())
        {
            from = event->Origin;
            direction = event->Direction;
            ackFrame = event->AckFrame;
            return;
        }
        const IntentPacket& intent = NetSession::RemoteIntent(static_cast<std::size_t>(slot));
        ackFrame = intent.AckFrame;
        if (intent.HasShot)
        {
            from = intent.ShotOrigin;
            direction = intent.ShotDirection;
            return;
        }
        direction = intent.Aim;
        if (Sane(intent.Position) && LengthSquared(intent.Position) > 0.0001F)
        {
            from = intent.Position + (drawnMuzzle - static_cast<OpenTK::Mathematics::Vector3>(shooter.Position));
        }
    }

    void NetPlayerBridge::NoteLocalShot(OpenTK::Mathematics::Vector3 origin, OpenTK::Mathematics::Vector3 direction) noexcept
    {
        _localShotFrame = std::max(1U, NetSession::NetFrame());
        _localShotOrigin = origin;
        _localShotDirection = direction;
    }

    void NetPlayerBridge::AttachLocalShot(IntentPacket& intent) noexcept
    {
        if (_localShotFrame == 0 || _localShotFrame != std::max(1U, NetSession::NetFrame()))
        {
            return;
        }
        intent.HasShot = true;
        intent.ShotOrigin = _localShotOrigin;
        intent.ShotDirection = _localShotDirection;
        _localShotFrame = 0;
    }

    OpenTK::Mathematics::Vector3 NetPlayerBridge::RetargetAtLocal(const Entities::PlayerEntity& shooter,
        OpenTK::Mathematics::Vector3 origin, OpenTK::Mathematics::Vector3 aim, std::uint32_t ackFrame,
        OpenTK::Mathematics::Vector3 aimedFrom)
    {
        using OpenTK::Mathematics::Vector3;
        const std::int32_t local = NetHooks::LocalSlot();
        if (!_retargetEnabled || !NetSession::Active() || NetSession::IsHost() || NetSession::IsAuthority() || ackFrame == 0
            || local < 0 || local >= static_cast<std::int32_t>(Entities::PlayerEntity::Players().size())
            || shooter.SlotIndex() == local)
        {
            return aim;
        }
        const Entities::PlayerEntity& me = *Entities::PlayerEntity::Players()[static_cast<std::size_t>(local)];
        const float length = OpenTK::Mathematics::Length(aim);
        if (me.Health() <= 0 || !(length > 0.0001F))
        {
            return aim;
        }
        // The newest recorded frame at or before the one the shooter drew.
        Vector3 past{};
        bool found = false;
        for (std::uint32_t back = 0; back < 8 && back < ackFrame; back++)
        {
            const std::uint32_t frame = ackFrame - back;
            if (_localFrames[frame % LocalHistory] == frame)
            {
                past = _localPositions[frame % LocalHistory];
                found = true;
                break;
            }
        }
        if (!found)
        {
            return aim;
        }
        const Vector3 direction = OpenTK::Mathematics::Scale(aim, 1.0F / length);
        // The aim was taken from where the shooter really stood, which on a
        // pad is several units from the puppet drawn here: test the ray from
        // there, then draw the turned shot from the gun this player can see.
        if (!Sane(aimedFrom) || !(LengthSquared(aimedFrom) > 0.0001F))
        {
            aimedFrom = origin;
        }
        const Vector3 toPast = past - aimedFrom;
        const float along = Vector3::Dot(toPast, direction);
        if (along <= 0)
        {
            return aim;
        }
        // A weapon that falls (the Battlehammer) is aimed above its target,
        // so its ray never crosses the old body: it was aimed at this player
        // when it left within a cone of them, and it is turned by the same
        // rotation that carries the old line of sight onto the new one, which
        // keeps the arc's own elevation.
        const auto beam = static_cast<std::size_t>(shooter.CurrentWeapon());
        const bool arcs = beam < (*::MphRead::Weapons::Current).size()
            && NativeRuntime::RequireReference((*::MphRead::Weapons::Current)[beam]).UnchargedGravity != 0;
        // Where on that old body the aim crossed it: the same capsule the
        // beam test uses, -0.5 to +1.1 above Position and half a unit wide,
        // with a little slack for the puppet's own interpolation.
        const Vector3 offset = aimedFrom + OpenTK::Mathematics::Scale(direction, along) - past;
        const bool crossed = offset.X * offset.X + offset.Z * offset.Z <= 0.9F * 0.9F
            && offset.Y >= -0.6F && offset.Y <= 1.2F;
        Vector3 turned{};
        if (crossed && !arcs)
        {
            const Vector3 at = me.Position;
            turned = at + offset - origin;
        }
        else if (arcs)
        {
            const Vector3 chest(0.0F, 0.3F, 0.0F);
            const Vector3 was = (past + chest - aimedFrom).Normalized();
            const Vector3 now = (static_cast<Vector3>(me.Position) + chest - origin).Normalized();
            if (Vector3::Dot(was, direction) < std::cos(OpenTK::Mathematics::MathHelper::DegToRad * 20.0F))
            {
                return aim;
            }
            // Rodrigues: rotate the fired direction by the rotation was -> now.
            const Vector3 axis = Vector3::Cross(was, now);
            const float sine = OpenTK::Mathematics::Length(axis);
            const float cosine = Vector3::Dot(was, now);
            if (sine < 1e-5F)
            {
                turned = direction;
            }
            else
            {
                const Vector3 k = OpenTK::Mathematics::Scale(axis, 1.0F / sine);
                turned = OpenTK::Mathematics::Scale(direction, cosine)
                    + OpenTK::Mathematics::Scale(Vector3::Cross(k, direction), sine)
                    + OpenTK::Mathematics::Scale(k, Vector3::Dot(k, direction) * (1.0F - cosine));
            }
        }
        else
        {
            return aim;
        }
        const float turnedLength = OpenTK::Mathematics::Length(turned);
        if (!(turnedLength > 0.0001F))
        {
            return aim;
        }
        if (_lastRetargetFrame != NetSession::NetFrame())
        {
            _lastRetargetFrame = NetSession::NetFrame();
            NativeRuntime::IncrementInPlace(_aimsRetargeted);
        }
        return OpenTK::Mathematics::Scale(turned, length / turnedLength);
    }

    FormCorrection NetPlayerBridge::ReconcileForm(std::int32_t slot, std::uint32_t frame, bool desiredAlt,
        bool actualAlt, bool morphing, bool unmorphing, std::int32_t ping)
    {
        return PlayerFormReplication::Reconcile(slot, frame, desiredAlt, actualAlt, morphing, unmorphing, ping);
    }

    bool NetPlayerBridge::Diverged(Entities::PlayerEntity& player, const PlayerState& state, std::int32_t slot)
    {
        if (slot < 0 || slot >= static_cast<std::int32_t>(_divergedFrames.size()))
        {
            return false;
        }
        OpenTK::Mathematics::Vector3 then = player.Position;
        const std::int32_t lagFrames = slot < static_cast<std::int32_t>(SessionRoster::SlotCapacity)
            ? std::clamp(SessionSlots().PingAt(slot) * 60 / 1000, 0, 100)
            : 0;
        OpenTK::Mathematics::Vector3 past{};
        if (lagFrames > 0 && NetSession::NetFrame() > static_cast<std::uint32_t>(lagFrames)
            && player.ModGetNetworkPosition(NetSession::NetFrame() - static_cast<std::uint32_t>(lagFrames), past))
        {
            then = past;
        }
        if (LengthSquared(state.Position - then) <= DesyncDistance * DesyncDistance)
        {
            _divergedFrames[Index(slot)] = 0;
            return false;
        }
        _divergedFrames[Index(slot)]++;
        return _divergedFrames[Index(slot)] >= DivergedFramesBeforeCorrecting;
    }

    void NetPlayerBridge::NoteRoomChanged()
    {
        PlayerFormReplication::NoteRoomChanged();
        _lifeApplied.fill(false);
        _reportSeen.fill(false);
        _divergedFrames.fill(0);
    }

    void NetPlayerBridge::Reset()
    {
        PlayerFormReplication::Reset();
        _appliedLifeId.fill(0);
        _lifeApplied.fill(false);
        _snaps = 0;
        _worstSnap = 0.0F;
        NodeLookupsUnresolved = 0;
        PlacementsRefused = 0;
        SpawnFacingsTurned = 0;
        WorstSpawnFacing = 0.0F;
        StaleDeathsIgnored = 0;
        NetInputPressReplay::Reset();
        _aimHeld.fill(false);
        SpawnFrame.fill(0);
        ShootPressAge.fill(0);
        NetInputHistory::Reset();
        _hasLatch = false;
        _divergedFrames.fill(0);
        _lastReportPosition.fill(OpenTK::Mathematics::Vector3::Zero);
        _lastReportFrame.fill(0);
        _reportSeen.fill(false);
    }

    void NetPlayerBridge::ForgetSlot(std::int32_t slot)
    {
        if (slot < 0 || slot >= Entities::PlayerEntity::SlotCapacity)
        {
            return;
        }
        const auto s = Index(slot);
        NetInputHistory::ForgetSlot(slot);
        NetInputPressReplay::ForgetSlot(slot);
        PlayerFormReplication::ForgetSlot(slot);
        _lifeApplied[s] = false;
        _appliedLifeId[s] = 0;
        _aimHeld[s] = false;
        NetShotEvents::BeginLife(slot);
        SpawnFrame[s] = 0;
        ShootPressAge[s] = 0;
        _respawnRequested[s] = false;
        if (slot == NetSession::LocalSlot())
        {
            _hasLatch = false;
            _latchedCharge = _latchedBoostDamage = 0;
        }
        _divergedFrames[s] = 0;
        _lastReportPosition[s] = OpenTK::Mathematics::Vector3::Zero;
        _lastReportFrame[s] = 0;
        _reportSeen[s] = false;
    }

    void NetPlayerBridge::ApplyReportedPosition(Entities::PlayerEntity& player, const IntentPacket& intent)
    {
        if (!Sane(intent.Position))
        {
            NativeRuntime::IncrementInPlace(_rejectedUpdates);
            return;
        }
        if (FrozenInPlace(player))
        {
            return;
        }
        if (VectorEquals(intent.Position, OpenTK::Mathematics::Vector3::Zero))
        {
            return; // the owner has not spawned yet
        }
        if (StaleSinceSpawn(player, intent))
        {
            return;
        }
        const OpenTK::Mathematics::Vector3 reported = InForm(player, intent.Position,
            HasFlag(intent.Buttons, IntentButtons::AltFormState));
        NoteReportedVelocity(player, reported, intent.Frame);
        const OpenTK::Mathematics::Vector3 delta = reported - static_cast<OpenTK::Mathematics::Vector3>(player.Position);
        const float distance = Length(delta);
        if (distance > SnapDistance)
        {
            NativeRuntime::IncrementInPlace(_snaps);
            _worstSnap = std::max(_worstSnap, distance);
            Move(player, reported);
            return;
        }
        Move(player, reported);
    }

    void NetPlayerBridge::RestoreSnapshotPosition(Entities::PlayerEntity& player, const PlayerState& state)
    {
        if (FrozenInPlace(player))
        {
            return;
        }
        // This path restores the authority's simulation position before local
        // collision and movement processing. The rendered replica is sampled
        // from NetSmoothing later, when ApplyState applies the snapshot.
        if (!Sane(state.Position) || VectorEquals(state.Position, OpenTK::Mathematics::Vector3::Zero))
        {
            return;
        }
        Move(player, InForm(player, state.Position, (state.Flags & PlayerState::FlagAltForm) != 0));
    }

    void NetPlayerBridge::RestoreReportedPosition(Entities::PlayerEntity& player, const IntentPacket& intent)
    {
        if (!Sane(intent.Position) || VectorEquals(intent.Position, OpenTK::Mathematics::Vector3::Zero)
            || StaleSinceSpawn(player, intent) || FrozenInPlace(player))
        {
            return;
        }
        Move(player, InForm(player, intent.Position, HasFlag(intent.Buttons, IntentButtons::AltFormState)));
    }

    bool NetPlayerBridge::StaleSinceSpawn(Entities::PlayerEntity& player, const IntentPacket& intent)
    {
        return !NetPlayerLifecycle::Matches(player.SlotIndex(), intent.SlotGeneration, intent.LifeId)
            || !HasFlag(intent.Buttons, IntentButtons::InPlayState);
    }

    void NetPlayerBridge::NoteReportedVelocity(Entities::PlayerEntity& player,
        OpenTK::Mathematics::Vector3 reported, std::uint32_t frame)
    {
        const std::int32_t slot = player.SlotIndex();
        if (slot < 0 || slot >= static_cast<std::int32_t>(_lastReportFrame.size()))
        {
            return;
        }
        const auto s = Index(slot);
        if (_reportSeen[s] && frame > _lastReportFrame[s])
        {
            const std::uint32_t elapsed = std::min(frame - _lastReportFrame[s], 8U);
            const OpenTK::Mathematics::Vector3 travelled = reported - _lastReportPosition[s];
            const float step = Length(travelled);
            if (!Sane(travelled) || step > SnapDistance)
            {
                player.SetSpeed(OpenTK::Mathematics::Vector3::Zero);
            }
            else
            {
                OpenTK::Mathematics::Vector3 speed = OpenTK::Mathematics::Divide(travelled, static_cast<float>(elapsed));
                const float magnitude = Length(speed);
                if (magnitude > MaxReportedSpeed)
                {
                    speed = OpenTK::Mathematics::Multiply(speed, MaxReportedSpeed / magnitude);
                }
                player.SetSpeed(speed);
            }
        }
        if (!_reportSeen[s] || frame > _lastReportFrame[s])
        {
            _reportSeen[s] = true;
            _lastReportFrame[s] = frame;
            _lastReportPosition[s] = reported;
        }
    }

    bool NetPlayerBridge::FrozenInPlace(Entities::PlayerEntity& player)
    {
        return player.ModFrozen();
    }

    void NetPlayerBridge::Move(Entities::PlayerEntity& player, OpenTK::Mathematics::Vector3 position,
        bool resolveNodeFromPosition)
    {
        const OpenTK::Mathematics::Vector3 previous = player.Position;
        player.Position = position;
        player.SetPrevPosition(position);
        if (resolveNodeFromPosition)
        {
            player.ModResolveNetworkNodeRef();
        }
        else
        {
            player.ModRefreshNodeRef(previous);
        }
        player.ModRefreshVolume();
        player.ModRefreshAttachedEffects();
    }
}
