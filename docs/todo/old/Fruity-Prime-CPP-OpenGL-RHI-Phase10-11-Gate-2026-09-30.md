# Native OpenGL RHI Phase 10 and 11 validation

Date: 2026-09-30 (Asia/Tokyo). Branch: `develop3_rendering`.

## Preserved Phase 6 through 9 evidence

The preceding session implemented these phases before the Phase 10 interruption.
Their preserved runtime manifests and decoded PNGs were inspected again on
2026-09-30. The common harness and all four runtime-input hashes match Phase 3;
each set contains seven PNGs and every decoded RGB pixel matches Phase 3.

| Phase | Verified capture source | Output | RGB parity |
|---|---|---|---|
| 6 | `d57f60102bc731858106a08c97e808ae9f859e0f` | `C:/tmp/gp/out-p6` | 7/7 exact |
| 7 | `a0dfe24dac521f254791afa56837e5c4367702f1` | `C:/tmp/gp/out-p7` | 7/7 exact |
| 8 | `6cee585ccfde01de78660e06d845f70bba81a666` | `C:/tmp/gp/out-p8` | 7/7 exact |
| 9 | `ac829441e9e7a5ae7a1f2ba0e21e13ae30ea0c77` | `C:/tmp/gp/out-p9` | 7/7 exact |

Current-source inspection confirms scene textures/views/samplers and pipelines
are RHI-owned, FBO construction and uploads are in OpenGlDevice, and the scene
uses explicit rendering scopes. The pass order and load/store rules are recorded
in `docs/app_design/Fruity-Prime-CPP-Native-Render-Pass-Sequence.md`.
Renderer.cpp, Renderer.hpp, Movie.cpp and PreviewPass.cpp satisfy the Phase 9
frontend audit. The alpha test compares quantized 8-bit alpha; the Phase 7
correction preserved fixed-function behavior in desktop and ES shaders.

This reinspection establishes preserved image parity and current implementation
structure. It does not relabel canceled historical CI runs as successful or
claim that every animated shell image is deterministic. The final integrated
code's exact-SHA CI is recorded below separately.

## Phase 10

Status: COMPLETE. The context-lifetime and local network map-rotation regressions
have been corrected. Integrated exact-SHA CI passed 10/10 at `a6144b61`.
Native source is byte-identical to the runtime-tested `220e900a` (the intervening
commits change the report/plan and compiler-cache workflow only).

Base: `ac829441e9e7a5ae7a1f2ba0e21e13ae30ea0c77`.
Implementation: `d57eb70863e00f3f125e38ee373ac21ce1e70e37`.
Corrections: `3c287b6c46fd0e071d20ae17a623d87d43b0298c`,
`669ef5c6a2a4da6439ed44c40d6ea9cdef20d8e2`.
Further corrections: `5d679ed7db3a37bee7dad817600181bf9aa59e75`,
`f7a8e974c9db50ba304e2323f72f7f364a0c9571`,
`220e900aa5250b9f631b44ece4b58baa2e52d75a`.

The original implementation's preserved capture at `C:/tmp/gp/out-p10`
failed: only the first candidate produced a PNG; subsequent candidates rendered
black. The retirement queue and GLsync objects outlived their desktop GL
context. Before destroying a desktop context, the platform adapter now makes
it current and drains GPU retirement and fences. The unchanged capture harness
then passed all seven candidates at `3c287b6c` against Phase 3 with exact decoded
RGB equality. This comparison establishes the sequential-context regression
and correction, independently of a successful compile.

Frame slots now wait for completion before starting work in the reused slot.
A failed/timed-out wait, or unavailable fence, uses `Finish` to establish real
GPU completion rather than claiming completion. Geometry buffers and depth
renderbuffers are counted separately. The lifetime diagnostic requires every
resource category and the retirement queue to be zero after each scene release;
a constant leak present from cycle one cannot pass merely by remaining steady.

Changed files across the implementation and corrections can be reproduced with:

```powershell
git diff --name-status ac829441e9e7a5ae7a1f2ba0e21e13ae30ea0c77 669ef5c6a2a4da6439ed44c40d6ea9cdef20d8e2
```

Build and checks (MSYS2 MinGW64 environment, CMake Release):

```text
cmake --build tools/build/out/msys2-mingw64-Release -j 6
ctest --test-dir tools/build/out/msys2-mingw64-Release --output-on-failure
python tools/check-phase4-legacy-gl.py
python tools/check-phase5-shader-interface.py
python tools/check-phase9-frontend-gl.py
FruityPrime.exe -gpulifetime "TEST ARENA" -cycles 5 -frames 12
FruityPrime.exe -gpulifetime "MP1 SANCTORUS" -cycles 3 -frames 12 -cel on
FruityPrime.exe -shellshot C:/tmp/gp/shell-p10final
```

