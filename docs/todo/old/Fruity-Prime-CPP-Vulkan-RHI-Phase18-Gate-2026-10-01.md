# Vulkan RHI Phase 18 — COMPLETE (2026-10-01)

Code: the Phase 17 commits (`feb0a40b`, `bc6468cc`, `74a4aaf4`). Phase 18
needed no scene-renderer change of its own. The post passes go through the
same machinery the main scene does:
- the deferred pass pipelines;
- the generated composite, cel and shift programs;
- automatic layout transitions for SceneColor, CelColor and CelDepth;
- `CopyColorAttachmentToTexture`;
- a depth-only sampled view of the D24S8 CelDepth, sampled in
  `SHADER_READ_ONLY_OPTIMAL` after the transition that `DrawScene` inserts.

## Completion conditions

Verified locally on RTX 5070 Ti with validation on and 0 errors.

| Condition | Evidence |
|---|---|
| cel shading, outline | `-maptest "TEST ARENA" -renderprobe -cel on -rhi vulkan`. Spawns 0–2 are the ones OpenGL reproduces against itself, and there Vulkan differs from OpenGL by at most 1 level in 15/38/96 pixels. The depth-quantum measurement is identical (24-bit, 3.037481e-8 against 5.960465e-8). Later spawns differ OpenGL against OpenGL too. |
| RTT composite | Every golden candidate's window image is the composite: 7/7 captured and gated. |
| HUD, mask | Golden `hud`. `-hudshots` on a live 4-player match, 20 frames: helmet mask, radar, energy, weapon and death text match, with 3–22 pixels over threshold per frame. |
| whiteout, disruption | Golden `whiteout-disruption`, final-stage gate verified. |
| fade | Golden `fade`, final-stage gate verified. |
| scoreboard | `-hudshots` frames 03 and 09: every element is there. About 5300 pixels differ, all of them one-pixel edges of glyphs, boxes and portrait quads. |
| pause game background | The scene frame under the pause menu is the scene target, shown above. Compositing the launcher over it is Skia's job, and that is Phase 19. |
| OpenGL comparison | This table. The OpenGL path is unchanged: golden 7/7 exact. |

## Tolerance: 2D quad sample ties

Every remaining difference is a one-pixel edge on a 2D HUD quad (glyph rows,
box borders, a portrait diagonal) at a pixel or texel centre that falls
exactly on a tie. Two conventions were measured:
- *GL rows in memory, winding inverted* (kept). 303 pixels on the golden
  candidates.
- *upright memory with a negative-height viewport* (reverted). This keeps GL
  winding and the orientation of the fill rule. The HUD row was unchanged,
  and new differences appeared: 328 to 1324 pixels.

Swapping the quad diagonal also changed nothing. The tie is therefore not
orientation, winding or triangulation. It is the implementation's tie
resolution (the Vulkan spec leaves it implementation-defined). Phase 24 records
it as the defined tolerance: isolated 1-pixel edges on 2D quads, no missing
element, no offset.
