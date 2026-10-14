# Server authority and hosted rooms

Current Native C++ implementation on `develop12_homing`, updated 2026-10-14.
The C# sources are historical reference; the paths below are relative to
`src/MphRead.Native/`.

## The role split

Only the dedicated server is the simulation authority. It runs the match,
resolves damage and pickups, owns match progression, and publishes snapshots.
Every human player is a normal client: input, local movement, firing, hit
prediction and drawing run on that player's machine.

| Launcher choice | Match simulation | Creator / first participant |
|---|---|---|
| Dedicated server | dedicated server process | normal client |
| Hosted lobby, local or online | dedicated server process for that room | normal client; lobby ownership does not grant authority |

Starting a server on the player's own PC does not change those roles. That
client still exchanges UDP with the server. Its loopback latency may be very
small, but it has no special simulation or damage privilege. The same
rewind, prediction and claim paths apply to it as to other players.

`NetSession::IsAuthority()` requires `NetRole::Server` and server authority
state. Clients ignore legacy `PacketType::Authority` grants. A client cannot
publish a snapshot or end the match by sending `MatchEnd`. Leaving the room
does not migrate authority to another participant.

## Shooter priority is retained

Server authority does not mean waiting for the server's projectile to hit.
The owner fires and predicts hits immediately. Its hit claim tells the server
what landed on its screen, including hits the server's own replay missed and
shots it did not replay because the shooter died during the round trip.
The server validates and settles that claim; its own matching hit is not a
prerequisite. Existing identity/history, damage-limit and duplicate checks
remain in place.

Shots fired before the shooter was down still count when their claims arrive
later. Arrival order does not decide a trade. `NetworkCombat` exercises both
arrival orders and delayed claims. See [NETWORK-PREDICTION.md](NETWORK-PREDICTION.md)
and [NETWORK-HITCLAIMS.md](NETWORK-HITCLAIMS.md).

Movement still uses the owner's reported position and intents. This change
does not replace that path with a new input-only movement simulation.

## One room per process

`NetSession` remains static. `GameState`, players and gameplay histories also
hold process-wide state, so running multiple authoritative rooms as threads
inside one process would mix worlds. Hosted rooms now use separate dedicated
server processes; this is not an instance-based session refactor.

One physical server can run many rooms. Its directory or parent server
allocates a port and starts one child process per room. Each child has its
own session, world, history and memory, and runs one authoritative match.

| Piece | Responsibility |
|---|---|
| `Mods/Network/Server/Core/DedicatedServerProcess` | shared process launcher, unique rotation file, exact argument transport, socket readiness, stop/detach |
| `Mods/Network/Server/Core/HostPool` | port allocation, room-process ownership, status probes and empty-room cleanup |
| `Mods/Network/Discovery/NetMaster` | directory requests use the same HostPool instead of another threaded room implementation |
| `Mods/Network/Integration/Launch/NetHostSession` | local create-and-join starts a child server and joins it as a normal client |
| `Mods/Network/Server/Core/LocalServer` | the launcher's Dedicated path shares the process-launch implementation |
| `Mods/Network/Server/Core/ServerLaunchOptions` | applies lobby policy, format, ready/join settings and owner token in the child |

The lobby owner's token grants lobby control only. It survives the process
argument boundary with the same sixteen wire bytes. The creator may edit and
start the lobby, while the child server runs the match after the load barrier.
Child startup has a 30-second limit; GUI/CLI room requests allow 35 seconds
so a slow room load is not abandoned by the client first.

A locally created room detaches from its creator when the creator leaves;
the server keeps running. Online rooms stay owned by their directory, which
reaps them after the configured empty-room grace period or when the child
exits. A directory shutting down stops the rooms it owns. Every room needs
game files, and each process consumes its own simulation memory.

## Headless simulation and wire contract

