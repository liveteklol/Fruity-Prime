# Fruity Prime — NVIDIA Reflex `On + Boost` 利用不可の原因調査と修正指示

## 1. 調査対象

### Fruity Prime
- Repository: `Zection6V/Fruity-Prime`
- Branch: `develop3_rendering`
- 調査時 HEAD: `ff753f9b643e86c936632e32b7521c4b1c25d8ee`
- 低遅延実装本体: 親 commit `dd69cb2467e6fdc322f4f7ad73f52ac7ba31df31`

`ff753f9b...` は MD 移動のみで、低遅延 source は `dd69cb...` と同じ。

### melonPrimeDS
- Repository: `ag-advania/melonPrimeDS`
- Branch: `main`
- Commit: `c4165c87416902bb13b670e3147ecf05988017ed`

---

# 2. 結論

現在 Settings に表示されている、

```text
Low Latency
On + Boost

Boost is unavailable...
```

は GPU の誤検出ではない。

**現在の Fruity Prime Vulkan backend が NVIDIA Reflex / `VK_NV_low_latency2` をまだ実装しておらず、Boost capability を意図的に `false` として返していることが直接原因。**

さらに重要なのは、

> 現在の `Low Latency = On` も NVIDIA Reflex ではない。

今の `On` は、

```text
generic one-frame submission budget
+
generic presentation pacing
```

である。

したがって現状は、

```text
Off
    normal scheduling

On
    Fruity generic low-latency scheduling

On + Boost
    requested=OnBoost
    ↓
    Vulkan supportsBoost=false
    ↓
    effective=On
```

となっている。

---

# 3. 原因が確定する source path

## 3.1 UI は正常

`SettingsView.cpp` は、

```text
Off
On
On + Boost
```

を正しく保持している。

説明文は、

```cpp
ResolveLowLatency(mode, SceneLowLatencyCaps())
```

の結果なので、スクリーンショットの表示は backend capability の結果そのもの。

## 3.2 Scene capability は device へ委譲

`NativeRuntime/Rhi/SceneBackend.cpp`

```cpp
LowLatencyCapabilities SceneLowLatencyCaps() noexcept
{
    if (!session) return {};
    try { return session->Device().LowLatencyCaps(); }
    catch (...) { return {}; }
}
```

ここも問題なし。

## 3.3 Vulkan capability がハードコード

`NativeRuntime/Rhi/Vulkan/VulkanGraphicsDevice.cpp`

```cpp
[[nodiscard]] LowLatencyCapabilities LowLatencyCaps() const noexcept override
{
    return {true, false, LowLatencyProvider::Generic};
}
```

意味:

```text
supportsLowLatency = true
supportsBoost      = false
provider           = Generic
```

したがって NVIDIA GPU でも必ず Boost unsupported。

## 3.4 `ResolveLowLatency()` はその結果を正しく fallback

`NativeRuntime/Rhi/PresentationScheduler.hpp`

```cpp
if (requested == LowLatencyMode::OnBoost && !caps.supportsBoost)
{
    state.effective = LowLatencyMode::On;
    state.fallbackReason = "Boost is unavailable; On is active.";
}
```

つまり現在の UI 表示は設計どおり。

---

# 4. melonPrimeDS との決定的な差

| Reflex 要件 | Fruity Prime | melonPrimeDS |
|---|---|---|
| `VK_NV_low_latency2` probe | なし | あり |
| `VK_NV_low_latency2` device enable | なし | あり |
| `VK_KHR_present_id` | なし | あり |
| Reflex entry points | なし | あり |
| dedicated sleep semaphore | なし | あり |
| `VkSwapchainLatencyCreateInfoNV` | なし | あり |
| `vkSetLatencySleepModeNV` | なし | あり |
| `lowLatencyBoost=VK_TRUE` | なし | あり |
| `vkLatencySleepNV` | なし | あり |
| Reflex markers | なし | あり |
| submit/present frame attribution | なし | あり |
| `vkGetLatencyTimingsNV` | なし | あり |
| native provider | Generic固定 | NVIDIA |

したがって **`supportsBoost=true` だけに変える修正は禁止**。

---

# 5. Device extension が有効化されていない

Fruity Prime の `VulkanContextInternal.hpp` では logical device extensions は概ね、

```cpp
VK_KHR_swapchain
VK_EXT_memory_budget
VK_KHR_portability_subset
VK_EXT_swapchain_maintenance1
```

のみ。

現在ここに、

```text
VK_NV_low_latency2
VK_KHR_present_id
```

がない。

device dispatch にも、

