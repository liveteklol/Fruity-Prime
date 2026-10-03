# Fruity Prime Vulkan 最適化レビュー — `cpp-port` + melonPrimeDS

実装状況（2026-10-03）: P1-1〜5とP2-1〜3のgeneric policy/設定を実装・検証済み。
P2-4/P3は条件付き判断を記録。Metal/DX12の実装は今回の依頼から除外。
測定値・検証範囲・条件付き項目の理由は [実装結果](Fruity-Prime-Vulkan-Optimization-Result-2026-10-03.md) を参照。

## 1. 目的

`liveteklol/Fruity-Prime` の `cpp-port` ブランチと `ag-advania/melonPrimeDS` の Vulkan / renderer infrastructure を調査し、現在の `Zection6V/Fruity-Prime` `develop3_rendering` に取り込む価値がある最適化を整理する。

本書の目的は他実装をそのまま移植することではない。現在の `develop3_rendering` 側の RHI、GPU resource lifetime、upload arena、pipeline cache、swapchain retirement、memory admission などの設計を維持しながら、`cpp-port` の draw hot path と melonPrimeDS の descriptor / presentation / low-latency / publication 設計から、Fruity Prime に適する部分だけを抽出する。

## 2. 比較対象

### liveteklol/Fruity-Prime

- Branch: `cpp-port`
- Commit: `17f80dc860244a26a4ed7ee6b8b1ce2ce8e1b05b`

主な確認対象:

- `cpp/src/render/VulkanWindow.h`
- `cpp/src/render/VulkanWindow.cpp`
- `cpp/src/render/SceneRenderer.h`
- `cpp/src/render/SceneRenderer.cpp`

### ag-advania/melonPrimeDS

- Branch: `main`
- Commit: `c4165c87416902bb13b670e3147ecf05988017ed`

主な確認対象:

- `src/VulkanSync.h`
- `src/VulkanSync.cpp`
- `src/VulkanDescriptors.h`
- `src/VulkanDescriptors.cpp`
- `src/VulkanPresentPacer.h`
- `src/VulkanPresentPacer.cpp`
- `src/VulkanPresentPacingPolicy.h`
- `src/VulkanPresenterFrameBudget.h`
- `src/VulkanMemoryAdmission.h`
- `src/VulkanMemoryTelemetry.h`
- `src/VulkanPipelineCache.h`
- `src/VulkanGpuTimestamp.h`
- `src/RendererOutputRing.h`
- `src/RendererOutputRing.cpp`
- `src/GPU3D_Vulkan.cpp`
- `src/frontend/qt_sdl/MelonPrimeVulkanPresenter.cpp`
- `src/DX12UploadRing.h`
- `src/DX12DescriptorRing.h`
- `src/DX12CommandContext.h`

### Zection6V/Fruity-Prime

- Branch: `develop3_rendering`
- **更新後 Commit: `eeff2ebfb591830e35ee99cbc44b1cf0ee3a8ff1`**
- 前回レビュー基準: `6d0590077b6569c268907959bed62342868e6e7a`
- 更新差分: **33 commits ahead**

今回の再監査で特に確認したファイル:

- `src/MphRead.Native/NativeRuntime/Rhi/SceneShaderAbi.def`
- `src/MphRead.Native/NativeRuntime/Rhi/SceneShaderAbi.hpp`
- `src/MphRead.Native/NativeRuntime/Rhi/Capabilities.hpp`
- `src/MphRead.Native/NativeRuntime/Rhi/CommandList.hpp`
- `src/MphRead.Native/NativeRuntime/Rhi/Pipeline.hpp`
- `src/MphRead.Native/NativeRuntime/Rhi/ResourceStatePolicy.hpp`
- `src/MphRead.Native/NativeRuntime/Rhi/Vulkan/VulkanCommandListInternal.inc`
- `src/MphRead.Native/NativeRuntime/Rhi/Vulkan/VulkanCommandSlots.*`
- `src/MphRead.Native/NativeRuntime/Rhi/Vulkan/VulkanFrameSlots.*`
- `src/MphRead.Native/NativeRuntime/Rhi/Vulkan/VulkanFeatureProbe.*`
- `src/MphRead.Native/NativeRuntime/Rhi/Vulkan/VulkanResources.*`
- `src/MphRead.Native/NativeRuntime/Rhi/Vulkan/VulkanSynchronization.*`
- `src/MphRead.Native/NativeRuntime/Rhi/Vulkan/VulkanTransferScratch.*`
- `src/MphRead.Native/NativeRuntime/Rhi/Vulkan/VulkanPipelineCache.*`
- `src/MphRead.Native/NativeRuntime/Rhi/Vulkan/VulkanSwapchain.cpp`
- `docs/todo/Fruity-Prime-Rendering-Architecture-Implementation.md`

今回の更新で、前回レビュー以降に RHI / Vulkan backend の責務分離、state policy、feature probe、frame / command slot owner、conformance test が大幅に進んでいるため、それを前提に推奨事項を再分類する。

# 3. 結論

`cpp-port` や melonPrimeDS の Vulkan 基盤をそのまま移植する必要はない。

更新後の `develop3_rendering` は前回よりさらに整理され、以下はすでに強い基盤になっている。

- backend-neutral `SceneShaderAbi`
- **Frame / Material / Draw / Post の4 update/lifetime groups**
- multi-group `PipelineLayout`
- submission serial / timeline completion
- deferred retirement
- frame / command slot owner の分離
- persistent mapped upload arena
- VMA + memory admission
- persistent validated native pipeline cache
- dynamic rendering
- Synchronization2
- resource-state policy の pure mapping
- structured Vulkan feature probe
- typed presentation lifecycle
- async readback / timestamp query seam
- resource / ownership / state conformance tests

したがって、前版MDにあった **「descriptor set を更新頻度で分割する」自体はTODOではない**。これはすでに `SceneShaderAbi::Group { Frame, Material, Draw, Post }` として実装済みであり、今後の最適化の前提として維持する。

一方、scene draw hot path にはまだ改善余地がある。現行 `DrawScene()` は同一 descriptor set が継続していても各 logical group に対して `vkCmdBindDescriptorSets()` を記録し、変更された uniform block は upload slice + descriptor set 再生成経路を通る。RHI の `CommandList` に small/push constants API はまだなく、persistent material/texture descriptor cache もない。

更新後も優先価値が高いのは以下である。

1. scene path の redundant descriptor bind elimination
2. per-draw small constants の push/root/inline constants 化
3. frame-local descriptor semantic cache
4. fixed ABI descriptor slot の preallocation
5. bounded persistent material/texture descriptor cache + frame-local fallback
6. optional work の non-blocking admission
7. presentation の one-frame budget / present-wait / target-time pacing
8. specialization constants の限定利用
9. telemetry は既存 timestamp / diagnostics seam を利用し、shipping hot path に詳細計測を常駐させない

