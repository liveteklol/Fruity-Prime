# Fruity-Prime C++版 OpenGL → RHI → Vulkan 実装計画
## Phase-by-Phase Worker AI Execution Plan

- Repository: `Zection6V/Fruity-Prime`
- Branch: `develop3_rendering`
- Baseline HEAD: `bb8f619da7abbe614ea60765006f290a60938f98`
- Baseline date: 2026-09-28
- Scope: **C++版のみ**
- Immediate implementation targets:
  - Desktop OpenGL
  - Desktop Vulkan
  - Android Vulkan
- Existing Android OpenGLES: **維持する。今回の新規RHI実装の破壊対象にしない**
- Future backends:
  - Metal
  - Direct3D 12
- C#版: **変更禁止**
- Primary owner:
  - `src/MphRead.Native`
- Android platform owner:
  - `src/MphRead.Native.Android`

---

# 0. この文書の役割

この文書は設計メモではなく、**ワーカーAIへPhase単位でそのまま渡して実装を進めるための実行計画**である。

一度に全Phaseを実装させない。

基本運用:

```text
Phase Nを依頼
↓
Phase Nだけ実装
↓
静的監査
↓
build
↓
可能なruntime検証
↓
commit
↓
exact commit CI確認
↓
Phase N完了判定
↓
Phase N+1
```

各Phaseは、前Phaseの完成状態を前提とする。

Phaseを飛ばしてVulkan実装を先に作ってはならない。

---

# 1. 今回の最終目標

今回完成させる構造:

```text
Game / Scene / Entity / HUD / Launcher
                  │
                  ▼
          Renderer Frontend
                  │
                  ▼
        Explicit Render Passes
                  │
                  ▼
                 RHI
        ┌─────────┴─────────┐
        ▼                   ▼
     OpenGL               Vulkan
        │                   │
        └─────────┬─────────┘
                  ▼
                 GPU
```

将来的にはRHIの下だけを追加する。

```text
                 RHI
    ┌────────┬────────┬────────┬────────┐
    ▼        ▼        ▼        ▼
 OpenGL   Vulkan    Metal    D3D12
```

将来Metal/D3D12を追加するとき、

```text
Scene
Renderer Frontend
RenderItem
Material
Mesh
HUD
Render pass ordering
```

を原則変更しなくてよい構造を今回の段階で作る。

---

# 2. 今回やらないこと

今回のOpenGL/Vulkan作業へ以下を混ぜない。

- Metal backend実装
- D3D12 backend実装
- ray tracing
- mesh shader
- bindless全面移行
- async compute
- dedicated transfer queue最適化
- parallel command recording
- renderer threadの大規模再設計
- ECS化
- Render Graph全面導入
- gameplay rewrite
- UI rewrite
- C#版変更
- Android UIの無関係な全面書き換え
- shader表現の芸術的リファクタ
- 無関係な性能最適化

今回の優先順位:

```text
Correctness
↓
Backend separation
↓
OpenGL parity
↓
Vulkan parity
↓
Stability
↓
Performance tuning
```

---

# 3. 現行develop3_renderingの重要なBaseline

Baseline:

```text
develop3_rendering
bb8f619da7abbe614ea60765006f290a60938f98
```

現行C++ Rendererの主な特徴:

```text
src/MphRead.Native/Renderer.cpp
約 6611 lines
GL:: 呼び出し 約 1104 箇所

GL::Begin 約 34
GL::End 約 31
display-list related calls 約 8
GL::UseProgram 約 9
GL::BindTexture 約 40
GL::BindFramebuffer 約 10
GL::Uniform* 約 115
GL::ReadPixels 約 3
```

これは目安であり、実作業開始時に必ず再計測する。

現行構造:

```text
Renderer.cpp
  ↓ direct
OpenTK::Graphics::OpenGL::GL
```

また、

```text
NativeRuntime/Skia/SkiaGpu.cpp
```

もOpenGL GaneshとOpenGL stateに直接依存している。

現行CMake desktop:

```text
find_package(OpenGL REQUIRED)
OpenGL::GL
glfw
Skia[gl,freetype]
```

を前提としている。

現行shader:

```text
GLSL 120
gl_Vertex
gl_Normal
gl_Color
gl_MultiTexCoord0
```

を使用する。

現行desktop window:

```text
GLFW OpenGL compatibility context
```

を作成する。

---

# 4. 最重要設計ルール

## 4.1 OpenGL wrapperをRHIと呼ばない

禁止:

```cpp
rhi.EnableBlend();
rhi.DisableDepth();
rhi.BindTexture();
rhi.UseProgram();
rhi.Begin();
rhi.Vertex();
rhi.End();
```

これはRHIではない。

Vulkan、Metal、D3D12へ不自然なOpenGL state machineを強制する。

---

## 4.2 RHIは明示的GPU API型にする

共通概念:

```text
GraphicsDevice
CommandList
Buffer
Texture
TextureView
Sampler
Shader
GraphicsPipeline
BindingLayout
BindingSet
Swapchain
Fence / completion value
FrameContext
ResourceState
Capabilities
```

---

## 4.3 Backend固有handleをFrontendへ漏らさない

通常コードで禁止:

```text
GLuint
VkImage
VkBuffer
VkPipeline
VkDescriptorSet
VkCommandBuffer
ID3D12Resource
MTLTexture
```

許可範囲:

```text
NativeRuntime/Rhi/OpenGL/*
NativeRuntime/Rhi/Vulkan/*
明示されたSkia interop adapter
platform surface adapter
```

---

## 4.4 Vulkan固有概念をRHI APIにしない

将来D3D12/Metalを追加するため、以下を共通公開API名にしない。

禁止例:

```text
DescriptorSet
DescriptorPool
VkImageLayout
PipelineBarrier2
VkQueue
VkSemaphore
```

共通側では:

```text
BindingSet
BindingAllocator
ResourceState
Transition
GraphicsQueue abstraction
Completion/Fence abstraction
```

とする。

---

## 4.5 Metal/D3D12用stub backendを作らない

今回作るbackend:

```text
OpenGL
Vulkan
```

のみ。

以下は禁止:

```text
MetalDevice.cpp
D3D12Device.cpp
return Unsupported; だけの巨大stub
dummy backend
```

ただしenumやfactoryが将来拡張できる設計にはしてよい。

---

## 4.6 Capabilityで差分を扱う

Frontendで禁止:

```cpp
if (backend == Vulkan) { ... }
if (backend == OpenGL) { ... }
```

必要な差は:

```cpp
if (device.Capabilities().SupportsX) { ... }
```

またはbackend内部で吸収する。

---

# 5. 将来Metal/D3D12へ拡張できるRHI契約

## 5.1 Backend type

例:

```cpp
enum class GraphicsBackend
{
    OpenGL,
    Vulkan,
    Metal,
    D3D12
};
```

今回実装するfactory:

```text
OpenGL
Vulkan
```

のみ。

Metal/D3D12選択要求が来た場合は明確なunsupported error。

dummy implementationへ進まない。

---

## 5.2 Buffer

共通descriptor例:

```cpp
struct BufferDesc
{
    std::size_t Size;
    BufferUsage Usage;
    MemoryUsage Memory;
    std::string DebugName;
};
```

`BufferUsage`:

```text
Vertex
Index
Constant
Storage
TransferSrc
TransferDst
Readback
```

Metal/D3D12でも成立する意味だけを持たせる。

---

## 5.3 Texture

例:

```cpp
struct TextureDesc
{
    uint32_t Width;
    uint32_t Height;
    uint32_t MipLevels;
    TextureFormat Format;
    TextureUsage Usage;
    SampleCount Samples;
    std::string DebugName;
};
```

Usage:

```text
Sampled
ColorAttachment
DepthStencilAttachment
TransferSrc
TransferDst
Storage
Present
```

---

## 5.4 Resource State

共通状態:

```text
Undefined
CopySource
CopyDestination
VertexBuffer
IndexBuffer
ConstantBuffer
ShaderResource
StorageRead
StorageWrite
ColorAttachment
DepthWrite
DepthRead
Present
```

Backend変換:

```text
OpenGL
    → logical state / state cache / memory barrier where required

Vulkan
    → stage
    → access
    → image layout

D3D12 future
    → D3D12_RESOURCE_STATES

Metal future
    → usage / encoder ordering / barriers
```

---

## 5.5 Binding model

共通:

```text
BindingLayout
BindingSet
```

用途:

```text
Frame bindings
Material bindings
Draw/Object bindings
```

Vulkan:

```text
VkDescriptorSetLayout
VkDescriptorSet
```

OpenGL:

```text
texture units
UBO binding points
sampler objects
```

Future D3D12:

```text
Root Signature
Descriptor Table
```

Future Metal:

```text
buffer/texture/sampler slots
argument buffer if later selected
```

---

## 5.6 GraphicsPipelineDesc

共通:

```text
Shader set
Vertex layout
Topology
Rasterizer state
Depth state
Stencil state
Blend state
Color formats
Depth format
Sample count
Binding layout
```

OpenGL backendはこれを、

```text
GL program
+
state bundle
```

へ変換する。

Vulkan backendは、

```text
VkPipeline
```

へ変換する。

---

# 6. 推奨ファイル構成

今回作る:

```text
src/MphRead.Native/
└─ NativeRuntime/
   └─ Rhi/
      ├─ Backend.hpp
      ├─ BackendFactory.hpp
      ├─ BackendFactory.cpp
      ├─ GraphicsDevice.hpp
      ├─ CommandList.hpp
      ├─ Resources.hpp
      ├─ Pipeline.hpp
      ├─ Bindings.hpp
      ├─ Swapchain.hpp
      ├─ FrameContext.hpp
      ├─ Capabilities.hpp
      ├─ ResourceState.hpp
      ├─ Formats.hpp
      │
      ├─ OpenGL/
      │  ├─ GlDevice.hpp
      │  ├─ GlDevice.cpp
      │  ├─ GlCommandList.hpp
      │  ├─ GlCommandList.cpp
      │  ├─ GlResources.hpp
      │  ├─ GlResources.cpp
      │  ├─ GlPipeline.hpp
      │  ├─ GlPipeline.cpp
      │  ├─ GlBindings.hpp
      │  ├─ GlBindings.cpp
      │  ├─ GlSwapchain.hpp
      │  └─ GlSwapchain.cpp
      │
      └─ Vulkan/
         ├─ VkDevice.hpp
         ├─ VkDevice.cpp
         ├─ VkInstance.hpp
         ├─ VkInstance.cpp
         ├─ VkCommandList.hpp
         ├─ VkCommandList.cpp
         ├─ VkResources.hpp
         ├─ VkResources.cpp
         ├─ VkPipeline.hpp
         ├─ VkPipeline.cpp
         ├─ VkBindings.hpp
         ├─ VkBindings.cpp
         ├─ VkSwapchain.hpp
         ├─ VkSwapchain.cpp
         ├─ VkMemory.hpp
         ├─ VkMemory.cpp
         ├─ VkSynchronization.hpp
         └─ VkSynchronization.cpp
```

ファイル数は必要に応じて調整してよい。

ただし、

```text
Rhi/
OpenGL/
Vulkan/
```

の境界は維持する。

---

# 7. Worker AI共通作業規則

各Phaseを担当するAIは必ず以下を行う。

## 作業開始前

1. `develop3_rendering` 最新HEADを取得
2. 前PhaseのcommitがHEADに含まれることを確認
3. 対象ファイルを全読
4. 関連直接caller/calleeを確認
5. 現行挙動を理解してから編集開始

---

## 編集中

禁止:

- unrelated cleanup
- unrelated rename
- formatting-only大量変更
- force push
- C#変更
- APIを通すだけのdummy return
- broad `catch (...) {}` で不具合隠蔽
- silent fallback
- unsupported pathを「成功」と返す
- CIだけ通すための機能無効化

---

## Phase終了時

必ず記録:

```text
Base SHA
Final SHA
Changed files
Added files
Deleted files
Build commands
Test commands
Runtime tests
Static audits
Known limitations
CI run IDs
```

---

# 8. Phase 0 — Baseline固定・計測・Golden capture

## 目的

変更前OpenGLの正解状態を固定する。

このPhaseではrenderer behaviorを変更しない。

---

## 対象

主に:

```text
docs/
tools/
必要ならdebug-only capture helper
```

---

## 作業

### 0.1 最新GL依存集計

以下を記録:

```bash
rg -n "\bGL::" src/MphRead.Native
rg -n "GL::Begin|GL::End" src/MphRead.Native
rg -n "GenLists|NewList|CallList|DeleteLists" src/MphRead.Native
rg -n "gl_Vertex|gl_Normal|gl_Color|gl_MultiTexCoord" src/MphRead.Native
```

対象外分類:

```text
OpenGL backend候補
Skia GL interop
diagnostics
game renderer direct dependency
```

---

### 0.2 GPU resource ownership一覧

`Renderer.hpp/.cpp` から以下の所有者を記録:

```text
shader program
texture
framebuffer
renderbuffer
display list
depth texture
cel texture
screen texture
HUD textures
mask textures
model textures
```

---

### 0.3 Frame orderを書き出す

現行:

```text
simulation
Scene::OnDrawFrame
Scene::OnRenderFrame
Skia Shell::TickUi
UiOverlay
LauncherHunter
SwapBuffers
AfterRenderFrame
```

を実ソースから確認して文書化する。

---

### 0.4 Render pass semanticsを書き出す

特に:

```text
opaque
decal
translucent stencil pre-pass
depth clear
opaque depth rebuild
translucent pass
preview
HUD model
cel outline
RTT composite
HUD objects
fade
UI
present
```

を正確に記録する。

---

### 0.5 Golden image候補

最低限:

```text
Launcher
Offline map
Hunter
room geometry
transparent object
decal
particle
trail
HUD
pause menu
Map Vote
cel off
cel on
cel outline
fog on
fog off
fade
whiteout/disruption
end screen
```

同じ:

```text
resolution
camera
hunter
map
settings
```

で比較できるよう固定する。

---

## 完了条件

- [x] Baseline SHA記録
- [x] GL依存数記録
- [x] render order記録
- [x] resource ownership表完成
- [x] golden capture条件固定
- [x] renderer behavior変更なし

---

## Commit例

```text
Document native renderer baseline before RHI migration
```

---

# 9. Phase 1 — RHI Core型だけ導入

## 目的

まだrenderer挙動を変えず、将来OpenGL/Vulkan/Metal/D3D12で共用できるRHI契約を追加する。

---

## 重要

このPhaseではRenderer.cppをRHIへ全面移行しない。

まず型を固定する。

---

## 追加候補

```text
NativeRuntime/Rhi/Backend.hpp
NativeRuntime/Rhi/GraphicsDevice.hpp
NativeRuntime/Rhi/CommandList.hpp
NativeRuntime/Rhi/Resources.hpp
NativeRuntime/Rhi/Pipeline.hpp
NativeRuntime/Rhi/Bindings.hpp
NativeRuntime/Rhi/ResourceState.hpp
NativeRuntime/Rhi/Capabilities.hpp
NativeRuntime/Rhi/Swapchain.hpp
```

---

## 実装

### 1.1 Handle設計

推奨:

```text
move-only owning object
または
typed opaque handle + device ownership
```

避ける:

```text
int textureId
void* nativeHandle
uint64_t nativeObject
```

をFrontend APIへ公開すること。

---

### 1.2 Resource descriptors

作る:

```text
BufferDesc
TextureDesc
TextureViewDesc
SamplerDesc
ShaderDesc
GraphicsPipelineDesc
BindingLayoutDesc
BindingSetDesc
SwapchainDesc
```

---

### 1.3 Enum

最低限:

```text
GraphicsBackend
BufferUsage
TextureUsage
TextureFormat
MemoryUsage
ShaderStage
PrimitiveTopology
CullMode
FrontFace
FillMode
CompareOp
StencilOp
BlendFactor
BlendOp
ColorWriteMask
ResourceState
LoadOp
StoreOp
```

---

### 1.4 CommandList contract

最低限:

```text
Begin()
End()

BeginRendering()
EndRendering()

SetPipeline()
SetViewport()
SetScissor()

SetVertexBuffer()
SetIndexBuffer()

SetBindingSet()

SetStencilReference()

Draw()
DrawIndexed()

CopyBuffer()
CopyBufferToTexture()
CopyTextureToBuffer()

Transition()
```