Results: CTest 5/5 PASS; all three static audits PASS. Lifetime: 5/5 arena
cycles and 3/3 real-room cel cycles PASS. While drawing, arena used 724 buffers,
653 textures, one depth renderbuffer and one FBO; the real room used 1,628
buffers, 703 textures, one depth renderbuffer and two FBOs. After every release,
all counts and pending retirement were zero. Shellshot returned to a production
hunter preview after the match, observing scene generation 1 to 2.

Preserved local evidence is under `C:/tmp/gp/`: `p10final-lifetime-arena.log`,
`p10final-lifetime-cel.log`, `p10-stats-final-build.log`,
`p10-stats-final-tests.log`, `shell-p10final.log` and `p10fix1-validation.log`.
Final-SHA fresh-build capture: `p10final-r3-capture.log`,
`p10final-r3-parity.log`, `out-p10final-r3`.
At final code SHA `669ef5c6`, the fresh-build runner returned zero and all
seven candidates and their controls passed exact RGB parity. Executable
SHA-256: `db3207599aab8b6e51e6a223593d184784cb86f3e4c452ae2d8e1115cc2abc8a`.
The final-source shell run produced all 24 PNGs and passed the production
model unload/reload/draw probe and the post-match hunter preview reload check.
The additional localhost network run reached the MP1 SANCTORUS to MP3 PROVING
GROUND transition and failed with `GPU mesh cache entry is missing` (client
exit 2). Evidence: `phase10-local-client.log`, `phase10-local-server.log`.
This failed map-change gate was traced to fade completion during uniform upload,
after old-room draw collection. It synchronously loaded the new room and erased
the old GPU mesh cache before submitting the collected old-room draws. Fade
completion now also runs before draw collection. Capture-time injected fade
state is still processed by the existing uniform path; the capture harness and
validation strength remain unchanged.
Two clients then passed the real localhost transition in both directions
(SANCTORUS to PROVING GROUND and back), with both process exit codes zero and
both diagnostic `RESULT: PASS`. Evidence: `phase10-two-{A,B,server}.log`,
`phase10-two-results.log`. This run used the transition correction `5d679ed7`.
Retirement cleanup no longer swallows native-destruction exceptions; WaitIdle
closes the frame. Diagnostic Dispose and RenderWindow destruction explicitly
release scenes while their context is still alive.

Image provenance: use the unmodified runner and validator; the runner requires
a fresh build directory. Phase 3 comparison source is
`13c49e35f2a314662c7cc639e5e56fa784ac8bfd`, with capture output `out-p3`.
Harness SHA-256 is
`1e7daefc29245d8cf9c8b2a5078b2e586527e325f52ed95d7ccfccb2b6d1f387`.
Inputs: `C:/tmp/gp/inputs/paths.txt`, `C:/tmp/gp/inputs/maps2`; working
directory `C:/tmp/gp/cwd`. Validator checks actual runtime input hashes.
This is the established Phase 3 rendering comparison baseline; Phase 0's
baseline document explicitly recorded conditions without capturing PNGs.
Do not describe this as a direct comparison against Phase 0 PNGs. Correction (2026-09-30): Phase 0 DID implement the opt-in capture helper; it
did not execute it or preserve PNGs. The final Phase 0 source is `5d3a0892`,
not the pre-helper baseline `bb8f619d`. After the user clarified this distinction,
a Phase 0 to Phase 3 bridge comparison was started with a common isolated
capture adapter. Existing Phase 3 parity validation remains unchanged.

Renderprobe is an additional visibility smoke test, not a deterministic pixel
oracle: separate simulation/update and drawing schedules change the camera and
animated actors. The `3c287b6c` cel and non-cel sweeps both rendered all eight
spawns and returned exit code zero. Preserve screenshot differences and use
the fixed-frame GoldenCapture for exact parity.

Final verification source is `220e900aa5250b9f631b44ece4b58baa2e52d75a`.
Build and CTest 5/5 PASS; shell produced all 24 PNGs, exit zero, after the exit
lifetime correction. Kanden, four-player SANCTORUS with fog enabled: maptest
exit zero, 4/4 spawned, minimum visible fraction 90.3%; 3,692 main and 3,688
remote node lookup samples agreed. This short render smoke run did not establish
combat or affliction coverage (no shots), and is not described as doing so.
Evidence: `p10-final-exit-{build,tests}.log`, `shell-p10complete.log`,
`p10complete-maptest.log`. Capture and two-client rerun evidence is preserved
as `p10complete-validation.log`, `out-p10complete`, and
`phase10-complete-two-{A,B,server,results}.log`.

