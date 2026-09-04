# Testing — the hard cases

`TEST-HARNESS.md` covers the normal run: several real clients playing the
tour and cross-checking what they saw. This file covers the runs where
something is deliberately wrong — a player disconnects, a line goes away, a
ninth player arrives at an eight-slot server, everybody spectates at once,
the Pi is asked to relay twenty matches.

Everything here runs **against the Pi** (`net.livetek.fr`), never a loopback
server. A loopback server has none of the reordering, none of the jitter and
none of the Pi's processor; a result from one must not be reported as a
result about the real thing.

## The two instruments

| Tool | What it is | What it must never be used for |
|---|---|---|
| `-netcheck` (real client) | the whole engine, driven by the tour | nothing — it is the only thing that can say what a player would see |
| `netprobe.py` / `netload.py` (Python) | the wire protocol with no engine behind it | any claim about what a player sees. It measures the **server** |

The Python side exists because several of these questions cannot be asked
with a real client: a Hello claiming protocol 3, a Snapshot from somebody
who is not the authority, a hundred and sixty players from one box. It
mirrors `NetProtocol.cs`, so **a layout change there must be mirrored in
`netproto.py`** or every probe reports a dead server.

## The rig

```
~/mph-net-test/
  netproto.py        the wire format: packets, a virtual client, a status query
  netprobe.py        the edge-case suite -- one subcommand per question
  netload.py         synthetic players, for capacity: one process per game
  udp-blackout.py    a relay that can take the line away and give it back
  udp-lag.py         a relay that holds every datagram for a fixed delay
  hard/common.sh     shared setup: build refresh, leftover kill, host_game,
                     pi_ssh/pi_watch/pi_journal (the Pi, from the Pi)
  hard/pi-sample.py  copied to the Pi: CPU per role, memory, eth0, UDP errors
  hard/run-*.sh      one scenario each
  hard/rotation-ab.sh  the same rotation with two builds, to answer "whose bug"
  hard/run-all.sh    the batch, serialised (they all share one server)
  hard/run-all2.sh   the second half: match boundaries, promotion, capacity
  hard/run-all3.sh   the third: the A/B, and everything the first two got wrong
  hard/report.sh     one screen of what a batch found
```

The Pi's own numbers need `~/.mph-pi-pass` (600) or `MPH_SERVER_PASS`; without
either, `pi_watch` is skipped and the scenario says so rather than reporting a
one-sided run as a whole one.

## The scenarios

| Script | The question |
|---|---|
| `run-capacity.sh` | eleven real clients at an eight-slot server: who gets in, and what the refused ones are told |
| `run-blackout.sh` | one player's line disappears for 1, 3, 8 and 40 s while the others keep playing |
| `run-churn.sh` | players leaving and rejoining mid-match, both by quitting and by vanishing |
| `run-authority.sh` | the authority leaving mid-match, and coming back into a match it no longer runs |
| `run-latency.sh` | 100 / 200 / 300 / 500 ms of round trip, added by `netem` in the kernel — the same line for every client |
| `run-netlag.sh` | one match, a **different** line per client (`-netlag`, inside each process): the report that says "a couple of players had 100-200 ms and stuttered" is a mixture, not a uniform delay, and which of them the server made the authority is most of the answer |
| `run-loss.sh` | 5 / 15 / 30 % packet loss, shaped on both legs, no added latency |
| `run-spectate.sh` | one spectator among players, then every player spectating at once |
| `run-demos.sh` | every client recording the same match, then replaying every file |
| `run-rotation.sh` | a match boundary crossed with real clients: multi-map and single-map |
| `run-fullhouse.sh` | eight real clients, nothing artificial — the baseline the rest is read against |
| `run-pi-limit.sh` | how many matches the Pi will relay: a ramp of hosted games with synthetic players |

`netprobe.py` subcommands: `capacity`, `protocol`, `slotclaim`, `spoof`,
`matchend`, `fuzz`, `names`, `churn`, `idle`, `statusflood`, `masterhost`.

## What the first full run found (2026-08-31/09-01, against the Pi)

Eight faults, every one reproduced on the real server, and three of them
things a player would meet on an ordinary evening:

| Fault | How it showed | Fix |
|---|---|---|
| **Every client crashes when the server changes map** | 3 of 3 clients died in `RoomEntity.DrawRoomParts` at the first rotation; reproduced on the build from *before* this batch, so it is not a regression from it | pooled players keep a `NodeRef` into the room just unloaded; `NetRoomChange.RebuildPlayers` now hands each slot `NodeRef.None` (0 of 3 crashed, 2 rotations followed) |
| **A player whose line drops strobes on every other screen** | 1600-2176 position snaps per client in a run with 52 s of cuts, 0 in every clean run | the position pin is "this player says they are here", which stops being true when they stop saying it: `NetHooks` now skips it once an intent is half a second stale |
| **Spectating never left the spectator's own machine** | 6001 frames spectating, observers saw 0 | the intent had no spectating bit and only the authority's snapshot carries one: `IntentButtons.SpectatingState` |
| **A spectator's own respawn put them back in the match** | observers saw 189 frames of 6001 | `PlayerEntity.Spawn` clears `Flags2` wholesale; `NetHooks` re-asserts the flag every frame |
| **An ex-authority kept simulating after reconnecting** | it ignored every snapshot and played a private match | `PacketType.Authority` only ever promotes, so a client stands down on re-admission and waits to be told again |
| **An authority handover froze every puppet for ~2.7 s** | 159 and 169 snapshots refused in a row as "late" | the new authority's frame counter is its own: the ordering guard re-bases after 12 consecutive older snapshots |
| **A refused player could not tell "full" from "off"** | eight seconds of silence, then a three-way guess | `RefusedPacket` from the server, and a StatusQuery fallback for servers already deployed |
| The scripted tour drove a spectating player | "spectating" tested nothing | gated in `NetHooks` |

And what held up, measured rather than assumed:

| Question | Answer |
|---|---|
| Ninth player at an eight-slot server | refused, slots 0-7 distinct, the eight playing were undisturbed while three refused clients hammered Hello |
| 100 / 200 / 300 / 500 ms of round trip | scoreboards **identical at every level**; snaps 0/0/18/190, mismatches 0/0/1/9/16 -- observation gets coarser, nothing diverges |
| 5 / 15 / 30 % packet loss | no divergence at any level, scoreboards agree exactly; what degrades is hit registration |
| Connection lost for 1, 3, 8 and 40 s | under 30 s the server never notices; at 40 s it times the peer out, promotes, and re-admits it to the same slot within a second of the line returning |
| Every player spectating at once | match, clock and rotation carry on; 0 mismatches; nobody can be hit, which is the point |
| Every client recording a demo | 13 KiB/s each, `dropped=0`, all six files replay, 3.5x compression |
| Match end and rotation | announced once, to the announced map; a one-map rotation reloads correctly (five consecutive matches) |
| 313 malformed / truncated / oversized datagrams | server still answering, joinable and playing; zero exceptions in its journal all session |
| A browser polling at 233 queries/s | the player's ping went 12 ms -> 16 ms |
| Eight real clients, 220 s | 0 mismatches, 0 snaps, 2 ms pings, Pi at 25 % of one core, no UDP errors |
| The directory's hosted games | 20 (its configured range), all serving, the 21st refused with a reason |

## What the Pi will hold (2026-09-01)

A ramp of hosted games, eight synthetic players each, every one at a full
60 Hz, with the box sampled from inside (`hard/pi-sample.py`):

| Games | Players in | Delivery | An idle poller's round trip | Pi CPU (400% = all four cores) | The directory process | UDP errors |
|---:|---:|---:|---:|---:|---:|---:|
| 1 | 8/8 | 100% | 21 ms | 15.7% | 55% | 0 |
| 4 | 32/32 | 99.8% | 29 ms | 45.0% | 150% | 0 |
| 8 | 64/64 | 99.8% | 54 ms | 53.5% | 187% | 0 |
| 12 | 96/96 | 99.3% | 81 ms | 65.3% | 231% | 0 |
| 16 | 128/128 | 99.0% | 103 ms | 77.2% | 283% | 0 |
| **20** | **160/160** | 98.8% | 125 ms | 86.9% | 331% | 0 |
| 24 | 160/192 | 98.7% | 124 ms | 87.4% | 332% | 0 |
| 32 | 160/256 | 98.7% | 102 ms | 87.5% | 330% | 0 |

