# Fruity Prime Launcher Backdrop OpenGL / Vulkan 描画差
## 根本原因調査と修正指示

調査対象:

- Repository: `Zection6V/Fruity-Prime`
- Branch: `develop3_rendering`
- Source SHA: `84419c601d63f0045cc207af0013267537f6a4de`
- 症状:
  - OpenGLではオープニング/ランチャー背景の「ぐにゃぐにゃ」エフェクトが滑らかに見える
  - Vulkanでは大きなブロック単位で下方向へ移動していくように見える
  - OpenGLとVulkanで同一演出の見え方が一致していない

---

# 1. 結論

## 根本原因

OpenGL/Vulkan間で異なる値を渡していることが主因ではない。

両backendは現在、

- 同じ `launcher-bg.jpg`
- 同じcrop UV
- 同じ4頂点
- 同じ `TexCoord1` / noise coordinate
- 同じ `strength = 0.62`
- 同じ経過時間
- 同じ `BackdropVertexShader`
- 同じ `BackdropFragmentShader`

を使用している。

vertex semantic/locationも、

```text
Position  -> location 0
TexCoord  -> location 3
TexCoord1 -> location 4
```

でOpenGL DesktopとVulkanが一致している。

Vulkan側のpacked vertexも、

```text
position.xyz
texcoord.xyz
texcoord1.xy
```

を正しいoffset/strideで渡している。

したがって、**OpenGLとVulkanで意味が変わる最初の箇所は `BackdropFragmentShader::hash12()` の transcendental `sin()` hash である。**

現在:

```glsl
float hash12(vec2 p)
{
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}
```

この式をcross-backend deterministic random sourceとして使っていることが問題である。

OpenGL DesktopではGLSL sourceをdriverが直接compileする。

Vulkanでは同じsourceを、

```text
Shaders.cpp
    ↓
generate-vulkan-scene-shaders.py
    ↓
GLSL 450
    ↓
glslc --target-env=vulkan1.3 --target-spv=spv1.5 -O0
    ↓
SPIR-V
    ↓
Vulkan driver
```

として実行する。

`sin()`の超越関数結果はbackend/compiler/device間でbit-identicalなrandom hash sourceとして扱えない。

しかもこのshaderは、

```glsl
fract(sin(...) * 43758.5453123)
```

としており、

1. `sin()`の小さな数値差
2. `* 43758.5453123` による差の増幅
3. `fract()` による整数境界での完全な別値化

を行う。

したがって非常に小さいbackend差が、

```text
同じcell
↓
OpenGL: 0.18
Vulkan: 0.83
```

のような全く別のpseudo-random lattice valueへ化ける可能性がある。

これは「同じshader sourceだから同じ絵になる」という前提を破る。

---

# 2. なぜ「ブロックごとに下がる」ように見えるのか

現在のnoiseはvalue noiseである。

```glsl
float value_noise(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);

    float a = hash12(i);
    float b = hash12(i + vec2(1.0, 0.0));
    float c = hash12(i + vec2(0.0, 1.0));
    float d = hash12(i + vec2(1.0, 1.0));

    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}
```

つまり画面を明示的に、

```text
floor(p)
```

で格子cellへ分割している。

さらに時間方向は、

```glsl
float wy = value_noise(vec2(uv.x, uv.y - time * 1.6)) * 4.4;
```

となっている。

したがってnoise fieldのbackend差が大きくなると、

```text
格子cell単位の塊
+
uv.y - time * 1.6
```

という構造がそのまま見え、

> 大きなブロックが順番に下方向へ移動していく

ような見え方になる。

報告されている症状と現在のshader構造は整合する。

---

# 3. 静的調査で除外できたもの

## 3.1 OpenGL/Vulkanで別のエフェクトを実装している

**除外。**

両方とも最終的には `Shaders.cpp` の同じBackdrop shaderを使用する。

OpenGL:

```text
OpenGlLauncherPhoto
    ↓
OpenGlWindowDraw
    ↓
Shaders::BackdropVertexShader
Shaders::BackdropFragmentShader
```

