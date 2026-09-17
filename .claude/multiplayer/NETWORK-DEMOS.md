# Replay system

New recordings and instant clips use `.fpdemo` v3. Version 2 remains readable;
its binary layout is unchanged. The viewer still feeds the recorded packets
through `DemoPlayback`, `NetSession.StartPlayback`, `InjectPlaybackPacket`, and
the normal session handlers. Local intent and authority snapshot synthesis are
preserved. Protocol mismatch is refused before packet playback.

## Controls and clock

`ReplayController` schedules complete 1/60-second engine steps. Rates are
0.25, 0.5, 1, 2 and 4; neither physics constants nor recorded frame numbers are
scaled. Paused and ended replays run camera/input presentation only. Particle
and fade timers are stepped for every replay simulation frame, including seek
batches. The final frame remains visible and restart/exit stay available.

The replay packet clock also advances while the session is waiting for a match
to start, so a recorded `InMatch` packet can release the load barrier. Gameplay
remains frozen during these steps; this needs no live socket or load acknowledgement.

Space plays/pauses; period steps forward; comma steps backward; brackets change
speed; arrows seek five seconds; Home restarts; Tab displays the scoreboard.
F toggles free camera, C selects chase, O selects orbit, 1-8 selects a player,
and mouse buttons cycle players. The replay pause menu exposes the timeline,
rate/camera/player controls, event filter/navigation, clip In/Out, and export.
Gamepad input uses the existing abstraction; Android uses the shared menu and
its scene-reconstruction path. B/N save/preview camera keyframes. Up to 64
frame-indexed keys persist in a checksummed `.fpdemo.camera` sidecar, atomically
replaced and bound to the replay's size and modification time. Position/FOV
interpolate linearly; orientation uses quaternion interpolation. Optional
look-at targets follow a player while that player is spawned. Keys survive
seek/restart. The shared desktop/Android menu can save, preview or remove a
key at the current frame, and explicitly enable track playback.

Faithful is the default camera profile; Presentation optionally smooths the
chase camera and enables camera tracks. Neither profile changes packets,
entity state, simulation timing or network smoothing. The optional director
changes focus on kill/objective annotations only. Camera tracks are bounded
presentation tools, without spline editing or collision avoidance along
interpolated free-camera paths.

The replay HUD shows time, duration, rate, watched player/camera, event marks,
and stopped/error state; it dims after inactivity. Presentation is suppressed
and audio is muted/stopped during fast-forward seek batches.

## V3 storage and safety

`ReplayFormatV3.cs` contains a bounded binary reader/writer with no new package.
The uncompressed dispatch prefix remains `FPDM`, format byte, protocol byte.
It is followed by length/CRC-protected metadata: tick rate, build identity,
UTC date, full-match/clip type, room, mode, content hash, roster, and bootstrap.
Bootstrap packets reuse SessionState, MatchState, Roster and Snapshot structs.
The initial scene therefore needs no forward packet search or reopen/rewind.
V2 retains its old bounded match/roster search and duration scan/cache.

Packet chunks normally cover 120 simulation frames and are independently
Deflated. Each has first/last frame, record count, compressed/raw lengths and
CRC32. The footer contains the duration, chunk index and event index; a fixed
trailer locates it. A metadata-only library read retains neither the packet
stream nor all event/chunk entries. A checksum-valid header/footer means
integrity **Unknown**, not Healthy; Healthy requires a full chunk validation.

Limits include 1024 bytes per metadata string, eight players, 32 bootstrap
packets, 128 KiB metadata, 2 MiB raw chunks, 16 MiB footer, 200,000 chunk/event
entries, and seven days of frame numbers. V3 packet size is the current wire
limit (1024 bytes). V2 retains its UInt16 packet size. Index extents, frame
order, record counts, compressed/decompressed lengths and checksums are checked
before data is accepted. Corruption and partial records are explicit results,
not ordinary EOF. A v2 Deflate stream has no checksum/footer; a truncated stream
that happens to end on a complete record cannot always be identified.

Full recordings write `filename.fpdemo.part` with complete chunks flushed to
disk. Successful close writes footer/trailer, closes the file, then renames it
without replacing an existing replay. I/O errors stop recording without
crashing the match and preserve recoverable data. Recovery creates a separate
marked file containing only complete CRC-valid chunks; it retains the source.
A full-match recording closes at match/map rotation so the next match cannot
inherit the old map hash/bootstrap.

