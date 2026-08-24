# Testing — test harness

This document explains the netcheck, maptest and the harness scripts used in `~/mph-net-test`.

Philosophy

- `-netcheck` runs the real client in a hidden window; using fake packets or stepping the network without the engine can miss failures where clients have disconnected scenes.
- `NetTestScript` replaces AI with a fixed tour (15 phases) keyed to the server's clock so all clients are in the same phase simultaneously.

How to run

```bash
cd ~/mph-net-test
./run-check.sh 150 Samus Weavel Sylux Trace Samus Noxus   # seconds, then hunters
```

What the harness records

- Every client records what it DID and what it SAW. `compare-reports.py` cross-checks them; an action claimed by one client must show up in others' observations.

Map sweeps and probes

- `-maptest "ROOM" -players 8 -seconds 22` loads a room with eight players (a
  different hunter per slot), drives them through the tour, and prints an
  inventory: spawns, jump pads, teleporters, doors, afflictions, deaths.
  `./run-maps.sh 8 22` in `~/mph-net-test` sweeps every room (~15 min for 33);
  `grep MAPCRASH` / `grep MAPFAIL` the log.
- `-maptest -bots` runs the same rooms with **AI bots** instead of the
  scripted tour — a different code path (the tour writes `Controls` directly
  and never touches behaviour trees), and the only one that reaches bugs only
  `PlayerAi` triggers. This is how the slot-capacity work (`SlotCapacity` to
  8) got finished: raising it is not one constant, every array indexed by a
  player slot has to grow with it, and several only crashed under bots —
  `GameState.BeamKills[4,9]`, `TeleporterEntity._triggeredSlots`,
  `AreaVolumeEntity._triggeredSlots`/`_cooldownSlots`/`_prioritySlots`,
  `PlayerAi._slotHits`/`_slotDamage`/`captureList`,
  `PlayerAi._playerVisibility` (`bool[4,4]`), `PlayerAi._globalObjs`. If the
  capacity is raised again, grep for `[4]` and `[player.SlotIndex]` before
  trusting it.
- **The world probe.** Counting jump pads answers whether a map contains one,
  not whether it works. After the tour ends, the audit teleports a player
  into every jump pad's and teleporter's trigger volume in turn, holds it
  twelve frames, and watches for the event via `Mods.WorldEvents`. Aim at the
  volume's centre (`JumpPadEntity.ModVolume`), not the entity's own position —
  several pads carry their volume beside or above the model, and standing at
  the entity stands the player next to the box, not in it. A pad that stays
  silent is retried higher and in alt form (some ignore bipeds) before being
  called silent; three of MP6 HEADSHOT's eight pads were wrongly reported dead
  for a fortnight on the strength of the first mistake. A map where *every*
  pad stays silent is `MAPFAIL`; one silent pad is not (some are meant to be
  dropped onto from above).
- **The affliction probe.** Freeze/burn/disrupt are states the plain tour can
  never reach, because three things must be true at once: (1) the hunter must
  hold its *own* affinity weapon (`beam + 9`), which the probe issues via
  `ModArmAffinityWeapon` since in a match it's a pickup, not a given; (2) the
  shot must be **charged** — every affliction sits on the charged entry, and a
  weapon without `PartialCharge` only counts as charged at `FullCharge * 2`
  (120 frames for the Judicator), so the probe holds until the weapon itself
  says `ModChargeReady` rather than counting frames; (3) releasing has to
  reach the engine — edges must be computed once at the end of the frame, not
  inside each `Hold` call, or a later clear wipes the edge an earlier press
  produced (a charged weapon fires on release, so a broken edge means it never
  fires). The probe stands the one hunter that can inflict each state two
  units from a target, everyone else stands down, and reports `ok` / `nohit`
  (charged shot missed) / `FAIL` (hit, no state) / `n/a`. `-netdebug` prints
  what each probe actually saw.
