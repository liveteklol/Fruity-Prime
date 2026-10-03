# Fruity Prime — Vulkan Optimization Post-Implementation Audit & Fix Instructions

## 1. Audit scope

Repository:

`Zection6V/Fruity-Prime`

Branch:

`develop3_rendering`

Pinned audit HEAD:

`ff753f9b643e86c936632e32b7521c4b1c25d8ee`

The optimization implementation itself is its parent:

`dd69cb2467e6fdc322f4f7ad73f52ac7ba31df31`

Commit:

`Optimize scene descriptors and add generic low-latency presentation policy`

This audit is deliberately pinned to `ff753f9b...` so the NVIDIA Reflex work being implemented afterward is not mixed into the review.

---

# 2. Overall result

The major Vulkan hot-path changes are structurally sound.

In particular, no concrete stale-descriptor / use-after-free defect was found in the following areas:

```text
Small Draw Constants / push constants
descriptor-set bind suppression
frame-local descriptor semantic cache
persistent material descriptor cache
resource generation tracking
view replacement invalidation
program / texture / sampler ForgetScene invalidation
submission-serial retirement
optional preview admission
latest-submission wait tracking
```

The implementation does several important things correctly:

- `mat_alpha` and `alpha_test` are moved out of the Vulkan UBO path while the large matrix stack remains buffer-backed.
- Vulkan only pushes small constants when their bytes change.
- descriptor-set reuse checks layout-prefix compatibility and push-constant range compatibility.
- descriptor keys include logical identity, generation and native image/sampler state.
- texture resize increments resource generation.
- view replacement clears persistent material and frame-local descriptor caches.
- destruction flushes recorded scene work before cached resources are forgotten/retired.
- the persistent material cache is bounded and falls back to the frame-local path at capacity.
- `TryPrepareOptionalWork()` is used only on the launcher preview path; primary game simulation/drawing is not silently dropped.
- the Vulkan frame-budget wait tracks the latest queue submission through the existing submission timeline instead of a recycled frame slot.

However, three changes are recommended before treating the implementation as fully closed.

```text
P1  Settings Cancel does not roll back the live Low Latency preference.
P1  A successfully accepted Vulkan present can be reported to the presentation scheduler as unavailable.
P2  Fixed-ABI descriptor preallocation is globally broadcast, over-eager, and fail-hard for an optional optimization.
```

There is also one low-latency ordering item that should be folded into the Reflex work now underway.

---

# 3. P1 — Cancel does not restore the live Low Latency mode

## 3.1 Proven behavior

`SettingsView.cpp`, current pinned source:

```cpp
_lowLatencyRow->Changed += [this, updateLatency](ChoiceRow&)
{
    LauncherPrefs::LowLatency(
        static_cast<Rhi::LowLatencyMode>(
            std::clamp(_lowLatencyRow->Index(), 0, 2)));
    updateLatency();
};
```

Changing the row immediately mutates the process-global `LauncherPrefs::LowLatency()` value.

This is not merely a UI preview value. `Renderer.cpp::BeforeFrame()` and `ApplyFrameRateSettings()` read `LauncherPrefs::LowLatency()` directly, so the new mode becomes live during the settings session.

However `SettingsView::Close()` currently rolls back only FOV:

```cpp
void SettingsView::Close()
{
    if (!_saved)
    {
        RenderOptions::FieldOfView(...);
    }
    Closed(*this);
}
```

It does **not** restore the original low-latency mode.

`Commit()` writes the row value again and then calls `LauncherPrefs::Save()`, but Cancel / Escape never performs the inverse runtime mutation.

Therefore this sequence is currently possible:

```text
saved setting = Off
open Settings
select On
runtime becomes On immediately
press Cancel / Escape
UI closes
runtime remains On
launcher.txt remains Off
```

The result is a session-only preference different from the persisted preference.

Once real NVIDIA Reflex is added, the same bug would leave native Reflex / Boost active after the user explicitly cancels the change.

---

## 3.2 Required correction

