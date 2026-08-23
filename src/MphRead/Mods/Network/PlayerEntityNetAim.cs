using System;
using MphRead.Mods.Network;
using OpenTK.Mathematics;

namespace MphRead.Entities
{
    /// <summary>
    /// Network aim injection for remote players.
    ///
    /// Lives here rather than in PlayerInput.cs because PlayerEntity is
    /// already a partial class: the aim helpers (UpdateAimX/UpdateAimY) are
    /// private, and reaching them from a partial declaration costs upstream
    /// exactly one call site instead of four visibility changes.
    ///
    /// The reason remote aim needs its own path at all: the mouse branch in
    /// PlayerInput is gated on "!IsBot", and a networked player is driven the
    /// same way a bot is. Without this, remote players would move correctly
    /// but always face straight ahead.
    /// </summary>
    public partial class PlayerEntity
    {
        private const int NetworkHistoryLength = 120;
        private readonly Vector3[] _networkPositionHistory = new Vector3[NetworkHistoryLength];
        private readonly uint[] _networkPositionFrames = new uint[NetworkHistoryLength];
        private int _networkPositionHistoryCount;

        internal void ModRecordNetworkPosition(uint frame)
        {
            int count = Math.Min(_networkPositionHistoryCount, NetworkHistoryLength - 1);
            for (int i = count; i > 0; i--)
            {
                _networkPositionHistory[i] = _networkPositionHistory[i - 1];
                _networkPositionFrames[i] = _networkPositionFrames[i - 1];
            }
            _networkPositionHistory[0] = Position;
            _networkPositionFrames[0] = frame;
            _networkPositionHistoryCount = Math.Min(count + 1, NetworkHistoryLength);
        }

        internal bool ModGetNetworkPosition(uint frame, out Vector3 position)
        {
            for (int i = 0; i < _networkPositionHistoryCount; i++)
            {
                if (_networkPositionFrames[i] <= frame)
                {
                    position = _networkPositionHistory[i];
                    return true;
                }
            }
            position = default;
            return false;
        }

        /// <summary>Where this player's gun points. Sent as the aim in every intent.</summary>
        internal Vector3 ModGunVector => _gunVec1;


        internal void ModRefreshNetworkAim()
        {
            if (NetSession.Active && SlotIndex != NetHooks.LocalSlot
                && NetSession.RemoteIntentValid[SlotIndex])
            {
                ModSetAim(NetSession.RemoteIntents[SlotIndex].Aim);
            }
        }

        /// <summary>
        /// Point a remote player's gun where its owner is pointing it.
        ///
        /// Absolute, not a rotation: see IntentPacket.Aim. Everything derived
        /// from the aim ray is recomputed here, because the authority decides
        /// what a shot hits from _aimPosition and would otherwise fire along
        /// last frame's ray.
        /// </summary>
        internal void ModSetAim(Vector3 aim)
        {
            if (!(aim.LengthSquared > 0.0001f))
            {
                return;
            }
            if (NetSession.Active && SlotIndex != NetHooks.LocalSlot)
            {
                // A remote player's camera is not the authoritative state.
                // Repositioning the player without moving this cached camera
                // made its transmitted aim originate from the previous room
                // position, so every remote beam missed its target.
                //
                // At eye height, which this line used to leave out. Every
                // shot in the game starts from CameraInfo.Position -- it is
                // what _gunDrawPos and then _muzzlePos are built from in
                // UpdateAimVecs -- and UpdateCameraFirst puts it AimYOffset
                // above the feet, nine tenths of a unit. Assigning the bare
                // position therefore fired every remote player's beam from
                // its ankles, along a ray parallel to the one its owner was
                // looking down and nine tenths of a unit under it. Aimed at
                // somebody's chest that goes into the floor in front of
                // them.
                //
                // The tell was that it looked exactly like latency and was
                // not: on the authority the shots that landed were its own,
                // the one player whose camera the engine still owned. With
                // eleven milliseconds of ping and three clients on one map,
                // slot 1's own machine counted 69 of its shots overlapping
                // slot 2 while the authority counted none at all.
                CameraInfo.Position = _field6D0
                    ? Position
                    : Position.AddY(Fixed.ToFloat(Values.AimYOffset));
            }
            _gunVec1 = aim.Normalized();
            float flat = MathF.Sqrt(_gunVec1.X * _gunVec1.X + _gunVec1.Z * _gunVec1.Z);
            _aimY = Math.Clamp(MathHelper.RadiansToDegrees(MathF.Atan2(_gunVec1.Y, flat)), -85, 85);
            _aimPosition = CameraInfo.Position + _gunVec1 * Fixed.ToFloat(Values.AimDistance);
            UpdateAimFacing();
        }

