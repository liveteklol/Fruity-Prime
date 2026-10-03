# Vulkan RHI Phase 21 — COMPLETE (2026-10-01)

This records the original startup-selection gate. Desktop settings subsequently
gained switching without ending the match. See the
[hot-switch repair and current validation](Fruity-Prime-CPP-Renderer-Hot-Switch-Fix-2026-10-01.md)
for the current desktop behavior; Android still applies the setting next start.

## Selection

| Source | Values | Wins |
|---|---|---|
| `-rhi opengl\|vulkan\|auto` (`gl`, `vk` accepted) | explicit | always |
| `launcher.txt` `renderer=` (Settings → Game → Window → **Renderer (next start)**) | preference | when no `-rhi` |
| neither | OpenGL | the default for now, which the plan allows ("既存ユーザー体験を優先して最初はOpenGL default") |

The request is resolved once, the first time the scene device or window mode
is asked for (`Rhi::SelectedSceneBackend`), and the window is made for the
result: NoApi with Vulkan presentation, or an OpenGL context. It is a startup
choice; the setting says "next start".

- **Explicit Vulkan that cannot start is an error, not a fallback.**
  `SceneBackendUnavailable` names what is missing: no Vulkan in this build,
  Skia without Vulkan for the launcher, no loader or ICD, or the device probe's
  own reason (no Vulkan 1.3 GPU with dynamic rendering and synchronization2).
  The GUI launcher shows it in a native error dialog (`ShowErrorDialog`,
  MessageBoxW on Windows, which is a GUI binary with no console) and exits.
  It does not open a different launcher and does not start OpenGL.
- **Auto** takes Vulkan when that same check passes, and otherwise takes OpenGL,
  logging why: `[render] auto: OpenGL, since ...`.
- **Logging.** One line at window creation, printed and in the debug log:
  `[render] backend requested auto (command line), selected vulkan, NVIDIA
  GeForce RTX 5070 Ti, Vulkan 1.4.351, driver 617.14, swapchain 1280x768
  BGRA8, depth D24S8, frames in flight 2, validation on`. That covers
  requested, selected, GPU, API version, driver, swapchain format, depth
  format, frames in flight and validation. The OpenGL line carries the
  adapter string.
- Hot switching is out of scope, as the plan says. The device, swapchain and
  window are made through `SceneBackend` in one place, so a later switch has
  one place to tear down and remake.

## Completion conditions

| Condition | Evidence |
|---|---|
| explicit OpenGL | MSVC build, `-launcher -rhi opengl`: `selected opengl` |
| explicit Vulkan | MSVC build, `-launcher -rhi vulkan` (and Phase 19's whole shell loop) |
| Auto | MSVC build (Skia with Vulkan): `requested auto ..., selected vulkan`. MinGW build (MSYS2 Skia without Vulkan): `auto: OpenGL, since this build's Skia has no Vulkan backend ...`, and the OpenGL launcher opens |
| error visible | MinGW build, `-launcher -rhi vulkan`: the process's only window is the modal "Fruity Prime" dialog carrying the reason and what to choose instead, until it is dismissed |
| no silent fallback | the same run opens no launcher and no OpenGL window; the text launcher fallback is skipped for this error |
| preference | `renderer=vulkan` in `launcher.txt` with no `-rhi`: `requested vulkan, selected vulkan`; the Settings row writes it (`-uishot` settings.png shows the row) |