Map identity hashes actual room model, collision, entities, node data,
animation and texture files. Missing/different identified map content refuses
playback. Build identity is shown separately from protocol compatibility.
No protocol conversion or silent interpretation of incompatible packet structs
is performed.

## Recording, clips and annotations

`DemoClip` uses 64 KiB pooled packet pages with a bootstrap captured before the
page's first packet. It retains 15/30/60/120 seconds (30 by default), with a
24 MiB bound that also accounts for record descriptors and bootstrap overhead.
A frame-clock stall or a flood of tiny packets cannot bypass the memory bound.
Windows are page-aligned, so duration can include up to about a second of slack.
Post-roll options are 0/2/3/5 seconds, default 3. A second save finishes the
pending clip and creates a distinct new request; it cannot overwrite the first.
Disconnect during post-roll saves the available part. Keyboard, controller-menu
and touch-menu paths call the same save operation.

Events are annotations and never drive simulation. Roster, match, accepted
snapshot and objective scoring hooks annotate joins/leaves, spawn/death/kill,
damage, score, objectives and match boundaries. Source-replay extraction copies
packets and events, rebases frames, and creates a packet bootstrap at the In
point. It never re-records engine output.

The Replays library displays v3 metadata and offers watch, display rename,
favorite, delete, export, folder reveal on desktop, and `.part` recovery.
Display names/favorites are sidecars. Imported files stay in place. Desktop
export uses the app's exports folder rather than the headless Avalonia native
storage provider; Android uses its platform document provider.

## Reset, seek and verification

Seeking destroys/reconstructs the scene, restores bootstrap, and runs every
packet/simulation frame from zero to the requested frame in bounded batches.
It never jumps a reader offset into a live world. Replay session startup resets
both RNG streams and the otherwise process-global item rotation seed. Playback
uses a socket-free transport; connection-control packets cannot assign a local
slot, promote authority, or terminate the spectator session. Normal recorded
bursts are retained; pathological single-frame queues fail explicitly at 65,536
packets or 32 MiB instead of silently dropping packets.

Commands:

- `-replaycontrolcheck`: rate arithmetic, pause, step, resume and presentation-rate independence.
- `-replayformatcheck`: v2/v3 records, metadata, protocol rejection, malformed lengths/CRC,
  recovery, extraction/rebased events, packet bursts, socket isolation and stalled clip-buffer bounds.
- `-replayvalidate FILE`: complete integrity scan.
- `-replayrecover FILE`: recover complete chunks into a new replay.
- `-replaydeterminism FILE`: real engine linear/reconstructed seek comparisons and
  every-frame gameplay comparisons at all playback rates; needs extracted game assets.
- `-replaydeterminism FILE -replayhashout OUTPUT.fpdemo`: after all comparisons pass,
  creates a separate v3 copy with expected gameplay hashes every 300 frames and at
  EOF. The source and packet contents are preserved. A matching engine build/hash
  schema verifies these references during playback and stops explicitly on a mismatch.
  Different builds/schemas report that reference verification is skipped.
- `-demoinfo FILE -replay`: original snapshot/intent distribution diagnostic, now
  also reports corruption. Network bursts already present in a source recording
  can still fail its historical burst/gap threshold.

Verification for this branch included a 22-second two-player stock-map
recording and 32-second four-player TEST ARENA recordings. All four arena
clients passed their gameplay harness and produced distinct instant clips.
Real engine comparisons passed on a combat recording and an instant clip at
sampled targets, all replay rates, and frozen EOF. The comparison hashes scene
and entity/player scalar state, positions, match score/timers and packet counts;
it is a replay-versus-replay check, not a claim of identical live-client state.
The verifier also writes a temporary disk-backed SHA-256 trace of an explicit
gameplay projection (player transforms, health, form/weapon/spawn state, scores,
match timers and flag/node objectives). It compares every complete simulation
frame inside seek/rate batches, reporting the first differing gameplay frame.
Camera/render/audio state is excluded. The optional v3 expected-hash footer stores
the hash schema and reference engine build separately from the recording build.
These hashes are produced offline from the replay baseline: a live local player's
prediction is not a valid expected state for a replay puppet. Source clips and
recovered files intentionally require their own reference pass.
Desktop and dedicated-server builds were clean. Android builds passed with
existing binding/XML warnings; device interaction remains unverified.

## Remaining roadmap work

This is not the complete P0-P3 roadmap. In particular:

- Full-world checkpoint DTO capture/restore and checkpoint-assisted indexed seeks
  are absent. Current seeking replays from frame zero and can be slow on long matches.