        /// <summary>
        /// Recompute this player's room node after its position was written
        /// in from the network.
        ///
        /// NodeRef is what the renderer culls against (PlayerDraw:
        /// `IsMainPlayer || IsVisible(NodeRef)`), and the engine normally
        /// advances it during simulation. A remote player whose position is
        /// assigned directly skips that step, keeps the node it spawned in,
        /// and disappears -- or shows only a shadow -- once the viewer is
        /// somewhere else in the room.
        /// </summary>
        internal void ModRefreshNodeRef(OpenTK.Mathematics.Vector3 previousPosition)
        {
            _volume = CollisionVolume.Move(_volumeUnxf, Position);
            NodeRef = _scene.UpdateNodeRef(NodeRef, previousPosition, Position);
        }

        /// <summary>
        /// Record the positions a collision query is about to be built from.
        /// The candidate pool drains when limitMin/limitMax span a huge grid
        /// range, and those come straight from PrevPosition and Position, so
        /// logging both says whether a bad position is the cause.
        /// </summary>
        internal void ModLogCollisionRange()
        {
            if (!NetLog.Enabled || !NetSession.Active)
            {
                return;
            }
            NetLog.CollisionRange(SlotIndex, "pre-check", PrevPosition, Position);
        }

        /// <summary>
        /// Point a remote player where the authority says it is pointing.
        ///
        /// Only the authority runs a remote player's aim (it has the relayed
        /// deltas); everyone else receives the resulting facing in the
        /// snapshot. Without applying it, a puppet slid around the map still
        /// facing whatever direction it spawned in.
        /// </summary>
        internal void ModSetFacing(OpenTK.Mathematics.Vector3 facing)
        {
            // Not-a-number fails every comparison, including this one, so the
            // length test has to be written as a test for a good value rather
            // than a test for a bad one.
            if (!(facing.LengthSquared > 0.0001f))
            {
                return;
            }
            _facingVector = facing.Normalized();
            SetTransform(_facingVector, _upVector, Position);
        }

        /// <summary>
        /// Whether this player is standing in the map rather than waiting at
        /// the origin to be placed. Spawned is set by Spawn() and never
        /// cleared, so health is what distinguishes "in the match" from
        /// "dead, waiting for a respawn point".
        /// </summary>
        internal bool ModIsInPlay => LoadFlags.TestFlag(LoadFlags.Spawned) && _health > 0;

        /// <summary>
        /// Put a puppet on the map where the authority put the real player.
        ///
        /// Spawn() is what clears HideModel, gives the player its health and
        /// marks the slot Spawned; a puppet that only ever had a position
        /// written into it stayed hidden, which is why a correctly tracked
        /// remote player could still be invisible. Only the transition needs
        /// this -- once spawned, plain position writes are enough.
        /// </summary>
        internal void ModNetSpawn(OpenTK.Mathematics.Vector3 position,
            OpenTK.Mathematics.Vector3 facing)
        {
            OpenTK.Mathematics.Vector3 forward = facing.LengthSquared > 0.0001f
                ? facing.Normalized()
                : -OpenTK.Mathematics.Vector3.UnitZ;
            Spawn(position, forward, OpenTK.Mathematics.Vector3.UnitY,
                _scene.GetNodeRefByPosition(position), respawn: true);
        }