---

### 1.5 将来APIに備える

RHI interfaceへOpenGL/Vulkan固有名を入れない。

将来:

```text
Metal
D3D12
```

をbackend実装だけで追加できることをコードレビューする。

---

## Unit/static tests

最低限compile test。

可能なら:

```text
descriptor equality
pipeline key hashing
format mapping helper
resource-state validation
```

を純C++ test化する。

---

## 完了条件

- [x] RHI core compile
- [x] Renderer behavior変更なし
- [x] raw GL/Vulkan typeがcommon RHI headerにない
- [x] Metal/D3D12を追加可能なinterface
- [x] dummy Metal/D3D12 implementationなし

---

## Commit例

```text
Add backend-neutral RHI core contracts
```

---

# 10. Phase 2 — Backend FactoryとWindow/Presentation分離

## 目的

現在:

```text
Window creation
=
OpenGL context creation
```

になっている構造を分離する。

---

## 対象

```text
Renderer.hpp
NativeRuntime/OpenTK/RendererPlatform.cpp
Mods/Render/DesktopGlContext.*
NativeRuntime/Rhi/BackendFactory.*
NativeRuntime/Rhi/Swapchain.*
```

---

## 2.1 Window責務

残す:

```text
OS window
event loop
input
focus
size
position
fullscreen
native handle
```

外す:

```text
必ずOpenGL contextを作る
SwapBuffersが唯一のpresent手段
```

---

## 2.2 Window creation mode

導入:

```text
GraphicsWindowMode::OpenGL
GraphicsWindowMode::NoApi
```

OpenGL:

```text
GLFW_OPENGL_API
context hints
make current
swap interval
```

Vulkan:

```text
GLFW_CLIENT_API = GLFW_NO_API
```

---

## 2.3 Presentation ownership

最終的に:

```text
RHI Swapchain::Present()
```

がpresentを担当。

OpenGL backend:

```text
glfwSwapBuffers
```

Vulkan backend:

```text
vkQueuePresentKHR
```

---

## 2.4 既存OpenGL pathを壊さない

このPhase終了時点ではOpenGL rendererは旧描画ロジックのままでよい。

目的はwindow/presentation boundaryのみ。

---

## 完了条件

- [x] OpenGL window従来動作
- [x] NoApi windowを生成可能
- [x] Vulkan device未実装でもNoApi window compile
- [x] input/window behaviorに差なし
- [x] RendererPlatformにVulkan型なし

---

## Runtime test

```text
launcher open
resize
maximize
fullscreen
minimize
restore
focus
mouse capture
close
```

---

## Commit例

```text
Separate native window ownership from graphics presentation
```

---

# 11. Phase 3 — Geometry Decoder導入

## 目的

最大のVulkan blockerである:

```text
GL::Begin
GL::Vertex
GL::Normal
GL::Color
GL::TexCoord
display lists
```

をRenderer frontendから排除できるデータ形式へ変換する。

このPhaseではまだOpenGLで描画する。

---

## 対象

主に:

```text
Renderer.cpp
Renderer.hpp
Formats / Model data direct dependencies
新規 RendererGeometry.*
または NativeRuntime/Rhi-independent geometry helper
```

---

## 3.1 Vertex format

最低限:

```cpp
struct SceneVertex
{
    Vector3 Position;
    Vector3 Normal;
    Vector4 Color;
    Vector2 TexCoord;
    uint32_t MatrixIndex;
};
```

packing最適化は後回し。

---

## 3.2 Geometry decoder

現行 `DoDlist()` のstate machineを、

```text
OpenGL immediate command emitter
```

から、

```text
CPU geometry builder
```

へ置換する。

入力:

```text
RenderInstructionList
texture width
texture height
texgen
isRoom
```

出力:

```text
vertices
indices
topology ranges
```

---

## 3.3 Current attribute stateを再現

RenderInstructionは状態持続型なので、

```text
current color
current normal
current texcoord
current position
current matrix index
```

を持つ。

Vertex命令が来た瞬間の状態をvertexへ焼く。

---

## 3.4 Primitive変換

対応:

```text
Triangles
Quads
TriangleStrip
QuadStrip
```

Primitive単位でrangeを保持してもよい。

GPU backendが理解しやすい形として最終的に:

```text
Triangles
Lines
```

中心へnormalizeしてよい。

ただし意味が完全一致すること。

---

## 3.5 Quads

OpenGL GL_QUADS依存を排除するためtriangleへ展開。

三角形分割方向は現行描画結果から決める。

勝手に対角線を選ばない。

---

## 3.6 Strip

TriangleStripではwinding parityに注意。

QuadStripでは元OpenGL semanticsを忠実に展開する。

---

## 3.7 MTX_RESTORE

現行:

```text
matrixId
```

をtexcoord Z経由でshaderへ渡している。

これを明示的:

```text
MatrixIndex
```

vertex attributeへする。

現行bit mask:

```text
& 0x1F
```

とclamp意味を維持する。

---

## 3.8 DIF_AMB

現行shaderはcolor alphaを特殊sentinelとして利用している。

意味を分析し、

```text
explicit flag
```

へ変更するのが望ましい。

ただし最初はvertex color alphaの既存意味を維持してもよい。

---

## 3.9 Decoder test

現行GL streamと新geometry outputを比較できるtestを作る。

最低限:

```text
single triangle
quad
triangle strip
quad strip
COLOR inheritance
NORMAL inheritance
TEXCOORD inheritance
VTX_16
VTX_10
VTX_XY
VTX_XZ
VTX_YZ
VTX_DIFF
MTX_RESTORE
DIF_AMB
```

---

## 完了条件

- [x] Geometry decoder存在
- [x] Decoder pure C++ test可能
- [x] 全RenderInstruction対応
- [x] 現行OpenGL描画結果をまだ維持
- [x] Vulkan code未導入

---

## Commit例

```text
Decode NDS render instructions into explicit geometry
```

---

# 12. Phase 4 — OpenGL Display List撤去・VBO/IBO化

## 目的

OpenGL自身をmodern GPU buffer modelへ移す。

Vulkanより先に実施する。

---

## 4.1 Model GPU cache

新しいbackend-neutral identity:

```text
GpuMesh
```

概念:

```text
VertexBuffer
IndexBuffer
IndexCount
Topology
```

---

## 4.2 Ownership

禁止:

```text
Mesh::ListId = GLuint
```

Model/Mesh domain objectへOpenGL IDを保存しない。

推奨:

```text
GpuMeshCache
key = Model identity + Mesh identity
```

---

## 4.3 OpenGL buffer implementation

最低限:

```text
glGenBuffers
glBindBuffer
glBufferData
glVertexAttribPointer
glEnableVertexAttribArray
glDrawElements / glDrawArrays
```

OpenGL compatibility contextをこのPhaseで即削除する必要はない。

---

## 4.4 Dynamic geometry

対象:

```text
particle
trail
collision/debug shapes
fullscreen quad
temporary HUD geometry
```

方法:

```text
per-frame transient vertex/index buffer
```

初期実装はsimple orphan/updateでもよい。

後でpersistent mappingへ最適化可能。

---

## 4.5 Display List完全撤去

削除対象:

```text
GenLists
NewList
EndList
CallList
DeleteLists

_displayLists
_displayListModels
Mesh::ListId GPU ownership
```

---

## 静的監査

Phase終了時:

```bash
rg -n "GenLists|NewList|EndList|CallList|DeleteLists" src/MphRead.Native
```

許容結果:

```text
0
```

診断コードに残す必要も基本ない。

---

## 完了条件

- [x] Game sceneがVBO/IBOで描画
- [x] display listsゼロ
- [x] immediate mode model renderingゼロ
- [x] screenshot parity確認
- [x] model unload/reload正常
- [x] hunter preview正常
- [x] match終了後preview破損なし

---

## Commit例

```text
Replace OpenGL display lists with explicit mesh buffers
```

---

# 13. Phase 5 — Explicit Vertex Input + Shader Interface modernization

## 目的

GLSL 120 built-in input依存を排除し、OpenGL/Vulkanで同じvertex semanticsを使う。

---

## 5.1 Built-in attributes撤去

対象:

```text
gl_Vertex
gl_Normal
gl_Color
gl_MultiTexCoord0
```

明示入力へ変更。

GLSL 120を維持する場合でも:

```text
attribute
```

を使える。

Vulkan shaderではlocation明示。

---

## 5.2 Semantic contract

固定:

```text
location 0: Position
location 1: Normal
location 2: Color
location 3: TexCoord
location 4: MatrixIndex
```

実際のlocationは変更してよいが、共通定義を1箇所に置く。

---

## 5.3 Constant blocks

現行115前後のGL uniform呼び出しを分類する。

最低限:

```text
FrameConstants
SceneConstants
MaterialConstants
DrawConstants
Hud/PostConstants
```

---

## 5.4 Matrix stack

現行:

```text
mat4[32] mtx_stack
```

意味を維持。

最初はUniform/UBOでよい。

SSBO化は不要。

---

## 5.5 Shader source strategy

今回推奨:

### Stage A

OpenGL:

```text
GLSL
```

Vulkan:

```text
SPIR-V
```

ただしshader interface definitionは共有する。

### Stage B

Vulkan parity完成後、必要ならcanonical shader sourceを統一する。

将来D3D12/Metalを考えるなら候補:

```text
HLSL/Slang
→ SPIR-V
→ DXIL
→ MSL
→ GLSL
```

しかし今回のOpenGL/Vulkan完成をshader-toolchain全面刷新でブロックしない。

---

## 5.6 ShaderLocations

現行OpenGL location ID集合:

```text
ShaderLocations
```

をFrontend契約として残さない。

OpenGL backend内部でlocation cacheとして存在するのは可。

Frontendは:

```text
constant struct
binding slot
semantic binding
```

を使う。

---

## 完了条件

- [x] built-in vertex attribute依存撤去
- [x] OpenGLで描画一致
- [x] backend-neutral constant structures
- [x] Renderer frontendからuniform location concept撤去開始
- [x] future Vulkan shader interface確定

---

## Commit例

```text
Use explicit vertex and shader interfaces for native rendering
```

---

# 14. Phase 6 — OpenGL Resource RHI化

## 目的

Texture/Buffer/Sampler/RenderTargetをRHI resourceへ移す。

---

## 対象

特に:

```text
_screenTexture
_celTexture
_depthTexture
_renderBuffer
_frameBuffer
_celFrameBuffer
_celFrameBufferColor

model textures
mask textures
HUD textures
```

---

## 6.1 TextureHandle

Sceneから:

```text
int textureId
```

を排除する方向へ進める。

---

## 6.2 Texture upload

現行:

```text
CPU palette decode
↓
GL::TexImage2D
```

を:

```text
CPU decoded pixels
↓
GraphicsDevice::CreateTexture
または UploadTexture
```

へする。

CPU decode自体は残してよい。

---

## 6.3 Sampler separation

texture objectへfilter/wrapを暗黙格納する設計から、

```text
Texture
Sampler
```

を分離できるRHIにする。

OpenGL backendでは必要ならtexture parameterとして内部実装してもよい。

---

## 6.4 Render target

共通名:

```text
SceneColor
SceneDepthStencil
CelColor
CelDepth
```

---

## 6.5 Depth format

RHI:

```text
D24S8
D32S8
```

等の意味型。

OpenGL backend mappingを実装。

Vulkan mappingは後Phase。

---

## 完了条件

- [x] Scene resource fieldsがRHI handle化
- [x] OpenGL resource creation backend内
- [x] model texture upload backend内
- [x] FBO構築 backend内
- [x] resize正常
- [x] cel depth attachment切替正常

---

## Commit例

```text
Move OpenGL render resources behind the RHI
```

---

# 15. Phase 7 — Pipeline State RHI化

## 目的

現行OpenGL state machineを、Vulkan/Metal/D3D12対応可能なPipeline Stateへ整理する。

---

## 7.1 現行state分類

調査対象:

```text
DepthFunc
DepthMask
BlendFunc
AlphaFunc
StencilFunc
StencilOp
StencilMask
ColorMask
PolygonOffset
CullFace
PolygonMode
```

---

## 7.2 Fixed pipeline variants

最初は実際に必要なvariantだけ作る。

例:

```text
SceneOpaque
SceneDecal
SceneTranslucentStencil
SceneTranslucentColor
HudModel
CelOutline
FullscreenComposite
Fade
DebugLines
```

---

## 7.3 Alpha Test

OpenGL fixed function:

```text
AlphaFunc Equal 1.0
AlphaFunc Less 1.0
```

をshader logicへ移す。

共通:

```text
AlphaMode::Disabled
AlphaMode::EqualOne
AlphaMode::LessThanOne
```

境界を変更しない。

---

## 7.4 Stencil reference

stencil referenceはdynamic stateとしてRHIに残してよい。

Stencil ops/compareはPipelineDescへ。

---

## 7.5 Color write mask

translucent prepassの:

```text
ColorMask(false,false,false,false)
```

をPipeline stateへ。

---

## 完了条件

- [x] RenderItem描画時にGL state callをFrontendが直接しない
- [x] pipeline variants整理
- [x] alpha behavior一致
- [x] stencil behavior一致
- [x] decals一致
- [x] translucent ordering一致

---

## Commit例

```text
Express native scene state through RHI graphics pipelines
```

---

# 16. Phase 8 — Explicit Render Pass Sequence化

## 目的

巨大な `Scene::OnRenderFrame()` のGPU操作順を、backend-neutralなpass sequenceへ整理する。

Render Graphはまだ作らない。

---

## 8.1 Pass候補

```text
SceneOpaquePass
SceneDecalPass
TranslucentStencilPrePass
DepthRebuildPass
TranslucentResolvePass
PreviewPass
HudModelPass
CelOutlinePass
PostProcessPass
Hud2DPass
FadePass
UiCompositePass
Present
```

---

## 8.2 重要

passクラス乱立が目的ではない。

重要なのは:

```text
attachment
pipeline
resource state
load/store
draw ordering
```

が明確になること。

---

## 8.3 BeginRendering

RHI:

```text
BeginRendering(RenderingInfo)
```

RenderingInfo:

```text
Color attachment
Depth/stencil attachment
LoadOp
StoreOp
Clear value
```

Vulkan Dynamic Renderingへ自然に対応できる形。

---

## 8.4 Depth clear

現行途中の:

```text
depth clear
```

をpass boundaryとして表現。

---

## 8.5 Stencil preservation

stencil load/store semanticsを明示。

OpenGLでは暗黙だった部分をRHI上で明文化する。

---

## 完了条件

- [x] Scene GPU sequenceが明示Pass化
- [x] OpenGL output parity
- [x] pass order documentation更新
- [x] Vulkan Dynamic Renderingへ変換可能

---

## Commit例

```text
Make native scene render passes explicit
```

---

# 17. Phase 9 — OpenGL CommandList完全化

## 目的

`Renderer.cpp` の直接GL呼び出しをOpenGL backendへ押し込む。

---

## 作業

Scene/Renderer frontendから:

```text
GL::Clear
GL::Viewport
GL::BindTexture
GL::UseProgram
GL::Uniform
GL::Enable
GL::Disable
GL::Blend*
GL::Stencil*
GL::Depth*
GL::BindFramebuffer
GL::Draw*
```

等を削る。

代わり:

```text
CommandList
GraphicsPipeline
BindingSet
Buffer
Texture
```

---

## Skiaは例外

このPhaseでは:

```text
NativeRuntime/Skia/SkiaGpu.cpp
```

のOpenGL interopは残してよい。

ただし例外として文書化。

---

## 静的監査

```bash
rg -n "\bGL::" src/MphRead.Native/Renderer.cpp
```

目標:

```text
0
```

または本当に診断だけの明示例外。

---

## 完了条件

- [x] Renderer.cpp direct GL ≈ 0
- [x] Renderer.hpp OpenGL include不要
- [x] Scene raw GL IDsなし
- [x] OpenGL描画完全動作
- [x] Skia GLのみbackend-specific exception

---

## Commit例

```text
Route native scene rendering entirely through OpenGL RHI
```

---

# 18. Phase 10 — FrameContext・GPU Lifetime・Deferred Destruction

## 目的

