# Vulkan RHI Phase 14 validation — complete

Date: 2026-09-30. Branch: `develop3_rendering`.

## Final result

Phase 14 is complete on `db2ed0f4d353b88b2cee196232ac9b04bbf9f069`.
Desktop run 36704806153 completed 3/3 successfully (Windows/MSVC,
Linux/GCC, macOS/Clang); Android run 36704808948 completed 4/4 successfully.
Linux runtime output proves validation-clean resources, normal presentation
and forced fallback presentation. Local NVIDIA resources and fallback
presentation also passed. Earlier failures and their corrections below remain
as historical evidence. The next implementation phase is Phase 15.

Implementation SHA: `53298011d0d88ff6762eb80c8b0918d596b86b4e`.
[desktop CI 36700074212](https://github.com/Zection6V/Fruity-Prime/actions/runs/36700074212)
passed macOS/Clang; Linux/GCC built successfully but failed the resource
diagnostic because it compared undefined D24 padding. Windows/MSVC is still
running. Exact-SHA
[Android CI 36700149322](https://github.com/Zection6V/Fruity-Prime/actions/runs/36700149322)
completed successfully: 4/4 jobs (native build contract, NDK arm64-v8a,
NDK x86_64 and APK packaging).

Correction SHA: `2286f764f90f517c3791fd9d4bc0a9a39e09da66` compares all
defined D24 depth bits while ignoring only the undefined upper eight bits in
the buffer representation. RG16 and stencil comparisons remain byte-exact.
The rebuilt RTX 5070 Ti resource diagnostic passed with validation=1,
errors=0 and live=0 (`C:/tmp/gp/p14-padding-resources.log`).
[Correction desktop CI 36704306661](https://github.com/Zection6V/Fruity-Prime/actions/runs/36704306661)
is pending on that exact SHA. The earlier Linux failure remains recorded;
the phase is not complete until the corrected runtime gate passes.

Dispatch 36704086720 failed during checkout because the dispatch omitted
`checkout_ref` and inherited the stale `cpp_v0.8.2` default. It provides no
implementation evidence. Replacement 36704306661 has the correction SHA as
its workflow head and explicitly selects that full SHA as the checkout input.

Run 36704306661 passed the Linux resource diagnostic and normal presentation
(validation=1, errors=0). The forced fallback diagnostic failed its coverage
assertion: Mesa supplied four images and the diagnostic drew only four frames
before each recreation, so no presented image was reacquired. Correction
`db2ed0f4d353b88b2cee196232ac9b04bbf9f069` draws at least imageCount+1 frames
per stage. Local fallback validation passed with six retired chains released
and zero errors (`C:/tmp/gp/p14-reacquire-fallback.log`). Final exact-SHA gates
are pending in [desktop 36704806153](https://github.com/Zection6V/Fruity-Prime/actions/runs/36704806153)
and [Android 36704808948](https://github.com/Zection6V/Fruity-Prime/actions/runs/36704808948).

Run 36704806153 has completed Linux/GCC and macOS/Clang successfully on
`db2ed0f4`. Linux llvmpipe passed resource upload/copy/readback with live=0,
validation=1 and errors=0. Normal presentation passed with 35 present-fence
waits; forced fallback presentation passed with six retired chains released
and zero validation errors. Android run 36704808948 also completed 4/4 jobs
successfully (build contract, both NDK ABIs and APK packaging) on `db2ed0f4`.
Windows/MSVC remains pending.
Linux log: `C:/tmp/gp/p14-reacquire-linux-ci.log`.

## Working-tree implementation

VMA owns buffer/image allocations behind the Vulkan backend boundary. Buffer
usage maps Vertex, Index, Uniform, Storage and transfer flags; host upload and
readback use CpuToGpu/GpuToCpu memory. GPU-only uploads use staging buffers.
Texture images and image views are separate objects, and resizing rebuilds
native views while preserving the RHI texture/view objects. Resource states
map to synchronization2 stages, access masks and layouts in one helper.
Transfer command lists currently submit synchronously and wait their fence.
Graphics commands, descriptors and pipelines remain later-phase work.

`BufferTextureCopy::aspect` selects colour/depth/stencil without leaking Vulkan
types. Automatic selects colour or depth. Copy bounds include mip, layer,
extent, row pitch and buffer range. Buffer copies support arbitrary byte
offsets/sizes and reject overlapping source/destination ranges.

## Local evidence

MSYS2 MinGW64 Release build passed. RTX 5070 Ti Vulkan resource diagnostic
passed with validation=1, errors=0 and live resource counts=0. Coverage includes:

- Vertex/Index/Uniform initial-state barriers and GPU upload restoring that state.
- A 127-byte GPU upload/copy/readback with an unaligned source offset.
- Storage read/write and transfer state transitions.
- RGBA8 upload/readback, upload-triggered resize and explicit resize with retained views.
- RG16Float transfer and separate D24S8 depth/stencil aspect transfers.
- Color attachment and depth attachment state transitions.
- Resource release and device/allocator teardown.

CTest passed 5/5 after the shared copy-contract change. The final subsequent
buffer-copy adjustment changed only Vulkan sources; the rebuilt resource
diagnostic passed again. `git diff --check` passed.

Build log: `C:/tmp/gp/p14-bytecopy-build.log`.

## Retained notes

- Final architecture audit must retain the extension-less final-shutdown
  synchronization limitation documented in the Phase 13 gate record.
- The implementation commit excludes the pre-existing VCPKG_ROOT workflow edits,
  which remain unstaged in the local worktree.
- Final exact-SHA build/runtime gates passed and Phase 14 completion conditions
  are checked in the plan.

The remaining overall goal is Phase 15 through Phase 26.