Vulkan:

```text
LauncherPhoto
    ↓
Rhi::WindowUi
    ↓
Vulkan::WindowUi
    ↓
generated backdrop.vert / backdrop.frag
```

Vulkan shaderは `Shaders.cpp` の同じshader bodyから生成される。

---

## 3.2 crop UVの違い

**除外。**

両方とも同じ計算:

```cpp
const double window = static_cast<double>(width) / height;
const double picture = static_cast<double>(photoWidth) / photoHeight;

float u = 1;
float v = 1;

if (window > picture)
    v = static_cast<float>(picture / window);
else
    u = static_cast<float>(window / picture);

const float u0 = (1 - u) / 2;
const float u1 = u0 + u;
const float v0 = (1 - v) / 2;
const float v1 = v0 + v;
```

を使用している。

---

## 3.3 NoiseCoordの値が違う

**除外。**

両方とも4 cornerは、

```text
top-right     (1, 0)
top-left      (0, 0)
bottom-right  (1, 1)
bottom-left   (0, 1)
```

である。

Vulkan側は `WindowQuadVertex::TexCoord1` を32-byte vertexのoffset 24へpackし、

```text
stream 4
stride 32
offset +24
Float2
location 4
```

として読む。

`VertexSemantics.hpp`でもOpenGL Desktop/Vulkanともに`TexCoord1 = location 4`である。

---

## 3.4 strength/timeが違う

**除外。**

OpenGL/Vulkanともに、

```text
Strength = 0.62
```

を使用する。

timeも各backendでsteady-clock由来の開始時刻からsecondsへ変換している。

Vulkan:

```cpp
program.Write("strength", ...);
program.Write("time", ...);
program.Write("view_width", ...);
program.Write("view_height", ...);
```

となっており、ABIにも4値が定義されている。

---

## 3.5 std140 packingの明白な破損

**根拠なし。**

Backdrop blockは4つのfloatであり、

```text
strength
time
view_width
view_height
```

という単純な16-byte blockになる。

`VulkanSceneUniforms`はoffset/size/alignmentをruntime validationしている。

今回の症状を説明するようなpacking mismatchは静的コード上確認できない。

---

## 3.6 VulkanのTexCoord1 vertex layout mismatch

**除外。**

`VulkanCommandList::VariantFor()`はBackdropだけ追加streamを定義している。

```cpp
desc.vertexBuffers = {
    {0, 12},
    {1, 12},
    {2, 16},
    {3, 12}
};

if (program.Id == SceneProgram::Backdrop)
    desc.vertexBuffers.push_back({4, 8});
```

Backdrop attributes:

```cpp
{
    {0, 0, VertexFormat::Float3, 0},
    {3, 3, VertexFormat::Float3, 0},
    {4, 4, VertexFormat::Float2, 0}
};
```

`WindowUi::Quad()`のstream 4も同じoffset/strideを指している。

---

## 3.7 VulkanのY反転そのもの

**主因ではない。**

Vulkan window targetはOpenGL row conventionとして内部描画され、present前の`vkCmdBlitImage()`でsource Yを反転してswapchainへコピーする。

```cpp
region.srcOffsets[0] = {0, extent.height, 0};
region.srcOffsets[1] = {extent.width, 0, 1};
```

写真本体とnoise coordinateは同じquad上で補間されるため、Y conventionだけがnoiseをブロック化する構造ではない。

**今回の修正でTexCoord1だけを上下反転してはいけない。**

それは症状を別方向へ移すだけであり、cross-backend random fieldの不一致を直さない。

---

# 4. 根本修正方針

## `sin()` hashをproduction backdropから除去する

### やってはいけない修正

以下は採用しない。

```text
Vulkanだけtime速度を変える
VulkanだけTexCoord1.yを反転する
Vulkanだけnoise scaleを小さくする
Vulkanだけstrengthを下げる
Vulkanだけ別hash constantを使う
Vulkan shaderへdriver/vendor別補正を入れる
```

