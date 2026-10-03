# Fruity-Prime C++ OpenGL RHI Phase 0 Baseline
## Baseline measurement and golden-capture contract

- Repository: `Zection6V/Fruity-Prime`
- Branch: `develop3_rendering`
- Phase: **0 only**
- Phase 0 gate: **COMPLETE — all 19 golden-capture conditions are source-fixed; no golden PNG is claimed as captured in this audit**
- Plan baseline SHA: `bb8f619da7abbe614ea60765006f290a60938f98`
- Branch HEAD immediately before Phase 0 documentation: `92b2734da568593de4be809cc40efd1039486ea0`
- Initial Phase 0 documentation commit: `08c8a6c8b405b44398bb261533db34ed11737873`
- Plan: `docs/todo/Fruity-Prime-CPP-OpenGL-Vulkan-RHI-Phase-Plan-2026-09-28.md`
- Renderer behavior changes in this phase: **none**
- Runtime/golden images captured in this phase: **none**

The Phase 0 baseline required by the plan remains
`bb8f619da7abbe614ea60765006f290a60938f98`. Immediately before Phase 0 documentation,
`develop3_rendering` was at `92b2734da568593de4be809cc40efd1039486ea0`, seven commits
ahead of that baseline. A GitHub compare from `bb8f619...` to `92b2734...` shows only
documentation moves/additions and a Codex skill file; no `src/MphRead.Native` file changed.
The branch-start renderer blobs therefore remain source-identical to the plan baseline:

- `src/MphRead.Native/Renderer.cpp`: `de9de1ebe2d08bdbc0de77cf7b9148ed7df11bc0`
- `src/MphRead.Native/Renderer.hpp`: `534a56a813dcf1fd833c065ffefa888ebb6929a2`

Accordingly, **`bb8f619...` is the recorded Phase 0 baseline SHA** and `92b2734...` is recorded
separately as the branch HEAD from which the documentation-only Phase 0 delivery began.

---

## 1. Fresh OpenGL dependency inventory

### 1.1 Search method

The plan specifies these repository-wide searches:

```bash
rg -n "\bGL::" src/MphRead.Native
rg -n "GL::Begin|GL::End" src/MphRead.Native
rg -n "GenLists|NewList|CallList|DeleteLists" src/MphRead.Native
rg -n "gl_Vertex|gl_Normal|gl_Color|gl_MultiTexCoord" src/MphRead.Native
```

The first expression above contains **one literal backslash** before `b`; `\b` is the regular
expression word boundary. The initial Phase 0 document incorrectly rendered that command with two
literal backslashes and only counted a graphics-focused subset. The post-push audit corrected both
issues.

No clone was used for this task. The complete repository-wide `rg` result from the independent
post-push audit was cross-checked path-by-path against the GitHub-visible `develop3_rendering`
blobs; all 16 matching paths and their counts are recorded below. Because
`bb8f619... -> 92b2734...` changes no `src/MphRead.Native` file, these counts apply to both the
plan baseline source and the branch-start source snapshot.

Counts below distinguish **occurrences** from **matching lines**. `rg -n` reports matching lines;
occurrence counts are also retained because the plan's earlier approximate `GL::` figure was an
occurrence/call count.

The literal `GL::Begin|GL::End` expression also matches the `GL::EndList` prefix. Therefore both
the literal-plan result and exact immediate-mode call counts are recorded.

### 1.2 Verified repository-wide counts

| Search | Verified occurrences | Matching lines | Files with matches | Notes |
|---|---:|---:|---:|---|
| `\bGL::` | **1729** | **1098** | **16** | Complete repository-wide qualified GL-use inventory |
| `GL::Begin|GL::End` | **76** | **76** | **5** | Includes one `GL::EndList` prefix match |
| exact `GL::Begin\b` | **39** | **39** | **5** | Immediate mode only |
| exact `GL::End\b` | **36** | **36** | **5** | Immediate mode only |
| `GenLists|NewList|CallList|DeleteLists` | **27** | **27** | **5** | Renderer use plus OpenTK/GLES declarations/implementation |
| `gl_Vertex|gl_Normal|gl_Color|gl_MultiTexCoord` | **16** | **16** | **2** | 15 shader-source uses + 1 Skia compatibility-profile comment |

