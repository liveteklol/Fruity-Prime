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