最適な方針は、

> **現在の RHI / scheduler / lifetime / memory / pipeline 基盤は維持し、その上に frequency-aware state caching と latency-aware presentation policy を追加する**

ことである。

# 4. 比較概要

| 項目 | `cpp-port` | melonPrimeDS | 更新後 `develop3_rendering` | 判断 |
|---|---|---|---|---|
| Frames in flight | 2 | 2を明示 | 2 | 現行維持 |
| Binding groups by frequency | 単純 | 明示的に分割 | **Frame / Material / Draw / Post 実装済み** | TODOから除外 |
| Per-draw constants | Push constants | Push constantsあり | Draw UBO + upload slice | Push/inline化を検討 |
| Pipeline bind cache | あり | あり | Scene pathにあり | 現行維持 |
| Descriptor bind cache | あり | current-set抑制あり | Scene pathは各groupを毎draw bind | 改善価値大 |
| Descriptor semantic cache | texture set cache | bounded persistent + per-frame fallback | current draw state中心 | 改善価値大 |
| Descriptor allocation | scene-oriented | fixed/preallocated + fallback | reusable page allocator | fixed ABI fast pathを追加候補 |
| Dynamic upload | per-frame mapped | staging/upload ring | persistent `VulkanUploadArena` / scratch | 現行が優秀 |
| Resource lifetime | idle wait依存あり | fence deferred destruction | serial/timeline retirement | 現行が優秀 |
| Memory | raw allocation中心 | admission + telemetry | VMA + live budget/admission | 現行が優秀 |
| Pipeline cache | 単純 | persistent cache | validated persistent cache | 現行が優秀 |
| Renderer state policy | backend直結 | backend専用 | `ResourceStatePolicy` + Vulkan mapping分離 | 現行が優秀 |
| Vulkan responsibility split | 小規模 | subsystem分割 | **Resources / Sync / FeatureProbe / FrameSlots / CommandSlots 等へ分離済み** | 前回より改善 |
| Non-blocking readiness | 限定的 | `TryBeginFrame()` | `CanBeginWithoutWait()` / `PollComplete()` が低レベルに存在 | policy seamは未実装 |
| Present pacing | CPU pacing | present wait / target timing / vendor authority | present mode + present-fence correctness中心 | melonPrimeDSを参考 |
| GPU timestamps | なし/限定 | optional telemetry | RHI timestamp query seamあり | 基盤は既存 |
| Specialization constants | なし | 活用 | scene graphics pathでは未採用 | 条件付き候補 |

重要な更新点は、**frequency grouping、責務分離、state policy、feature probe はすでに実装済み**ということである。今後の最適化は新しい基盤を増やすより、既存 group / slot / scheduler を利用して hot path の native command と descriptor churn を減らす方向がよい。

# 5. P1: Redundant descriptor-set bind の削減

## 5.1 `cpp-port` の実装

`SceneRenderer::recordPass()` は現在 bind されている pipeline と descriptor set をローカルに記録している。

概念的には以下である。

```cpp
VkPipeline bound = VK_NULL_HANDLE;
VkDescriptorSet boundSet = VK_NULL_HANDLE;

if (pipeline != bound)
{
    vkCmdBindPipeline(...);
    bound = pipeline;
}

if (item.textureSet != boundSet)
{
    vkCmdBindDescriptorSets(...);
    boundSet = item.textureSet;
}
```

同じ material / texture が連続する場合、不要な `vkCmdBindDescriptorSets()` が記録されない。

## 5.2 現行 `develop3_rendering`

pipeline についてはすでに、

```cpp
if (native.Native() != _boundNative)
{
    vkCmdBindPipeline(...);
    _boundNative = native.Native();
}
```

という抑制が存在する。

一方 descriptor set は `DrawScene()` 内で logical group ごとに処理され、set が再生成されていない場合でも最終的に `vkCmdBindDescriptorSets()` が呼ばれる。

したがって、

```cpp
std::array<VkDescriptorSet, SceneShaderAbi::GroupCount> _boundSets{};
```

のような command-list local state を追加し、

```cpp
if (_boundSets[group] != _sets[group])
{
    vkCmdBindDescriptorSets(...);
    _boundSets[group] = _sets[group];
}
```

とする価値がある。

## 5.3 Reset 条件

以下では `_boundSets` を invalidation する必要がある。

- native pipeline layout が変更されたとき
- scene program が変更されたとき
- command buffer recycle / reset 時
- pipeline variant が変更され、layout compatibility が保証されない場合
- resource destruction により referenced descriptor state が invalidated された場合

ただし Vulkan の pipeline layout compatibility を正しく利用できるなら、必要以上に reset しない方がよい。

## 5.4 優先度

**P1**

実装コストが小さく、RHI の意味論を変更せずに command recording overhead を削減できる。

---

# 6. P1: Per-draw small constants を Push Constants に分離

## 6.1 `cpp-port` の設計

`cpp-port` は draw ごとに変化する値を `DrawConstants` にまとめ、`vkCmdPushConstants()` で送っている。

含まれる値は概ね以下である。

- texture matrix
- diffuse
- ambient
- specular
- alpha
- lighting enable
- polygon mode
- texture enable
- light index
- billboard mode
- pass
- matrix base

これにより draw ごとに、

- uniform buffer slice allocation
- `memcpy`
- descriptor set invalidation
- descriptor allocation
- descriptor update

を行う必要がない。

## 6.2 現行 `develop3_rendering`

現在の scene constant path は、

```text
logical constant write
↓
VulkanSceneUniforms::Block.Generation 更新
↓
VulkanUploadArena から slice 確保
↓
block data memcpy
↓
descriptor group invalidation
↓
descriptor set allocate
↓
vkUpdateDescriptorSets
↓
vkCmdBindDescriptorSets
```

となる可能性がある。

この設計は汎用性と正確性に優れている一方、数十 byte 程度の per-draw data には重い。

## 6.3 推奨分類

constants を更新頻度で明示的に分類する。

```text
Frame Constants
    View
    Projection
    global timing

Scene Constants
    room lights
    fog
    scene-wide state

Material Constants
    material colors
    texture-related state
    alpha / lighting mode

Draw Constants
    matrix index / base
    billboard
    small flags
    draw-local override
```

そのうち `Draw Constants` の小さい部分のみ push constants にする。

## 6.4 RHI として定義する

Vulkan 専用 API を renderer 上位層へ露出させるべきではない。

例えば、

```cpp
CommandList::SetSmallConstants(...)
```

のような logical operation を RHI に追加する。

各 backend では以下へ変換できる。

```text
Vulkan
    vkCmdPushConstants

D3D12
    Root Constants

Metal
    setVertexBytes / setFragmentBytes

OpenGL
    glUniform / small UBO
```