### 1.3 Qualified `GL::` distribution and plan classification

| Plan category | Path | Occurrences | Matching lines |
|---|---|---:|---:|
| game renderer direct dependency | `src/MphRead.Native/Renderer.cpp` | 1104 | 686 |
| game renderer direct dependency | `src/MphRead.Native/Mods/Render/UiOverlay.cpp` | 111 | 65 |
| game renderer direct dependency | `src/MphRead.Native/Mods/Render/PreviewPass.cpp` | 39 | 25 |
| game renderer direct dependency | `src/MphRead.Native/Mods/Render/LauncherNoise.cpp` | 40 | 20 |
| game renderer direct dependency | `src/MphRead.Native/Mods/Render/LauncherPhoto.cpp` | 156 | 96 |
| game renderer direct dependency | `src/MphRead.Native/Formats/Movie.cpp` | 64 | 41 |
| game renderer direct dependency | `src/MphRead.Native/Export/Images.cpp` | 3 | 3 |
| **game renderer subtotal** |  | **1517** | **936** |
| Skia GL interop | `src/MphRead.Native/NativeRuntime/Skia/SkiaGpu.cpp` | 114 | 82 |
| **Skia GL interop subtotal** |  | **114** | **82** |
| diagnostics | `src/MphRead.Native/Mods/Diagnostics/ThumbnailWindowCheck.cpp` | 51 | 34 |
| diagnostics | `src/MphRead.Native/Mods/Diagnostics/LauncherWindowCheck.cpp` | 27 | 26 |
| diagnostics | `src/MphRead.Native/Mods/MapGen/AltFormProbe.cpp` | 1 | 1 |
| diagnostics | `src/MphRead.Native/Mods/Network/MapAudit.cpp` | 1 | 1 |
| diagnostics | `src/MphRead.Native/Mods/Network/NetCheckClient.cpp` | 1 | 1 |
| diagnostics | `src/MphRead.Native/Mods/Network/WeaponDps.cpp` | 1 | 1 |
| diagnostics | `src/MphRead.Native/Mods/ThumbnailCapture.cpp` | 1 | 1 |
| diagnostics | `src/MphRead.Native/Mods/ScreenCapture.cpp` | 15 | 15 |
| **diagnostics subtotal** |  | **98** | **80** |
| OpenGL backend candidates | OpenTK/GL, GlEs, DesktopGlContext, GlNames under this qualified-call query | **0** | **0** |
| **grand total** |  | **1729** | **1098** |

The diagnostics classification includes bounded checks, probes, audits and capture utilities that
directly touch OpenGL but are not part of the ordinary scene/UI render path. `Export/Images.cpp`
is kept under the game-renderer dependency category because Scene recording reaches its readback
from `Scene::AfterRenderFrame`; `Formats/Movie.cpp` implements live movie texture upload/draw.

The two files omitted by the initial audit that also change the immediate-mode total are:

- `Formats/Movie.cpp`: 2 exact `GL::Begin`, 2 exact `GL::End`.
- `Mods/Diagnostics/ThumbnailWindowCheck.cpp`: 1 exact `GL::Begin`, 1 exact `GL::End`.

Display-list symbols are split separately:

- Game renderer: `Renderer.cpp` = 7 occurrences.
- OpenGL/OpenGLES compatibility/backend candidates:
  - `NativeRuntime/OpenTK/GL.cpp` = 8
  - `NativeRuntime/OpenTK/GL.hpp` = 4
  - `Mods/Render/GlEs.cpp` = 4
  - `Mods/Render/GlEs.hpp` = 4
- Total = **27**.

