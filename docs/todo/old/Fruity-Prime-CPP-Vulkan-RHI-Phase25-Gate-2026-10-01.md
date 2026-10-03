# Vulkan RHI Phase 25 — Stability / Lifetime Stress: PASS on desktop (2026-10-01)

Code: `4727389e`. Machine: RTX 5070 Ti, driver 617.14, Vulkan 1.4.351, MSVC
Release build. Validation via `-vkvalidation`.

## Found and fixed: every room load leaked about 25 MB

The stress batch saw private memory climb steadily across `-gpulifetime`
cycles while every GPU count went back to 0. Over 40 cycles of TEST ARENA:
- OpenGL: 191 → 1175 MB;
- Vulkan: 259 → 1256 MB.

The same per-load leak applies to every match start and every map rotation.
Map rotation also goes through `PlayerEntity::Reset`.

The cause was `shared_ptr` cycles, harmless under C#'s collector and
uncollectable here, plus one pool that only ever grew:

| Cycle | Fix |
|---|---|
| player ↔ halfturret, AI data, Sylux bombs, last attacker/target, effects | `PlayerEntity::ReleaseReferences`, called by `Reset`; `PlayerAiData::InitializeGlobals` clears the static AI table |
| beam ↔ `EquipInfo` (which holds the beam array), owner, effects | `BeamProjectileEntity::ReleaseReferences` via `BeamProjectileArray::ReleaseReferences` (players, platforms, enemies) |
| effect element ↔ particle (`Owner`), element ↔ entry | `Scene::BreakEffectCycles` in `DoCleanup` |
| `CollisionDetection::Init` added 2048 candidates per load, and kept the old room's collision alive | `Init` restarts the pool |

How it was traced, using temporary probes since removed:
1. `weak_ptr`s showed the Scene and the room were freed.
2. 34 of 64 models survived; after the player fix, 3; after the effect and
   beam fixes, 0.
3. A `HeapWalk` diff by block size then found the last leak: exactly 2048 ×
   (56 + 16) bytes a cycle, which is the collision pool.

## Monitor added to the check

`-gpulifetime` now reports each cycle's private memory and fails on growth.
- **Measure:** the peak of the last quarter against the peak of the second
  quarter, per cycle between them.
- **Fail at:** 1 MB a cycle or more.
- **Not tuned to the old leak:** that leak (about 25 MB a cycle) is 25 times
  over the limit.
- **Readout:** `ProcessMemory::PrivateKiB` (PrivateUsage on Windows,
  RSS−shared on Linux).

## Results after the fix (40 cycles each, validation on)

| Room | OpenGL MB/cycle | Vulkan MB/cycle | GPU objects after release | VUIDs |
|---|---|---|---|---|
| TEST ARENA | 0.2 (229→238 peak) | 0.2 (333→340) | 0 / 0 retired | 0 |
| MP2 HARVESTER | 0.50 | 0.35 | 0 / 0 retired | 0 |
| MP3 PROVING GROUND | 0.46 | 0.29 | 0 / 0 retired | 0 |

100 fast cycles of TEST ARENA (OpenGL) hold a flat band of about 220 MB
(with dips to about 140 MB), with no slope.

**The residual is not ours.** On MP2, a heap diff of Vulkan cycles 10→30
shows about 25 KB a cycle in small blocks. The same engine code on OpenGL
shows about 0.3 MB a cycle, in odd-sized blocks (256 × 9431 bytes, single
blocks of 850 KB and 160 KB) that the engine never allocates under Vulkan.
That is the GL driver's own bookkeeping for the 10 shaders and 11 programs
relinked every cycle. The GL device's own counts return to 0.

## Stress matrix

| Plan item | Run | Result |
|---|---|---|
| match start/end, map change | `-gpulifetime` × 3 rooms × 40 cycles × 2 backends | PASS, memory flat, 0 VUID |
| launcher/game, resize, fullscreen, minimize/restore | `-shellshot` loop: 8 runs before the fix, 3 after, Vulkan + validation; 1 OpenGL | 28/28 shots every run, 0 VUID |
| backend startup alternate | `-maptest` 6 × (OpenGL, Vulkan) alternating | 12/12 exit 0 |
| bots / AI teardown | `-maptest "MP2 HARVESTER" -players 8 -bots` on both backends | identical report on both |
| image regressions | golden 7/7 identical to before on OpenGL; cross-backend parity 7/7 | PASS |
| handles | sampled every 2 s across the batch | stable within a process (486 / 578 / 607 by kind of run) |
| host waits | steady state | 3 per 30 frames, constant (no trend) |

Vulkan objects (command pools, fences, semaphores, descriptor pools) are
per-device and created once. The lifetime check's zero retired resources and
the validation layer's clean shutdown cover them.

## Not run: Android

Android pause/resume repeat and surface recreate repeat need a device (see
[Phase 22](Fruity-Prime-CPP-Vulkan-RHI-Phase22-Gate-2026-10-01.md)).