これらは全てbackend差を隠すだけで、Metal/D3D12追加時に再発する。

---

# 5. 推奨修正: deterministic noise lattice texture

最も堅牢な修正は、pseudo-random lattice valueをshader内の`sin()`で生成せず、

> **CPUで一度生成した固定noise tableを全backendが同じtextureとしてsampleする**

ことである。

これならOpenGL/Vulkanだけでなく、将来のMetal/D3D12でも同じrandom latticeを使用できる。

---

# 6. Noise textureの仕様

## 6.1 サイズ

推奨:

```text
64 x 64
RGBA8Unorm
```

R channelのみnoiseとして使う。

G/Bも同値にしておけばdebug captureしやすい。

Alphaは255。

64x64で十分である。

現在のbackdrop noise domainは画面内で数十cell以下であり、domain warpを含めても64-cell周期なら通常画面内で露骨なrepeatは出にくい。

---

## 6.2 CPU生成

C++のunsigned integer arithmeticだけを使う。

例:

```cpp
[[nodiscard]] constexpr std::uint32_t BackdropNoiseHash(
    std::uint32_t x,
    std::uint32_t y) noexcept
{
    std::uint32_t h
        = x * 0x9E3779B1u
        + y * 0x85EBCA77u
        + 0xC2B2AE3Du;

    h ^= h >> 16;
    h *= 0x7FEB352Du;
    h ^= h >> 15;
    h *= 0x846CA68Bu;
    h ^= h >> 16;

    return h;
}
```

pixel:

```cpp
const std::uint8_t value
    = static_cast<std::uint8_t>(BackdropNoiseHash(x, y) >> 24);

rgba[index + 0] = value;
rgba[index + 1] = value;
rgba[index + 2] = value;
rgba[index + 3] = 255;
```

C++ unsigned overflowはwell-definedなので、同じsourceから常に同じbyte tableを生成できる。

**`std::sin`, `float`, RNG library, current time, platform random sourceを使用しない。**

---

# 7. 共通RHI resourceにする

推奨新規component例:

```text
src/MphRead.Native/NativeRuntime/Rhi/BackdropNoise.hpp
src/MphRead.Native/NativeRuntime/Rhi/BackdropNoise.cpp
```

概念:

```cpp
struct BackdropNoiseResources
{
    std::unique_ptr<Texture> Texture;
    std::unique_ptr<Sampler> Sampler;
};

[[nodiscard]] BackdropNoiseResources
CreateBackdropNoiseResources(GraphicsDevice& device);
```

内部で、

```text
RGBA8Unorm
64x64
Sampled | TransferDst
Nearest
Repeat
No mipmap
```

を作る。

### Sampler

noise latticeは整数cell値を読むため、

```text
minFilter = Nearest
magFilter = Nearest
addressU = Repeat
addressV = Repeat
minLod = 0
maxLod = 0
```

とする。

**linear filteringは使わない。**

smooth interpolationは現在の`value_noise()`内の`mix()`で行う。

これによりhardware filtering implementationをnoise lattice生成へ混ぜない。

---

# 8. SceneShaderAbi変更

既存のPost groupには、

```text
PostTexture
PostSampler
PostAuxTexture
PostAuxSampler
```

がすでにある。

Backdropのnoiseには既存`PostAuxTexture/PostAuxSampler`を利用できる。

`SceneShaderAbi.def`へ追加:

```text
RHI_SCENE_TEXTURE("backdrop", "noise_tex", PostAuxTexture, PostAuxSampler, 1)
```

新しいlogical binding groupを追加する必要はない。

これにより、

```text
photo      -> unit 0
noise_tex  -> unit 1
```

となる。

---

# 9. BackdropFragmentShader修正

現在のこれを削除する。

```glsl
float hash12(vec2 p)
{
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}
```

追加:

```glsl
uniform sampler2D noise_tex;

const float noise_size = 64.0;

float lattice_noise(vec2 cell)
{
    vec2 wrapped = mod(cell, noise_size);
    vec2 uv = (wrapped + vec2(0.5)) / noise_size;
    return texture2D(noise_tex, uv).r;
}
```

`value_noise()`:

```glsl
float value_noise(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);

    float a = lattice_noise(i);
    float b = lattice_noise(i + vec2(1.0, 0.0));
    float c = lattice_noise(i + vec2(0.0, 1.0));
    float d = lattice_noise(i + vec2(1.0, 1.0));

    return mix(
        mix(a, b, u.x),
        mix(c, d, u.x),
        u.y);
}
```

残りのwarp式はまず変更しない。

```glsl
vec2 cells = vec2(
    clamp(view_width / 6.0, 1.0, 320.0),
    clamp(view_height / 6.0, 1.0, 320.0));

vec2 uv = noisecoord * cells * 0.055;

float wx
    = value_noise(vec2(uv.x + time * 2.0, uv.y)) * 4.4;

float wy
    = value_noise(vec2(uv.x, uv.y - time * 1.6)) * 4.4;

float n
    = value_noise(uv + vec2(wx, wy));

n = n * n * (3.0 - 2.0 * n);
```

これによりエフェクトの構造・速度・cell size・色調は維持しながら、random latticeだけをdeterministicにする。

---

# 10. OpenGL修正

対象:

```text
NativeRuntime/Rhi/OpenGL/OpenGlWindowDraw.cpp
```

`Impl`へ、

```text
BackdropNoiseResources Noise
NoiseLocation
```

を追加する。

`EnsureBackdrop()`で、

1. shader compile/link
2. `noise_tex` uniform location取得
3. common `CreateBackdropNoiseResources(Device)` でnoise resource生成

を行う。

Backdrop draw時:

```text
photo      -> unit 0
noise_tex  -> unit 1
```

をbindする。

既存のborrowed OpenGL photo texture pathは維持する。

noise textureだけRHI-owned resourceとしてよい。

draw終了時はunit 1もbackend state contractに従ってunbind/resetする。

---

# 11. Vulkan修正

対象:

```text
NativeRuntime/Rhi/Vulkan/VulkanSceneInternal.inc
```

`WindowUi::Impl`へcommon noise resourceを所有させる。

`DrawBackdrop()`:

現在:

```cpp
commands.BindSampledTexture(0, &photo, &sampler);
_impl->Quad(strip);
commands.BindSampledTexture(0, nullptr, nullptr);
```

修正概念:

```cpp
commands.BindSampledTexture(0, &photo, &sampler);
commands.BindSampledTexture(
    1,
    _impl->Noise.Texture.get(),
    _impl->Noise.Sampler.get());

_impl->Quad(strip);

commands.BindSampledTexture(1, nullptr, nullptr);
commands.BindSampledTexture(0, nullptr, nullptr);
```

Vulkan側で専用noise生成アルゴリズムを持ってはいけない。

**OpenGL/Vulkanともcommon CPU-generated byte tableを使用する。**

---

# 12. Shader generatorは原則変更不要

`generate-vulkan-scene-shaders.py`は、

```text
uniform sampler2D noise_tex
```

を`SceneShaderAbi.def`から、

```text
sampled image
+
sampler
```

へ変換する。

既存のtexture ABI generationを利用する。

個別にVulkan GLSLへnoise implementationを書かない。

`Shaders.cpp`をsingle source of shader behaviorとして維持する。

---

# 13. 修正前に行う最小A/B診断

静的解析では、OpenGL/Vulkanのinput transportは一致しており、`sin` hashが最初の非deterministic boundaryである。

ただし、報告されている現象の**唯一の原因**であることまで閉じるため、修正前に以下の小さいdiagnosticを実施する。

ゲーム本編を変更する必要はない。

launcher backdropだけでよい。

## Test A: NoiseCoord

一時的にBackdrop fragment outputを、

```glsl
gl_FragColor = vec4(noisecoord, 0.0, 1.0);
```

