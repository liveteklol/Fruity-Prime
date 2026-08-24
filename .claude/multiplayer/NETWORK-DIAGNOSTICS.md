# Multiplayer — diagnostics, and the damage bug (resolved 2026-08-23)

The long-running "one remote slot receives zero damage for the whole match" bug
was two faults, both from commit `bbb13b8`, and **neither was latency**. Full
account in `CLAUDE.md`, "The damage bug, and what it actually was".

1. **Every client except the authority was frozen.** `ApplyState` took position
   and speed from the snapshot for the local player. The authority's copy of
   that player is the player's own report from a round trip earlier, so writing
   it back pinned the character forever and every frame of local movement was
   discarded before it could be published. Three clients, seventy seconds: the
   authority moved 95 units, the other two moved 0 and 2. It reproduced on
   loopback too.
2. **Remote players fired from their ankles.** `ModSetAim` assigned a remote
   player's `CameraInfo.Position` from its bare `Position`, missing the
   `AimYOffset` (0.9 units) that `UpdateCameraFirst` adds. Every shot starts
   from that field, so on the authority a remote beam travelled parallel to the
   one its owner aimed and 0.9 units below it. The authority's own player was
   unaffected -- which is why the failure looked directional and looked like lag.

The measurement that separated them from latency, at 11 ms of ping:

```text
authority   player overlaps by shooter: [0->1] 2 [1->0] 1
slot 1      player overlaps by shooter: [1->2] 69
slot 2      player overlaps by shooter: [2->0] 58
```

After the fix, at 130 ms of induced latency, all three agree within two hits.

Against the Pi on MP3 PROVING GROUND, three clients, 100 s, after all four
fixes: **PASS on all three**, 0 mismatches, 0 visible teleports, damage
`[0] 21 [1] 19 [2] 37` resolved on the authority and replayed identically by
both observers, and `player overlaps by shooter` agreeing within 4 on every
pair. Pick the map deliberately -- MP1 SANCTORUS, which the Pi's rotation opens
on, is big enough that three scripted players never meet, and a run there shows
zero overlaps for *everybody* and deaths by kill plane.

## Two more the first fix uncovered

Taking the local player's position from the snapshot had been hiding
disagreements, not only freezing people.

3. **Every respawn placed the two machines differently.** `GetRespawnPoint`
   picks among points free of living players *on the machine running it* and
   rotates with the frame counter, and the local player respawns early by
   holding fire -- so it chooses well before the authority does, and neither
   ever yields. 55 in 100 s, up to 175 units apart. Local spawning is kept for
   responsiveness; only the placement is handed over, on the frame the
   authority's snapshot first reports the player spawned.
4. **A derived velocity became a launch.** `Speed` was worked out from two
   reported positions; across a respawn that is the whole level over two
   frames, so ~150 units per frame -- published in the snapshot, applied by
   every client, and handed back to the owner at its next respawn. The
   authority was measured holding a player at Y=163 climbing 35 units a frame,
   with 975 corrections logged in 100 s. Steps beyond `SnapDistance` now yield
   zero, the result is clamped to `MaxReportedSpeed` (5 u/frame against boost's
   0.6), and a freshly placed player gets no speed at all.

Three clients, 100 s at 130 ms, after both: **0 visible teleports** (was 715,
worst 265 units), 3/0/0 corrections (was 975/55/0), 0 mismatches, damage
`31/3/20` resolved and replayed identically, and Weavel's halfturret exercised
for the first time (601 frames, observers 600 and 617).

## The randomised sweep

`~/mph-net-test/run-batch.sh <runs> [seed]` draws map, roster, player count,
match length, point goal, latency (0-250 ms each way) and packet loss (0-3%) at
random, keeps every run's logs under `batch-<seed>/NN/`, and prints only the runs
that reported something. Short matches and low point goals are deliberate: a
match that ends inside a run is the case every "is this a new match" bug hides
in, and the fixed scenarios were all long enough to avoid it.

Four more came out of it, three of them only at a rotation:

5. **`NetRoomChange.Settling` had no callers** -- the guard went with the loop it
   was attached to. 26 corrections in 4 s at one restart, the local player
   alternating between two rooms' coordinates.
6. **The damage sequence was reset on each side separately at a room change.**
   It is a sequence number, not a tally; resetting it on machines that change
   room on different frames means either a resync that swallows the next real
   hits or **up to 32 hits replayed into a player who has just spawned**. Four
   `damage sequence jumped` events per client per rotation; one run had the
   authority resolve 115 hits and three of five clients replay none.
