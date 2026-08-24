# Testing — metrics and interpreting results

This file describes how to read netcheck and maptest results and common traps.

Key lines and their meaning

| Line | Reads as |
|---|---|
| `damage pipeline (resolved here / replayed here)` | the two ends of the damage path. The authority shows `N/0`, everyone else `0/N`. They must match — a shortfall means hits were resolved and never reached the victim |
| `remote position snaps` | visible teleports (bad). Smoothed catch-up is invisible; a snap isn't. Healthy is 0-3 per client per 100 s, worst ones are respawns. A run with rotations in it is not comparable to one without — clients don't finish loading at the same instant |
| `late=N` in the packets line | snapshots that arrived after a newer one and were refused — not loss, these are reordered, and applying them would run health/score/damage backwards |
| `scoreboards agree (within N event)` | N > 1 means the clients are keeping different scores. Clients stop a few seconds apart, so one event of difference is timing |
| `form disagreed ... longest run` | one morph animation's worth is normal. A long run means a puppet is stuck in the wrong form |
| `FAIL: no beam can hurt these players` | `BeamEffectiveness` is all-zero — the player is literally invulnerable (see spawning in `MECHANICS.md`) |
| `untested` | the feature was never performed, so nothing is being claimed — not a pass, and not a fail either; it's a question about the harness |
| `dropped=N` in the packets line | this client couldn't keep up with what it was sent. Non-zero here reads on *other* clients' reports as "they never saw me turn" |
| `pings: slot 0 12 ms ...` | what the server measured per slot, which is what the scoreboard draws |
| `alt-attack` | rising edges on the alt-attack button, both sides — separates "the press never arrived" from "it arrived and laid no bomb" |
| `jumppads 7 (7/7 launched)` | seven in the room, seven tried, seven launched the player standing on them |
| `afflicted freeze 1 burn 2 disrupt 0` | how many players were frozen/burned/disrupted at least once during the run |
| `(probe freeze ok burn nohit disrupt FAIL)` | the affliction probe: `ok` inflicted it, `nohit` the charged shot missed, `FAIL` hit with no state, `n/a` that hunter wasn't in the match |

Common traps

- Comparing raw totals without normalising for join times or rotations.
- Assuming a hit is a hit: afflictions require a charged shot; some weapons require holding `FullCharge * 2` frames.
- Standing at an entity's position expecting its trigger to notice — volumes are relative to entities.
- Writing a button twice in one frame: edges must be computed once, at the end of the frame, from the state at its start — computing them inside each write lets a later clear wipe the edge an earlier press produced (a charged weapon fires on release, so nothing charged ever fired).
- A tolerance of 1.0 silently turns a check into decoration — if a check stops failing, make sure it can still fail.
- Judging a Sylux against a Weavel's abilities: only three hunters lay bombs, only Weavel leaves a halfturret.
- Testing a rotation with only one map in it misses nothing — testing it with only *more than one* map misses the "is this a new match" class of bug, which needs the single-map case too. Test both `maprotation-test.txt` (three maps) and `maprotation-one.txt` (one), each with a two-point goal so a match ends inside a run.
- A run with match restarts in it is not a clean read of the tour: `NetTestScript`'s 15 phases key to the *server's* clock, and a restart resets it, so clients that finish loading a fraction of a second apart land in different phases. Expect `alt form`/`their form stayed wrong for N frames` failures a restart-free run doesn't produce — 181 frames in particular is the correction machinery's full budget working as designed (90 frames grace, a transition attempt, 90 more, then forced), and the check fails at 60, below that budget.
- Reading one client's column of a cross-check as a defect before checking the others: low for *every* observer is systematic (a rate, a sampling difference); low for one is that client's own story, usually a late join.
- Reading `damage pipeline` as healthy just because both ends are non-zero: `25/0` against `0/258` is a byte counter that ran backwards and nearly wrapped forward, not noise on a working pipeline — the three-digit number is the tell.

## Last verified status (2026-08-23)

| Check | Result |
|---|---|
| 3 clients, 130 s, `run-check.sh` | PASS on all three, 0 mismatches, scoreboards agree within 0 events |
| Damage pipeline on that run | 69/56/49 resolved on the authority, 69/56/48-49 replayed on both observers (shortfall = one hit still in flight at cutoff) |
| Match won on points, 3-map rotation, 4 clients | every rotation announced once, all four peers carried across |
| Match won on points, one-map rotation | correctly reloads the same room — the case that used to strand everybody |
| Authority leaving mid-session | promoted to the next peer, session continued |
| `tools/check-dedicated-server.sh` | server and directory both start from a published build, register, and answer |

Last full map pass (2026-08-20):

| Check | Result |
|---|---|
| Every multiplayer map, 8 players / 8 AI bots, 22-24 s each | 33/33, zero crashes, both modes |
| Players placed | 8/8 on 27 maps; 0/8 on the six First Hunt "biodefense chamber" rooms (no spawn points — the only `MAPFAIL`s) |
| Jump pads / teleporters, stood on in turn | every pad on all 24 maps that have one; both of AD1 TRANSFER LOCK's teleporters moved the player |
| Afflictions, probed per map | freeze on 14 maps, disrupt on 13, burn on 6 — a sample, not a verdict: ~2 charged shots per state per map, and the probe's first (uncharged) press of each cycle can land a hit that registers as "hit, no affliction," which understates burn's column most |
| 6 clients against the Pi, 110 s | 3 mismatches, all late-joiner/stop-skew artifacts; pings 6-16 ms, `dropped=0` |
| Pi 3B under 6 clients | server process 11-16% of one core, system 65-75% idle |
| Invulnerable players (`BeamEffectiveness` all-zero) | none |