このため、これは Vulkan 固有最適化ではなく、将来の Metal / D3D12 にも有効な RHI 改善となる。

## 6.5 注意点

`cpp-port` の `DrawConstants` をそのままコピーする必要はない。

push constant size は GPU ごとの上限があり、Vulkan の最低保証は 128 bytes である。

したがって、本当に draw ごとに変更される小さな値のみを push constant 化するべきである。

大きな matrix stack などは buffer に残す。

## 6.6 優先度

**P1**

---

# 7. P1～P2: Frame-local Descriptor Binding Cache

## 7.1 `cpp-port` の特徴

`textureSetFor()` は texture / sampler の組み合わせに対して descriptor set をキャッシュしている。

概念的な key は、

```text
model
recolor
textureId
paletteId
sampler mode
```

である。

同じ material state が再利用される場合、

```text
vkAllocateDescriptorSets
vkUpdateDescriptorSets
```

自体が発生しない。

## 7.2 現行実装の特徴

現在の `develop3_rendering` では `VulkanDescriptorAllocator` により frame-local descriptor lifetime はよく管理されている。

一方で scene hot path は、

```text
texture A
texture B
texture A
```

のような sequence で A に戻った場合、同じ descriptor contents であっても新しい set を構築する可能性がある。

現在保持しているのは主として「現在 draw に必要な set」であり、「この frame 中に以前生成した同一 descriptor contents」を検索する semantic cache ではない。

## 7.3 推奨設計

frame-local に以下のような key を持つ。

```text
DescriptorBindingKey
    program / layout identity
    group
    uniform slice identity
    texture image views
    samplers
```

そして、

```text
DescriptorBindingKey
    ↓
VkDescriptorSet
```

を frame slot 内でキャッシュする。

frame fence が完了して descriptor pool を reset するとき、cache も破棄する。

## 7.4 なぜ persistent cache にしないか

最初から scene lifetime cache にすると、

- texture destruction
- sampler destruction
- shader reload
- pipeline layout change
- backend restart
- scene lifetime

との invalidation が複雑になる。

現行の frame-local descriptor allocator と整合させ、descriptor cache も frame-local にする方が安全である。

## 7.5 Push Constants との相性

per-draw constants を push constants へ移すと descriptor group 内で変化する resource が減る。

例えば material group が、

```text
texture
sampler
```

中心になる。

その結果 descriptor cache の hit rate が高くなる。

したがって、

1. push constants
2. descriptor cache
3. redundant bind elimination

はセットで進める価値が高い。

## 7.6 優先度

**P1～P2**

---

# 8. Constant / Buffer の更新頻度分類 — logical grouping は実装済み

前版では resource / constant を更新頻度で分類することを提案したが、更新後の Fruity Prime では logical layer はすでに実装されている。

`SceneShaderAbi` は group を、

```text
Frame
Material
Draw
Post
```

として定義し、各 constant / texture binding をこの lifetime boundary に割り当てている。

例:

```text
Frame
    projection / view / light / fog

Material
    material colors
    texture / sampler
    texture matrix
    toon table
    alpha-test related state

Draw
    inverse view
    matrix stack

Post
    HUD / cel / disruption / backdrop
```

したがって今後の課題は「分類すること」ではなく、

> **この既存 logical grouping を physical update cost に反映すること**

である。

具体的には以下を狙う。

```text
Frame
    frame内では再利用

Material
    material identityで再利用 / cache

Draw
    small scalar/flagは push constants
    大きな matrix stack は upload buffer

Post
    passごとの専用 binding / cache
```

`VulkanUploadArena` 自体は維持する。melonPrimeDS / cpp-port の per-frame mapped buffer に置き換える必要はない。

状態は **logical grouping DONE / physical hot-path optimization OPEN** とする。

# 9. P2: FIFO Presentation Pacing

## 9.1 `cpp-port`

`VulkanWindow.cpp` では FIFO present 時、display refresh rate を基準に次 frame の deadline を計算している。

Windows では high-resolution waitable timer を使用し、deadline 直前まで sleep したあと短い yield loop を使う。

目的は FPS を増やすことではない。

主な狙いは、

- FIFO acquire / present 内の driver busy wait を減らす
- CPU usage を減らす
- power consumption を減らす
- frame pacing を安定させる

ことである。

## 9.2 現行 Fruity Prime

現在の `FrameTiming` は、

- 60 Hz simulation
- FPS cap
- catch-up
- stalls
- dropped simulation steps
- measured render FPS

を扱っている。

これは game loop timing であり、presentation deadline control とは責務が異なる。

## 9.3 推奨設計

backend-neutral な `PresentPacer` または `PresentationScheduler` を設ける。

責務:

```text
requested frame cap
display refresh
present mode
last present deadline
CPU sleep strategy
```

backend mapping:

```text
Vulkan
    FIFO / MAILBOX / IMMEDIATE

D3D12
    DXGI Present / waitable swapchain

Metal
    CAMetalDrawable / display link

OpenGL
    swap interval
```

Vulkan 専用 `sleepUntil()` として固定しない方が将来設計として良い。

## 9.4 優先度

**P2**

---

# 10. 現行 Fruity Prime を維持すべき部分

## 10.1 Upload Arena

`cpp-port` の texture upload は、

```text
staging buffer作成
↓
vkAllocateCommandBuffers
↓
copy
↓
vkQueueSubmit
↓
vkQueueWaitIdle
↓
command buffer破棄
↓
staging buffer破棄
```

である。

これは runtime texture creation が増えた場合に GPU pipeline stall の原因になる。

現行の、

```text
VulkanUploadArena
VulkanFrameScheduler
SubmissionSerial
deferred retirement
```

を維持すべきである。

## 10.2 Resource Lifetime

`cpp-port` では以下で `vkDeviceWaitIdle()` が使用される。

- swapchain recreation
- scene replacement
- vertex buffer rebuild
- shutdown
- offscreen grab の一部

通常の resource lifetime 管理に device-wide idle を使うべきではない。

現行の、

```text
SubmissionSerial
↓
completion tracking
↓
RetirementQueue
↓
safe destruction
```

の方向が正しい。

## 10.3 Memory Allocation

`cpp-port` は resource 単位で `vkAllocateMemory()` を呼ぶ設計が中心である。

現行では、

- VMA
- allocation telemetry
- memory admission
- allocation accounting

が存在する。

現行を維持する。

## 10.4 Pipeline Cache

`cpp-port` は単純な `vkCreatePipelineCache(...)` を利用する。

現行の `VulkanPipelineCache` はさらに、

- disk persistence
- vendor ID 検証
- device ID 検証
- driver version 検証
- `pipelineCacheUUID` 検証
- payload size validation
- checksum
- corrupt cache fallback
- atomic replacement
- cache statistics