```text
vkSetLatencySleepModeNV
vkLatencySleepNV
vkSetLatencyMarkerNV
vkGetLatencyTimingsNV
```

がない。

つまり current device は Reflex を実行できる状態で作られていない。

---

# 6. Vulkan extension dependency

2026-10-03 時点の Vulkan specification では `VK_NV_low_latency2` は、

```text
Vulkan 1.2
OR VK_KHR_timeline_semaphore

AND

VK_KHR_present_id
OR VK_KHR_present_id2
```

を要求する。

Fruity Prime は Vulkan 1.3 を要求済みなので timeline semaphore は core で満たせる。

ただし present ID は別途 probe / enable が必要。

---

# 7. melonPrimeDS の Boost 本体

melonPrimeDS の `VulkanNvidiaReflex::ApplySleepMode()` は、

```cpp
VkLatencySleepModeInfoNV info{};
info.sType = VK_STRUCTURE_TYPE_LATENCY_SLEEP_MODE_INFO_NV;

info.lowLatencyMode =
    enable ? VK_TRUE : VK_FALSE;

info.lowLatencyBoost =
    Mode == VulkanNvidiaReflexMode::OnBoost
        ? VK_TRUE
        : VK_FALSE;

info.minimumIntervalUs = 0;

vkSetLatencySleepModeNV(
    device,
    swapchain,
    &info);
```

を行う。

つまり Vulkan Reflex では、

```text
On
    lowLatencyMode=true
    lowLatencyBoost=false

On + Boost
    lowLatencyMode=true
    lowLatencyBoost=true
```

が native 差。

別の Boost extension は不要。

---

# 8. Swapchain opt-in も必須

melonPrimeDS は swapchain 作成時に、

```cpp
VkSwapchainLatencyCreateInfoNV latencyInfo{};
latencyInfo.sType =
    VK_STRUCTURE_TYPE_SWAPCHAIN_LATENCY_CREATE_INFO_NV;
latencyInfo.latencyModeEnable = VK_TRUE;

latencyInfo.pNext = createInfo.pNext;
createInfo.pNext = &latencyInfo;
```

を追加している。

これは現在の setting が Off でも extension が利用可能なら有効化する。

理由:

```text
Off ↔ On ↔ On + Boost
```

を runtime で切り替えても swapchain を再生成しないため。

現在の Fruity Prime `VulkanSwapchain.cpp` にはこれがない。

---

# 9. `supportsBoost=true` だけではダメな理由

今 `ResolveLowLatency()` は NVIDIA/Amd provider なら、

```text
PacingAuthority::Native
```

にする。

しかし native Reflex を実装せず、

```cpp
supportsBoost=true;
provider=Nvidia;
```

だけ返すと、

```text
generic pacing authority
    OFF

native Reflex
    未実装
```

という壊れた状態になる。

Boost 表示だけ直してはいけない。

---

# 10. 現行 generic one-frame budget との二重pacingにも注意

現在 `Renderer.cpp::BeforeFrame()` は、

```cpp
if (ResolveLowLatency(
        mode,
        device.LowLatencyCaps()).effective
    == LowLatencyMode::Off)
{
    return true;
}

return device.WaitForLatestSubmission(
    PresentationScheduler::FrameBudgetWait.count());
```

となっている。

NVIDIA Reflex を実装後もこれを残すと、

```text
generic latest-submission wait
+
Reflex vkLatencySleepNV
```

の二重pacingになる。

修正後:

```cpp
const auto state =
    ResolveLowLatency(mode, device.LowLatencyCaps());

if (state.effective == LowLatencyMode::Off)
    return true;

if (state.authority == PacingAuthority::Generic)
{
    return device.WaitForLatestSubmission(
        PresentationScheduler::FrameBudgetWait.count());
}

// Native authority
return BeginNativeLowLatencyFrame(...);
```

とする。

原則:

```text
one frame = one pacing authority
```

---

# 11. 推奨 architecture

既存 RHI logical type は維持する。

```cpp
LowLatencyMode
LowLatencyProvider
PacingAuthority
LowLatencyCapabilities
LowLatencyState
```

NVIDIA implementation は Vulkan swapchain side に置く。

```text
VulkanContext / Device
    extension support
    enabled extension
    native function pointers
    extension revision

VulkanSwapchain
    VulkanNvidiaReflex
        current mode
        sleep semaphore
        frame ID
        native runtime state
        markers
```

`VK_NV_low_latency2` は swapchain scoped の操作が中心なので、melonPrimeDS と同様に swapchain owner が runtime controller を持つ形が自然。

---

# 12. 新規 `VulkanNvidiaReflex` を追加

推奨:

```text
src/MphRead.Native/NativeRuntime/Rhi/Vulkan/
    VulkanNvidiaReflex.hpp
    VulkanNvidiaReflex.cpp
```

責務:

```cpp
Initialize(...)
Shutdown()

SetSwapchain(...)
SetMode(...)

BeginFrame(frameId)

MarkInputSample()
MarkSimulationStart()
MarkSimulationEnd()
MarkRenderSubmitStart()
MarkRenderSubmitEnd()
MarkPresentStart()
MarkPresentEnd()

FinishFrame()

Available()
Active()
UnavailableReason()
```

melonPrimeDSを丸ごとcopyするのではなく、Fruityの、

```text
SubmissionSerial
VulkanFrameScheduler
VulkanSwapchain
PresentationScheduler
```

へ適合させる。

---

# 13. `VulkanFeatureProbe` 修正

`PhysicalDeviceSnapshot` / `PhysicalDeviceProbe` に追加:

```text
NvLowLatency2
NvLowLatency2SpecVersion
PresentId
PresentId2（任意）
```

extension enumeration では name だけでなく `specVersion` を保持できるようにする。

`VK_NV_low_latency2` revision 3 以降では、

```text
VkLatencySubmissionPresentIdNV
```

による explicit submission attribution が定義されているため、revisionを記録する価値がある。

Reflex は optional。

extension がない GPU を Vulkan renderer unavailable にしてはいけない。

---

# 14. Device creation 修正

dependency が揃えば device creation 時に、

```cpp
VK_NV_LOW_LATENCY_2_EXTENSION_NAME
VK_KHR_PRESENT_ID_EXTENSION_NAME
```

を追加。

`VK_KHR_present_id` の feature:

```cpp
VkPhysicalDevicePresentIdFeaturesKHR
```

も probe / `pNext` chain に追加。

## 重要

setting が現在 On の時だけ extension を有効化してはいけない。

正解:

```text
supported hardware
    ↓
device creation時にextension enable
    ↓
modeはOffでもよい
    ↓
後からruntime toggle
```

---

# 15. Device dispatch 修正

最低限ロード:

```text
vkSetLatencySleepModeNV
vkLatencySleepNV
vkSetLatencyMarkerNV
vkGetLatencyTimingsNV
```

必要に応じて:

```text
vkQueueNotifyOutOfBandNV
```

extension usable 判定は、

```text
extension supported
+
dependency enabled
+
required entrypoints resolved
```

まで確認する。

---

# 16. Dedicated Reflex timeline semaphore

`vkLatencySleepNV` 用には専用 timeline semaphore を作る。

既存 `VulkanFrameScheduler` timeline を再利用しない。

理由:

```text
FrameScheduler timeline
    queue submission completion serial

Reflex timeline
    driverがframe start timingとしてsignal
```

で value ownership が別。

---

# 17. Swapchain作成修正

`VulkanSwapchain::Recreate()` の `VkSwapchainCreateInfoKHR` に、

```cpp
VkSwapchainLatencyCreateInfoNV
```

を chain。

extension usable の場合:

```cpp
latency.sType =
    VK_STRUCTURE_TYPE_SWAPCHAIN_LATENCY_CREATE_INFO_NV;
latency.latencyModeEnable = VK_TRUE;
latency.pNext = create.pNext;
create.pNext = &latency;
```

setting が Off でも opt-in は維持する。

---

# 18. Swapchain recreate lifecycle

```text
old swapchain
↓
Reflex detach / disable
↓
old swapchain retire
↓
new swapchain create + latencyModeEnable
↓
Reflex attach new swapchain
↓
requested mode reapply
```

resize / fullscreen / present-mode change 後も requested `OnBoost` を保持。

---

# 19. Native mode mapping

```text
Off
    lowLatencyMode=false
    lowLatencyBoost=false

On
    lowLatencyMode=true
    lowLatencyBoost=false

OnBoost
    lowLatencyMode=true
    lowLatencyBoost=true
```

modeが変わっていないsteady stateでは `vkSetLatencySleepModeNV` を毎frame呼ばない。

---

# 20. `LowLatencyCaps()` 修正

現在:

```cpp
return {true, false, LowLatencyProvider::Generic};
```

を削除。

Reflex usable:

```text
supportsLowLatency = true
supportsBoost      = true
provider           = Nvidia
```

Reflex unavailable / generic usable:

```text
supportsLowLatency = true
supportsBoost      = false
provider           = Generic
```

native runtime failure後も Nvidia provider を残さない。

---

# 21. Reflex sleep placement

Native NVIDIA authority 時:

```text
RenderWindow::BeforeFrame
↓
vkLatencySleepNV
↓
vkWaitSemaphores
↓
input collection
```

とする。

Vulkan spec に合わせ、

```text
input samplingより前
present間で一度
```

を守る。

---

# 22. Input / simulation markers

Fruityの render loopでは `Renderer.cpp::OnRenderFrame()` の、

```cpp
WindowsPenInput::Read(...)
```

付近から明示的 input sampling が始まる。

`INPUT_SAMPLE` marker は最初の fresh input read より前。

simulationは、

```cpp
steps = FrameTiming::Advance(...);

for (...)
    _scene->OnSimulationFrame();
```

周辺を、

```text
SIMULATION_START
SIMULATION_END
```

で囲む。

複数simulation stepが1 render frameにある場合も一つのapplication-rendered frame IDで扱う。

---

# 23. Render submit attribution

`VulkanSwapchain::SubmitAndPresent()` の、

```cpp
vkQueueSubmit2(...)
```

を、

```text
RENDERSUBMIT_START
vkQueueSubmit2
RENDERSUBMIT_END
```

で囲む。

`VK_NV_low_latency2 specVersion >= 3` では、

```cpp
VkLatencySubmissionPresentIdNV
```

を `VkSubmitInfo2::pNext` に chain。

同じ logical frame ID を使う。

Fruityは `state.FlushScene()` と final swapchain blit が別 submit になり得るので、explicit attribution を有効にする場合は、その frame に属する queue submissions の attribution が矛盾しないことも確認する。

---

# 24. Present attribution

現在の `VulkanSwapchain.cpp` は既存の、

```cpp
VkSwapchainPresentFenceInfoEXT
```

を `VkPresentInfoKHR::pNext` に使っている。

Reflex用に、

```cpp
VkPresentIdKHR
```

も同じ chain に入れる。

例:

```text
VkPresentInfoKHR
↓
VkPresentIdKHR
↓
VkSwapchainPresentFenceInfoEXT
```

**present fence を上書きで消さないこと。**

---

# 25. Present markers

実際の、

```cpp
vkQueuePresentKHR(...)
```

直前/直後:

```text
PRESENT_START
PRESENT_END
```

を発行。

unrelated CPU work を marker span に含めない。

---

# 26. Runtime toggle

要求:

```text
Off
↕
On
↕
On + Boost
```

device / swapchain recreationなし。

extension / swapchain latency capabilityは最初から準備。

setting変更時は native mode updateだけ行う。

---

# 27. Failure policy

Reflexはoptional。

以下で renderer 全体を落とさない。

```text
extension unsupported
present ID unsupported
entry point missing
sleep semaphore failure
vkSetLatencySleepModeNV failure
vkLatencySleepNV failure
```

fallback:

```text
Requested = OnBoost
↓
native Reflex unavailable
↓
Effective = On
Provider = Generic
Authority = Generic
Reason = exact native reason
```

---

# 28. UI message改善

現在の、

```text
Boost is unavailable...
```

だけでは原因が不明。

可能なら具体的理由を出す。

例:

```text
NVIDIA Reflex unavailable:
VK_NV_low_latency2 is not exposed by this driver.
Using generic Low Latency On.
```

```text
NVIDIA Reflex unavailable:
VK_KHR_present_id is unavailable.
Using generic Low Latency On.
```

runtime failureなら native errorも保存。

---

# 29. OpenGLはGenericのまま

今回の修正は Vulkan NVIDIA path。

OpenGLで NVIDIA GPU を検出しただけで、

```text
provider=Nvidia
supportsBoost=true
```

にしない。

native providerは、

```text
Vulkan + VK_NV_low_latency2
```

として扱う。

---

# 30. 絶対にしない修正

## NG 1

```cpp
return {true, true, LowLatencyProvider::Nvidia};
```

だけに変更。

## NG 2

UI messageだけ消す。

## NG 3

NVIDIA vendor IDだけでBoost対応扱い。

## NG 4

setting=Onの時だけdevice extensionをenable。

## NG 5

Reflexとgeneric `WaitForLatestSubmission`を同時使用。

## NG 6

`VkPresentInfoKHR::pNext`を上書きしてpresent fenceを消す。

## NG 7

FrameScheduler timeline semaphoreをReflex sleepに流用。

---

# 31. 推奨 source changes

新規:

```text
NativeRuntime/Rhi/Vulkan/
    VulkanNvidiaReflex.hpp
    VulkanNvidiaReflex.cpp
```

修正候補:

```text
NativeRuntime/Rhi/Vulkan/VulkanFeatureProbe.hpp
NativeRuntime/Rhi/Vulkan/VulkanFeatureProbe.cpp

NativeRuntime/Rhi/Vulkan/VulkanContextInternal.hpp
NativeRuntime/Rhi/Vulkan/VulkanContext.cpp

NativeRuntime/Rhi/Vulkan/VulkanGraphicsDevice.cpp

NativeRuntime/Rhi/Vulkan/VulkanSwapchain.cpp
NativeRuntime/Rhi/Vulkan/VulkanSwapchain.hpp

NativeRuntime/Rhi/PresentationScheduler.hpp
NativeRuntime/Rhi/Swapchain.hpp

Renderer.cpp
Mods/Launcher/Gui/SettingsView.cpp

CMakeLists.txt
```

---

# 32. 実装順序

## Phase A — capability

```text
VK_NV_low_latency2 probe
VK_KHR_present_id probe
extension revision
entrypoint load
```

この段階ではまだ `provider=Nvidia` にしない。

## Phase B — swapchain / mode

```text
VkSwapchainLatencyCreateInfoNV
VulkanNvidiaReflex controller
vkSetLatencySleepModeNV
```

## Phase C — actual Reflex pacing

```text
dedicated timeline semaphore
vkLatencySleepNV
vkWaitSemaphores
```

input前へ配置。

ここまで動いてから、

```text
provider=Nvidia
authority=Native
```

を有効化。

## Phase D — markers / attribution

```text
INPUT_SAMPLE
SIMULATION_START/END
RENDERSUBMIT_START/END
PRESENT_START/END

VkLatencySubmissionPresentIdNV
VkPresentIdKHR
```

## Phase E — diagnostics

```text
vkGetLatencyTimingsNV
requested/effective/provider/boost
runtime failure reason
```

---

# 33. Acceptance criteria

NVIDIA + supported driver + Vulkan:

```text
requested=OnBoost
effective=OnBoost
provider=Nvidia
boost_supported=1
authority=Native
reason=
```

On:

```text
lowLatencyMode=true
lowLatencyBoost=false
```

On + Boost:

```text
lowLatencyMode=true
lowLatencyBoost=true
```

Off:

```text
lowLatencyMode=false
lowLatencyBoost=false
```

Runtime:

```text
Off → On → OnBoost → Off
```

で device / scene を再生成しない。

Native Reflex active時:

```text
generic WaitForLatestSubmission
    low-latency authorityとして呼ばれない

Reflex sleep
    実際に呼ばれる
```

Validation:

```text
VUID = 0
SYNC-HAZARD = 0
device lost = 0
```

既存:

```text
present fence
swapchain retirement
golden capture
renderer switch
resource check
present conformance
GPU lifetime
```

を壊さない。

---

# 34. Test matrix

```text
Vulkan / NVIDIA:
    Off
    On
    On + Boost

Present:
    FIFO
    Immediate
    Mailbox

FPS:
    Display
    60
    144
    240
    Unlimited

Window:
    windowed
    borderless fullscreen
    resize
    minimize / restore

Switch:
    Vulkan → OpenGL → Vulkan

Runtime:
    launcherでtoggle
    match中toggle

Failure:
    NV extension unavailable
    present ID unavailable
    vkSetLatencySleepModeNV failure
    vkLatencySleepNV failure
```

OpenGLへ切替:

```text
Requested=OnBoost
Effective=On
Provider=Generic
```

requested値は保持。

Vulkanへ戻すと対応GPUでは、

```text
Requested=OnBoost
Effective=OnBoost
Provider=Nvidia
```

へ復帰。

---

# 35. 最終まとめ

現在 Boost が利用不可なのは GPU 側ではなく、Fruity Prime Vulkan backend が現在、

```cpp
LowLatencyCaps()
{
    return {
        true,
        false,
        LowLatencyProvider::Generic
    };
}
```

としか実装していないため。

本当の修正は、

```text
VK_NV_low_latency2 probe
↓
device extension enable
↓
present ID dependency
↓
native entrypoints
↓
swapchain latencyModeEnable
↓
vkSetLatencySleepModeNV
↓
vkLatencySleepNV before input
↓
markers
↓
submit/present attribution
↓
NVIDIA capability reporting
```

までを一式で行うこと。

既存 generic low-latency path は削除せず、

```text
Generic
    OpenGL
    Vulkan Reflex unsupported時

NVIDIA
    Vulkan VK_NV_low_latency2
```

として共存させる。

これにより Settings の `On + Boost` が単なる保存値ではなく、melonPrimeDS と同様に実際の NVIDIA Reflex `lowLatencyBoost` へ到達する。