At this exact source SHA, the fresh-build capture passed all seven candidates
with exact decoded RGB equality against Phase 3. Executable SHA-256:
`3807f5de6e3f84876025d45289f11c0e1cc8ac70a7331d373243b1318736e80e`.
Both final-source localhost clients reported `RESULT: PASS`, exit zero,
through the two room transitions.

CI for integrated source SHA `a6144b61b7ee77bd9cf82d6bab9f6c4db18bd50e`:
[build_cpp run 36647299171](https://github.com/Zection6V/Fruity-Prime/actions/runs/36647299171),
10/10 PASS including Windows/MSVC, Linux/GCC, macOS/Clang, Android contract,
both NDK ABIs, APK and all three static audits. Run `36644549244` was canceled
by the subsequent compiler-cache update. Earlier runs were cancelled by normal push/PR concurrency when
required corrections were pushed. A cancelled run is not counted as a pass.
Original implementation CI `36613750411` passed, but does not validate these
corrections and does not establish runtime parity.

## Phase 0 bridge comparison

The original pre-helper baseline `bb8f619d` is not the completed Phase 0
capture revision. The completed Phase 0 source is
`5d3a0892aa849994864c27cd9fd5184eb8b92e65`.
Both that source and Phase 3 were captured with one byte-identical isolated
adapter, composite SHA-256
`2ac95f8e4253f18183a9b2ef04fc470480737e779d7dd9498782dac8d933ac15`.
The adapter adds a conditional pre-RHI window presentation API and recognizes
exactly these two immutable hook-free revisions. Production renderer files
are unchanged; only the three allowed capture files are overlaid. The normal
Phase 3/final-source validator and its stronger production-hook checks remain
unchanged.

The bridge verifier reuses the tracked validator's source-checkout, harness,
runtime provenance, fixture/control, fade postcondition, half-white and decoded
RGB checks. All four runtime-input hashes match. All seven candidates passed
exact RGB equality from Phase 0 to the new Phase 3 captures, and the new Phase 3
captures also match the preserved Phase 3 pixels used by the main parity gate.

Evidence: `C:/tmp/gp/phase0-bridge-validation.log`, `out-p0bridge`,
`out-p3bridge`, `p0bridge-capture.log`, `p3bridge-capture.log`.
Phase 0 executable SHA-256:
`e674c3bf491dfd493c7d9943ef2dbaf21ee459912958a783b8d5e0c75d906d33`.
Phase 3 bridge executable SHA-256:
`abe5e61e214f7597cfb99af7e38a20fcdd0de6000e107c35ed44fe9678c8b137`.
Reproduction wrappers: `C:/tmp/gp/phase0-bridge-runner.py` and
`C:/tmp/gp/validate-phase0-bridge.py`.

Supplemental static UI comparison: both sources were run with `-uishot` using
identical paths.txt, mapdir and working directory. Phase 0 and runtime-tested
Phase 10 both returned zero and produced 26/26 matching RGBA images, including
launcher, offline/online picker, hunter, pause, Map Vote and end panel. These
are layout captures, not proof of a live server vote or rendered game transition.
Evidence: `ui-p0bridge`, `ui-p10bridge`, and their `.log`/`.err` files under
`C:/tmp/gp/`. The live runtime gates remain separately required.

## Phase 11

Final audit also reconciles the stale Phase 0–3 plan checkboxes. Phase 0's
baseline document already records its complete inventory, ownership, frame
order, and 19 fixed capture conditions. Common RHI headers compile and expose
no raw backend handle types; unavailable Metal/D3D12/Vulkan backends explicitly
reject creation rather than supplying dummy implementations. A temporary
standalone probe linked against the current native library creates/destroys
a hidden NoApi window and asserts actual GLFW_CLIENT_API=GLFW_NO_API, exit zero
(`C:/tmp/gp/noapi-gate.cpp`, `noapi-build.log`, `noapi-runtime.log`).
RendererGeometry tests cover source topology, all vertex encodings, inherited
attributes, matrix restore and decoder state; the current CTest suite passes.
The Phase 0/3/final capture chain above verifies retained OpenGL pixels.

Final verification source: `5e3c3275c96d588e42cb626b890c0a6293d8af71`.
Phase 11 is COMPLETE. Exact-source
[CI run 36666551184](https://github.com/Zection6V/Fruity-Prime/actions/runs/36666551184)
completed successfully: 11/11 jobs including Windows/MSVC, Linux/GCC,
macOS/Clang, Android contract, both NDK ABIs, APK and all four static audits.
The completion delivery changes only this report and the plan; native sources
remain identical to the tested CI commit. Desktop runtime evidence is Windows;
Android evidence here is compile/package verification, not device gameplay.
Images screenshot/record readback now uses RHI commands. Launcher photo/noise
GL ownership is contained in the OpenGL backend; Skia interop and diagnostics
are explicitly classified. The tracked boundary audit scans 1,048 C++ files:
A=601, B=250, C=122, D=0. Its two regression tests pass. Earlier Phase 4/5/9
audits also pass. MinGW Release builds and CTest passes 5/5.

Fresh final-source Golden Capture passes 7/7 exact decoded RGB comparisons
against preserved Phase 3. The Phase 0 bridge above establishes the same seven
images against the completed Phase 0 baseline. Evidence:
`C:/tmp/gp/out-p11verified`, `p11verified-validation.log`.
Final capture executable SHA-256:
`dc95a45b309722921288011f20a4697537f088d8465690cbe5d4fec55c74b065`.

Final-source shellshot exits zero and writes all 28 PNGs, covering launcher,
offline game, pause, end screen, hunter change, fullscreen, resize, and actual
minimize/restore in both launcher and match. Its RHI screenshot/record test
passes exact RGB, vertical orientation, and odd-width row packing (641x127).
The recording verifier waits for the asynchronous PNG writer to release its
Windows file handle before decoding; it still rejects incomplete/wrong pixels.
Evidence: `C:/tmp/gp/p11-delivery-shell` and its `.log`/`.err` files.

SANCTORUS with cel/fog ON passes GPU lifetime 3/3, exit zero; every resource
category and retirement count reaches zero after each scene release.
TRANSFER LOCK DM with Kanden and four players passes render smoke runs with
cel/fog ON and OFF, both exit zero. HUD is visible in both. These are rendering
and transition checks, not combat coverage. Evidence: `p11verified-lifetime.log`,
`p11-fog-cel-on`, `p11-fog-cel-off` and corresponding logs under `C:/tmp/gp/`.
An additional cel-OFF/fog-ON run exits zero, isolating the fog toggle from cel;
evidence: `p11-fog-only-on` and its log. The maptest initial pre-draw frame is
reported black and skipped by the existing capture harness; subsequent scene
captures render normally. This initial sample is not counted as a visible frame.

Live two-client Map Vote checks each observe two votes cast and two agreed by
the server, an end-screen vote list, and SANCTORUS to TRANSFER LOCK BT change.
These satisfy the plan's online/local rendering, map-change and Map Vote checks;
the Phase 11 plan does not require taking hits or combat feature coverage.
Those vote-focused runs return overall FAIL because the scripted players do
not hit opponents; their overall result is not claimed as PASS. Evidence:
`phase11-two-{A,B,server,results}.log` and `phase11-vote-shots` under `C:/tmp/gp/`.
The final-source non-vote two-client rerun also connects, renders remote players,
and changes rooms without position/form disagreement. Both clients return 1
solely for never taking a hit; this is likewise not a full combat PASS.
Evidence: `p11verified-two-{A,B,server,results}.log` under `C:/tmp/gp/`.

Supplemental final UI captures match Phase 0 exactly in 21/26 images. The five
remaining images differ in animated portraits/backdrops/thumbnails; repeating
Phase 0 itself also produces these differences (offline picker: 85,784 pixels,
vote picker: 79,713). This is recorded separately from the exact seven-image
Golden gate. Evidence: `ui-p11final`, `ui-p0final-control`,
`ui-p0final-control2` under `C:/tmp/gp/`.

### TRANSFER LOCK existing light geometry — preserve baseline

The reported yellow plane through the building is also visible in completed
Phase 0 and desktop C# death-camera captures, with identical model/texture
input hashes. It belongs to `ad1model`, mesh/display-list 142, node 94
`giantLights`, material 16 `lambert_shiled01Glow`, texture 23 (A5I3, 8x64).
Temporarily omitting only that mesh removes the plane while leaving pickups.
Disabling its depth test instead makes it cover additional walls/floor,
confirming the normal depth test still occludes sections. Read-only decoded
base geometry finds 11 triangle-pair intersections with building surfaces.
This is pre-existing light geometry, not a new RHI regression.

The user explicitly chose to preserve existing rendering and record the
finding on 2026-09-30. No light clipping, hiding, or model edits are applied;
all temporary diagnostic renderer changes were reverted. Evidence:
`transfer-end-phase0`, `transfer-end-phase11`, `transfer-end-csharp`,
`transfer-skip-142`, `transfer-ignore-depth`, and
`transfer-intersections.{json,log}` under `C:/tmp/gp/`.

Scope remains native C++ only. C# sources are not modified. No Vulkan Phase 12
implementation is authorized by this gate.