を持つ。

現行の方が大幅に優れている。

## 10.5 Dynamic Rendering

`cpp-port` は従来の `VkRenderPass` / `VkFramebuffer` モデルを使用する。

現行は dynamic rendering を使用している。

将来の RHI、Metal、D3D12 との conceptual mapping も考えると、dynamic rendering ベースを維持する方がよい。

## 10.6 Swapchain Retirement

`cpp-port` は resize 時に `vkDeviceWaitIdle()` を使う。

現行は old swapchain を retire し、

- present fence
- image reacquisition
- fallback retirement

などで安全な destruction timing を決定する。

現行の方が高度である。

---

# 11. 推奨 Hot Path

最終的には scene draw を以下へ近づける。

```text
Draw
 │
 ├─ Pipeline / PSO changed?
 │      └─ Yes → bind
 │
 ├─ Binding group changed?
 │      └─ Yes
 │           ├─ frame cache lookup
 │           ├─ miss → allocate/update
 │           └─ bind only when different
 │
 ├─ Small draw constants changed?
 │      └─ Yes → push/root/setBytes
 │
 ├─ Vertex/index stream changed?
 │      └─ Yes → bind
 │
 └─ DrawIndexed
```

重要なのは「API call を減らす」ことそのものではなく、logical state transition が発生したときだけ native command を記録することである。

---

# 12. 将来の Metal / D3D12 を考えた対応

今回参考にできる最適化は Vulkan 固有実装として追加しない方がよい。

## 12.1 Small Constants

```text
RHI SmallConstants
    Vulkan → Push Constants
    D3D12  → Root Constants
    Metal  → setVertexBytes / setFragmentBytes
    OpenGL → uniforms / compact UBO
```

## 12.2 Descriptor / Binding Cache

```text
RHI Binding Group Cache
    Vulkan → VkDescriptorSet
    D3D12  → descriptor table / root binding
    Metal  → buffer / texture / sampler binding state
    OpenGL → texture unit / buffer binding state
```

## 12.3 Pipeline Bind Cache

```text
RHI Pipeline State Cache
    Vulkan → VkPipeline
    D3D12  → ID3D12PipelineState
    Metal  → MTLRenderPipelineState
    OpenGL → program + fixed-function state
```

## 12.4 Presentation Pacing

```text
PresentationScheduler
    Vulkan → VkSwapchainKHR
    D3D12  → DXGI swapchain
    Metal  → CAMetalLayer
    OpenGL → platform swap interval
```

この形にすることで Vulkan 最適化が backend-specific hack にならない。

---

# 13. 実装優先順位

## P1-A: Redundant descriptor bind elimination

実施内容:

- command list に currently bound descriptor groups を保持
- identical set の再bindを回避
- pipeline layout change 時のみ適切に invalidate

リスク:

- 低

期待効果:

- CPU command recording overhead削減
- driver overhead削減

## P1-B: Small Draw Constants

実施内容:

- scene constants を更新頻度で分類
- draw-local small values を logical small constants として分離
- Vulkan は push constants へ mapping

リスク:

- 中

注意:

- screenshot parity を維持
- shader ABI layout を固定
- Vulkan 最低保証サイズ内に抑える

## P1-C: Frame-local Descriptor Cache

実施内容:

- descriptor contents の structural key を定義
- frame slot ごとの cache
- pool reset と同時に cache reset

リスク:

- 中

期待効果:

- repeated material / texture draw で descriptor allocation/update 削減

## P2-A: Update-frequency Resource Classification

実施内容:

- Frame
- Scene
- Material
- Draw
- Static

の lifetime / update domain を RHI 内で明文化する。

既存 UploadArena は維持する。

## P2-B: Presentation Pacer

実施内容:

- `FrameTiming` とは別の presentation scheduling layer
- display refresh / frame cap / present mode を統合
- platform-specific waiting mechanism を backend implementation へ分離

---

# 14. 実施しないもの

以下は `cpp-port` から移植しない。

- resource upload ごとの `vkQueueWaitIdle`
- resize ごとの `vkDeviceWaitIdle`
- scene replacement ごとの `vkDeviceWaitIdle`
- raw `vkAllocateMemory` 中心の resource management
- old-style render-pass architecture
- non-persistent pipeline cache
- renderer 内部に直接 Vulkan-specific presentation pacing policy を埋め込む設計

---

# 15. `cpp-port` 単独評価

`cpp-port` は Vulkan renderer 全体として現在の `develop3_rendering` より洗練されているわけではない。

むしろ、

```text
memory
lifetime
upload
pipeline cache
swapchain
synchronization
RHI abstraction
```

については現在の `develop3_rendering` の方が優れている。

しかし `cpp-port` は draw hot path が非常に直接的で、

```text
state changed?
    ↓ yes
native command
```

という最適化が分かりやすく実装されている。

現在の Fruity Prime は RHI を汎用化した結果、一部 hot path で、

```text
logical state
↓
buffer slice
↓
descriptor reconstruction
↓
descriptor bind
```

というコストを負っている。

したがって今後の最適化では RHI を崩すのではなく、

> **RHI 内部へ state caching と frequency-aware binding model を追加する**

のが最も良い。

最も効果が期待できる組み合わせは、

```text
Small Draw Constants
+
Frame-local Descriptor Cache
+
Redundant Binding Elimination
```

である。

この3点は Vulkan の CPU overhead を減らすだけでなく、将来追加する Metal / D3D12 の command encoding にも自然に対応できるため、Fruity Prime 全体の renderer architecture 改善として採用価値が高い。

---

# 16. melonPrimeDS 追加調査の結論 — 更新後コードとの再照合

melonPrimeDS の設計のうち、更新後 Fruity Prime がすでに取り込んでいる、または同等以上を持つものが増えた。

**すでに実装済み / 現行維持:**

- update/lifetime group の明示化: `Frame / Material / Draw / Post`
- deferred resource destruction
- frame / submission completion tracking
- frame slot / command slot の独立 owner
- GPU memory admission / live budget
- persistent Vulkan pipeline cache
- GPU timestamp query seam
- persistent mapped upload
- swapchain lifetime の fence-based retirement
- structured feature probe
- resource-state policy の backend-neutral contract

特に更新後は、

```text
VulkanFrameScheduler
VulkanFrameSlots
VulkanCommandSlots
SubmissionSerial
RetirementQueue
VulkanUploadArena
VulkanTransferScratch
VulkanMemory + VMA
VulkanPipelineCache
VulkanFeatureProbe
VulkanResources
VulkanSynchronization
VulkanDescriptorAllocator
```

へ責務が分かれており、melonPrimeDS 型へ backend を置換する理由はさらに小さくなった。

一方、melonPrimeDS から今も追加で参考価値が高いのは、

