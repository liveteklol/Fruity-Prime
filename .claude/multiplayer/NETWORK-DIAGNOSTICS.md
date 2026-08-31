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

## Intents go every frame now

`NetConfig.IntentSendInterval` is **1**, so a player publishes position and aim
at 60 Hz rather than 30.

This is not a smoothing question, it is what a remote player *is*: a puppet is
pinned to the position its owner reported (`NetPlayerBridge.RestoreReportedPosition`
runs after the engine's own movement step), so whatever the local simulation
does between intents is thrown away. At 2, everybody but yourself moved in 30 Hz
steps on a 60 Hz screen, and a recorded demo -- where every player is a puppet,
including the recorder's own -- stepped from end to end. That is what "the demo
looks like 30 fps" was. Measured in the file: a two-player recording carried
**53.8 SlotIntent/s before and 104.7 after** (~27/s per player, then ~52/s),
and grew from 3.0 to 4.2 KiB/s on disk.

It was 2 because the server relays N*(N-1) intents a frame and at six players
that was losing enough of them to leave gaps. What made that true was fault 12
below: a send queue that dropped the *newest* packets when it filled, which has
since been fixed.

Four eight-client 120 s loopback runs, two each way:

| | mismatches | scoreboards |
|---|---|---|
| 30 Hz | 15 (11 unmorph, 4 alt-attack) | agree |
| 30 Hz | 5 (3 unmorph, 1 alt-attack) | disagree by 2 |
| 60 Hz | 9 (6 unmorph, 2 teleports) | disagree by 5 |
| 60 Hz | 14 (13 unmorph) | disagree by 4 |

**What that shows is that it is not worse, and nothing finer than that.** The
run-to-run spread (5 to 15) swamps the difference between the cadences, and the
scoreboard drifts in three runs of four at both -- so it is this harness on this
machine, not the change. Eight clients on one box measure the box; `run-remote.sh`
against the Pi is the instrument for anything sharper. The cost is about 100
bytes on the wire per player per frame, so ~42 KB/s into each client of an
eight-player match.

No protocol bump: the layout did not move and nothing reads the cadence, so a
build sending at 30 and one sending at 60 understand each other exactly as
before -- the older one is simply seen in coarser steps. `ProtocolVersion` is
for a build that would read the bytes correctly and then play a different game,
which this is not.

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
13. **The check compared two different sample rates.** A player published
    position/aim every `IntentSendInterval` frames, which was then 30 Hz, but
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


## A vacated slot kept its occupant's score

Reported as: kill somebody, leave the match, reconnect to the same server, and
the kill is still there against your name.

It is the same shape as every other rejoin fault -- per-slot state describing
whoever *used* to be in the slot -- and the score was the one piece nobody had
cleared. `NetSlotManager` already forgot the simulation's state, the wire's and
the damage sequence's when a slot changed hands (three `ForgetSlot` calls);
`NetScoreboard.ForgetSlot` is the fourth, and it runs both when a slot is
vacated and when it is taken, because a slot can be refilled before this
machine has run a frame with it empty.

It has to happen on every machine and not only on the one that left: the
scoreboard belongs to the authority, which publishes Points, Kills and Deaths
per slot in each snapshot for everyone else to adopt. Team totals are not
touched -- `GameState.UpdateStandings` recomputes them from the per-slot
figures every update, so clearing the slot clears the team, and clearing a team
total directly would be wrong in a team mode.

Measured with four scripted clients on a 15-minute match, A leaving at 50 s and
D taking its slot:

| | A's slot when it left | slot 0 at the end |
|---|---|---|
| before | 0k/7d/-7p | 0k/**11d**/-11p (A's 7 plus D's 4) |
| after | 0k/6d/-6p | D's own 0k/4d/-4p, and 0k/0d/0p once D also left |

The rig's default `maprotation.txt` runs one-minute matches, and a rotation
resets every score (`NetRoomChange.ResetScores`) -- which hides this fault
completely. Any measurement of a scoreboard needs a match longer than the run.

## Rejoining: per-slot state, and what `run-rejoin.sh` measures

The report: A creates the game, B joins, A quits, B stays, A rejoins the
same match. B sees A but cannot hit them; A can hit B.

`~/mph-net-test/run-rejoin.sh [seconds] [leave_at] [rejoin_at] [host] [port]`
builds exactly that topology and adds a control:

- A joins first, so it takes slot 0 and becomes the authority.
- B joins (slot 1).
- A leaves. The server promotes B (`DedicatedServer.Remove`), so the
  authority moves mid-match -- which is half of what makes this scenario
  different from any other.
- Then **two** clients join at the same moment: C, which takes slot 0, the
  one A vacated, and D, which takes slot 2, never used. Both then play the
  same tour against the same authority for the same time.

A is two processes on purpose: the suspicion is state that is only cleared
when a session starts or stops, so a rejoin simulated inside one process
would clear the very thing that needs to survive.

**Not reproduced.** Loopback and against the Pi, the rejoined client in the
reused slot registers damage in both directions, and if anything more than
the control:

| | slot | took a hit |
|---|---|---|
| C (reused slot) loopback | 0 | 23 |
| D (fresh slot) loopback | 2 | 6 |
| C (reused slot) on the Pi | 0 | 13 |
| D (fresh slot) on the Pi | 2 | 5 |

A later run of the same script, after the fix below, gave C 1 hit and D 0 --
which is not a regression, it is the variance. Two to four scripted clients
duelling for a minute engage each other by luck: across runs the same slot
has come out anywhere between 0 and 23 hits. **The hit counts here answer
"did damage flow at all in this topology", and nothing finer.** The
regression gate is `run-check.sh`, which cross-checks what each client did
against what the others saw and reports mismatches; that is what to run
after touching any of this.

So slot reuse plus an authority handover does not, on its own, break
damage. Whatever the report is about needs something this does not have --
a human's engagement pattern rather than the tour's, the launcher's own
host path rather than a dedicated server, or a particular moment (a
rotation, an intermission) to rejoin in.

**One trap to avoid when running this.** An earlier run of this script was
read as a reproduction: the rejoining client reported `took a hit 0` and
`authority=True` when the server said the authority was the other client.
Both were artefacts. The `authority=True` was real but legitimate -- the
other client had left seconds before the report was printed, so the
authority had moved back. And `took a hit 0` was a two-player match with
little engagement, not a client that could not be hurt. **A single run of
two scripted clients is not evidence of a damage fault**; that is what the
control client is for.

### What was fixed anyway

Per-slot state was cleared only when the session started or stopped, or the
room changed -- never when a slot changed hands. So a slot's next occupant
inherited the previous one's reported position and frame number, spawn
barrier, divergence and staleness counters, and damage sequence.

`StaleSinceSpawn` names this hazard in its own comment -- "a peer that
reconnects restarts its counter at zero, and a slot that changes hands
inherits the barrier of whoever held it... a player nobody can hit and who
slides without ever taking a step" -- and bounds it with a 120-frame give-up,
so it costs two seconds rather than a session. Two seconds of a player who
cannot be hit is still the reported symptom.

`NetPlayerBridge.ForgetSlot` and `NetDamage.ForgetSlot`, called from both
`NetSlotManager.Activate` and `Deactivate`, clear it. A slot changing hands
means the old occupant's history is meaningless by definition, so there is
nothing to weigh up. **This is hardening, not a confirmed fix for the
report**: the report was not reproduced, so nothing here is known to be the
cause of it.

### Reproduced: the intent stream's ordering guard

The report *was* reproducible. What the harness had been missing was not the
topology -- it built that correctly -- but **how long A played before it
left**, and that turns out to be the whole size of the fault.

`NetSession.HandleSlotIntent` refused any relayed intent whose frame was not
newer than the last one accepted for that slot. Sound, for reordering. But a
client's frame counter starts at zero in `NetSession.StartClient`, so a
player rejoining a match they had been in for five minutes comes back
numbering from 1 while the authority still holds `_lastSlotIntentFrame` =
18000 -- and **every intent they send is refused for the next five minutes**.
The barrier is exactly as tall as their previous session.

That is the asymmetry in the report. The rejoiner's own arrays are fresh, so
they see everyone normally and their shots at other people are resolved from
intents the authority does accept. The authority holds the stale counter, so
the rejoiner's aim and trigger never arrive there, and the rejoiner's puppet
is pinned to wherever the previous occupant was standing -- which is why
nobody can hit them and they cannot hit anyone, on the one machine that
decides either. It clears itself when the counter climbs past the old value,
which from a player's seat looks like "it started working again after a
while", and dying and respawning is the thing they happened to be doing
while they waited.

`run-rejoin.sh 130 60 68` -- A plays for a minute before leaving, so the
barrier outlasts the rest of the run:

| | slot | intents refused *by the authority* | took a hit |
|---|---|---|---|
| before, C (reused slot) | 0 | **1547** | **0** |
| before, D (fresh slot) | 2 | 0 | 9 |
| after, C (reused slot) | 0 | **0** | 2 |
| after, D (fresh slot) | 2 | 0 | 8 |

The hit counts still carry the variance the section above warns about. **The
number that is not noise is the refusal count**, which is now reported by
`-netcheck` as `intents late=` beside the snapshot equivalent, and which went
from 1547 to 0 while the authority's accepted intents went 3591 -> 5452.

The fix is the one the snapshot stream already had: a reset gap.
`IntentResetGap` = 600 frames in `NetSession` and in `DedicatedServer` --
below it a packet is a reordered straggler and is dropped, above it the
sender's counter has restarted and the newcomer is who to believe. Plus
`NetSession.ForgetSlot`, called beside the other two from
`NetSlotManager.Activate`/`Deactivate`, which clears the frame counter, the
last intent and the last snapshot for a slot that has changed hands -- a
vacated slot was keeping its old occupant's intent flagged *valid*, so the
authority went on placing, aiming and firing a player who had left.

## Ping: 20 ms to a server 1 ms away

The scoreboard's ping is a round trip the **server** measures (clients never
exchange packets, so nobody else can measure it for everybody), and it was
measuring far more than the network:

- the client's Pong was sent from `NetSession.Update`, i.e. **on the next
  rendered frame** -- half a frame on average, a whole one at worst, more
  when the frame ran long;
- `NetTransport.ReceiveLoop` polled `Available` and `Thread.Sleep(1)` between
  passes, on **both** machines, and `Sleep(1)` is not a millisecond on
  Windows, where the scheduler tick is 15.6 ms unless something in the
  process has raised the timer resolution;
- the server's own loop sleeps a millisecond between passes.

So ICMP 1 ms, scoreboard 20 ms, and none of the difference was the wire.

Two changes. `ReceiveLoop` now **blocks** on `Receive` with a 500 ms timeout
(there only so shutdown is prompt) instead of polling -- which takes the
arrival delay off the front of *every* packet the session receives, not just
pings. And a client's transport answers Ping on its own thread
(`AnswerPingsImmediately`, opt-in, set in `StartClient`): a Pong echoes an id
and needs no game state, so there is nothing for it to wait for a frame for.

Measured on loopback, where the true round trip is nil: **8-11 ms before,
1 ms after.**