Vulkan導入前にGPU lifetime contractを確定する。

---

## 10.1 FrameContext

2 frames in flightを初期値とする。

共通:

```text
FrameContext[0]
FrameContext[1]
```

各frame:

```text
command resources
transient upload
binding allocator
retirement/completion value
deferred delete list
```

---

## 10.2 OpenGL implementation

OpenGLにはVulkan同等のframe fenceが必須ではないが、上位契約を合わせる。

必要なら:

```text
GLsync
```

を使用。

少なくともdeferred-destruction API契約を成立させる。

---

## 10.3 Resource destroy

API:

```text
Destroy requested
↓
retire queue
↓
GPU completion
↓
native destroy
```

---

## 10.4 UnloadGl replacement

現行 `UnloadGl()` の責務を:

```text
Scene GPU resource release
```

へ改名・再整理する。

GL固有名をFrontendから減らす。

---

## 完了条件

- [x] Scene unload安全
- [x] match end安全
- [x] preview shared resource破損なし
- [x] repeated load/unload leakなし
- [x] lifetime contract Vulkan対応

---

## Commit例

```text
Add frame retirement and deferred GPU resource destruction
```

---

# 19. Phase 11 — OpenGL RHI完成Gate

## 目的

Vulkan実装前の必須Gate。

ここでOpenGL版を一度完成扱いにする。

---

## 必須静的監査

```bash
rg -n "\bGL::" src/MphRead.Native \
  -g "*.cpp" -g "*.hpp"
```

全結果分類:

```text
A OpenGL RHI backend
B Skia GL interop
C diagnostics
D legacy invalid
```

D = 0。

---

## 必須runtime

```text
Launcher
offline game
online/local game if available
map change
hunter change
HUD
pause
Map Vote
end screen
cel
fog
fullscreen
resize
minimize restore
scene unload/reload
```

---

## 必須parity

Phase 0完了時のGolden Captureと比較。Phase 0では撮影補助を実装したがPNGは未取得だったため、2026-09-30に完了SHA `5d3a0892` とPhase 3の比較を追加する。Phase 3との既存の厳密比較も継続する。

重大差分ゼロ。

---

## Vulkanへ進む条件

以下全てYes:

- [x] immediate modeなし
- [x] display listなし
- [x] Renderer.cpp direct GLなし
- [x] resource RHI化
- [x] pipeline RHI化
- [x] pass明示化
- [x] OpenGL stable
- [x] CI green