```text
fixed ABI descriptor preallocation
bounded persistent descriptor cache
non-blocking presenter/backpressure policy
one-frame presentation budget
present-wait / target-time pacing
renderer-output lease ring
specialization constants
detailed perf telemetryのoptional化
```

である。

なお **descriptor update-frequency architecture 自体は現行 Fruity Prime ですでに実装済み**なので、以降では「新規実装」ではなく「hot path最適化に活用する既存基盤」として扱う。

# 17. 実装済み: Descriptor Binding を更新頻度で分割

## 17.1 現行 Fruity Prime

更新後の `SceneShaderAbi.hpp` は group を明示的に、

```cpp
enum class Group : std::uint8_t
{
    Frame,
    Material,
    Draw,
    Post,
    Count
};
```

としている。

`SceneShaderAbi.def` では例えば、

```text
Frame
    Frame / Light / Fog uniform buffers

Material
    Material uniform
    MaterialTexture
    MaterialSampler

Draw
    Draw uniform buffer

Post
    Cel / Hud / Disruption / Backdrop
    Post textures / samplers
```

へ分離されている。

さらにコメントで、

> group は update/lifetime boundary であり、Vulkan descriptor setそのものではない

と定義されている。

これは Metal / D3D12 を将来追加する上でも正しい抽象化である。

## 17.2 melonPrimeDS との対応

melonPrimeDS が、

```text
set 0
    per-frame resources

set 1
    texture-switch resources
```

へ分けている設計思想は、Fruity Prime ではより backend-neutral な4 groupとしてすでに取り込まれているとみなせる。

したがって、ここは **追加実装不要**。

## 17.3 今後の利用方法

残る課題は group が存在することではなく、

```text
groupが変わっていない
    ↓
descriptor再生成しない
    ↓
descriptor再bindしない
```

まで hot path に反映することである。

特に `DrawScene()` は現在、`_sets[group]` が既存でも各 group について `vkCmdBindDescriptorSets()` を呼ぶため、Section 5 の redundant bind elimination が次の直接的改善になる。

## 17.4 状態

**DONE / 維持**

この項目を実装優先順位から外し、後続最適化の前提条件とする。

# 18. P1: Fixed ABI Descriptor の Preallocation

## 18.1 melonPrimeDS の方式

melonPrimeDS の `DescriptorPool` は、固定された rasterizer ABI に必要な descriptor set を renderer startup 時にまとめて確保する。

特徴:

```text
pool size
    実際の binding type count × 必要 set 数から算出

VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT
    使用しない

descriptor sets
    startup 時に全 allocate

per-frame
    allocate しない
```

これにより hot path から `vkAllocateDescriptorSets()` を完全に排除している。

さらに個別 free を使わないため descriptor pool fragmentation の経路も消している。

## 18.2 現行 Fruity Prime との違い

現行の `VulkanDescriptorAllocator` は非常に安全な汎用 allocator であり、

```text
submission slot
    ↓
descriptor pool pages
    ↓
Allocate()
    ↓
fence completion
    ↓
vkResetDescriptorPool()
```

という構造になっている。

これは generic RHI には適している。

ただし SceneShaderAbi のように、

- layout が固定
- 最大 group 数が固定
- 1 frame の必要 set 数を上限化できる

hot path まで毎 frame allocate する必要はない。

## 18.3 推奨するハイブリッド方式

`VulkanDescriptorAllocator` を捨てない。

その上に、

```text
FixedSceneDescriptorSlots
    preallocated

Generic / overflow / diagnostic
    VulkanDescriptorAllocator
```

を置く。

つまり、

```text
fast path
    preallocated slot reuse

slow / generic path
    allocator fallback
```

にする。

これなら RHI の汎用性を失わず、scene renderer の deterministic workload だけ最適化できる。

## 18.4 優先度

**P1**

ただし「最大必要数が明確な binding domain」に限定する。

---

# 19. P1: Persistent Texture Descriptor Cache + Frame-local Fallback

## 19.1 melonPrimeDS の実装

`GPU3D_Vulkan.cpp::AcquireTextureSet()` は、

```text
texture identity
+
sampler
```

を key にした bounded hash cache を持つ。

cache hit:

```text
descriptor updateなし
descriptor allocateなし
既存 VkDescriptorSet を返す
```

cache miss では、persistent descriptor slot に空きがある限りそこへ書く。

persistent capacity が埋まった場合は、

```text
per-frame texture descriptor set
```

へ fallback する。

つまり、

```text
bounded persistent cache
    +
safe per-frame fallback
```

である。

これは `cpp-port` の単純な scene-lifetime descriptor cache より安全である。

## 19.2 Fruity Prime への修正案

前章で推奨した frame-local descriptor cache を基本としつつ、安定した resource identity を持てる場合のみ第二段階として persistent cache を追加する。

推奨順:

```text
Stage 1
    frame-local DescriptorBindingKey cache

Stage 2
    stable Texture/Sampler identity を導入

Stage 3
    bounded persistent Material/Texture cache

Stage 4
    capacity miss は frame-local allocator へ fallback
```

persistent cache の key には raw pointer だけを使わず、

```text
resource identity
generation
view identity
sampler identity
layout identity
```

を含めるべきである。

## 19.3 なぜ bounded にするか

unbounded persistent descriptor cache は、

- texture churn
- dynamic assets
- shader reload
- backend recreation

で増え続ける可能性がある。

melonPrimeDS のように明示的 capacity を設け、

> cache overflow は correctness failure ではなく per-frame fallback

とする方がよい。

## 19.4 優先度

**P1**

Redundant descriptor bind elimination と組み合わせると効果が高い。

---

# 20. P2: Non-blocking Backpressure / Frame Drop Path

## 20.1 melonPrimeDS

melonPrimeDS の `FrameRing` は blocking `BeginFrame()` に加え `TryBeginFrame()` を持ち、slot が busy なら待たずに戻れる。

この仕組みは presenter / compositor のような「待つより1 frame落とした方が低latency」な仕事に有効である。

## 20.2 更新後 Fruity Prime

更新後は、低レベルの Vulkan command-slot owner にすでに、

```cpp
bool PollComplete();
bool CanBeginWithoutWait() const;
```

が存在する。

さらに command slot は、

> pending slot を再利用するときだけ wait する

という ownership contract を持つ。

したがって、新しい second scheduler を追加する必要はない。

不足しているのは、

> **この readiness 情報を「optional work を待たずにskipする」という上位 policy へ接続する seam**

である。

現在の public `CommandList::Begin()` は blocking semantics のままであり、presenter / preview / thumbnail 等が non-blocking admission を明示的に要求する API はない。

## 20.3 推奨

既存 `VulkanCommandSlots::CanBeginWithoutWait()` を利用し、backend-neutral に例えば、