`Mods/Network/Server/Match/ServerSim` runs the real scene simulation at 60 Hz.
`Mods/Headless` suppresses work whose output is rendering/audio; synthetic
keyboard/mouse states satisfy the scene's input interface without a window.
The server has `LocalSlot = -1`, so no human slot gets a local-player exemption.
The roster and match state are applied to the simulation through the same
session paths used by clients; rotation remains a normal room transition.

Important headless invariants:

- Slot admission/setup must allow server `LocalSlot = -1`.
- The server uses a roaming camera; it must not treat slot 0 as the main player
  or run that player's multiplayer intro camera/input blocks.
- Simulation-owned poses and collision must advance without a draw pass.
- Headless fade/effect timers must advance so room transitions finish.
- Idle authoritative rooms still owe a 60 Hz simulation step. A server log's
  scene room must follow map rotation, rather than only its advertised room.

`RunsTheMatch` defaults to true. Production hosting no longer disables it.
The remaining false setting is confined to protocol/diagnostic fixtures and
cannot grant a client authority. `-simulate` and `-authority` are accepted as
compatibility flags and do nothing. Game servers need assets; they do not
fall back to client-authority relaying when assets are missing.

The current Native wire version is **33**. Homing shots carry firing-time
variant/role data, and snapshots carry the Prime holder. Role snapshot frames
are separate from interpolated target acknowledgements. Both ends must use
the same version; legacy authority grants are not a compatibility fallback.
See [NETWORK-HOMING.md](NETWORK-HOMING.md) and [SERVER.md](../../SERVER.md).

## Validation

Windows MSVC Release and CTest cover the Native implementation.
`FruityPrime -hostauthoritycheck [ROOM]` verifies local create-and-join,
normal client prediction/claim eligibility, exact owner-token transport,
online lobby options, two simultaneous isolated rooms, and continued server
snapshots after creator departure. CTest calls this `HostedAuthority`.

`tools/testing/run-homing-net-matrix-all.ps1` runs real loopback UDP cells
with 2/8 clients, bots, RTT, jitter, loss, reorder, Prime and morphing. Hosted
cells request a room from a real directory/parent server, then verify a
separate authoritative child and `authority=False` on all clients. Logs are
isolated per process so normal log retention cannot remove another cell's
evidence. This is not a new Pi, remote-network or physical-GPU measurement.

The current gate results and their limits are recorded in
[the validation report](../../docs/todo/Fruity-Prime-Homing-Audit-Validation-2026-10-14.md).

## Historical C# measurements

The following measurements and relay comparisons describe earlier C# builds.
They are retained as historical evidence, not as current Native C++ memory,
performance or platform acceptance, and not as supported hosting modes.

## Cost, measured

`-simcheck "ROOM" [-players N] [-seconds N]` runs the headless simulation on
its own, with nobody connected and synthetic intents, and reports what a room
costs in memory and in milliseconds a step. It is not a network test --
`run-check.sh` is that -- it measures the part that used to run on a player's
PC and now has to run on a server.

Every saving below is work whose only output was a picture:

| Cut | Where | Peak RSS |
|---|---|---|
| (a full client, `-maptest`, 8 players, for comparison) | | **337 MB** |
| GL context, shaders, display lists, texture upload | `InitTextures`, `GenerateLists`, `OnLoad` | 183 MB |
| the SFX bank (every sample in the game, decoded) | `Sfx.Load` takes the stub `ThumbnailMode` uses | *included above* |
| texture **pixel data**, never decoded because nothing binds it | `Read.cs`, empty `TextureData` per texture | 118 MB |
| render instructions -- the geometry of the picture. The simulation collides against the room's *collision* file and never against its render meshes | `Read.cs`, empty list per display list | **110 MB** |

MP3 PROVING GROUND, 8 players, x86-64, Debug: **110 MB peak, 0.31 ms mean
step, 2% of the 16.7 ms budget**, load 0.5 s. The .NET runtime alone is 50 MB
of that, so the room and eight players cost about 60 MB.