Preserve the existing live-preview behavior, but make it transactional like FOV.

Add an original value owned by the `SettingsView` instance:

```cpp
LowLatencyMode _originalLowLatency;
```

Initialize it once when the view is created:

```cpp
_originalLowLatency = LauncherPrefs::LowLatency();
```

Keep the existing `Changed` callback if immediate runtime preview is desired.

Then in `Close()`:

```cpp
if (!_saved)
{
    RenderOptions::FieldOfView(...);
    LauncherPrefs::LowLatency(_originalLowLatency);
}
```

Do not call `LauncherPrefs::Save()` on rollback; the saved file was never changed.

An alternative is to stop mutating `LauncherPrefs` from the row's `Changed` event and apply only in `Commit()`, but that removes live preview. The existing code clearly chose immediate application, so rollback is the narrower correction.

---

## 3.3 Acceptance tests

Test both launcher Settings and in-game Settings:

```text
Off → On → Cancel
On → OnBoost → Cancel
OnBoost → Off → Escape
```

For every case:

```text
runtime value after close == value at SettingsView construction
launcher.txt unchanged
```

Then test Save / Apply:

```text
new runtime value remains active
launcher.txt stores the new value
```

After native Reflex lands, also verify that Cancel restores the actual native mode, not just the enum.

---

# 4. P1 — `ResizeRequired` loses accepted-present semantics

## 4.1 Proven Vulkan behavior

`VulkanSwapchain::TryPresent()` currently does:

```cpp
Present();
return {
    _needsRecreate
        ? PresentationStatus::ResizeRequired
        : PresentationStatus::Ready
};
```

Inside `SubmitAndPresent()`:

```cpp
const VkResult result =
    vk.vkQueuePresentKHR(...);

if (result == VK_SUCCESS
    || result == VK_SUBOPTIMAL_KHR
    || result == VK_ERROR_OUT_OF_DATE_KHR
    || result == VK_ERROR_SURFACE_LOST_KHR)
{
    image.presentPending = true;
}

...

if (_recreateAfterPresent
    || result == VK_SUBOPTIMAL_KHR)
{
    _needsRecreate = true;
}
```

A `VK_SUBOPTIMAL_KHR` present is therefore:

```text
actually submitted / accepted by WSI
+
requires swapchain recreation
```

and `TryPresent()` returns:

```text
PresentationStatus::ResizeRequired
```

---

## 4.2 Renderer currently treats it as not accepted

`Renderer.cpp` currently does:

```cpp
const auto result =
    Rhi::PresentSceneWindow(*_swapchain);

if (result.status == PresentationStatus::Ready)
{
    _window->PresentationAccepted();
}
else
{
    _window->PresentationUnavailable();
}
```

So a successfully queued present which also requests recreation is reported to `PresentationScheduler` as unavailable.

`PresentationUnavailable()` resets the pacing deadline.

The semantic problem is:

```text
presentation status
```

and

```text
whether this present was accepted
```

are not the same axis.

`ResizeRequired` can mean:

```text
accepted + recreate
```

not merely:

```text
not presented
```

This can cause deadline / accepted-present tracking to reset unnecessarily around resize, suboptimal surface transitions, DPI/display transitions, fullscreen changes, etc.

It becomes more important when present IDs / Reflex attribution are added because an accepted present must retain a coherent logical frame identity even if the swapchain needs replacement immediately afterward.

---

# 5. Required presentation-result correction

Do not infer present acceptance from `status == Ready`.

Make acceptance explicit.

Recommended contract:

```cpp
struct PresentResult final
{
    PresentationStatus status =
        PresentationStatus::Ready;

    bool accepted = false;

    std::optional<BackendFailure> failure;
};
```

Then each backend reports both facts.

Examples:

```text
successful normal present
    status   = Ready
    accepted = true

VK_SUBOPTIMAL_KHR
    status   = ResizeRequired
    accepted = true

present rejected because surface is unavailable
    status   = TemporarilyUnavailable
    accepted = false

VK_ERROR_OUT_OF_DATE_KHR
    status   = ResizeRequired
    accepted = false

surface/device loss
    accepted = false
    failure/status retains the typed loss
```