```cpp
CommandListReadiness QueryReadiness();
bool TryBegin();
```

のような optional seam を検討する。

ただし primary game rendering の correctness path を drop 対象にしない。

候補:

- preview
- thumbnail
- duplicated presentation work
- repeated unchanged frame
- optional capture preview
- nonessential post-processing

## 20.4 優先度

**P2**

基盤のready判定はすでに存在するため、実装するときは既存 slot owner を再利用する。

# 21. P2: RendererOutputRing の Lease / Publication Model

## 21.1 melonPrimeDS の方式

`RendererOutputRing` は Vulkan / DX12 共通の backend-neutral な publication ring である。

各 slot について、

```text
published?
presenter が lease 中?
GPU work が完了?
```

を確認し、安全な slot のみ producer が上書きする。

`FindFreeSlot()` は、

- 現在 published 中の slot
- presenter が lease 中の slot
- GPU がまだ使用中の slot

を除外する。

presenter は `AcquireLease()` により refcount を持つ。

## 21.2 Fruity Prime にとっての意味

将来、

```text
Renderer
    ↓
backend-neutral output
    ↓
Presenter / UI compositor
```

を明確に分離する場合に有効である。

特に Metal / D3D12 / Vulkan が同じ presentation contract を共有するなら、

> GPU object を知らない publication protocol

として使える。

## 21.3 直ちに導入する必要はない

現在の Fruity Prime が renderer と presenter を同一 submission path で十分に扱えているなら、ring を追加するだけでは複雑性が増える。

したがって導入条件は、

- renderer と window presentation の非同期化
- cross-backend presenter 共通化
- screenshot / stream / UI consumer との同時利用

が必要になった時点とする。

## 21.4 優先度

**P2～P3 / 条件付き**

---

# 22. P2: Presenter One-Frame Budget と Present Wait

## 22.1 melonPrimeDS の設計

melonPrimeDS は、

```text
frames in flight
    CPU が GPU より何 frame 先行できるか

swapchain image count
    surface が持つ presentable image 数
```

を別概念として扱う。

さらに low-latency presenter では「次の再利用slot」ではなく「latest submitted frame」を bounded wait の基準にできる。

また `VulkanPresentPacer` は、

- `VK_KHR_present_wait2`
- `VK_KHR_present_wait`
- `VK_EXT_present_timing`
- `VK_GOOGLE_display_timing`
- latest-ready
- NVIDIA low-latency authority
- AMD Anti-Lag authority

を capability / policy に応じて分離する。

## 22.2 更新後 Fruity Prime の状態

現行 `VulkanSwapchain` には、`VK_EXT_swapchain_maintenance1` が利用できる場合の **per-image present fence** がある。

これは非常に有用だが、役割は主に、

```text
present operation の完了を証明
↓
swapchain image / old swapchain の安全な再利用・破棄
```

である。

つまり、

> **present fence があることと、present pacing / low-latency scheduling があることは別**

である。

現行コードでは `VK_KHR_present_wait(2)`、`VK_EXT_present_timing`、`VK_GOOGLE_display_timing`、vendor low-latency authority を使う presentation scheduler は確認できない。

## 22.3 推奨

presentation policy は段階的に追加する。

```text
Level 0
    現行 typed acquire/present + present-fence correctness

Level 1
    generic CPU deadline / frame cap coordination

Level 2
    present ID + bounded previous-present wait

Level 3
    target display-time scheduling

Level 4
    vendor low-latency authority
```

vendor API が pacing authority を持つ場合は generic pacer と二重制御しない。

swapchain image retirement用 fence はそのまま維持し、pacing mechanismと責務を分離する。

## 22.4 優先度

**P2**

throughput最適化ではなく、input-to-photon latency と pacing stability の改善として扱う。

## 22.5 設定画面の低遅延モード: Off / On / On + Boost

Fruity Prime の低遅延機能は、内部で自動的に有効化するだけではなく、melonPrimeDS と同様に**ユーザーが設定画面から明示的に選択できる機能**とする。

設定値は3段階を基本とする。

```text
Low Latency
    Off
    On
    On + Boost
```

RHI / renderer 内部では bool を複数持つのではなく、backend-neutral な enum として保持する。

```cpp
enum class LowLatencyMode : std::uint8_t
{
    Off,
    On,
    OnBoost
};
```

意味論:

```text
Off
    通常の presentation / frame scheduling
    vendor low-latency API は無効

On
    利用可能な backend / vendor の low-latency mode を有効化
    CPU run-ahead と presentation scheduling も低遅延 policy に従う

On + Boost
    On の全動作
    +
    backend / vendor が対応する場合は boost / high-performance latency mode を要求
```

重要なのは、UI の選択値と実際に有効になった native capability を分離することである。

例えば、

```text
RequestedLowLatencyMode
    OnBoost

EffectiveLowLatencyMode
    On

Reason
    Boost unsupported by this backend/device
```

のように、requested state と effective state を別に保持する。

これにより、未対応 GPU / backend で `On + Boost` が選択されても silent failure にしない。

### Backend mapping

想定 mapping:

```text
Vulkan / NVIDIA
    Off      → low-latency API disabled
    On       → NVIDIA low-latency mode
    OnBoost  → NVIDIA low-latency mode + boost

D3D12 / NVIDIA
    Off      → Reflex disabled
    On       → Reflex enabled
    OnBoost  → Reflex enabled + boost

Vulkan / AMD
    Off      → Anti-Lag path disabled
    On       → supported Anti-Lag path enabled
    OnBoost  → Boost相当機能がなければ Effective=On とし理由を公開

D3D12 / AMD
    同様に backend capability に応じて Anti-Lag 系へ mapping

Metal
    vendor-specific boost API を前提にしない。
    generic one-frame budget / presentation scheduling のみを適用し、
    Boost 非対応なら Effective=On または capability unavailable とする。

OpenGL
    vendor API が利用できない場合は generic scheduling のみ。
    Boost を native feature として偽装しない。
```

`On + Boost` は「必ず GPU clock を固定する」という RHI 契約ではなく、

> **利用可能な backend / vendor の boost-capable low-latency mode を要求する**

という logical request とする。

### Capability model

設定画面は少なくとも以下を参照できるようにする。

```text
supportsLowLatency
supportsLowLatencyBoost
lowLatencyProvider
requestedLowLatencyMode
effectiveLowLatencyMode
fallbackReason
```

例えば provider は、

```text
Generic
Nvidia
Amd
None
```

程度の logical value でよい。

設定画面では `On + Boost` が native に対応しない場合、

- 項目を disabled にする
- または選択を許可し `On` へ fallback したことを表示する

のどちらかにする。

後者を採用する場合も、内部では `Requested=OnBoost / Effective=On` を保持して診断可能にする。