**Twenty matches, a hundred and sixty players.** Past that the numbers stop
moving: the directory process plateaus at ~330% of the four cores and the
joined count sticks at 160, because the limit arrives as *new players cannot
get in* rather than as anything going wrong for the players already in --
three whole games at the 24-game step never got a single player, each of
their clients having sent twelve Hellos over six seconds to no answer, while
everyone already playing kept 98.7% of their snapshots and the box logged not
one UDP error.

What it costs before that: one full-rate eight-player match is about 55% of
one core, and **concurrency costs more than throughput does** -- at a
constant 1.7 MB/s outbound, going from four games to eight pushed an idle
poller's round trip from 29 ms to 54 ms, because each game is two more
threads inside one process.

Two honest limits on the table. Above four games the load generator could not
offer a full 60 Hz (this box tops out near 2000 datagrams a second to a
remote host -- see the `sendto` trap below), so the higher rows are the Pi
holding *more matches* at a *fixed* total traffic, not more traffic. And the
players are synthetic: this measures the relay, and says nothing about
whether a person would have enjoyed the game.

## Traps this batch walked into

- **A pipeline is a subshell.** `refresh_build | tee summary.txt` set `GAME`
  in the subshell and nothing in the caller, and with `set -u` every client
  died on the spot — reported as eleven clients failing to *join*. Anything
  a script needs afterwards has to be assigned outside a pipe.
- **A leftover client from an earlier run is another player in the match**,
  and an unexplained one: two different players called SMOKEA in one report
  cost half an hour. `kill_leftovers` runs before every scenario now.
- **A probe that answers pings is not idle.** The server takes a Pong as a
  sign of life, so the "silent client" check watched a client that was
  quietly staying alive. `Client.auto_pong = False` is what makes it look
  like a line that has gone away.
- **A flood measured from the process that sends it measures that process.**
  The first status-flood run reported the player's round trip going from
  ~15 ms to 242 ms; the sender had starved its own receive loop. The number
  that decides it is the server's own measurement, off the roster it
  broadcasts once a second, with the flood in a process of its own.
- **A relay written in Python is the experiment, not the instrument.**
  `udp-lag.py` asked for 100 ms with five clients behind it and produced
  round trips of 1767-1859 ms while losing 40% of the traffic: a
  single-threaded forwarder cannot move ~7000 datagrams a second. Latency and
  loss are now shaped by the kernel (`netem` on `eth0`, and on an `ifb`
  device fed by an ingress redirect for the return leg), which is exact and
  free. `modprobe ifb numifbs=1` succeeds on this kernel and creates no
  device -- `ip link add ifb0 type ifb` is what makes one, and without it the
  whole chain silently falls back to shaping one direction.
- **`sendto` costs 3.9 ms on a socket that is also receiving**, through this
  box's WSL NAT -- against 0.019 ms to a loopback server with the same code.
  It is a blocking cost, so one thread doing eight sends a frame tops out at
  31 Hz however idle the processor is, and the first capacity ramp therefore
  offered the Pi a third of the load it claimed to. `netload.py` gives every
  virtual client its own sender thread and reaches the rate asked for; it
  prints the rate it actually achieved, and the ramp says so per step, because
  a load test that quietly under-loads reads exactly like a server with
  headroom.
- **The session's clock is frames, not seconds.** `NetSession` is driven by
  `Scene._globalElapsedTime`, which advances one frame's worth per frame, so
  every "second" it measures is a sixtieth of a frame count. Five real
  clients on one box render at about 24 fps, and a 40-second outage was
  reported as 15.9 "seconds" of silence. The netcheck report now prints the
  client's real frame rate and converts.
- **A hosted game the directory started is torn down if the same address
  asks for another one while it is still empty** (`StartHosted`: one game per
  asker, replaced only when empty). A ramp that asks for twenty games has to
  put a player in each one before asking for the next, or it ends up with
  one.
- **A game hosted with mode 0 is `GameMode.None`, not Battle.** `SingleMatch`
  substitutes Battle, but the request should say what it means.