- Reference hashes require the offline verification command; live recording does
  not run a second engine alongside gameplay. The explicit gameplay hash is not
  a complete serialization of all hidden simulation state.
- Packet bootstraps capture packet-visible match/player state, not every live
  projectile, pickup, flag or other world-entity timer at a mid-match clip start.
  The broad mode/map/network-loss/8-player matrix and clip-versus-source world
  equivalence still need validation before declaring checkpoint/clip fidelity complete.
- Camera tracks provide bounded persistent keys and frame interpolation, without
  spline editing or interpolated-path collision avoidance. Faithful/Presentation
  profiles affect cameras only. The director follows annotated events; it does
  not implement the proposed proximity/damage/lead-change scoring model.
- Server-side canonical replay capture is absent. Client recordings remain the source.

## V2 implementation history

The notes below preserve the reasons for the original packet-stream design.
They describe the v2 implementation before the controls and v3 changes above.
# Demos: recording a match and watching it back

`Mods/Network/DemoRecorder.cs`, `DemoFile.cs`, `DemoPlayback.cs`,
`DemoInfo.cs`. Started and stopped from the pause menu ("Record demo",
online matches only) and from `-netcheck ... -recorddemo`; watched from the
front screen's demo entry, which runs `MatchStart.LaunchDemo`.

## The design, in one line

A demo is **every packet this client received, verbatim**, replayed into the
same `NetSession` on the same frame it originally arrived on. Nothing is
re-encoded, so every packet-type handler, room transition and match-end
sequence runs unchanged during playback; the player decides only *when* a
packet is handed over, never what it means.

That decision has consequences worth knowing before touching any of it.

## Two things a demo has to synthesize

A client does not receive everything it knows. Two holes, both filled by
writing the packet this machine was about to send in the shape it would have
arrived in:

| Hole | Why | Filled by |
|---|---|---|
| This player's own input | the server never relays your `SlotIntent` back to you; you already know what you pressed | `DemoRecorder.RecordOwnIntent`, from `NetSession.SendIntent` |
| The authority's own snapshot | `DedicatedServer.HandleSnapshot` forwards to every peer **except the sender** | `DemoRecorder.RecordOwnSnapshot`, from `NetSession.BroadcastSnapshot` |

The second one was missing and it mattered more than it sounds. The
authority is whichever client connected first, which is normally whoever set
the match up, so it is the common case rather than a corner one. Measured on
the harness: an authority recording for 30 s **received 31 snapshots** (the
once-a-second `NotifyAuthority` echo) while sending 1800. And the snapshot is
not one stream among several -- it is the only carrier of health, score, the
damage sequence and the spawn flag, and `NetPlayerBridge.ApplyState` is the
only thing during playback that ever calls `ModNetSpawn`. So the host's demo
did not look thin, it opened on **an empty room**: nobody was ever placed,
nothing was ever hit, no score ever moved.

## Frames, not milliseconds

Format version 1 stamped each record with `Environment.TickCount64` and the
player released them against a `Stopwatch`. Three separate faults, all
visible as the same complaint -- the replay is choppy and drops things:

- **The engine's clock is not the wall clock.** `Renderer` advances the
  simulation by a fixed 1/60 s per frame however long the frame took, so a
  replay at 58 fps consumed 60 frames of recording every 60 frames, fell
  behind real time, and caught up in bursts. Only the newest of a burst
  survives: `RemoteStates` and `RemoteIntents` are one slot each.
- **`TickCount64` ticks every 15.6 ms on Windows.** A 60 Hz stream stamped on
  a 64 Hz clock clumps and drifts against the frame boundaries, producing the
  same bursts on a machine holding a perfect 60 fps.
- **The room loads after the stopwatch starts.** `DemoPlayback.Join` returns,
  `renderer.AddRoom` takes seconds, nothing is pumped, and the first frame
  afterwards released all of it at once -- so the replay opened several
  seconds in with everything between discarded.

Version 2 stamps `NetSession.NetFrame - startFrame` and `PumpFrame` releases
one frame's worth per simulated frame. None of the three can happen: the
recorder counts the frames the simulation counts, and a load in the middle
costs nothing because no time passes.

Measured with `-demoinfo FILE -replay` on a real 27 s recording:
**99.5% of replayed frames got exactly one fresh snapshot, 1 frame in 1499
got more than one, longest run with none: 6** (a hitch that was in the
recording, faithfully reproduced).