Legacy GLSL built-ins are in `Shaders.cpp`: `gl_Vertex` 4, `gl_Normal` 2,
`gl_Color` 2, and `gl_MultiTexCoord` 7 (15 total). The 16th repository match is the comment
at `NativeRuntime/Skia/SkiaGpu.cpp:489` noting that compatibility-profile attribute 0 aliases
`gl_Vertex`.

Important source anchors:

- `Renderer.cpp:1018-1062`: display-list generation and immediate-mode display-list compilation.
- `Renderer.cpp:1957-2077`: main scene pass sequence.
- `Formats/Movie.cpp:3776-3782,4021-4050`: live movie texture upload/draw OpenGL dependency.
- `Mods/Diagnostics/ThumbnailWindowCheck.cpp:45-92`: diagnostic GL/FBO/readback path.
- `Mods/Diagnostics/LauncherWindowCheck.cpp:25-154`: launcher GL diagnostics.
- `Shaders.cpp:46-87,238-240,303-304`: compatibility GLSL built-ins.
- `NativeRuntime/Skia/SkiaGpu.cpp:489`: compatibility-profile `gl_Vertex` comment.

---

## 2. GPU resource ownership

Ownership here means the code responsible for allocating and deleting the GL object. A stored
binding/list ID in another object is treated as a non-owning reference unless that code performs
the deletion.

| Resource | Owner / storage | Allocation / acquisition | Release / lifetime evidence |
|---|---|---|---|
| Main shader program | `Scene::_shaderProgramId` | `Renderer.cpp:796` | `Scene::UnloadGl`; delete helper at `Renderer.cpp:3686-3693`, invoked for all four program IDs at `Renderer.cpp:3707-3710` |
| RTT shader program | `Scene::_rttShaderProgramId` | `Renderer.cpp:826` | `Scene::UnloadGl` |
| Shift/whiteout shader program | `Scene::_shiftShaderProgramId` | `Renderer.cpp:850` | `Scene::UnloadGl` |
| Cel shader program | `Scene::_celShaderProgramId` | `Renderer.cpp:866` | `Scene::UnloadGl` |
| Scene framebuffer | `Scene::_frameBuffer` | `Renderer.cpp:875` | deleted/reset at `Renderer.cpp:3662-3666` |
| Screen/RTT color texture | `Scene::_screenTexture` | `Renderer.cpp:877` | deleted by `UnloadGl` texture helper |
| Depth/stencil renderbuffer | `Scene::_renderBuffer` | `Renderer.cpp:906` | deleted/reset at `Renderer.cpp:3673-3677` |
| Cel copy texture | `Scene::_celTexture` | `Renderer.cpp:892` | deleted by `UnloadGl` texture helper |
| Sampleable depth texture | `Scene::_depthTexture` | lazily created at `Renderer.cpp:1767` when cel edge requires depth sampling | detached/deleted when not wanted or unsupported; final cleanup in `UnloadGl` |
| Cel framebuffer | `Scene::_celFrameBuffer` | lazy `GenFramebuffer` at `Renderer.cpp:1851` | deleted/reset at `Renderer.cpp:3667-3671` |
| Display lists | `Scene::_displayLists`; meshes hold non-owning `ListId` | `GenLists` and set insertion at `Renderer.cpp:1045-1046` | `DeleteLists` in `UnloadGl`; matching mesh `ListId` values reset before set clear |
| Model textures | `Scene::_texPalMap` + `Scene::_ownedTextures` | `InitTextures` starts at `Renderer.cpp:1254`; generated binding inserted into `_ownedTextures` at `Renderer.cpp:1377` | map bindings deleted first, remaining owned textures then deleted in `UnloadGl` |
| Generic/HUD CPU-image textures | `Scene::_ownedTextures` | `Scene::BindGetTexture(data,...)` inserts at `Renderer.cpp:1407` | deleted through `Scene::_ownedTextures` in `UnloadGl` |
| HUD object texture IDs | non-owning IDs in `HudObjectInstance::BindingId` / player HUD fields | `HudInfo.cpp:448-454` requests texture allocation from `Scene`; `PlayerHud.cpp:224-251` caches returned IDs | actual texture remains Scene-owned |
| HUD layer texture IDs | non-owning `LayerInfo::BindingId` | assigned from player HUD bindings, e.g. `PlayerHud.cpp:760,830-852` | actual texture remains Scene-owned |
| HUD mask texture | non-owning `LayerInfo::MaskId` | scan HUD aliases the scan texture at `PlayerHud.cpp:841-842`; sampled as texture unit 1 at `Renderer.cpp:2046-2057` | same Scene-owned texture lifetime; `MaskId` is not a second allocation |
| Movie textures | Scene-side movie bindings | maintained by renderer movie path | explicitly deleted in `Renderer.cpp:3694-3703` |