        /// <summary>
        /// How far this player would have to turn, in the degrees
        /// UpdateAimX/UpdateAimY take, to be pointing at a position.
        ///
        /// Derived from _gunVec1 rather than the facing vector because that
        /// is what the aim helpers rotate and what the gun actually fires
        /// along; the biped's facing lags it while turning.
        /// </summary>
        internal (float X, float Y) ModAimDeltaTowards(Vector3 target)
        {
            Vector3 desired = ModAimVectorTowards(target);
            float flatLength = MathF.Sqrt(desired.X * desired.X + desired.Z * desired.Z);
            float aimFlat = MathF.Sqrt(_gunVec1.X * _gunVec1.X + _gunVec1.Z * _gunVec1.Z);
            if (flatLength < 0.001f || aimFlat < 0.001f)
            {
                return (0, 0);
            }
            float tx = desired.X / flatLength;
            float tz = desired.Z / flatLength;
            float ax = _gunVec1.X / aimFlat;
            float az = _gunVec1.Z / aimFlat;
            // UpdateAimX rotates (x, z) by the negative of its argument
            // (x' = x cos + z sin), so the turn to make is the negated
            // signed angle from the current aim to where it should be.
            float turn = -MathHelper.RadiansToDegrees(
                MathF.Atan2(ax * tz - az * tx, ax * tx + az * tz));
            float targetPitch = MathHelper.RadiansToDegrees(MathF.Atan2(desired.Y, flatLength));
            float currentPitch = MathHelper.RadiansToDegrees(MathF.Atan2(_gunVec1.Y, aimFlat));
            return (turn, targetPitch - currentPitch);
        }

        /// <summary>
        /// Where _gunVec1 has to point for a shot to hit a position.
        ///
        /// Not simply "at the target". A shot travels from the muzzle towards
        /// _aimPosition, which sits a fixed distance down the aim ray from the
        /// eye, so the gun and the crosshair only agree at that one distance --
        /// the game's convergence point, which a person compensates for by
        /// watching where the shots land. Pointing the aim ray straight at a
        /// target further away therefore missed low and to the side, every
        /// time. This solves for the aim direction whose convergence point
        /// lies on the muzzle-to-target line instead.
        /// </summary>
        private Vector3 ModAimVectorTowards(Vector3 target)
        {
            Vector3 eye = CameraInfo.Position;
            float aimDistance = Fixed.ToFloat(Values.AimDistance);
            Vector3 fromMuzzle = target - _muzzlePos;
            if (fromMuzzle.LengthSquared < 0.0001f || aimDistance <= 0)
            {
                return target - eye;
            }
            Vector3 direction = fromMuzzle.Normalized();
            Vector3 offset = _muzzlePos - eye;
            float b = Vector3.Dot(offset, direction);
            float c = Vector3.Dot(offset, offset) - aimDistance * aimDistance;
            float discriminant = b * b - c;
            if (discriminant < 0)
            {
                // The target line never reaches the convergence sphere -- at
                // point-blank range the two are close enough that aiming
                // straight at it is right anyway.
                return target - eye;
            }
            float t = -b + MathF.Sqrt(discriminant);
            return _muzzlePos + direction * t - eye;
        }

        /// <summary>Where a shot aimed at this player should be pointed.</summary>
        internal Vector3 ModAimTarget => Position + PlayerVolumes[(int)Hunter, 0].SpherePosition;

        /// <summary>
        /// Change which hunter this slot is playing. Initialize() rebuilds the
        /// models and values from it, so callers must run that afterwards --
        /// NetSlotManager does, as part of activating the slot.
        /// </summary>
        internal void ModSetHunter(Hunter hunter)
        {
            Hunter = hunter;
        }