Do not simply change the renderer to:

```cpp
Ready || ResizeRequired
```

because `ResizeRequired` is not guaranteed to imply that a present was accepted.

The Vulkan implementation should retain the native `VkResult` long enough to construct the typed `PresentResult` correctly.

A small internal structure is preferable to hidden mutable state:

```cpp
struct NativePresentOutcome
{
    VkResult result;
    bool accepted;
    bool recreate;
};
```

or make the internal submit/present routine return the final RHI `PresentResult`.

---

## 5.1 Renderer consumption

Change:

```cpp
if (result.status == PresentationStatus::Ready)
    PresentationAccepted();
else
    PresentationUnavailable();
```

to:

```cpp
if (result.accepted)
    PresentationAccepted();
else
    PresentationUnavailable();
```

Handle `result.status` separately for recreate / surface / device actions.

This will also provide the correct foundation for Reflex present-ID accounting.

---

## 5.2 Tests

Add a focused semantic test independent of a real resize:

```text
accepted=true, status=ResizeRequired
    -> scheduler Accepted()
    -> accepted present ID advances
    -> deadline is not reset

accepted=false, status=ResizeRequired
    -> scheduler Unavailable()
    -> deadline resets
```

The Vulkan presentation integration test should additionally verify the actual `VK_SUBOPTIMAL_KHR` path when it can be induced.

---

# 6. P2 — Fixed descriptor preallocation is too broad

## 6.1 Current mechanism

Each non-transfer `VulkanCommandList` registers a device-global descriptor preparer:

```cpp
_device->SceneDescriptorPreparers[this] = prepare;
```

and replays **all** device-global `SceneDescriptorLayouts` already known.

Whenever a new Vulkan scene program is created, its non-empty layouts are broadcast to **every** registered command list:

```cpp
for (const auto& [owner, prepare]
    : _device->SceneDescriptorPreparers)
{
    prepare(program.get(), prepared.back());
}
```

Each command list then calls:

```cpp
_commandSlots->PreallocateDescriptors(
    identity,
    layout,
    desc);
```

and each of the two command slots performs:

```cpp
Preallocate(..., 512);
```

---

## 6.2 Concrete scale

A `VulkanSceneShaderSet` currently creates:

```text
Main
Composite
Shift
CelOutline
Backdrop
```

Its non-empty descriptor layouts are:

```text
Main:
    Frame
    Material
    Draw
        = 3

Composite:
    Post
        = 1

Shift:
    Post
        = 1

CelOutline:
    Post
        = 1

Backdrop:
    Post
        = 1
```

Total:

```text
7 non-empty layouts
```

At 512 fixed descriptor sets per layout and two command slots:

```text
7 × 512 × 2
=
7168 VkDescriptorSet handles
```

for **one shader-set identity collection on one command list**.

Because layout preparation is device-global, multiple command lists and multiple scene shader sets can form a cross-product: a command list can preallocate fixed sets for programs it never draws.

There is a 64-layout cap per allocator, but that cap still permits:

```text
64 × 512 × 2
=
65,536 fixed sets
```

per command list across its two slots.

This does not prove a current OOM, but it is much broader than the intended “fixed ABI fast path”.

---

# 7. The preallocation is also fail-hard

`VulkanDescriptorAllocator::Preallocate()` directly checks:

```text
vkCreateDescriptorPool
vkAllocateDescriptorSets
```

and throws on native failure.

That means failure of an **optional optimization** can fail command-list / shader initialization even though the ordinary descriptor allocator remains a valid fallback path.

The fallback currently covers:

```text
fixed layout not admitted because the internal 64-layout cap was reached
fixed cursor exhausted at draw time
```

but not:

```text
large fixed-pool creation/allocation cannot be satisfied
```

That is the wrong failure boundary for a performance cache.