Primary ownership fields are visible in `Renderer.hpp:1035-1041` and
`Renderer.hpp:1076-1083`. `Scene::UnloadGl` begins at `Renderer.cpp:3608` and is the central
destruction boundary for Scene-owned GL resources.

---

## 3. Actual frame order

The frame order is traced from the current `RenderWindow::OnRenderFrame` implementation, not
inferred from the plan.

### 3.1 Scene-active path

1. **Simulation**: zero or more fixed simulation steps,
   `Renderer.cpp:6126-6128`.
2. **Scene::OnDrawFrame**: `Renderer.cpp:6146`.
   - locks the scene gate;
   - binds the offscreen scene framebuffer;
   - resizes attachments if required;
   - selects the main shader;
   - processes load/unload;
   - clears/recycles prior render-item queues;
   - updates camera/projection;
   - collects the current draw items.
   Source: `Renderer.cpp:1601-1639`.
3. **Scene::OnRenderFrame**: entered at `Renderer.cpp:6147`, implementation
   `Renderer.cpp:1957-2077`.
4. **Skia Shell::TickUi**: `Renderer.cpp:6156`.
5. **UiOverlay composite/draw**: `Renderer.cpp:6158`.
6. **LauncherHunter draw**: `Renderer.cpp:6159`.
7. **Shell::AfterDraw**: immediately after LauncherHunter.
8. **Present / SwapBuffers**: `Renderer.cpp:6162`.
9. **Reveal + pause-menu polling**.
10. **Scene::AfterRenderFrame**: `Renderer.cpp:6165`; recording capture, when enabled, is
    performed here before frame-advance state is cleared (`Renderer.cpp:1741-1752`).
11. Base window render-frame callback.

The source explicitly states at `Renderer.cpp:6151-6155` that Ganesh must not run before the
game OpenGL scene and that game map/model/HUD rendering remains on the existing OpenGL path.

### 3.2 Launcher-only path

When `_scene == nullptr`, the order differs deliberately:

`Shell::TickUi -> UiOverlay::DrawAlone -> Shell::AfterDraw -> SwapBuffers`

at `Renderer.cpp:6053-6066`. There is no Scene pass and no LauncherHunter call in that branch.

---

## 4. Exact render-pass semantics

`Scene::OnRenderFrame` currently implements these semantics in order:

1. **Frame clear** (`Renderer.cpp:1961`): clear color, depth, and stencil; stencil clear value is 0.
2. **Opaque/main pass** (`Renderer.cpp:1966-1975`):
   - color writes enabled;
   - alpha test `Equal 1.0`;
   - depth function `Less`, depth writes enabled;
   - stencil enabled but configured with zero operations;
   - render all `_nonDecalItems`.
3. **Decal pass** (`Renderer.cpp:1976-1982`):
   - alpha test disabled;
   - polygon offset fill enabled with `(-1,-1)`;
   - depth function `Lequal`;
   - alpha blending `SrcAlpha / OneMinusSrcAlpha`;
   - render `_decalItems`;
   - polygon offset restored/disabled.