7. **The divergence backstop compared against "now"** instead of against the
   instant the authority was looking at. A player falling out of the level covers
   30 units in half a round trip at 250 ms, so it was hauled back up out of its
   own fall and could never die -- 77 in one run, peers seeing 64-unit jumps. It
   now indexes this player's own recorded history by the ping and requires a
   second of disagreement. After: zero corrections, zero teleports.
8. **Every jump pad was being called a desync.** `Mods.WorldEvents` already
   reports pads and teleporters; the check now asks and grants the same grace it
   gives a respawn.

## What is left at 250 ms and is not a bug

`alt-attack` at ~40% (one-frame presses collapsing into one intent window),
`movement` at ~50% (a 30 Hz reconstruction of a 60 Hz path with a fifth of the
updates reordered and refused), `zoom` lagging (a toggle with a half-second round
trip), `form stayed wrong for 181 frames` (the correction machinery's full budget
-- and the check fails at 60, *below* that budget, so it cannot pass whenever the
machinery has acted), and `never took a single hit` on a large map (check
`player overlaps by shooter`: empty for *everybody* means the room, not the
network).

## The sweep has to reach the Pi

**A randomised run against a loopback server is a regression check, not a
real-world one.** Loopback has none of the reordering, jitter or server-side CPU
load the bugs in this file were found under. `run-batch.sh` is the fast local
check; `run-batch-pi.sh` is the one whose result counts -- it puts `udp-lag.py`
*in front of the Pi*, so the real path is underneath and the chosen delay is on
top, which is the only way to ask a server that answers in 7-17 ms what happens
at 250. It sets the map, match length and point goal in the server's own
rotation over SSH per run and restores it on the way out.

## Reproducing latency

```bash
cd ~/mph-net-test
./run-lag.sh 60 90 Samus Kanden Trace     # 60 ms each way
```

`udp-lag.py` is the relay; `run-lag.sh` puts a loopback server behind it.

## Traps

- `run-check.sh` copied `MphRead.dll`, which the rename to `FruityPrime` deleted,
  with `2>/dev/null`. Every run silently tested a stale binary. Fixed in both
  runners.
- The Pi was running **protocol 4** while the notes claimed protocol 3. A
  mismatched Hello is dropped silently, so the client reports "the server did
  not answer in time" -- which reads as a dead server. Probe it with a
  `StatusQuery` (packet type 14); the protocol byte is at payload offset 96.
- `NetFeatureCheck` compared puppets against `NetSession.RemoteStates` on the
  authority, which never receives a snapshot. It read clean only while everybody
  was frozen. Skipped there now.
- `NetTestScript.FindTarget`'s deterministic ring was `% 3`, so every run with
  more than three players aimed the extra slots back at slots 1 and 2. It now
  rings over the whole roster.
- **`untested` is a question about the harness, not a pass.** The zoom phase
  pressed the Imperialist's own weapon key, which only selects a weapon already
  picked up -- so on a fresh roster it did nothing, the zoom button was never
  held, and the phase reported `untested` for months. Instrumented, no intent in
  a whole run carried `IntentButtons.Zoom`. The tour now *issues* a zoom-capable
  weapon (`ModArmZoomWeapon`), presses towards the wanted state rather than on a
  timer (`UpdateZoom` is an edge toggle, so a cadence zooms in and back out),
  and presses zoom off on the way out (nothing else clears it, and the
  Imperialist does half damage unzoomed). Zoom replication was never broken:
  `mine 124 / theirs 123`, PASS both ways.

## Flying bodies, invincible players, hits never counted

Three separate faults, reported together as "weird things happen with two
Trace clients" -- each one a shape worth recognising again.