MP1 SANCTORUS, 8 players, on the Pi 3B (ARM64, Release, self-contained single
file): **131 MB peak, 8.04 ms mean step -- 48% of the budget**, load 11.9 s,
20 s of match simulated in 9.7 s of wall clock. It fits, with the Pi showing
225 MB still available afterwards, and it is the machine that decides the
ceiling: the same room costs a fifteenth of the budget on a desktop.

That 48% is the **worst** case and reads worse than the machine is. It is
eight players all firing on the same schedule, which is what the synthetic
intents do; a real three-player match on the same Pi ran at **0.90 ms a step**
after ten minutes, and held **exactly 1800 steps per 30 seconds** the whole
way. The `-simcheck` mean is also dragged up by its first step (1252 ms once,
540 ms another time), which is the JIT compiling the entity path rather than
the simulation -- five steps in twelve hundred overran at all. Publishing the
ARM64 server with ReadyToRun would take most of that and a good part of the
11.9 s load with it, and has not been tried.

**A room load blocks the relay**, because the sim is stepped from the server's
own loop. Measured on the Pi: about **4 seconds** for a reload with the file
cache warm (the 11.9 s figure is a cold process, JIT included), against the
30 s `NetConfig.TimeoutSeconds` that would start dropping peers. It happens
twice in a session's life -- once when the first player joins an empty server,
because `_matchId++` there is a deliberate clean restart, and once per
rotation -- and both are moments every client is loading the same room anyway,
so most of it overlaps with a wait they are already having. It is fine as it
stands; moving the sim to a thread of its own is the answer if it ever is not,
at the price of putting a thread boundary between the sim and the peer table.

The counts are preserved where they are dropped -- one empty `TextureData` per
`Texture`, one empty instruction list per display list -- because `Recolor`
asserts one entry per texture and `mesh.DlistId` indexes the lists by
position.

## Measured against the relay, on the Pi

Three scripted clients, 150 s, MP1 SANCTORUS on `net.livetek.fr`, with 150 ms
injected on two of the three (`run-remote-lag.sh`). The same scenario twice,
the only difference being where the simulation lives.

| | relay (a client is the authority) | server authority |
|---|---|---|
| cross-client mismatches | **1** | **0** |
| scoreboards | agree within 0 events | agree within 0 events |
| remote position snaps | 0 | 0 |
| per-client verdict | 3 PASS | 2 PASS, 1 FAIL -- see below |

A separate run, two clients over three one-minute matches on the Pi: **two
rotations followed, 0 mismatches**, the server's own log naming each new room
as it reached it, and 1800 steps per 30 seconds held straight through both
room changes.

The relay run's mismatch is `CHARLIE shooting: they did 2578, ALPHA saw 557` --
the authority seeing a fifth of a laggy player's trigger frames. It is gone
under server authority.

**The one FAIL is the interesting result and it is not a regression.** ALPHA
reported *"their form stayed wrong for 78 frames in a row -- authority wanted
biped, puppet alt/Morph/ended"*. In the relay run ALPHA does not report it
because ALPHA **is** the authority, and `NetFeatureCheck` skips this check on
the authority: it is the one machine with nothing to compare against.
At the time of this run, `NetPlayerBridge` reconciled a puppet's form against
`IntentButtons.AltFormState` only on the authority. Clients replayed the morph
press but had no snapshot-based correction.

So the refactor did not create this. It removed slot 0's exemption from it:
what ALPHA reported is what the other seven players had already seen. The
current bridge reconciles the owner's form state on the authority and
the authority's snapshot on each client. The transition-aware guard lets a
puppet finish morphing while an older state is still in flight, then corrects
a lasting mismatch. This 78-frame result predates that change; a restart-free
latency run is still needed to measure it in play. See
`NETWORK-DIAGNOSTICS.md` and `.claude/KNOWN-GAPS.md`.
