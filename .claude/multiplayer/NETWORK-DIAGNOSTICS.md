# Multiplayer — diagnostics and network damage investigation

This file captures the network damage diagnostics work and the 2026-08-22 handoff notes.

Summary of the 2026-08-22 investigation

- The latency bug is reproducible against `france-mining.com:27888` (RPi3B). It does not reproduce reliably on loopback. The failure is directional and intermittent: a client can fire normally while one remote slot receives zero damage for the whole match.

Work completed

- Added diagnostics to `NetDamage`: `Fired`, `AimDrift`, and projectile player collision counters (`PlayerChecks`, `PlayerOverlaps`, `PlayerAccepted`).
- Made `MPHREAD_NETLOG_INTERVAL` configurable in `NetLog` to vary logging frequency during comparisons.
- Tried multiple mitigation experiments (position corrections, authority handoff snapshot transfer) and reverted the temporary protocol-4 experiment that crashed the Pi; protocol version remains **3**.

Measurements

- Corrected client build was run in campaigns of four 70-second matches against the Pi, with three clients started three seconds apart. The final protocol-3 run reproduced the immune slot.

Example log excerpt (authority reports):

```
damage pipeline: [0] 5/0 [1] 0/0 [2] 0/0
player collision checks: [0] 911/3/0 [1] 916/1/0 [2] 916/0/0
FAIL: never took a single hit: BRAVO_F (slot 1), CHARLIE_F (slot 2)
```

See the code in `src/MphRead/Mods/Network/` for packet and net handling.