        /// <summary>
        /// Whether the HUD's directional damage indicator is currently lit.
        ///
        /// The check that says the damage feedback actually ran: these timers
        /// are set inside TakeDamage and nowhere else, so a client that had
        /// its health assigned from a snapshot never lit one.
        /// </summary>
        internal bool ModDamageIndicatorActive
        {
            get
            {
                for (int i = 0; i < _damageIndicatorTimers.Length; i++)
                {
                    if (_damageIndicatorTimers[i] > 0)
                    {
                        return true;
                    }
                }
                return false;
            }
        }

        /// <summary>
        /// Put a remote player into or out of alt form to match the authority.
        ///
        /// Morphing is an input the owner performs, and only the authority
        /// receives that input: everyone else has a puppet with no controls,
        /// so the snapshot's alt-form bit was written every frame and read by
        /// nobody. A player who morphed stayed a biped on every other screen.
        ///
        /// Routed through the engine's own switch rather than the flag it
        /// sets, because the form change also swaps the collision volume, the
        /// model and the transform -- setting the flag alone would leave a
        /// morph ball standing upright with a hunter's hitbox.
        /// </summary>
        /// <summary>
        /// Begin the transition into or out of alt form, animation and all.
        ///
        /// The owner's relayed input normally does this by itself; this is
        /// for the case where the two machines have ended up disagreeing.
        /// </summary>
        internal void ModStartFormSwitch()
        {
            bool switched = TrySwitchForms(force: true);
            NetLog.Event($"slot {SlotIndex} form switch requested -> {switched}, now {ModFormState()}");
        }

        /// <summary>
        /// Put this player in a form outright, skipping the transition.
        ///
        /// A last resort, and deliberately not the first thing tried.
        /// EnterAltForm only sets Morphing and leaves UpdateForm to run when
        /// the morph animation ends, while ExitAltForm applies the change
        /// immediately -- so forcing the form as soon as a difference
        /// appeared removed the morph-in animation and left the morph-out one
        /// intact, which is exactly the asymmetry that showed up in play.
        /// </summary>
        internal void ModForceForm(bool altForm)
        {
            if (altForm == IsAltForm)
            {
                return;
            }
            NetLog.Event($"slot {SlotIndex} form forced to {(altForm ? "alt" : "biped")} "
                + $"from {ModFormState()}");
            Flags1 &= ~PlayerFlags1.Morphing;
            Flags1 &= ~PlayerFlags1.Unmorphing;
            UpdateForm(altForm);
        }

        /// <summary>
        /// Put a remote player on the weapon the authority says it is
        /// holding.
        ///
        /// The weapon travels in every snapshot and was never applied. Input
        /// alone does not settle it: a NextWeapon press cycles through
        /// whatever that machine believes is available, and availability
        /// depends on pickups, which are not replicated -- so two clients
        /// pressing the same button landed on different beams and drew the
        /// wrong gun firing the wrong colour.
        /// </summary>
        internal void ModSetWeapon(BeamType weapon)
        {
            if (weapon == CurrentWeapon || weapon < BeamType.PowerBeam
                || weapon > BeamType.OmegaCannon)
            {
                return;
            }
            // Availability is a local notion built from local pickups; the
            // authority saying this player holds the weapon is what settles
            // it. Charging comes with it: a puppet that could hold a weapon
            // but not charge it fired uncharged shots for its owner's charged
            // ones, which is every affliction in the game.
            _availableWeapons[weapon] = true;
            _availableCharges[weapon] = true;
            TryEquipWeapon(weapon, silent: true);
        }

        /// <summary>
        /// Universal ammo and missiles, as the owner's own machine counts
        /// them.
        /// </summary>
        internal (int Ua, int Missiles) ModAmmo => (_ammo[UA], _ammo[Missiles]);

