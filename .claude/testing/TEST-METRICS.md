# Testing — metrics and interpreting results

This file describes how to read netcheck and maptest results and common traps.

Key lines and their meaning

| Line | Reads as |
|---|---|
| `damage pipeline (resolved here / replayed here)` | the two ends of the damage path. The authority shows `N/0`, everyone else `0/N`. They must match. |
| `remote position snaps` | visible teleports (bad). Healthy is 0-3 per client per 100 s. |
| `late=N` in the packets line | snapshots that arrived after a newer one and were refused. |
| `scoreboards agree (within N event)` | N > 1 means clients have different scores. |

Common traps

- Comparing raw totals without normalising for join times or rotations.
- Assuming a hit is a hit: afflictions require a charged shot; some weapons require holding `FullCharge * 2` frames.
- Standing at an entity's position expecting its trigger to notice — volumes are relative to entities.