4. **Translucent stencil pre-pass** (`Renderer.cpp:1983-1989`):
   - alpha test `Less 1.0`;
   - **color mask disabled**;
   - stencil operation replaces on depth pass;
   - each translucent item uses `StencilFunc(Greater, polygonId, 0xFF)`.
5. **Mid-frame depth clear** (`Renderer.cpp:1990`).
6. **Opaque depth rebuild** (`Renderer.cpp:1991-1994`):
   - color remains masked off;
   - stencil no longer writes;
   - alpha returns to `Equal 1.0`;
   - `_nonDecalItems` are rendered again to rebuild opaque depth.
7. **Translucent color pass** (`Renderer.cpp:1994-2003`):
   - color writes re-enabled;
   - depth writes disabled; depth test `Lequal`;
   - translucent items are rendered once with stencil `Notequal polygonId`, then again with
     stencil `Equal polygonId`.
8. **State restore**: depth writes on; alpha/stencil tests off; polygon mode fill.
9. **Preview pass**: `ModDrawPreview()` at `Renderer.cpp:2006`.
10. **HUD model pass**: player HUD models, or scoreboard-over-free-camera HUD models,
    `Renderer.cpp:2008-2014`.
11. **Cel outline**: `DrawCelOutline()` at `Renderer.cpp:2016`.
    - copies scene color to the cel texture;
    - samples both copied color and sampleable depth;
    - draws the cel quad into the cel framebuffer whose color attachment aliases
      `_screenTexture` (`Renderer.cpp:1834-1882`).
12. **RTT/shift/whiteout setup** (`Renderer.cpp:2017-2031`): selects normal RTT shader or the
    disruption/whiteout shift shader and sets its frame/factor uniforms.
13. **RTT composite to window framebuffer** (`Renderer.cpp:2032-2039`):
    - bind framebuffer 0;
    - set window viewport;
    - clear color;
    - disable depth, enable blend;
    - bind `_screenTexture`;
    - draw a fullscreen triangle strip.
14. **HUD layers and HUD objects** (`Renderer.cpp:2041-2060`):
    - optional pause background;
    - layers 4,3,1,2,5 in that order;
    - optional layer-1 mask sampled on texture unit 1;
    - HUD objects;
    - optional pause foreground.
15. **Debug/movie overlays**: aim-assist debug and optional movie frame
    (`Renderer.cpp:2061-2062`).
16. **Fade pass** (`Renderer.cpp:2063-2073`): if an active player-camera fade has non-zero
    opacity, set fade color and draw a fullscreen triangle strip.
17. **Final GL state normalization**, then return to `RenderWindow` for Skia/UI and present.

Render-item classification is source-defined in `Renderer.cpp:3246-3267`:
decals go to `_decalItems`; everything else goes to `_nonDecalItems`; translucent render mode
or alpha below 1.0 also enters `_translucentItems`. A translucent item can therefore also be in
the non-decal list, which is why backend migration must preserve the existing multi-pass semantics
rather than model these vectors as mutually exclusive pass buckets.

---

## 5. Golden-image candidates and capture contract

### 5.1 Fixed common contracts

No golden PNG was captured during this implementation/audit. Phase 0 fixes the conditions and
provides an opt-in helper; executing the helper later creates the reference image plus a text
manifest. A capture is valid only when the helper reports success.

The existing deterministic anchors remain:

- UI states: the exact `UiCapture` named views at **940 x 528**, camera **N/A**.
- Static scene/cel anchor: `TEST ARENA`, Samus recolor 0, Battle mode, **1600 x 900**,
  preview camera position **(0,16,30)** targeting **(0,1,0)**.
- Fog anchor: `DUST2`, **1600 x 900**, camera **(20,24,33)** targeting **(-8,-6,-12)**,
  map fog color **(25,22,16)**, slope **3**, offset **65100**.

The seven previously blocked scene candidates now use
`Mods/Render/GoldenCapture.hpp/.cpp`. This is a Phase-0-only, explicit CLI harness; there is no
normal gameplay call site.