### Runtime toggle

低遅延設定は可能な限り **renderer / device の再起動なしで切り替え可能** にする。

```text
Off
 ↕
On
 ↕
On + Boost
```

の切替で、

- shader
- pipeline
- texture
- scene
- swapchain

を不要に再生成しない。

native API の仕様上 device creation 時に extension enable が必要な場合は、対応 extension 自体は device 作成時に capability として要求しておき、実際の low-latency mode は runtime command / state で有効・無効化する構成を優先する。

これは melonPrimeDS の「利用可能な low-latency extension を device 作成時に確保し、設定変更では renderer 全体を作り直さない」という設計思想を踏襲する。

### Presentation Scheduler との関係

`LowLatencyMode` は vendor API の単純な ON/OFF だけを意味しない。

presentation policy の authority selection に入力する。

```text
LowLatencyMode::Off
    → Generic presentation policy

LowLatencyMode::On
    → low-latency authorityを優先
    → one-frame budget
    → bounded present wait / late acquire 等を capability に応じて使用

LowLatencyMode::OnBoost
    → On と同じ scheduling
    → さらに native boost capability を要求
```

vendor low-latency API が active な場合、generic pacer が独立して同じ frame を制御しないようにする。

```text
one frame
    one pacing authority
```

を原則とする。

### 設定保存

設定は renderer backend ごとに別値へ分裂させず、原則として共通の logical setting とする。

```text
LowLatencyMode = Off / On / OnBoost
```

backend 切替時に同じ requested mode を引き継ぎ、新 backend が対応可能な effective mode を再評価する。

例:

```text
Vulkan NVIDIA
Requested = OnBoost
Effective = OnBoost

↓ OpenGLへ切替

Requested = OnBoost
Effective = On
Reason = native boost unavailable

↓ Vulkanへ戻す

Requested = OnBoost
Effective = OnBoost
```

これにより renderer 切替でもユーザー設定を失わない。

### 検証項目

低遅延設定の実装時には最低限、

```text
Off → On
On → OnBoost
OnBoost → Off

Vulkan → OpenGL → Vulkan
Vulkan → future D3D12
Vulkan → future Metal

unsupported boost fallback
device/session recreate
settings persistence
fullscreen/windowed
VSync on/off
```

を確認する。

また telemetry では、

```text
requested_low_latency_mode
effective_low_latency_mode
low_latency_provider
boost_supported
pacing_authority
present_wait_count
optional_work_busy_skip_count
```

を記録できるようにする。

この3段階設定は **P2 Presentation Scheduler / One-frame Low-Latency Budget の正式なユーザー向け contract** として扱う。

# 23. P3: Specialization Constants の限定利用

## 23.1 melonPrimeDS

`GPU3D_Vulkan.cpp` は、

- screen width
- screen height
- max work tiles

を `VkSpecializationInfo` で pipeline creation 時に与える。

shader 内でこれらから導出される値は SPIR-V specialization により compile-time fold され、runtime 演算を減らせる。

## 23.2 Fruity Prime で使える場所

以下のような、

> pipeline lifetime 中ほぼ不変

な値には有効である。

例:

```text
render-scale class
feature variant
MSAA sample class
rare backend capability branch
static post-process mode
```

## 23.3 使うべきでない場所

頻繁に変わる値を specialization constant にすると pipeline variant が増える。

以下には向かない。

- per-frame viewport size
- camera
- material
- animation
- frequently resized window dimensions
- per-draw state

Fruity Prime はすでに semantic pipeline cache を持つため、specialization key を追加する場合は必ず cache key に含める必要がある。

## 23.4 優先度

**P3 / 条件付き**

profile で shader ALU / branch が実際に問題になった場合のみ。

---

# 24. P3: Telemetry は既存 RHI seam を活用する

melonPrimeDS は GPU timestamp、memory telemetry、descriptor counters などを developer / telemetry build に分離している。

更新後 Fruity Prime にはすでに、

- `Capabilities::supportsTimestampQueries`
- `CommandList::InitializeTimestamps()`
- `CommandList::WriteTimestamp()`
- GPU diagnostics / conformance checks
- memory telemetry
- host-wait / allocation / resource counters

などの計測基盤がある。

したがって「GPU timestamp infrastructure を新設する」は不要である。

今後追加価値があるのは、最適化の効果を測る focused counters である。

例:

```text
scene_descriptor_allocations
scene_descriptor_updates
scene_descriptor_bind_commands
scene_descriptor_cache_hits
scene_descriptor_cache_misses

scene_pipeline_bind_commands
small_constant_updates

present_wait_count
present_wait_ns
optional_work_busy_skip_count
```

重要なのは、これらを常時production hot pathへ重く載せないこと。

推奨:

```text
always-on
    correctness / fatal diagnostics
    minimal lifetime counters

developer/perf build
    GPU timestamps
    descriptor hit/miss
    detailed CPU timers
    presentation timing
    allocation histograms
```

優先度は **P3**。まず最適化前後を比較できる最小counterだけ追加する。

# 25. melonPrimeDS から「確認材料にはなるが追加不要」な項目

## 25.1 Deferred destruction

melonPrimeDS の `DeferredDestroyQueue` は良い reference だが、更新後 Fruity Prime は `SubmissionSerial` / timeline completion / retirement queue を持ち、frame / command slot owner も分離された。

追加不要。

## 25.2 Memory admission

melonPrimeDS は live budget、allocation-count limit、largest-allocation limit、安全 reserve を pure policy として評価する。

更新後 Fruity Prime の `VulkanMemory` は VMA budget、live heap budget、pending reservation、allocation admission を持つ。

追加移植不要。

## 25.3 Pipeline cache

melonPrimeDS は device identity を検証して persistent `VkPipelineCache` を扱う。

Fruity Prime の `VulkanPipelineCache` は vendor / device / driver / UUID / checksum / size gate / atomic replacement / native rejection fallback を持つ。

現行維持。

## 25.4 Upload ring

melonPrimeDS / DX12 の persistently mapped linear upload ring は良い設計だが、Fruity Prime は `VulkanUploadArena` と `VulkanTransferScratch` を持つ。

現行維持。

## 25.5 Feature probe

前版では melonPrimeDS の structured feature probe を参考項目としていたが、更新後 Fruity Prime には `VulkanFeatureProbe` が追加され、

```text
InstanceSnapshot / InstanceProbe
PhysicalDeviceSnapshot / PhysicalDeviceProbe
ProbeFinding
queue selection
capabilities
timestamp properties
```

を logical-device creation と分離している。

この項目も追加不要。

## 25.6 Resource-state mapping

更新後は `ResourceStatePolicy` と `VulkanSynchronization` に state / layout / access の policy が分離されている。

melonPrimeDS の barrier policy は比較材料にはなるが、別体系への置換は不要。

