# Vulkan RHI Phase 26 — OpenGL/Vulkan Architecture Freeze: PASS (2026-10-01)

## Final audit

`tools/check-rhi-isolation.py` runs in CI (`phase26 / RHI backend isolation`).
It strips comments, then checks three rules:

1. **Common RHI.** No `Vk*` / `vk*` / `GLuint` / `OpenGL::GL` / `ID3D12*` /
   `MTL*` anywhere in `Rhi/` outside `Rhi/OpenGL/` and `Rhi/Vulkan/`.
   Result: 0. The only hits were in comments.
2. **Renderer frontend.** No `GL::` / `Vk` / `vk` in `Renderer.cpp`/`.hpp`.
   Result: 0.
3. **Backend isolation.** Native GL and Vulkan calls appear only in the
   backend directories, the Skia interop, and 8 listed files, each with a
   reason:
   - the GL binding: `OpenTK/GL.cpp`, `GLAndroid.cpp`, `GlEs.cpp`;
   - the Android head's EGL/GLES platform layer: 3 files;
   - two instruments whose subject is the OpenGL window itself.

   A planted violation fails the check.

Moved into the backend to get there:

| Was | Now |
|---|---|
| `UiOverlay`'s GL upload and composite | `Rhi/OpenGL/OpenGlLauncherOverlay` |
| `GL::Viewport` in 5 harness windows | `Rhi::ResetWindowViewport` (nothing under Vulkan) |
| the capture diagnostics' GL_KHR_debug hook | `Rhi/OpenGL/OpenGlDiagnostics` |
| `Rhi::Vulkan::WindowUi` named in `Mods/Render` and the Android overlay | abstract `Rhi::WindowUi`, created by `Rhi::CreateSceneWindowUi`; Mods hold `SceneWindowUi` |

Verification:
- golden 7/7 byte-identical to the previous OpenGL captures;
- cross-backend parity 7/7;
- `-shellshot` Vulkan + validation: 28 shots, 0 VUID; OpenGL: 28 shots, with
  the overlay composited over the match;
- CI green, including all Android jobs.

## Metal review (no code)

| Question | Answer |
|---|---|
| Does `GraphicsDevice` change for a MetalDevice? | **No.** Devices, resources, pipelines, bindings, command lists and statistics are abstract; Vulkan was added without changing them. |
| Does `Renderer.cpp` change for a MetalSwapchain? | **No.** It asks for `CreateSceneWindowSwapchain` / `PresentSceneWindow`. Backend selection (`SceneBackend.cpp`: the enum, `SelectedSceneBackend`, `SceneDevice`, `CreateSceneWindowUi`) is the one place a new case goes. That is the factory's job, not the frontend's. |
| Does Scene change for a MetalPipeline? | **No.** Scene programs are `SceneShaderSet` / `SceneProgram`; Vulkan resolves pipelines per draw from the same descriptions. The shader source has to be translated (SPIR-V → MSL via SPIRV-Cross). That is build work, not a frontend change. |
| Does Material change for a MetalTexture? | **No.** Materials hold `Rhi::Texture` / `Rhi::Sampler`. |
| The launcher? | Implement `Rhi::WindowUi`, plus a Skia Metal interop next to `Skia/VulkanInterop`. Before this phase this meant a branch in three Mods files; now it needs none. |

## D3D12 review (no code)

`D3D12Device`, `CommandList`, `Pipeline`, `Bindings`, `Swapchain` and
`WindowUi` map one to one onto the Vulkan implementation:
- explicit barriers through `ResourceState`;
- two submission slots with fences;
- descriptor sets → descriptor heaps.

The frontend is complete without changes. Things to know before writing it:
- OpenGL-convention rows: the present is a flipped blit, so D3D's top-left
  origin is handled the way Vulkan's is.
- Depth is D24S8.
- HLSL can come from the same SPIR-V via SPIRV-Cross, or DXC from GLSL.

## Contract frozen

With this phase the RHI contract stands as the base for adding Metal or D3D12:
- `GraphicsDevice`, `CommandList`, `Pipeline`, `Swapchain`, `WindowUi`;
- the `SceneBackend` factory;
- `SceneShaderSet` / `SceneProgram`.