Common debug-helper contract:

```text
map                 TEST ARENA
mode                Battle
hunter/recolor      Samus / 0
required framebuffer 1600x900
required client size 1600x900
required Scene size  1600x900
warm-up             exactly 12 simulation/draw updates
capture update      exactly update ordinal 13
injection point     after OnDrawFrame on ordinal 13, immediately before OnRenderFrame
camera position     (0,16,30)
camera target       (0,1,0)
FOV                 78 degrees
resolution scale    100%
lighting            on
cel shading         off
cel bands           8
cel edge            0.5
fog                 off
texture filtering   off
```

The 12-update warm-up is not an arbitrary new number: it is the existing
`ThumbnailCapture::SettleFrames` contract. The helper makes the following update the sole accepted
capture update. If framebuffer, client, or Scene dimensions differ from 1600x900 (including
DPI-induced framebuffer drift), the helper fails instead of accepting another size or later frame.

Invocation:

```text
FruityPrime -mapdir maps -goldencapture all -goldendir <output-directory>

FruityPrime -mapdir maps -goldencapture transparent-object -goldendir <output-directory>
FruityPrime -mapdir maps -goldencapture decal -goldendir <output-directory>
FruityPrime -mapdir maps -goldencapture particle -goldendir <output-directory>
FruityPrime -mapdir maps -goldencapture trail -goldendir <output-directory>
FruityPrime -mapdir maps -goldencapture hud -goldendir <output-directory>
FruityPrime -mapdir maps -goldencapture fade -goldendir <output-directory>
FruityPrime -mapdir maps -goldencapture whiteout-disruption -goldendir <output-directory>
```

Each successful invocation writes `<candidate>.png` and `<candidate>.txt`. The manifest records
the requested and actual dimensions, exact camera, update ordinal, render settings, trigger/injection
point, fixture/state description, and whether image capture succeeded.

### 5.2 Resolved deterministic fixtures/states

Four cases intentionally use **synthetic geometry fixtures** because the production entity/effect
generators do not guarantee a named visible object at a source-fixed frame. The fixtures are
inserted through the normal `Scene::AddRenderItem` classification and are rendered by the
production `Scene::OnRenderFrame` pass implementation.

- **Transparent object**: synthetic `RenderItemType::Quad`,
  `RenderMode::Translucent`, alpha **0.45**, cyan override; vertices
  `(-2,2,0),(2,2,0),(2,6,0),(-2,6,0)`.
- **Decal**: synthetic `RenderItemType::Quad`, `RenderMode::Decal`, alpha **1.0**,
  orange/red override; unobstructed TEST ARENA floor patch in front of the central block, vertices
  `(-4,0.02,6),(4,0.02,6),(4,0.02,10),(-4,0.02,10)`.
- **Particle**: synthetic `RenderItemType::Particle`, translucent alpha **0.85**,
  Scene-owned deterministic 2x2 cyan/white checker texture, upright quad at z=0.
- **Trail**: synthetic `RenderItemType::TrailMulti`, translucent alpha **0.90**,
  the same deterministic Scene-owned texture, 8 strip vertices from x=-4 to x=4 around
  y=3.2..4.8,z=0.

These four captures validate the production RenderItem classification, stencil/depth/blend pass
routing, material/texture setup, and the production quad/particle/trail draw routines. They
**do not** claim to validate ObjectEntity material extraction, decal creation logic, effect
particle generation/lifetime, or Lockjaw trail generation/noise. Those upstream generators remain
separate runtime/audit concerns.

The remaining three cases use production final render stages, with only the state made deterministic:

- **HUD**: real Samus HUD rendering. A debug-only player-camera override copies the canonical
  TEST ARENA transform into `CameraInfo`, sets CameraMode to Player and FOV to 78, then the
  unchanged HUD-model/layer/object path executes. This validates HUD composition, not natural
  first-person camera-placement calculation.
