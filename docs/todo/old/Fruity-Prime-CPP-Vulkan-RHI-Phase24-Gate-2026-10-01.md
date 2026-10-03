# Vulkan RHI Phase 24 — Cross-backend Golden Parity: PASS (2026-10-01)

The checker is `tools/validate-cross-backend-parity.py OPENGL_DIR VULKAN_DIR`.
It defines the tolerance in one place.

## Tolerance

Refused outright:
- black frames (under 1% of pixels lit);
- flipped output (the pair must beat the vertically flipped pair by 4×);
- a mean per-channel difference over 1 level. This catches offsets, wrong
  textures or UVs, and missing or extra geometry.

Tolerated:
- At most 0.05% of pixels may differ by more than 8 levels, and only in groups
  at most 2 px thick: an edge, never an area.
- A differing pixel that equals the other backend's pixel one step away is a
  **nearest-sampling tie** and does not count towards the 0.05%. On a
  magnified nearest-filtered texture, a texel boundary lands exactly on a
  pixel centre and the two APIs round it opposite ways. It still counts
  towards the mean and the thickness rule.

Why the tie rule exists: the 4-player scoreboard frames (`-hudshots` 03, 09)
differ on every third row of the 3×-magnified portraits and panels (0.90%).
That is a single row per texel, rows 130, 133, 136, and so on. None of the
following changed it by a single pixel:
- coordinate-convention experiments;
- integer shifts;
- strip-order changes (`FRUITY_VK_STRIP`), now removed;
- quad split (`FRUITY_VK_EVEN`), now removed.

That leaves rasteriser and sampler tie-breaking, which is
implementation-defined.

The negative tests still fail as intended:
- 1 px vertical shift: mean 2.95 and 95 thick areas;
- 2 px horizontal shift: 1.83%;
- flip;
- black frame.

## Corpus

| Set | Result |
|---|---|
| Golden (fade, HUD, whiteout, disruption, ...) | 7/7 |
| renderprobe TEST ARENA, fog, two runs | 16/16, 16/16 |
| renderprobe TEST ARENA, cel | 16/16 |
| renderprobe MP2 HARVESTER | 8/8 |
| renderprobe MP3 PROVING GROUND | 12/12 |
| renderprobe UNIT2 LANDING BAY | 8/8 |
| real match `-hudshots`, 4 players (HUD, masks, scoreboard) | 20/20 |

Coordinate convention: one place only. The flipped blit at present plus the
OpenGL-convention scene targets (Phase 17); no per-shader flips.
