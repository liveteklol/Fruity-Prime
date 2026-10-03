# Vulkan RHI Phase 23 — CI for both backends: PASS (2026-10-01)

Green run: build_cpp [36802488060](https://github.com/Zection6V/Fruity-Prime/actions/runs/36802488060) on `b0fb6c09`. Every job passed.

| Condition | Job / step |
|---|---|
| Windows CI | Windows / MSVC, including the backend contract (`-rhicontract`): opengl, vulkan, 10 SPIR-V stages, Skia Vulkan |
| Linux CI | Linux / GCC: backend contract, Vulkan foundation runtime gate |
| macOS OpenGL CI | macOS / Clang |
| Android GLES CI | Android NDK arm64-v8a, x86_64, Android APK |
| Android Vulkan CI | the same NDK jobs assert `Android Vulkan backend: on`, 10 `.spv`, and the `AttachSceneSurface` / `CreateSurfaceSwapchain` / `WindowUi` symbols |
| shader build reproducible | Linux step "SPIR-V is regenerated reproducibly": two independent generations plus the build's own, compared byte for byte (10 stages and `bindings.json`) |

The first run failed on Linux: `-rhicontract` was given a relative path, and
startup changes the working directory to the installation directory. The fix
(`b0fb6c09`) resolves the path against the directory the command was typed
in. The same push added the phase26 isolation audit job.
