# Vulkan RHI Phase 16 — COMPLETE (2026-10-01)

Implementation: `cebd532c2d75714d67139a640e96a8ab81ec7899`.
Windows completion gate was changed by the user to a successful local build;
remote Windows/MSVC completion is not required for this phase and is not claimed.
Current HEAD `6e2073a5` has no changes to the implementation, CMake or shader
build scripts relative to the audited implementation SHA.

## Completed requirements

- CMake uses explicitly selected glslc, Vulkan 1.3 / SPIR-V 1.5, for eight stages
  extracted from the frozen desktop main/composite/cel/shift shader bodies.
  Independent-directory regeneration produced identical SPIR-V and embedded
  headers; spirv-val passed 8/8. `C:/tmp/gp/p16-repro-independent` retains evidence.
- Generated metadata fixes explicit std140 offsets, array strides and separate
  image/sampler bindings in one descriptor set. Pipeline diagnostics use these
  manifests for all four production program layouts and GPU descriptor binds.
- GraphicsPipelineDesc maps to VkPipelineLayout and dynamic-rendering VkPipeline.
  Physical-device limits/formats/features are checked; invalid descriptions and
  incompatible descriptor layouts are rejected before submission.
- Pipeline cache hashes state, shader bytecode/entry points and layout contents.
  Full equality handles hash collisions. Identical/content-equivalent resources
  reuse native pipelines; changed culling creates a distinct pipeline.
- All required semantics are mapped in
  [shader semantics](../app_design/Fruity-Prime-CPP-Vulkan-Shader-Semantics.md).
  Shader calculations are preserved; GL clip Z is remapped for Vulkan.

## Verification

Fresh local Windows MinGW Release build at HEAD `6e2073a5` succeeds:
`cmake --build tools/build/out/msys2-mingw64-Release --target fruity_prime --parallel 8`.
CTest passes 5/5. `FruityPrime.exe -vulkanresourcecheck -noupdate` passes on
RTX 5070 Ti (API 1.4), validation=1, errors=0, live=0. It verifies four graphics
pipelines, eight shader modules, descriptor arrays/alignment/overflow/frame reuse,
GPU binds, invalid-description rejection, cache reuse and resource release.
OBS/Bandicam implicit-layer version warnings remain; no validation errors occur.

Exact-SHA [Desktop CI 36746271237](https://github.com/Zection6V/Fruity-Prime/actions/runs/36746271237)
passes Linux/GCC and macOS/Clang. Linux job `109993146058` confirms the full
implementation checkout, eight-stage compilation, foundation, pipeline/module/
binding/resource runtime checks and normal/fallback presentation, validation=1,
errors=0 and live=0. Evidence: `C:/tmp/gp/p16-final-linux-ci.log`.
Exact-SHA [Android CI 36744453750](https://github.com/Zection6V/Fruity-Prime/actions/runs/36744453750)
passes 4/4, including both ABIs and APK packaging.

Earlier local normal/fallback presentation checks also pass; logs are
`C:/tmp/gp/p16-present-runtime.log` and `p16-present-fallback-runtime.log`.
Negative pipeline diagnostics pass in `C:/tmp/gp/p16-negative-pipeline-runtime.log`.

No shader draws have executed. Triangle drawing and coordinate/image checks
belong to Phase 17; full scene pixel parity remains a later gate. Creation and
binding results do not establish image parity. Phase 11 was not audited again.
The user requested stopping after Phase 16; Phase 17 has not started.
