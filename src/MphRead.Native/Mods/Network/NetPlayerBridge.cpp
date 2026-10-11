#include "NetPlayerBridge.hpp"

#include "../../GameState.hpp"
#include "../EndScreen.hpp"
#include "NetDamage.hpp"
#include "NetHitClaims.hpp"
#include "NetHitPrediction.hpp"
#include "NetHooks.hpp"
#include "NetLog.hpp"
#include "NetPlayerLifecycle.hpp"
#include "NetShotEvents.hpp"
#include "NetRoomChange.hpp"
#include "NetSession.hpp"
#include "NetShotDiagnostics.hpp"
#include "NetSmoothing.hpp"
#include "NetUnlagged.hpp"
#include "HitLocation.hpp"
#include "../../NativeRuntime/System/Managed.hpp"
#include "../../NativeRuntime/System/Globalization.hpp"
#include "../../Formats/Types.hpp"
#include "../../Metadata/Weapons.hpp"
#include "../../Entities/BeamProjectileEntity.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

using ::MphRead::NativeRuntime::HasFlag;
using ::OpenTK::Mathematics::Length;
using ::OpenTK::Mathematics::LengthSquared;

namespace MphRead::Mods::Network
{
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
        std::string text;
        text.reserve(Entities::PlayerEntity::SlotCapacity);
        for (std::int32_t i = 0; i < Entities::PlayerEntity::MaxPlayers() && i < static_cast<std::int32_t>(_formSaid.size()); ++i)
        {
            text.push_back(_formSaid[Index(i)] == 0 ? '-' : _formSaid[Index(i)] == 2 ? 'A' : 'b');
        }
        return text;
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
        if (!player.ModIsInPlay())
        {
            _pressHistory.fill(0);
            _hasLatch = false;
            return;
        }
        Entities::PlayerControls& c = player.Controls();
        IntentButtons pressed = IntentButtons::None;
        if (c.MoveLeft().IsPressed()) pressed |= IntentButtons::MoveLeft;
        if (c.MoveRight().IsPressed()) pressed |= IntentButtons::MoveRight;
        if (c.MoveUp().IsPressed()) pressed |= IntentButtons::MoveUp;
        if (c.MoveDown().IsPressed()) pressed |= IntentButtons::MoveDown;
        if (c.Shoot().IsPressed()) pressed |= IntentButtons::Shoot;
        if (c.Zoom().IsPressed()) pressed |= IntentButtons::Zoom;
        if (c.Jump().IsPressed()) pressed |= IntentButtons::Jump;
        if (c.Morph().IsPressed()) pressed |= IntentButtons::Morph;
        if (c.Boost().IsPressed()) pressed |= IntentButtons::Boost;
        if (c.AltAttack().IsPressed()) pressed |= IntentButtons::AltAttack;
        if (c.ScanVisor().IsPressed()) pressed |= IntentButtons::ScanVisor;
        if (c.NextWeapon().IsPressed()) pressed |= IntentButtons::NextWeapon;
        if (c.PrevWeapon().IsPressed()) pressed |= IntentButtons::PrevWeapon;
        if (c.RolltLeft().IsPressed()) pressed |= IntentButtons::RollLeft;
        if (c.RollRight().IsPressed()) pressed |= IntentButtons::RollRight;
        if (c.RollUp().IsPressed()) pressed |= IntentButtons::RollUp;
        if (c.RollDown().IsPressed()) pressed |= IntentButtons::RollDown;
        for (std::size_t i = _pressHistory.size() - 1; i > 0; i--)
        {
            _pressHistory[i] = _pressHistory[i - 1];
        }
        _pressHistory[0] = static_cast<std::uint32_t>(pressed);
        if (c.Shoot().IsReleased() || c.Boost().IsReleased() || c.AltAttack().IsPressed())
        {
            _latchedCharge = player.ModChargeLevel();
            _latchedBoostDamage = player.ModBoostDamage();
            _hasLatch = true;
        }
    }

    IntentPacket NetPlayerBridge::CaptureIntent(Entities::PlayerEntity& player)
    {
        Entities::PlayerControls& c = player.Controls();
        IntentButtons buttons = IntentButtons::None;
        if (c.MoveLeft().IsDown()) buttons |= IntentButtons::MoveLeft;
        if (c.MoveRight().IsDown()) buttons |= IntentButtons::MoveRight;
        if (c.MoveUp().IsDown()) buttons |= IntentButtons::MoveUp;
        if (c.MoveDown().IsDown()) buttons |= IntentButtons::MoveDown;
        if (c.Shoot().IsDown()) buttons |= IntentButtons::Shoot;
        if (c.Zoom().IsDown()) buttons |= IntentButtons::Zoom;
        if (c.Jump().IsDown()) buttons |= IntentButtons::Jump;
        if (c.Morph().IsDown()) buttons |= IntentButtons::Morph;
        if (c.Boost().IsDown()) buttons |= IntentButtons::Boost;
        if (c.AltAttack().IsDown()) buttons |= IntentButtons::AltAttack;
        if (c.ScanVisor().IsDown()) buttons |= IntentButtons::ScanVisor;
        if (c.NextWeapon().IsDown()) buttons |= IntentButtons::NextWeapon;
        if (c.PrevWeapon().IsDown()) buttons |= IntentButtons::PrevWeapon;
        if (c.RolltLeft().IsDown()) buttons |= IntentButtons::RollLeft;
        if (c.RollRight().IsDown()) buttons |= IntentButtons::RollRight;
        if (c.RollUp().IsDown()) buttons |= IntentButtons::RollUp;
        if (c.RollDown().IsDown()) buttons |= IntentButtons::RollDown;
        if (player.EquipInfo()->Zoomed) buttons |= IntentButtons::ZoomedState;
        if (player.IsAltForm()) buttons |= IntentButtons::AltFormState;
        if (HasFlag(player.LoadFlags(), Entities::LoadFlags::Spawned) && player.Health() > 0)
        {
            buttons |= IntentButtons::InPlayState;
        }
        if (HasFlag(player.Flags2(), Entities::PlayerFlags2::Spectating))
        {
            buttons |= IntentButtons::SpectatingState;
        }
        if (Mods::EndScreen::Ready())
        {
            buttons |= IntentButtons::ReadyState;
        }
        IntentPacket intent{};
        intent.Buttons = buttons;
        intent.Aim = player.ModGunVector();
        intent.Position = player.Position;
        intent.WeaponSelect = static_cast<std::uint8_t>(player.CurrentWeapon());
        intent.AmmoUa = static_cast<std::uint16_t>(std::clamp(player.ModAmmo().first, 0, 0xFFFF));
        intent.AmmoMissiles = static_cast<std::uint16_t>(std::clamp(player.ModAmmo().second, 0, 0xFFFF));
        intent.Presses = std::make_shared<std::vector<std::uint32_t>>(_pressHistory.begin(), _pressHistory.end());
        intent.ChargeLevel = static_cast<std::uint8_t>(std::clamp(_hasLatch ? _latchedCharge : player.ModChargeLevel(), 0, 255));
        intent.BoostDamage = static_cast<std::uint8_t>(std::clamp(_hasLatch ? _latchedBoostDamage : player.ModBoostDamage(), 0, 255));
        intent.ShotFlags = static_cast<std::uint8_t>((player.DoubleDamage() ? IntentPacket::FlagDoubleDamage : 0)
            | (player.IsPrimeHunter() ? IntentPacket::FlagPrimeHunter : 0));
        intent.HasState = true;
        intent.SetTouchReport(player.ModTouchReport());
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
        std::int32_t shootAge = 0;
        const IntentButtons missed = MissedPresses(player.SlotIndex(), intent, shootAge);
        if (player.SlotIndex() >= 0 && player.SlotIndex() < static_cast<std::int32_t>(ShootPressAge.size()))
        {
            ShootPressAge[Index(player.SlotIndex())] = shootAge;
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
        Set(c.Morph(), HasFlag(intent.Buttons, IntentButtons::Morph), HasFlag(missed, IntentButtons::Morph));
        if (c.Morph().IsPressed())
        {
            NetLog::Event("slot " + std::to_string(player.SlotIndex()) + " morph press received, now " + player.ModFormState());
        }
        Set(c.Boost(), HasFlag(intent.Buttons, IntentButtons::Boost), HasFlag(missed, IntentButtons::Boost));
        Set(c.AltAttack(), HasFlag(intent.Buttons, IntentButtons::AltAttack), HasFlag(missed, IntentButtons::AltAttack));
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
            ApplyForm(player, HasFlag(intent.Buttons, IntentButtons::AltFormState));
        }
        // Every intent, with or without a touch block: one without (a demo
        // recorded before) is no contact, never the last contact repeated.
        player.ModSetReportedTouch(intent.TouchReport(), intent.SlotGeneration, intent.LifeId);
        if (intent.HasState && (NetSession::IsAuthority() || NetSession::IsHost()))
        {
            player.ModSetShotState(intent.ChargeLevel, intent.BoostDamage,
                (intent.ShotFlags & IntentPacket::FlagDoubleDamage) != 0);
        }
    }

    void NetPlayerBridge::NoteSpawn(std::int32_t slot)
    {
        if (slot < 0 || slot >= static_cast<std::int32_t>(_pressSeen.size()))
        {
            return;
        }
        const auto s = Index(slot);
        _pressSeen[s] = false;
        ShootPressAge[s] = 0;
        SpawnFrame[s] = NetSession::NetFrame();
        _aimHeld[s] = true;
    }

    bool NetPlayerBridge::AimAvailable(std::int32_t slot)
    {
        if (slot < 0 || slot >= static_cast<std::int32_t>(NetSession::RemoteIntents.size())
            || !NetSession::RemoteIntentValid[slot] || !AimTrusted(slot)
            || NetSession::RemoteIntentAge(slot) > NetHooks::StaleIntentFrames)
            return false;
        const auto& intent = NetSession::RemoteIntents[slot];
        return intent.MatchId != 0 && intent.AuthorityEpoch != 0
            && intent.MatchId == NetSession::CurrentMatchId() && intent.AuthorityEpoch == NetSession::AuthorityEpoch()
            && intent.LifeId != 0 && NetPlayerLifecycle::Matches(slot, intent.SlotGeneration, intent.LifeId)
            && HasFlag(intent.Buttons, IntentButtons::InPlayState) && Sane(intent.Aim);
    }

    bool NetPlayerBridge::AimTrusted(std::int32_t slot)
    {
        return slot < 0 || slot >= static_cast<std::int32_t>(_aimHeld.size()) || !_aimHeld[Index(slot)];
    }

    IntentButtons NetPlayerBridge::MissedPresses(std::int32_t slot, const IntentPacket& intent, std::int32_t& shootAge)
    {
        shootAge = 0;
        if (slot < 0 || slot >= static_cast<std::int32_t>(_lastPressFrame.size()) || intent.Presses == nullptr)
        {
            return IntentButtons::None;
        }
        const auto s = Index(slot);
        if (!_pressSeen[s])
        {
            _pressSeen[s] = true;
            _lastPressFrame[s] = intent.Frame;
            return IntentButtons::None;
        }
        IntentButtons missed = IntentButtons::None;
        const std::vector<std::uint32_t>& presses = *intent.Presses;
        for (std::int32_t i = static_cast<std::int32_t>(presses.size()) - 1; i >= 0; i--)
        {
            if (intent.Frame < static_cast<std::uint32_t>(i))
            {
                continue;
            }
            const std::uint32_t frame = intent.Frame - static_cast<std::uint32_t>(i);
            if (frame <= _lastPressFrame[s])
            {
                continue;
            }
            const auto press = static_cast<IntentButtons>(presses[Index(i)]);
            missed |= press;
            if (shootAge == 0 && HasFlag(press, IntentButtons::Shoot))
            {
                shootAge = i;
            }
        }
        _lastPressFrame[s] = std::max(_lastPressFrame[s], intent.Frame);
        return missed;
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
            Move(player, state.Position);
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
            || (player.Hunter() == Hunter::Weavel
                && ((state.WeavelFlags & ~PlayerState::WeavelFlagsKnown) != 0
                    || ((state.WeavelFlags & PlayerState::WeavelFlagMorphing) != 0
                        && (state.WeavelFlags & PlayerState::WeavelFlagUnmorphing) != 0)
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
        _formSaid[s] = static_cast<std::uint8_t>((state.Flags & PlayerState::FlagAltForm) != 0 ? 2 : 1);
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
        if (!isLocal)
        {
            Move(player, InForm(player, state.Position, (state.Flags & PlayerState::FlagAltForm) != 0));
            player.SetSpeed(state.Speed);
            player.SetHealth(NetHitPrediction::HealthFor(slot, state.Health));
            player.ModSetFacing(state.Facing);
            player.ModSetWeapon(static_cast<BeamType>(state.CurrentWeapon));
            player.ModSetZoom((state.Flags & PlayerState::FlagZoomed) != 0);
            if (player.Hunter() != Hunter::Weavel)
                ApplyForm(player, (state.Flags & PlayerState::FlagAltForm) != 0);
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
            player.ModApplyWeavelState(state.HeadingAlt(),
                (state.WeavelFlags & PlayerState::WeavelFlagTurretActive) != 0, state.HalfturretHealth,
                state.HalfturretPosition, (state.WeavelFlags & PlayerState::WeavelFlagTurretGrounded) != 0);
        }
        else if (player.Hunter() == Hunter::Weavel && !NetSession::IsAuthority())
        {
            player.ModApplyOwnWeavelTurret(state.HeadingAlt(),
                (state.WeavelFlags & PlayerState::WeavelFlagTurretActive) != 0, state.HalfturretHealth);
        }
        player.ModSetFrozen((state.Flags & PlayerState::FlagFrozen) != 0);
        ApplyAfflictions(player, state);
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
        // The same shot's launch frame as two machines stamped it: the
        // victim's copy is the ack of whichever intent was newest when the
        // trigger pull arrived, a frame or two either side of the shooter's.
        bool SameLaunch(std::uint8_t a, std::uint8_t b) noexcept
        {
            const auto diff = static_cast<std::uint8_t>(a - b);
            return diff <= 3U || diff >= 253U;
        }

        bool ConfirmableBeam(std::uint8_t beam) noexcept
        {
            // The Shock Coil is one continuous beam drawn every frame: there
            // is no projectile to bring in, and its ticks would each be an
            // impact. It is drawn as the shooter aims it.
            return beam < 9 && static_cast<::MphRead::BeamType>(beam) != ::MphRead::BeamType::ShockCoil;
        }

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

        // A remote human's projectile on a machine that only draws it.
        Entities::PlayerEntity* RemoteShooterOf(Entities::BeamProjectileEntity& beam) noexcept
        {
            if (!NetSession::Active() || NetSession::IsAuthority() || NetSession::IsHost())
            {
                return nullptr;
            }
            Entities::PlayerEntity* owner = NetHitPrediction::OwnerOf(&beam);
            if (owner == nullptr || owner->IsBot() || owner->SlotIndex() == NetHooks::LocalSlot()
                || owner->SlotIndex() < 0)
            {
                return nullptr;
            }
            return owner;
        }
    }

    void NetPlayerBridge::ConfirmIncoming(std::int32_t attackerSlot, std::int32_t victimSlot, std::uint8_t beam,
        bool keyed, std::uint8_t launchLow, ImpactOffset impact, bool headshot)
    {
        if (!keyed)
        {
            // An older event of the history, which carries no shot key: the
            // hit is still drawn, at the chest, with no shot brought in.
            const std::int32_t me = NetHooks::LocalSlot();
            if (_confirmedImpacts && attackerSlot >= 0 && attackerSlot != me && victimSlot >= 0
                && attackerSlot != victimSlot && ConfirmableBeam(beam) && (victimSlot == me || _observedImpacts))
            {
                NativeRuntime::IncrementInPlace(_unkeyedImpacts);
                SynthesizeImpact(attackerSlot, victimSlot, beam, 0, OpenTK::Mathematics::Vector3(0.0F, 0.3F, 0.0F), headshot);
            }
            return;
        }
        using OpenTK::Mathematics::Vector3;
        const auto& players = Entities::PlayerEntity::Players();
        const std::int32_t local = NetHooks::LocalSlot();
        // The shooter's own machine predicted this hit itself; any other
        // machine is the victim's or an observer's.
        if (_confirmedImpacts && attackerSlot >= 0 && attackerSlot < static_cast<std::int32_t>(_coilVictim.size())
            && attackerSlot != local && victimSlot >= 0 && victimSlot != attackerSlot
            && beam == static_cast<std::uint8_t>(::MphRead::BeamType::ShockCoil)
            && (victimSlot == local || _observedImpacts) && impact.Known())
        {
            // A tick of the continuous beam: nothing to bring in, but the
            // beam itself is aimed at where the ticks land for a few frames.
            const auto a = static_cast<std::size_t>(attackerSlot);
            _coilVictim[a] = victimSlot;
            _coilOffset[a] = impact.Value();
            _coilUntil[a] = NetSession::NetFrame() + 12U;
            NativeRuntime::IncrementInPlace(_coilTicks);
            return;
        }
        if (!_confirmedImpacts || local < 0 || attackerSlot < 0 || attackerSlot == local || attackerSlot == victimSlot
            || victimSlot < 0 || static_cast<std::size_t>(attackerSlot) >= players.size()
            || static_cast<std::size_t>(victimSlot) >= players.size() || !ConfirmableBeam(beam)
            || (victimSlot != local && !_observedImpacts))
        {
            NativeRuntime::IncrementInPlace(_confirmsIgnored);
            return;
        }
        if (victimSlot != local)
        {
            NativeRuntime::IncrementInPlace(_observedConfirms);
        }
        const Entities::PlayerEntity& victim = *players[static_cast<std::size_t>(victimSlot)];
        // Where the shooter saw it land; the chest when the authority
        // resolved the hit itself and nobody said.
        const Vector3 offset = impact.Known() ? impact.Value() : Vector3(0.0F, 0.3F, 0.0F);
        Entities::PlayerEntity& shooter = *players[static_cast<std::size_t>(attackerSlot)];
        if (shooter.EquipInfo() != nullptr && shooter.EquipInfo()->Beams != nullptr)
        {
            // The closest launch frame wins: a fast weapon has a shot in the
            // air every few frames, and the neighbour is not the one that hit.
            auto& beams = *shooter.EquipInfo()->Beams;
            Entities::BeamProjectileEntity* best = nullptr;
            std::int32_t bestGap = 4;
            for (std::int32_t i = 0; i < beams.Length(); ++i)
            {
                Entities::BeamProjectileEntity* shot = beams[i].get();
                if (shot == nullptr || shot->Lifespan() <= 0 || shot->ModShooterAck == 0 || shot->ModConfirmedTarget
                    || ::MphRead::TestFlag(shot->Flags(), Entities::BeamFlags::Collided)
                    || (shot->ModTargetSlot >= 0 && shot->ModTargetSlot != victimSlot))
                {
                    continue;
                }
                const auto diff = static_cast<std::int8_t>(static_cast<std::uint8_t>(shot->ModShooterAck & 0xFFU) - launchLow);
                const std::int32_t gap = std::abs(static_cast<std::int32_t>(diff));
                if (gap < bestGap)
                {
                    bestGap = gap;
                    best = shot;
                }
            }
            if (best != nullptr && !best->ModPassedTarget && !best->ModTouchedTarget && best->ModHeldUntil == 0)
            {
                // Bringing it in must not mean turning it round: a shot that
                // has gone past, or would have to bend more than 60 degrees,
                // is drawn landing on the spot instead.
                const Vector3 toTarget = static_cast<Vector3>(victim.Position) + offset
                    - static_cast<Vector3>(best->Position);
                const Vector3 velocity = best->Velocity();
                const float lengths = OpenTK::Mathematics::Length(toTarget) * OpenTK::Mathematics::Length(velocity);
                const float cosine = lengths > 0.0001F ? Vector3::Dot(toTarget, velocity) / lengths : 1.0F;
                if (cosine < 0.5F)
                {
                    best->ModTargetSlot = victimSlot;
                    best->ModPassedTarget = true;
                    NoteRemoteShotGone(*best);
                    SynthesizeImpact(attackerSlot, victimSlot, beam, launchLow, offset, headshot);
                    return;
                }
                const double angle = std::acos(std::clamp(static_cast<double>(cosine), -1.0, 1.0)) * 57.29578;
                _homeAngleSum += angle;
                _homeAngleMax = std::max(_homeAngleMax, angle);
            }
            if (best != nullptr && !best->ModPassedTarget && !best->ModTouchedTarget)
            {
                if (best->ModHeldUntil != 0)
                {
                    NoteConfirmDelay(NetSession::NetFrame() - best->ModHeldSince);
                }
                best->ModTargetSlot = victimSlot;
                best->ModConfirmedTarget = true;
                best->ModConfirmedOffset = offset;
                NativeRuntime::IncrementInPlace(_confirmsInFlight);
                return;
            }
            if (best != nullptr)
            {
                // It went by before the word came: draw the hit where it was.
                SynthesizeImpact(attackerSlot, victimSlot, beam, launchLow, offset, headshot);
                return;
            }
        }
        const ConfirmGoneShot* gone = RecentlyGone(attackerSlot, launchLow);
        if (gone != nullptr && gone->Blast > 0.0F
            && OpenTK::Mathematics::Length(gone->Where - static_cast<Vector3>(victim.Position)) <= gone->Blast + 1.6F)
        {
            // It went out in a blast that reached them here: that explosion is
            // the hit they saw. Another impact on top would be one too many.
            NativeRuntime::IncrementInPlace(_seenAsBlast);
            HitLocation::Synthesized(attackerSlot, victim, beam, 0, gone->Where, headshot, true);
            return;
        }
        if (gone == nullptr)
        {
            // Not drawn yet: the damage outran the relayed trigger pull. Wait
            // a few frames for the shot to appear before drawing it ourselves.
            for (ConfirmPending& pending : _pendingConfirms)
            {
                if (!pending.Live)
                {
                    pending = ConfirmPending{attackerSlot, launchLow, beam, offset,
                        NetSession::NetFrame() + ConfirmWaitFrames, true, headshot, victimSlot};
                    return;
                }
            }
        }
        SynthesizeImpact(attackerSlot, victimSlot, beam, launchLow, offset, headshot);
    }

    void NetPlayerBridge::OnRemoteShotSpawned(Entities::BeamProjectileEntity& beam)
    {
        if (!_confirmedImpacts || beam.ModShooterAck == 0)
        {
            return;
        }
        Entities::PlayerEntity* owner = RemoteShooterOf(beam);
        if (owner == nullptr)
        {
            return;
        }
        const auto low = static_cast<std::uint8_t>(beam.ModShooterAck & 0xFFU);
        for (ConfirmPending& pending : _pendingConfirms)
        {
            if (pending.Live && pending.Attacker == owner->SlotIndex() && SameLaunch(pending.LaunchLow, low))
            {
                beam.ModTargetSlot = pending.Victim;
                beam.ModConfirmedTarget = true;
                beam.ModConfirmedOffset = pending.Offset;
                pending.Live = false;
                NativeRuntime::IncrementInPlace(_confirmsAtSpawn);
                return;
            }
        }
    }

    void NetPlayerBridge::NoteRemoteShotGone(const Entities::BeamProjectileEntity& beam)
    {
        if (beam.ModShooterAck == 0)
        {
            return;
        }
        Entities::PlayerEntity* owner = RemoteShooterOf(const_cast<Entities::BeamProjectileEntity&>(beam));
        if (owner == nullptr)
        {
            return;
        }
        const bool blast = beam.SplashDamage() > 0.0F
            && ::MphRead::TestFlag(beam.Flags(), Entities::BeamFlags::Collided);
        _goneShots[_goneNext] = ConfirmGoneShot{owner->SlotIndex(),
            static_cast<std::uint8_t>(beam.ModShooterAck & 0xFFU), std::max(1U, NetSession::NetFrame()),
            static_cast<OpenTK::Mathematics::Vector3>(beam.Position), blast ? beam.SplashRadius() : 0.0F};
        _goneNext = (_goneNext + 1) % _goneShots.size();
    }

    const ConfirmGoneShot* NetPlayerBridge::RecentlyGone(std::int32_t attackerSlot, std::uint8_t launchLow)
    {
        const std::uint32_t now = NetSession::NetFrame();
        for (const ConfirmGoneShot& gone : _goneShots)
        {
            if (gone.Frame != 0 && gone.Attacker == attackerSlot && SameLaunch(gone.LaunchLow, launchLow)
                && now - gone.Frame < 120U)
            {
                return &gone;
            }
        }
        return nullptr;
    }

    bool NetPlayerBridge::PassesThrough(Entities::BeamProjectileEntity& beam, const Entities::PlayerEntity& player)
    {
        if (!_confirmedImpacts || (beam.ModConfirmedTarget && beam.ModTargetSlot == player.SlotIndex())
            || ::MphRead::TestFlag(beam.Flags(), Entities::BeamFlags::Continuous))
        {
            return false;
        }
        const Entities::PlayerEntity* owner = RemoteShooterOf(beam);
        return owner != nullptr && owner != &player
            && (player.SlotIndex() == NetHooks::LocalSlot() || _observedImpacts);
    }

    void NetPlayerBridge::NotePassedLocal(const Entities::BeamProjectileEntity& beam)
    {
        NativeRuntime::IncrementInPlace(_passedThrough);
        NoteRemoteShotGone(beam);
    }

    void NetPlayerBridge::TickConfirms()
    {
        const std::uint32_t now = NetSession::NetFrame();
        for (ConfirmPending& pending : _pendingConfirms)
        {
            if (pending.Live && static_cast<std::int32_t>(now - pending.Until) >= 0)
            {
                pending.Live = false;
                SynthesizeImpact(pending.Attacker, pending.Victim, pending.Beam, pending.LaunchLow, pending.Offset,
                    pending.Headshot);
            }
        }
    }

    void NetPlayerBridge::SynthesizeImpact(std::int32_t attackerSlot, std::int32_t victimSlot, std::uint8_t beam,
        std::uint8_t launchLow, OpenTK::Mathematics::Vector3 offset, bool headshot)
    {
        // Drawn on a body that is falling as well: the shot that kills you
        // is the one most worth seeing.
        const auto& players = Entities::PlayerEntity::Players();
        if (victimSlot < 0 || static_cast<std::size_t>(victimSlot) >= players.size())
        {
            return;
        }
        const Entities::PlayerEntity& victim = *players[static_cast<std::size_t>(victimSlot)];
        const OpenTK::Mathematics::Vector3 point = static_cast<OpenTK::Mathematics::Vector3>(victim.Position) + offset;
        Entities::BeamProjectileEntity::ModSpawnImpact(_scene, static_cast<::MphRead::BeamType>(beam), point,
            OpenTK::Mathematics::Vector3(offset.X, 0.0F, offset.Z).LengthSquared() > 0.0001F
                ? OpenTK::Mathematics::Vector3(offset.X, 0.0F, offset.Z).Normalized()
                : OpenTK::Mathematics::Vector3(0.0F, 1.0F, 0.0F));
        NativeRuntime::IncrementInPlace(_impactsSynthesized);
        // The shot key's full frame, from the newest ack this shooter sent.
        std::uint32_t launch = launchLow;
        const auto s = static_cast<std::size_t>(attackerSlot);
        if (attackerSlot >= 0 && s < NetSession::RemoteIntents.size() && NetSession::RemoteIntentValid[s])
        {
            const std::uint32_t newest = NetSession::RemoteIntents[s].AckFrame;
            launch = newest - static_cast<std::uint8_t>(static_cast<std::uint8_t>(newest & 0xFFU) - launchLow);
        }
        HitLocation::Synthesized(attackerSlot, victim, beam, launch, point, headshot);
    }

    bool NetPlayerBridge::CoilAimFor(const Entities::PlayerEntity& shooter, OpenTK::Mathematics::Vector3 muzzle,
        OpenTK::Mathematics::Vector3& aim)
    {
        const std::int32_t slot = shooter.SlotIndex();
        const auto& players = Entities::PlayerEntity::Players();
        if (slot < 0 || slot >= static_cast<std::int32_t>(_coilVictim.size())
            || shooter.CurrentWeapon() != ::MphRead::BeamType::ShockCoil)
        {
            return false;
        }
        const auto s = static_cast<std::size_t>(slot);
        const std::int32_t victim = _coilVictim[s];
        if (victim < 0 || static_cast<std::size_t>(victim) >= players.size()
            || static_cast<std::int32_t>(NetSession::NetFrame() - _coilUntil[s]) >= 0)
        {
            return false;
        }
        const OpenTK::Mathematics::Vector3 to = static_cast<OpenTK::Mathematics::Vector3>(players[static_cast<std::size_t>(victim)]->Position)
            + _coilOffset[s] - muzzle;
        const float length = OpenTK::Mathematics::Length(to);
        const float scale = OpenTK::Mathematics::Length(aim);
        if (!(length > 0.0001F))
        {
            return false;
        }
        aim = OpenTK::Mathematics::Scale(to, (scale > 0.0001F ? scale : 1.0F) / length);
        return true;
    }

    std::uint32_t NetPlayerBridge::HoldFrames() noexcept
    {
        if (_confirmDelayCount < 16)
        {
            return 6;
        }
        std::array<std::uint8_t, 64> sorted = _confirmDelays;
        const auto used = static_cast<std::ptrdiff_t>(std::min(_confirmDelayCount, sorted.size()));
        std::sort(sorted.begin(), sorted.begin() + used);
        const std::uint32_t p90 = sorted[static_cast<std::size_t>((used - 1) * 9 / 10)];
        return std::clamp(p90 + 1U, 2U, 8U);
    }

    void NetPlayerBridge::NoteConfirmDelay(std::uint32_t frames) noexcept
    {
        _confirmDelays[_confirmDelayNext] = static_cast<std::uint8_t>(std::min(frames, 255U));
        _confirmDelayNext = (_confirmDelayNext + 1) % _confirmDelays.size();
        _confirmDelayCount++;
    }

    std::string NetPlayerBridge::DescribeConfirms()
    {
        return "confirmed impacts (hold " + std::to_string(HoldFrames()) + " frames): " + std::to_string(_confirmsInFlight) + " shots in the air brought in (turned "
            + NativeRuntime::ToString(_confirmsInFlight > 0 ? _homeAngleSum / static_cast<double>(_confirmsInFlight) : 0.0, "F1")
            + " deg on average, at most " + NativeRuntime::ToString(_homeAngleMax, "F1") + "), "
            + std::to_string(_confirmsAtSpawn) + " brought in from the moment they appeared, "
            + std::to_string(_impactsSynthesized) + " drawn on the spot (shot gone or never drawn), "
            + std::to_string(_seenAsBlast) + " already seen as the blast that reached them, "
            + std::to_string(_coilTicks) + " Shock Coil ticks aiming its beam, "
            + std::to_string(_unkeyedImpacts) + " older events drawn at the chest, "
            + std::to_string(_passedThrough) + " unconfirmed shots let through, "
            + std::to_string(_observedConfirms) + " of them on other players (observed), "
            + std::to_string(_confirmsIgnored) + " not applicable (continuous beam, own or unknown shooter)";
    }

    void NetPlayerBridge::ShooterRay(const Entities::PlayerEntity& shooter, OpenTK::Mathematics::Vector3 drawnMuzzle,
        OpenTK::Mathematics::Vector3& from, OpenTK::Mathematics::Vector3& direction, std::uint32_t& ackFrame)
    {
        const std::int32_t slot = shooter.SlotIndex();
        from = drawnMuzzle;
        direction = OpenTK::Mathematics::Vector3::Zero;
        ackFrame = 0;
        if (slot < 0 || static_cast<std::size_t>(slot) >= NetSession::RemoteIntents.size()
            || !NetSession::RemoteIntentValid[static_cast<std::size_t>(slot)])
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
        const IntentPacket& intent = NetSession::RemoteIntents[static_cast<std::size_t>(slot)];
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

    void NetPlayerBridge::ApplyAfflictions(Entities::PlayerEntity& player, PlayerState state)
    {
        player.ModSetDisrupted((state.Flags & PlayerState::FlagDisrupted) != 0);
        player.ModSetBurning((state.Flags & PlayerState::FlagBurning) != 0);
    }

    void NetPlayerBridge::ApplyForm(Entities::PlayerEntity& player, bool altForm)
    {
        const std::int32_t slot = player.SlotIndex();
        if (slot < 0 || slot >= static_cast<std::int32_t>(_formReconciliation.size()))
        {
            return;
        }
        const FormCorrection correction = ReconcileForm(slot, NetSession::NetFrame(),
            altForm, player.IsAltForm(), player.IsMorphing(), player.IsUnmorphing(),
            NetSession::SlotPing[Index(slot)]);
        if (correction == FormCorrection::Start)
        {
            player.ModStartFormSwitch();
        }
        else if (correction == FormCorrection::Force)
        {
            player.ModForceForm(altForm);
        }
    }

    FormCorrection NetPlayerBridge::ReconcileForm(std::int32_t slot, std::uint32_t frame, bool desiredAlt,
        bool actualAlt, bool morphing, bool unmorphing, std::int32_t ping)
    {
        return slot < 0 || slot >= static_cast<std::int32_t>(_formReconciliation.size()) ? FormCorrection::None
            : _formReconciliation[Index(slot)].Step(frame, desiredAlt, actualAlt, morphing, unmorphing, ping);
    }

    bool NetPlayerBridge::Diverged(Entities::PlayerEntity& player, const PlayerState& state, std::int32_t slot)
    {
        if (slot < 0 || slot >= static_cast<std::int32_t>(_divergedFrames.size()))
        {
            return false;
        }
        OpenTK::Mathematics::Vector3 then = player.Position;
        const std::int32_t lagFrames = slot < static_cast<std::int32_t>(NetSession::SlotPing.size())
            ? std::clamp(NetSession::SlotPing[Index(slot)] * 60 / 1000, 0, 100)
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
        _formReconciliation.fill(FormReconciliation{});
        _lifeApplied.fill(false);
        _reportSeen.fill(false);
        _divergedFrames.fill(0);
    }

    void NetPlayerBridge::Reset()
    {
        _formReconciliation.fill(FormReconciliation{});
        _appliedLifeId.fill(0);
        _lifeApplied.fill(false);
        _snaps = 0;
        _worstSnap = 0.0F;
        NodeLookupsUnresolved = 0;
        PlacementsRefused = 0;
        SpawnFacingsTurned = 0;
        WorstSpawnFacing = 0.0F;
        StaleDeathsIgnored = 0;
        _formSaid.fill(0);
        _lastPressFrame.fill(0);
        _pressSeen.fill(false);
        _aimHeld.fill(false);
        SpawnFrame.fill(0);
        ShootPressAge.fill(0);
        _pressHistory.fill(0);
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
        _formReconciliation[s].Reset();
        _lifeApplied[s] = false;
        _appliedLifeId[s] = 0;
        _lastPressFrame[s] = 0;
        _pressSeen[s] = false;
        _aimHeld[s] = false;
        NetShotEvents::BeginLife(slot);
        SpawnFrame[s] = 0;
        ShootPressAge[s] = 0;
        _respawnRequested[s] = false;
        if (slot == NetSession::LocalSlot())
        {
            _pressHistory.fill(0);
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
        OpenTK::Mathematics::Vector3 smoothed{};
        bool smoothedAlt = false;
        if (NetSmoothing::Sample(player.SlotIndex(), smoothed, smoothedAlt)
            && Sane(smoothed) && !VectorEquals(smoothed, OpenTK::Mathematics::Vector3::Zero))
        {
            Move(player, InForm(player, smoothed, smoothedAlt));
            return;
        }
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

    void NetPlayerBridge::Move(Entities::PlayerEntity& player, OpenTK::Mathematics::Vector3 position)
    {
        const OpenTK::Mathematics::Vector3 previous = player.Position;
        player.Position = position;
        player.SetPrevPosition(position);
        player.ModRefreshNodeRef(previous);
        player.ModRefreshVolume();
        player.ModRefreshAttachedEffects();
    }
}