2026-09-30完了。最終コード `5e3c3275` はGolden Capture 7/7完全一致（Phase 3、およびPhase 0 bridge経由）、CTest 5/5、GL分類D=0、shellshot 28枚・終了コード0、GPU lifetime全種ゼロ。オンライン描画・マップ変更・Map Vote、cel/fog、最小化復帰を確認。[CI 36666551184](https://github.com/Zection6V/Fruity-Prime/actions/runs/36666551184) は11/11 PASS。被弾は本Phaseの要件ではない。TRANSFER LOCKの黄色い光は移行前にも存在し、ユーザー指定により既存描画を維持。詳細は [検証記録](Fruity-Prime-CPP-OpenGL-RHI-Phase10-11-Gate-2026-09-30.md)。Phase 11までの当初依頼はここまで完了。追加のPhase 26までの依頼により、Phase 12以降を再開する。

一つでもNoならVulkan Phaseへ進まない。

---

# 20. Phase 12 — Vulkan Instance / Device / Debug bring-up

## 目的

まだgame sceneを描画しない。

Vulkan backendの基盤だけ作る。

---

## Vulkan baseline

推奨:

```text
Vulkan 1.3
```

理由:

```text
Dynamic Rendering core
Synchronization2 core
modern explicit rendering path
```

Timeline semaphoreはVulkan 1.2でcore。

互換性を広げる必要がある場合:

```text
Vulkan 1.2
+
VK_KHR_dynamic_rendering
+
VK_KHR_synchronization2
```

も許容できる設計にする。

---

## 12.1 Instance

実装:

```text
VkInstance
required instance extensions
debug utils
validation layer optional
```

---

## 12.2 Validation

Debug:

```text
VK_LAYER_KHRONOS_validation
```

利用可能なら有効。

Releaseで必須にしない。

---

## 12.3 Physical device selection

評価:

```text
graphics support
present support
required Vulkan version
required features
swapchain extension
format support
memory
```

---

## 12.4 Device

初期:

```text
one graphics queue
present queue
```

可能なら同一family。

---

## 12.5 Capabilities

RHI `Capabilities`へ変換。

Vulkan feature structをFrontendへ漏らさない。

---

## 12.6 Debug naming

`VK_EXT_debug_utils` で:

```text
Buffer
Image
Pipeline
Descriptor
CommandBuffer
```

へdebug name。

---

## 完了条件

- [x] Vulkan instance creation
- [x] physical GPU列挙
- [x] device creation
- [x] queue取得
- [x] validation重大エラーなし
- [x] clean shutdown
- [x] OpenGL build/runtime unaffected

2026-09-30完了。コード `5403e1ea` は実GPUおよびLinux/llvmpipeでvalidation有効・正常終了、Golden Capture 7/7完全一致、CTest 5/5、OpenGL shellshot 28枚・終了コード0、[CI 36670960931](https://github.com/Zection6V/Fruity-Prime/actions/runs/36670960931) 11/11 PASS。Vulkanドライバーを利用できない条件でもOpenGL shellshotは成功。詳細は [Phase 12検証記録](Fruity-Prime-CPP-Vulkan-RHI-Phase12-Gate-2026-09-30.md)。

---

## Commit例

```text
Add Vulkan instance and device backend foundation
```

---

# 21. Phase 13 — Vulkan Surface / Swapchain / Present

## 目的

clear colorだけをVulkanでpresentできるところまで進める。

---

## Desktop

GLFW:

```text
GLFW_NO_API
glfwCreateWindowSurface
```

---

## Swapchain

選択:

```text
surface format
present mode
extent
image count
```

---

## VSync

RHI:

```text
VSync on/off
```

Vulkan:

```text
FIFO
MAILBOX
IMMEDIATE
```

へbackend mapping。

---

## Swapchain image ownership

swapchain imageはRHIがdestroyしない。

wrapする。

---

## Resize

処理:

```text
VK_ERROR_OUT_OF_DATE_KHR
VK_SUBOPTIMAL_KHR
framebuffer resize
0x0/minimized
```

---

## Frame sync

初期:

```text
2 frames in flight
imageAvailable binary semaphore
renderFinished binary semaphore
frame fence
```

Timelineを使ってframe retirementを補助してよい。

presentation binary semaphoreは必要に応じて維持。

---

## Clear-only test

```text
Acquire
Transition
BeginRendering
Clear
EndRendering
Transition Present
Submit
Present
```

---

## 完了条件

- [x] Vulkan window表示
- [x] clear color present
- [x] resize
- [x] fullscreen
- [x] minimize/restore
- [x] validation clean
- [x] shutdown clean

## 検証結果 (2026-09-30)

実装SHA `8c6f2d2a044544c5975515fa838730271a02f003`。Windows RTX 5070 Ti / Vulkan 1.4 の `-vulkanpresentcheck` でclear present、windowed resize、fullscreen往復、minimize/restore、FIFO/Mailbox切替、終了を通し、validation有効・errors 0。`-vulkancheck` のfoundation PASSと既存OpenGLの `-thumbnailwindowcheck` もPASS。CTestは5/5 PASS。

同一SHAの [CI run 36684051768](https://github.com/Zection6V/Fruity-Prime/actions/runs/36684051768) は11/11 jobs PASS。Windows/MSVC、Linux/GCC、macOS/Clang、Android NDK arm64/x86_64とAPKを含み、LinuxではVulkan foundationおよびpresentation runtime gateが成功。実装と検証範囲は [Phase 13 gate記録](Fruity-Prime-CPP-Vulkan-RHI-Phase13-Gate-2026-09-30.md) を参照。

この段階のruntime gateはswapchain上のclear-only描画であり、ゲームrendererのVulkan移植・画像parityは後続フェーズの対象。ローカル検証機では `VK_EXT_swapchain_maintenance1` が有効でpresent fenceによる終了を検証した。未対応機器の終了fallbackは別条件でのruntime検証をしていない。

---

## Commit例

```text
Add Vulkan swapchain and frame presentation
```

---

# 22. Phase 14 — Vulkan Memory / Buffer / Texture / Upload

## 目的

RHI resourceをVulkan objectへ実装する。

---

## 14.1 Memory allocator

選択肢:

```text
VMA
または
project-owned allocator
```

VMA採用は推奨可能。

ただしRHI public APIへVMA型を漏らさない。

---

## 14.2 Buffer

対応:

```text
Vertex
Index
Constant
TransferSrc
TransferDst
Readback
```

---

## 14.3 Texture

対応:

```text
sampled
color attachment
depth/stencil
transfer
```

---

## 14.4 ImageView

textureとviewを分離。

---

## 14.5 Staging

upload:

```text
CPU
↓ staging buffer
↓ vkCmdCopyBuffer / vkCmdCopyBufferToImage
↓ transition
↓ GPU resource
```

---

## 14.6 ResourceState mapping

集中管理helperを作る。

禁止:

各call siteで独自に:

```text
oldLayout
newLayout
srcAccess
dstAccess
```

を手書き乱立。

---

## 14.7 Synchronization2

`vkCmdPipelineBarrier2` 系を基本とする。

transitionはactive rendering scope外で行う。

---

## 完了条件

- [x] Buffer RHI実装
- [x] Texture RHI実装
- [x] staging upload
- [x] readback buffer
- [x] resource transition helper
- [x] validation clean
- [x] leakなし

完了SHA: `db2ed0f4d353b88b2cee196232ac9b04bbf9f069`。
Windows/MSVC・Linux/GCC・macOS/ClangとAndroid 4/4のCI成功、
Windows NVIDIAとLinux llvmpipeのresource診断でvalidation error 0・live 0。
詳細: [Phase 14 gate](Fruity-Prime-CPP-Vulkan-RHI-Phase14-Gate-2026-09-30.md)。
次はPhase 15。

---

## Commit例

```text
Implement Vulkan RHI buffers textures and uploads
```

---

# 23. Phase 15 — Vulkan Descriptor / Binding model

## 目的

共通BindingLayout/BindingSetをVulkan descriptorへ変換する。

---

## 15.1 Layout

例:

```text
Frame set
Material set
Draw set
```

---

## 15.2 Descriptor pool

最初は:

```text
per-frame descriptor pool
```

が安全。

Frame fence完了後reset。

---

## 15.3 更新

GPU使用中descriptorを更新しない。

frame safe pointで更新。

---

## 15.4 Texture/Sampler

combined image samplerまたは分離descriptorを採用。

RHI semanticsを優先。

---

## 15.5 Constant buffer alignment

`minUniformBufferOffsetAlignment` を考慮。

---

## 完了条件

- [x] BindingLayout Vulkan mapping
- [x] BindingSet Vulkan mapping
- [x] per-frame safe allocator
- [x] descriptor lifetime errorなし
- [x] frontend Vulkan descriptor awarenessなし

---

## 検証記録

実装 `7f9e42493d8e3d749acf4896387c16e9e75ccdf4`。
Desktop CI 36727161309 は3/3、Android CI 36727166007 は4/4 PASS。
ローカルCTest 5/5、GPU bind送信・pool拡張・frame再利用を含む
resource checkはvalidation=1、errors=0、live=0。
詳細: [Phase 15 gate](Fruity-Prime-CPP-Vulkan-RHI-Phase15-Gate-2026-09-30.md)。
次はPhase 16。

## Commit例

```text
Implement Vulkan RHI resource bindings
```

---

# 24. Phase 16 — Vulkan Shader / Pipeline

## 目的

OpenGLで固定したshader semanticsをVulkanへ実装する。

---

## 16.1 SPIR-V build

CMakeにshader compile stepを追加。

候補:

```text
glslc
glslangValidator
DXC -spirv
Slang
```

一つを明示的に選ぶ。

CIも同じtoolchainを使用。

---

## 16.2 Reflection

初期実装ではreflection必須ではない。

binding contractをC++/shader shared constantsで固定してもよい。

将来reflection導入可能。

---

## 16.3 Pipeline

RHI `GraphicsPipelineDesc` から:

```text
VkPipelineLayout
VkPipeline
```

を生成。

Dynamic Rendering:

```text
VkPipelineRenderingCreateInfo
```

利用。

---

## 16.4 Cache

`GraphicsPipelineDesc` hashでcache。

drawごとのpipeline作成禁止。

---

## 16.5 Shader parity

必須:

```text
lighting
fog
texgen
matrix stack
palette override
flat color
alpha test
material alpha
material mode
cel
shift
whiteout
fade
```

---

## 完了条件

- [x] SPIR-V reproducible build
- [x] pipeline creation
- [x] descriptor/pipeline layout一致
- [x] validation clean
- [x] shader semantics documented

---

## 検証記録

2026-10-01完了。実装 `cebd532c`。ユーザー指定によりWindowsはローカルMinGW Releaseビルド成功を完了条件とした。CTest 5/5、実GPUのpipeline/module/binding/resource検証はvalidation=1、errors=0、live=0。Linux・macOS CI成功、Android 4/4 PASS。詳細は[Phase 16 gate](Fruity-Prime-CPP-Vulkan-RHI-Phase16-Gate-2026-10-01.md)。Phase 16完了後に停止する指示に従い、Phase 17は未着手。

## Commit例

```text
Add Vulkan shaders and graphics pipeline implementation
```

---

# 25. Phase 17 — Vulkan Main Scene描画

## 目的

UIなしでgame sceneをVulkan描画する。

---

## 実装順

### 17.1 Static mesh

まず:

```text
room
hunter/model
```

---

### 17.2 Textures

model textures。

---

### 17.3 Lighting

OpenGL parity。

---

### 17.4 Opaque

まずopaqueだけ。

---

### 17.5 Decal

polygon offset相当。

---

### 17.6 Translucent + Stencil

現行アルゴリズムをそのまま移植。

簡略化禁止。

---

### 17.7 Dynamic geometry

```text
particles
trails
debug geometry
```

---

## 完了条件

- [x] room表示
- [x] model表示
- [x] textures正常
- [x] no flipped UV
- [x] correct winding
- [x] depth正常
- [x] decals正常
- [x] translucent正常
- [x] stencil正常
- [x] particles/trails正常

2026-10-01完了。実装 `feb0a40b` / `bc6468cc` / `74a4aaf4`。`-rhi vulkan` でGolden 7/7 captured・全gate verified、scene部はOpenGLとpixel一致（HUD文字quadの半画素境界fill-rule差303px のみ、Phase 18へ持越し）。実マップ3種renderprobe PASS、validation errors 0、`-gpulifetime -rhi vulkan` 4/4 解放後ゼロ。OpenGLはGolden 7/7維持。詳細は[Phase 17 gate](Fruity-Prime-CPP-Vulkan-RHI-Phase17-Gate-2026-10-01.md)。

---

## Commit分割推奨

1 commitに全部入れず:

```text
Render opaque scene through Vulkan
Add Vulkan decals and translucent passes
Add Vulkan dynamic scene geometry
```

---

# 26. Phase 18 — Vulkan Cel / RTT / Post / HUD

## 目的

OpenGL固有だった後段処理をVulkan化する。

---

## 18.1 SceneColor

offscreen color attachment。

---

## 18.2 Depth

cel outline用sampleable depth。

---

## 18.3 Cel outline

transition:

```text
DepthWrite
↓
DepthRead / ShaderResource
```

---

## 18.4 RTT composite

fullscreen triangle/quad。

---

## 18.5 Shift / whiteout

現行table/factor semanticsを維持。

---

## 18.6 HUD

```text
HUD models
HUD layers
HUD objects
mask texture
```

---

## 18.7 Fade

fullscreen pass。

---

## 完了条件

- [x] cel shading
- [x] outline
- [x] HUD
- [x] mask
- [x] whiteout
- [x] disruption
- [x] fade
- [x] scoreboard
- [x] pause game background
- [x] OpenGL comparison pass

2026-10-01完了。Phase 17のdeferred pipeline / 自動layout遷移 / depth-only sampled viewで後段処理も通り、追加コード不要。cel probe（決定的なspawn 0–2で差は最大1階調）、Golden fade/hud/whiteout-disruption、実試合 `-hudshots` 20枚（HUD・mask・scoreboard）で確認。残差は2D quadの画素/texel中心tieに当たる1px縁のみで、座標規約の反転・quad分割変更でも不変＝実装依存のtie解決としてPhase 24の許容差に定義。pause背景のscene側は完成、Launcher合成はPhase 19。詳細は[Phase 18 gate](Fruity-Prime-CPP-Vulkan-RHI-Phase18-Gate-2026-10-01.md)。

---

# 27. Phase 19 — Skia Vulkan GPU Integration

## 目的

VulkanモードでもLauncher / Pause / Map Vote / End ScreenをGPU描画する。

hidden OpenGL contextは禁止。

CPU full-frame fallback禁止。

---

## 現行

```text
Skia Ganesh GL
```

今回:

```text
OpenGL backend
    → Skia Ganesh GL

Vulkan backend
    → Skia Ganesh Vulkan
```

---

## 19.1 Skia build

現在のCIは概ね:

```text
skia[gl,freetype]
```

前提。

Vulkan backendを明示的に有効にする必要がある。

Skia Vulkan capabilityをconfigure時に検出する。

「headerがあるだけ」で成功扱いしない。

---

## 19.2 Shared device

Skia自身に別VkDeviceを作らせない。

原則:

```text
RHI Vulkan VkInstance
RHI Vulkan VkPhysicalDevice
RHI Vulkan VkDevice
RHI Vulkan graphics queue
```

をnarrow interop adapter経由でSkiaへ渡す。

---

## 19.3 Interop boundary

例:

```text
NativeRuntime/Skia/VulkanInterop.*
```

のみがraw Vk handleを見てよい。

Common Skia canvas APIへVk型を漏らさない。

---

## 19.4 UI target

推奨:

```text
RHI-owned UiColor Vulkan image
↓
Skia wraps image
↓
UI draw
↓
Skia flush/submit
↓
RHI waits/orders correctly
↓
UiColor transition ShaderResource
↓
UiComposite pass
```

---

## 19.5 Synchronization

最重要。

Skia公式仕様上、client-owned VkImageをSkiaへ渡す場合、client側が必要な同期/barrierを担当する。

必要な設計:

```text
RHI state before Skia
↓
Skia expected layout
↓
Skia draw/submit
↓
Skia final image layout取得
↓
RHI state trackerへ反映
↓
RHI composite
```

---

## 19.6 Queue ordering

初期は同一graphics queueを優先。

複数queue最適化は禁止。

---

## 19.7 Frame order

維持:

```text
Scene
HUD/Post
Shell TickUi
Ui image
UiOverlay / LauncherHunter
Present
```

UI offscreen生成のsubmission時刻は内部的に変わっても、visual semanticsを変えない。

---

## 19.8 Launcher-only path

Sceneがない場合も:

```text
UI
↓
Vulkan present
```

が成立すること。

---

## 完了条件

- [x] Launcher Vulkan
- [x] Settings Vulkan
- [x] Pause Vulkan
- [x] Map Vote Vulkan
- [x] End Screen Vulkan
- [x] hidden GL contextなし
- [x] CPU full-frame fallbackなし
- [x] Skia/Vulkan sync validation clean
- [x] texture corruptionなし

2026-10-01完了。`f3650254` / `cbdde122` / `19bcb655`。`-rhi vulkan` でゲームウィンドウはNoApi＋Vulkan present、Skia GaneshはRHIのVkDevice/queueをVulkanInteropから共有（VMAアロケータ供給）、UI targetはRHI texture（COLOR_ATTACHMENTで受け渡し）。MSVC＋vcpkg skia[vulkan]で `-shellshot` 28枚（Launcher/Settings/Pause/Map Vote/End Screen含む）exit 0、同期検証込みvalidation 0。詳細は[Phase 19 gate](Fruity-Prime-CPP-Vulkan-RHI-Phase19-Gate-2026-10-01.md)。

---

## Commit例

```text
Render native Skia UI through the Vulkan backend
```

---

# 28. Phase 20 — Vulkan Readback / Screenshot / Recording

## 目的

`GL::ReadPixels`依存をbackend-neutral化する。

---

## 対象

```text
ReadWindowBuffer
ReadSceneTarget
screenshot
recording
thumbnail/capture
cel calibration where applicable
```

---

## Vulkan path

```text
Image
↓ transition TransferSrc
↓ CopyImageToBuffer
↓ fence/completion
↓ mapped readback
↓ CPU image
```

---

## 注意

通常frameを毎回GPU idleしない。

必要時だけreadback。

---

## Row pitch

Vulkan buffer layoutのrow alignmentを正しく処理。

---

## Orientation

OpenGL/Vulkanでvertical orientation差を統一。

Frontendには同じtop/bottom conventionを返す。

---

## 完了条件

- [x] screenshot一致
- [x] recording一致
- [x] scene target capture一致
- [x] unnecessary WaitIdleなし
- [x] no leak

2026-10-01完了。`09d67cc1` / `19bcb655` / `715f5131`。全captureは `CommandList::ReadColor` 経由（window readbackもRHI化）。RHI screenshot/record exportは両backendでPASSかつ同一バイト、定常フレームのhost stallは0（以前は35/frame、uploadをstream内記録に変更）、gpulifetime解放後ゼロ。詳細は[Phase 20 gate](Fruity-Prime-CPP-Vulkan-RHI-Phase20-Gate-2026-10-01.md)。

---

# 29. Phase 21 — Backend Selection

## 目的

ユーザーがOpenGL/Vulkanを選択できるようにする。

---

## 選択値

```text
OpenGL
Vulkan
Auto
```

---

## 明示選択

Vulkan指定:

```text
Vulkan init fail
↓
error
```

silent OpenGL fallback禁止。

---

## Auto

初期推奨:

Desktop Windows/Linux:

```text
Vulkan requirements satisfied
    → Vulkan
otherwise
    → OpenGL
```

ただし既存ユーザー体験を優先して最初はOpenGL defaultでもよい。

Rollout policyは別。

---

## Logging

起動時:

```text
Requested backend
Selected backend
GPU
API version
driver
swapchain format
depth format
frames in flight
validation status
```

---

## Runtime hot switching

今回の必須条件にはしない。

まずstartup selectionを完成させる。

ただしRHI ownershipは将来hot switchingできる構造にする。

---

## 完了条件

- [x] explicit OpenGL
- [x] explicit Vulkan
- [x] Auto
- [x] error visible
- [x] no silent fallback

2026-10-01完了。`-rhi opengl|vulkan|auto` と `launcher.txt` の `renderer=`（Settings→Game→Renderer (next start)）。明示Vulkanが不可なら `SceneBackendUnavailable` を理由付きのネイティブエラーダイアログで表示しフォールバックなし、Autoは判定理由をログ。起動時にrequested/selected/GPU/API/driver/swapchain/depth/frames in flight/validationを1行記録。詳細は[Phase 21 gate](Fruity-Prime-CPP-Vulkan-RHI-Phase21-Gate-2026-10-01.md)。

---

# 30. Phase 22 — Android Vulkan Platform Integration

## 目的

既存Android GLESを維持したまま、共通Vulkan backendをAndroidへ接続する。

---

## 重要

Vulkan実装をAndroid用に複製しない。

共有:

```text
VkDevice implementation
VkResources
VkPipeline
VkBindings
VkSynchronization
```

Android専用:

```text
surface
window lifecycle
app lifecycle
```

---

## 22.1 ANativeWindow

platform adapterで保持。

---

## 22.2 Surface

```text
VkAndroidSurfaceCreateInfoKHR
vkCreateAndroidSurfaceKHR
```

---

## 22.3 Lifecycle

独立状態:

```text
App alive
Device alive
Surface alive
Swapchain alive
Game state alive
```

---

## 22.4 Surface loss

surface破棄時:

```text
swapchain-dependent objects release
surface release
```

Device/game stateを不用意に破棄しない。

---

## 22.5 Resume

surface再生成後:

```text
surface
swapchain
backbuffer-dependent resources
```

再構築。

---

## 22.6 Orientation

```text
surface pixel extent
UI coordinates
touch mapping
projection
```

を一致させる。

---

## 22.7 GLES coexistence

Android:

```text
OpenGLES
Vulkan
Auto
```

明示Vulkan失敗でsilent GLES fallback禁止。

---

## 22.8 Android Skia/UI

Vulkanモードでhidden GLES contextを作らない。

GPU Vulkan pathを使う。

---

## 完了条件

- [ ] Android Vulkan scene
- [ ] Android Vulkan HUD/UI
- [ ] pause/resume
- [ ] surface destroy/recreate
- [ ] orientation
- [ ] touch mapping
- [ ] Vulkan validation clean
- [ ] GLES build/run維持

2026-10-01: **コード実装・両ABIビルド完了、実機検証は未了**（この環境にAndroid端末/エミュレータが無く、adbがローカルサーバへ接続できない）。共通Vulkan backendをlibvulkan.so/ANativeWindow surfaceで接続、device生存のままsurface/swapchainのみ再生成、明示Vulkan失敗はエラー通知（GLESへ黙って戻らない）。端末での確認手順は[Phase 22 gate](Fruity-Prime-CPP-Vulkan-RHI-Phase22-Gate-2026-10-01.md)。

---

# 31. Phase 23 — CI分割

## 目的

OpenGL/Vulkan双方を継続的にbuild gateへする。

---

## Windows

最低限:

```text
OpenGL build
Vulkan build
```

可能なら同一binaryに両backendを含める。

それでもcompile definitions/optionsの検証を分ける。

---

## Linux

```text
OpenGL
Vulkan
```

必要package:

```text
Vulkan headers
loader
shader compiler
validation/dev package as appropriate
Skia Vulkan
```

---

## macOS

今回:

```text
OpenGL build維持
```

Vulkan via MoltenVKは任意。

将来Metal Phaseでnative backend追加。

macOS Vulkanを今回の必須Gateにしなくてもよい。

---

## Android

```text
GLES existing build
Vulkan build
```

ABIs:

repository contractに従う。

---

## Shader build

SPIR-V compilationもCIで実行。

precompiled stale shaderをcommitしてcompile stepを避けない。

---

## 完了条件

- [x] Windows CI green
- [x] Linux CI green
- [x] macOS OpenGL CI green
- [x] Android GLES CI green
- [x] Android Vulkan CI green
- [x] shader build reproducible

2026-10-01完了。build_cpp run 36802488060 全job green。[Phase 23 gate](Fruity-Prime-CPP-Vulkan-RHI-Phase23-Gate-2026-10-01.md)。

---

# 32. Phase 24 — Cross-backend Golden Parity

## 目的

「Vulkanで動く」ではなく「OpenGLと同じ結果」を確認する。

---

## 比較項目

### Geometry

- winding
- quad split
- strips
- matrix index
- normals

### Texture

- UV
- filter
- wrap
- palette
- alpha

### Depth

- near/far
- depth compare
- depth write

### Stencil

- polygon ID
- translucent ordering

### Blending

- decals
- translucent
- HUD

### Shader

- lighting
- fog
- texgen
- cel
- fade
- whiteout

### Coordinate systems

- framebuffer Y orientation
- clip-space depth
- front-face winding
- texture origin

---

## Vulkan/OpenGL座標差

特に確認:

```text
clip space
Y inversion
depth range
viewport convention
framebuffer origin
```

場当たり的に各shaderへflipを追加しない。

共通coordinate conventionを1箇所で定義する。

---

## 許容差

pixel-perfectが不可能なdriver差はtoleranceを定義。

ただし以下は不許可:

```text
missing pixels
wrong textures
wrong UV
different geometry
broken stencil
broken alpha
UI offset
black frame
flipped output
```

---

## 完了条件

- [x] 全golden testで重大差分なし。

2026-10-01完了。Golden 7/7、renderprobe 4マップ76枚、実試合hudshots 20/20がクロスバックエンド検証を通過。許容差は`tools/validate-cross-backend-parity.py`に一元定義（隣接画素一致のnearest sampling tieのみ別枠、shift/flip/黒画面は不合格）。[Phase 24 gate](Fruity-Prime-CPP-Vulkan-RHI-Phase24-Gate-2026-10-01.md)。

---

# 33. Phase 25 — Stability / Lifetime Stress

## 目的

過去のPC freezeやresource lifetime問題を再発させない。

---

## Stress

最低限:

```text
match start/end repeat
map change repeat
launcher/game repeat
resize repeat
fullscreen repeat
minimize/restore repeat
Map Vote repeat
backend startup alternate
Android pause/resume repeat
Android surface recreate repeat
```

---

## Monitor

```text
CPU memory
GPU memory
handle count
buffer count
texture count
pipeline count
descriptor allocation
command pools
fences
semaphores
```

---

## Vulkan validation

ゼロにする:

```text
use-after-free
destroy-in-use
layout mismatch
invalid descriptor
bad access masks
bad stage masks
command buffer misuse
swapchain lifetime error
```

---

## 完了条件

- [x] 長時間増加傾向なし。（デスクトップ。Android pause/resume・surface再生成の反復は実機待ち）

2026-10-01完了。ストレスで1ルームロード毎に約25MBのCPUリーク（両バックエンド、map rotationも同経路）を発見し修正：player/beam/effectのshared_ptr循環とcollision候補プールの増殖。`-gpulifetime`がcycle毎のprivate memoryを出し、1MB/cycle以上で失敗する。3マップ×40cycle×両backend、validation 0。[Phase 25 gate](Fruity-Prime-CPP-Vulkan-RHI-Phase25-Gate-2026-10-01.md)。

---

# 34. Phase 26 — OpenGL/Vulkan Architecture Freeze

## 目的

次のMetal/D3D12追加前にRHI contractを安定化する。

---

## 最終監査

### Common RHI

禁止type:

```bash
rg -n "Vk[A-Z]|vk[A-Z]|GLuint|OpenGL::GL|ID3D12|MTL[A-Z]" \
  src/MphRead.Native/NativeRuntime/Rhi \
  -g "*.hpp" -g "*.cpp"
```

backend directory以外でraw API typeがないこと。

---

### Renderer frontend

```bash
rg -n "\bGL::|Vk[A-Z]|vk[A-Z]" \
  src/MphRead.Native/Renderer.cpp \
  src/MphRead.Native/Renderer.hpp
```

目標:

```text
0
```

---

### Backend isolation

OpenGL native calls:

```text
Rhi/OpenGL
Skia/OpenGL interop
```

以外にない。

Vulkan native calls:

```text
Rhi/Vulkan
Skia/Vulkan interop
platform surface adapter
```

以外にない。

---

## Metal追加シミュレーションレビュー

コードを書かずにレビューする。

質問:

```text
MetalDeviceを追加するときGraphicsDevice interface変更が必要か？
MetalSwapchainを追加するときRenderer.cpp変更が必要か？
MetalPipelineを追加するときScene変更が必要か？
MetalTextureを追加するときMaterial変更が必要か？
```

理想:

```text
No
```

必要ならRHI contractの問題として修正。

---

## D3D12追加シミュレーションレビュー

同じく:

```text
D3D12Device
D3D12CommandList
D3D12Pipeline
D3D12Bindings
D3D12Swapchain
```

を追加するだけでFrontendが成立するか確認。

---

## Phase 26 結果

2026-10-01完了。分離監査を`tools/check-rhi-isolation.py`としてCI化（違反0）。共通RHIに`Rhi::WindowUi`を追加し、Mods側のVulkan名指しを除去。Metal/D3D12レビューではfrontend変更不要。[Phase 26 gate](Fruity-Prime-CPP-Vulkan-RHI-Phase26-Gate-2026-10-01.md)。

---

# 35. Phase間の依存関係

```text
Phase 0 Baseline
   ↓
Phase 1 RHI Core
   ↓
Phase 2 Window/Presentation
   ↓
Phase 3 Geometry Decoder
   ↓
Phase 4 OpenGL VBO/IBO
   ↓
Phase 5 Shader Interface
   ↓
Phase 6 Resource RHI
   ↓
Phase 7 Pipeline State
   ↓
Phase 8 Render Pass Sequence
   ↓
Phase 9 OpenGL CommandList
   ↓
Phase 10 Frame Lifetime
   ↓
Phase 11 OpenGL Gate
   ↓
Phase 12 Vulkan Device
   ↓
Phase 13 Vulkan Swapchain
   ↓
Phase 14 Vulkan Resources
   ↓
Phase 15 Vulkan Bindings
   ↓
Phase 16 Vulkan Shader/Pipeline
   ↓
Phase 17 Vulkan Scene
   ↓
Phase 18 Vulkan Post/HUD
   ↓
Phase 19 Skia Vulkan
   ↓
Phase 20 Readback
   ↓
Phase 21 Backend Selection
   ↓
Phase 22 Android Vulkan
   ↓
Phase 23 CI
   ↓
Phase 24 Golden Parity
   ↓
Phase 25 Stress
   ↓
Phase 26 Architecture Freeze
```

---

# 36. Worker AIにPhaseを渡すときのテンプレート

毎回以下の形式で依頼する。

```text
Repository:
https://github.com/Zection6V/Fruity-Prime

Branch:
develop3_rendering

Task:
OpenGL/Vulkan RHI移行計画の Phase N を完遂してください。

Plan:
docs/app_design/Fruity-Prime-CPP-OpenGL-Vulkan-RHI-Phase-Plan-2026-09-28.md

Requirements:
- Phase Nの範囲だけ実装
- develop3_renderingの最新HEADから開始
- 他作業者のcommitを保持
- C#版は変更禁止
- force-push禁止
- dummy/stubで完了扱い禁止
- build成功だけで完了扱い禁止
- PhaseのDefinition of Doneを全項目確認
- 未実施runtime testは未実施と明記
- 最後にcommit SHA / files / build / tests / CIを報告
```

---

# 37. Phase完了報告フォーマット

Worker AIは最後に必ず以下を返す。

```markdown
## Phase N Result

### Status
PASS / PARTIAL / BLOCKED

### Base
- branch:
- base SHA:

### Result
- final SHA:
- commit:

### Changed files
- ...

### Implementation
- ...

### Static audit
- command:
- result:

### Build
- Windows:
- Linux:
- macOS:
- Android:

### Runtime
- tested:
- not tested:

### CI
- workflow:
- run ID:
- exact SHA:
- result:

### Remaining issues
- ...

### Phase N+1 readiness
READY / NOT READY
```

---

# 38. Phase失敗時のルール

Phaseが完成しなかった場合:

```text
PARTIAL
```

として止める。

禁止:

```text
未完成のまま次Phaseへ進む
```

特に以下が残ったら次へ進まない。

```text
render regression
texture corruption
validation error
lifetime crash
CI failure
known Vulkan misuse
```

---

# 39. OpenGLからVulkanへ移す際の危険箇所リスト

## Immediate mode state inheritance

現行OpenGLは:

```text
Color
Normal
TexCoord
```

が次vertexまで持続する。

decoderで再現する。

---

## Quad semantics

GL_QUADSを適当にtriangle化しない。

---

## Matrix index

現在texcoord Zへ隠している意味を落とさない。

---

## Alpha test

fixed functionを削除しただけにしない。

shader discardへ移す。

---

## Stencil

translucent renderingの中心。

「見た目大体同じ」に簡略化禁止。

---

## Depth clear timing

frame途中のdepth clearを忘れない。

---

## Color mask

translucent stencil prepassで重要。

---

## Polygon offset

decal z-fighting対策。

---

## Texture origin

OpenGL/Vulkan差で上下反転しやすい。

---

## Clip depth

OpenGLとVulkanのNDC差を共通projection policyで吸収する。

---

## Skia state

OpenGLではglobal state restore問題。

Vulkanではimage layout/synchronization問題へ変わる。

---

## UI texture ownership

Skiaが作る/使うimageとRHIがcompositeするimageのownershipを曖昧にしない。

---

# 40. Performance方針

最初のVulkan版でOpenGLより速いことは完成条件ではない。

ただし明らかなanti-patternは禁止。

禁止:

```text
vkDeviceWaitIdle every frame
vkQueueWaitIdle every frame
pipeline create every draw
descriptor pool create every draw
buffer create every draw
image create every frame unnecessarily
map/unmap tiny buffer for every vertex
one submission per draw
```

初期推奨:

```text
1 graphics submission per main frame
必要最小限のSkia submission
2 frames in flight
per-frame transient upload
pipeline cache
descriptor pool per frame
```

---

# 41. Vulkan API方針

今回のVulkan実装ではmodern pathを優先。

推奨:

```text
Dynamic Rendering
Synchronization2
Timeline Semaphore where useful
Debug Utils
Pipeline cache
```

Vulkan 1.3ではDynamic RenderingとSynchronization2がcore。

Timeline Semaphoreは1.2でcore。

ただしAPI機能をRHI frontendへ漏らさない。

---

# 42. Skia Vulkan方針

SkiaはOpenGL/Vulkan両GPU backendを持つ。

重要:

```text
SkiaはVulkan deviceを自動作成しない
```

client側がdevice/queueを提供する構成を使う。

RHI Vulkan deviceとSkia Vulkan contextは同一deviceを共有する。

Skiaが使用するRHI-owned imageのlayout/synchronizationはclient側責任として扱う。

この境界は必ず専用interop layerに閉じ込める。

---

# 43. Build system最終イメージ

CMake概念:

```cmake
option(FRUITY_RENDERER_OPENGL "Build OpenGL backend" ON)
option(FRUITY_RENDERER_VULKAN "Build Vulkan backend" ON)
```

Targets概念:

```text
fruity_rhi
fruity_rhi_opengl
fruity_rhi_vulkan
fruity_mphread_native
```

Dependencies:

```text
fruity_rhi
    no OpenGL
    no Vulkan

fruity_rhi_opengl
    OpenGL
    GLFW

fruity_rhi_vulkan
    Vulkan
    GLFW desktop
    Vulkan Android loader on Android
```

Skia backend dependenciesも選択backendごとに明示。

---

# 44. Definition of Done — 今回全体

## Architecture

- [ ] Renderer frontend API-neutral
- [ ] RHI OpenGL-shapedではない
- [ ] OpenGL backend独立
- [ ] Vulkan backend独立
- [ ] Future Metal/D3D12追加でFrontend変更不要
- [ ] raw handle leakなし

## OpenGL

- [ ] immediate mode撤去
- [ ] display list撤去
- [ ] VBO/IBO
- [ ] explicit shader input
- [ ] RHI resources
- [ ] RHI pipeline
- [ ] RHI command list
- [ ] visual parity

## Vulkan Desktop

- [ ] instance/device
- [ ] swapchain
- [ ] buffer/texture
- [ ] descriptor/bindings
- [ ] pipeline
- [ ] scene
- [ ] stencil/translucency
- [ ] cel
- [ ] HUD
- [ ] Skia UI
- [ ] readback
- [ ] resize/fullscreen
- [ ] validation clean

## Android Vulkan

- [ ] shared Vulkan backend
- [ ] Android surface adapter
- [ ] scene
- [ ] HUD/UI
- [ ] lifecycle
- [ ] orientation
- [ ] GLES維持

## Stability

- [ ] resource leakなし
- [ ] repeated scene load安全
- [ ] match end安全
- [ ] Map Vote安全
- [ ] long-run freeze regressionなし

## CI

- [ ] Windows
- [ ] Linux
- [ ] macOS OpenGL
- [ ] Android GLES
- [ ] Android Vulkan

---

# 45. 今回の最重要原則

ワーカーAIは常に以下を判断基準にする。

```text
「この変更はVulkanを追加するためのOpenGL特例か？」
```

Yesなら再設計を検討する。

正しい構造:

```text
Game
↓
Renderer Frontend
↓
RHI semantics
↓
backend implementation
```

誤った構造:

```text
Game
↓
OpenGL behavior
↓
if Vulkan
↓
OpenGL emulation
```

---

# 46. 最終的な完成イメージ

今回:

```text
                         Fruity-Prime C++

Game / Scene / HUD / Launcher
              │
              ▼
        Renderer Frontend
              │
              ▼
        Explicit Passes
              │
              ▼
             RHI
       ┌──────┴──────┐
       ▼             ▼
    OpenGL         Vulkan
       │             │
       └──────┬──────┘
              ▼
             GPU
```

将来:

```text
             RHI
   ┌─────────┼─────────┬─────────┐
   ▼         ▼         ▼         ▼
OpenGL    Vulkan     Metal      D3D12
```

今回の実装が正しければ、

```text
Metal追加
D3D12追加
```

はRendererの再設計ではなく、**backend追加作業**になる。

これを今回のアーキテクチャ成功条件とする。

---

# 47. 参考仕様・公式資料

Vulkan Documentation Project:

```text
https://docs.vulkan.org/
```

Vulkan Dynamic Rendering:

```text
https://docs.vulkan.org/refpages/latest/refpages/source/VK_KHR_dynamic_rendering.html
```

Vulkan Synchronization2:

```text
https://docs.vulkan.org/guide/latest/extensions/VK_KHR_synchronization2.html
```

Vulkan Synchronization:

```text
https://docs.vulkan.org/guide/latest/synchronization.html
```

Vulkan core revisions:

```text
https://docs.vulkan.org/spec/latest/appendices/versions.html
```

Skia Vulkan backend:

```text
https://skia.org/docs/user/special/vulkan/
```

Skia GPU canvas/context guidance:

```text
https://skia.org/docs/user/api/skcanvas_creation/
```

---

# 48. この計画と既存マルチバックエンド指示書の関係

既存:

```text
docs/app_design/Fruity-Prime-CPP-MultiBackend-RHI-Work-Instructions-2026-09-28.md
```

は最終的なmulti-backend思想を示す。

この文書はそのうち:

```text
OpenGL
Vulkan
Android Vulkan
```

を先行して完成させるための詳細なexecution planである。

Metal/D3D12を今回実装しないことは、multi-backend設計思想を放棄する意味ではない。

むしろ、

```text
OpenGL + Vulkan
```

の2つでRHI抽象が本当に成立することを先に証明してから、

```text
Metal
D3D12
```

を追加する。

この順序を厳守する。

# 49. 進捗管理 (2026-09-29)

この節をRHI移行の実作業ログとして更新する。本文中の完了条件は項目ごとに、最新の該当SHAで実証できたものだけをチェックし、この節に根拠となるSHA・検証結果を記録する。フェーズ全体は全条件が満たされるまで完了扱いにしない。

## 現在の作業位置

- **Phase 17完了 (2026-10-01)。** Vulkanでmain sceneを描画（`-rhi vulkan`）。Golden 7/7がOpenGLとscene部pixel一致、validation 0、GPU lifetime 4/4。[Phase 17 gate](Fruity-Prime-CPP-Vulkan-RHI-Phase17-Gate-2026-10-01.md)。**Phase 18完了** ([gate](Fruity-Prime-CPP-Vulkan-RHI-Phase18-Gate-2026-10-01.md))。**Phase 19完了**（Skia Ganesh Vulkan、[gate](Fruity-Prime-CPP-Vulkan-RHI-Phase19-Gate-2026-10-01.md)）。**Phase 20完了**（readback/定常stall 0、[gate](Fruity-Prime-CPP-Vulkan-RHI-Phase20-Gate-2026-10-01.md)）。**Phase 21完了**（[gate](Fruity-Prime-CPP-Vulkan-RHI-Phase21-Gate-2026-10-01.md)）。**Phase 22はコード完了・実機検証待ち**（[gate](Fruity-Prime-CPP-Vulkan-RHI-Phase22-Gate-2026-10-01.md)）。**Phase 24完了**（[gate](Fruity-Prime-CPP-Vulkan-RHI-Phase24-Gate-2026-10-01.md)）。**Phase 25完了**（[gate](Fruity-Prime-CPP-Vulkan-RHI-Phase25-Gate-2026-10-01.md)）。**Phase 23完了**（[gate](Fruity-Prime-CPP-Vulkan-RHI-Phase23-Gate-2026-10-01.md)）。**Phase 26完了**（[gate](Fruity-Prime-CPP-Vulkan-RHI-Phase26-Gate-2026-10-01.md)）。Phase 17–26完遂（Phase 22の実機検証のみ端末待ち）。
- **Phase 13まで完了 (2026-09-30)。** Phase 11最終コード `5e3c3275` はGolden 7/7、CTest 5/5、GL分類D=0、shellshot 28枚、[CI 36666551184](https://github.com/Zection6V/Fruity-Prime/actions/runs/36666551184) 11/11 PASS。Phase 10最終コード `220e900a` はGolden 7/7、GPU lifetime arena 5/5・実マップcel 3/3で解放後全種ゼロ、shellshot 24枚、2クライアントのSANCTORUS↔PROVING GROUND遷移がPASS。C++ソース同一の `a6144b61` は [CI 36647299171](https://github.com/Zection6V/Fruity-Prime/actions/runs/36647299171) 10/10 PASS。Phase 13実装SHA `8c6f2d2a` はclear-only Vulkan presentation、resize/fullscreen/minimize復帰、終了を実機でvalidation errors 0、CTest 5/5、[CI 36684051768](https://github.com/Zection6V/Fruity-Prime/actions/runs/36684051768) 11/11 PASS。詳細は[Phase 10/11検証記録](Fruity-Prime-CPP-OpenGL-RHI-Phase10-11-Gate-2026-09-30.md)と[Phase 13 gate記録](Fruity-Prime-CPP-Vulkan-RHI-Phase13-Gate-2026-09-30.md)を参照。**次はPhase 14。** 以下のPhase 4〜5記録は過去の経緯として保持する。

- **Phase 5 完了 (2026-09-30, SHA `5e52078b5545294cfa423715457e1a2279cd9398`)。** 本文の完了条件5項目すべてチェック済み。次はPhase 6。
  - **built-in撤去:** desktopのGLSL 1.20 shaderは`gl_Vertex`/`gl_Normal`/`gl_Color`/`gl_MultiTexCoord*`を読まず、`a_position`/`a_normal`/`a_color`/`a_texcoord`/`a_texcoord1`を`attribute`で宣言。`GL::LinkProgram`がリンク前に名前でlocationをbindする。共通定義は`NativeRuntime/Rhi/VertexSemantics.hpp`の1箇所（desktop 0/2/3/8/9 = NV alias表、GLES 0/2/1/3、Vulkan 0/1/2/3/4）。matrix-stack indexは`TexCoord.z`で運ぶことを契約として明記。current-value呼出しはgenericとconventionalの両方を設定し、link時にconventional既定値をgenericへ写す。
  - **定数構造体:** `NativeRuntime/Rhi/ShaderConstants.hpp`（Frame / SceneLight / SceneFog / Material / Draw / CelPost / HudPost / DisruptionPost）と`ShaderConstantSink`。uniform location cacheはOpenGL backend（`Rhi/OpenGL/OpenGlShaderInterface`）へ移動。Rendererの直接uniform upload約110箇所→42箇所（残りはtexture/override/flat/fog等の個別フラグ）。`DrawMovieFrame`のint版2呼出しはGLが拒否する前提の上流挙動なので意図的に保持。
  - **Vulkan interface:** `NativeRuntime/Rhi/VulkanShaderInterface.hpp`（vertex location、descriptor set/binding、各定数群のstd140 mirrorとpacking、GLSL 450宣言）。
  - **描画一致:** Phase 3 baseline `13c49e35...`と同一harness/入力で、`21b846ba`（explicit input）・`ce596880`（定数IF）・`5e52078b`（HUD/Post定数）の3段階すべて7候補exact RGB PASS。`-shellshot`の試合/ポーズ画面もPhase 4 SHAと一致（ランチャーはbackdropの時間アニメーション差のみ）。
  - **CI:** `ce596880`のPR run [36596973738](https://github.com/Zection6V/Fruity-Prime/actions/runs/36596973738) 9/9 PASS（Windows/MSVC、Linux、macOS、Android contract/NDK×2/APK、Phase 4監査、新規Phase 5監査）。
  - **静的ゲート:** `tools/check-phase5-shader-interface.py`（CI job）とconfigure時gate。desktop shaderのbuilt-in禁止、ES shaderのlocationと表の一致、`EsShaders`のdesktop hash一致、移行済み定数群の生upload禁止を検査。
  - **既存不具合の修正:** `EsShaders::CheckInSync`の記録hashがshader clamp修正以降ずっと古く、Android headは最初のshader compileで例外になる状態だった。現行sourceのhashに更新（ES版の内容はclampも含め同期済みを確認）。

- **Phase 4 完了 (2026-09-30, SHA `1e98392c98fd37c33932bc6d65eb1878325874e2`)。** 本文の完了条件7項目すべてチェック済み。以下の旧記録は経緯として残す。
  - **色の回帰の原因と修正:** desktopのgeometryはgeneric location 1-3で送られ、GLSL 1.20の組み込み入力向けにconventional arrayへmirrorされていた。NVIDIA (RTX 5070 Ti, driver 617.14) のcompatibility driverはgeneric 3を`gl_Color`にaliasするため、色がInheritedの描画（地形・particle・HUD）はtexcoord `(s,t,1)`を色として読み、赤・黄・緑に飽和していた。desktopのlocationをconventional属性のalias表の位置（position 0 / normal 2 / color 3 / texcoord 8）へ移し（`GL::VertexInput`）、Androidは0-3のまま。generic API（`glVertexAttribPointer`/`glEnableVertexAttribArray`）は引き続き実使用で、§4.3を満たす。
  - **MSVC link修正:** `WeaponInfo`/`MessageInfo`のforward宣言のclass/struct不一致（MSVCはmangleが変わる）を定義側に揃え、未定義だった`TestMiscInterop::LoadPngRgb`を`NativeRuntime::LoadPng(bytes, 3)`で実装。
  - **Screenshot parity:** Phase 3 baseline `13c49e35...` とPhase 4 `1e98392c...` を同一harness `1e7daefc...`・同一paths.txt `2dd4142d...`・同一mapdir/cwd/startup maps（`C:	mp\gp\inputs\maps2` = arena+pads）で新規capture。`tools/validate-golden-parity.py` で**7候補すべてexact RGB PASS**。対照として修正直前の `341d8f0e...` を同一条件でcaptureし、**7候補すべてFAIL**、HUD画像で報告どおりの赤・黄・緑を再現。差分は本修正のみなので因果を確認。
  - **CI:** PR run [36589852308](https://github.com/Zection6V/Fruity-Prime/actions/runs/36589852308) が8/8 jobs PASS（Windows/MSVC、Linux/GCC、macOS/Clang、Phase 4 static audit、Android contract/NDK arm64+x86_64/APK）。
  - ローカル: MSYS2 Release build、CTest 3/3、`tools/check-phase4-legacy-gl.py` PASS。

- **作業中: Phase 4 — OpenGL Display List撤去・VBO/IBO化。** Phase 3は比較用の基準であり、現在の実装対象ではない。
- 作業ブランチ: `develop3_rendering`
- 前回のPhase 4監査・runtime検証済みSHA: `72535095f7e340488a1c0191091b09f2b1f1c82f`。
- 現在のローカル／リモートSHA: `341d8f0ec3bd5ac4af75c3c0773851b6b45cb7cb`（`git ls-remote origin refs/heads/develop3_rendering` と一致）。このSHAはskillと本進捗MDのみの更新で、renderer/workflow sourceは`cb44456...`から不変。作業ツリーでは `src/MphRead.Native/NativeRuntime/OpenTK/GL.cpp` だけが未コミットで変更されているため、ユーザー所有の実験パッチとして保持し、commit・reset・captureに混ぜない。
- **最新SHA `341d8f0` のPhase 4検証は未完了。** canonical PR run [36574766655](https://github.com/Zection6V/Fruity-Prime/actions/runs/36574766655) は2026-09-29 15:05 UTCに終端FAIL（他7 jobs PASS、Windows/MSVC final link FAIL）。runner-owned adapter run [36570852867](https://github.com/Zection6V/Fruity-Prime/actions/runs/36570852867) はSHA `cb44456...`で終端FAIL。artifactはdirect target object symbols、archive linkermembers、vcxproj/tlogs、binlogを保持し、未解決3シンボルについて`WeaponInfo`のclass/struct ABI tag (`V`/`U`) と `MessageInfo`のtag (`U`/`V`) が定義側と呼出側で不一致と証明した。sourceにも対応するclass/struct forward宣言不一致があるが、修正・再buildは未実施。同じrenderer sourceを含む別SHAの結果とは区別する。SHA `5cb29c2...` のcaptureでは7候補すべてexact-RGB比較FAILし、control群も不一致。Phase 4 parity未確立。
- この修正は親 `72535095...` に対するcommitで、MSVC parity adapter構成に限って既存のproduction TU `Metadata/Weapons.cpp` と `Entities/EntityBase.cpp` を実行ファイルへ直接含め、adapter以外のcanonical MSVC設定は維持するもの。実CMake targetのlink成功を狙ったが、Actions run `36549202239` で同じ3シンボルの未解決により失敗した。従って、この修正はリンク問題を解決していない。
- ローカル未コミット変更は `GL.cpp` のみ。generic APIのindices 0–3を従来client arrayだけへ特例化する実験で、Phase 4の汎用attribute契約とPhase 5 explicit inputを壊すため、ChatGPTのread-only調査でも本番修正として未承認・未検証。ユーザー所有差分を保持し、根本原因・安全な修正の証拠なしに採用しない。
- **Phase 4全体は未完了。** 後述の保留ゲートが解消し、Phase 4本文の完了条件が満たされるまで後続フェーズへ進まない。

## Phase 4の検証状況

| 項目 | 状況 | 根拠・制約 |
|---|---|---|
| ゲームシーンのVBO/IBO化とバックエンド中立なリソース／キャッシュ所有 | PASS | SHA `72535095...` を対象にしたPhase 4監査、および実ゲーム経路の検証。 |
| 汎用頂点属性の実利用 | PASS | 同SHAの監査で、単なる宣言でなく実際に使用されることを確認。 |
| Display List APIの残存 | PASS（残存なし） | C/C++ 1,030ファイルの静的検索で該当0件。 |
| モデル描画経路のImmediate Mode残存 | PASS（該当なし） | 同SHAの監査。 |
| 対象の動的ジオメトリがTransient VBO/IBOを使用 | PASS | 同SHAの監査。 |
| 実ゲーム経路でのモデルGPUリソース破棄・再ロード・再描画 | PASS | Release実行で `MP10 OVERLOAD` mesh 2 のGPU teardown/reload/drawを確認。再ロード後の描画はframe 8。 |
| 試合前ハンター・プレビュー | PASS | 実ランチャーのプレビュー経路で確認。 |
| 実試合後の結果画面とプレビュー再描画 | PASS | 実試合後READYを確認し、プレビューscene generation 1→2を確認。 |
| Phase 4の新規Golden Captureと厳密RGB比較 | **FAIL（未解決）** | source SHA `5cb29c2...` の新規captureと同一harness/runtime inputで比較。7候補（transparent-object/decal/particle/trail/hud/fade/whiteout-disruption）がすべて`capture_rgb_fnv1a64`不一致。control画像も全群で異なるため、原因は未特定。ChatGPTのread-only調査ではInherited色のattrib-3→`gl_Color`経路が歴史的NVIDIA GLSL資料と実際の呼出順に整合するとされたが、617.14上の再現とGolden Capture因果は未証明。 |
| Windows/MSVC canonical GitHub Actions (前SHA) | **FAIL** | SHA `0da7f785...` のrun `36549208952` が終端FAIL。リンクで4 unresolved externals。ローカルMSYS2成功ではMSVCゲートを代替できない。 |
| Windows runner-owned adapter real CMake target (前SHA) | **FAIL** | SHA `0da7f785...` のrun `36549202239`。依存導入・configure・Phase 3/4 shared compileはPASSだが、実 `fruity_prime` linkで `Weapons::Current`、`Weapons::WeaponsMP`、`EntityBase::HandleMessage` の3 unresolved externals。 |
| Windows runner-owned adapter診断run (SHA `5cb29c2`) | **FAIL** | run `36563182417` は12:43:31 UTCに終端。artifactにはbuild log/binlog/exit codeのみで、直接object dump、archive linkermember、vcxproj/tlogがなく、必要なlink診断は未取得。 |
| Windows runner-owned adapter診断run | **FAIL** | SHA `cb44456...` のrun `36570852867`。Phase 3/4 shared compile、依存導入、configure PASS、final link FAIL（2026-09-29 14:32:23 UTC）。artifact `C:\Users\Admin\AppData\Local\Temp\FruityPrimeAdapterArtifacts-36570852867`でdirect `Weapons.obj`/`EntityBase.obj` exportsと`MphRead.Native.lib` indexの一致、および呼出側要求との `WeaponInfo` / `MessageInfo` class-key tag不一致を確認。source上のforward declaration不一致が有力原因。 |
| 通常build_cpp push run (`341d8f0`) | **CANCELLED** | run `36574760243` は同SHAのPR runとのconcurrencyによりcancelled。 |
| 通常build_cpp PR run (`341d8f0`) | **FAIL** | run `36574766655` は2026-09-29 15:05:00 UTCに終端。Phase 4 static audit、Linux/GCC、macOS/Clang、Android contract/NDK arm64+x86_64/APKはPASS。Windows/MSVC final linkはFAIL。 |
| 通常build_cpp PR run (`cb44456`) | **CANCELLED** | run `36570861650` は後続のPR実行とのconcurrencyによりcancelled。 |
| 通常build_cpp PR run (SHA `5cb29c2`) | **FAIL** | run `36563190293` は終端FAIL。7 jobs PASS、Windows/MSVC final linkで `LoadPngRgb`、`Weapons::Current`、`Weapons::WeaponsMP`、`EntityBase::HandleMessage` が未解決（`LNK1120: 4 unresolved externals`）。 |

上表のPASSは特定SHAで得た個別証拠であり、フェーズ全体の完了宣言ではない。Phase 4本文の6条件はSHA `72535095...` の証拠に基づき個別にチェック済み。最新SHAのscreenshot parityはcapture済みだが7候補のexact-RGB比較がFAILし、原因修正・再captureは未完了。Phase 4全体は未完了。

## CI記録

- 前回のcanonical workflow: [36545375961](https://github.com/Zection6V/Fruity-Prime/actions/runs/36545375961)、SHA `72535095f7e340488a1c0191091b09f2b1f1c82f`。結果は**他7 jobs PASS、Windows/MSVC FAIL**。最終リンクで `LNK1120: 4 unresolved externals`。
- 未解決シンボル: `MphRead::Testing::TestMiscInterop::LoadPngRgb(std::istream&)`、`MphRead::Weapons::Current`、`MphRead::Weapons::WeaponsMP`、`MphRead::Entities::EntityBase::HandleMessage(MessageInfo)`。
- したがって、MSVCでのリンク成功は未達。スタブ追加やテスト弱体化を成功扱いにせず、実際の定義元・ターゲットへの組み込みを調査する。MSVC向けadapter構成の成功とcanonical構成の成功も区別して記録する。
- 別途、Windows MSYS2 Release buildとCTest 3/3 (`RendererGeometry`, `RendererGpuMesh`, `GoldenCaptureValidation`) はPASS。ただし、これは上記MSVC失敗を解消しない。
- 現行head SHA `0da7f785acb6cd6aaca5adc01c7b26dab36758f0` のcanonical PR workflow: [36549208952](https://github.com/Zection6V/Fruity-Prime/actions/runs/36549208952)。PR #3のheadは `develop3_rendering` / `0da7f785...`、baseは検証専用 `z_develop2` / `412d72e...`、実ジョブはmerge ref `a20d1cceea146bd8d87a1872db734f511bed111a` をcheckout。最終結果は**他7 jobs PASS、Windows/MSVC FAIL**。MSVC library作成後の `FruityPrime.exe` linkで `TestMiscInterop::LoadPngRgb`、`Weapons::Current`、`Weapons::WeaponsMP`、`EntityBase::HandleMessage` の4 unresolved externals。PR head SHAとmerge refを混同しない。
- 現行head SHA `0da7f785...` のrunner-owned adapter workflow: [36549202239](https://github.com/Zection6V/Fruity-Prime/actions/runs/36549202239)。eventはpush。Phase 3/4 shared adapter compile jobs、依存導入、CMake configureはPASS。Phase 4実CMake `fruity_prime` buildはFAILし、最終linkで `Weapons::Current`、`Weapons::WeaponsMP`、`EntityBase::HandleMessage` が未解決。今回のadapter logには `TestMiscInterop::LoadPngRgb` は現れない。
- 同じSHAへのpush workflow [36549202568](https://github.com/Zection6V/Fruity-Prime/actions/runs/36549202568) はPR実行によりcancelled。上記PR runとadapter runの代わりの成功証拠にしない。
- 診断専用commit `5cb29c2562f5238d60d73e5ae4bdb9175252ae0b` は`.github/workflows/golden-parity-adapter.yml`のみ変更。build stepのbinlog/build log/exit code、direct target object symbol dump、`MphRead.Native.lib` linker-member listing、vcxproj/tlogを収集し、artifactをalways uploadしてから元のbuild exit codeを戻す計画。pushで起動したadapter run [36563182417](https://github.com/Zection6V/Fruity-Prime/actions/runs/36563182417) はPhase 3/4 compile PASS、Windows依存導入中。push build_cpp run [36563182864](https://github.com/Zection6V/Fruity-Prime/actions/runs/36563182864) は全jobがPR concurrencyによりcancelled。PR build_cpp run [36563190293](https://github.com/Zection6V/Fruity-Prime/actions/runs/36563190293) は他7 jobs PASS、Windows/MSVC `Build C++ FruityPrime` 実行中。cancelはGitHub concurrencyによるもので手動cancelではない。
- adapter run `36563182417` は12:43:31 UTCにFAILで終端。artifact `C:\Users\Admin\AppData\Local\Temp\FruityPrimeAdapterArtifacts-36563182417` は`fruity_prime.binlog`、`fruity_prime-build.log`、`fruity_prime-build-exit-code.txt`のみを含み、collector構文不備によりdirect target object symbols、`MphRead.Native.lib` linkermember、vcxproj/tlogは採取されなかった。ChatGPTは閉じquote一文字を直したcommit `cb44456d4bbf221f60a894084a7cf6f9cb12a1a5` をpush。差分は当該workflow 1行のみ。ローカルへfast-forward後にremote SHA一致を確認済み。修正後adapter run [36570852867](https://github.com/Zection6V/Fruity-Prime/actions/runs/36570852867) とcanonical PR run [36570861650](https://github.com/Zection6V/Fruity-Prime/actions/runs/36570861650) が進行中。push build_cpp run `36570852756` はPR concurrencyでcancelled。
- latest SHA `5cb29c2562f5238d60d73e5ae4bdb9175252ae0b` のPhase 4 captureは新規managed worktree `C:\Users\Admin\.codex\worktrees\phase4-head-capture\Fruity-Prime` で作成。MSYS2 RelWithDebInfo buildとcaptureは成功し、出力先は`C:\Users\Admin\AppData\Local\Temp\FruityPrimeGoldenParity-20260929-Phase4-head5cb-run1`。同一harness SHA `1e7daefc...`・同一runtime input hashesのPhase 3 baseline `13c49e35...` と比較した7候補すべてでcapture RGB fingerprintが異なり、control画像も不一致。HUD画像では大きな色差も目視確認。入力差か描画差かの原因切り分けは未完了で、PASS扱いにしない。

確認時点 (`2026-09-29 10:12 UTC`) では、canonical PR runは終端し他7 jobs PASS、Windows/MSVC FAIL。job logは `FruityPrime.exe` の最終linkで上記4 unresolved externalsを記録。adapter runはPhase 3/4 shared compileがPASS、実CMake buildは `Install native dependencies` 中（step start `2026-09-29T09:26:56Z`）。そのvcpkg stepの対象はcurl/libarchive/zlib/glfw3/freetype/openal-soft/Skia。実行中job log APIは現時点で404 `BlobNotFound` を返すが、run/job自体はIN PROGRESSであり、失敗根拠とは扱わない。ChatGPT parity会話は同じ会話・同じタブで停止ボタン表示、回答生成中。**adapter buildが終端するまではbranchを書き換えず**、同じSHAのadapter実CMake結果を待つ。その後canonical MSVC失敗の最小かつ正直な修正を同じ会話で詰める。Screenshot parityは未確立。

## 次に行うこと

1. 並行するChatGPT作業は最大2件。`Screenshot parity validation`はMSVC linker診断の回答生成中なのでStop表示の間は送信せず、結果を読んでから同じ会話で必要な原因確認・最小修正を続ける。GL診断設計チャットの回答は履歴から実際に再オープンして全文確認済み。設計はGL 3.2 compatibility context上の四つのケースと、driver機構／Golden Capture因果の証明を分離する。完了した設計チャットを閉じてから、新規の独立した実装チャットを開始する。回答のRetryは押さない。
2. adapter run `36570852867` とcanonical PR run `36574766655` はともに終端FAIL。adapter artifactから、定義object/archiveは`Weapons::Current`/`WeaponsMP`を`class WeaponInfo` (`V`) で、`EntityBase::HandleMessage`を`struct MessageInfo` (`U`) で定義する一方、呼出側は逆tagを要求することを確認。sourceの`PlayerEntity.hpp`/`PlayerAi.hpp`の`struct WeaponInfo`、`EntityBase.hpp`/`PlayerProcess.hpp`の`class MessageInfo` forward宣言が不一致の根拠。Screenshot chatの厳密監査後、正確な整合修正を同じ会話で依頼し、commit SHA・差分・canonical MSVC結果を確認する。次のpush前にActions concurrencyを再確認する。
3. GL原因はゲームを起動せず隔離した最小FBOテストで調べる。generic attribute 3→`gl_Color`機構の再現とPhase 4 Golden Capture因果を分離し、メカニズムの再現だけでRGB原因確定としない。ユーザーPCでゲームを起動しない。
4. capture検証はrenderer source SHA `5cb29c2...`で実行済み（後続`cb44456...`はworkflow quote 1行のみ）。7候補すべてFAILし、control画像も不一致。source capture/harness `1e7daefc...`とPhase 3 baselineを保持する。原因修正後は新しいrenderer source SHAで同一条件のcaptureと全候補validatorを再実行しPASSを確認する。既存managed worktreeのoverlay/captureは消去・上書き・restoreしない。
5. Phase 4本文の全完了条件、監査、必要な修正、該当CIゲートがそろった時点で初めて本節と本文を更新し、次フェーズへ移る。

## 並行作業レジャー

| Work item | Chat / phase | Current evidence | Next action |
|---|---|---|---|
| MSVC parity diagnostics | [Screenshot parity validation](https://chatgpt.com/g/g-p-6ab90bc27cc8819194c3b1a42dff49d9-fruityprime/c/6abb3bc5-7620-83ee-9fcc-7c7d3cfedabe) — correction / CI verification | Adapter run `36570852867` FAILED at `cb44456...`; canonical run `36574766655` FAILED at `341d8f0...`. Artifact proves three class/struct ABI-tag mismatches and keeps direct object/archive/link inputs. The same chat is still processing the source-cause review. | Stop/waveform UIを監視。応答を読んで、必要なsource audit/fixを同じ会話で続ける。 |
| Phase 4 color regression / minimal GL repro | [Design GL Diagnostic Tests](https://chatgpt.com/g/g-p-6ab90bc27cc8819194c3b1a42dff49d9-fruityprime/c/6abbca6c-af50-83e9-b141-1ea59df77b44) — complete design | Reopened from visible history and read fully. It specifies a hidden GL 3.2 compatibility context, GLSL 1.20, raw GL calls, 1x1 RGBA8 FBO for cases 1-3 and 4x4 for case 4: legacy red control; generic-3 green only while conventional color array is off; both arrays with two setup orders (no specified alias precedence); and faithful inherited-color `(s,t,1)` with conventional texcoord mirror, magenta current color, shader reading `gl_Color` and `gl_MultiTexCoord0`, full state/error dump. | Complete design chat; start a new independent ChatGPT implementation item for a standalone diagnostic target, without running the game. |

## 更新履歴

- **2026-09-29:** SHA `72535095...` のPhase 4個別監査・runtime PASS、未完了のGolden Capture parity、Actions run `36545375961` のWindows/MSVC link failureを記録。後続のadapter修正commit `0da7f785...` と、そのcanonical/adapter Actions実行中状態を追記。Phase 4を未完了として維持。
- **2026-09-29 09:55 UTC:** SHA `0da7f785...` の既存run `36549208952` を再確認。7 jobsはPASSのまま、Windows/MSVCは `Build C++ FruityPrime` 実行中。run `36549202239` はPhase 3/4 shared adapter compileがPASSのまま、実CMake targetは `Install native dependencies` 中。終端結果・新規失敗なし。parity ChatGPT会話は同じタブで生成・監視継続中（停止ボタン表示）。再試行・再実行・新規pushはしていない。
- **2026-09-29 10:04 UTC:** canonical run `36549208952` が終端し、SHA `0da7f785...` のWindows/MSVCは他7 jobs PASSに対してFAIL。job logで4 unresolved externalsを確認し、PR head (`0da7f785...`) と検証用merge ref (`a20d1cce...`) を記録。adapter run `36549202239` は引き続き実CMake依存関係インストール中、共有Phase 3/4コンパイルPASS。ChatGPT parity会話は履歴から同じ会話を再オープンしてStop表示を確認、生成中。別runを起動せず、pushもしない。
- **2026-09-29 10:12 UTC:** adapter run `36549202239` を再ポーリングし、head SHA `0da7f785...` のままIN PROGRESS、実CMake jobは `Install native dependencies`（09:26:56 UTC開始）を継続、Phase 3/4 shared compileはPASS。定義済みvcpkg install対象はcurl/libarchive/zlib/glfw3/freetype/openal-soft/Skia。active jobのlogs endpointは404 `BlobNotFound` だが、run/job APIはlive状態を返すためterminal failureとは推定しない。ChatGPT会話はStop表示で監視・分析中。書き込み・再実行・キャンセルなし。
- **2026-09-29 10:19 UTC:** ローカルHEADと `origin/develop3_rendering` は引き続き `0da7f785acb6cd6aaca5adc01c7b26dab36758f0` で一致。canonical run `36549208952` は終端FAIL（他7 jobs PASS、Windows/MSVCは `FruityPrime.exe` 最終linkで4 unresolved externals）。adapter run `36549202239` はIN PROGRESSのまま、Phase 3/4 shared compileはPASS、Phase 4 runner-owned CMake jobは `Install native dependencies` を継続（09:26:56 UTC開始）；configure/build stepは未開始。ChatGPTの同一parity会話は回答生成中（Stopボタン表示）で、送信・Retryはしていない。ローカルのskill/進捗MD以外に変更なし。adapter終端までpush・再実行・キャンセルを行わず、Phase 4 screenshot parityも未確立として維持する。
- **2026-09-29 10:27 UTC:** ChatGPT parity会話がstream recovery polling timeoutで中断し、Retryボタンと音声アイコンを確認。Retryは押さず、同じ会話へ直前の作業状況と継続条件を一度送信し、再びStopボタン（生成中）になったことを確認。adapter run `36549202239` は再確認時もIN PROGRESS、Phase 4 runner-owned CMake jobは `Install native dependencies`（09:26:56 UTC開始）のまま。canonical run `36549208952` のMSVC失敗は既知の終端結果で、再実行なし。branchへの書き込み・push・キャンセルなし。Phase 4 screenshot parityは未確立。
- **2026-09-29 10:33 UTC:** adapter run `36549202239` の状態は変わらずIN PROGRESS。Windows runner-owned CMake jobは `Install native dependencies`（09:26:56 UTC開始、約66分経過）で、configure/buildは未開始。`gh run view --job ... --log` はjob進行中のため「logs will be available when it is complete」と返し、live step logを取得できない。shared Phase 3/4 compileはPASS。ChatGPTの同じparity会話は継続依頼送信後もStop表示（生成中）。Retry・再実行・キャンセル・branch pushなし。Screenshot parity未確立のまま。
- **2026-09-29 10:35 UTC:** adapter run `36549202239` のPhase 4 Windows runner-owned CMake jobで依存関係導入がSUCCESS、`Configure runner-owned parity build` もSUCCESS（開始10:34:15 UTC）、`Build real fruity_prime target with native validation dependency` は10:34:29 UTC開始でIN PROGRESS。従って、先ほどまでの依存導入待ちは解消し、実targetの結果待ちに移った。runのhead SHAは引き続き `0da7f785acb6cd6aaca5adc01c7b26dab36758f0`。canonical run `36549208952` は同SHAでWindows/MSVC FAILのまま。ChatGPT同一parity会話はStop表示で継続中。Retry・再実行・キャンセル・追加pushなし。Phase 4 screenshot parity未確立。
- **2026-09-29 10:40 UTC:** run `36549202239` を再確認。head SHA `0da7f785...`、依存関係導入とCMake configureはPASS、実 `fruity_prime` target build（開始10:34:29 UTC）は引き続きIN PROGRESSで、終端結果・新しい失敗なし。ChatGPT parity会話は引き続きStop表示。新規push・再実行・キャンセルなし。Screenshot parity未確立。
- **2026-09-29 10:47 UTC:** adapter run `36549202239` はhead SHA `0da7f785...` のままIN PROGRESS。実target buildは10:34:29 UTC開始から約12分継続しており、Actions API上で同じstepが実行中、終端結果・報告された失敗はまだない。ChatGPTの同一会話は継続依頼後約20分で、最新画面もStop表示（生成中）。Retryは使用せず、追加メッセージも送っていない。ブランチ更新なし。Screenshot parity未確立。
- **2026-09-29 10:51 UTC:** 次の未完了ゲートに備え、`tools/run-golden-parity-capture.py` を読み取り専用で確認。helperのprepare/capture/restoreは、3つのGoldenCapture overlay以外のdirty pathを検出すると拒否する。現在のskill/進捗MDを壊さず、Phase 3/Phase 4のcaptureとvalidatorを行うには対象SHAのcleanな隔離worktreeが必要と判明したため、手順3をその条件と「Phase 3/4の両capture validation完了後にrestore」に具体化。コードやcapture artifactは変更・作成していない。adapter run `36549202239` は依然IN PROGRESSで実target build中、ChatGPT会話はStop表示。Screenshot parity未確立。
- **2026-09-29 10:55 UTC:** run `36549202239` のreal `fruity_prime` target buildは10:34:29 UTC開始から約20分継続してIN PROGRESS、Actions APIは同じbuild stepを示し新たなFAILは報告していない。依存導入/configureはPASS済み。ChatGPTの同一parity会話も継続依頼後Stop表示のまま。追加メッセージ・Retry・push・再実行・キャンセルなし。Screenshot parity未確立。
- **2026-09-29 11:00 UTC:** canonical MSVC link errorを読み取り専用で追加調査。`TestMiscInterop::LoadPngRgb` は `TestMisc.cpp` 内にforward declarationと呼出ししかなく、native側定義が見当たらない。C# `TestMisc.cs` の対応箇所は `StbImage.Load(fs, StbiImageFormat.Rgb)` 後にRGB byte列を比較しており、nativeには既存 `NativeRuntime::LoadPng(bytes, 3)` がある。これは意味を保った実装候補で、空実装/スタブで置き換えない。`Weapons::Current` と `WeaponsMP` は `Weapons.cpp` に定義と `Metadata.hpp` に宣言があり、`EntityBase::HandleMessage` も既存定義があるため、MSVC archive/link構成の原因を別途解明する必要がある。まだコード変更なし。adapter run `36549202239` はreal target build中、ChatGPT同一会話もStop表示。Phase 4未完了。
- **2026-09-29 11:10 UTC:** adapter run [36549202239](https://github.com/Zection6V/Fruity-Prime/actions/runs/36549202239) がSHA `0da7f785acb6cd6aaca5adc01c7b26dab36758f0` で終端FAIL。Windows job `Phase 4 / Windows runner-owned CMake build with shared adapter` は依存導入PASS（09:26:41–10:34:15 UTC）、CMake configure PASS（10:34:15–10:34:29）、実 `fruity_prime` build FAIL（10:34:29–11:06:26）。Phase 3/4 shared adapter compileもPASSだが、本物のMSVC CMake targetはlinkできていない。link logの未解決は `MphRead::Weapons::Current`、`MphRead::Weapons::WeaponsMP`、`MphRead::Entities::EntityBase::HandleMessage(MessageInfo)` の3件で、今回のadapter logに `LoadPngRgb` は出ていない。canonical run `36549208952` も同SHAで終端FAIL（他7 jobs PASS、Windows/MSVC linkは4 unresolved externals）。
  - 失敗後に行ったローカルread-only確認では、3シンボルとも宣言・定義が存在する（`Metadata.hpp` 476–478行、`Metadata/Weapons.cpp` 389行および828行、`Entities/EntityBase.cpp` 1219行）。それでも直接追加した `Weapons.cpp` / `EntityBase.cpp` がMSVC final linkで解決されなかった。原因は未特定であり、「archive境界が原因」「CMake source追加で解決可能」と断定しない。11:00記録は調査時点の候補であり、現時点ではコード変更・commit・新規runなし。
  - ChatGPT `Screenshot parity validation` の同一会話は引き続き回答生成中（停止ボタン表示）。別チャット、Retry、追加送信は使わず、同じ会話の返答を待つ。Phase 4 screenshot parityは未確立、Phase 4全体も未完了。
- **2026-09-29 11:22 UTC:** 古い「Audit Golden Contract Mismatch」会話を読み直し、exact SHA `6395ca04...` では `phase4-final-stage-v3` shared adapter contractが意図された変更で、当時のnative validation test/CMake adapter-mode連携が不整合、という終端read-only監査結果を確認した。この契約不整合は現行SHA `0da7f785...` のrun `36549202239` の失敗原因ではない。同runはadapter共有compile、configure、検証targetのコンパイルを越え、最終 `fruity_prime` linkで3つのMSVC未解決シンボルを報告している。監査会話は完了済みとして閉じ、再監査しない。ローカル環境では `cl.exe` がPATH上に見つからず、検出されたCMake/NinjaはMSYS2版のみのため、MSVC失敗はActions上の実証に限る。メインの `Screenshot parity validation` は同じ会話・URLでStop表示の生成継続中で、重複メッセージは送っていない。branch・ソース変更なし。Phase 4 screenshot parity未確立。
- **2026-09-29 11:26 UTC:** 既存run `36549202239` のterminal job/logを再確認。SHA `0da7f785...` のCMake targetは依存導入・configure・Phase 3/4 adapter compile通過後、最終linkで同じ3 symbolの unresolvedによりFAIL。ログ上 `EntityBase.cpp` と `Weapons.cpp` はlibrary側のcompile時刻（10:39台/10:44台）と失敗直前のcompile時刻（11:06台）の双方に現れる。ただし実link input一覧やCOFF symbol tableはこの出力から確認できず、「直接objectがlinkに入った」とまでは断定しない。ローカル `cl.exe` 不在のため、原因特定は未完了。主 `Screenshot parity validation` チャットは同一URLでStop表示（回答生成中）；別タブの `CMake Build Audit` は会話読み込みエラーで、再試行せず一時タブを閉じた。追加CI起動・コード変更・pushなし。Phase 4 screenshot parity未確立。
- **2026-09-29 11:27 UTC:** 主 `Screenshot parity validation` 会話でstream recovery timeoutを検知。Retryは押さず、同じ会話に一度だけ最新の確定状態を送り、古い「adapter run実行中」という前提を訂正した。両run `36549202239` / `36549208952` はSHA `0da7f785...` で終端FAIL。direct TUのcompile表示はlink input/symbol proofではない点と、stub・検証弱体化・MinGW代替を避ける条件を伝え、証拠に基づく診断・修正継続を依頼。送信後、同タブでStop表示を確認（回答生成中）。コード変更・commit・push・run再実行なし。Phase 4 screenshot parity未確立。
- **2026-09-29 11:38 UTC:** 同じChatGPT会話の診断結果は「現時点で正直なlink修正を選ぶ証拠が不足」。adapter runではv3 parity validation executableのlink/runとgeometry/GPU-mesh testsが通過し、その後の `FruityPrime.exe` 最終linkだけが3 symbol unresolvedで失敗。`Weapons.cpp` / `EntityBase.cpp` はnative library作成後に直接target向けにもコンパイルされたログがあるが、対応するobject内のdecorated symbol有無とlink入力は未証明。宣言・定義とnamespaceは一致し、archive抽出のみの問題という仮説も確定できない。次の証拠はdirect target `.obj` の `dumpbin /symbols`、`MphRead.Native.lib` の `/linkermember`、最終MSBuild link入力/binlog。回答完了後、同一会話に診断専用workflow instrumentation（production C++、canonical設定、テスト強度、依存設定を変えず、一回の失敗runで必要なログ/artifactを残す）の実装・commit/push・一度の検証を依頼し、Stop表示を確認。現在コード変更・push・再実行なし。Phase 4 screenshot parity未確立。
- **2026-09-29 11:42 UTC:** ChatGPTが `5cb29c2562f5238d60d73e5ae4bdb9175252ae0b` をpush。親は `0da7f785...`、変更は`.github/workflows/golden-parity-adapter.yml`のみ。remote headを確認してローカルをfast-forwardし、未コミットの進捗MD/skill編集が残ることを確認。adapter run `36563182417` はPhase 3/4 shared compile PASS、Windows jobはnative dependencies install中。push build_cpp run `36563182864` はLinux job実行中で、Windows/MSVC等他jobはpull_request実行とのconcurrencyによりcancelled。PR run `36563190293` はpendingでjob未開始。どのrunも手動キャンセル・追加起動なし。workflow差分は診断のためbinlog・direct object / archive symbols・vcxproj/tlogを収集し、always uploadして元のbuild exit codeを最後に再伝播する構成。adapter runの結果/artifact待ち。Phase 4 screenshot parity未確立。
- **2026-09-29 11:46 UTC:** commit `5cb29c2...` をローカルでfast-forward後、Windows PowerShell parserで新しいdiagnostic workflow内の3つのpwsh script blockを構文検査。buildとexit propagation blockはPASSだが、collector blockは`.github/workflows/golden-parity-adapter.yml:181`の`Where-Object` regexに閉じsingle quoteがなくparse errorになることを検出。Actionsのadapter run `36563182417` は依然dependency installation中で、壊れたcollectorにはまだ到達していない。push canonical build_cpp run `36563182864` のLinux jobは実行中、同runの他platform jobはPR concurrencyでcancelled。PR run `36563190293` はpending。停止中のChatGPTに割り込まず、構文修正は回答停止後に同じ会話で依頼する。現行runのdiagnostic artifactも確認し、不完全なら最小構文修正後に証拠採取のためだけの一回を検討する。production code変更・commit/push追加・手動cancelなし。Phase 4 screenshot parity未確立。
- **2026-09-29 11:46 UTC:** commit `5cb29c2...` をローカルでfast-forward後、Windows PowerShell parserで新しいdiagnostic workflow内の3つのpwsh script blockを構文検査。buildとexit propagation blockはPASSだが、collector blockは`.github/workflows/golden-parity-adapter.yml:181`の`Where-Object` regexに閉じsingle quoteがなくparse errorになることを検出。Actionsのadapter run `36563182417` は依然dependency installation中で、壊れたcollectorにはまだ到達していない。push canonical build_cpp run `36563182864` のLinux jobは実行中、同runの他platform jobはPR concurrencyでcancelled。PR run `36563190293` はpending。停止中のChatGPTに割り込まず、構文修正は回答停止後に同じ会話で依頼する。現行runのdiagnostic artifactも確認し、不完全なら最小構文修正後に証拠採取のためだけの一回を検討する。production code変更・commit/push追加・手動cancelなし。Phase 4 screenshot parity未確立。
- **2026-09-29 11:50 UTC:** GitHub Actionsの最新状態を確認。adapter run `36563182417` はSHA `5cb29c2...` のままWindows native dependency installation中（開始11:40:33 UTC）、shared Phase 3/4 compile PASS。push build_cpp run `36563182864` は全jobがPR runとのconcurrencyによりCANCELLED（手動操作なし）。同SHAのPR run `36563190293` は他7 jobs PASS、Windows/MSVC jobが `Build C++ FruityPrime` 実行中。PowerShell diagnostic collectorのline 181構文不備は証拠で確認済みだが、このworkflow codeを使うadapter runはcollector step未到達。閉じquoteをメモリ上だけ補ったcollector全体のparseはPASSしたが、repo/workflowは未変更。ChatGPT主会話は同じタブで依存導入待機中（Stop表示）。追加push/cancel/retryなし。Phase 4 screenshot parity未確立。
- **2026-09-29 12:21 UTC:** 最新状態を再確認。ローカルと`origin/develop3_rendering`は引き続き`5cb29c2562f5238d60d73e5ae4bdb9175252ae0b`で一致し、未コミットのskill/進捗MDを保持。PR build_cpp run [36563190293](https://github.com/Zection6V/Fruity-Prime/actions/runs/36563190293) は12:17:53 UTCに終端FAIL、他7 jobs PASS。Windows/MSVCの実ログは`FruityPrime.exe`最終linkで`LNK1120: 4 unresolved externals`を確認: `TestMiscInterop::LoadPngRgb`、`Weapons::Current`、`Weapons::WeaponsMP`、`EntityBase::HandleMessage`。push run `36563182864`はPR concurrency起因のCANCELLED。診断adapter run [36563182417](https://github.com/Zection6V/Fruity-Prime/actions/runs/36563182417)はPhase 3/4 shared compile PASS、Windows jobは`Install native dependencies`のままIN PROGRESS。診断collectorはline 181のPowerShell parse不備があるため、run終端後にartifactを必ず確認し、証拠が不足する場合のみ同じChatGPT会話で最小修正と必要な検証を行う。ChatGPT「Screenshot parity validation」同一タブではStop表示（回答生成中）を確認したため送信せず、同runのみ監視。Phase 4は引き続き実施対象で、Phase 3は比較基準のみ。Golden Capture exact-RGB parity未確立、Phase 4未完了。
- **2026-09-29 12:30 UTC:** Phase 4 parity-capture worktreeの既存状態をread-onlyで調査。`phase4-parity-capture` はsource SHA `56b886d40efaf200410fffb819df0f369564d144` で、helper statusに記録されたGoldenCapture harness SHAは`1e7daefc29245d8cf9c8b2a5078b2e586527e325f52ed95d7ccfccb2b6d1f387`。`local-phase4-20260929-run3` のbuild manifestは実行ファイルSHA `ee2932bd34031bc2874dc6e388317be998c8106ca21ccbdf971ce310101ce218` を記録し、FruityPrimeログ末尾にはTEST ARENA読込、NVIDIA RTX 5070 Ti / OpenGL 4.6、`whiteout-disruption captured`、HUD/fade raw-RGB fingerprints distinct、process exitingがある。ただしcapture出力先のPNG/manifest一式とhelper最終validator結果はこのbuild dirでは見つからず、exact-RGB parityの証拠とは数えない。Phase 3関連worktreeもGoldenCapture overlay dirtyのため保持し、restore/再取得/上書きはしていない。既存run/出力の所在特定とvalidator確認を次に行い、不足分だけを再取得する。adapter run `36563182417` は引き続き依存導入中、ChatGPT同一会話はStop表示で生成継続中。Phase 4未完了。
- **2026-09-29 12:34 UTC:** capture出力を特定し、Phase 3 run2 (`source_commit=13c49e35...`) と旧Phase 4 run3 (`source_commit=56b886d...`)を同じharness SHA `1e7daefc...`・同じruntime input hashesでvalidatorにかけ、7候補すべてPASS exact pixel equalityとなった。ただしコミット鎖確認で`56b886d..5cb29c2`にPhase 4の描画実装コミット（`71d08f6b`, `72535095`）があり、`Renderer.cpp/.hpp`、`OpenGlGeometry.cpp`、GL関連ソースも変更済みと判明。よってこのPASSは旧snapshot限定で、現在のPhase 4 screenshot-parity gateには適用しない。現行対象SHA `5cb29c2...` の新しいcapture用managed worktree作成を開始（operation `bcb2d998-d5ca-4250-9209-312e8da18ba9`、作成完了前は使用しない）。Phase 4 parityは未確立。旧worktree/outputsは保存。
- **2026-09-29 12:52 UTC:** 最新状態を進捗更新。ローカルと`origin/develop3_rendering`はSHA `5cb29c2562f5238d60d73e5ae4bdb9175252ae0b`で一致。adapter run [36563182417](https://github.com/Zection6V/Fruity-Prime/actions/runs/36563182417)は12:43:31 UTCにFAILし、artifactにはbuild log/binlog/exit codeのみでdirect object/linker evidenceがない。ChatGPTの同じ`Screenshot parity validation`会話（`https://chatgpt.com/g/g-p-6ab90bc27cc8819194c3b1a42dff49d9-fruityprime/c/6abb3bc5-7620-83ee-9fcc-7c7d3cfedabe`）は、collector修正とparse検査を進行中でStop表示。重複送信・Retryなし。最新source SHAの新規MSYS2 build/captureは成功したが、baselineとの7候補すべてRGB fingerprint不一致、control画像も不一致のためparity FAIL。HUDの大きな色差を目視。診断workflow修正のcommit/runとGolden Capture mismatchの原因は未確定。既存worktreeとcapture出力は保持。
- **2026-09-29 12:55 UTC:** ChatGPTがcollectorのquote欠落だけを直したcommit `cb44456d4bbf221f60a894084a7cf6f9cb12a1a5` をpush。親`5cb29c2...`、変更は`.github/workflows/golden-parity-adapter.yml`の1行のみ。ローカルbranchをfast-forwardしHEADと`origin/develop3_rendering`を同期。修正後adapter run [36570852867](https://github.com/Zection6V/Fruity-Prime/actions/runs/36570852867) とcanonical PR run [36570861650](https://github.com/Zection6V/Fruity-Prime/actions/runs/36570861650) はIN PROGRESS、同SHAのpush build_cpp run `36570852756` はPR concurrencyでCANCELLED。過去のadapter artifact不足を受けた修正版の証拠採取中であり、新しい実装修正は未確認。Golden Captureはsource SHA `5cb29c2...` の実行で全7候補FAIL（control画像も不一致）。このcapture以降のcommitはworkflowのquote 1行のみでrenderer sourceに差分なし。ChatGPTの同一会話はStop表示で応答継続中。Phase 4未完了。
- **2026-09-29 13:01 UTC:** user指定計画書に並行作業レジャーを追加。MSVC診断は既存会話、Golden Capture原因調査は独立した2つ目のChatGPT browser tabで実施し、両tabをhandoff対象として保持。新しい会話にはbaseline/target SHA、同一harness/runtime入力の記録、7 candidate/controlのfingerprint差、HUD色差、変更禁止条件を英語で送り、read-onlyの原因調査を依頼。High推論設定をUIで確認し、送信後に同会話URLでChatGPT応答中・Stop表示を確認。両作業とも同じrenderer/workflow pathを同時編集しない。source codeの追加変更・commitなし。
- **2026-09-29 13:04 UTC:** canonical PR run `36570861650` をjob単位で再確認。8 jobs中7 jobs PASS（Phase 4 legacy-OpenGL static audit、macOS、Linux、Android contract/NDK arm64+x86_64/APK）、Windows/MSVC `Build C++ FruityPrime` はIN PROGRESS。adapter run `36570852867` はPhase 3/4 shared compile PASS、Windows dependency installは12:51:05 UTC開始で継続中、configure/buildはpending。MSVC診断ChatGPT会話はstream recovery polling timeoutでwaveform/ready状態になったため、同じ会話へ残作業だけを一度送信。送信済みuser message、composer clear、ChatGPT processing stateを画面で確認。Retryなし。並行Golden Capture調査チャットも処理中で、ChatGPTは「generic vertex attributes 0–3と従来配列を同じdrawで有効化」の可能性を検査中と報告したが、現時点では仮説であり原因確定・コード変更なし。会話の実URLは`https://chatgpt.com/c/6abbb654-75f0-83ee-b461-aca36d65b0c6`。
- **2026-09-29 13:08 UTC:** Actionsを再確認。canonical PR run `36570861650`（SHA `cb44456d4bbf221f60a894084a7cf6f9cb12a1a5`）は8 jobs中7 jobs PASS、Windows/MSVCの `Build C++ FruityPrime` がIN PROGRESS。診断adapter run `36570852867` はPhase 3/4 shared compileがPASS、Windows jobの `Install native dependencies` がIN PROGRESSで、configure/buildは未開始。2つの独立ChatGPTタブ（MSVC linker evidence、Golden Capture root-cause investigation）はともに画面上でStop表示＝回答生成中を確認したため、追加送信せず継続待機。仮説を原因確定として扱わず、run完了前のpush/再実行/cancelも行わない。branch HEAD=`origin/develop3_rendering`=`cb44456...`、手元の変更はskillと本進捗MDのみ。Phase 4 Golden Capture parityは未確立、Phase 4未完了。
- **2026-09-29 13:13 UTC:** 同一SHAでActionsを再確認し、canonicalは引き続き7/8 PASS・Windows/MSVC build IN PROGRESS、adapterはshared compile PASS・Windows dependency install IN PROGRESS。読み取り専用のsource差分確認で、Phase 4の `OpenGlGeometry.cpp` がgeneric attribute 0–3を使用し、`NativeRuntime/OpenTK/GL.cpp` の `EnableVertexAttribArray` / `VertexAttribPointer` が対応する従来のclient-state/pointerも設定することを確認（導入コミット `71d08f6b`）。これは現行互換描画経路の実装事実だが、Golden CaptureのRGB差を生じさせた原因とはまだ証明されていない。2つのChatGPTタブは引き続きStop表示のため送信せず待機。local HEAD、`origin/develop3_rendering`、`git ls-remote`はすべて`cb44456d4bbf221f60a894084a7cf6f9cb12a1a5`。Phase 4 parity未確立、原因修正なし、フェーズ未完了。
- **2026-09-29 14:26 UTC:** 完了済み`Investigate Phase 4 Regression`の回答を閉じる前に確認。結論は、Inherited色でgeneric attrib 3 (TexCoord)が残り、generic color attrib 1と`GL_COLOR_ARRAY`が無効になる呼出状態は、NVIDIA固有のattrib-3/`gl_Color`相互作用という歴史資料ベースの仮説に整合するが、現行617.14 driverでの再現もGolden Capture全群への因果も未証明。GL 2.1/GLSL 1.20の一般仕様上のaliasとして断定せず、indices 0–3を従来配列だけにする差分も本番修正にしない。閉じた履歴URLは読み込みエラーとなったためRetryせず、観測事実を含む4ケースの隔離FBO repro設計依頼を新規FruityPrime Chatに送信し、別タブで回答生成を確認。
- **2026-09-29 15:10 UTC:** run `36570852867` (adapter, SHA `cb44456d4bbf221f60a894084a7cf6f9cb12a1a5`) と run `36574766655` (canonical PR, SHA `341d8f0ec3bd5ac4af75c3c0773851b6b45cb7cb`) は両方terminal FAIL。Canonicalは他7 jobs PASS、Windows/MSVC final link FAIL。adapter artifact `C:\Users\Admin\AppData\Local\Temp\FruityPrimeAdapterArtifacts-36570852867`で、direct `Weapons.obj`/`EntityBase.obj` exportsと`MphRead.Native.lib` linker-member definitionsが同一tagで一致し、呼出側の未解決3 symbolsだけが opposite MSVC class-key tagsを要求することを確認: `Weapons::Current`/`WeaponsMP`は定義`VWeaponInfo`・要求`UWeaponInfo`、`EntityBase::HandleMessage`は定義`UMessageInfo`・要求`VMessageInfo`。sourceには`Metadata.hpp`の`class WeaponInfo`に対して`PlayerEntity.hpp`/`PlayerAi.hpp`に`struct WeaponInfo`、`Messaging.hpp`の`struct MessageInfo`に対して`EntityBase.hpp`/`PlayerProcess.hpp`に`class MessageInfo`のforward declarationがある。高確度原因だが、ChatGPT source audit/修正は未完了。branch HEADとremoteは`341d8f0ec3bd5ac4af75c3c0773851b6b45cb7cb`で一致。ユーザー所有のdirty `GL.cpp` は未変更・未commit。GL design chatはvisible historyから再オープンし、本文load中の状態。小型FBOの機構試験とGolden Capture因果を分け、Phase 4 exact-RGB parityは未確立のまま。
