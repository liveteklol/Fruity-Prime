# Vulkan RHI Phase 15 validation — complete

Date: 2026-09-30. Branch: `develop3_rendering`.

Current implementation SHA: `7f9e42493d8e3d749acf4896387c16e9e75ccdf4`.
Exact-SHA [desktop CI 36727161309](https://github.com/Zection6V/Fruity-Prime/actions/runs/36727161309)
and [Android CI 36727166007](https://github.com/Zection6V/Fruity-Prime/actions/runs/36727166007)
completed successfully on the implementation SHA. Desktop passed 3/3
(Windows/MSVC, Linux/GCC, macOS/Clang); Android passed 4/4 (contract, both NDK
ABIs and APK). Phase 15 completion conditions are satisfied.

Linux runtime logs confirm binding arrays, alignment, fresh sets, overflow pools,
frame reuse and GPU bind submission PASS; resource and normal/fallback
presentation checks also passed with validation=1 and errors=0.
Evidence: `C:/tmp/gp/p15-accounting-linux-ci.log`.

## Earlier CI failure and repair

Original implementation `cdf5cb72b596cf7b11f124dc6bffd325a5d0efe7` passed
Windows/MSVC, macOS/Clang and Android 4/4. Linux built but failed the pool-growth
coverage assertion: Mesa allowed more sets than the nominal pool budget and
never returned an exhaustion error. Repair `7f9e42493d8e3d749acf4896387c16e9e75ccdf4`
tracks set and per-type descriptor counts in the allocator and grows pages
before those budgets are exceeded, independently of driver behavior. Local
binding/resource validation passed with errors=0 and live=0 after the repair
(`C:/tmp/gp/p15-accounting-runtime.log`). Corrected Linux CI now confirms the
pool-growth coverage as well.

## Implementation

BindingLayout maps uniform/storage buffers, sampled/storage images and separate
samplers to Vulkan descriptor declarations, including stages and array counts.
BindingSetEntry now has a backend-neutral arrayElement index. Native Vulkan
handles remain private to the backend. Set descriptions are immutable; every
materialization allocates and updates a fresh descriptor set rather than
overwriting a set already recorded for GPU use. Resource and layout ownership
remains with callers, which must keep referenced objects alive through use.

Each of two frame slots owns a completion fence and descriptor pools. BeginFrame
waits for that slot's prior GPU submission before resetting every pool. EndFrame
submits a fence after prior graphics-queue work. Exhausted pools grow with
additional pages; pages are retained and reused after the frame fence.

Validation rejects duplicate/missing/undeclared array entries, resource type or
device mismatches, incorrect usage, out-of-bounds buffer ranges and offsets
violating minUniformBufferOffsetAlignment/minStorageBufferOffsetAlignment.
Layout creation queries native descriptor layout support.

## Local evidence

MSYS2 MinGW64 Release build passed. CTest passed 5/5 after the shared binding
contract change. RTX 5070 Ti diagnostic `-vulkanresourcecheck -noupdate` passed
with validation=1, errors=0 and live resources=0. Binding coverage includes all
five descriptor types, a uniform array, invalid duplicate/range/type/alignment
rejection, fresh sets, overflow page creation, and eight frames of slot reuse.
Each frame also records vkCmdBindDescriptorSets and submits the command buffer;
its command pool and pipeline layout remain alive until the frame completes.

Logs: `C:/tmp/gp/p15-final-build.log`, `C:/tmp/gp/p15-final-runtime.log`.

The bind diagnostic does not execute a shader or draw geometry. Shader reads
and production pipeline binding are Phase 16 and subsequent rendering gates.
Exact-SHA desktop and Android CI passed. The overall objective remains Phase 26.
Next is Phase 16; production shader consumption is verified in its rendering gates.