        /// <summary>
        /// Put a remote player's ammo where its owner says it is.
        ///
        /// Not cosmetic, and not a nicety: BeamProjectileEntity refuses to
        /// spawn a beam whose cost exceeds the shooter's ammo, so a puppet
        /// that has run dry produces no projectile at all -- on the very
        /// machine that decides what is hit. The shooter watches their own
        /// beam leave the gun and connect, the authority never creates one,
        /// and the target is untouchable. It reads exactly like an invincible
        /// player, and switching to a weapon on the other ammo pool "fixes"
        /// it, because that pool still has something in it.
        ///
        /// The two drift apart within a round for a reason that cannot be
        /// closed any other way: every machine simulates the shot and spends
        /// the ammo, but pickups are collected locally and are not
        /// replicated, so only the owner's count is ever right. Sending it is
        /// cheaper and more honest than trying to replicate item state.
        /// </summary>
        internal void ModSetAmmo(int ua, int missiles)
        {
            // -1 is the engine's "infinite" marker; a puppet must not be
            // handed one by a malformed packet.
            _ammo[UA] = Math.Clamp(ua, 0, _ammoMax[UA]);
            _ammo[Missiles] = Math.Clamp(missiles, 0, _ammoMax[Missiles]);
        }

        /// <summary>
        /// Put this hunter's own weapon in its hands, with ammo.
        ///
        /// The three affliction states -- frozen, burning, disrupted -- exist
        /// only on the affinity version of a weapon: TryEquipWeapon swaps in
        /// table entry beam + 9 when the beam is the hunter's own, and only
        /// those entries carry an Affliction flag. In a match that weapon is
        /// picked up, not issued, so a scripted tour that never happened to
        /// walk over the right pickup could not reach three of the states a
        /// player can be put into. The tour issues it instead.
        ///
        /// Test-only: nothing in a normal session calls this.
        /// </summary>
        internal void ModArmAffinityWeapon()
        {
            BeamType beam = Weapons.GetAffinityBeam(Hunter);
            if (beam < BeamType.PowerBeam || beam > BeamType.OmegaCannon)
            {
                return;
            }
            _availableWeapons[beam] = true;
            _availableCharges[beam] = true;
            WeaponInfo info = Weapons.Current[(int)beam];
            _ammo[info.AmmoType] = _ammoMax[info.AmmoType];
            if (CurrentWeapon != beam)
            {
                TryEquipWeapon(beam, silent: true);
            }
        }

        /// <summary>
        /// Issue a weapon that can zoom, for the tour's zoom phase.
        ///
        /// The phase used to press the Imperialist's own key and hope. In a
        /// match it is picked up, not issued, so a fresh roster has nobody
        /// holding one, the key press did nothing, and the phase reported
        /// `untested` run after run -- which reads as "not proven" and was
        /// really "never attempted". The same reasoning as
        /// <see cref="ModArmAffinityWeapon"/>: a probe that waits to walk over
        /// the right pickup never gets there.
        ///
        /// Judicator as the fallback because it is the other weapon in the
        /// table carrying <see cref="WeaponFlags.CanZoom"/>.
        /// </summary>
        internal void ModArmZoomWeapon()
        {
            BeamType beam = BeamType.Imperialist;
            if (!Weapons.Current[(int)beam].Flags.TestFlag(WeaponFlags.CanZoom))
            {
                beam = BeamType.Judicator;
            }
            _availableWeapons[beam] = true;
            _availableCharges[beam] = true;
            WeaponInfo info = Weapons.Current[(int)beam];
            _ammo[info.AmmoType] = _ammoMax[info.AmmoType];
            if (CurrentWeapon != beam)
            {
                TryEquipWeapon(beam, silent: true);
            }
        }

        /// <summary>
        /// Turn a scripted player without a session behind it.
        ///
        /// The map audit runs the tour with no network at all, and the hook
        /// that normally applies a scripted player's aim (ModApplyRemoteAim)
        /// returns early when the session is offline. Without this the audit's
        /// eight players walked, jumped and fired, but faced wherever they
        /// spawned -- so they rarely hit each other, and everything that only
        /// happens on a hit went untested.
        /// </summary>
        internal void ModApplyScriptAim(float deltaX, float deltaY)
        {
            UpdateAimY(deltaY);
            UpdateAimX(deltaX);
        }

