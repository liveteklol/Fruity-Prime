# Vulkan RHI Phase 20 — COMPLETE (2026-10-01)

Code: `09d67cc1` (two submission slots, no per-frame idle), `19bcb655`
(backend-neutral window readback), `715f5131` (in-place uploads, host-stall
count).

## What the readback path is

Every capture reaches the GPU through `CommandList::ReadColor`: the scene
target (`Scene::ReadSceneTarget`), the window (`Scene::ReadWindowBuffer` and
`ScreenCapture::SaveWindow`, which used raw GL until now), screenshots and
recording (`Export::Images`), thumbnails, and the cel depth calibration.

On Vulkan, `ReadColor` does the following:
1. Transitions the image to TransferSrc.
2. Copies it into a GpuToCpu buffer.
3. Restores the image's state.
4. Submits and waits on that submission's fence only.
5. Converts the result to the RGB8 or RGBA8 the caller asked for, in OpenGL's
   bottom-up row order.

The Vulkan targets already hold OpenGL's rows (Phase 17), so no caller
changed. The upright flip happens only in the present blit. Buffer rows are
tightly packed: the copy's `bufferRowLength` is zero and 4-byte texels need
no padding. The RGB8 targets are RGBA8 underneath, so alpha reads as one.

## Completion conditions

| Condition | Evidence |
|---|---|
| screenshot matches | the shell's RHI export check writes a 641x127 (odd row width) three-band frame through `Export::Images::Screenshot`: PASS on both backends, and `rhi-export-screen.png` is byte-identical between OpenGL and Vulkan |
| recording matches | the same through `Export::Images::Record` (the recording thread's PNG): PASS on both, and `rhi-export-record.png` is identical |
| scene target capture matches | renderprobe (`ReadSceneTarget`) through Vulkan on TEST ARENA and three cartridge rooms matches OpenGL's orientation and content; the cel probe (scene target plus cel depth) is within one level on the spawns OpenGL reproduces; the cel calibration's measured depth quantum is identical |
| no unnecessary WaitIdle | `-gpulifetime -rhi vulkan` counts actual CPU stalls on the GPU over each cycle's steady second half: **0** on TEST ARENA (3×120 frames) and MP2 HARVESTER (2×300 frames). It was 35 a frame before per-frame HUD text uploads were recorded in place instead of submitted and waited on. A frame waits only on the fence from two submissions back (frames in flight), and that is counted only when it actually blocks |
| no leak | `-gpulifetime -rhi vulkan`: every resource released after every cycle (4/4 and 3/3 PASS); golden and shell runs are validation-clean |

The OpenGL path is unchanged: golden 7/7 exact at `715f5131`. OpenGL's
statistics report 0 host waits (it waits inside the driver).

## Remaining waits, by design

- Destroying a texture still idles the device (`vkDeviceWaitIdle`), and a
  buffer, sampler or scene pipeline waits for in-flight scene work. These
  happen at scene release and resize, not per frame; the measurement above
  shows none in steady play. Phase 25's stress runs are where they are
  measured under load.
- A readback waits on its own submission: a capture is synchronous by contract.