- **Fade**: real production fade pass with a fixed `FadeOutWhite` render state:
  color **1.0**, percent **0.5**, applied after `OnDrawFrame` on update 13 so
  `UpdateFade` cannot advance it before the capture.
- **Whiteout/disruption**: real shift/whiteout post-process with fixed state:
  disruption state **2**, disruption factor **0.75**, whiteout state **1**,
  whiteout factor **1.0**, whiteout amount **48**, Scene elapsed time **0.25 s**.
  The helper calls the production `UpdateWhiteoutTable(48)` from a PlayerEntity member before
  the unchanged shift shader path executes.

None of these methods is called during ordinary startup, simulation, drawing, or presentation.
They are reachable only through the explicit `-goldencapture` CLI path.

### 5.3 All Phase 0 candidates

| Candidate | Exact reproducible condition | Camera | Status |
|---|---|---|---|
| Launcher | live launcher equivalent of `UiCapture` `start`, 940x528, fresh `MenuSettings`, clean launcher preferences | N/A | **fixed** |
| Offline map | live launcher equivalent of `play-offline`, 940x528; fresh Combat Hall settings; clean prefs => Samus/suit 0/bots 3/Normal | N/A | **fixed** |
| Hunter | live launcher equivalent of `end-panel-hunter`, 940x528, clean prefs/no active player choice => Samus/suit 0 | N/A | **fixed** |
| Room geometry | `TEST ARENA`, 1600x900, 12 warm-ups, capture update 13, `-cel off -fog off` | (0,16,30) -> (0,1,0) | **fixed** |
| Transparent object | `-goldencapture transparent-object`; synthetic translucent quad above; injected after draw collection on update 13 | preview (0,16,30) -> (0,1,0) | **fixed; synthetic fixture** |
| Decal | `-goldencapture decal`; synthetic floor decal above; injected after draw collection on update 13 | preview (0,16,30) -> (0,1,0) | **fixed; synthetic fixture** |
| Particle | `-goldencapture particle`; synthetic production Particle RenderItem + deterministic 2x2 texture on update 13 | preview (0,16,30) -> (0,1,0) | **fixed; synthetic fixture** |
| Trail | `-goldencapture trail`; synthetic production TrailMulti RenderItem + deterministic 2x2 texture on update 13 | preview (0,16,30) -> (0,1,0) | **fixed; synthetic fixture** |
| HUD | `-goldencapture hud`; Samus production HUD with debug-fixed Player camera on update 13 | Player override (0,16,30) -> (0,1,0), FOV 78 | **fixed** |
| Pause menu | live UI equivalent of `UiCapture` `pausemenu`, 940x528, `PauseMenuView(true)`, no active network vote | N/A | **fixed** |
| Map Vote | live UI equivalent of `UiCapture` `play-vote`, 940x528, `Face::Vote`, `overGame=true`, fresh Combat Hall settings | N/A | **fixed** |
| Cel off | TEST ARENA common static anchor, `-cel off -celbands 8 -celedge 50` | (0,16,30) -> (0,1,0) | **fixed** |
| Cel on | identical shot, `-cel on -celbands 8 -celedge 0` | (0,16,30) -> (0,1,0) | **fixed** |
| Cel outline | identical shot, `-cel on -celbands 8 -celedge 50` | (0,16,30) -> (0,1,0) | **fixed** |
| Fog on | DUST2 common fog anchor, `-cel off -fog on` | (20,24,33) -> (-8,-6,-12) | **fixed** |
| Fog off | identical DUST2 shot, `-cel off -fog off` | (20,24,33) -> (-8,-6,-12) | **fixed** |
| Fade | `-goldencapture fade`; production FadeOutWhite, color 1.0, percent 0.5 on update 13 | Player override (0,16,30) -> (0,1,0), FOV 78 | **fixed** |
| Whiteout/disruption | `-goldencapture whiteout-disruption`; disruption 2/0.75 + whiteout 1/1.0/amount 48 + elapsed 0.25 s on update 13 | Player override (0,16,30) -> (0,1,0), FOV 78 | **fixed** |
| End screen | live UI equivalent of `UiCapture` `end-panel`, 940x528, freshly constructed `EndPanelView` | N/A | **fixed** |