        /// <summary>
        /// What this player is holding and how charged it is, for the audit's
        /// affliction probe -- "the shot landed and nothing happened" has
        /// several causes and they are not distinguishable from outside.
        /// </summary>
        internal string ModWeaponState =>
            $"{CurrentWeapon} equip={EquipInfo.Weapon.Beam} charge={EquipInfo.ChargeLevel}"
            + $"/{EquipInfo.Weapon.MinCharge * 2} chargeable={_availableCharges[CurrentWeapon]}"
            + $" affliction={EquipInfo.Weapon.Afflictions[1]}"
            + $" shooting={Flags2.TestFlag(PlayerFlags2.Shooting)}";

        /// <summary>Charge built up on the current weapon, for the audit.</summary>
        internal int ModChargeLevel => EquipInfo.ChargeLevel;

        /// <summary>
        /// Whether letting go now would fire a *charged* shot.
        ///
        /// Not a fixed number of frames: a weapon without the PartialCharge
        /// flag counts as charged only at FullCharge (60 frames doubled), and
        /// one with it from MinCharge (15 doubled). A probe that held the
        /// trigger for what looked like plenty -- 64 frames -- fired shot
        /// after uncharged shot, and every affliction in the game lives on the
        /// charged entry of its weapon.
        /// </summary>
        internal bool ModChargeReady
        {
            get
            {
                WeaponInfo weapon = EquipInfo.Weapon;
                if (!weapon.Flags.TestFlag(WeaponFlags.CanCharge))
                {
                    return false;
                }
                int needed = weapon.Flags.TestFlag(WeaponFlags.PartialCharge)
                    ? weapon.MinCharge * 2
                    : weapon.FullCharge * 2;
                return EquipInfo.ChargeLevel >= needed;
            }
        }

        /// <summary>Frozen by an affinity Judicator, for the audit.</summary>
        internal bool ModFrozen => _frozenTimer > 0;

        /// <summary>Burning from an affinity Magmaul.</summary>
        internal bool ModBurning => _burnTimer > 0;

        /// <summary>Aim disrupted by an affinity Volt Driver.</summary>
        internal bool ModDisrupted => _disruptedTimer > 0;

        /// <summary>Whether this player can currently be zoomed, for the test tour.</summary>
        internal bool ModCanZoom => EquipInfo.Weapon != null
            && EquipInfo.Weapon.Flags.TestFlag(WeaponFlags.CanZoom);

        /// <summary>
        /// The form and animation state, for the per-client log.
        ///
        /// A puppet stuck between forms and one that simply has not been told
        /// yet look identical in the alt-form flag alone; the animation it is
        /// playing, and whether that animation has ended, is what separates
        /// them.
        /// </summary>
        internal string ModFormState()
        {
            string form = IsAltForm ? "alt" : "biped";
            if (IsMorphing)
            {
                form += "+morphing";
            }
            if (IsUnmorphing)
            {
                form += "+unmorphing";
            }
            return $"{form}/{Biped2Anim}"
                + (Biped2Flags.TestFlag(AnimFlags.Ended) ? "/ended" : "");
        }

        private OpenTK.Mathematics.Vector3 _modLastGoodFacing = -OpenTK.Mathematics.Vector3.UnitZ;
        private OpenTK.Mathematics.Vector3 _modLastGoodGunVec = -OpenTK.Mathematics.Vector3.UnitZ;
        private OpenTK.Mathematics.Vector3 _modLastGoodPosition;

