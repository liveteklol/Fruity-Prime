# Vulkan RHI Phase 22 — CODE COMPLETE, DEVICE VERIFICATION BLOCKED (2026-10-01)

Code: `bbf7298c` (the Android Vulkan path), `c050292a` follow-up (a launcher-only
Skia requirement, so harness windows are unaffected).

## What was built

The same backend is used on Android; nothing is duplicated. Device,
resources, pipelines, bindings and synchronization are the desktop's own
code, now compiled for Android. One platform layer covers what differs:
- **Loader.** `vkGetInstanceProcAddr` comes from `libvulkan.so` (dlopen)
  instead of GLFW (`Context::Impl::InstanceProc`).
- **Instance and surface.** The instance asks for `VK_KHR_surface` +
  `VK_KHR_android_surface`. The surface is made by `vkCreateAndroidSurfaceKHR`
  on the GameView's `ANativeWindow` (22.1, 22.2).
- **Swapchain.** `CreateSurfaceSwapchain` sizes itself from the surface's
  current extent; there is no GLFW window. Waits and close checks have
  platform forms.
- **Lifecycle (22.3–22.5).** The app, device, surface, swapchain and game
  state are separate:
  - The device is made with the first surface and lives for the process.
  - Losing the surface (pause, home screen, `surfaceDestroyed`) releases the
    swapchain, then the VkSurface (`DetachSceneSurface`). The scene, its GPU
    resources and the game stay.
  - The next surface replaces only those two (`AttachSceneSurface` →
    `Context::ReplaceAndroidSurface`).
- **Orientation (22.6).** A size change resizes the swapchain from the
  surface extent, and the scene gets the same size it gets under GLES
  (`Scene::Size`/`OnResize`). Touch mapping and projection are the existing
  GLES path's own, since they read the same `_size`.
- **GLES coexistence (22.7).** `GameView::CreateContext` resolves the
  renderer once (OpenGL ES default, Vulkan, Auto; Settings → Renderer). An
  explicit Vulkan that cannot start throws `SceneBackendUnavailable`, which
  reaches the player as the match's error notice. No EGL context is made in
  Vulkan mode.
- **UI (22.8).** The Android launcher has always been a CPU raster; in Vulkan
  mode it is uploaded to an RHI texture and composited by the scene device's
  `WindowUi`. There is no GLES context and no second Vulkan path.
- **Build.** The NDK's Vulkan headers, VMA from vcpkg (added to the Android CI
  job's package list) and the NDK's `shader-tools` glslc. `-- Android Vulkan
  backend: on`.

## Verified

| What | Evidence |
|---|---|
| arm64-v8a builds with Vulkan | local NDK 27.2 build: 10 SPIR-V stages generated, 501 `Rhi::Vulkan` symbols in `libFruityPrime.so`, including `AttachSceneSurface`, `CreateSurfaceSwapchain` and `WindowUi::DrawTexture` |
| x86_64 builds with Vulkan | local build: 502 `Rhi::Vulkan` symbols |
| desktop unaffected | golden 7/7 exact (OpenGL); Vulkan golden unchanged (the 303-pixel HUD tolerance only); the MSVC `-shellshot -rhi vulkan` loop: 28 shots, 0 validation messages |

## Not verified: needs a device

No Android device or emulator is reachable from this machine. The SDK has no
emulator or system image, and `adb` cannot connect to its own server
(`adb_connect: host:version` hangs on tcp:5037, with or without the
sandbox). None of the plan's runtime conditions has been observed:

- [ ] Android Vulkan scene
- [ ] Android Vulkan HUD/UI
- [ ] pause/resume
- [ ] surface destroy/recreate
- [ ] orientation
- [ ] touch mapping
- [ ] Vulkan validation clean (the validation layer is not packaged in the APK either)
- [x] GLES **build** (both ABIs); GLES **run** was not re-observed here

To close this phase, run on a Vulkan 1.3 Android device with Settings →
Renderer → Vulkan, or `renderer=vulkan` in the app's `launcher.txt`:
1. A match: is the scene and HUD there?
2. Home screen and back: are the swapchain and surface rebuilt with the game
   intact?
3. Rotation: does the surface resize?
4. Touch: do the controls land where they are drawn?
5. A Vulkan 1.3-less device with Vulkan forced: is the error notice shown,
   with no GLES fallback?
6. The same steps on OpenGL ES, to confirm GLES runs.

## Follow-up (2026-10-01)

- `adb` now works. The hang was a stuck old `adb` process; killing it fixed
  it. A connected device can now be used directly.
- Android Emulator 37.1.11 and the API 34 x86_64 image are installed (AVD
  `fp34`). The emulator will not start because Windows Hypervisor Platform is
  off on this PC. That is a Windows system setting the user has to enable.
- The user's decision: Phase 22 stays **built, device testing pending**. The
  steps above are what closes it.
