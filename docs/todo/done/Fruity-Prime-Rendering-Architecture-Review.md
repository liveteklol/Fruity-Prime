# Fruity Prime レンダリングアーキテクチャ比較レビュー

## OpenGL / Vulkan 現状と将来の Metal / D3D12 を見据えた melonPrimeDS 設計比較

作成日: 2026-10-01

改訂: Metal / D3D12 将来対応を前提にRHI境界・shader ABI・binding・presentation・backend lifecycleを再評価

### 今回の実装範囲（2026-10-01 確認）

現在提供する OpenGL / Vulkan を対象に、共通 RHI 契約と寿命・切替・描画経路を改善する。
Metal / D3D12 は将来追加するバックエンドであり、今回は実装しない。Phase F / G は将来の計画として残す。
macOS 環境はないため、今回の実動作検証は Windows の OpenGL / Vulkan で行う。
Phase H / R19 の今回の検証範囲もこの 2 バックエンドとし、将来の 4 バックエンド検証と区別する。
将来の API を共通境界で表現できることと、その API が実装・実機検証済みであることを混同しない。

対応状況と検証の証拠は [実装記録](Fruity-Prime-Rendering-Architecture-Implementation.md) に記録する。

**2026-10-03: 今回の範囲（Windows の OpenGL / Vulkan）の対応は完了。**
R1〜R20 と Phase A〜E / H の実装と検証は、実装記録に項目ごとに記録した。
範囲外として残すもの:
- Metal / D3D12（Phase F / G）: 将来の対応。
- Android / macOS の実機検証。
- 実 driver の reset / OOM の故障注入: 合成 fault で代替した。

### 比較対象を固定したコミット

- **Fruity Prime**: `develop3_rendering` @ `5503e2b35c97abbc63ff838adbc21e466831da38`
- **melonPrimeDS**: `main` @ `c4165c87416902bb13b670e3147ecf05988017ed`

この文書は、描画結果そのものの正しさを比較するものではなく、**レンダラー抽象化、GPUリソース寿命、同期、メモリ管理、descriptor/binding/pipeline管理、shader ABI、presentation、OpenGL状態管理、バックエンド切替、テスト容易性**という設計観点から比較したものです。現在のOpenGL/Vulkanだけでなく、将来Fruity Primeへ追加する**Metal / D3D12（UI上の呼称はDX12でも、内部API名はD3D12）**を同じScene/Rendererから無理なく支えられるかも評価対象に含めます。

melonPrimeDSはエミュレータ、Fruity Primeはゲームレンダラーであり、ワークロードは異なります。そのため、melonPrimeDSのクラス構成やAPIをそのまま移植することは推奨しません。取り入れる価値が高いのは、**責務境界、寿命規約、pure policy、failure reporting、stress testing、API固有機能をcore rendererへ漏らさない設計思想**です。特にD3D12/Metal追加を考えると、Vulkanを「標準形」として他APIを合わせるのではなく、RHIには共通の意味論だけを置き、API固有の最適化は各backend内部に閉じ込める方針が重要です。

---

# 1. 結論

Fruity Primeの現在のRHI方向性そのものは良いです。むしろ将来の4-backend化に向いています。`GraphicsBackend`はすでに`OpenGl / Vulkan / Metal / D3D12`を列挙しており、`GraphicsDevice` / `CommandList` / resource / pipeline / swapchainを型付きで分けているため、基礎を捨てる必要はありません。

ただし、**「enum上は4 backend対応」でも、Sceneとshaderの実体はまだOpenGL/Vulkanの2 backendを前提にしている**状態です。Metal/D3D12を追加する前に、ここを直しておく方が後の実装量と分岐を大きく減らせます。

維持すべき点は次です。

- `GraphicsDevice` / `CommandList` / `GraphicsPipelineDesc` / `ResourceState` を持つ型付きRHI
- `ScenePass`からpipeline stateを一元的に生成する方式
- logical shader constantsを`ShaderConstantSink`でSceneから分離していること
- OpenGL側のframe fence + `RetirementQueue` による遅延破棄
- Vulkan側のper-frame descriptor poolとoverflow poolの再利用
- model/meshの寿命に追従する`GpuMeshCache`
- lifetime / readback / shader-interface等のdiagnostic/testをRHIと併設する方針

一方、4-backend化を前提にした優先課題は次です。

| 優先度 | 問題 | 現在のFruity Prime | 将来backendを見据えた方針 |
|---|---|---|---|
| **P0** | Scene/backend境界 | `SceneBackendKind`はOpenGL/Vulkanのみ。`CreateSceneShaderSet()`が`OpenGL::SceneShaderSources`を受け取る | `GraphicsBackend`へ統合し、backend provider/session factoryでdevice・shader・geometry・presentationを生成 |
| **P0** | shader ABI | 共通に見えるset/binding/constant layoutが`VulkanShaderInterface.hpp`に置かれている | backend-neutral `SceneShaderAbi`をsingle source of truthにし、GLSL/SPIR-V/DXIL/MSLへ個別pack/compile |
| **P0** | Binding/Pipeline layout | generic pipelineは`BindingLayout*`を1つだけ持つ一方、scene Vulkan ABIはset 0～3を使用 | `PipelineLayout`で複数binding groupを正式表現。Vulkan set / D3D12 descriptor table / Metal index groupへmapping |
| **P0** | Vulkan resource lifetime | Buffer/Sampler破棄でscene wait、Texture破棄/resizeで`vkDeviceWaitIdle` | API共通のsubmission completion tokenに基づくdeferred destructionへ統一 |
| **P0** | OpenGL RHIの二重経路 | RHI `CommandList`のBuffer/Draw系が`NotYet`で、`OpenGlGeometry`とcompatibility wrapperが別経路 | 描画submissionを1経路に固定し、VAO/VBO/明示attribute interfaceへ統一 |
| **P1** | Presentation abstraction | `Immediate/Fifo/Mailbox`を全backend共通の実装可能modeとして扱う形 | requestとactual capabilityを分離し、Vulkan/DXGI/CAMetalLayer/GLの差をbackend内部へ閉じ込める |
| **P1** | backend lifecycle | process-global state / singleton / intentional lifetime extensionに依存 | 明示的`BackendSession`、deterministic teardown、passive probe→旧backend解放→active admission |
| **P1** | Vulkan実装の責務集中 | 約184KBの`VulkanGraphicsDevice.cpp`にresource、descriptor、sync、upload、pipeline等が集中 | sync、memory、descriptor、pipeline cache、feature probeをchange axisごとに分離 |
| **P1** | native pipeline cache | Vulkan semantic cacheのみ | Vulkan `VkPipelineCache`、D3D12 Pipeline Library/PSO cache、Metal binary archive等をbackend固有libraryへ分離 |

結論として、**Metal/D3D12を実装し始める前にRHIの「shader ABI / pipeline layout / backend session / presentation status」の4点を整えるのが最も費用対効果が高い**です。これらを後回しにすると、OpenGL用・Vulkan用・Metal用・D3D12用の例外コードがScene周辺へ増え、後から共通化する方が難しくなります。

# 2. Fruity Primeですでに良い部分

## 2.1 RHIの方向性はmelonPrimeDSより汎用的で、捨てるべきではない

Fruity Primeはすでに`GraphicsDevice`で以下をbackend-neutralに表現しています。

- Buffer / Texture / TextureView / Sampler
- Shader
- BindingLayout / BindingSet
- GraphicsPipeline
- CommandList
- Swapchain
- ResourceState transition
- BeginFrame / EndFrame / WaitIdle

参照:

- [`GraphicsDevice.hpp`](https://github.com/Zection6V/Fruity-Prime/blob/5503e2b35c97abbc63ff838adbc21e466831da38/src/MphRead.Native/NativeRuntime/Rhi/GraphicsDevice.hpp)
- [`CommandList.hpp`](https://github.com/Zection6V/Fruity-Prime/blob/5503e2b35c97abbc63ff838adbc21e466831da38/src/MphRead.Native/NativeRuntime/Rhi/CommandList.hpp)
- [`Pipeline.hpp`](https://github.com/Zection6V/Fruity-Prime/blob/5503e2b35c97abbc63ff838adbc21e466831da38/src/MphRead.Native/NativeRuntime/Rhi/Pipeline.hpp)
- [`Resources.hpp`](https://github.com/Zection6V/Fruity-Prime/blob/5503e2b35c97abbc63ff838adbc21e466831da38/src/MphRead.Native/NativeRuntime/Rhi/Resources.hpp)

melonPrimeDSは各rendererが比較的backend-specificな構造を持ちます。Fruity Primeでは今のRHIを基盤にし、melonPrimeDSからは**内部実装の責務分離とlifetime policy**を取り込む方が良いです。

## 2.2 `ScenePass`による描画状態の正規化は良い

`Renderer.cpp`では、旧OpenGLの逐次state mutationをそのまま各所へ残すのではなく、`Scene::DescribeScenePass()`がopaque / decal / translucent / HUD / composite等を`GraphicsPipelineDesc`へ変換しています。

これはVulkanへ自然に対応できるだけでなく、OpenGLでも「必要な状態の完全な記述」として扱えるため、非常に良い方向です。

参照:

- [`Renderer.cpp` - `DescribeScenePass`](https://github.com/Zection6V/Fruity-Prime/blob/5503e2b35c97abbc63ff838adbc21e466831da38/src/MphRead.Native/Renderer.cpp#L1372)

**この設計を逆戻りさせて、backend別にscene stateを組み直すべきではありません。**

## 2.3 OpenGL側の遅延破棄モデルは良い

`FrameContext.hpp`には、最大2 frames-in-flight、GPU完了後にnative resourceを破棄するという明確なcontractがあります。

OpenGL backendは実際に、

- frame slotごとの`GLsync`
- `RetirementQueue<GlObject>`
- fence完了後の`Collect()`
- teardown時のみ`CollectAll()`

を使っています。

参照:

- [`FrameContext.hpp`](https://github.com/Zection6V/Fruity-Prime/blob/5503e2b35c97abbc63ff838adbc21e466831da38/src/MphRead.Native/NativeRuntime/Rhi/FrameContext.hpp)
- [`OpenGlDevice.cpp`](https://github.com/Zection6V/Fruity-Prime/blob/5503e2b35c97abbc63ff838adbc21e466831da38/src/MphRead.Native/NativeRuntime/Rhi/OpenGL/OpenGlDevice.cpp#L519)

このcontractは正しいです。問題は**Vulkan実装がまだこのcontractに完全追従していないこと**です。

## 2.4 Vulkanのdescriptor pool戦略は捨てる必要がない

Fruity PrimeのVulkan backendには、frame slotごとに

- main descriptor pool
- overflow pools
- usage/capacity tracking
- fence完了後のpool reset

があります。

descriptor exhaustion時にはoverflow poolを追加し、frame slot再利用時にresetするため、基本設計は妥当です。

参照:

- [`VulkanGraphicsDevice.cpp` - `DescriptorFrame` / `BeginDescriptorFrame`](https://github.com/Zection6V/Fruity-Prime/blob/5503e2b35c97abbc63ff838adbc21e466831da38/src/MphRead.Native/NativeRuntime/Rhi/Vulkan/VulkanGraphicsDevice.cpp#L503)

改善点は**仕組みを捨てることではなく、`VulkanDescriptorAllocator`として独立させること**です。

---

# 3. P0: Backend選択を2-backend分岐からProvider/Sessionへ変える

## 3.1 `GraphicsBackend`はすでに4 APIを表現できている

`Backend.hpp`には既に次が定義されています。

```text
OpenGl
Vulkan
Metal
D3D12
```

参照:

- [`Backend.hpp`](https://github.com/Zection6V/Fruity-Prime/blob/5503e2b35c97abbc63ff838adbc21e466831da38/src/MphRead.Native/NativeRuntime/Rhi/Backend.hpp#L7-L13)

これは良い先行設計です。ただし`SceneBackend.hpp`には別に`SceneBackendKind { OpenGL, Vulkan }`があり、backend判定・device生成・window presentation・shader生成・geometry生成がOpenGL/Vulkanのif分岐としてまとまっています。

参照:

- [`SceneBackend.hpp`](https://github.com/Zection6V/Fruity-Prime/blob/5503e2b35c97abbc63ff838adbc21e466831da38/src/MphRead.Native/NativeRuntime/Rhi/SceneBackend.hpp#L21-L40)
- [`SceneBackend.cpp`](https://github.com/Zection6V/Fruity-Prime/blob/5503e2b35c97abbc63ff838adbc21e466831da38/src/MphRead.Native/NativeRuntime/Rhi/SceneBackend.cpp#L370-L410)

Metal/D3D12を足すと、このcentral switchへ条件分岐を追加し続ける形になりやすいです。

## 3.2 推奨形

`SceneBackendKind`は最終的に`GraphicsBackend`へ統合し、backendごとの生成責務をproviderへ寄せます。

```text
BackendRegistry
  OpenGlBackendProvider
  VulkanBackendProvider
  MetalBackendProvider
  D3D12BackendProvider

BackendProvider
  ProbePassive()
  ProbeRuntimeAdmission()
  CreateSession(window/surface, options)

BackendSession
  GraphicsDevice
  PresentationSurface / Swapchain
  SceneShaderLibrary
  Upload/Readback contexts
  backend diagnostics
```

Scene側が知るのは`BackendSession`とRHIだけです。`if (Vulkan)` / `if (Metal)`をSceneへ増やしません。

## 3.3 melonPrimeDSから特に取り入れるべきtransition思想

melonPrimeDSのD3D12 feature checkは、**passive eligibilityとdeviceを実際に作るruntime admissionを分離**しています。さらに、Vulkan deviceが生きている間にD3D12 deviceを作るとdriver/adapter enumerationへ悪影響が出るケースを明示し、旧backend解放後にactive probeする契約を置いています。

参照:

- [`MelonPrimeDX12FeatureCheck.h`](https://github.com/ag-advania/melonPrimeDS/blob/c4165c87416902bb13b670e3147ecf05988017ed/src/frontend/qt_sdl/MelonPrimeDX12FeatureCheck.h#L20-L69)

Fruity Primeでもruntime renderer switchを正式機能にするなら、順序は次に固定するのが安全です。

```text
1. passive eligibilityだけ確認
2. 新規GPU workを止める
3. outgoing backendをquiesce
4. scene GPU resourcesをrelease
5. swapchain/surface/device/contextを決定順で破棄
6. incoming backendのactive admission
7. BackendSession生成
8. CPU-side assetsからGPU resources再構築
```

**複数APIのdeviceを同時に生かして切替を速くする設計は、明確な必要性と検証が出るまで採らない方が良い**です。

---

# 4. P0: Shader ABIをVulkan名前空間からRHI共通契約へ移す

## 4.1 現状で最もMetal/D3D12追加を邪魔する境界

`SceneBackend.hpp`はRHI層であるにもかかわらず`OpenGL/OpenGlShaderInterface.hpp`をincludeし、`CreateSceneShaderSet()`は`const OpenGL::SceneShaderSources&`を受け取ります。

参照:

- [`SceneBackend.hpp`](https://github.com/Zection6V/Fruity-Prime/blob/5503e2b35c97abbc63ff838adbc21e466831da38/src/MphRead.Native/NativeRuntime/Rhi/SceneBackend.hpp#L3-L8)
- [`SceneBackend.hpp` - `CreateSceneShaderSet`](https://github.com/Zection6V/Fruity-Prime/blob/5503e2b35c97abbc63ff838adbc21e466831da38/src/MphRead.Native/NativeRuntime/Rhi/SceneBackend.hpp#L114-L119)

さらに`VulkanShaderInterface.hpp`には、実際にはVulkan固有ではない次のcontractが入っています。

- vertex semantic/location
- Frame / Material / Draw / Postという更新頻度別group
- binding番号
- scene constantのlogical grouping
- shader interfaceの整合性static_assert

参照:

- [`VulkanShaderInterface.hpp`](https://github.com/Zection6V/Fruity-Prime/blob/5503e2b35c97abbc63ff838adbc21e466831da38/src/MphRead.Native/NativeRuntime/Rhi/VulkanShaderInterface.hpp#L1-L45)

これはVulkan implementationではなく、**Scene shader ABI**としてRHI共通側にあるべき情報です。

## 4.2 `std140`そのものを全backend共通ABIにしない

ここは重要です。

Vulkanで使っている`std140` byte layoutを、そのままD3D12 HLSL cbufferやMetal structの絶対規格として他backendへ強制するのは避けるべきです。

推奨する二層構造は次です。

```text
SceneShaderConstants      // logical values, backend-neutral
SceneShaderAbi            // semantic/group/binding contract

OpenGL packer             // GLSL uniform/UBO representation
Vulkan packer             // std140/SPIR-V representation
D3D12 packer              // HLSL cbuffer/root constant representation
Metal packer              // MSL constant-buffer representation
```

`ShaderConstants.hpp`のlogical struct群は既に良い土台です。これをsingle source of truthにし、backendごとのpacked structは個別にstatic_assert/reflectionで検証します。

## 4.3 Shader source/binaryもbackend-neutral descriptorにする

現在`ShaderDesc`はbytecodeを持ち、OpenGLだけ`CreateGlslShader()`という別口を使います。4 backend化するなら、shaderの入力形式を明示した方がよいです。

概念例:

```text
ShaderModuleDesc
  stage
  entryPoint
  binary/source format
    GLSL source
    SPIR-V
    DXIL/DXBC
    Metal library/function
  reflection/ABI id
  debug name
```

ただし、**1つのshader言語へ全backendを強制すること自体は必須ではありません**。重要なのは、build時に同じ`SceneShaderAbi`へ適合していることを機械検証できることです。

将来HLSLをcanonical sourceにしてDXCからSPIR-V/DXILを生成する、あるいはMSLを別生成する方式を選ぶことはできますが、それはshader toolchainの選択です。RHI設計はその選択に依存させない方が良いです。

## 4.4 Acceptance criteria

- `SceneBackend.hpp`がOpenGL/Vulkan固有shader headerをincludeしない
- `SceneShaderAbi`がbackend-neutral namespaceにある
- OpenGL/Vulkan/D3D12/Metal全てが同じsemantic/group contractを検証する
- shader binary/source形式は`ShaderDesc`で明示可能
- shader reflectionまたは生成manifestでvertex input / binding / constant sizeをCI検証可能

---

# 5. P0: BindingLayoutを本当の4-backend PipelineLayoutへ昇格する

## 5.1 現在のgeneric RHIには構造上のズレがある

`CommandList`には`SetBindingSet(slot, bindingSet)`があり、複数setを想定できる形です。一方`GraphicsPipelineDesc`が持つ`bindingLayout`は**1つだけ**です。

さらに通常のVulkan pipeline creationでは`VkPipelineLayoutCreateInfo::setLayoutCount = 1`として1 layoutだけを組みますが、scene shader ABIは`FrameSet=0 / MaterialSet=1 / DrawSet=2 / PostSet=3`を定義しています。

つまり現在のscene pathはspecial/deferred pathで吸収できていますが、**generic RHIのpipeline layout model自体はscene shader contractを表現し切れていません**。

参照:

- [`Pipeline.hpp`](https://github.com/Zection6V/Fruity-Prime/blob/5503e2b35c97abbc63ff838adbc21e466831da38/src/MphRead.Native/NativeRuntime/Rhi/Pipeline.hpp#L208-L224)
- [`CommandList.hpp`](https://github.com/Zection6V/Fruity-Prime/blob/5503e2b35c97abbc63ff838adbc21e466831da38/src/MphRead.Native/NativeRuntime/Rhi/CommandList.hpp#L144-L159)
- [`VulkanShaderInterface.hpp`](https://github.com/Zection6V/Fruity-Prime/blob/5503e2b35c97abbc63ff838adbc21e466831da38/src/MphRead.Native/NativeRuntime/Rhi/VulkanShaderInterface.hpp#L18-L37)

## 5.2 D3D12/Metalまで考えた推奨形

```text
BindingLayout            // 1 logical groupの中身
PipelineLayout
  group[Frame]
  group[Material]
  group[Draw]
  group[Post]
  optional small constants

GraphicsPipelineDesc
  PipelineLayout*
```

backend mappingは次のようにします。

| RHI logical group | Vulkan | D3D12 | Metal | OpenGL |
|---|---|---|---|---|
| Frame | descriptor set | root descriptor/table | buffer/argument binding | UBO/binding points |
| Material | descriptor set | descriptor table | texture/sampler/buffer indices | texture + sampler + UBO |
| Draw | dynamic UBO/set | root CBV/constants or table | setVertexBytes/buffer | UBO/uniform path |
| Post | descriptor set | descriptor table | buffer/texture indices | UBO/texture units |

RHIの`slot`は「Vulkan descriptor set number」ではなく**logical binding group index**として扱います。

## 5.3 SamplerをTexture stateから完全分離する

`BindingType`が`SampledTexture`と`Sampler`を別々に持っている点は、Vulkan/D3D12/Metalへ非常に良く適合します。

一方OpenGLの現在の`BindSampledTexture()`はsampler descを`glTexParameter`としてtextureへ書き込んでいます。これは同じtextureを異なるsamplerで同時利用する設計と相性が悪く、他3 APIの「textureとsamplerは別resource」という意味論とも一致しません。

将来的にはOpenGL backendもGL sampler objectまたは同等のbackend-local sampler cacheを使い、RHIの`Sampler`を真に独立resourceとして扱う方が良いです。

同時に、`CommandList::BindSampledTexture()`というpush-descriptor-styleの特別経路は恒久的な第二binding systemにしない方が良いです。最終的には通常の`BindingSet`/material bindingへ統合するか、少なくとも「1 texture + samplerをlogical material slotへbindする高水準convenience」と定義し、Vulkan/D3D12/Metal/OpenGLで同じ意味になるようにします。

## 5.4 Push/small constantsはoptional拡張として追加可能

D3D12 root constants、Vulkan push constants、Metalのsmall constant bindingは、頻繁に変わる小さいdraw stateに適しています。ただし最初から必須にする必要はありません。

必要になった場合だけ、

```text
PushConstantRange
CommandList::SetPushConstants(...)
```

というsemantic APIを追加し、OpenGLはuniform/UBO fallbackで実装できます。


## 5.5 `Capabilities`はAPI名ではなくrendererが必要とするsemantic featureを表す

現在の`Capabilities`は`supportsCompute`、`supportsTimestampQueries`、`supportsWireframe`、`supportsDepthClamp`等を持っており、方向は良いです。

参照:

- [`Capabilities.hpp`](https://github.com/Zection6V/Fruity-Prime/blob/5503e2b35c97abbc63ff838adbc21e466831da38/src/MphRead.Native/NativeRuntime/Rhi/Capabilities.hpp)

Metal/D3D12追加時も、ここへ`supportsSynchronization2`、`supportsRootSignature11`、`supportsArgumentBufferTier2`のような**API固有機能名を増やさない**方が良いです。

RHI coreが知るのは、例えば次のようなrenderer requirementだけです。

```text
supportsWireframe
supportsIndependentBlend
supportsStorageTexture
supportsTimestampQueries
maxColorAttachments
maxVertexBuffers
maxBindingGroups
```

API固有extension/tier/familyはbackendのfeature probe内部でこのsemantic capabilityへ変換します。

また、現在の`maxBindingSets`という名前はVulkan用語に寄っているため、4 backend化時には`maxBindingGroups`等のlogical名称へ変える方が自然です。

presentation能力はdevice capabilityと性質が違うため、`Capabilities`へ全部詰めず、前節の`PresentationCapabilities`へ分けます。

## 5.6 Texture/View descriptionはbackendが推測しなくてよい形へ寄せる

現在の`TextureDesc`はwidth/height/depth/arrayLayersからbackend側が2D/3D/arrayを推測できます。現行用途では十分ですが、Metal/D3D12を含めて長期運用するなら、必要になった時点で次を明示できる形が安全です。

```text
TextureDimension
  1D / 2D / 3D / Cube

TextureViewDesc
  view dimension
  aspect
  mip range
  array range
```

これはMetal/D3D12着手前の必須P0ではありません。現在のFruity Primeが2D中心なら後回しで構いません。ただしcube/array/3D/MSAA resolve等を増やすときに、「backendがdesc値から推測する」仕様を拡張し続けないことを推奨します。

---

# 6. P0: Lifetime contractを「frame number」からsubmission completionへ一般化する

## 6.1 現在の`FrameContext`は良いが、4 APIでは一段抽象化した方がよい

現在の`RetirementQueue<T>`は`lastUsedFrame`と`completedFrame`で安全な破棄を管理します。OpenGL/Vulkanの現在構成では十分機能します。

ただしD3D12/Metalを入れると、同じ表示frame内でも、

- scene submission
- upload submission
- readback submission
- compositor/presenter submission

が別completion pointを持つ可能性があります。

melonPrimeDSのD3D12側は`DX12CommandContext`がmonotonic fence valueを持ち、Metal側はframe slotごとの`MTLCommandBuffer` completionとgeneration/in-flight stateを使っています。

参照:

- [`DX12CommandContext.h`](https://github.com/ag-advania/melonPrimeDS/blob/c4165c87416902bb13b670e3147ecf05988017ed/src/DX12CommandContext.h#L27-L88)
- [`GPU3D_MetalCompute.mm`](https://github.com/ag-advania/melonPrimeDS/blob/c4165c87416902bb13b670e3147ecf05988017ed/src/GPU3D_MetalCompute.mm#L1170-L1187)

## 6.2 推奨contract

RHI内部で概念的に次を持ちます。

```text
SubmissionSerial / CompletionToken
  queue/domain
  monotonic value

Retire(resource, lastUseToken)
Collect(completedToken)
```

現時点でsingle graphics queueしか使わないなら、単純な64-bit serialで十分です。将来本当にasync copy/compute queueが必要になったときだけqueue domainを増やします。

重要なのは、**「frame Nで死んだ」ではなく「GPUのどのsubmissionまでこのresourceを参照し得るか」**をlifetime source of truthにすることです。

backend mapping:

```text
OpenGL  -> GLsync / completed submission serial
Vulkan  -> fence or timeline value
D3D12   -> ID3D12Fence value
Metal   -> MTLCommandBuffer completion / shared-event value
```

RHI上位はnative synchronization objectを知りません。

## 6.3 `WaitIdle`の意味も統一する

`WaitIdle()`は、

- backend teardown
- explicit renderer transition boundary
- unrecoverable diagnostic/readback fallback

へ限定します。

通常のresource destructor / resize / cache evictionで呼ばないことを4 backend共通contractにします。

---

# 7. P1: Presentation APIをVulkan用語から「request + resolved capability」へ変える

## 7.1 現在の`PresentMode`はVulkanには自然だが、4 APIで完全同義ではない

現在は、

```text
Immediate
Fifo
Mailbox
```

というmodeを`Swapchain`が直接受けます。

参照:

- [`Swapchain.hpp`](https://github.com/Zection6V/Fruity-Prime/blob/5503e2b35c97abbc63ff838adbc21e466831da38/src/MphRead.Native/NativeRuntime/Rhi/Swapchain.hpp)

Vulkanでは自然ですが、D3D12/DXGIはsync interval・tearing・buffer count等で表現し、Metalは`CAMetalLayer`/drawableの提示モデルを使い、OpenGLはswap interval中心です。`Mailbox`を全backendへ1:1で要求すると、unsupported exceptionか意味の違う近似が増えます。

## 7.2 推奨形

UI/Renderer側は「希望」を出し、backendが実際に採用した状態を返します。

```text
PresentRequest
  vsync
  allowTearing
  lowLatencyPreference
  preferredImageCount

PresentationCapabilities
  supportsTearing
  supportedImageCountRange
  supportsLowLatencyMode
  ...必要なものだけ

ResolvedPresentationMode
  actual policy
  actual image count
  actual format
```

`Immediate/Fifo/Mailbox`を残す場合でも、**request enumとactual backend modeを分ける**べきです。

## 7.3 `Acquire` / `Present`はstatusを返せるようにする

4 backendでは正常系以外も重要です。

- Vulkan: suboptimal / out-of-date / surface lost / device lost
- D3D12: occlusion / resize / device removed
- Metal: drawable取得失敗 / command-buffer error
- OpenGL: context/window lifecycle failure

そのため例外だけに寄せず、概念的に

```text
AcquireResult
PresentResult
  Ok
  ResizeRequired
  TemporarilyUnavailable
  SurfaceLost
  DeviceLost
```

のようなtyped statusを持つとbackend switch/recoveryが整理しやすくなります。

## 7.4 Surface lifecycleをSwapchainと分離できる設計にする

Metalの`CAMetalLayer`、Vulkan surface、DXGI HWND/swapchain、OpenGL contextは寿命関係が違います。すぐに大規模interface変更をする必要はありませんが、`RendererPlatform::Window`から直接各API objectを作る処理は`BackendSession`または`PresentationSurface`内部へ閉じ込め、Sceneから見えない状態にします。

---

# 8. P0: Vulkan resource lifetimeを最優先で直すべき

## 8.1 RHIのcontractとVulkan implementationが食い違っている

`FrameContext.hpp`は「resource destructionはGPU完了までretireする」と明記しています。

しかしVulkan側では、resource destructorの一部が同期waitを直接行っています。

### Buffer

`VulkanBuffer::~VulkanBuffer()`は、破棄前にsceneをflushし、`WaitScene()`しています。

参照:

- [`VulkanGraphicsDevice.cpp` - `VulkanBuffer::~VulkanBuffer`](https://github.com/Zection6V/Fruity-Prime/blob/5503e2b35c97abbc63ff838adbc21e466831da38/src/MphRead.Native/NativeRuntime/Rhi/Vulkan/VulkanGraphicsDevice.cpp#L1145)

### Sampler

`VulkanSampler::~VulkanSampler()`も同じく`FlushScene()` + `WaitScene()`を行います。

参照:

- [`VulkanGraphicsDevice.cpp` - `VulkanSampler::~VulkanSampler`](https://github.com/Zection6V/Fruity-Prime/blob/5503e2b35c97abbc63ff838adbc21e466831da38/src/MphRead.Native/NativeRuntime/Rhi/Vulkan/VulkanGraphicsDevice.cpp#L1208)

### Texture

`VulkanTexture::~VulkanTexture()`はさらに強く、`vkDeviceWaitIdle()`を呼びます。

参照:

- [`VulkanGraphicsDevice.cpp` - `VulkanTexture::~VulkanTexture`](https://github.com/Zection6V/Fruity-Prime/blob/5503e2b35c97abbc63ff838adbc21e466831da38/src/MphRead.Native/NativeRuntime/Rhi/Vulkan/VulkanGraphicsDevice.cpp#L1326)

### Texture resize

`VulkanTexture::Resize()`も、image/viewを作り直す前に`vkDeviceWaitIdle()`します。

参照:

- [`VulkanGraphicsDevice.cpp` - `VulkanTexture::Resize`](https://github.com/Zection6V/Fruity-Prime/blob/5503e2b35c97abbc63ff838adbc21e466831da38/src/MphRead.Native/NativeRuntime/Rhi/Vulkan/VulkanGraphicsDevice.cpp#L1346)

## 8.2 なぜ問題か

resourceの破棄やresizeが発生するだけで、無関係なdraw、upload、presentを含むdevice全体のGPU workが完了するまでCPUが止まる可能性があります。

特に次の場面で悪化します。

- window resize
- render target resize
- texture refresh
- map/scene unload
- launcher/game間のresource入替
- runtime renderer switch
- transient resource churn

さらに重要なのはperformanceだけではなく、**RHIのlifetime modelがbackendごとに違ってしまうこと**です。

OpenGLでは「C++ object死亡 → native objectをretire → fence後破棄」なのに、Vulkanでは「C++ object死亡 → GPUを待つ → 即破棄」となっています。

RHIレベルで同じcontractを定義しているなら、backendも同じ意味論にする方が安全です。

## 8.3 melonPrimeDSの取り入れるべき設計

melonPrimeDSの`VulkanSync.h`は、この問題を明確に1か所へ集約しています。

`DeferredDestroyQueue`のコメントでは、rendering開始後はresourceを直接`vkDestroy*`せず、**最後に参照したabsolute frame numberと一緒にqueueへ入れ、対応frameのfence完了後に破棄する**というcontractを明示しています。

参照:

- [`VulkanSync.h` - `DeferredDestroyQueue`](https://github.com/ag-advania/melonPrimeDS/blob/c4165c87416902bb13b670e3147ecf05988017ed/src/VulkanSync.h#L81)
- [`VulkanSync.h` - `FrameRing`](https://github.com/ag-advania/melonPrimeDS/blob/c4165c87416902bb13b670e3147ecf05988017ed/src/VulkanSync.h#L220)

`FrameRing`では通常frame pathのblocking waitをslot reuse時のfence waitへ限定し、`vkDeviceWaitIdle` / `vkQueueWaitIdle`をsteady-state pathから除外しています。

## 8.4 Fruity Primeでの推奨形

既存の`RetirementQueue<T>`をVulkanにも適用できるよう、native resource bundleをretire対象にします。

例えば概念上は次のような単位です。

```text
RetiredVulkanObject
  type
  handle(s)
  VmaAllocation
  lastUsedFrame
```

Textureなら、

```text
VkImage
VmaAllocation
sampled VkImageView
TextureView由来のVkImageView群
```

をまとめて旧generationとしてretireします。

### Resizeの理想形

現在:

```text
flush
vkDeviceWaitIdle
old views destroy
old image destroy
new image create
views recreate
```

推奨:

```text
new image create
new views create
resource objectをnew generationへ切替
old image/views/allocationをcurrent frameでretire
GPU完了後にold generationをdestroy
```

resizeのたびにdevice全体を停止する必要がなくなります。

## 8.5 Acceptance criteria

- steady-state中のBuffer/Sampler/Texture destructorで`WaitScene()`を呼ばない
- steady-state中のTexture resizeで`vkDeviceWaitIdle()`を呼ばない
- native destroyはdeferred destruction queue経由に統一
- `WaitIdle()`はbackend teardown、device-loss recovery、明示的diagnostic等の限定経路のみ
- `GpuResourceStatistics::HostWaits`がresource churnだけでは増えない
- 2 frames-in-flightでresource destroy/recreate stressを行ってvalidation error 0

---

# 9. P0: OpenGL RHIの「二重描画経路」を解消する

## 9.1 現在の問題

Fruity Primeの`GraphicsDevice` / `CommandList`はmodern RHI型のinterfaceですが、OpenGL backendでは主要機能がまだ完成していません。

`OpenGlGraphicsDevice::CreateBuffer()`:

```text
NotYet("CreateBuffer (mesh buffers are owned by OpenGlGeometry)")
```

さらに`OpenGlCommandList`では、

- `SetVertexBuffer`
- `SetIndexBuffer`
- `SetBindingSet`
- `Draw`
- `DrawIndexed`
- `CopyBuffer`
- `CopyBufferToTexture`
- `CopyTextureToBuffer`

が`NotYet`です。

参照:

- [`OpenGlDevice.cpp`](https://github.com/Zection6V/Fruity-Prime/blob/5503e2b35c97abbc63ff838adbc21e466831da38/src/MphRead.Native/NativeRuntime/Rhi/OpenGL/OpenGlDevice.cpp#L390)
- [`OpenGlDevice.cpp` - `OpenGlCommandList`](https://github.com/Zection6V/Fruity-Prime/blob/5503e2b35c97abbc63ff838adbc21e466831da38/src/MphRead.Native/NativeRuntime/Rhi/OpenGL/OpenGlDevice.cpp#L800)

その代わり、scene geometryは`OpenGlGeometry.cpp`が直接bufferを作り、直接GL submissionしています。

参照:

- [`OpenGlGeometry.cpp`](https://github.com/Zection6V/Fruity-Prime/blob/5503e2b35c97abbc63ff838adbc21e466831da38/src/MphRead.Native/NativeRuntime/Rhi/OpenGL/OpenGlGeometry.cpp)

つまり現在は概念的に、

```text
Renderer
  -> RHI pipeline/state
  -> OpenGlCommandList   [一部]

Renderer geometry
  -> OpenGlGeometry
  -> raw-ish OpenTK GL   [別経路]
```

となっています。

これではRHIが「backendを隠す唯一のsubmission layer」になりません。

## 9.2 状態キャッシュも二重経路の影響を受けている

`BeginRendering()`では、RHI外部のコードがGL stateを変更した可能性があるため、毎回`_applied = nullptr`に戻しています。

これは安全策としては正しいですが、同時に**OpenGL state ownershipがRHIへ集約されていないことの証拠**です。

参照:

- [`OpenGlDevice.cpp` - `BeginRendering`](https://github.com/Zection6V/Fruity-Prime/blob/5503e2b35c97abbc63ff838adbc21e466831da38/src/MphRead.Native/NativeRuntime/Rhi/OpenGL/OpenGlDevice.cpp#L840)

## 9.3 melonPrimeDSから取り入れるべき点

melonPrimeDSのOpenGL rendererは抽象RHIではありませんが、**vertex submission contractが明示的**です。

- VBO
- VAO
- explicit generic vertex attributes
- UBO
- explicit attribute location

を使います。

参照:

- [`GPU3D_OpenGL.cpp`](https://github.com/ag-advania/melonPrimeDS/blob/c4165c87416902bb13b670e3147ecf05988017ed/src/GPU3D_OpenGL.cpp#L198)
- [`OpenGLSupport.cpp`](https://github.com/ag-advania/melonPrimeDS/blob/c4165c87416902bb13b670e3147ecf05988017ed/src/OpenGLSupport.cpp#L340)

例えば3D vertex pathではVAOにposition/color/texcoord/attributeを固定し、draw時の状態を明確にしています。

Fruity PrimeではmelonPrimeDSのdirect-GL構造をコピーするのではなく、**この明示的なvertex-state設計をRHIの`Buffer + Pipeline + CommandList`へ落とし込む**のが良いです。

## 9.4 推奨する最終形

`OpenGlGeometry`がGL APIを直接呼ぶのではなく、backend-neutral geometryから次を作成するようにします。

```text
GraphicsDevice::CreateBuffer(vertex)
GraphicsDevice::CreateBuffer(index)
GraphicsPipelineDesc::vertexBuffers
GraphicsPipelineDesc::vertexAttributes
CommandList::SetVertexBuffer
CommandList::SetIndexBuffer
CommandList::DrawIndexed
```

OpenGL backend内部だけが、

```text
GLuint VBO
GLuint IBO
GLuint VAO
```

を知ります。

これによりVulkanとOpenGLのgeometry submission pathが本当に同一になります。

---

# 10. P1: OpenGL compatibility profile依存を段階的に消す

## 10.1 現在はgeneric attributeとlegacy conventional arrayを同時維持している

`GL.cpp`には現在の状態が明記されています。

> Phase 4 keeps the desktop GLSL 1.20 built-ins until Phase 5.

`EnableVertexAttribArray()`や`VertexAttribPointer()`がgeneric attributeを設定したあと、

- `glEnableClientState`
- `glVertexPointer`
- `glColorPointer`
- `glNormalPointer`
- `glTexCoordPointer`

へmirrorしています。

参照:

- [`GL.cpp` - compatibility mirror`](https://github.com/Zection6V/Fruity-Prime/blob/5503e2b35c97abbc63ff838adbc21e466831da38/src/MphRead.Native/NativeRuntime/OpenTK/GL.cpp#L470)
- [`GL.cpp` - `VertexAttribPointer`](https://github.com/Zection6V/Fruity-Prime/blob/5503e2b35c97abbc63ff838adbc21e466831da38/src/MphRead.Native/NativeRuntime/OpenTK/GL.cpp#L1049)

`Shaders.cpp`もdesktop shaderが`#version 120`で、`gl_FragColor`等のlegacy GLSL surfaceを使います。

参照:

- [`Shaders.cpp`](https://github.com/Zection6V/Fruity-Prime/blob/5503e2b35c97abbc63ff838adbc21e466831da38/src/MphRead.Native/Shaders.cpp)

この互換mirrorは移行期間の実装として理解できますが、**恒久設計にはしない方が良い**です。

## 10.2 melonPrimeDSの参考点

melonPrimeDSのOpenGL shaderは少なくともGLSL 1.40系のexplicit inputを使い、program link前にattribute locationを固定しています。

参照:

- [`3DRenderVS.glsl`](https://github.com/ag-advania/melonPrimeDS/blob/c4165c87416902bb13b670e3147ecf05988017ed/src/OpenGL_shaders/3DRenderVS.glsl)
- [`OpenGLSupport.cpp`](https://github.com/ag-advania/melonPrimeDS/blob/c4165c87416902bb13b670e3147ecf05988017ed/src/OpenGLSupport.cpp#L340)

Fruity Primeでも、最終的には

```text
legacy built-in attributes
  -> explicit generic attributes
legacy individual uniforms
  -> UBO / explicit binding model where適切
conventional client arrays
  -> VAO state
```

へ移す方が良いです。

## 10.3 重要な注意

「GLSL versionをmelonPrimeDSと同じ1.40にする」こと自体が目的ではありません。

目的は、

- attribute locationを明示する
- vertex layoutをVAO/RHI pipelineへ固定する
- compatibility state aliasingをなくす
- shader interfaceをbackend-neutral metadataと一致させる

ことです。

対応GPU範囲次第では、core-profile pathとlegacy fallbackを明確に別backendとして残す選択肢もあります。

---

# 11. P1: Vulkan backendを「change axis」で分割する

## 11.1 現在の集中度

`VulkanGraphicsDevice.cpp`は約184KBあり、単に長いだけでなく、次の責務が同じtranslation unitにあります。

- VMA allocation
- Buffer / Texture / TextureView / Sampler / Shader
- descriptor pool management
- BindingLayout / BindingSet
- pipeline validation
- in-process pipeline caching
- pipeline creation
- CommandList
- upload ring
- staging fallback
- frame fencing
- readback
- resource registry
- diagnostics
- scene-specific integration

参照:

- [`VulkanGraphicsDevice.cpp`](https://github.com/Zection6V/Fruity-Prime/blob/5503e2b35c97abbc63ff838adbc21e466831da38/src/MphRead.Native/NativeRuntime/Rhi/Vulkan/VulkanGraphicsDevice.cpp)

ファイルサイズそのものが問題なのではありません。

問題は、**「変更理由が違うもの」が同じownership boundaryに入っていること**です。

例えばdescriptor allocatorのpool sizingを変える変更と、texture lifetimeを変える変更と、pipeline keyを変える変更が、同じ巨大implementationの内部状態に触れます。

## 11.2 melonPrimeDSの分割思想

melonPrimeDSはVulkanを以下のように分けています。

- `VulkanDevice.*`
- `VulkanFeatureProbe.*`
- `VulkanMemory.*`
- `VulkanMemoryAdmission.h`
- `VulkanMemoryTelemetry.h`
- `VulkanSync.*`
- `VulkanDescriptors.*`
- `VulkanPipelineCache.*`
- `VulkanDebugLabels.h`
- `VulkanSwapchain`相当のpresent subsystem
- `VulkanPresentPacer.*`
- backend本体

重要なのは「小さいファイルにする」ことではなく、例えば`VulkanPipelineCache.h`自身が、cache framing / device identity validation / driver rejection handlingは**独立したchange axis**なので分離した、と説明している点です。

参照:

- [`VulkanPipelineCache.h`](https://github.com/ag-advania/melonPrimeDS/blob/c4165c87416902bb13b670e3147ecf05988017ed/src/VulkanPipelineCache.h#L31)

## 11.3 Fruity Prime向け推奨構造

例えば次の程度で十分です。

```text
NativeRuntime/Rhi/Vulkan/
  VulkanDevice.cpp
  VulkanDevice.hpp

  VulkanResources.cpp
  VulkanResources.hpp

  VulkanDeferredDeletion.cpp
  VulkanDeferredDeletion.hpp

  VulkanFrameScheduler.cpp
  VulkanFrameScheduler.hpp

  VulkanDescriptorAllocator.cpp
  VulkanDescriptorAllocator.hpp

  VulkanUploadContext.cpp
  VulkanUploadContext.hpp

  VulkanPipelineLibrary.cpp
  VulkanPipelineLibrary.hpp

  VulkanFeatureProbe.cpp
  VulkanFeatureProbe.hpp

  VulkanSwapchain.cpp
  VulkanSwapchain.hpp

  VulkanDiagnostics.cpp
  VulkanDiagnostics.hpp
```

`GraphicsDevice`はそれらを所有・調停するだけにします。

---

# 12. P1: Feature ProbeをContext生成処理から分離する

## 12.1 現在のFruity Prime

`VulkanContextInternal.hpp`で、

- loader version
- instance extension
- validation layer
- physical device enumeration
- API 1.3要求
- dynamic rendering要求
- synchronization2要求
- format capability
- queue family
- memory heap size
- device scoring
- logical device creation

が一連のcontext initialization内に入っています。

参照:

- [`VulkanContextInternal.hpp`](https://github.com/Zection6V/Fruity-Prime/blob/5503e2b35c97abbc63ff838adbc21e466831da38/src/MphRead.Native/NativeRuntime/Rhi/Vulkan/VulkanContextInternal.hpp#L220)

この実装は動作判定としては合理的ですが、**policyとdevice creationが強く結合**しています。

また、unsupported deviceが「なぜ不適格だったか」をstructured dataとして保持しにくい構造です。

## 12.2 melonPrimeDSの良い点

`VulkanFeatureProbe`は、

- requirementごとのfinding
- queue family selection
- resolution budget
- device capability
- memory admission snapshot
- optional extension capability

をdevice creationから分離しています。

参照:

- [`VulkanFeatureProbe.h`](https://github.com/ag-advania/melonPrimeDS/blob/c4165c87416902bb13b670e3147ecf05988017ed/src/VulkanFeatureProbe.h)

特に良いのは、**unsupportedを単なる例外文字列で終わらせず、判定結果そのものをデータにしていること**です。

## 12.3 Fruity Primeで採るべき形

例えば、

```text
VulkanProbeResult
  apiVersion
  queues
  requiredFeatures
  optionalFeatures
  formats
  memory
  reasons[]
  eligible
```

を返し、

```text
ProbePhysicalDevice()
SelectPhysicalDevice()
CreateLogicalDevice(probeResult)
```

を分離します。

なお、Fruity PrimeがVulkan 1.3 + dynamic rendering + Synchronization2を最低条件にすること自体は、必ずしも悪い設計ではありません。

melonPrimeDSは逆にVulkan 1.1を最低ラインとし、timeline semaphore、dynamic rendering、Synchronization2を意図的に使わない方針です。

参照:

- [`VulkanFeatureProbe.h` - minimum API policy](https://github.com/ag-advania/melonPrimeDS/blob/c4165c87416902bb13b670e3147ecf05988017ed/src/VulkanFeatureProbe.h#L49)

ここで取り入れるべきなのは**API versionではなく、capability policyを明示し、判定を独立・検証可能にする設計**です。

---

# 13. P1: Vulkan pipeline cacheを二段構成にする

## 13.1 Fruity Primeにすでにある良いcache

Fruity Primeは`GraphicsPipelineDesc`、shader code、binding layout等から`VulkanPipelineKey`を作り、同一pipelineをprocess内でreuseしています。

これは残すべきです。

参照:

- [`VulkanPipelineInternal.inc` - `VulkanPipelineKey`](https://github.com/Zection6V/Fruity-Prime/blob/5503e2b35c97abbc63ff838adbc21e466831da38/src/MphRead.Native/NativeRuntime/Rhi/Vulkan/VulkanPipelineInternal.inc)
- [`VulkanGraphicsDevice.cpp` - semantic pipeline cache](https://github.com/Zection6V/Fruity-Prime/blob/5503e2b35c97abbc63ff838adbc21e466831da38/src/MphRead.Native/NativeRuntime/Rhi/Vulkan/VulkanGraphicsDevice.cpp#L2780)

## 13.2 足りないもの

native `vkCreateGraphicsPipelines()`には現在`VK_NULL_HANDLE`がcacheとして渡されています。

参照:

- [`VulkanPipelineInternal.inc`](https://github.com/Zection6V/Fruity-Prime/blob/5503e2b35c97abbc63ff838adbc21e466831da38/src/MphRead.Native/NativeRuntime/Rhi/Vulkan/VulkanPipelineInternal.inc#L220)

つまり、

1. **Fruityのsemantic object cache**はある
2. **Vulkan driver pipeline cache**はない

という状態です。

## 13.3 melonPrimeDSの取り入れるべき点

melonPrimeDSの`VulkanPipelineCache`は、

- `VkPipelineCache`を専用ownerが持つ
- disk payloadを保存する
- device identityを検証する
- 古い/不一致/driver rejectionはrenderer failureにしない
- cacheなしでもpipeline creation可能

という設計です。

参照:

- [`VulkanPipelineCache.h`](https://github.com/ag-advania/melonPrimeDS/blob/c4165c87416902bb13b670e3147ecf05988017ed/src/VulkanPipelineCache.h)

Fruity Primeでは次の二段構成が良いです。

```text
Level 1: VulkanPipelineKey -> shared VulkanGraphicsPipeline
Level 2: VkPipelineCache   -> driver compilation cache
```

Level 1はC++ object reuse、Level 2はdriver compilation reuseであり、役割が異なります。

4 backend化するなら、この概念を`PipelineLibrary`という**RHI内部サービスの役割名**として揃え、native mechanismはbackend側に残すのが良いです。

```text
OpenGL  -> linked-program cache / optional program-binary cache
Vulkan  -> VkPipelineCache
D3D12   -> ID3D12PipelineLibrary + PSO cached blob
Metal   -> MTLRenderPipelineState cache + optional MTLBinaryArchive
```

melonPrimeDSのD3D12実装も、root signature・pipeline library・PSO cached blob・disk validationを`DX12PipelineRepository`へ分離しています。

参照:

- [`DX12PipelineRepository.h`](https://github.com/ag-advania/melonPrimeDS/blob/c4165c87416902bb13b670e3147ecf05988017ed/src/DX12PipelineRepository.h#L200-L280)

重要なのは、`GraphicsPipelineDesc`というsemantic keyはRHI共通のまま、native cache serialization/device identity/driver rejection handlingを各backendへ任せることです。

---

# 14. P1: Memory budget / admission / telemetryをVMAの上に追加する

## 14.1 VMAを使っていること自体は問題ではない

Fruity PrimeはVMAを使い、`MemoryUsage`を

- GPU only
- CPU to GPU
- GPU to CPU

へ抽象化しています。

これはそのままで構いません。

問題は、**allocation可能性を事前に判断するpolicy layerが薄いこと**です。

現行Vulkan sourceには`VK_EXT_memory_budget`や`vmaGetHeapBudgets()`を使ったlive-budget policyは確認できません。

## 14.2 melonPrimeDSの良い設計

melonPrimeDSは`VulkanMemoryAdmissionSnapshot`と`VulkanMemoryAdmissionRequest`を分離し、

- heap budget
- heap usage
- allocation count
- max allocation size
- already reserved bytes
- safety reserve

からallocation可否を**pure function**で判断します。

参照:

- [`VulkanMemoryAdmission.h`](https://github.com/ag-advania/melonPrimeDS/blob/c4165c87416902bb13b670e3147ecf05988017ed/src/VulkanMemoryAdmission.h)

特に良いのは、policy functionがVulkan APIやglobal stateを直接触らないため、UMA / discrete / multi-heap等をsynthetic fixtureでテストできる点です。

## 14.3 Fruity Prime向け推奨

VMAを捨てず、上位に

```text
MemoryBudgetSnapshot
AllocationRequest
AllocationDecision
MemoryTelemetry
```

を置きます。

特に、

- render target resize
- high-resolution asset
- map thumbnail pool
- Vulkan/Skia interop target

等の大きいresource生成前にbudgetを評価できると、`VK_ERROR_OUT_OF_DEVICE_MEMORY`まで突っ込む前に説明可能なfailureを返せます。

このpolicy layerはVulkan専用にしすぎない方が良いです。melonPrimeDSはD3D12にもDXGI live budgetを使うpure admission modelを置いています。Metalでは同じAPIが存在するわけではありませんが、`recommendedMaxWorkingSetSize`等の利用可能情報とallocation failureをbackend-specific snapshotへ落とし、上位には同じ`MemoryBudgetSnapshot / AllocationDecision`を返せます。

参照:

- [`DX12MemoryAdmission.h`](https://github.com/ag-advania/melonPrimeDS/blob/c4165c87416902bb13b670e3147ecf05988017ed/src/DX12MemoryAdmission.h)
- [`MelonPrimeMetalFeatureCheck.h`](https://github.com/ag-advania/melonPrimeDS/blob/c4165c87416902bb13b670e3147ecf05988017ed/src/frontend/qt_sdl/MelonPrimeMetalFeatureCheck.h#L15-L42)

---

# 15. P1: Upload pathをpersistent staging modelへ寄せる

## 15.1 現在のFruity Prime

`WriteBuffer()`のGPU-only pathでは、recording command listが使えない場合、

1. staging bufferを新規生成
2. map
3. memcpy
4. flush
5. unmap
6. temporary command list生成
7. transition
8. copy
9. submit

という経路があります。

Texture uploadにも類似fallbackがあります。

一方、`VulkanCommandList`自身にはring chunkを使う仕組みがあり、既にpersistent uploadへ向かう設計要素があります。

したがって問題は「upload ringがない」ことではなく、**upload policyが複数経路へ分かれていること**です。

## 15.2 melonPrimeDSの考え方

melonPrimeDSの`VulkanMemory`はワークロードに合わせてpersistent mapped stagingを明示的に扱い、map/unmapの反復を避けています。

参照:

- [`VulkanMemory.h`](https://github.com/ag-advania/melonPrimeDS/blob/c4165c87416902bb13b670e3147ecf05988017ed/src/VulkanMemory.h#L33)

melonPrimeDSがthird-party allocatorを使わないという選択自体はFruity Primeへコピーする必要はありません。Fruity PrimeではVMAを維持したまま、

```text
VulkanUploadContext
  per-frame persistently mapped staging pages
  Allocate(size, alignment)
  Flush(range)
  CopyBuffer
  CopyTexture
  retire/reuse by frame fence
```

へsubmissionを統一する方が良いです。

この形はD3D12にも直接対応します。melonPrimeDSの`DX12UploadRing`はpersistently mapped UPLOAD bufferをframeごとにresetするlinear allocatorで、Vulkan staging ringとほぼ同じ**lifetime policy**を別API mechanismで実現しています。Metalではshared/storage bufferのper-frame arenaとして同じ上位概念を使えます。

参照:

- [`DX12UploadRing.h`](https://github.com/ag-advania/melonPrimeDS/blob/c4165c87416902bb13b670e3147ecf05988017ed/src/DX12UploadRing.h)

---

# 16. P2: Synchronous readbackは「便利API」と「hot path」を分ける

`GraphicsDevice::ReadBuffer()`と`CommandList::ReadColor()`は同期readback semanticsを持ち、Vulkanの`ReadBuffer()`は`WaitIdle()`してからmap/invalidate/copyします。

参照:

- [`VulkanGraphicsDevice.cpp` - `ReadBuffer`](https://github.com/Zection6V/Fruity-Prime/blob/5503e2b35c97abbc63ff838adbc21e466831da38/src/MphRead.Native/NativeRuntime/Rhi/Vulkan/VulkanGraphicsDevice.cpp#L2948)

Diagnostic APIとしては問題ありません。

ただしcapture、thumbnail、screenshot等の通常機能で頻繁に呼ぶ場合、GPU pipelineを止めます。

推奨は、同期APIを削除するのではなく、別に

```text
ReadbackTicket
  EnqueueReadback(...)
  IsReady()
  MapResult()
```

のような非同期経路を持たせることです。

melonPrimeDSの`RendererOutputRing`は、backend-specific GPU objectを上位へ漏らさず、producer/presenter間をslot lease + serial + backend-specific ready callbackで扱っています。

参照:

- [`RendererOutputRing.h`](https://github.com/ag-advania/melonPrimeDS/blob/c4165c87416902bb13b670e3147ecf05988017ed/src/RendererOutputRing.h#L31)

Fruity Primeでthreaded presenterが不要なら、このclass構成をそのまま入れる必要はありません。ただし、**「結果のownership/backpressure contract」と「GPU completion mechanism」を分離する考え方**はreadbackにも有効です。

---

# 17. P1: Backend lifetimeをprocess-global singletonから明示ownerへ移す

## 17.1 Vulkan側

`SceneBackend.cpp`にはnamespace-globalな選択状態があり、Vulkan scene stateは

```text
static auto* scene = new VulkanScene();
```

として意図的に破棄されない形になっています。

コメント上の理由は、static resourceのdestructor orderingより後までGPU objectsが生きる可能性を避けるためです。

参照:

- [`SceneBackend.cpp`](https://github.com/Zection6V/Fruity-Prime/blob/5503e2b35c97abbc63ff838adbc21e466831da38/src/MphRead.Native/NativeRuntime/Rhi/SceneBackend.cpp#L28)

これはprocess exitだけを考えれば実用的な回避策ですが、

- backend hot-switch
- repeated create/destroy test
- validation leak checks
- deterministic shutdown
- device-loss recovery

を難しくします。

## 17.2 OpenGL側

OpenGLも`ContextDevice()`がfunction-static `unique_ptr<OpenGlGraphicsDevice>`を所有します。

参照:

- [`OpenGlDevice.cpp` - `ContextDevice`](https://github.com/Zection6V/Fruity-Prime/blob/5503e2b35c97abbc63ff838adbc21e466831da38/src/MphRead.Native/NativeRuntime/Rhi/OpenGL/OpenGlDevice.cpp#L1250)

## 17.3 推奨形

明示的なownerを置きます。

```text
RendererRuntime
  BackendSelection
  BackendSession
    GraphicsDevice
    Swapchain
    ShaderLibrary
    ResourceCaches
    UploadContext
    Diagnostics
```

backend switch時は、

```text
1. stop producing new GPU work
2. finish/retire current frame
3. release backend GPU resources
4. destroy swapchain/context/device in deterministic order
5. create new BackendSession
6. rebuild GPU resources from CPU-side source assets
```

とします。

**CPU-side model/texture sourceとGPU-side resourceを分離すること**が重要です。これは現在のFruity Primeのrenderer-switch対応方向とも整合します。

さらに4 backend化では、probeを二段階に分けるべきです。

```text
Passive probe
  OS / loader / framework / build supportだけ確認
  native deviceを作らない

Active admission
  adapter/device/queue/required shader capabilityを実際に検証
  outgoing backend teardown後に実施
```

この分離は、Vulkan→D3D12、Vulkan(MoltenVK)→Metal、OpenGL→Metal等のruntime switchでdriver/device lifecycleを重ねないために有効です。

---

# 18. P2: Renderer switch / resizeを専用stress testにする

melonPrimeDSには`MelonPrimeRendererSwitchStress`があり、

- renderer switching: backend自体をtear down/rebuild
- scale switching: backendを維持しresolution-dependent resourcesだけ再作成

を別のlifecycleとして明確にテストします。

参照:

- [`MelonPrimeRendererSwitchStress.cpp`](https://github.com/ag-advania/melonPrimeDS/blob/c4165c87416902bb13b670e3147ecf05988017ed/src/frontend/qt_sdl/MelonPrimeRendererSwitchStress.cpp#L48)

Fruity Primeにも既にlifetime系diagnosticがありますが、OpenGL/Vulkan runtime switchingを今後正式機能にするなら、専用stress harnessを置く価値があります。

推奨ケース:

```text
OpenGL -> Vulkan -> OpenGL -> Vulkan ... 100 cycles
```

各cycleで、

- scene load
- model upload
- render target resize
- thumbnail / HUD / launcher resources
- window resize
- present
- scene unload

を実施し、

- validation error = 0
- live GPU resource countがbaselineへ戻る
- retired queueが最終的に0
- stale resource reuseなし
- device-wide wait回数が想定外に増えない
- screenshot/parity contract維持

を確認します。

---

# 19. melonPrimeDSから取り入れるべき設計思想

## 19.1 「backend-neutral protocol」と「backend-specific mechanism」を分ける

`RendererOutputRing`はslot publication、lease、serial、backpressureをbackend-neutralにし、GPU完了判定だけをcallbackにしています。

Fruity PrimeのRHIにも同じ思想を徹底すべきです。

例:

```text
resource retirement policy = RHI共通
fence query              = backend固有

pipeline description     = RHI共通
VkPipeline / GL program  = backend固有

readback ticket lifecycle = RHI共通
VkFence / GLsync          = backend固有
```

## 19.2 「change axis」で分離する

melonPrimeDSの`VulkanPipelineCache`が良い例です。

pipeline compilationそのものと、

- cache serialization
- device identity
- corrupted cache rejection

は別の変更理由なので別componentにしています。

Fruity Primeでも同様に、

- synchronization
- resource lifetime
- descriptor allocation
- memory policy
- feature probing
- pipeline cache persistence
- presentation timing

を独立させるべきです。

## 19.3 Device API queryとpolicy判定を分ける

`VulkanMemoryAdmission`はpure policyにしています。

これは非常に取り入れる価値があります。

```text
query hardware/driver -> snapshot
snapshot + request    -> pure decision
pure decision         -> unit test
```

という構造にすると、GPUがなくてもpolicy testができます。

## 19.4 Optional featureはcore pathへ埋め込まない

melonPrimeDSのpresent pacingは、optional extensionごとのresult classificationやdispatch subsetを専用componentへ隔離しています。

参照:

- [`VulkanPresentPacer.h`](https://github.com/ag-advania/melonPrimeDS/blob/c4165c87416902bb13b670e3147ecf05988017ed/src/VulkanPresentPacer.h)

Fruity Primeでも将来、

- NVIDIA Reflex
- AMD Anti-Lag
- present wait / display timing
- telemetry
- GPU timestamps

を追加する場合、`VulkanGraphicsDevice`へ直接積み上げず、optional controllerとして分離した方が良いです。

## 19.5 「最新Vulkan機能を使うこと」自体をbest practiceにしない

melonPrimeDSは明示的に、Vulkan 1.1をbaselineとし、timeline semaphore、dynamic rendering、Synchronization2を使わない設計を選んでいます。

Fruity Primeは逆にVulkan 1.3 + dynamic rendering + Synchronization2を使っています。

どちらも、そのsoftwareのsupport matrixに合っていれば成立します。

重要なのは、

- なぜその最低条件なのか
- fallbackするのかfailするのか
- どのfeatureがrequired / optionalなのか
- failure理由をどう報告するか

を明文化・テスト可能にすることです。

---

# 20. Fruity Primeで取り入れない方が良いmelonPrimeDS要素

## 20.1 melonPrimeDSのbackend-specific renderer構造をそのままコピーしない

Fruity PrimeのRHIは、OpenGL/Vulkan/将来のD3D12/Metalを同じscene rendererから扱うための良い基礎です。

melonPrimeDSの`GPU3D_OpenGL` / `GPU3D_Vulkan`のように上位rendererをbackendごとに大きく分ける方向へ戻す必要はありません。

## 20.2 melonPrimeDSが独自memory allocatorだからといってVMAを外さない

melonPrimeDSは「少数・巨大・長寿命allocation」という独自workloadのため、third-party suballocatorを使わない理由を明示しています。

Fruity Primeのallocation patternは異なります。

**VMAを維持し、budget/admission/telemetryだけ取り入れる**方が合理的です。

## 20.3 Optional extensionを全部入れない

melonPrimeDSはlow-latency/present telemetry関連がかなり高度です。

Fruity Primeの現在の優先順位では、まず

1. lifetime correctness
2. RHI path unification
3. deterministic backend lifecycle
4. allocation/upload efficiency

を終える方が先です。

present pacingの高度化はその後で十分です。


## 20.4 Vulkan set / D3D12 root parameter / Metal argument bufferをRHIの共通語彙にしない

melonPrimeDSの`VulkanDescriptors`や`DX12RootSignatureLayout`は各backend内部では非常に良いsingle source of truthです。しかし、その**native配置そのもの**をFruity Primeのcore RHIへ持ち込むべきではありません。

RHIで共通にするのは、

```text
Frame resources
Material resources
Draw resources
Post resources
```

というlogical groupとresource typeまでです。

そのgroupを、

```text
Vulkan -> descriptor set/binding
D3D12  -> root parameter/descriptor table/register
Metal  -> buffer/texture/sampler index or argument buffer
OpenGL -> binding point/texture unit
```

へ配置するのはbackend shader ABI adapterの責務にします。

これにより、将来Metalでargument bufferを使う/使わない、D3D12でroot CBVを使う、Vulkanでdynamic uniform offsetを使う、といった最適化をScene/RHI API変更なしで選択できます。

---

# 21. 推奨リファクタリング順序

Metal/D3D12を実装する予定なら、**Vulkanを先に完全整理してからAPIを増やす**だけではなく、最初に4 backend共通contractを固定する方が安全です。

## Phase A: 4-backend RHI contractを固定

### 作業

- `SceneBackendKind`を`GraphicsBackend`へ統合
- `BackendProvider` / `BackendSession`境界を導入
- `SceneBackend.hpp`からOpenGL/Vulkan固有shader typeを排除
- `SceneShaderAbi`をbackend-neutral化
- `PipelineLayout`を導入し複数binding groupを正式表現
- samplerをtexture stateから分離
- `Acquire/Present`のtyped resultとpresentation capabilityを定義

### 完了条件

- Scene/Renderer headerがGL/Vulkan/D3D12/Metal native headerをincludeしない
- 新backend追加時、Scene renderer本体へbackend分岐を追加しなくてよい
- shader/binding ABIが1つのcontract testで検証できる

---

## Phase B: GPU lifetimeをsubmission serial基準へ統一

### 作業

- `SubmissionSerial` / completion tokenをbackend-neutral化
- deferred destructionをframe number依存からsubmission completion依存へ整理
- Vulkan Buffer/Sampler/Texture/View/Pipelineのnative destructionをqueueへ集約
- Texture resizeをallocate-new + retire-oldへ変更
- `WaitIdle`をteardown/explicit transitionへ限定

### 完了条件

- resource destroy/resize stressでvalidation 0
- resource churnでdevice-wide waitが発生しない
- OpenGL/Vulkan/D3D12/Metalで同じlifetime contractを実装できる

---

## Phase C: Vulkan内部責務を分離・完成

### 作業

- descriptor pool logicを`VulkanDescriptorAllocator`へ抽出
- frame fences/submissionを`VulkanFrameScheduler`へ抽出
- VMA-backed resource creationを`VulkanResources`へ抽出
- staging ringを`VulkanUploadContext`へ統合
- feature selectionを`VulkanFeatureProbe`へ抽出
- persistent `VkPipelineCache`
- memory budget/admission/telemetry

### 完了条件

- `VulkanGraphicsDevice`はRHI entry pointとsubsystem orchestrationが中心
- sync/lifetime/memory/pipeline policyを独立テスト可能

---

## Phase D: OpenGLを本当のRHI backendにする

### 作業

- `CreateBuffer`
- `SetVertexBuffer`
- `SetIndexBuffer`
- `SetBindingSet`
- `Draw`
- `DrawIndexed`
- copy operations
- GL sampler object/cache

を実装し、`OpenGlGeometry`の直接submissionをRHIへ吸収します。

### 完了条件

- Renderer domainからGL buffer/name/stateを扱わない
- scene geometryとtransient geometryが`CommandList`経由
- GL state mutation ownerがbackend内部に限定

---

## Phase E: Desktop OpenGL compatibility debtを除去

### 作業

- desktop shaderのlegacy built-insをexplicit inputへ移行
- conventional client arrays mirrorを削除
- VAOをpipeline/vertex-layout cacheと組み合わせる
- shader interface validationを4 backend共通ABIへ接続

### 完了条件

- `glEnableClientState`不要
- `glVertexPointer` / `glColorPointer` / `glTexCoordPointer`不要
- shader attribute aliasingに依存しない
- Golden Capture parity維持

---

## Phase F: Metal backend

### 実装方針

- `MetalBackendProvider` / `MetalBackendSession`
- `MTLDevice` / command queue / drawable surfaceをsession内部所有
- `GraphicsPipelineDesc` -> `MTLRenderPipelineState` + depth/stencil state
- RHI binding group -> Metal buffer/texture/sampler index、必要ならargument buffer
- per-frame shared upload arena
- command-buffer completionをsubmission serialへ変換
- Metal feature probeをdevice creationと分離

melonPrimeDSのMetal実装から取り入れるべきなのは、frame slotごとのresource ownership、in-flight completion確認、scale再構築時の明示的resource release、baseline feature smoke testです。

参照:

- [`GPU3D_MetalCompute.h`](https://github.com/ag-advania/melonPrimeDS/blob/c4165c87416902bb13b670e3147ecf05988017ed/src/GPU3D_MetalCompute.h)
- [`MelonPrimeMetalFeatureCheck.h`](https://github.com/ag-advania/melonPrimeDS/blob/c4165c87416902bb13b670e3147ecf05988017ed/src/frontend/qt_sdl/MelonPrimeMetalFeatureCheck.h)

### 完了条件

- MetalだけのScene分岐なし
- same RHI conformance suiteがOpenGL/Vulkanと通る
- resize/backend switchでstale drawable/resourceなし
- screenshot parity gateが成立

---

## Phase G: D3D12 backend

### 実装方針

- `D3D12BackendProvider` / `D3D12BackendSession`
- command allocator/list/fenceをper-frame contextへ
- persistent upload ring
- descriptor heap/ringをbinding lifetime別に管理
- `ResourceState` -> `D3D12_RESOURCE_STATES` mapping
- `PipelineLayout` -> root signature / descriptor table mapping
- semantic pipeline cache + `ID3D12PipelineLibrary`/cached PSO
- DXGI memory budget/admission
- device removal reasonをtyped backend errorへ変換

melonPrimeDSのD3D12は、command context、upload ring、descriptor ring、resource factory、pipeline repositoryを既にchange axisで分けており、Fruity PrimeのD3D12 backend内部構造を考える上で参考価値が高いです。

参照:

- [`DX12CommandContext.h`](https://github.com/ag-advania/melonPrimeDS/blob/c4165c87416902bb13b670e3147ecf05988017ed/src/DX12CommandContext.h)
- [`DX12DescriptorRing.h`](https://github.com/ag-advania/melonPrimeDS/blob/c4165c87416902bb13b670e3147ecf05988017ed/src/DX12DescriptorRing.h)
- [`DX12UploadRing.h`](https://github.com/ag-advania/melonPrimeDS/blob/c4165c87416902bb13b670e3147ecf05988017ed/src/DX12UploadRing.h)
- [`DX12ResourceFactory.h`](https://github.com/ag-advania/melonPrimeDS/blob/c4165c87416902bb13b670e3147ecf05988017ed/src/DX12ResourceFactory.h)
- [`DX12PipelineRepository.h`](https://github.com/ag-advania/melonPrimeDS/blob/c4165c87416902bb13b670e3147ecf05988017ed/src/DX12PipelineRepository.h)

### 完了条件

- D3D12 native descriptor/root-signature detailがRHI外へ出ない
- same RHI conformance suiteが通る
- D3D12 debug layer / GPU-based validation対象テストでerror 0
- screenshot parity gateが成立

---

## Phase H: 4-backend switch / resize / recovery stress

### 推奨ケース

```text
Windows:
  OpenGL -> Vulkan -> D3D12 -> OpenGL ...

macOS:
  OpenGL -> Vulkan(MoltenVK) -> Metal -> OpenGL ...
```

各cycleで、scene load、model upload、render target resize、thumbnail/HUD/launcher resources、window resize、present、scene unloadを通します。

### 完了条件

- backendを繰り返しcreate/destroy可能
- intentional leaked singleton不要
- resource countがbaselineへ復帰
- retired/deferred queueが最終的に0
- stale resource / stale descriptor / stale drawable reuseなし
- device-wide waitが設計したtransition boundary以外で増えない
- Golden Capture / screenshot parity維持


# 22. 優先順位付き改善一覧

| ID | 優先度 | 改善 | 4-backendでの効果 | melonPrimeDSの参考 |
|---|---|---|---|---|
| R1 | **P0** | `SceneBackendKind`を`GraphicsBackend`/Providerへ統合 | Metal/D3D12追加時のScene分岐増殖を防止 | renderer transition / feature checks |
| R2 | **P0** | `SceneShaderAbi`をbackend-neutral化 | GLSL/SPIR-V/DXIL/MSLのinterface drift防止 | Vulkan/DX12 binding contract思想 |
| R3 | **P0** | `PipelineLayout`で複数binding groupを正式化 | Vulkan set / D3D12 table / Metal bindingを同じ意味論で表現 | `VulkanDescriptors`, `DX12RootSignatureLayout` |
| R4 | **P0** | submission serialベースdeferred destruction | 4 APIで同一lifetime contract、stall削減 | `DeferredDestroyQueue`, D3D12 fence, Metal command completion |
| R5 | **P0** | Texture resizeから`vkDeviceWaitIdle`除去 | resize/hot-switch stall削減 | frame-retired resource model |
| R6 | **P0** | OpenGL `CommandList` Buffer/Draw実装 | RHI二重経路解消 | explicit VAO/VBO submission |
| R7 | **P1** | samplerをtexture stateから分離 | D3D12/Metal/Vulkanと意味論を一致 | API native sampler separation |
| R8 | **P1** | `BackendSession` + deterministic teardown | safe hot-switch / testability | DX12 passive/active admission |
| R9 | **P1** | Presentation request/capability/status化 | Vulkan/DXGI/Metal/GLのmode差を吸収 | presenter分離思想 |
| R10 | **P1** | Vulkan subsystem分割 | testability、保守性 | Sync/Memory/Descriptors/Probe分離 |
| R11 | **P1** | backend native pipeline library | warm-up/compile stall低減 | `VulkanPipelineCache`, `DX12PipelineRepository` |
| R12 | **P1** | cross-backend memory budget/admission | predictable OOM、scale/resource判断 | Vulkan/DX12 admission |
| R13 | **P1** | upload context/ring統一 | allocation/map/submission churn削減 | Vulkan staging / `DX12UploadRing` |
| R14 | **P1** | feature probeをpassive/activeへ分離 | backend transition中のdevice競合を防止 | DX12/Metal feature checks |
| R15 | **P2** | OpenGL explicit attributes/VAO化 | compatibility alias問題解消 | melon GL vertex contract |
| R16 | **P2** | async readback ticket | screenshot/capture stall削減 | output lease/backpressure思想 |
| R17 | **P2** | typed device/surface error | device lost / surface lost処理の統一 | API別failure classification思想 |
| R18 | **P2** | cross-backend debug labels/timestamps | GPU診断を同じテストから利用 | Vulkan/DX12 telemetry components |
| R19 | **P2** | 4-backend conformance/switch stress | API追加時のsemantic drift検出 | renderer-switch stress |
| R20 | **P3** | present pacing / low-latency extension layer | Reflex/Anti-Lag/XeLL等をcore RHIから隔離 | dedicated low-latency controllers |


# 23. 最終的に目指す責務境界

```text
Renderer / Scene
    |
    | backend-neutral semantics only
    v
RHI Core
  GraphicsDevice
  CommandList
  Resources / Views / Samplers
  GraphicsPipelineDesc
  PipelineLayout / BindingLayout / BindingSet
  SceneShaderAbi
  ResourceState / SubmissionSerial
  PresentationRequest / PresentResult
    |
    v
BackendProvider / BackendSession
    |
    +------------------+------------------+------------------+
    |                  |                  |                  |
    v                  v                  v                  v
OpenGL Backend     Vulkan Backend     Metal Backend       D3D12 Backend
  GL Device          Vk Device          MTLDevice          D3D12 Device
  GL Command         FrameScheduler     Command Queue       CommandContext
  GL Resources       Resources          Resources           ResourceFactory
  VAO/State Cache    DeferredDelete     Frame Arenas        Descriptor Rings
  GLSL Programs      DescriptorAlloc    Pipeline Cache      Upload Rings
  GL Samplers        UploadContext      Surface/Drawable    PipelineRepository
  GL Swap/Surface    PipelineLibrary    FeatureProbe        DXGI Swapchain
                     FeatureProbe                          FeatureProbe
                     Swapchain
```

上位`Renderer` / `Scene`が一切知るべきでないもの:

```text
GLuint / GL client state
VkImage / VkBuffer / VkDescriptorSet / VkFence
id<MTLTexture> / id<MTLCommandBuffer> / CAMetalDrawable
ID3D12Resource / descriptor handle / root parameter / fence value
```

例外は、Skia等とのnative interopが本当に必要な専用bridgeだけです。その場合も`Renderer`へnative handleを散らさず、`InteropSurface` / `ExternalImage`のような狭いbackend extensionへ隔離します。

## 23.1 Core RHIへ入れない方が良いもの

次の機能はAPI差が大きいため、generic `GraphicsDevice`へ無理に共通化しない方が良いです。

- NVIDIA Reflex / AMD Anti-Lag / Intel XeLL
- Vulkan present-wait/display-timing extension
- DXGI frame-latency waitable object
- Metal-specific display timing/counter features
- PIX / Xcode GPU capture / RenderDoc固有integration
- Skia native backend interop

これらは`BackendSession`のoptional capability/controllerとして公開し、core rendererが使う場合も抽象的な`LowLatencyController`等の狭いinterface経由にします。


# 24. 総評

Fruity Primeは、Metal/D3D12を追加するためにmelonPrimeDS型のbackend別rendererへ作り直す必要はありません。むしろ現在のRHIを**4 APIが実装できるsemantic contractへ仕上げる**のが最も良い方向です。

今回、将来backendの観点を加えると、優先順位はより明確になります。

1. **Scene/backend境界からOpenGL/Vulkan固有型を除去し、`BackendProvider / BackendSession`へする。**
2. **shader ABIを`VulkanShaderInterface`からbackend-neutral `SceneShaderAbi`へ移し、GLSL/SPIR-V/DXIL/MSLを同じcontractへ適合させる。**
3. **`PipelineLayout`を導入し、複数binding groupとtexture/sampler分離をRHIの正式仕様にする。**
4. **GPU lifetimeをframe番号より一段一般的なsubmission completionで管理し、Vulkanのsteady-state waitを除去する。**
5. **OpenGLの別submission pathをなくし、4 backendすべてが同じ`CommandList`意味論を実装する。**
6. **presentation、feature probe、pipeline cache、memory admission、upload ringは共通policyとbackend mechanismを分離する。**

melonPrimeDSから特に参考になるのは、Vulkanだけではありません。D3D12側では`DX12CommandContext`、`DX12DescriptorRing`、`DX12UploadRing`、`DX12PipelineRepository`が責務別に切られ、Metal側でもfeature baselineとframe-slot/resource lifetimeを明示しています。

一方で、Fruity Primeが維持すべき独自の強みは、`ScenePass`やtyped RHIによって**1つのScene rendererを複数APIへ投影できる構造**です。

最終的な設計原則は次の一文にまとめられます。

> **Sceneは描画の意味だけを記述し、RHIは共通のGPU意味論だけを定義し、OpenGL/Vulkan/Metal/D3D12固有の同期・descriptor・shader binary・presentation・最適化は各BackendSession内部に閉じ込める。**

この境界をMetal/D3D12実装前に固定すれば、将来4 backendになっても「4個のrendererを保守する」のではなく、**1個のrenderer + 4個のbackend adapterを保守する構造**にできます。