9. **A hit launched the victim across the level.** `TakeDamage` adds its
   `direction` argument straight onto `Speed`, so whatever is in it is a
   velocity in units per frame. Most beams carry no direction of their own
   (`DamageDirType` is 0), so `NetDamage.Note` filled one in as
   `victim.Position - attacker.Position` -- the *distance between the two
   players*, not a unit vector, so a hit from ten units away launched the
   victim at ten units a frame through the wall. It looked asymmetric ("A
   shoots B and B flies, B shoots A and it's fine") because A was the
   authority, applying its own damage directly with the real vector, while
   everyone else replays. Fix: relay the impulse verbatim, zero included, and
   let the receiver's own fallback (indicator only, never `Speed`) handle
   zero.
10. **A player took no damage at all, until the shooter switched weapons.**
    `BeamProjectileEntity` refuses to spawn a beam whose ammo cost exceeds the
    shooter's ammo. Every machine simulates every player's shots and spends
    ammo, but pickups are collected locally and not replicated -- so the
    puppet on the authority's machine ran dry within a round, the authority
    created no projectile, and the target took nothing while the shooter's own
    screen showed a hit. Switching weapons "fixed" it only until the other
    pool ran out too. **Look for this shape whenever a remote player can do
    something on their own machine and not anyone else's: the puppet runs the
    same code with different resources, and only the owner's copy of those is
    authoritative.** Fix: the intent now carries the owner's ammo, the way it
    already carried position and weapon; `ModSetAmmo` writes it onto the
    puppet.
11. **Snapshots were the one stream nobody ordered.** Both intent streams
    refuse an older frame; `HandleSnapshot` did not, and it's the stream
    carrying health, score and the damage counter. An overtaken datagram put a
    player back where they'd been, undid a kill, and ran the (byte-sized)
    damage counter backwards -- `Replay` read the difference as ~250 new hits:
    25 resolved on the authority against 258 "replayed" on a client. Ordering
    the stream took three lines. `NetDamage.Replay` also now refuses more than
    32 hits in one snapshot, so one bad packet can't flinch, shove or kill
    from a corrupted count.

## "Observers only see half your turn"

Listed for a long time as packet loss under load. It was two things, neither
quite that:

12. **The transport dropped the wrong packets.** `NetTransport` queued 256
    received packets and, when full, dropped the *arriving* one -- the newest
    input, in a protocol whose packets say "this is where I'm aiming *now*".
    Eight clients on one machine produce ~2000 packets/s between them, so one
    130 ms frame overflows it. Queue is 2048 now, socket buffers 1 MB, and an
    overflow drops the oldest.
13. **The check compared two different sample rates.** A player publishes
    position/aim every `IntentSendInterval` frames (30 Hz), but
    `NetFeatureCheck` measured the *local* player's path every frame (60 Hz)
    and compared lengths -- looked clean with one opponent (aim moves slowly),
    fell apart with seven (aim slews several times a second, the 30 Hz
    reconstruction cuts every corner). Fixed by sampling the local player only
    at the instants it actually publishes.

Eight clients, 150 s, one machine: 33 mismatches before both fixes, 10 after,
none of them `facing`.

## The Shock Coil did double damage against players

Not a network fault. It's the one beam that stays alive and re-tests
collision every frame, and every beam hit carries `DamageFlags.NoDmgInvuln`
unconditionally, so the per-hit invulnerability window never applied and
nothing else limited how often it landed. At 30 fps that was fine; this
engine runs at 60, so it landed roughly twice as often -- ~600 dmg/s, a full
hunter in a sixth of a second. The identical compensation already existed in
the same file for the same weapon against *enemies* (with a `// todo: FPS
stuff` beside it); only the player path had been missed. Now does half what
it did before -- **not independently measured**, since the scripted tour
barely fires the beam at a player; rests on reading the code beside its
already-corrected twin.

## Idle server cost

The Pi's run loop slept 1 ms between passes whether or not anyone was
connected -- 5-7% of one core burnt for nothing. Now sleeps 20 ms while empty.
Six real clients on the Pi 3B: 5-22% of one core (typically 11-16%), system
65-75% idle, 0 packets dropped.

## The scoreboard's ping column

Tab shows scores as always; in a networked match there's now a third column,
from the server's per-second roster measurement, smoothed so one late
datagram isn't a worse connection. Green under 80 ms, amber under 160, red
above, `--` before the first measurement. A player on the same machine as the
server reads ~20 ms, not 0 — the round trip includes the client's own frame.
The two stock columns shift left in a networked session (`ModScoreColumn1/2`)
to make room.

## Diagnostics available

`NetDamage`: `Fired`, `AimDrift`/`WorstDrift`, `PlayerChecks`,
`PlayerOverlaps`, `PlayerAccepted`, `PlayerOverlapsByShooter`.
`MPHREAD_NETLOG_INTERVAL` sets the netlog cadence.

`player overlaps by shooter` is the one that mattered: comparing a shooter's own
count against the authority's is what separates "the shot missed" from "the shot
was resolved somewhere else".

## Still open

No lag compensation, and one missing field is why it cannot have any: nothing a
client sends says which snapshot it was looking at. `IntentPacket.Frame` and
`SnapshotHeader.Frame` are counters in unrelated clocks, each starting at zero
when its own process joins -- so the abandoned frame-stamped rewind was indexing
the authority's history with the shooter's frame number. Echoing
`NetSession.LastSnapshotFrame` back in the intent fixes that for four bytes, a
protocol bump and a Pi redeploy. Not needed for correctness.

`double damage` is still usually `untested` -- nothing in the tour picks one up.
Item pickup is simulated on every machine from replicated positions and the
clients agree exactly on how many items were taken, so it is probably fine, but
probably is not measured.