`Join` also stopped waiting on a clock. It reads records until the match
state is known plus a 120-frame grace for the roster, which is a few hundred
frames of parsing rather than the up-to-8-second wall-clock wait it was.

### And then it rewinds

The search is not free: it hands its records to the session to be acted on,
and there is no scene yet to act on them. So the first one to three seconds
of every recording were parsed and thrown away, and the replay opened that
far in. Reported as **"the first shot isn't in the demo"** -- a charged
missile fired right after pressing record. It was in the demo; it was never
played.

`DemoPlayback.Rewind` reopens the file at frame 0 once the room key is known,
and `NetSession.RewindPlayback` clears the bookkeeping that would otherwise
refuse the rewound packets as stale -- `_lastSnapshotFrame`,
`_lastSlotIntentFrame`, the bridge and damage baselines -- while **keeping**
what the search was for: `ServerMatch`, `SlotOccupied`, `SlotHunter`,
`GameState.Nicknames`, all of which `NetLaunch.BuildPlayers` reads on the
next line. Re-delivering the same `MatchState` is a no-op (`HandleMatchState`
only raises `MapChanged` when the room key differs), so nothing reloads.

Measured on a 25 s authority recording, frames 1-1500 with 1410 SlotIntent
records:

| | frames replayed | intents applied |
|---|---|---|
| before | 1499 of 1620 | 1496 of 1666 |
| after | **1501 of 1500** | **1410 of 1410** |

Every intent in the file now reaches the simulation, and 99.9% of frames get
exactly one snapshot with no frame taking two.

## The file

```
"FPDM" | version (2) | protocol      <- 6 bytes, never compressed
--- deflate ---
[frame delta: 1 byte, 0xFF = escape + uint32] [length: uint16] [packet bytes]
...
```

Deflated because the stream is 60 snapshots a second whose neighbours differ
in a few floats. Flushed every 15 frames rather than per record: a sync flush
costs 14% at one per record and a fraction of a percent at this rate, and a
quarter of a second is what a demo that dies with the game loses.

Measured on the harness, 2 players:

| | v1 rules | v2 |
|---|---|---|
| non-authority, 27 s | ~381 KiB (14.1 KiB/s) | **76.6 KiB** (2.8 KiB/s), 4.7x |
| authority, 30 s | ~138 KiB *and no snapshots* | **68.6 KiB** (2.3 KiB/s), 5.5x |

So the authority's demo became correct -- 1831 snapshots instead of 31 -- and
still came out at half the size of the broken one.

**Version 1 files are refused, not read.** Their timestamps mean something
else and their body is not compressed, so there is nothing that could read
one by accident.

## Checking a demo

```bash
MphRead -demoinfo "path/to/x.fpdemo"            # what is in it
MphRead -demoinfo "path/to/x.fpdemo" -replay    # and how it lands, frame by frame
~/mph-net-test/run-demo.sh 30 authority         # record one, then do both
```

`-demoinfo` needs no game files, no window and no server. Read it in this
order: the `Snapshot` row (none means an empty room -- it says so), then the
`KiB/s`, then, with `-replay`, the percentage of frames that got a snapshot.
Exit code 1 for a demo with no snapshots, or a replay where more than 5% of
frames took a burst or a gap ran past 10 frames.

Note the working directory: `ConsoleSetup.Run` does
`Directory.SetCurrentDirectory(BaseDirectory)`, so a relative path is
relative to the binary, not to the shell. Pass an absolute one.

## Gotchas

- **`LocalSlot` stays -1 for the whole playback session**, on purpose
  (`NetSession.StartPlayback`). There is no local player, so every slot is a
  puppet driven by the recorded intent stream, and `NetHooks.LocalSlot`
  returns -1 rather than falling through to 0 the way "connected, Welcome
  hasn't landed" does.
- **The viewer is a spectator and cannot be anything else.** `Renderer`
  starts `SpectatorMode` on the first frame anyone is available; F
  toggles a free camera on top of it.
- **Nothing spawns without a snapshot.** `NetHooks.ForceSpawn` returns false
  during playback -- neither host nor authority -- so placement comes only
  from `ApplyState`'s `FlagSpawned` branch. This is why the missing authority
  snapshots produced an empty room rather than a degraded one.
- **A demo recorded on a listen host (`NetSession.StartHost`) has no
  `MatchState` in it** and so cannot be played back: `Join` needs a room key
  and only a dedicated server sends one. Every path the launcher offers goes
  through a dedicated server, in-process or otherwise, so this is a note
  rather than a bug.
