# Homing shots: the row, the target, the 30 Hz flight, and how all three are checked

Four weapon families home in a match -- the Power Beam (affinity row, partial
charge), the Missile and the Volt Driver (affinity rows, full charge) and the
Shock Coil (a continuous lock on every row) -- and the enemy, boss, Gorea and
platform tables add eleven more rows. Every one of them is decided by three
things, and each has its own owner in `Mods/`:

| Question | Answered by | Where |
|---|---|---|
| Which weapon row did the shot leave with? | the variant rule: hunter affinity, the story's exceptions, and the Prime Hunter role **at the frame it fired** | `Mods/Combat/WeaponVariant.hpp` (`ResolveWeaponVariantAtFire`, `ResolveEquippedWeaponVariant`), `Mods/Network/Gameplay/Weapons/FiringVariant` |
| How charged, and so how strongly does it steer? | the charge rule, one for spawn, claims and the reference | `Mods/Combat/WeaponCharge.hpp` |
| What does it steer at? | chosen once, at spawn, by whoever fired it; carried on the wire; never looked for again | `HomingTargetAcquisition`, `HomingTargetSelection`, `HomingTargetHistory`, `HomingTargetReference` |
| How does it fly? | moves every 60 Hz frame by half a tick, changes velocity (gravity, speed curve, steering) only as each 30 Hz tick ends | `Mods/Combat/ProjectileFlight`, `ProjectileHoming`, `HomingSteeringRules` |
| What does another screen draw? | a drawn flight beside the physical one; corrections move only the drawn one | `Mods/Network/Gameplay/Weapons/ShotPresentation`, `ConfirmedShotSteering` |

`BeamProjectileEntity` holds none of this: it calls these classes and carries
three `Mod` members (`ModPresentation`, `ModHomingPhase`, `ModFlightPhase`).

## The rules that are easy to break

- **Equipped is not fired.** The equipped row (what charges, what the HUD
  shows) never includes the Prime Hunter role; the row a shot *leaves* with
  does, as of its fire frame. The shot event carries that row and a Prime flag;
  the authority does not take the flag on trust -- it checks it against its own
  history of who held the role in that frame (`NetUnlagged::PrimeHunterAt`).
- **Role snapshots and playout acknowledgements are separate.** Protocol 33
  carries the authority's Prime holder in the snapshot header without growing
  its size. A shot's `RoleFrame`, and a continuous tick's
  `ContinuousRoleFrame`, name that snapshot. `AckFrame` still names the
  interpolated target world. A predicted local kill cannot grant a firing
  variant; the owner uses the received role, and the authority validates it
  against its history. Continuous claims retain the role frame through
  settlement, including an intent arriving after its claim.
- **The target is decided once, by the shooter.** The shot event (or, for a
  continuous weapon, the intent) names it: player slot + generation + life, a
  scene entity's id, or an enemy's stable id (`EnemyTargetIdentity`, derived
  from its spawner, never a pointer). Another copy takes that entity as it
  stands; the authority first checks the choice against its history of the
  fire frame. A copy that receives a name it cannot lock right now (the target
  already down on that machine) locks nothing rather than choosing for itself.
- **`BeamResultFlags::Homing` means a continuous lock**, as it always has: it
  drives the shot sound. A ballistic shot that took a target does not set it.
- **The 60 Hz flight is the 30 Hz flight.** Velocity changes once a tick and
  the frame between is the tick's straight midpoint, so every second frame lands
  exactly where the DS's flight lands. Applying gravity or the speed curve every
  frame instead was measured at 0.30-0.54 u off and a charged Volt Driver
  arriving 49 ms late -- more than a tick. Steering was already on the tick.
- **A picture never moves a shot.** `ShotPresentation::DrawScope` swaps the
  drawn flight in for the draw and hands every field back; the retarget and
  confirmed-impact corrections write only the drawn flight.

## How it is checked

| Command | What it proves |
|---|---|
| `fruity_weapon_combat_rules_tests` (CTest `WeaponCombatRules`) | the rules in isolation: rows, charges, selection, and the 60 Hz core against the 30 Hz reference to 1e-4 u |
| `FruityPrime -homingparitycheck [ROOM]` | the real projectile (Spawn/Process in a loaded room) against `Mods/Diagnostics/HomingReferenceFlight` for every homing row: path, heading, first contact, wall-before-target order, release of a target behind, no re-acquisition |
| `FruityPrime -homingdisplaycheck [ROOM]` | a remote copy drawn at 60/144/240/540 fps through `FrameTiming`, confirmed impacts and retarget each on and off: no picture moves the physical shot, the drawn flight does not depend on the frame rate, each correction still does its job |
| `FruityPrime -homingcontinuouscheck [ROOM]` | the Shock Coil on a real player: lock every tick, damage, drain only on the affinity row, a moving target, the morph cone, a wall that is locked through but stops the beam |
| `FruityPrime -homingspawncheck [SECONDS]` | a census of every story room's weapon users, then the real spawners, AIs and platforms firing at a player: row, spawn charge, steering against the table at that charge, lock, turn |
| `tools/testing/run-homing-net-matrix.ps1` / `-all.ps1` | a dedicated server and `-netcheck -hitrig homing` clients (plus `-bots`, `-rigmorph`, `-netlag`, `-netloss`, `-netreorder`, Prime Hunter mode), every machine writing `-homingtrace`; `compare-homing-trace.py` lines every shot up across the owner, the authority and every observer |

`-HostMode` requests a room from a real loopback directory, which starts
an isolated dedicated-server process. Every player, including the creator,
joins as a client and must report `authority=False`. Only the server judges
claims and publishes snapshots. The shooter's predicted hit and hit claim
retain priority; assigning lobby ownership never changes that path. Prime
cells require a Prime shot, and the full matrix returns failure if any cell
fails. It runs up to three independent cells concurrently; `-Parallelism 1`
runs them serially. Spawn checks require every named weapon row, including a
controlled equipment fixture for Enemy:8 when the installed assets have no
placement using it. The live AI still chooses its spawn charge.

`FruityPrime -hostauthoritycheck [ROOM]` checks the local create-and-join
path, exact owner-token transport, online lobby ownership/options, two
concurrent isolated rooms, server snapshots after creator departure, and
continued client prediction/claim eligibility. CTest runs it as
`HostedAuthority` when game files are present.

CTest's `NetworkCombat` also runs the real combat fixture with game files;
the role handoff and a false Prime declaration have explicit regressions.
The real projectile parity fixture also changes the Prime holder while a
Prime Power Beam is airborne; its firing-time trajectory and first contact
must still match the independent reference.
Joining waits for the selected hunter to appear in the server roster before
gameplay can send its first shot. Hello/Identify ordering under lag must not
let a placeholder hunter choose the first projectile's affinity row.
The display fixture checks the production presentation state at four draw
rates; it does not capture GPU pixels or measure a physical monitor.

`-homingtrace` writes one `[homing]` line per projectile, claim, verdict and
drain to the debug log. A ballistic shot is keyed by
`slot gen life seq proj`; a continuous tick by `slot gen life intent` (the
shooter's frame). `acquired=` says how each copy came by its target --
`chosen`, `received`, `received-ineligible`, `refused`.
Each projectile line also carries `rawOrigin`, `rawRay`, physical `velocity`,
`range`, `cosLimit`, and the selected target's `targetDistance`/`targetCos`
measured at spawn. The last two are `-` when no target geometry is available.

The netcheck client's own verdict reads `FAIL -- never took a single hit` in
the homing rig: its runners never fire. That is the rig, not the result; read
`compare-homing-trace.py`'s `RESULT`.