### 5.4 Capture truthfulness and Phase 0 gate

The conditions for all **19/19** minimum candidates are now fixed in source/documentation.

No golden image was captured while implementing or auditing this helper. Therefore this document
does **not** claim that any PNG exists, that the game was launched for golden capture, or that a
pixel comparison was performed. The helper is the reproducible contract for producing those images.

Phase 0's golden-condition requirement is satisfied when the helper compiles and the pushed source
passes the required validation gates. Phase 1 remains out of scope.

---

## 6. Validation and evidence

### Static/source validation

- Target baseline document was confirmed absent before initial creation.
- The plan baseline is `bb8f619da7abbe614ea60765006f290a60938f98`.
- `develop3_rendering` was re-read immediately before initial writing and remained at
  `92b2734da568593de4be809cc40efd1039486ea0`.
- Current Renderer blobs at that branch-start SHA match the plan baseline exactly.
- `bb8f619... -> 92b2734...` changes no `src/MphRead.Native` file.
- The post-push inventory audit corrected the first search to the exact single-backslash
  `rg -n "\bGL::" src/MphRead.Native` expression and records all 16 matching files.
- Frame order and pass semantics were traced from current branch source.
- Resource ownership was traced from current allocation/storage/destruction code.

### Build evidence

The Phase 0 helper source was compile-fixed at
`416452d633d326429d82184ec2bf5bbf2b063897` by qualifying the fixture allocation as
`std::shared_ptr<::MphRead::RenderItem>`; this removes the nested-name lookup collision with
`Scene::RenderItem`.

Independent local Windows MinGW verification succeeded with:

```text
cmake --build tools/build/out/msys2-mingw64-RelWithDebInfo --target fruity_prime --parallel 8
```

Exact-head native GitHub Actions verification for `416452d...` also completed successfully:

- Native C++ Windows / MSVC: run `36423503365`
- Native C++ Linux / GCC: run `36423503342`
- Native C++ macOS / Clang: run `36423503345`
- Native C++ Android: run `36423503249`
  - Android native build contract: success
  - arm64-v8a NDK build: success
  - x86_64 NDK build: success

The general exact-head `build` workflow also completed successfully:

- pull-request run `36423503284`: success
- push run `36423357905`, attempt 2: success

The native workflows normally auto-run only for `develop2`. For Phase 0 verification, a temporary
draft pull request from `develop3_rendering` to its ancestor `develop2` was used to invoke those
existing native gates; the recorded workflow `head_sha` is exactly `416452d...`.

### Runtime / golden capture validation

- Game/runtime launch: **not performed**.
- OpenGL runtime visual inspection: **not performed**.
- Golden screenshots: **not captured**.
- Pixel comparison: **not performed**.

No runtime image result is implied by the static/source and CI evidence above. The opt-in golden helper fixes the seven formerly blocked conditions but was not executed for image capture during this audit.

---

## 7. Phase 0 Definition of Done

- [x] Baseline SHA recorded.
- [x] Fresh OpenGL dependency counts recorded and categorized.
- [x] Actual frame order traced and recorded.
- [x] GPU resource ownership table completed.
- [x] Golden-image candidate set and reproducible capture contract fixed — all 19 minimum candidates have explicit dimensions/state/camera/timing conditions.
- [x] Normal renderer behavior unchanged by Phase 0: Renderer.cpp/Renderer.hpp remain unchanged; new debug-only helper methods have no normal-path call site and are reachable only through explicit `-goldencapture` invocation.
- [x] Phase 1 implementation/design not started.

Phase 0 is the boundary. No RHI types, backend factory, Vulkan code, geometry conversion, shader
migration, window/presentation redesign, or normal renderer behavior change belongs in this commit.