# 26. 更新後の推奨実装順序

## 完了済みの前提

以下は新規TODOから外す。

```text
✓ backend-neutral shader ABI
✓ Frame / Material / Draw / Post group
✓ multi-group PipelineLayout
✓ SubmissionSerial / timeline scheduler
✓ frame / command slot owner
✓ deferred retirement
✓ VMA + memory admission
✓ persistent upload arena
✓ persistent pipeline cache
✓ feature probe
✓ resource-state policy / Vulkan synchronization mapping
✓ timestamp query seam
```

## P1-1: Small Draw Constants

`Draw` groupのうち本当に小さく高頻度な値を、

```text
Vulkan → Push Constants
D3D12  → Root Constants
Metal  → set*Bytes
OpenGL → compact uniforms / UBO
```

へmapする。

`mtx_stack`のような大きな配列はbufferに残す。

## P1-2: Redundant Scene Descriptor Bind Elimination

現行 `DrawScene()` の各group bindを追跡し、同じ native set + compatible layoutなら `vkCmdBindDescriptorSets()` を省略する。

最も低リスク。

## P1-3: Frame-local Descriptor Semantic Cache

同一 frame / command-slot generation内で、

```text
layout
uniform slice identity
texture view
sampler
image layout
resource generation
```

が一致する descriptor set を再利用する。

## P1-4: Fixed ABI Descriptor Preallocation

最大数を決定できる scene hot path に限り preallocated slotを導入する。

既存 `VulkanDescriptorAllocator` は generic / overflow / diagnostics fallback として維持。

## P1-5: Bounded Persistent Material / Texture Descriptor Cache

stable resource identity / generation contract を使い、persistent cacheに上限を設ける。

capacity missは frame-local pathへfallbackする。

## P2-1: Presentation Scheduler

`FrameTiming` と swapchain lifetime fenceから責務を分離し、

```text
display deadline
present mode
present ID / wait
target-time scheduling
pacing authority
```

を扱う。

## P2-2: Non-blocking Optional Work

新schedulerは作らず、既存 `VulkanCommandSlots::CanBeginWithoutWait()` / completion状態を backend-neutral optional-work policyへ接続する。

## P2-3: Low-Latency Settings + One-frame Budget

設定画面に backend-neutral な3段階設定を追加する。

```text
Low Latency
    Off
    On
    On + Boost
```

`RequestedLowLatencyMode` と `EffectiveLowLatencyMode` を分離し、backend / device capability に応じて native low-latency provider と boost を選択する。

low-latency modeでは latest submitted work / previous accepted present を基準に CPU run-ahead を制御する。可能な限り runtime toggle とし、設定変更だけで renderer / device / swapchain を再生成しない。

## P2-4: Backend-neutral Output Publication

renderer / presenter / capture consumer の非同期化が必要になった場合のみ `RendererOutputRing` 型 lease protocol を導入する。

## P3: Specialization / Vendor Features

profiling evidence が得られてから、

- specialization constants
- `VK_KHR_present_wait2`
- `VK_EXT_present_timing`
- `VK_GOOGLE_display_timing`
- `VK_NV_low_latency2`
- AMD Anti-Lag

を capability-driven に追加する。

ただし present-wait / target-time scheduling は vendor feature より先に generic policy として設計する。

# 27. 統合後の推奨 Hot Path

理想的な scene draw は以下になる。

```text
Draw
 │
 ├─ pipeline variant lookup
 │
 ├─ pipeline changed?
 │      └─ bind only if changed
 │
 ├─ material binding key lookup
 │      ├─ persistent cache hit
 │      ├─ frame cache hit
 │      └─ miss → preallocated/fallback descriptor update
 │
 ├─ descriptor group changed?
 │      └─ bind only if changed
 │
 ├─ small draw constants changed?
 │      └─ push/root/inline constants
 │
 ├─ vertex/index stream changed?
 │      └─ bind only if changed
 │
 └─ DrawIndexed
```

frame boundary:

```text
wait only for slot actually being reused
↓
retire completed GPU resources
↓
reset transient upload/descriptor state
↓
record
↓
submit
```

presentation:

```text
renderer output ready
↓
presenter slot available?
 ├─ yes → present
 └─ no  → wait only when policy requires
          otherwise reuse/drop optional presentation work
```

---

# 28. 更新後の統合最終評価

更新後 `develop3_rendering` を再監査すると、前版MDから状況は明確に進んでいる。

特に、

```text
SceneShaderAbiの4 update/lifetime groups
VulkanFeatureProbe
VulkanResources
VulkanSynchronization
VulkanFrameSlots
VulkanCommandSlots
ResourceStatePolicy
```

が追加・整理され、melonPrimeDSから参考にしていた「責務分離」「feature probe」「update-frequency contract」の多くはすでに実装済みになった。

したがって今後は architecture をさらに増やすより、既存 architecture の hot path を軽くする段階に入っている。

`cpp-port` から今も参考になるもの:

```text
simple draw-state caching
push constants
native commandをstate change時だけ記録する思想
basic pacingの発想
```

melonPrimeDS から今も参考になるもの:

```text
descriptor preallocation
bounded persistent descriptor cache
non-blocking backpressure policy
presentation frame budget
capability-driven present pacing
backend-neutral output lease ring
optional specialization constants
focused performance telemetry
```

更新後 Fruity Prime がそのまま維持すべきもの:

```text
generic RHI
SceneShaderAbi groups
multi-group PipelineLayout
SubmissionSerial
timeline-backed scheduler
FrameSlots / CommandSlots
deferred retirement
VMA + memory admission
persistent pipeline cache
dynamic rendering
Synchronization2
FeatureProbe
ResourceStatePolicy
typed presentation lifecycle
async readback / timestamps
cross-backend conformance
```

現時点の最優先セットは、

```text
Small Draw Constants
+
Redundant Descriptor Bind Elimination
+
Frame-local Descriptor Semantic Cache
```

である。

その効果を計測した後に、

```text
Fixed ABI Descriptor Preallocation
+
Bounded Persistent Material/Texture Cache
```

へ進む。

presentation側は別軸として、

```text
Presentation Scheduler
+
Low Latency: Off / On / On + Boost
+
Non-blocking Optional Work
+
One-frame Low-Latency Budget
```

を進める。低遅延設定は backend 共通の logical preference とし、renderer 切替後も requested mode を保持して、新 backend の capability から effective mode を再評価する。

重要なのは、現在の present fence は swapchain lifetime / completion correctness のための仕組みであり、low-latency presentation scheduler の代替ではない点である。

この順序なら、現在までに構築された RHI / Vulkan ownership architecture を壊さずに CPU command-recording cost と descriptor churn を減らし、その設計を将来の Metal / D3D12 にも自然に再利用できる。