にする。

OpenGL/Vulkanで同一gradientになることを確認。

違う場合:

```text
vertex layout / row orientation / varying interpolation
```

を再調査する。

現在の静的コードでは一致するはずである。

---

## Test B: UV cell

```glsl
vec2 cells = vec2(
    clamp(view_width / 6.0, 1.0, 320.0),
    clamp(view_height / 6.0, 1.0, 320.0));

vec2 uv = noisecoord * cells * 0.055;

gl_FragColor
    = vec4(fract(uv), 0.0, 1.0);
```

timeは関係させない。

これもOpenGL/Vulkanで一致すること。

---

## Test C: 現行hashを直接可視化

timeを0へ固定。

```glsl
vec2 i = floor(uv);

gl_FragColor
    = vec4(vec3(hash12(i)), 1.0);
```

ここでOpenGL/Vulkanのcell patternが異なれば、

> `sin` hash divergence

が直接証明される。

今回の症状に対する最重要gate。

---

## Test D: deterministic latticeへ交換

CPU-generated noise texture版へ交換し、

```glsl
gl_FragColor
    = vec4(vec3(value_noise(uv)), 1.0);
```

を比較する。

OpenGL/Vulkanのcell structureが揃えば根本修正成立。

その後final backdrop shaderへ戻す。

---

# 14. 診断をproduction hackとして残さない

Test A～Dは、

- dedicated diagnostic shader
- compile-time diagnostic mode
- test-only fragment body

のいずれかにする。

production shaderへ、

```glsl
uniform int debug_mode;
if (debug_mode == ...)
```

のような恒久branchを入れない。

確認後はtest infrastructureのみ残す。

---

# 15. Backdrop parity testを追加する

同じ問題をMetal/D3D12追加時に再発させないため、専用gateを追加する。

推奨:

```text
-backdropparity
```

または既存RHI conformance suite配下。

固定条件:

```text
photo         = fixed fixture
strength      = 0.62
view size     = 1280 x 720
time          = 0.000
time          = 0.250
time          = 0.500
time          = 1.000
```

最低限2 resolution:

```text
1280x720
1920x1080
```

を確認する。

比較対象:

```text
OpenGL
Vulkan
future Metal
future D3D12
```

---

# 16. Parity acceptance criteria

修正完了条件:

1. OpenGL/VulkanでNoiseCoord diagnosticが一致
2. OpenGL/Vulkanでfixed noise latticeが一致
3. final backdropでcell-alignedな大差が消える
4. 時間を進めてもVulkanだけ「ブロックが順番に落ちる」動きにならない
5. resize後も同じnoise scaleになる
6. fullscreen/windowedで方向が変わらない
7. photo cropは変更しない
8. `strength = 0.62`は変更しない
9. current animation speedは変更しない
10. Vulkan validation error = 0
11. OpenGL error = 0
12. backend switch後もnoise resource leak = 0

画像比較はbit-identicalを必須にしなくてもよいが、

> cell topology / animation direction / coarse deformation morphology

は一致させる。

可能ならRGBA8 quantization後の画像差もCI artifactとして保存する。

---

# 17. 修正範囲

主な変更対象:

```text
src/MphRead.Native/Shaders.cpp
src/MphRead.Native/NativeRuntime/Rhi/SceneShaderAbi.def

新規:
src/MphRead.Native/NativeRuntime/Rhi/BackdropNoise.hpp
src/MphRead.Native/NativeRuntime/Rhi/BackdropNoise.cpp

OpenGL:
src/MphRead.Native/NativeRuntime/Rhi/OpenGL/OpenGlWindowDraw.cpp
src/MphRead.Native/NativeRuntime/Rhi/OpenGL/OpenGlWindowDraw.hpp

Vulkan:
src/MphRead.Native/NativeRuntime/Rhi/Vulkan/VulkanSceneInternal.inc

tests/tools:
Backdrop parity diagnostic
```

原則変更不要:

