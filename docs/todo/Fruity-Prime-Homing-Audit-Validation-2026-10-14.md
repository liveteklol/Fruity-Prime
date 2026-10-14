# Homing and server authority validation — 2026-10-14

Both Dedicated server and Hosted lobby now use a dedicated authoritative
server. The creator, first participant and every other player remain normal
clients. A lobby owner may edit/start the lobby, but cannot publish snapshots,
receive an authority grant, or migrate authority when leaving.

Shooter priority is retained. A client predicts its own shot/hit and submits
a hit claim. The server can apply that claim even when its own projectile
simulation did not resolve the hit. Existing claim checks and duplicate
damage handling remain. Combat tests cover delayed claims and trades in both
arrival orders, including shots fired before the shooter died.

Native C++ working tree on `develop12_homing`, based on
`5f6b87d977978d1e44e707a41c64ec56429e8e20`. Existing work was continued in
place. This record describes validation before Git delivery; delivery commits
are recorded in the branch history.

## Fixes found by the remaining checks

- Prime ownership was missing from authority snapshots. The owner now fires
  using the received role, with a separate role snapshot frame rather than
  the target interpolation acknowledgement. Copies and claims validate that
  frame against authority history. Claims also recheck a continuous intent
  that arrived after the claim.
- Join now waits for the server roster to acknowledge the selected hunter.
  With lag, a first shot could otherwise arrive before Identify and be
  reproduced using the placeholder hunter's affinity row. Eight-player Battle
  and Prime regressions cover this race.
- Hosted rooms now run in separate dedicated-server processes because the
  native simulation has global state. NetHostSession, HostPool and MasterServer
  use this model; LocalServer shares the launch implementation. Lobby policy,
  format, ready/join settings and the exact owner token cross the process
  boundary. Creator departure does not stop or migrate the simulation.
- GUI/CLI hosting requests allow 35 seconds for the 30-second child startup
  budget. The production request API is checked with a deliberately delayed
  reply beyond its former six-second timeout. Startup readiness compares the
  bounded wire name, and macOS paths stay in the user-data directory.
- The matrix now fails on a failed cell, requires actual Prime shots in Prime
  cells, requires all participant logs, rejects unexpected client failures,
  verifies authority=False and cleans up its own processes. Per-process log
  directories prevent the normal eight-log retention policy from deleting
  another cell's evidence.
- Traces explicitly record muzzle contact before target acquisition. It is
  reported separately from changed locks; unexplained missing locks and
  changed target identities still fail. Collision and hit registration are
  unchanged by this diagnostic addition.
- Shot traces include raw origin/ray, physical velocity, acquisition range
  and cosine limits, and the selected target's distance/cosine at spawn.
  A real projectile fixture changes the Prime holder while a Prime Power Beam
  is airborne and checks its firing-time trajectory and first contact.
- The spawn check requires every named row and visits every placement. A
  controlled equipment fixture runs Enemy:8 through a real PsychoBit AI when
  the installed assets contain no placement using that row; its charge remains
  the live AI's charge (observed 0). Charged trajectory cases are separate.
- The ally/hostile fixture advances between shots so it does not accidentally
  inspect the previous projectile. Charge 35/36, 1P missile 90 and additional
  plain/affinity Shock Coil cases remain as reusable checks.

The wire version is **33**. Snapshot size and the eight-player pickup budget
are unchanged. Historical versions 30, 31 and 32 have immutable fixture gates;
version 33 has capture and round-trip checks. Client and server need the same
wire version.

## Verified

| Gate | Result |
|---|---|
| MSVC Release | passed |
| CTest | 112/112 passed, including all six game-file fixtures; no skips |
| Real projectile vs 30 Hz reference | 128/128 checks; paths within 0.002 units at each tick, first contact, target release and airborne Prime handoff checked |
| Presentation A/B | 76/76; 60/144/240/540 draw rates, confirmed impacts and retarget independently on/off, identical physical state |
| Shock Coil | 17/17; both rows, movement, morph cone, team filtering and walls |
| Enemy/boss/platform spawning | 80/80; required Enemy 0/2/8/9/10, Cretaphid, Slench Tear, Gorea Volt and ArcWelder observed |
| Combat fixture | 154 assertions, including separate role/target frames, false Prime declaration, continuous lock consumption and claim arbitration |
| Trace comparator self-test | 14 cases passed, including empty/missing-Prime cells, recorded muzzle contact and unexplained missing/changed locks |
| Final trace fields over UDP | additional two-client cell passed under Windows PowerShell; 688 projectile records and 480 target geometries checked for finite vectors/range and valid cosines across owner/server/observer |
| Hosted authority integration | passed; local/online creation, owner token/options, two concurrent isolated rooms and simulation/claim eligibility after creator departure |
| Dedicated-server host pool | passed; the real parent server accepts a room request and starts an isolated authoritative child |

## Full UDP matrix

The final 19-cell batch passed: every cell is `RESULT PASS` and the wrapper
exited 0. It includes Battle/Prime with 2/8 players, bots, RTT 0/80/150 ms,
jitter, loss/reorder, Shock Coil morphing, hosted rooms through the master
directory and through a dedicated parent server's host pool. No unexplained
weapon/role/target mismatches or homing straight-ray refusals were reported.
All participants remained non-authoritative clients.

Earlier batches with shared log storage or the unacknowledged first-shot
hunter are superseded. Outputs are under
`tools/build/out/msvc-Release/homing-matrix/`; final transcripts are
`tools/build/out/homing-server-authority-delivery-matrix.txt`,
`tools/build/out/homing-server-authority-final-ctest.txt` and
`tools/build/out/homing-server-authority-final-build.txt`.
The additional trace check is in
`tools/build/out/homing-server-authority-trace-fields.txt` and
`tools/build/out/homing-server-authority-trace-numeric.txt`. The harness
retains client process handles so Windows PowerShell can reliably read their
exit codes after termination. Its earlier trace-only run with missing exit
codes is superseded by the passing run `20261014-230959-trace-fields`.

Repeat with:

```powershell
tools\build\build-cpp.bat msvc Release
ctest --test-dir tools/build/out/msvc-Release --output-on-failure
powershell -NoProfile -ExecutionPolicy Bypass -File tools/testing/run-homing-net-matrix-all.ps1
```

These are separate native processes exchanging real UDP on loopback, with
RTT/loss/reorder injected by the production transport. They do not measure a
remote physical connection, Wi-Fi, a Pi or another platform. The presentation
fixture executes production flight/presentation logic at synthetic draw rates;
it does not photograph GPU output or measure a physical display.

Logs are local build artifacts. CTest fixtures requiring game files skip with
77 when the files are absent; such a skip is not a local acceptance pass.