---

# 8. Required descriptor-preallocation correction

## 8.1 Scope preallocation to the command list which can draw the program

The generic backend API already supplies a `CommandList&` while creating scene shaders.

The Vulkan backend currently discards that ownership relationship and instead broadcasts prepared layouts through `VulkanDeviceState`.

Prefer:

```text
Scene / WindowUi
    owns CommandList
    creates its SceneShaderSet
        ↓
prepare fixed layouts only for that command list
```

Do not preallocate every program layout on every Vulkan command list.

Keep device-global invalidation only where resource lifetime genuinely requires it.

---

## 8.2 Do not hard-code 512 as an unconditional reservation

Use observed high-water data to choose a useful fixed-page capacity.

At minimum:

```text
small fixed fast-path reservation
+
ordinary allocator fallback
```

is preferable to reserving hundreds of sets for every known layout.

A stronger design is:

```text
first-use/lazy fixed page
+
bounded capacity
+
generic overflow
```

No draw is allowed to fail merely because the fixed fast path is full.

---

## 8.3 Make optional preallocation fail-soft

If the fixed-page allocation cannot be created for a recoverable capacity/allocation reason:

```text
mark that identity as fixed-preallocation unavailable
use the existing generic descriptor allocator
continue rendering
```

Do not swallow:

```text
device lost
invalid API contract
validation/correctness failures
```

Only the optional capacity optimization should fail soft.

---

# 9. Descriptor metrics need one more dimension

The implementation report correctly notes that the hot-path descriptor allocation counter excludes fixed batch allocation.

That means:

```text
descriptor allocations / draw
```

is useful for command-recording churn, but it is **not** a measure of total descriptor allocation work or startup resource cost.

Add opt-in counters such as:

```text
fixed_descriptor_pools_created
fixed_descriptor_sets_reserved
fixed_descriptor_sets_high_water
fixed_descriptor_sets_unused
fixed_descriptor_preallocation_failures
fixed_descriptor_overflow_to_generic
fixed_descriptor_setup_time_ns
```

Then evaluate the fixed cache on:

```text
startup / scene load time
host memory
driver allocation time
steady-state draw recording
```

rather than only steady-state per-draw metrics.

---

# 10. Low-latency ordering — fold into the Reflex patch

This is not a separate correctness blocker for the completed generic implementation, but it should be corrected while the frame path is already being touched for Reflex.

Current desktop loop order in `RendererPlatform.cpp` is:

```text
presentation deadline sleep
↓
glfwGetCursorPos
↓
glfwPollEvents
↓
BeforeFrame()
    generic GPU frame-budget wait, up to 2 ms
↓
OnRenderFrame
```

So a successful bounded GPU wait may happen after the latest host input state was collected.

For generic low latency this can add up to the bounded wait duration to input freshness.

For NVIDIA Reflex, the native latency sleep must be placed before the fresh input sample anyway.

Recommended final conceptual order:

```text
service window events often enough to remain responsive
↓
presentation/native pacing admission
↓
fresh input poll/sample
↓
simulation
↓
render
↓
submit
↓
present
```

If event servicing must occur before a bounded wait, refresh the actual input sample again immediately after successful admission.

Do not let:

```text
generic WaitForLatestSubmission
+
Reflex sleep
```

both become pacing authorities for the same frame.

This point should be resolved together with the separate Reflex implementation instructions.

---

# 11. Items audited and accepted as-is

## 11.1 Small constants split

Accepted.

The implementation keeps:

```text
mat_alpha
alpha_test
```

in the small constant path while keeping the matrix stack buffer-backed.

The Vulkan push path checks byte equality before `vkCmdPushConstants`.

No recommendation to move the matrix stack into push constants.

## 11.2 Pipeline / descriptor compatibility

Accepted.

The scene path invalidates previously bound sets when pipeline-layout prefix compatibility or push-constant range compatibility breaks.

This is necessary; merely comparing raw `VkDescriptorSet` handles would not have been sufficient.