```text
LauncherPhoto.cpp
WindowUi.hpp
VertexSemantics.hpp
Vulkan viewport convention
Vulkan swapchain blit orientation
general Scene rendering
```

---

# 18. 変更してはいけない領域

今回の不具合修正を理由に以下を触らない。

```text
general world vertex layout
main scene shaders
cel shader
disruption shader
swapchain Y flip
global Vulkan viewport convention
OpenGL context state
general texture upload orientation
```

launcher backdrop限定の問題として修正する。

特に、

> Vulkan全体のY軸を反転する

ような修正は絶対にしない。

現在のscene/presentation row conventionは別の既存contractであり、launcher backdropのrandom field問題を直すための場所ではない。

---

# 19. Quick patchを採用する場合

noise texture追加を一度に行いたくない場合、暫定的に`sin`を使わないfloat hashへ交換する方法はある。

例:

```glsl
float hash12(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}
```

これは、

```text
sin
↓
large multiply
↓
fract
```

よりcross-backend divergenceを起こしにくい。

ただし、float contraction / precisionの違いを完全には排除できない。

したがってこれは、

> **診断用または暫定fix**

に留める。

最終形はfixed byte noise textureを推奨する。

---

# 20. 将来のMetal / D3D12

今回の修正はVulkan専用workaroundにしない。

理想:

```text
CPU deterministic noise bytes
        |
        v
RHI Texture + Sampler
   /       |       |       \
OpenGL   Vulkan   Metal   D3D12
```

shader側も同じlogical operation:

```text
fixed lattice lookup
+
manual smooth interpolation
+
domain warp
```

とする。

Metal/D3D12を追加しても、

```text
native sin approximation
compiler optimization
vendor transcendental implementation
```

にrandom fieldを依存させない。

---

# 21. 修正指示まとめ

実装担当は次の順序で進める。

```text
1. Current HEADを固定
   84419c601d63f0045cc207af0013267537f6a4de

2. Backdrop専用A/B diagnosticを追加

3. NoiseCoord parityを確認

4. UV/cell parityを確認

5. current hash12(floor(uv))を可視化
   OpenGL/Vulkan差を証明

6. common deterministic 64x64 RGBA8 noise tableを実装

7. RHI共通noise texture/sampler creatorを追加

8. SceneShaderAbiへbackdrop.noise_texを追加
   PostAuxTexture/PostAuxSampler
   texture unit 1

9. BackdropFragmentShaderからsin hashを削除

10. texture lattice lookup + manual value-noise interpolationへ交換

11. OpenGlWindowDrawでnoise textureをunit 1へbind

12. Vulkan WindowUiでnoise textureをunit 1へbind

13. fixed-time OpenGL/Vulkan capture比較

14. animation capture比較

15. resize/fullscreen/backend-switch確認

16. validation/resource-lifetime確認

17. diagnostic hackをproduction shaderから除去

18. parity regression testを残す
```

---

# 22. 最終判断

今回の描画差について、静的コード上は、

```text
photo
crop
vertex positions
NoiseCoord
strength
time
shader source
vertex semantic locations
```

までOpenGL/Vulkanで揃っている。

その先で最初にcross-backend deterministicではなくなるのが、

```glsl
fract(
    sin(dot(...))
    * 43758.5453123
)
```

である。

この式はvisual noiseにはよく使われるが、

> **複数graphics APIで同じrandom fieldを保証するhashとしては不適切**

である。

今回の「Vulkanだけ格子状の塊が下へ流れる」という症状も、

```text
floor-based value-noise cells
+
backendごとに異なり得るsin hash
+
time-dependent vertical warp
```

で説明できる。

したがって、修正の中心はVulkanの座標補正ではなく、

> **Backdropのrandom latticeをshader transcendental mathから切り離し、全backend共通のdeterministic dataにすること**

とする。

この修正なら現在のOpenGL/Vulkan差を閉じるだけでなく、将来のMetal/D3D12追加時にも同じ問題を再発させない。
