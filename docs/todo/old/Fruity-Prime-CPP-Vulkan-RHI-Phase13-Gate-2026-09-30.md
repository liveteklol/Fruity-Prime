# Vulkan RHI Phase 13 validation

Date: 2026-09-30 (Asia/Tokyo). Branch: `develop3_rendering`.

## Result

### Audit correction (2026-09-30)

The audit found that `Context::Impl::swapchainMaintenance1` remained false
even when the device enabled the extension and the log reported 1. The original
run below did **not** exercise present fences. Its claim about that cleanup
path is superseded by this audit.

Audit repair SHA `53298011d0d88ff6762eb80c8b0918d596b86b4e` stores the enabled feature flag, preserves pending
present-fence waits for enqueued out-of-date/surface-lost presentations,
cleans partially constructed image resources on exceptions, and rejects
duplicate acquisition/command recording. Context teardown also tolerates
incomplete function loading after initialization failure.

The rebuilt RTX 5070 Ti diagnostic passed resize, fullscreen, minimize/restore
and shutdown with validation=1, errors=0 and `present-fence-waits=28`. It now
fails if enabled present fences were never waited. Foundation and resource
diagnostics also passed, and CTest passed 5/5. Exact-SHA CI for these repairs
is complete on final correction SHA `db2ed0f4d353b88b2cee196232ac9b04bbf9f069`
in [desktop CI 36704806153](https://github.com/Zection6V/Fruity-Prime/actions/runs/36704806153).
All three desktop jobs passed. Linux normal presentation recorded 35 present
fence waits and forced fallback recorded six retired releases, both with zero
validation errors.

Extension-less recreation now defers old swapchains and present semaphores
until a completed fence-backed reacquisition from the replacement chain proves
presentation progress. `-vulkanpresentfallbackcheck` disables maintenance1
and asserts this path actually released retired chains. RTX 5070 Ti passed
with validation=1, errors=0 and `fallback-retired-releases=6`.

At final shutdown, the extension-less path still uses device idle and releases
remaining objects, as the Khronos sample does. Runtime shutdown passed; this
does not establish specification-level presentation completion for that last
presentation. Keep this limitation visible in the final architecture audit.

Phase 13 is complete at implementation commit
`8c6f2d2a044544c5975515fa838730271a02f003` (`Add Vulkan swapchain and presentation`).
The gate is intentionally clear-only; Vulkan game rendering and image parity
belong to later phases.

The desktop backend creates a `GLFW_NO_API` window surface, chooses a supported
surface format, present mode, extent, and image count, wraps swapchain-owned
images without destroying them, and owns their image views. It uses FIFO as the
fallback present mode; Mailbox and Immediate are selected when requested and
supported. Two frame slots own command buffers, acquire semaphores, and frame
fences. Each swapchain image owns its render-finished semaphore and, when
`VK_EXT_swapchain_maintenance1` is available, a present-completion fence.
Acquire/present handle out-of-date and suboptimal results, resize, and zero-sized
minimized windows. The clear path records synchronization2 transitions around
dynamic rendering.

## Local validation

MSYS2 MinGW64 Release build succeeded:

```text
cmake --build tools/build/out/msys2-mingw64-Release --config Release --target fruity_prime --parallel 8
ctest --test-dir tools/build/out/msys2-mingw64-Release -C Release --output-on-failure
```

CTest passed 5/5. On Windows 11 with an NVIDIA GeForce RTX 5070 Ti (Vulkan API
1.4), `FruityPrime.exe -vulkanpresentcheck -noupdate` passed clear presentation,
resize, fullscreen/windowed transitions, minimize/restore, FIFO/Mailbox changes,
and shutdown with validation enabled and zero errors. The device reported
`swapchainMaintenance1=1`; the audit found that the original run used the
extension-less cleanup path because the stored feature flag was false.
The validation layer also reported API-version warnings from the Bandicam and
OBS implicit layers (1.2 versus the application's 1.3); these were warnings,
not validation errors. `-vulkancheck -noupdate` passed the foundation control,
and `-thumbnailwindowcheck -noupdate` passed the OpenGL control.

Logs: `C:/tmp/gp/p13-build.log`, `C:/tmp/gp/p13-foundation.log`,
`C:/tmp/gp/p13-present.log`, and `C:/tmp/gp/p13-opengl.log`.

## Exact-SHA CI

[CI run 36684051768](https://github.com/Zection6V/Fruity-Prime/actions/runs/36684051768)
completed successfully on exact implementation SHA
`8c6f2d2a044544c5975515fa838730271a02f003`: 11/11 jobs passed. This includes
Windows/MSVC, Linux/GCC, macOS/Clang, Android build contract, Android NDK
arm64-v8a and x86_64, Android APK packaging, and the Phase 4/5/9/11 static
audits. Linux/GCC passed both the Vulkan foundation and the Xvfb/Openbox Vulkan
presentation runtime gates.

## Scope and lifecycle note

The presentation diagnostic submits several clear colors but does not read back
pixels or draw the production game renderer. This phase proves swapchain
creation, presentation, window transitions, validation-clean operation on the
tested devices, and orderly shutdown; it does not claim game-scene parity.

The local device supports `VK_EXT_swapchain_maintenance1`. The audit repair
above verified its present-fence cleanup; the original implementation did not. On
devices without that extension, the implementation uses per-image
`renderFinished` semaphores and deferred retirement during recreation. The
forced extension-less diagnostic exercised recreation and shutdown on hardware
with zero validation errors; final shutdown still waits for device work. The
[Khronos Vulkan Guide](https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html)
documents image reacquisition as the portable synchronization point for reusing
present wait semaphores, and notes that ordinary queue/device idle waits alone
do not provide a specification-level proof that a pending presentation has
released swapchain resources. The
[Khronos swapchain recreation sample](https://docs.vulkan.org/samples/latest/samples/api/swapchain_recreation/README.html)
describes the corresponding deferred-retirement workaround before
`VK_EXT_swapchain_maintenance1`.