## 11.3 Frame-local semantic descriptor cache

Accepted.

The descriptor key includes the relevant:

```text
program identity
group
buffer / offset / range
texture identity
texture generation
image view
sampler identity
VkSampler
image layout
```

and is cleared per native command-buffer / pool generation.

## 11.4 Persistent material cache lifetime

Accepted.

The cache is bounded and invalidated on:

```text
program/resource forget
texture/sampler lifetime change
image-view replacement
command-list shutdown
```

Persistent uniform buffers and descriptor allocator ownership are retired through submission completion.

No stale-handle defect was found in the inspected path.

## 11.5 Optional-work admission

Accepted.

The nonblocking path is used for the standalone launcher hunter preview.

The primary scene/simulation path is not recorded and then discarded when the GPU slot is busy.

## 11.6 Generic latest-submission wait

Accepted for the generic provider.

Vulkan uses its submission timeline and inserts a marker after external swapchain queue work, so the wait targets current queue progress rather than an old frame-slot serial.

The future NVIDIA native provider must bypass this generic pacing authority, as documented in the separate Reflex fix plan.

---

# 12. CI / evidence status

The exact implementation commit:

`dd69cb2467e6fdc322f4f7ad73f52ac7ba31df31`

has a `build_cpp` workflow run:

`37105040002`

whose final conclusion is:

```text
cancelled
```

The current audit HEAD:

`ff753f9b643e86c936632e32b7521c4b1c25d8ee`

also has a `build_cpp` run:

`37105221476`

whose final conclusion is:

```text
cancelled
```

Therefore there is no independent terminal-green GitHub Actions result at either exact SHA from these runs.

This does **not** prove a build failure. It means the repository's local/self-reported test evidence should not be promoted into “exact-SHA CI green” evidence.

After the corrections, obtain a fresh terminal CI result.

---

# 13. Required validation after fixes

Minimum:

```text
Windows MSVC build
Linux GCC build
macOS Clang build
Android NDK contract/build
CTest full suite
```

Rendering:

```text
OpenGL golden/parity capture
Vulkan golden/parity capture
Vulkan validation clean
renderer switch GL → Vulkan → GL
resize
fullscreen
minimize / restore
```

Settings:

```text
Low Latency live preview
Cancel rollback
Escape rollback
Save/Apply persistence
```

Presentation:

```text
Ready + accepted
ResizeRequired + accepted
ResizeRequired + not accepted
TemporarilyUnavailable
surface loss
device loss
```

Descriptor telemetry:

```text
fixed sets reserved
fixed high-water
fixed overflow
frame-local cache hits/misses
persistent material hits/misses
descriptor binds
descriptor updates
```

Compare both:

```text
steady-state command-recording cost
startup / scene-load allocation cost
```

---

# 14. Recommended correction order

```text
1. Fix Settings Cancel rollback.
2. Add explicit PresentResult.accepted semantics.
3. Make descriptor fixed-page preallocation command-list scoped.
4. Make fixed preallocation bounded / fail-soft and measure high-water.
5. Fold input freshness / pacing order into the Reflex patch.
6. Re-run exact-SHA build, CTest, validation and parity gates.
```

The first two are narrow semantic fixes and should be done regardless of further optimization.

The third/fourth preserve the descriptor optimization while removing unnecessary global reservation and preventing an optional fast path from becoming a renderer-start failure mode.

---

# 15. Final assessment

The descriptor and push-constant implementation is not something that needs to be reverted or redesigned wholesale.

Its core direction is good:

```text
frequency-aware constants
+
semantic descriptor reuse
+
redundant native-bind suppression
+
bounded persistent material reuse
+
nonblocking optional work
+
generic presentation policy
```

The remaining work is mostly boundary correctness:

```text
settings transaction boundary
present-acceptance boundary
descriptor-preallocation ownership/capacity boundary
native-vs-generic pacing authority boundary
```

Fix those boundaries while keeping the existing RHI / lifetime architecture intact.