        /// <summary>
        /// Put back the last usable vectors if this player's have stopped
        /// being numbers.
        ///
        /// UpdateAimFacing normalises (facing - gun * dot), which is the zero
        /// vector exactly when the aim and the body line up -- and
        /// normalising zero gives NaN. In a networked match that does not stay
        /// local: the NaN facing is published, every client applies it, anyone
        /// aiming at that player computes a NaN aim of their own, and within
        /// seconds several players are frozen, dying repeatedly, with every
        /// measurement reading NaN. One frame of stale facing is a far smaller
        /// price than that.
        /// </summary>
        internal void ModRepairVectors()
        {
            if (Finite(_facingVector))
            {
                _modLastGoodFacing = _facingVector;
            }
            else
            {
                _facingVector = _modLastGoodFacing;
                NetLog.Event($"slot {SlotIndex} facing repaired, {ModFormState()}, gun={_gunVec1}");
            }
            if (Finite(_gunVec1))
            {
                _modLastGoodGunVec = _gunVec1;
            }
            else
            {
                _gunVec1 = _modLastGoodGunVec;
                NetLog.Event($"slot {SlotIndex} aim repaired");
            }
            if (Finite(Position))
            {
                _modLastGoodPosition = Position;
            }
            else
            {
                Position = _modLastGoodPosition;
                NetLog.Event($"slot {SlotIndex} position repaired");
            }
            if (!Finite(Speed))
            {
                Speed = OpenTK.Mathematics.Vector3.Zero;
            }
            if (!Finite(_aimPosition))
            {
                _aimPosition = Position + _gunVec1;
            }
        }

        private static bool Finite(OpenTK.Mathematics.Vector3 value)
        {
            return Single.IsFinite(value.X) && Single.IsFinite(value.Y) && Single.IsFinite(value.Z);
        }

        /// <summary>
        /// Run the engine's own death sequence for a player the authority has
        /// killed by something that was not a hit -- a fall, a kill plane.
        /// Those never produce a damage record, so the victim only saw its
        /// health become zero and never counted a death or played any of it.
        /// </summary>
        internal void ModNetDie()
        {
            TakeDamage(1, DamageFlags.Death | DamageFlags.NoDmgInvuln, null, null);
        }

        /// <summary>
        /// The scoreboard's row count and the height it would need, for the
        /// test report.
        ///
        /// The list is drawn from a fixed centre on a 192-pixel screen, so
        /// past a certain number of players the first and last rows simply
        /// leave it. Checking the height is how a headless run can tell that
        /// a six-player scoreboard still fits, since the panel is drawn
        /// straight to the window and never appears in a captured frame.
        /// </summary>
        internal (int Rows, float Height) ModScoreboardSize()
        {
            return (GameState.ActivePlayers, GetScoreboardHeight());
        }

        /// <summary>
        /// Whether any beam can hurt this player at all.
        ///
        /// BeamEffectiveness is an array of nine multipliers and it defaults
        /// to Zero for every beam; only Spawn() fills it in. TakeDamage
        /// returns immediately on a Zero entry, so a player that reached the
        /// map without going through Spawn is not hard to hit -- it is
        /// literally invulnerable, and nothing about it looks wrong.
        /// </summary>
        internal bool ModCanBeHurt()
        {
            for (int i = 0; i < BeamEffectiveness.Length; i++)
            {
                if (BeamEffectiveness[i] != Effectiveness.Zero)
                {
                    return true;
                }
            }
            return false;
        }

        private void ApplyModAim()
        {
            if (!NetSession.Active)
            {
                return;
            }
            if (SlotIndex == NetHooks.LocalSlot)
            {
                // A scripted local player has no mouse, so its rotation has
                // to enter the same way a remote player's does. Same call
                // site, same point in the frame -- an autopilot that turned
                // at a different moment from the engine would be a test of
                // something the game never does.
                if (NetTestScript.Enabled)
                {
                    UpdateAimY(NetTestScript.AimDeltaY);
                    UpdateAimX(NetTestScript.AimDeltaX);
                }
                return;
            }
            if (!NetSession.RemoteIntentValid[SlotIndex])
            {
                return;
            }
            ModSetAim(NetSession.RemoteIntents[SlotIndex].Aim);
        }
    }
}
