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

- `-maptest` loads a room with eight players, drives them through the tour, and prints an inventory of the room's contents.
- Probes stand a player on every trigger (jumppad/teleporter) and check for events; affliction probes arm, charge, fire and verify states such as freeze/burn/disrupt.
