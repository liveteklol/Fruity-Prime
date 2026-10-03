# Vulkan RHI Phase 19 — COMPLETE (2026-10-01)

Code: `f3650254` (the game window presents through Vulkan), `cbdde122` (Skia
Ganesh Vulkan, WindowUi, backdrop), `19bcb655` (minimize/close/readback, the
MSVC build, Skia's memory allocator).

## What was built

- **The game window presents through Vulkan.** With `-rhi vulkan` the
  RenderWindow is a no-API GLFW window: no GL context exists anywhere in the
  process. The scene device is made on its surface, and the swapchain shares
  that context. Each frame, the device's window target is blitted into the
  acquired image with the rows flipped, which is the one place the backend's
  row order meets the screen's.
- **Skia Ganesh Vulkan on the RHI device.** `NativeRuntime/Skia/VulkanInterop`
  is the only code that sees Vk handles. It gives Ganesh the RHI's own
  VkInstance, VkPhysicalDevice, VkDevice and graphics queue (no second device
  or queue), plus a VMA-backed `skgpu::VulkanMemoryAllocator` on that device,
  since the one Skia ships is not exported from the DLL.
- **The UI target is an RHI texture**, handed over in
  COLOR_ATTACHMENT_OPTIMAL both ways:
  - The RHI submits its work and leaves the image as a colour attachment.
  - Skia flushes it back to that layout and its submit is waited on.
  - The RHI adopts the state, so its next sample inserts the barrier.
  - GENERAL was tried first and made Skia derive `HOST_WRITE` access that its
    own barriers cannot carry, which validation reported.
- **The launcher composites in Vulkan.** `Rhi::Vulkan::WindowUi` draws the
  photograph through a generated `backdrop` program and the Skia UI through
  the composite program, premultiplied. `UiOverlay` and `LauncherPhoto` route
  to it when the window is Vulkan's. The OpenGL paths are untouched.
- **Build.** CMake enables `FRUITY_SKIA_VULKAN` only for a Skia exporting
  `SK_VULKAN`, not for headers alone. CI asks vcpkg for `skia[gl,freetype,vulkan]`
  on Windows and Linux.
  - A build without Skia Vulkan refuses a Vulkan window with a visible error.
    There is no CPU fallback.
  - `NCSF.cpp`'s code-page tables are now split into pieces MSVC's literal
    limits accept, joined byte for byte.
- **Frame order is unchanged:** Scene → HUD/post → TickUi (Skia) → overlay /
  launcher hunter → present. Only one command list on the device holds
  unsubmitted work at a time, so the queue sees work in draw order.

## Completion conditions

Verified locally on a Windows MSVC Release build against vcpkg skia 148 with
Vulkan, on an RTX 5070 Ti.

| Condition | Evidence |
|---|---|
| Launcher Vulkan | `-shellshot -rhi vulkan`: exit 0, 28 photographs of the real window, including `shell-start`, maximized, fullscreen, resized, minimize/restore |
| Settings Vulkan | `shell-click` (front-screen settings), `shell-settings-ingame` |
| Pause Vulkan | `shell-pause`, `shell-pause-maximized`, `shell-pause-fullscreen` over a live match |
| Map Vote Vulkan | `shell-endgame`: the vote list with every map's preview |
| End Screen Vulkan | `shell-endgame-hunter`: results, hunter picker and the 3D preview model |
| no hidden GL context | the window is `GraphicsWindowMode::NoApi`; any GL call would fault with no current context |
| no CPU full-frame fallback | the UI is Ganesh-on-Vulkan (`GpuSurface::RhiTexture`); the CPU upload path is never taken, and a Skia without Vulkan throws rather than falling back |
| Skia/Vulkan sync validation clean | the full shell loop under `-vkvalidation` with `VK_KHRONOS_VALIDATION_VALIDATE_SYNC=true`: 0 messages besides the OBS/Bandicam implicit-layer notices |
| no texture corruption | side by side with the OpenGL run of the same MSVC build (`C:/tmp/gp/shell-gl`, `shell-vk`): the same screens with the same content. The differences are the animated backdrop and match timing (fade, intro camera, bot scores) |

The RHI screenshot/record export check (`rhi-export-*`) is byte-identical
between the backends. The OpenGL path is unchanged: golden 7/7 exact at
`19bcb655`.

## Notes

- The window's Vulkan device lives for the process, as OpenGL's context device
  does. The launcher's Skia surface, overlay and side scene are statics that
  are destroyed after the window, and each still holds Vulkan objects.
- `-shellshot` minimizes the window mid-script. Presenting now skips a frame
  while there is no drawable area (or the window is closing), where the
  acquire used to wait inside the frame loop for one.
- Fence and acquire waits log `[vulkan] still waiting for ...` every 2 s, so a
  GPU hang is a line in the log rather than a silent stop.
