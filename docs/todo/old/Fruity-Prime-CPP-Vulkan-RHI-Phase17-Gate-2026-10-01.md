# Vulkan RHI Phase 17 — COMPLETE (2026-10-01)

Implementation: `feb0a40b` (scene through Vulkan), `bc6468cc` (frame reopen),
`74a4aaf4` (flush snapshot, lifetime check on the scene device).

## What was built

- `Rhi/SceneBackend` selects the scene's device, programs, meshes and
  transient geometry. The Scene names no backend. `-rhi vulkan` draws the scene
  through one headless Vulkan device, and `-vkvalidation` adds the layers.
  OpenGL stays the default and is unchanged.
- Scene passes (`DescribeScenePass`) become *deferred* Vulkan pipelines. The
  command list resolves a native pipeline per draw from the pass state, the
  program's generated binding layout, the attachment formats (none, when the
  pass has no depth), and triangle or line topology. It caches that pipeline.
- The row convention lives in one place: GL rows with no Y flip. Only the
  front face is inverted, and GL's single-face stencil is applied to both
  faces. Clip Z comes from the Phase 16 shaders.
- `VulkanCommandList` handles:
  - dynamic rendering with deferred clears, where `DontCare` keeps the contents as GL does;
  - automatic attachment and sampled-texture layout transitions;
  - dynamic viewport, scissor/render area, stencil reference and vertex stride;
  - a per-list transient ring and descriptor pools, reset on flush;
  - `ReadColor`, `CopyColorAttachmentToTexture` and an offscreen window target.
- Scene work is flushed at `EndFrame`, before any other submission, and before
  destroying a resource. GPU order is therefore record order.
- RGB8 targets are stored as RGBA8: sampled views swizzle alpha to one, and
  pipelines mask alpha writes. An unbound unit samples opaque black, as it
  does in GL.
- `VulkanSceneConstants` writes the std140 blocks through the generated
  manifest into the *current* program, which reproduces glUniform/UseProgram
  semantics. `alpha_test` comes from the pipeline.
- Meshes: static interleaved buffers. GL current colour, normal and texcoord
  arrive as stride-0 streams, and Mixed attributes are filled per draw. The
  terminal state is kept. Quads, strips, fans and quad strips are converted to
  triangle lists in GL order, and LineLoop to lines.

## Completion conditions

Verified locally on RTX 5070 Ti with validation on.

| Condition | Evidence |
|---|---|
| room / model / textures / no flipped UV / winding / depth | Golden capture through Vulkan: 7/7 captured, all control and final-stage gates `verified`, and the scene pixel-identical to OpenGL. Real rooms MP2 HARVESTER, UNIT2 LANDING BAY and MP3 PROVING GROUND pass renderprobe with `0 drew nothing`, and look the same side by side (`C:/tmp/gp/p17-rooms.png`). |
| decals | Golden `decal` identical outside the HUD band. |
| translucent + stencil | Golden `transparent-object` identical outside the HUD band. |
| particles / trails | Golden `particle`, `trail` identical outside the HUD band. |
| validation | 0 validation messages other than the OBS/Bandicam implicit-layer version notices, across golden, renderprobe and lifetime runs. |
| lifetime | `-gpulifetime "TEST ARENA" -cycles 4 -rhi vulkan`: 657 textures / 722 buffers / 8 shaders / 11 pipelines while drawing, and 0 of everything after every release, 4/4 PASS. |
| OpenGL unchanged | Golden 7/7 exact vs the Phase 3 baseline at `74a4aaf4`. The shellshot `shell-match` is identical to Phase 16 (`18892da2`), and `shell-pause` has 0 pixels over threshold. |

Evidence: `C:/tmp/gp/out-p17a` (GL) and `C:/tmp/gp/out-p17a-vk` (Vulkan);
the `p17-*` logs.

## Known difference (carried to Phase 18 / 24)

Every candidate differs in the same 303 pixels. They are the bottom pixel row
of the HUD "PRESS FIRE TO RESPAWN" glyph quads, whose edges fall exactly on
pixel centres. Keeping GL's memory rows means Vulkan's top-left fill rule
breaks that tie on the other side. A sub-pixel viewport nudge was measured:
it moves 800–2500 world pixels instead, so it was rejected. The HUD is Phase
18's scope. The main-scene passes (Phase 17) have no difference.

Renderprobe is not a parity instrument: it differs even OpenGL against
OpenGL. The golden capture is.
