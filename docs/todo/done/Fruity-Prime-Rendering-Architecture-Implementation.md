# レンダリングアーキテクチャ対応記録

更新日: 2026-10-03

対象: [比較レビュー](Fruity-Prime-Rendering-Architecture-Review.md)。比較時の固定コミットは変更しない。
今回は Windows の OpenGL / Vulkan を実装・検証対象にする。ユーザー確認により、
Metal / D3D12 の実装（Phase F / G）と macOS 実機検証は将来の対応に残す。
共通契約に将来の API 名があることは、そのバックエンドが実装済みという意味ではない。

## 進捗

Windows の OpenGL / Vulkan について、R1〜R20 と Phase A〜E / H の対応を **完了** とする（2026-10-03）。
範囲外として残すもの:
- Metal / D3D12（Phase F / G）: 将来の対応。
- Android / macOS の実機検証。
- 実 driver の reset / OOM の故障注入: hardware / driver 側の手段がこの環境にない。代わりに合成 fault で検査した。

| 項目 | 現在の実装・残作業 |
|---|---|
| R1: 選択と Provider | `SceneBackendKind` を `GraphicsBackend` へ統合。OpenGL / Vulkan Provider と Session を登録し、shader・mesh・transient geometry・UI・表示生成を委譲。本番の scene / transient geometry / launcher UI は R6 / R15 で共通の Buffer / CommandList 経路へ統一済み |
| R2: shader ABI | `SceneShaderAbi.def` を本番5 programs / 55 constants / 7 textures の共通定義にし、OpenGL texture units と Vulkan 生成 manifest / 実 SPIR-V / native layouts を接続。実バイナリの type・offset・stride・set/binding・vertex location を build 時に検証。std140 packing は Vulkan 内部。単体9/9と固定14画像の一致を確認 |
| R3: PipelineLayout | 複数グループの記述を値で所有。両 backend の generic pipeline / binding を共通 GPU fixture で検証。cache key に全グループを含め、本番 Vulkan scene も Frame / Material / Draw / Post の4 layouts を使う。16形式の作成・コピー、packed depth / stencil、subresource 上限は R19 の conformance で検査済み |
| R4 / R5: submission と resize | 共通 `SubmissionSerial` / `SubmissionProgress` に Vulkan timeline と OpenGL GLsync scheduler を接続。frame number を retirement の証拠にしない。Vulkan は Buffer / Sampler / Image / ImageView / Pipeline を実際の送信完了で破棄し、resize は先に画像・全ビューを確保して旧世代を retire。OpenGL はフレーム外の command / resource release も実際の stream marker で覆う。GL の release marker は集約済み（Alinos 40 cycles で 69,640 → 2,440）。ownership / format は R19 で検査済み |
| R6 / R7: OpenGL command / sampler | Buffer / vertex・index binding / Draw / DrawIndexed / GPU Copy / BindingSet と独立 sampler・value cache を実装。Windows scene / transient geometry を同じ Buffer / CommandList / VAO 経由へ接続。共通 GPU fixture と旧新7画像の一致を確認。logical recording intervalと明示的transition / copyの状態契約を統一。OpenGL の画像は単一 level 2D で、`Capabilities` の mip / layer / 3D 上限として明示し作成時に拒否。packed depth / stencil のコピーは `supportsPackedDepthStencilTransfer`（Vulkan のみ）。usage / format / state の検証は R19 で検査済み。OpenGL で mip / array / 3D を実装するのは将来 |
| R8: Session 寿命 | Vulkan の意図的に解放しない `VulkanScene` を削除。切替で scene / UI → commands → swapchain → device / context → window の順に解放する。OpenGL device は Session が単独所有。Vulkan も device 終了時に全 native owner を閉じ、shared state を context 非依存の CPU descriptor にする。両 backend で未送信 copy / 旧 wrapper を残す8回の shutdown / recreate を検査。swapchain は Session 終了前に解放する caller 契約を維持。device loss / admission failure の teardown は R17 の合成 fault で検査済み |
| R9: presentation | request と実際の mode / capabilities を分離。typed acquire / present status を実装し、frame loop で利用。最小化・明示的 close request は一時停止、API の device / surface loss は別分類。OpenGL の generic conformance coverage は R19 で拡張する |
| R10: Vulkan 責務分離 | `VulkanSynchronization` が resource stateからnative stage / access / layoutとsampled descriptor layoutへのpure mapping、`VulkanFrameScheduler` が queue submit / completion、`VulkanFrameSlots` が frameのfence / descriptor世代 / transient再利用とframe番号、`VulkanDescriptorAllocator` が slot ごとの pools、`VulkanUploadArena` が mapped pages / suballocation / flush / completion 後 reset / close を所有。`VulkanTransferScratch` が GPU-onlyのtransfer pagesとcompletion後の再利用、`VulkanRgbTransfer` が logical RGB3 / native RGBA4のaddress-only copy planを所有。`VulkanMemory` が VMA と buffer / image の admission / allocation を所有。`VulkanResources` が native buffer / image / view / sampler の検査・生成・生成失敗時のrollbackを所有し、通常リソースとupload pageが共用する。pipeline library は R11 の専用 owner。`VulkanFeatureProbe` が instance / device の facts・必須条件・任意機能・queue選択・scoreを生成し、logical device生成と分離。`VulkanCommandSlots` が command listの2枠のnative pool / buffer / fence / allocators、再利用の調停を所有。logical recording / draw-stateとqueue schedulerは別の責務として維持。最終監査を実施し、command list と公開 wrapper を device 本体から分けた（3,652 → 2,165 行）。command list の別 TU 化は将来の backend 追加時 |
| R12: memory budget | API に依存しない snapshot / request / decision / telemetry と pure admission を実装。Vulkan は VMA / optional EXT live budget、OpenGL は optional NVX counters。未知・推定・driver 情報を区別し、buffer / texture / resize / thumbnail / interop target の確保前に判定。合成 heap / UMA / limits / overflow と両 backend の実 GPU 拒否・旧画像保持を検査。実 driver OOM、eviction、全 GPU の容量保証は含めない |
| R13: upload | Vulkan の scene uniforms / transient geometry / texture と GPU-only buffer upload を slot ごとの persistent mapped arena へ統一。描画外の writes は専用 transfer stream で batch し、consumer / frame / readback / release の順序境界で submit。static mesh は GPU-only destination。CPU fake と実 GPU の再利用・コピー順・overflow・切替を検査。将来の API の機構は追加しない |
| R11: native pipeline library | 既存 semantic cache を維持し、専用 `VulkanPipelineCache` を全 RHI native graphics pipeline 生成へ接続。identity / framing / checksum / size gate、atomic disk replacement、driver rejection / native cache 不可時の fallback と deterministic close を実装。CPU fault dispatch と実 GPU の cold / warm・破損・保存失敗を検証。cold 9.2〜11.9 ms / warm 0.84〜0.88 ms（本番 14 pipelines、driver cache 無効、RTX 5070 Ti）。OpenGL は既存 linked-program cache、Metal / D3D12 は将来対応 |
| R14: eligibility / admission | Vulkan passive probe は instance / physical device の確認で止まり、logical device / queue を作らない。専用 probe の structured findings と pure policy をCPUで検査し、実GPUでもpassive後のlogical device / queuesが0であることを確認。incoming Session の device / swapchain 生成が active admission。合成 fault と実 session の途中失敗・復旧はR17の記録を参照。長時間 stress は 98be22e1 の100試合 / 200切替で実施。実 driver の reset / OOM は、hardware / driver 側の故障注入がこの環境にないため未実施 |
| R15: GL vertex interface | Windows scene / transient / launcher UI を explicit input と RHI Buffer / CommandList / VAO へ統一。desktop wrapper の conventional array / current-value mirror を除去。本番 GLSL declarations と共通 ABI、実 SPIR-V vertex location の一致を検査。GPU composite / 旧新14画像の一致を確認。既存の backend 間 caption 差は画像 gate に残る |
| R16: readback | 共通 ticket / immutable CPU output lease / staging+output quota と両 GPU の非同期 copy を実装。本番 screenshot / recording を接続。source の即時 resize / release、shutdown 後の CPU output、件数・byte 制限、RGB/RGBA packing と alpha を検査。同期互換 API は維持。全16形式と Vulkan の mip / layer を GPU コピー + 非同期 buffer readback で検査済み。recording の長時間は 98be22e1 の700 PNG |
| R17: error | native backend / kind / code / message を acquire → present → scene facade と起動例外で保持。GL context loss と Vulkan unsupported / loss / OOM を分類。switch を部分再構築まで含む transaction にし、元の backend への復旧と両方失敗した場合を合成 fault / 実 GPU session で検査。ownership / failure は R19 と合成 fault で検査済み。実 driver の reset / OOM は、故障注入の手段がこの環境にないため未実施 |
| R18: 診断 | 共通 debug scope / marker / 一度限りの timestamp query を両 backend に接続。native set は解放待ちを含め8個まで、結果確認は wait / submit を挿入しない。共通 GPU transfer、容量、shutdown 後の取消を検査。HUD に依存しない FPS / CPU frame time / optional GPU scene time の CSV を本番 window に接続 |
| R19 / Phase H | 同じ fixture で両 backend を検証する `-rhiconformance` を追加。lifetime gate に sampler / VAO を追加。切替の直前直後で simulation / bomb / particle / texture binding の不変性を検査し、死亡による通常の爆発と区別する。35件の foreign frontend / stale view 拒否、既存 descriptor の texture 解放後の再利用、終了済み command の incoming session への操作を検査。通常の session teardown は両方8回検査。明示的transition / copyと初期stateの拒否55ケース、declared usage / attachment format適合性、拒否後のGPUコピー、resize / upload / storage stateを共通fixtureで確認。combined readのsampled draw、再bindなしのlayout変更、non-readable stateでのdraw拒否も検査。RGB8のGPUコピーを3-byte packingで統一し、pitch / subrectangle / padding / Vulkan mip・array・3D / 64回のscratch再利用を確認。98be22e1で100試合 / 200切替 / 両backend各100回resize / 700 PNG保存がPASS。全16形式・packed depth / stencil・subresource 上限・非同期 readback、presentation の共通 conformance（`-presentconformance`）、最終画像 gate（golden / renderprobe 4 map / HUD を両 backend、C# 版と比較）を実施。C# 版との1件の差は描画ではなく移植のゲームロジックで、別 task とした |
| R20: optional pacing | 現行のframe capはplatform loop、60 Hz simulationは`FrameTiming`、native presentation modeはswapchainに分離。core RHIにvendor extensionのdispatch / policyはないことを監査。将来vendor extensionを追加するときはSessionのoptional controllerへ隔離する。新しいvendor featureは今回追加しない |

## 最初の基盤変更

- `SceneBackend.hpp` は OpenGL / Vulkan 固有 shader header を include しない。
- `BackendSession.hpp` は API の native 型を持たない。実装は各バックエンドの Provider 内に置く。
- `PipelineLayout` は binding layout の記述をコピーして保持する。入力 layout の一時オブジェクトや pointer identity が cache key を支配しない。
- Vulkan generic pipeline の診断で4グループを bind し、範囲外グループを拒否する。
- `BackendError` は backend・分類・native code を保持する。OOM / 未分類エラーを一時的な表示不可として隠さない。
- close request は drawable の破棄ではない。シェルの切替操作が close request を使うため、この差を typed presentation で扱う。
- Windows の切替回帰に含まれる着弾エフェクト・新旧 Lockjaw bomb・texture source の寿命チェックは維持する。

## 検証の記録

MSVC Release build、CTest 5/5、`tools/check-phase5-shader-interface.py` が成功。
GPU: NVIDIA GeForce RTX 5070 Ti / driver 617.14。Khronos validation 有効。

| 実動作検証 | 結果・証拠 |
|---|---|
| `-vulkanresourcecheck` | exit 0、live=0、validation errors=0。4グループの generic pipeline / binding 診断を含む。ログ `C:/tmp/gp/architecture-vulkanresourcecheck.log` |
| `-vulkanpresentcheck` | exit 0、resize / fullscreen / minimize / restore / requested-resolved mode / typed acquire-present / shutdown、errors=0。ログ `C:/tmp/gp/architecture-vulkanpresentcheck.log` |
| `-vulkanpresentfallbackcheck` | exit 0、errors=0、fallback-retired-releases=6。ログ `C:/tmp/gp/architecture-vulkanpresentfallbackcheck.log` |

基盤変更後の試合内切替は OpenGL 開始・Vulkan 開始とも exit 0。
各プロセスで front screen の切替1回と、同じ Alinos Perch の試合で Settings から3回の切替・Resume を通した。
全6回で scene / window geometry / visibility を維持し、simulation frame が進み、
effect definitions は105/105、新しい impact / bomb と既存 bomb は各2 particles。
texture-only source は切替中に保持され、scene 終了時に解放された。
Khronos validation は有効で、両ログに `VUID` / `Validation Error` / failed-step はない。

最終ログ・画像は `C:/tmp/gp/architecture-foundation-20261002-003241/`。
各開始バックエンド7枚、合計14枚。両方の `switch-5-match.png` を目視し、
黄色の impact と青い Lockjaw core の描画を確認した。
これは動的な試合の切替回帰であり、固定 frame の Golden Capture 全体の pixel parity の証拠とは区別する。
途中で検出した close request の誤分類を修正してから再実行した最終結果である。
本番の切替テスト手順は [既存の再実行手順](old/Fruity-Prime-CPP-Renderer-Hot-Switch-Fix-2026-10-01.md#repeating-the-effect-regression-on-windows) にある。
Android は今回ビルド・実動作検証していない。enum 統合に必要な参照の更新だけを含む。
remote CI と、実際の device loss / surface loss / OOM 故障注入は未実行。

## Vulkan の submission retirement / resize

`SubmissionSerial` は simulation / presentation frame や command slot と独立した値。
native submit が成功した後だけ進め、timeline の実際の完了値で release callback を回収する。
descriptor frame marker・generic/scene command list・binding 診断の送信を scheduler に接続した。
swapchain の blit と Skia の外部描画も、同じ graphics queue 上の後続 marker で対象画像の寿命を覆う。
present queue の swapchain / present semaphore retirement は既存の別契約を維持する。
marker は `ALL_COMMANDS` の signal scope を使う。
仕様確認: [vkQueueSubmit2 の synchronization scope](https://docs.vulkan.org/refpages/latest/refpages/source/vkQueueSubmit2.html)。

resource destructor は recorded use を submit して logical binding を忘れ、native handle と
VMA allocation だけを retirement queue に渡す。callback が device state を所有しないので循環参照を作らない。
通常の Buffer / Sampler / Texture / View / Pipeline の破棄で `WaitScene` / device idle を呼ばない。
command ring slot の再利用に必要な fence 待機、明示 `WaitIdle`、session teardown は別の境界として残る。
同期 readback / upload の改善は R13 / R16 の残作業であり、今回除去済みと扱わない。

texture resize は新しい画像・既存 public view に対応する新しい native view・sampled view の
確保が全部成功してから交換する。public Texture / TextureView / TextureHandle は変えない。
すでに submit した descriptor / rendering は旧世代を完了まで保持し、後続 descriptor は新世代を見る。
現在の rendering target が保持する native view も交換時に更新する。
確保失敗では元の画像・サイズ・ビューが残る。

再実行手順（repo root の PowerShell、MSVC Release build 済み、game files / paths.txt 配置済み）:

```powershell
Push-Location tools/build/out/msvc-Release
try {
    & .\FruityPrime.exe -vulkanresourcecheck -noupdate
    if ($LASTEXITCODE -ne 0) { throw 'resource regression failed' }
    foreach ($backend in @('opengl', 'vulkan')) {
        & .\FruityPrime.exe -gpulifetime 'AD2 ALINOS PERCH' -cycles 40 -frames 3 -rhi $backend -vkvalidation -noupdate
        if ($LASTEXITCODE -ne 0) { throw "$backend lifetime regression failed" }
    }
} finally { Pop-Location }
```

`-vulkanresourcecheck` は 64 回の clear / descriptor bind / resize / release を、
clear の完了を待たずに通す。ビューの object identity、native 世代の交換、範囲外サイズでの失敗後の
元画像保持、queue への retirement、通常 churn の `DeviceWideWaits` 不変を検査する。
最後の明示 `WaitIdle` 後に resource baseline・`Retired=0`・`Completed=Submitted` を要求する。
CPU 単体テストでは failed submit で番号を消費しないこと、古い completion が値を戻さないこと、
未来の completion / submission gap の拒否、retire 順が逆でも last use で回収することを検査する。

今回の結果:

- MSVC Release build / CTest 5/5 / shader interface audit 成功。
- `C:/tmp/gp/architecture-retirement-vulkanresourcecheck.log`: 64 cycles PASS、churn device-wide waits=0、retired=0、validation errors=0。
- `C:/tmp/gp/architecture-retirement-vulkanpresentcheck.log`: presentation PASS、validation errors=0。
- `C:/tmp/gp/architecture-retirement-vulkanpresentfallbackcheck.log`: PASS、fallback-retired-releases=6、errors=0。
- `C:/tmp/gp/architecture-retirement-20261002-010404/`: OpenGL / Vulkan 開始の切替各3回 PASS。全6回 definitions=105/105、impact / new bomb / existing bomb 各2 particles。両方の最終画像で黄色 impact と青い bomb core を目視。game source は scene 終了時に解放。
- `C:/tmp/gp/architecture-retirement-lifetime40-opengl.log`: 40/40 PASS、毎回 resource / retired=0、private memory の測定増分 0.238 MB/回。
- `C:/tmp/gp/architecture-retirement-lifetime40-vulkan.log`: 40/40 PASS、毎回 resource / retired=0、private memory の測定増分 0.162 MB/回。

短い OpenGL 12 回測定は resource / retired count はすべて0だったが、private memory peak が
208→242 MB（5.67 MB/回）となり memory gate が FAIL。
`C:/tmp/gp/architecture-retirement-lifetime-opengl.log` に保存。
同じ閾値・同じ実装の40回測定では PASS したが、短い測定の失敗を削除したり、
40回の結果を無制限の長時間メモリ保証として扱ったりしない。
OpenGL submission 移行・非同期 readback・責務分離・Phase H の拡張は引き続き残る。

## OpenGL 共通コマンドと scene geometry の接続

Windows のメッシュと transient geometry は API の buffer 名を保持しない。
Provider が受け取った `GraphicsDevice` / `CommandList` を使い、RHI `Buffer` の所有と
`SetVertexBuffer` / `SetIndexBuffer` / `DrawIndexed` へ接続した。
scene shader / pass state を維持する backend 内の adapter が頂点レイアウトを明示する。
通常 generic draw は pipeline が必須。この adapter は GPU rebuild 中に先に bind された
scene program も扱う。完全な本番 shader ABI 統一が済んだという意味ではない。

- quads / strips / fans / line loop の index helper を両 backend で共有。
  単体テストは交互の winding、fan の先頭、loop の閉じ方、不完全な末尾を検査する。
- static / inherited / mixed の色・法線と display-list terminal state を保持。
  transient stream は容量が足りる間は同じ2つの native buffer 名を使う。
  全範囲の `WriteBuffer` は storage を orphan し、部分更新は残りの内容を保持する。
- VAO cache は vertex layout / native buffer / offset / index buffer で区別し、buffer retirement で無効化。
- sampler は texture parameter の書換えではなく native sampler object を独立して bind。
  同じ description の native object を weak cache で共有し、最後の使用者で retire する。
- GPU buffer copy と PBO image transfer は行 pitch の余白も扱う。
  新しい CPU texture backup は作らない。モデルの解読・mixed attribute の生成・upload 元は CPU 側にある。
- Android geometry は従来の GLES draw wrapper を内部ファイルに残す。
  今回 Android は build / 実機検証していない。共通 device の変更を含むので無変更とは主張しない。

### 共通 GPU fixture の再実行

repo root の PowerShell、MSVC Release build 済み。この検査自体には game files は不要。

```powershell
Push-Location tools/build/out/msvc-Release
try {
    & .\FruityPrime.exe -rhiconformance -noupdate
    if ($LASTEXITCODE -ne 0) { throw 'RHI conformance failed' }
} finally { Pop-Location }
```

今回の MSVC build は **OpenGL PASS と Vulkan PASS の両行**を要求する。
Vulkan は Khronos validation を有効にし、shutdown を含め errors=0 を検査する。
同じ fixture / 同じ論理 shader ABI を使用し、GLSL / SPIR-V の生成だけが backend 別。

非整列 offset の buffer GPU copy、余白のある row pitch の image 往復、4グループの layout
（3グループを実際に使用・最後は空）、UBO / sampled image / sampler、宣言順に依存しない
logical binding、非 index draw と `firstIndex=1` の index draw、範囲外 group の拒否を検査する。
同じ赤青2 texelの画像を **1回の draw 内で2つの unit** に bind し、nearest の青と
linear の紫が同時に出ることを readback する。次の draw で2つを交換する。
texture parameter の共有によって両方の sampling が同じになる実装を検出できる。
release / `TrimCaches` / `WaitIdle` 後は `LiveObjects=0` / `Retired=0` を要求する。
通常描画が readback / idle を必要とするという意味ではない。

結果: `C:/tmp/gp/architecture-glgeometry-final-rhiconformance.log`、両 backend PASS。
MSVC Release build / CTest 5/5 / shader audit 18 sources も成功。
`-vulkanresourcecheck` / `-vulkanpresentcheck` / `-vulkanpresentfallbackcheck` は PASS、errors=0、
fallback-retired-releases=6。ログは `C:/tmp/gp/architecture-glgeometry-<check>.log`。

### scene の画像と lifetime の回帰

同じ common device 実装で旧 geometry と新 geometry を一時的に切り替え、
`-goldencapture all` を同じ inputs / harness / 1600x900 で実行した。
transparent-object / decal / particle / trail / hud / fade / whiteout-disruption の7ケースで
**decoded RGB の差は0 bytes**。geometry 移行を切り分ける比較であり、
`a29ceb` 全体との cross-revision 比較や、OpenGL / Vulkan 全画素一致の証拠ではない。
旧画像: `C:/tmp/gp/architecture-legacy-geometry-golden-opengl/`。
新画像: `C:/tmp/gp/architecture-glgeometry-final-golden-opengl/`。

両 backend の7ケースはそれぞれ capture gate を通った。
backend 間には各画像で303 pixels / 909 bytes の差があり、同じ画面下部の文字範囲に集中する。
旧新 OpenGL geometry が完全一致するため、geometry 移行の新しい差とは扱わない。
完全解消は R15 / Phase E の画像 gate に残す。

既述の40回 lifetime 手順を再実行し、Sampler / VAO も release=0 の対象にした。
毎回すべての live object / retired count が0。private memory の測定増分は
OpenGL 0.145 MB/回、Vulkan 0.303 MB/回、両方40/40 PASS。
ログ: `C:/tmp/gp/architecture-glgeometry-final-lifetime40-<backend>.log`。
長時間・他 GPU・全 format の保証とは区別する。

### effect fixture と動く bot の切替を分ける

既存の `FRUITY_SWITCHCHECK=1` は7体の bot も動く。
新旧ボムの粒子数を固定して比較する場合は
`FRUITY_SWITCHCHECK_HOLD_ACTORS=1` で非 main の7体の controls を hold する。
8体の actor、scene、physics / simulation は維持する。通常のユーザー操作には適用されない。
game files / paths.txt を配置済みの build で、設定と環境変数を保存・復元して両方向を実行する手順:

```powershell
$captureRoot = 'C:/tmp/gp/rhi-switch-' + (Get-Date -Format yyyyMMdd-HHmmss)
New-Item -ItemType Directory -Path $captureRoot -Force | Out-Null
$savedSwitch = $env:FRUITY_SWITCHCHECK
$savedRoom = $env:FRUITY_SHOT_ROOM
$savedHold = $env:FRUITY_SWITCHCHECK_HOLD_ACTORS
Push-Location tools/build/out/msvc-Release
$prefsPath = Join-Path $PWD 'launcher.txt'
$hadPrefs = Test-Path -LiteralPath $prefsPath
$prefsBytes = if ($hadPrefs) { [IO.File]::ReadAllBytes($prefsPath) } else { $null }
try {
    $env:FRUITY_SWITCHCHECK = '1'
    $env:FRUITY_SHOT_ROOM = 'AD2 ALINOS PERCH'
    $env:FRUITY_SWITCHCHECK_HOLD_ACTORS = '1'
    foreach ($backend in @('opengl', 'vulkan')) {
        $shots = Join-Path $captureRoot $backend
        'q' | & .\FruityPrime.exe -shellshot $shots -rhi $backend -vkvalidation -fpscap 60 -noupdate *> "$captureRoot-$backend.log"
        if ($LASTEXITCODE -ne 0) { throw "$backend switch fixture failed" }
    }
} finally {
    if ($hadPrefs) { [IO.File]::WriteAllBytes($prefsPath, $prefsBytes) }
    elseif (Test-Path -LiteralPath $prefsPath) { Remove-Item -LiteralPath $prefsPath }
    $env:FRUITY_SWITCHCHECK = $savedSwitch
    $env:FRUITY_SHOT_ROOM = $savedRoom
    $env:FRUITY_SWITCHCHECK_HOLD_ACTORS = $savedHold
    Pop-Location
}
```

`C:/tmp/gp/architecture-glgeometry-held-20261002-074848/`:
両開始 backend とも exit 0、front 切替1回、同じ Alinos Perch で Settings / Apply / Resume の切替3回。
全6回で simulation が進み、definitions=105/105、impact / new bomb / existing bomb は各2 particles、
source は scene 終了時に解放。両最終画像で黄色 impact / 青い bomb core を目視。

動く bot の検査は置き換えない。今回の通常 mode は OpenGL 開始で全3回 PASS。
Vulkan 開始は最終の bomb が0になった試行と、診断付きで全3回 PASS した2試行の両方がある。
`architecture-glgeometry-final-20261002-074551/vulkan.log` などの失敗を残す。
BombEntity の一時的な Destroy 診断は元に戻した。
通常の命中・爆発も起きる code path があるため、現時点で0 particles の原因を確定したり、
動く bot の stress が全試行 PASS と主張したりしない。Phase H で actor / fixture 寿命の観測を拡張する。

## Desktop OpenGL の conventional vertex input 除去（Phase E）

scene に続き、launcher overlay と背景写真を `OpenGlWindowDraw` の
explicit shader / RHI Buffer / Pipeline / CommandList / VAO 経由へ移した。
Skia の native texture は同じ GL context で1回の draw の間だけ借りる。
既存の GPU surface をそのまま sample し、UI の毎フレームの CPU readback / texture copy は追加しない。
背景の移動 shader が利用できない場合の静止画 fallback は維持する。

Desktop `GL.cpp` の conventional array enable / pointer mirror と
native `glColor` / `glNormal` / `glTexCoord` による current-value mirror を削除。
Desktop の頂点位置を Position=0 / Normal=1 / Color=2 / TexCoord=3 / TexCoord1=4 にそろえ、
Vulkan と同じ明示入力にした。array draw 後の未定義な native current value を読まず、
scene が明示した色・法線・UV の値を context device が保持する。
これは scalar の描画状態であり、texture の画素を CPU に複製する仕組みではない。
Android の頂点位置と既存 UI 実装は変更せず、専用 include に保持する。
Android のビルド・実動作は今回未実施。

静的 audit は desktop conventional input の呼び出しと desktop / Vulkan の頂点位置不一致も拒否する。
GL 2.1 向け thumbnail 診断は explicit GLSL 1.20 / generic input を使う。
新しい composite 診断は通常の `-thumbnailwindowcheck` に含め、GL 2.1 専用 mode では実行しない。
GL 2.1 環境そのものは今回検証していない。

### 検証

- MSVC Release build、CTest 5/5、shader interface audit（22 source）が成功。
- `-rhiconformance`: OpenGL / Vulkan とも PASS、release=0、Vulkan validation errors=0。
  ログ `C:/tmp/gp/architecture-phasee-final-rhiconformance.log`。
- `-thumbnailwindowcheck`: explicit shader の色 readback と、実際の window composite を検証。
  半透明赤 / 緑の2行の texture を青背景へ合成し、premultiplied / opaque の色と上下の向きを確認。
  explicit inherited color の維持、buffer / shader / program / sampler / VAO / texture の release=0、
  native error=0。ログ `C:/tmp/gp/architecture-phasee-composite-check.log`。
- Golden Capture の7 candidate を両 backend で撮影し、各 backend の変更前画像と比較。
  OpenGL は `architecture-glgeometry-final-golden-opengl`、Vulkan は
  `architecture-glgeometry-golden-vulkan` を比較元にした。
  新画像は `C:/tmp/gp/architecture-phasee-final-golden-{opengl,vulkan}/`。
  全14画像で decoded RGB の相違 byte=0。比較記録
  `C:/tmp/gp/architecture-phasee-golden-comparison.txt`。
  同一 harness の変更前後の比較であり、両 backend 間の完全一致を新たに主張するものではない。
- `C:/tmp/gp/architecture-phasee-held-20261002-082118/`:
  7体の非 main controls を hold した fixture は両開始 backend で成功。
- `C:/tmp/gp/architecture-phasee-bots-20261002-082625/`:
  7体の bot が動く通常 fixture も、今回は両開始 backend で成功。
  各 process で front 1回、同じ Alinos Perch で Settings / Apply / Resume の切替3回。
  全6回で同じ scene / window geometry / visibility を維持して simulation が進み、
  effect definitions=105/105、impact / new bomb / existing bomb は各2 particles。
  texture-only source は切替中に保持され、scene 終了時に解放。
  両ログに VUID / Validation Error / failed-step はない。
  held fixture の両最終画像で黄色 impact と青い Lockjaw core を目視。
  front 画像で UI と背景写真も確認。
  以前の bot fixture の失敗の原因が今回確定したという記録ではなく、Phase H の観測は継続する。

再実行は上記の切替手順を使う。動く bot の通常 mode では
`FRUITY_SWITCHCHECK_HOLD_ACTORS` を未設定にする。
composite / core GPU / Golden Capture は game binary のディレクトリから実行:

```powershell
Push-Location tools/build/out/msvc-Release
try {
    'q' | & .\FruityPrime.exe -thumbnailwindowcheck -noupdate
    if ($LASTEXITCODE -ne 0) { throw 'Window composite check failed' }
    'q' | & .\FruityPrime.exe -rhiconformance -noupdate
    if ($LASTEXITCODE -ne 0) { throw 'RHI conformance failed' }
    foreach ($backend in @('opengl', 'vulkan')) {
        'q' | & .\FruityPrime.exe -goldencapture all -goldendir "C:/tmp/gp/repeat-phasee-$backend" -rhi $backend -vkvalidation -noupdate
        if ($LASTEXITCODE -ne 0) { throw "$backend Golden Capture failed" }
    }
} finally { Pop-Location }
```

この Phase E 時点では、R2 の本番 binding / generated manifest、R4 の OpenGL submission 契約、
R8 の完全な session ownership、R10～R13、R16 / R18 と Phase H の残項目は引き続き対応する。
Metal / D3D12（Phase F / G）と macOS 実機検証は今回の完了条件に含めない。

## OpenGL の実際の submission completion（R4）

`OpenGlFrameScheduler` を独立させ、GL context の stream marker を共通
`SubmissionSerial` に対応させた。frame slot は再利用の順序だけを扱い、
`RetirementQueue` は typed submission token のみを受け取る。
`CommandList::End` と frame 終了は marker を挿入して flush する。
resource release は、それ以前の RHI / native / Skia の仕事を覆う marker を挿入して retire する。
frame を開かずに upload / copy / release した場合も同じ契約を使う。

fence の作成に失敗した場合は番号を消費せず、native error を `BackendError` に保持する。
zero-time poll の timeout は完了ではない。slot の blocking wait は対象 marker だけを待ち、
timeout で device 全体の `Finish` に切り替えない。WAIT_FAILED も完了として扱わない。
通常の resource release は GPU を待たず、明示 `WaitIdle` / device teardown は待機して解放する。
GL 2.1 診断用 context で ARB_sync が使えない場合は、実際の `Finish` による同期 fallback を残す。
sync 対応 context に必要な entry point がない場合は Unsupported として報告する。
この fallback の CPU 契約は検査したが、GL 2.1 実機は未検証。
仕様確認: [glFenceSync](https://registry.khronos.org/OpenGL-Refpages/gl4/html/glFenceSync.xhtml)、
[glClientWaitSync](https://registry.khronos.org/OpenGL-Refpages/gl4/html/glClientWaitSync.xhtml)。

現段階では native / Skia の全使用箇所の通知に依存しないよう、resource ごとに保守的な
marker を挿入する。40 cycles の OpenGL では合計69,640 marker になった。
marker の集約や upload / readback の暗黙 driver wait は今後の改善対象であり、
今回の変更を速度向上や完全非同期化の証拠にしない。
`HostWaits` は明示的な scheduler wait の計数で、driver 内部の upload / map の待機時間は含まない。

### 検証と再実行

- MSVC Release build、CTest 5/5、shader interface audit（22 sources）成功。
  CPU dispatch fixture は、未完了 marker の保持、失敗で番号を消費しないこと、
  completed prefix のみ回収、timeout の再試行、WAIT_FAILED 時の保持、明示 idle を検査する。
- `C:/tmp/gp/architecture-glsubmission-final-rhiconformance.log`:
  OpenGL / Vulkan とも PASS。追加した16回のフレーム外 copy / readback は内容一致、
  frame number 不変のまま submission が進み、通常 release の明示 wait は増えない。
  最後の明示 idle で `Completed=Submitted`、live / retired=0。Vulkan validation errors=0。
- `C:/tmp/gp/architecture-glsubmission-final-thumbnail.log`:
  実際の window composite / 色 / 上下方向 / inherited color / release=0 が PASS。
- `C:/tmp/gp/architecture-glsubmission-lifetime40-{opengl,vulkan}.log`:
  Alinos Perch の読み込み・描画・解放が両方40/40 PASS。毎回、全 resource / retired=0、
  `Completed=Submitted`。各 cycle の最後の2 frame の明示 host wait=0。
  最後は OpenGL 69,640/69,640、Vulkan 25,520/25,520 submissions。
  private memory の測定増分はそれぞれ0.0715 / 0.1928 MB/cycle。
  これは CPU process private memory であり、VRAM 使用量や長時間の上限を保証する測定ではない。
- Golden Capture の7候補を両 backend で再撮影。
  `architecture-phasee-final-golden-{opengl,vulkan}` と
  `architecture-glsubmission-golden-{opengl,vulkan}` の全14画像で decoded RGB 差分0。
  記録 `C:/tmp/gp/architecture-glsubmission-golden-comparison.txt`。
  以前からある backend 間の caption 差についての判定は変えない。
- 切替テスト: `C:/tmp/gp/architecture-glsubmission-final-bots-20261002-091241/`。
  両開始 backend で front 1回と、同じ試合内の Settings / Apply / Resume 3回が PASS。
  全6回 definitions=105/105、impact / new bomb / existing bomb 各2 particles。
  scene / window geometry / visibility を維持して simulation が進み、texture-only source は
  切替中に保持・scene 終了時に解放。VUID / Validation Error はなし。
- `C:/tmp/gp/architecture-glsubmission-final-held-20261002-091409/`:
  非 main controls を hold した効果 fixture も両開始 backend で PASS。
  全6回で同じ効果数と source 寿命を確認し、両方の最後の画像で impact と bomb の描画を目視。
  通常 bot / held の全4 process で試合前の hunter preview も PASS。

試合前の UI 検査で固定 draw 数の待機だけでは再描画前に先へ進む失敗が出た。
右キーが PlayScreen に届くことを診断で確認したが、その後の map click / hunter preview が
揃わない試行があった。UI の dispatcher / redraw が実時間を使うため、試合前5箇所は
従来の draw 数に加え `frames / 60` 秒の待機も要求する `WaitUi` にした。
クリックや preview の失敗判定は維持し、失敗時は step / surface / candidate bounds と PNG を残す。
失敗ログは `architecture-glsubmission-{bots,held,clickdiag,focusdiag,playdiag-repeat,boundsdiag}-*`
に残してある。動く bot の別の寿命問題がすべて解決したという判定ではない。

repo root の PowerShell、MSVC Release と game files / paths.txt を用意した状態で:

```powershell
ctest --test-dir tools/build/out/msvc-Release --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'CPU contract checks failed' }
python tools/check-phase5-shader-interface.py
if ($LASTEXITCODE -ne 0) { throw 'Shader interface audit failed' }
Push-Location tools/build/out/msvc-Release
try {
    'q' | & .\FruityPrime.exe -rhiconformance -noupdate
    if ($LASTEXITCODE -ne 0) { throw 'RHI conformance failed' }
    'q' | & .\FruityPrime.exe -thumbnailwindowcheck -noupdate
    if ($LASTEXITCODE -ne 0) { throw 'Window composite failed' }
    foreach ($backend in @('opengl', 'vulkan')) {
        'q' | & .\FruityPrime.exe -gpulifetime 'AD2 ALINOS PERCH' -cycles 40 -frames 3 -rhi $backend -vkvalidation -noupdate
        if ($LASTEXITCODE -ne 0) { throw "$backend lifetime failed" }
    }
} finally { Pop-Location }
```

Golden Capture は Phase E の手順、切替操作は上記リンク先の再実行手順を使用する。
通常 bot stress は `FRUITY_SWITCHCHECK_HOLD_ACTORS` を未設定、効果だけを隔離する fixture は `1`。
設定ファイルは試験前の byte 列を保管し、終了後に復元する。
Android build / 実機、remote CI、実際の device loss / OOM 故障注入は今回未実行。
R2 / R8 / R10～R13 / R16～R19 などの残項目は引き続き対応する。

## OpenGL device の Session 所有と終了後の wrapper（R8）

OpenGL device の process 単位の所有を除去し、各 `BackendSession` の
`unique_ptr<GraphicsDevice>` にした。current context の lookup は借用 pointer だけを保持し、
同じ context を二つの Session が所有しようとした場合は拒否する。
Session 終了時は自分の context を current にして GPU の完了を待ち、native object を解放する。
window が先に閉じられた場合も、その context が有効な間に native state を閉じる。
その後に残った wrapper の destructor は、他の context に対して削除命令を出さない。
pipeline / layout / binding set は session の lifetime token を検査し、
allocator が device の同じアドレスを再利用しても旧世代を新 device と誤認しない。
旧 texture の view 作成・resize、旧 pipeline / binding の利用も拒否する。

linked OpenGL program は pipeline が所有する。shader wrapper を先に解放しても
作成済み executable は描画に使える。Vulkan の作成済み native pipeline も
descriptor に borrowed shader pointer を残さない。deferred scene adapter の所有は R2 で続ける。

Golden Capture の再実行で、既存 harness が7個の窓を順に作り、lazy scene Session を
残している経路を検出した。最初だけ撮影に成功し、次の窓から
`The current context has no OpenGL session` になった。
閉じた context の device は既に無効なので、次の `Session::Device` で新しい device を生成する。
harness の画像判定は変更しない。失敗ログ `C:/tmp/gp/architecture-session-golden-opengl.log`
も保持し、修正後の7画像で回帰を確認した。

### 検証と再実行

- MSVC Release build 成功。CTest 5/5 と shader interface audit（22 sources）成功。
- `C:/tmp/gp/architecture-session-window-rhiconformance.log`:
  両 backend の共通 GPU fixture が PASS。pipeline 作成後に public shader を解放してから
  描画・色・buffer / texture transfer を検査する。
  OpenGL はさらに8回の shutdown / recreate を検査した。
  Buffer / Texture / Renderbuffer / Shader / Program / Sampler / FBO / VAO の名前を
  終了前に `glIs*` で確認し、context を残して Session を終了した後は実際に存在しないことを検査。
  explicit shutdown と window 側の defensive close を交互に使う。
  新 device が旧 logical texture handle を再利用した後に旧 wrapper を破棄し、
  新 resource の数と readback 内容が変わらないこと、旧 binding の拒否も検査する。
  Vulkan validation errors=0。Vulkan の強制 late-wrapper teardown stress はまだ実装していない。
- `C:/tmp/gp/architecture-session-window-thumbnailwindowcheck.log`:
  window composite / 色 / 上下方向 / inherited color / release=0 が PASS。
- `C:/tmp/gp/architecture-session-lifetime40-{opengl,vulkan}.log`:
  Alinos Perch の読み込み・描画・解放が両方40/40 PASS。
  毎回 resource / retired=0、Completed=Submitted、最後2 frame の明示 host wait=0。
  最後は OpenGL 69,640/69,640、Vulkan 25,520/25,520 submissions。
  CPU process private memory の測定増分は0.8984 / 0.4477 MB/cycle。
  前回 R4 の測定より大きく、メモリ改善や長時間安定の証拠とはしない。
  native object の解放 gate と CPU allocator / driver の process memory は分けて観測する。
- `C:/tmp/gp/architecture-session-final-golden-{opengl,vulkan}/`:
  7候補ずつ撮影成功。R4 の `architecture-glsubmission-golden-{opengl,vulkan}` と比較し、
  全14画像の decoded RGB 差分0。
  記録 `C:/tmp/gp/architecture-session-final-golden-comparison.txt`。
- 切替: `C:/tmp/gp/architecture-session-bots-20261002-093038/`。
  動く bot の通常 fixture は両開始 backend で front 1回、同じ試合内3回が PASS。
  同じ scene / geometry / visibility を維持して simulation が進む。
  全6回 definitions=105/105、impact / new bomb / existing bomb 各2 particles。
  texture-only source は切替中に保持され、scene 終了時に解放。validation error はない。
- `C:/tmp/gp/architecture-session-final-held-20261002-094140/`:
  窓の再作成修正後も held fixture が両開始 backend で PASS。
  全6回の effect 数と source 寿命は上記と同じ。最後の画像で黄色 impact を確認し、
  OpenGL 開始の画像では青い Lockjaw core も確認した。
  Vulkan 開始の最終画像では手前の hunter が core の一部を遮っている。

再実行は R4 の PowerShell 手順と上記の切替手順を使う。
`-rhiconformance` に OpenGL の8回の終了・再作成検査が含まれる。
Golden Capture は backend ごとに別の出力ディレクトリを指定する。
Android build / 実機、remote CI、device loss / OOM 故障注入は今回未実行。
Metal / D3D12 は将来対応。R8 は Vulkan 側の終了後の wrapper と shared device state の
context 寿命を整理するまで、全体完了とは扱わない。

## Vulkan native state の Session 終了（R8）

前節で残していた `VulkanDeviceState` の raw context 参照を終了境界で切り離した。
resource wrapper が shared state を保持しても native device / context は延命しない。
native owner の登録は借用であり、constructor が失敗した場合も registration は解放される。
終了は記録中の command を submit → GPU 完了 → command list の ring / descriptor pool /
cache と native command pool / fence → shader / pipeline / layout / sampler / image / view /
buffer → frame descriptor pool / fence → VMA allocator / scheduler → context の順。
通常の resource release はこれまでどおり submission retirement を使い、
device 終了時だけ、完了を待った native state を即時解放する。
終了後の texture handle / native name は無効化する。古い commands は recording を拒否し、
新 device は古い pipeline / layout / set / texture / view を受け入れない。
texture が view wrapper より先に破棄される場合も、view の destructor が解放済み texture を参照しない。

`Session::Shutdown` は明示的に context を終了してから validation 件数を保存する。
これにより、`vkDestroyDevice` が報告する未解放 child object も検査結果に含まれる。
swapchain / Skia UI は従来の caller 契約どおり、Session 終了前に解放する。
device loss / OOM の故障注入と、swapchain が caller の終了順を破る場合の検査は後続。

### 検証と再実行

- MSVC Release build 成功。CTest 5/5、shader interface audit（22 sources）成功。
- `C:/tmp/gp/architecture-vksession-final-view-conformance.log`:
  両 backend の共通 GPU fixture と8回ずつの shutdown / recreate が PASS。
  copy を記録して `End` を呼ばず、buffer / texture / view / sampler / shader /
  pipeline / layout / binding set / commands の wrapper を残したまま Session を終了する。
  Vulkan は全 cycle で Khronos validation が有効。
  終了時に copy の submission が進み Completed=Submitted、native owner / resource count /
  retired=0、VMA の終了直前の allocation count=0、context / allocator / scheduler の参照なし。
  shutdown を2回呼び、新 device が旧 logical texture handle を再利用した後に
  古い wrapper を破棄しても、新 buffer の GPU copy / readback 内容と resource count が維持される。
  incoming session で再描画して色が一致し、終了時の validation errors=0。
- 作成済み pipeline の `Desc` は shader の借用 pointer を持たず、値の state を保持する。
  resource 診断の古い pointer identity 判定が失敗したため、この明示契約に合わせて更新した。
  全 state の一致、4 binding groups、異なる state の区別、shader code の内容での cache 再利用、
  不正 pipeline の拒否は引き続き検査する。
  shader wrapper 解放後の実際の描画は共通 conformance fixture で検査する。
- `C:/tmp/gp/architecture-vksession-final-vulkanresourcecheck.log`:
  buffer / image upload / copy / readback / resize、全 shader modules / pipeline state /
  descriptor allocation / frame reuse が PASS、live=0、validation errors=0。
- `C:/tmp/gp/architecture-vksession-final-vulkanpresentcheck.log` と
  `architecture-vksession-final-vulkanpresentfallbackcheck.log`:
  resize / fullscreen / minimize / restore / presentation / shutdown が PASS、validation errors=0。
  fallback-retired-releases=6。
- `C:/tmp/gp/architecture-vksession-final-bots-20261002-095535/`:
  両開始 backend で front 1回、同じ Alinos Perch の Settings / Apply / Resume 切替3回が PASS。
  全6回 definitions=105/105、impact / new bomb / existing bomb 各2 particles。
  同じ scene / window geometry / visibility と simulation の継続、source の保持・終了時の解放を確認。
- `C:/tmp/gp/architecture-vksession-final-golden-{opengl,vulkan}/`:
  各7候補の撮影が成功。前節の `architecture-session-final-golden-{opengl,vulkan}` と比較し、
  全14画像の decoded RGB 差分0。
  記録 `C:/tmp/gp/architecture-vksession-final-golden-comparison.txt`。
- `C:/tmp/gp/architecture-vksession-lifetime40-vulkan.log`:
  Alinos Perch 40/40 PASS。全 release の resource / retired=0、Completed=Submitted。
  最後は25,520/25,520 submissions、最後2 frame の明示 host wait=0。
  CPU private memory peak は342→351 MB、0.4365 MB/cycle。
  VRAM / 長時間の上限やメモリ改善の証拠とはしない。

再実行は R4 のコマンドと既存の切替手順を使う。`-rhiconformance` は両 backend の
未送信 copy を残した8回の終了・再作成検査を含む。
途中の `architecture-vksession-{first,second,third}-conformance.log` は保持する。
fixture の readback buffer への CPU upload、copy 前の resource state、cache の明示解放を
修正した最終 fixture が上記ログであり、API の memory usage / state 検査は弱めていない。
Android build / 実機、remote CI、device loss / OOM / admission failure の故障注入は未実行。
Metal / D3D12 は将来対応。R2 / R10～R13 / R16～R19 などの残項目は対応中。

## Vulkan driver pipeline cache（R11）

`VulkanPipelineKey` → shared native pipeline の既存 semantic cache を維持し、
driver の compilation data を `VulkanPipelineCache.hpp/.cpp` の専用 owner に分離した。
scene の variant と共通 RHI pipeline はこの owner の `CreatePipeline` を通る。
生成・data retrieval・保存を mutex で直列化し、device 終了時に保存・破棄する。
cache なしの pipeline 生成も可能で、cache hint の失敗と本来の pipeline 生成エラーは区別する。
実際の pipeline の device loss 等は従来の `BackendError` に伝わり、cache が隠さない。

desktop の保存先は user data directory 内の `render-cache/vulkan-pipelines.bin`。
Windows の portable build では exe の隣になる。検査用の
`FRUITY_VK_PIPELINE_CACHE_DIR` がある場合はそのディレクトリを使う。
Android は同じ native cache owner を使うが、今回 disk persistence は追加しない。
生成データは `.gitignore` の対象。

保存形式は64 byte の明示 little-endian header と、driver が返した payload の組。
magic / schema / header size / vendor / device / driver version / pointer size / cache UUID /
payload length / checksum を検査し、native Vulkan header の identity も検査する。
読み込み・保存 payload の上限は64 MiB。長さの過大申告や途中の data は driver に渡さない。
この checksum は破損検出であり、認証の仕組みではない。
一時ファイルに書いてから atomic replacement し、同時に起動した process 同士が
不完全な data を読むことを避ける。最後の保存が勝つため cache entry の完全な union は保証しない。
保存不能の場合は前の file を維持する。

古い・不一致・破損した保存 data は捨てて空で生成する。
driver が initial data を拒否した場合は空で再試行し、native cache が生成できない場合や
optional cache entry point がない場合は `VK_NULL_HANDLE` で pipeline を生成する。
`vkGetPipelineCacheData` の `VK_INCOMPLETE` は bounded retry し、保存失敗は起動失敗にしない。
`Loaded` は initial data を受け取った cache の生成成功を示すだけで、driver cache hit とは区別する。
契約確認: [cache header](https://docs.vulkan.org/refpages/latest/refpages/source/VkPipelineCacheHeaderVersionOne.html)、
[data retrieval](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetPipelineCacheData.html)、
[native pipeline creation](https://docs.vulkan.org/refpages/latest/refpages/source/vkCreateGraphicsPipelines.html)。
参考はレビューが挙げた [melonPrimeDS の cache owner](https://github.com/ag-advania/melonPrimeDS/blob/c4165c87416902bb13b670e3147ecf05988017ed/src/VulkanPipelineCache.h)。

### 検証と再実行

- MSVC Release build、CTest 6/6、shader interface audit（22 sources）成功。
  `FruityPrime.VulkanPipelineCache` は Vulkan headers がある build で実行する CPU dispatch 検査。
  すべての truncation / 各 byte の bit corruption、trailing data、device / driver / UUID / schema /
  pointer size / native header、上限、cold / warm、driver rejection、cache 生成失敗、
  optional entry point 欠如、uncached compile、`VK_INCOMPLETE` retry、保存失敗、
  pipeline device loss の伝達、二つの thread の128生成、二重 close を検査する。
- `C:/tmp/gp/architecture-nativecache-gpu-20261002-101503/{cold,warm}.log`:
  実 GPU の共通 conformance と両 backend の8回の session teardown が PASS。
  cold process の最初は initial-bytes=0、保存は15,869 bytes。
  次の process の最初は initial-bytes=15,869。file は64 byte framing込み15,933 bytes。
  cache が pipeline 生成へ渡り、GPU の描画・readback が成功し、終了時 validation errors=0。
  driver の cache-hit feedback / compile 時間は計測しておらず、速度向上の証拠とはしない。
- 同じ evidence directory の `corrupt.log` / `identity.log` / `truncated.log`:
  実 driver が返した保存 file の破損・driver 不一致・truncation を作り、
  native loading 前に捨てたこと（initial-bytes=0）と resource check の PASS を確認。
  live=0、validation errors=0、各12 native pipeline を生成し185,982 bytes を保存。
- `unwritable.log`: 保存 directory の代わりに regular file を置いて検査。
  saved-bytes=0 でも resource check が PASS、既存 file の内容を維持、validation errors=0。
  native driver の initial-data rejection / cache allocation failure は CPU dispatch で検査し、
  実 GPU の故障として注入したという記録ではない。
- `C:/tmp/gp/architecture-nativecache-final-bots-20261002-101820/`:
  両開始 backend で front 1回、同じ Alinos Perch の Settings / Apply / Resume 切替3回が PASS。
  全6回 definitions=105/105、impact / new bomb / existing bomb 各2 particles。
  scene / geometry / visibility と simulation の継続、source の保持・終了時の解放を確認。
- `C:/tmp/gp/architecture-nativecache-final-golden-{opengl,vulkan}/`:
  各7候補を撮影。前節の `architecture-vksession-final-golden-{opengl,vulkan}` と比較し、
  全14画像の decoded RGB 差分0。
  記録 `C:/tmp/gp/architecture-nativecache-final-golden-comparison.txt`。

repo root の PowerShell で、新しい output directory を選んで:

```powershell
ctest --test-dir tools/build/out/msvc-Release --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'CPU contract checks failed' }
$savedCacheDirectory = $env:FRUITY_VK_PIPELINE_CACHE_DIR
$cacheEvidence = 'C:/tmp/gp/repeat-nativecache-' + (Get-Date -Format yyyyMMdd-HHmmss)
New-Item -ItemType Directory -Path $cacheEvidence -Force | Out-Null
Push-Location tools/build/out/msvc-Release
try {
    $env:FRUITY_VK_PIPELINE_CACHE_DIR = "$cacheEvidence/data"
    foreach ($phase in @('cold', 'warm')) {
        'q' | & .\FruityPrime.exe -rhiconformance -noupdate *> "$cacheEvidence/$phase.log"
        if ($LASTEXITCODE -ne 0) { throw "$phase GPU check failed" }
    }
} finally {
    $env:FRUITY_VK_PIPELINE_CACHE_DIR = $savedCacheDirectory
    Pop-Location
}
```

固定画像と試合中の操作は既存の Golden Capture / 切替手順を使う。
Android build / 実機、remote CI、native driver の故障注入は今回未実行。
R2 / R10 / R12 / R13 / R16～R19 などの残項目は引き続き対応する。
Metal / D3D12 は将来対応であり、今回追加していない。


## Vulkan descriptor allocator の分離（R10）

`VulkanDescriptorAllocator.hpp/.cpp` が submission slot ごとの descriptor pool を所有する。
generic `BindingSet` の frame slot と、scene command list の2つの slot が同じ実装を使う。
pool の確保・native descriptor type ごとの capacity admission・overflow・bulk reset・解放を
`VulkanGraphicsDevice.cpp` から取り出した。command / frame の fence と queue submission は
引き続き caller と `VulkanFrameScheduler` が担当し、allocator は queue を待たない。

- native pool を作る前に CPU page storage を reserve する。retention の allocation failure で
  確保済み native pool を失う順序を避ける。
- 1つの layout が要求する全 descriptor counts を確認し、default capacity より大きい
  layout も1つの新しい page に収める。合計 count の uint32 overflow / 不正な type / count=0 は
  native call 前に拒否する。
- `maxSets` と各 type の残 capacity を別々に数える。既存 page の exhaustion / fragmentation
  は次の page を使う。新しい page でも失敗した場合はその native error を返し、
  1回の request で pool を無制限に作る loop にしない。
- host / device OOM を descriptor exhaustion として扱わない。失敗した admission は
  既存 page の所有を変えず、reset failure の途中では allocation を許可しない。
- 成功した queue submit の `SubmissionSerial` を allocator に記録する。
  `ResetAfterCompletion` は timeline の実完了が last use に達していなければ、native reset 前に拒否する。
  frame 数や slot index を完了の代わりにしない。未送信 recording は caller が終了／破棄してから reset する。
- scene / generic の capacity と submission slots は共有しない。page は各 slot の high water まで
  保持し、実完了後に main / overflow とも再利用する。Session teardown で明示的に閉じ、
  late wrapper の破棄から native API を呼ばない。Session witness に page count=0 を追加した。

仕様確認: [pool reset は全 use の完了後](https://docs.vulkan.org/refpages/latest/refpages/source/vkResetDescriptorPool.html)、
[pool exhaustion と system/device memory failure の区別](https://docs.vulkan.org/refpages/latest/refpages/source/vkAllocateDescriptorSets.html)。

### 検証と再実行

MSVC Release build、CTest **7/7**、shader interface audit **22 sources** が成功。
`FruityPrime.VulkanDescriptorAllocator` は native dispatch の CPU fake を使う。
GPU / game files / Vulkan loader を起動せず、実際の driver OOM を注入した証拠とは区別する。

単体検査は type capacity と set capacity の別々の exhaustion、全5 type の大きい layout、
completion 前の reset 拒否、overflow page の同一 native handle 再利用、独立2 slots、
fragmentation / fresh-page failure の bounded growth、create / allocation / reset の native error 保持、
failed admission の所有保持、double close / ended allocator の拒否を含む。

repo root の PowerShell、game files / paths.txt と MSVC Release build 配置済み:

```powershell
ctest --test-dir tools/build/out/msvc-Release --output-on-failure
python tools/check-phase5-shader-interface.py
Push-Location tools/build/out/msvc-Release
try {
    & .\FruityPrime.exe -rhiconformance -noupdate
    if ($LASTEXITCODE -ne 0) { throw 'RHI conformance failed' }
    & .\FruityPrime.exe -vulkanresourcecheck -noupdate
    if ($LASTEXITCODE -ne 0) { throw 'Vulkan descriptor/resource regression failed' }
    & .\FruityPrime.exe -gpulifetime 'AD2 ALINOS PERCH' -cycles 40 -frames 3 -rhi vulkan -vkvalidation -noupdate
    if ($LASTEXITCODE -ne 0) { throw 'Vulkan lifetime regression failed' }
} finally { Pop-Location }
```

実 GPU は NVIDIA GeForce RTX 5070 Ti、Khronos validation 有効:

- `C:/tmp/gp/architecture-descriptor-final-conformance.log`: 共通 GPU fixture と両 backend の
  未送信 copy / old wrappers を残した Session shutdown / recreate 各8回 PASS。
  Vulkan native descriptor pages を含め解放し、validation errors=0。
- `C:/tmp/gp/architecture-descriptor-resourcecheck.log`: descriptor arrays / alignment / fresh sets /
  overflow pool growth / frame reuse / GPU bind submit、64回の clear / bind / resize / release PASS。
  最後は live=0 / retired=0 / completed=submitted / errors=0、churn device-wide waits=0。
- `C:/tmp/gp/architecture-descriptor-final-bots-20261002-124352/`: 動く bots の Alinos Perch で
  OpenGL 開始 / Vulkan 開始の両方、front screen 1回と同じ試合で Settings 保存 / Resume 各3回 PASS。
  全6回 scene / window geometry / visibility / simulation の進行を維持。
  definitions=105/105、impact / new bomb / existing bomb 各2 particles。
  texture-only source は切替中 alive、scene 解放時に released。両最終画像で黄色 impact / 青い Lockjaw core を目視。
- `C:/tmp/gp/architecture-descriptor-final-golden-{opengl,vulkan}/`: 7ケースずつ capture gate PASS。
  R11 実装後の `architecture-nativecache-final-golden-{opengl,vulkan}` と比較し、全14画像で
  decoded RGB の差0 bytes。比較表は `architecture-descriptor-final-golden-comparison.txt`。
- `C:/tmp/gp/architecture-descriptor-final-lifetime40-vulkan.log`: 40/40 PASS。
  毎回解放後の resource / retired は0、最後の submitted=completed=25520、最後2 frames の host waits=0。
  CPU private memory peak は340→348 MB、0.3961 MB/cycle。VRAM budget や速度向上の計測ではない。

R10 全体は未完了。frame slot / VMA memory / upload の change axis は続けて分離する。
page の high water 保持は admission budget / eviction の完成ではなく、R12 の残作業。
R2 の本番 shader ABI、R13 の upload ring、R16 の非同期 readback、R17 の実 driver 故障注入も残る。
Android build / 実機、remote CI は未実行。Metal / D3D12 は将来対応。

## 本番 shader ABI の接続（R2 / R3 / R15）

`SceneShaderAbi.def` に論理 binding、constant の型と配列数、texture / sampler の組と
OpenGL texture unit を定義する。C++ の共通 ABI header と shader 生成ツールが同じ定義を読む。
native API の buffer offset や std140 padding はここへ持ち込まない。
対象は main / composite / cel / shift / backdrop の5 programs、55 constants、7 textures。

- 生成ツールは本番 `Shaders.cpp` の GLSL declarations と型・配列数・名前を照合する。
  未定義 uniform / vertex attribute、重複 binding、desktop vertex location の相違で停止する。
  OpenGL の sampler 初期化と backdrop も共通 texture unit を使う。
- Vulkan は Frame / Material / Draw / Post の4 descriptor set layouts を使う。
  使わない group の空 layout も保持し、scene pipeline の cache key に全 layout を含める。
  textures は manifest の明示 unit / group / image binding / sampler binding で接続する。
- std140 は Vulkan の生成 adapter が計算する。embed 前に `reflect_scene_spirv.py` が
  コンパイル済み10 SPIR-V modules を独立に読み、実際の type / count / offset /
  array stride / column-major matrix stride / block size / set / binding / vertex input を照合する。
  runtime が使う member table と block 内の table の一致も要求する。
  これは interface の検査であり、SPIR-V の全 instruction validity を検証するツールではない。
  仕様参照: [Khronos SPIR-V specification](https://registry.khronos.org/SPIR-V/specs/unified1/SPIRV.html)。
- `VulkanSceneUniforms` は生成 metadata を使う本番 packer。
  不正な型・サイズ・配列幅・重複・範囲外を拒否し、配列の padding と隣接 constant を保持する。
  値が変わった block だけ generation を進める。描画時もその block だけを ring へ書き、
  影響した group に新しい descriptor set を作る。GPU 使用中の set は上書きしない。
  command slot / ring の再利用時には upload / set cache を無効化する。
- 未使用の手書き std140 prototype と raw shader snippets を削除した。
  prototype の検査に代えて、本番 declarations / 生成 metadata / 実 packer / 実 SPIR-V を検査する。
  gameplay / AI / effect lifetime の判定は変更しない。

### 単体検査と再実行

MSVC Release build、CTest **9/9**、shader interface audit **20 sources** が成功。
22→20 は未使用の prototype snippets 2つの削除によるもので、本番 shader の削減ではない。

`FruityPrime.VulkanSceneUniforms` は全5 programs / 55 constants を本番 packer へ書き、
実 offset / stride / matrix order、配列の上限、padding / 隣接値保持、block ごとの generation、
同じ値で再 upload しないこと、不正 metadata / write の拒否を検査する。GPU は不要。
`FruityPrime.SceneShaderAbiDrift` は生成済みの実10 modules を使い、set / binding /
offset / array・matrix stride / vertex location / type / count / image dimension /
entry stage / binary framing / manifest / stale schema を壊して拒否を確認する7ケース。
OpenGL-only build の `ShaderInterface` も本番 GLSL と共通定義を照合する。

repo root の PowerShell、MSVC Release build と game files / paths.txt 配置済み:

```powershell
ctest --test-dir tools/build/out/msvc-Release --output-on-failure
python tools/check-phase5-shader-interface.py
Push-Location tools/build/out/msvc-Release
try {
    & .\FruityPrime.exe -rhiconformance -noupdate
    if ($LASTEXITCODE -ne 0) { throw 'RHI conformance failed' }
    & .\FruityPrime.exe -vulkanresourcecheck -noupdate
    if ($LASTEXITCODE -ne 0) { throw 'Vulkan resource regression failed' }
    & .\FruityPrime.exe -gpulifetime 'AD2 ALINOS PERCH' -cycles 40 -frames 3 -rhi vulkan -vkvalidation -noupdate
    if ($LASTEXITCODE -ne 0) { throw 'Vulkan lifetime regression failed' }
    foreach ($backend in @('opengl', 'vulkan')) {
        $captureDirectory = "C:/tmp/gp/sceneabi-repeat-$backend-$(Get-Date -Format yyyyMMdd-HHmmss)"
        & .\FruityPrime.exe -goldencapture all -goldendir $captureDirectory -rhi $backend -vkvalidation -noupdate
        if ($LASTEXITCODE -ne 0) { throw "$backend capture failed" }
    }
} finally { Pop-Location }
```

Settings 保存 / Resume を使う切替は
[既存の Windows 再実行手順](old/Fruity-Prime-CPP-Renderer-Hot-Switch-Fix-2026-10-01.md#repeating-the-effect-regression-on-windows)
を使う。settings file と環境変数を保存・復元する。通常の動く bots の試験と
`FRUITY_SWITCHCHECK_HOLD_ACTORS=1` の effect fixture を区別して記録する。
診断ログには新旧 bomb entity の生存・flags・countdown を追加した。
粒子が0のとき、描画だけが消えたのか entity が通常の試合処理で終わったのかを追えるようにする。

### 今回の実 GPU 結果

NVIDIA GeForce RTX 5070 Ti、Khronos validation 有効。

- `C:/tmp/gp/architecture-sceneabi-final-conformance.log`: 共通 GPU fixture と
  両 backend の未送信 copy / old wrappers を残した shutdown / recreate 各8回 PASS。
- `C:/tmp/gp/architecture-sceneabi-final-resourcecheck.log`: 実4 group layouts と
  descriptor / resource churn 64回 PASS。live / retired / validation errors=0。
- `C:/tmp/gp/architecture-sceneabi-final-golden-{opengl,vulkan}/`: 7ケースずつ PASS。
  直前の descriptor allocator 実装後画像と比べ、全14画像の decoded RGB 差0 bytes。
  比較表は `C:/tmp/gp/architecture-sceneabi-final-golden-comparison.txt`。
- `C:/tmp/gp/architecture-sceneabi-final-botdiag-20261002-133151/`: 動く bots の Alinos Perch で
  両 backend 開始、front screen 各1回と同じ試合の切替各3回 PASS。
  全6回 definitions=105/105、impact / new bomb / existing bomb 各2 particles。
  新旧 bomb entity は alive、flags=0。scene / window geometry / visibility を維持し、
  simulation frame が進む。texture-only source は切替中 alive、scene 解放時 released。
- `C:/tmp/gp/architecture-sceneabi-final-held-20261002-132946/`: actor controls を止めた
  fixture も両方向・全6切替 PASS。最終画像で黄色 impact と青い Lockjaw core を目視。
- `C:/tmp/gp/architecture-sceneabi-final-lifetime40-vulkan.log`: 40/40 PASS、毎回
  resource / retired=0、最後の completed=submitted=25520、最後2 frames の host waits=0。
  CPU private memory peak 342→345 MB、0.151 MB/cycle。VRAM budget / 性能向上の測定ではない。
- `C:/tmp/gp/architecture-sceneabi-final-vulkanpresentcheck.log` と
  `architecture-sceneabi-final-vulkanpresentfallbackcheck.log`: resize / fullscreen /
  minimize / restore / shutdown PASS、errors=0、fallback-retired-releases=6。

最初の動く bots の試験 `C:/tmp/gp/architecture-sceneabi-final-bots-20261002-132448/` は
OpenGL 開始が PASS、Vulkan 開始の最後だけ新旧 bomb の particles / elements が0で exit 1。
その時も definitions=105/105、impact=2、simulation / scene は継続し、validation error はない。
以前の動く bots stress でも出た観測であるが、今回は失敗時の entity 状態を記録していないため
原因を断定できない。failed log は保持する。後続の通常 trial と held fixture の成功で
この失敗が解決済みとは扱わず、Phase H の actor / effect lifetime stress の追跡に残す。

R10 の frame slot / memory / upload 分離、R12 budget、R13 upload ring、R16 async readback、
R17～R19 の故障・診断・ownership stress などは引き続き残る。
Android build / 実機と remote CI は未実行。Metal / D3D12 は今回追加しない。

## Vulkan persistent upload の統一（R10 / R13）

`VulkanUploadArena.hpp/.cpp` が1つの submission slot の staging pages を所有する。
scene constants / transient geometry / texture upload と、描画外の buffer / texture upload が
同じ page allocation / alignment / flush / completion / close の実装を使う。
queue / command pool / fence は caller、実際の submission serial は `VulkanFrameScheduler` が所有する。

- VMA の `HOST_ACCESS_SEQUENTIAL_WRITE | MAPPED` で page を一度確保する。
  CPU pointer は page の寿命中保持し、upload ごとの map / unmap を行わない。
  native admission 前に CPU page retention を reserve し、不完全な mapping は native page を解放して拒否する。
  これは GPU への転送用 staging であり、renderer 切替の復元用 texture backup を新設したものではない。
- size / alignment / reserved-byte overflow を確認し、任意 alignment と page overflow を扱う。
  大きい request は十分な1 page に収める。dirty ranges を page ごとにまとめて submit 前に flush し、
  部分的な flush failure は残りの dirty range を保持する。
- 成功した submit の serial を記録する。実 completion より早い reset / allocation は拒否する。
  完了後は default / overflow pages を再利用し、Session 終了で全部閉じる。
  caller は unsubmitted recording を終了／破棄してから reset、GPU を drain／破棄してから close する。
- 描画中の GPU-only buffer / texture write は、その位置の command stream に copy を挿入する。
  描画外では device が専用 transfer command list を保持し、小さい writes をまとめる。
  8 MB の batch threshold、別 command list の開始、frame 終了、readback、resource release、
  Session teardown が submit の境界。ring は2 slots を交換し、再利用時に必要な completion を待つ。
  upload ごとに staging Buffer / temporary CommandList を作る fallback を除去した。
- `CpuToGpu` destination 自体も persistently mapped にする。static mesh の vertex / index destination は
  `GpuOnly + TransferDst` にし、copy 後に vertex / index state へ transition する。
  static mesh ごとに mapped host destination を保持する構成は採用しない。
- transfer command list の lifetime count は scene command list と区別する。
  transfer stream の保持で scene の window target 解放を妨げない。
  Session teardown witness は upload command lists / pages の0も要求する。

仕様確認: [VMA persistent mapping / flush](https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/memory_mapping.html)。
VMA が non-coherent atom alignment を扱う。queue submission の後も memory を mapped のまま保持できる。
GPU-only は配置の意図であり、UMA / BAR も含め、常に CPU から不可視の heap に置かれるとの保証ではない。

### 検証と再実行

MSVC Release build、CTest **10/10**、shader interface audit **20 sources** が成功。
`FruityPrime.VulkanUploadArena` は native API / GPU を起動しない dispatch fixture。
任意 alignment、slice address / byte の保持、oversized admission、default / overflow page の再利用、
completion 前の拒否、独立2 slots、create / mapping / partial flush failure、dirty range の再試行、
double close / late calls を検査する。実 driver の OOM 注入とは区別する。

repo root の PowerShell、MSVC Release build と game files / paths.txt 配置済み:

```powershell
ctest --test-dir tools/build/out/msvc-Release --output-on-failure
python tools/check-phase5-shader-interface.py
Push-Location tools/build/out/msvc-Release
try {
    & .\FruityPrime.exe -vulkanresourcecheck -noupdate
    if ($LASTEXITCODE -ne 0) { throw 'upload/resource regression failed' }
    & .\FruityPrime.exe -rhiconformance -noupdate
    if ($LASTEXITCODE -ne 0) { throw 'RHI ownership regression failed' }
    & .\FruityPrime.exe -gpulifetime 'AD2 ALINOS PERCH' -cycles 40 -frames 3 -rhi vulkan -vkvalidation -noupdate
    if ($LASTEXITCODE -ne 0) { throw 'Vulkan lifetime regression failed' }
} finally { Pop-Location }
```

固定 Golden Capture と Settings 保存 / Resume は直前の shader ABI 節の再実行手順を使う。
GPU resource check は64回の buffer / texture writes で warmup 後の page creation が増えないこと、
小さい writes が1つの stream にまとまること、recording 中の更新前後のコピーが各々正しいことを検査する。
8 MB 超の upload も2 slots の warmup 後は page creation が増えず、全 bytes が readback と一致することを要求する。
通常 upload の device-wide waits が増えないことも要求する。readback 自体の同期化は R16 の残作業。

実 GPU は NVIDIA GeForce RTX 5070 Ti、Khronos validation 有効:

- `C:/tmp/gp/architecture-uploadarena-staticgpu-vulkanresourcecheck.log`: 上記 upload gate と
  64回の clear / descriptor bind / resize / release PASS、live / retired / validation errors=0。
  RGB32Float の sampled image はこの GPU の指定 usage では未対応と native query が返すため、
  実 GPU float texture は RGBA32Float で検査した。12-byte alignment は CPU fixture で検査する。
  RGB32Float の実 transfer 成功を主張しない。
- `C:/tmp/gp/architecture-uploadarena-staticgpu-rhiconformance.log`: 共通 GPU fixture、両 backend の
  pending copy / old wrappers を残した shutdown / recreate 各8回 PASS。
  upload pages と VMA allocations を閉じ、validation errors=0。
- `C:/tmp/gp/architecture-uploadarena-staticgpu-golden-{opengl,vulkan}/`: 各7ケース PASS。
  shader ABI 接続後の直前画像と比較し、全14画像の decoded RGB 差0 bytes。
  比較表は `C:/tmp/gp/architecture-uploadarena-staticgpu-golden-comparison.txt`。
- `C:/tmp/gp/architecture-uploadarena-staticgpu-bots-20261002-140708/`: 動く bots の Alinos Perch で
  両 backend 開始、front screen 各1回と同じ試合の Settings 保存 / Resume 各3回 PASS。
  全6回 definitions=105/105、impact / new bomb / existing bomb 各2 particles。
  新旧 bomb entity は alive / flags=0。scene / window geometry / visibility を保持し、
  simulation frame は進む。texture-only source は切替中 alive、scene 解放時 released。
  最終画像で黄色 impact / 青い Lockjaw core を目視した。validation error はない。
- `C:/tmp/gp/architecture-uploadarena-staticgpu-lifetime40-vulkan.log`: 40/40 PASS、毎回
  resource / retired=0、最後の completed=submitted=320、最後2 frames の host waits=0。
  CPU private memory peak 330→332 MB、0.0869 MB/cycle。
  この fixture の送信数は直前の25520から320へ減ったが、fps / wall time / VRAM budget の改善を測った結果ではない。

途中の実装では static mesh を `CpuToGpu` の mapped destination として保持していた。
その40回 gateは counts / trend とも PASS したが private peak は574→577 MBで、
絶対量が大きかった。`C:/tmp/gp/architecture-uploadarena-batched-lifetime40-vulkan.log` に保持する。
static mesh を GPU-only destination に変更した最終構成の330→332 MBと区別し、
安定した count / trend だけでメモリ使用量が改善したとの判断はしない。

R10 の frame slot / VMA memory factory、R12 の budget admission、R16 の async readback、
R17～R19 の故障・診断・ownership / format stress は残る。
upload の page high water 保持は全 workload の memory budget / eviction 保証ではない。
Android build / 実機と remote CI は未実行。Metal / D3D12 は将来対応。

## メモリ予算と確保前の判断（R10 / R12）

`MemoryBudget.hpp` に API に依存しない `MemoryBudgetSnapshot` / `AllocationRequest` /
`AllocationDecision` / `MemoryTelemetry` を追加した。heap ごとの容量・budget・usage・
backing reservation・pending bytes、単一確保の上限、native block 数の上限を扱う。
`EvaluateAllocation` は native API / global state を呼ばない pure function。

- `Unknown` / `Estimated` / `DriverLive` を区別する。未知なら `BudgetUnavailable` として
  native allocator に判断を委ねる。既知の budget=0 は容量不足であり、未知と扱わない。
- usage と backing は `max` で数え、二重加算しない。未完了の確保は別に加算する。
  独立 heap の空きを合計して一つの heap の余裕とみなさない。
- 通常は `min(128 MiB, budget / 16)` の余裕を残す。byte / count の overflow、
  不正な heap index / source / snapshot を拒否する。
- policy の拒否は `BackendError(OutOfMemory, nativeCode=0)`。実際の native failure の
  error code と混同せず、accepted / denied / native failures / pending を別に記録する。

`VulkanMemory` が唯一の VMA owner。RHI buffer / image と upload arena page の確保を
この境界へ集約した。native requirements と VMA の適合 memory type を調べ、その heap を
admit してから実際に確保する。拒否された heap の memory types を除外し、別の適合 heap
があれば再評価する。予約は mutex で計上し、成功・native failure とも RAII で解消する。
device allocation size / buffer size の上限を越える buffer は native requirements query 前に拒否。

`VK_EXT_memory_budget` が使える device は extension と VMA の対応 flag を有効にし、
確保時に driver の heap budget / usage を読み直す。使えない場合は VMA の推定値を使う。
VMA が resource より大きい backing block を確保する場合にも、`WITHIN_BUDGET` の
block 単位のチェックを残す。transfer / retirement は allocator を借り、owner の close 前に
native work を完了し、全 resource を閉じる。close 後に snapshot は空となり、allocator と
pending はゼロであることを session teardown fixture でも要求する。

OpenGL は `OpenGlMemory` が optional `GL_NVX_gpu_memory_info` の dedicated capacity と
current available dedicated memory を読む。異なる pool を含む total available counter は
組み合わせない。KiB を bytes に変換し、不正な counter pair は未知とする。
RHI の backing estimate は create / successful resize / release 時に増減させ、確保ごとに
全 resource を走査しない。buffer create / orphaning、texture / renderbuffer / resize、
launcher の external texture と Ganesh surface の確保前に判断する。native GL error は
code を保持し、OOM とその他の error を分類する。

RHI 経由の map thumbnail / Vulkan-Skia target もこの確保境界を通る。
テクスチャ復元用の CPU 画像コピーや毎フレームの GPU readback は追加していない。

情報の範囲には制約がある。GL counters は利用時点の目安で、portable budget API ではない。
GL backing estimate は RHI 管理分のみで、retired / Skia / external objects は driver usage に
依存する。VMA block count も VMA 管理分のみで、Skia 等の native allocation count を含まない。
reservation は各 resource につき追加 block を一つと仮定する保守的な count 判定。
snapshot は native allocation 成功・fragmentation・他プロセスとの競合を保証しない。
eviction / cache trimming policy、実 driver OOM の故障注入はこの gate に含めない。

参照: [VMA budget](https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/staying_within_budget.html)、
[NVX counters](https://registry.khronos.org/OpenGL/extensions/NVX/NVX_gpu_memory_info.txt)、
[buffer requirements query](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetDeviceBufferMemoryRequirements.html)。

### 再実行と今回の証拠

repo root の PowerShell、MSVC Release と game files / paths.txt 配置済み:

```powershell
ctest --test-dir tools/build/out/msvc-Release --output-on-failure
python tools/check-phase5-shader-interface.py
Push-Location tools/build/out/msvc-Release
try {
    & .\FruityPrime.exe -vulkanresourcecheck -noupdate
    if ($LASTEXITCODE -ne 0) { throw 'memory/resource gate failed' }
    & .\FruityPrime.exe -rhiconformance -noupdate
    if ($LASTEXITCODE -ne 0) { throw 'memory/ownership gate failed' }
    foreach ($backend in @('opengl', 'vulkan')) {
        & .\FruityPrime.exe -gpulifetime 'AD2 ALINOS PERCH' -cycles 40 -frames 3 -rhi $backend -vkvalidation -noupdate
        if ($LASTEXITCODE -ne 0) { throw "lifetime failed: $backend" }
    }
} finally { Pop-Location }
```

`FruityPrime.MemoryAdmission` は synthetic discrete / multi-heap / UMA、未知・推定・live zero、
pending / safety reserve / max size / count / overflow / invalid snapshot と、fake NVX source の
pool / units / invalid counters / error code を検査する。実 driver OOM 注入ではない。
実 GPU fixture は一時的に admission ceiling を1 byte にし、resize と buffer create が
native memory allocation 前に拒否され、元の image / view / extent / handle / readback pixels が
保たれること、telemetry の native failures が増えず pending がゼロであることを要求する。
ceiling は例外時も戻し、product setting として公開しない。

- `C:/tmp/gp/architecture-memory-final-build.log`: MSVC Release PASS。
- `C:/tmp/gp/architecture-memory-final-ctest.log`: 11/11 PASS。
  shader interface audit は20 sources PASS。
- `C:/tmp/gp/architecture-memory-final-resourcecheck.log`: memory / upload / resource gate PASS、
  giant buffer 拒否、live=0、Khronos validation errors=0。
- `C:/tmp/gp/architecture-memory-final-conformance.log`: 両 backend の memory gate と
  共通描画・コピー、各8回の session shutdown / recreate PASS。
- Golden Capture は OpenGL の `architecture-memory-ledger-golden-opengl/` と Vulkan の
  `architecture-memory-golden-vulkan/` 各7ケース PASS。直前の upload arena baseline と全14枚の
  decoded RGB の変更 pixel / 最大差は0。`architecture-memory-golden-comparison.txt` に比較表を保持。
- `C:/tmp/gp/architecture-memory-lifetime40-{opengl,vulkan}.log`: 両方40/40 PASS、
  毎回 resource / retired=0、最後2 frames の host waits=0。
  CPU private peak は GL 258→262 MB（0.167383 MB/cycle）、Vk 330→333 MB（0.141211 MB/cycle）。
  fps / wall time / VRAM 使用量が改善したという測定ではない。
- `C:/tmp/gp/architecture-memory-held-20261002-144613/`: non-main controls を止めた8 actors の
  Alinos Perch で、Settings の実 apply / Resume を使う各3回の切替 PASS。
  105/105 effect definitions、impact / 新旧 Lockjaw particles が各2、同じ scene / 進む simulation、
  visible window / geometry と source lifetime を確認。最終画像で黄色 impact / 青い core を目視。

active bots の `C:/tmp/gp/architecture-memory-bots-20261002-144128/` は GL 開始 PASS、
Vk 開始の切替2・3回目の bomb predicate は FAIL。bomb flags=2 / countdown=0 /
effect elements=0 だった。試験を緩めたり gameplay を変更して PASS にしていない。
actor / bomb の通常寿命と renderer failure の切り分けは Phase H の未解決項目として残す。
held fixture の PASS はこの active-bot failure の解決を意味しない。

R10 の frame slot / probe、R16 の async readback、R17～R19 の故障・診断・ownership / format
stress とレビュー全体の残作業を続ける。Android build / 実機と remote CI は未実行。
Metal / D3D12 は将来対応。

## 切替直前直後の状態検査とボムの通常寿命（R19 / Phase H）

上記 memory gate の active-bot failure を追跡した。Vulkan 開始時の2回目の切替では、
main player の HP はすでに0で、元の Lockjaw bomb は **GPU の解放前に** 爆発し、
effect を unlink 済みだった。C# の `Entities/BombEntity.cs` と native の
`Entities/BombEntity.cpp` は、ともに owner の HP=0 で countdown=0 / Exploded にする。
その後 HP=0 の owner で置いた新しい probe bomb も、通常の simulation step で爆発する。
「Resume 後は owner の生死にかかわらず新旧 bomb の particles が必要」という
future-time predicate がこの通常寿命を誤って失敗扱いしていた。gameplay の死亡・爆発処理は変更しない。

`RendererSwitchWitness` を追加し、GPU 解放前と完全な再構築後の、途中に simulation step が
一つも入らない区間を比較する。検査は `FRUITY_SWITCHCHECK` を設定した診断実行だけで動く。

- 同じ Scene、simulation frame / elapsed time、全 player の参照・HP・位置が完全一致する。
- active bomb の参照・owner・flags・countdown・位置・effect / element / particle の参照が一致する。
- particle の位置・速度・時刻・寿命・scale / rotation / color / alpha・内部 float fields・
  ID / owner / drawable 状態と texture binding IDs が一致する。float は bit pattern で比較する。
- 参照する GPU texture が両側に実在し、同じ handle / extent / layers / mips / format を持つ。

snapshot はこの区間の検査用の参照・数値だけであり、texture pixels の CPU backup や
GPU readback は追加しない。比較後すぐ解放する。GPU pixels の正しさは既存の描画・画像検査で確認する。
impact は各 Resume 後に作るため、この witness の particle snapshot 対象は live bomb である。

Resume 後の別検査は、生存 owner なら新旧 bomb の drawable particles と元の effect の所有を
引き続き要求する。死亡 owner なら、entity が Exploded / countdown=0 / effectなし /
particles=0 の通常寿命に到達したことを要求する。どちらでも impact と loaded effect definitions の
保持は必須。owner が死亡したケースを検査対象から外さず、切替そのものの不変性と別に判定する。

`FRUITY_SWITCHCHECK_WITNESS_SELFTEST=1` は最初の試合内切替直前に negative controls を実行する。
simulation を進める前に HP・particle alpha・texture binding をそれぞれ一時的に変え、
検査が拒否することを確認して元へ戻す。3種類の拒否と復元後の完全一致を要求する。
これは検査器の自己検証であり、driver OOM / device loss の故障注入ではない。

### Windows での再実行手順

repo root の PowerShell で以下を実行する。MSVC/vcpkg の build 環境、display、
両 renderer と extracted game files が必要。`paths.txt` は exe の隣へ配置する。
Khronos validation coverage には layer の導入とログの `validation=1` の確認が必要。
同じ `launcher.txt` を使うため、GPU 試験は順番に実行する。

```powershell
cmd /c tools\build\build-cpp.bat msvc Release
if ($LASTEXITCODE -ne 0) { throw 'Release build failed' }
ctest --test-dir tools/build/out/msvc-Release --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'CTest failed' }

$captureRoot = "C:/tmp/switch-witness-$(Get-Date -Format yyyyMMdd-HHmmss)"
$names = @('FRUITY_SWITCHCHECK', 'FRUITY_SHOT_ROOM',
    'FRUITY_SWITCHCHECK_HOLD_ACTORS', 'FRUITY_SWITCHCHECK_WITNESS_SELFTEST', 'FRUITY_SWITCHCHECK_FAILURES')
$savedEnv = @{}
foreach ($name in $names) { $savedEnv[$name] = [Environment]::GetEnvironmentVariable($name, 'Process') }
Push-Location tools/build/out/msvc-Release
$prefsPath = Join-Path $PWD 'launcher.txt'
$hadPrefs = Test-Path -LiteralPath $prefsPath
$prefsBytes = if ($hadPrefs) { [System.IO.File]::ReadAllBytes($prefsPath) }
try {
    $env:FRUITY_SWITCHCHECK = '1'
    $env:FRUITY_SHOT_ROOM = 'AD2 ALINOS PERCH'
    $env:FRUITY_SWITCHCHECK_WITNESS_SELFTEST = '1'
    $env:FRUITY_SWITCHCHECK_FAILURES = $null
    foreach ($mode in @('held', 'moving')) {
        $env:FRUITY_SWITCHCHECK_HOLD_ACTORS = if ($mode -eq 'held') { '1' } else { $null }
        foreach ($backend in @('opengl', 'vulkan')) {
            $captureDir = "$captureRoot/$mode-$backend"
            $logPath = "$captureRoot/$mode-$backend.log"
            New-Item -ItemType Directory -Path $captureDir -Force | Out-Null
            'q' | & .\FruityPrime.exe -shellshot $captureDir -rhi $backend `
                -vkvalidation -fpscap 60 -noupdate -debuglog *> $logPath
            $exitCode = $LASTEXITCODE
            Select-String -Path $logPath -Pattern `
                'switch witness|switchcheck context|effect definitions|match kept|source released|VUID|Validation Error'
            if ($exitCode -ne 0) { throw "${mode}/${backend}: inspect $logPath" }
            if (@(Select-String -Path $logPath -SimpleMatch '[switch witness] PASS;').Count -ne 3) {
                throw "Missing transition coverage: $logPath"
            }
            if (@(Select-String -Path $logPath -SimpleMatch '[switch witness] negative controls PASS;').Count -ne 1) {
                throw "Missing negative controls: $logPath"
            }
            if (Select-String -Path $logPath -Pattern 'VUID|Validation Error|lifecycle FAIL') {
                throw "Renderer/lifecycle validation failed: $logPath"
            }
        }
    }
} finally {
    if ($hadPrefs) { [System.IO.File]::WriteAllBytes($prefsPath, $prefsBytes) }
    elseif (Test-Path -LiteralPath $prefsPath) { Remove-Item -LiteralPath $prefsPath }
    foreach ($name in $names) { [Environment]::SetEnvironmentVariable($name, $savedEnv[$name], 'Process') }
    Pop-Location
}
Write-Output "Captures and logs: $captureRoot"
```

各 process は front screen で1回、PLAY → Offline の hunter preview を通り、Sylux + 7 actors の
Alinos Perch で Pause → Settings → Renderer → Apply → Resume を3回実行する。
同じ scene / 進む simulation、visible window / geometry、105/105 effect definitions、
texture-only source が切替中 alive / scene 終了時 released であることを要求する。
held は非 main の controls を止め、全6切替で生存中の新旧 bomb と impact の各2 particles を要求する。
moving は bots を動かすので、HP=0 の場合は上記の爆発条件と transition witness を確認する。
bot の攻撃結果は frame timing に依存し、必ず死亡ケースになる試験ではない。

held の `switch-effects-0-before.png` と `switch-3-match.png` / `switch-4-match.png` /
`switch-5-match.png` を開き、crosshair 左の黄色 impact、右の青い Lockjaw core を確認する。
moving の死亡後に core がない画像は、healthy owner の表示回帰の代替にはしない。
手動での発砲入力と Lockjaw snare triangles はこの emitter-placement fixture の対象外。

### 今回の証拠と残る範囲

- 原因追跡: `C:/tmp/gp/architecture-probe-context-moving-20261002-150743/vulkan.log` は旧 predicate で exit 1。
  HP=0 の owner / flags=2 / countdown=0 / effectなしを記録。
- `C:/tmp/gp/architecture-switch-witness-moving-20261002-152416/vulkan.log` も旧 predicate では exit 1。
  ただし全3回の transition witness は PASS、切替2・3回目は GPU 解放前から active bomb が0。
  当時の particle snapshot は部分項目で、後述の全 float / ID の snapshot と区別する。
- 修正後の moving 両開始 backend は `architecture-switch-witness-final-moving-20261002-153321/` で exit 0。
  生存 / 死亡それぞれの lifecycle と3種類の negative controls が PASS。
- 全 particle 項目へ拡張した held 両開始 backend は
  `C:/tmp/gp/architecture-switch-witness-final-held-20261002-153747/` で exit 0。
  全6切替の witness と新旧 bomb / impact の表示を確認。最終 Vulkan 画像で黄色 impact / 青い core を目視。
- 最終ソースの moving Vulkan 開始は
  `C:/tmp/gp/architecture-switch-witness-delivery-20261002-154925/vulkan.log` で exit 0。
  transition PASS が3件、negative controls PASS が1件。1回目は HP49 / bomb particles=2、
  2・3回目は HP0 / 通常の爆発条件 PASS。105/105 definitions、impact particles=2 を保持。
  Khronos validation 有効、VUID / Validation Error はなし。
- `C:/tmp/gp/architecture-switch-witness-final-build.log`: 最終 MSVC Release build PASS。
  `architecture-switch-witness-ctest.log`: 11/11 PASS。
- この MD の code block をそのまま実行した
  `C:/tmp/gp/architecture-switch-witness-documented-recipe.log` も PASS。
  captures / 各 process のログは `C:/tmp/switch-witness-20261002-155326/`。
  held / moving × OpenGL / Vulkan 開始の全4 process が exit 0、各3回の transition と
  1回の negative controls が PASS。全 held 切替で impact / 新旧 bomb 各2 particles、
  moving Vulkan の死亡ケースも通常寿命の条件を満たす。両 held 最終画像の黄色 impact / 青い core を目視。
  build / CTest 11/11 と validation error=0 を確認。

上記の古い FAIL ログは保持する。この修正は記録された active-bot fixture の誤判定を解消する証拠であり、
任意の gameplay failure がないことやレビュー全体の完了を意味しない。切替は各 process の試合内3回で、
Phase H の100-cycle / unload / resize / presentation ownership / format / failure stress は残る。
R10 の frame slot / probe、R16 の async readback、R17～R19 の残項目を続ける。
Android と remote CI は未実行。Metal / D3D12 は将来対応で、今回は追加しない。

## 非同期 readback と画像出力（R16）

`ReadbackTicket` / `ReadbackResult` / `ReadbackQueue` を追加した。buffer byte range と
render target color region の copy を GPU に送信し、`IsReady()` で非ブロッキングに確認してから
`MapResult()` で immutable CPU output を取得する。既存の同期 `ReadBuffer` / `ReadColor` は維持する。
これは screenshot / PNG recording のための出力であり、通常の texture の CPU backup は増やさない。
Metal / D3D12 backend は追加しない。

### API と所有権

- ticket / queue / native mapping は device thread で扱う。`IsReady()` は CPU copy を行わない。
  未完了 ticket の `MapResult()` は拒否する。完了後は一度だけ copy し、再取得は同じ output を共有する。
- source は enqueue が返った後に resize / release できる。staging は ticket がなくなっても
  実際の GPU completion まで queue が保持する。`Cancel()` は consumer の参照を手放すだけである。
- copied CPU output は immutable lease として writer thread へ渡せる。device / queue を閉じても
  この output は有効で、最後の所有者が破棄した時点で quota を解放する。
- shutdown / renderer switch は既存の明示的 idle / loss 境界の後に queue を閉じる。
  未 mapping ticket は `Cancelled` にして native storage を解放する。完了済みの CPU output は維持する。
- 件数と staging + CPU output bytes を **native allocation / submission より前に** 予約する。
  デフォルトは8件 / 64 MiB。mapped 後も output lease の件数・bytes は charge され、
  staging の bytes だけを解放する。満杯では empty ticket を返し、同期 readback へ逃げない。
- mapping / completion の例外は `Failed` として保持し、shutdown まで native ownership を維持する。
  合成 fault を検査した。実 driver device loss / OOM の検査は R17 の残作業である。

OpenGL は PBO と GLsync serial を使う。native completion を確認してから map / copy し、
read framebuffer / buffer、PBO、pack state、mapping 時の COPY_READ binding を復元する。
Vulkan は VMA staging buffer、COPY → HOST barrier と非ブロッキング completion poll を使う。
buffer copy 用の private command list は timeline と自身の fence の完了を確認してから解放する。
同期互換 `CommandList::End()` は使わずに submit する。
color capture の burst が Vulkan command list の2 recording slots を消費した場合も、
未完了 slot の通常の frame-throttle wait に入らず empty ticket を返す。
後続の通常描画の frame pacing / slot 待機方針は変更しない。

color API は resolved な RGB8 / RGBA8 / BGRA8 の base mip / layer から RGB8 / RGBA8 を出力する。
sRGB の source もこの8-bit storage 範囲で扱う。RGB source の RGBA output は alpha=255 とする。
float、depth/stencil、MSAA の未 resolve source、任意 mip / layer の読み取りは今回の対象外。
RGB/RGBA output は tightly packed / bottom row first で、PNG 側が上下を反転する。

### screenshot / recording

本番 `Export::Images` は async を使える desktop backend で ticket を保持し、毎フレームの
`PollReadbacks()` で ready output を writer queue に移す。writer は CPU output だけを扱う。
device ごとの quota に加えて、pending capture と書き込み中を含む PNG output は合計8件 / 64 MiB に制限する。
recording が満杯なら新しい frame を drop する。screenshot はログで skip を知らせる。
通常の capture を待つための GPU idle、無制限な GPU / CPU queue は追加しない。

writer は condition variable で待つ owned thread にし、shutdown で queued CPU output を drain / join する。
device 終了で未 mapping の capture が cancel された場合は、その画像は保存せずログへ記録する。
PNG write failure は render thread の poll に通知する。async を提供しない backend の既存同期経路は維持する。
Android GL は今回 async を有効にせず、Android build / device test も未実行。

### 検査

CPU の `FruityPrime.Readback` は readiness 前の map 拒否、一度だけの copy、件数 / byte 制限、
lease が残る間の backpressure、writer thread への lease 移動、abandoned transfer の保持、
factory / poll / map fault、overflow、queue 終了後の CPU output を検査する。

`-rhiconformance` は同じ GPU fixture を両 backend で実行する。

- buffer offsets / contents、enqueue 後の source release、quota 拒否時に native allocation / submission がないこと。
- 641×127 の RGBA / BGRA / RGB source に赤と緑の領域を描き、637×123 の offset subregion を
  RGB / RGBA で取得する。奇数 RGB row width、channel order、transparent / opaque alpha を全 pixel で比較する。
  enqueue 直後に source を32×16へ resize し、view / image を解放してから output を読む。
- setup / cleanup の明示的 idle を除く issue / poll / map / resize / release の
  `HostWaits` と `DeviceWideWaits` の増加がともに0。最終 live / retired resource と quota が0。
- 各 backend の4回の session shutdown / recreate で、pending / abandoned native copy の解放と
  shutdown 後の mapped output lease の保持、validation error=0 を要求する。
- 同じ command list へ32回連続で color capture を要求する。8件以下への制限、admitted output の
  全 pixel と host / device wait delta=0 を要求する。GPU が速い場合は recording-slot 拒否が
  実際に発生するとは限らないため、slot 拒否件数そのものを pass 条件にしない。

通常の `-shellshot` は本番 screenshot と recording の両 PNG を読み直し、641×127 / RGB / 上下方向と
3色の帯を全 pixel で検査する。通常 script の hunter side panel と試合終了後の再選択では、
UI の slide / dispatcher が wall clock で進むのに uncapped GL の draw count だけで待っていたため、
layout 完了前に次の操作へ進んでいた。既存 `WaitUi` に合わせて wall time も待つよう修正した。
クリック、production preview の描画・再生成の assert は維持する。
初期 FAIL ログ `architecture-async-readback-shell-final-20261002-175511/opengl.log` は保持する。

### Windows での再実行手順

repo root の PowerShell、MSVC / vcpkg、display、Khronos validation layer、exe 隣の `paths.txt` が必要。
同じ preferences を使うため GPU process は順番に実行する。下記は保存内容と process env を復元する。

```powershell
cmd /c tools\build\build-cpp.bat msvc Release
if ($LASTEXITCODE -ne 0) { throw 'Release build failed' }
ctest --test-dir tools/build/out/msvc-Release --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'CTest failed' }

$captureRoot = "C:/tmp/async-readback-$(Get-Date -Format yyyyMMdd-HHmmss)"
$names = @('FRUITY_SWITCHCHECK', 'FRUITY_SHOT_ROOM',
    'FRUITY_SWITCHCHECK_HOLD_ACTORS', 'FRUITY_SWITCHCHECK_WITNESS_SELFTEST')
$savedEnv = @{}
foreach ($name in $names) { $savedEnv[$name] = [Environment]::GetEnvironmentVariable($name, 'Process') }
Push-Location tools/build/out/msvc-Release
$prefsPath = Join-Path $PWD 'launcher.txt'
$hadPrefs = Test-Path -LiteralPath $prefsPath
$prefsBytes = if ($hadPrefs) { [System.IO.File]::ReadAllBytes($prefsPath) }
try {
    New-Item -ItemType Directory -Path $captureRoot -Force | Out-Null
    'q' | & .\FruityPrime.exe -rhiconformance -noupdate *> "$captureRoot/conformance.log"
    if ($LASTEXITCODE -ne 0) { throw 'GPU conformance failed' }
    $env:FRUITY_SWITCHCHECK = $null
    $env:FRUITY_SHOT_ROOM = 'AD2 ALINOS PERCH'
    foreach ($backend in @('opengl', 'vulkan')) {
        $captureDir = "$captureRoot/$backend"
        $logPath = "$captureRoot/$backend.log"
        New-Item -ItemType Directory -Path $captureDir -Force | Out-Null
        'q' | & .\FruityPrime.exe -shellshot $captureDir -rhi $backend `
            -vkvalidation -fpscap 60 -noupdate -debuglog *> $logPath
        if ($LASTEXITCODE -ne 0) { throw "${backend}: inspect $logPath" }
        if (-not (Select-String -Path $logPath -SimpleMatch 'RHI screenshot/record RGB, vertical orientation and odd row width PASS')) {
            throw "Missing production export coverage: $logPath"
        }
        if (Select-String -Path $logPath -Pattern 'VUID|Validation Error|cancelled on device shutdown') {
            throw "Readback/validation failed: $logPath"
        }
    }
} finally {
    if ($hadPrefs) { [System.IO.File]::WriteAllBytes($prefsPath, $prefsBytes) }
    elseif (Test-Path -LiteralPath $prefsPath) { Remove-Item -LiteralPath $prefsPath }
    foreach ($name in $names) { [Environment]::SetEnvironmentVariable($name, $savedEnv[$name], 'Process') }
    Pop-Location
}
Write-Output "Captures and logs: $captureRoot"
```

レンダラー切替は前節の held / moving × 両開始 backend の手順を併用する。
各 process の試合内3切替を検査するもので、100-cycle stress の完了とは扱わない。

### 証拠と残作業

- `C:/tmp/gp/architecture-async-readback-final-build.log`: MSVC Release build PASS。
- `C:/tmp/gp/architecture-async-readback-final-ctest.log`: 12/12 PASS。
- `C:/tmp/gp/architecture-async-readback-final-conformance.log`: 両 backend の async / lifetime fixture PASS。
  issue / poll / map / resize / release の host / device wait delta=0、release / quota=0、validation error=0。
- `C:/tmp/gp/architecture-async-readback-burst-build.log` / `architecture-async-readback-burst-conformance.log`:
  recording slot の非ブロッキング admission と32-request burst を追加した最終ソースも PASS。
- `C:/tmp/gp/architecture-async-readback-shell-ui3-20261002-180048/`: 通常 shell loop は両開始 backend exit 0、
  screenshot / recording PNG 検査 PASS、試合前後の production hunter preview draw / reload PASS。
- `C:/tmp/gp/architecture-async-readback-switch.log` / `C:/tmp/switch-witness-20261002-180152/`:
  held / moving × 両開始 backend の4 process exit 0、各3 transition / 1 negative control PASS。
  held の新旧 bomb / impact 各2 particles と moving の死亡時の通常爆発を維持。
- 新しい MD の code block を実行した `C:/tmp/gp/architecture-async-readback-final-recipe.log` は PASS。
  `C:/tmp/async-readback-20261002-181039/` に conformance / 両開始 backend の shell loop と PNG を保存。
  最終ソースの切替手順も `C:/tmp/gp/architecture-async-readback-final-switch.log` で PASS。
  `C:/tmp/switch-witness-20261002-181106/` の全4 process が exit 0、各3 transition / 1 negative control PASS。

この gate は async API の ownership / backpressure と上記出力形式・本番接続を確認する。
全 format / mip / layer、任意の presentation ownership / recording ordering、実 driver fault、
長時間 stress と Phase H の100-cycle は残る。R10 の frame slot / probe、R17～R19 とレビュー全体は進行中。
Android と remote CI は未実行。Metal / D3D12 は将来対応。

## エラー情報の保持と切替失敗時の復旧（R17）

従来の acquire / present は loss を typed status に変換していたが、native code / message を
result に残さず、scene facade は native code=0 の新しい例外を作っていた。
また、switch の catch は window / swapchain の生成までで、shader / texture / scene の再構築中に
失敗すると元の renderer へ戻れなかった。復旧成功の dialog も、元の renderer を作り直す前に出していた。

### 実装

- API に依存しない `BackendFailure` を acquire / present result に所有し、backend / kind /
  native code / message を保持する。一般の native loss と logical surface close を区別し、
  元の native 情報がある場合は scene facade まで同じ typed error を伝える。
- OOM / unsupported / unknown は、temporary surface unavailability に変換せずそのまま伝える。
  startup の `SceneBackendUnavailable` も元の structured failure を保持できる。
- Vulkan の classification / Check を専用 `VulkanResult.hpp` に分離し、device / surface loss、
  host / device OOM、missing feature / extension / driver / format と unknown を分類する。
  present が loss を返した後は、新しい completion marker の失敗で元のエラーを置き換えない。
- OpenGL storage / fence failure は `GL_CONTEXT_LOST` を DeviceLost として分類する。
  OOM と unknown の元の native code は維持する。
- switch の replacement attempt は window / presentation / geometry / input binding /
  scene GPU resources / UI hooks まで含む。失敗時は incoming の部分 GPU resources と UI を
  current device / context がある間に解放し、swapchain → session → window の順に閉じてから、
  previous backend を同じ source / texture handles で作り直す。
- 復旧成功の通知は元の renderer の再構築が完了した後に出す。復旧側も失敗した場合は、
  `SceneBackendRecoveryFailed` が両方の元の例外を保持し、成功とは通知しない。
  部分 recovery resources を閉じ、通常実行では両方の失敗内容を dialog に出す。
- scene の switch release は device pointer を先に切り離す。idle が DeviceLost で失敗した場合は、
  通常 completion を偽装せず session の loss / shutdown boundary で native ownership を閉じる。
  他の idle error を DeviceLost として隠さない。
- GL の `glFinish` も native error を検査し、失敗時は completed serial を進めない。
  消費した context-loss code は scheduler に保持し、後の idle を成功と誤認しない。
  resource destructor の fence failure は最初の例外を保持して次の明示操作へ伝え、
  retirement を未証明のまま context shutdown まで保持する。
  RenderWindow / GL device の destructor は idle failure でも session と native ownership を閉じる。
  shutdown による所有権終了を通常 GPU completion として記録しない。

GPU source の復旧方式、texture handles、simulation の内容は維持する。texture の CPU backup は追加しない。
Metal / D3D12 の追加は行わない。

### CPU と実 GPU の検査範囲

`FruityPrime.RhiLifetime` は両 backend × DeviceLost / SurfaceLost / OOM / Unsupported / Unknown を
fake swapchain の実際の default acquire / present facade に通す。loss result と例外を往復させ、
backend / kind / native code / operation message を比較する。Ready / ResizeRequired /
TemporarilyUnavailable を fatal loss として扱わないことも検査する。
GL の合成 fence / finish failure では context loss が completion を進めず live fence を保持することを要求する。
消費した loss code の保持、destructor 用 marker の OOM、次の明示操作への元の例外の伝達、
未証明 retirement が通常 collect で破棄されず context shutdown で解放されることも検査する。
memory fixture でも context loss の分類・native failure counter を確認する。

新しい `FruityPrime.VulkanErrors` は本番と同じ classifier / Check に実際の VkResult enum を渡し、
未認識の値を含む11種類の loss / OOM / unsupported / unknown と成功を検査する。
Vulkan headers は必要だが GPU / loader call は使わない。

`FRUITY_SWITCHCHECK=1` と `FRUITY_SWITCHCHECK_FAILURES=1` は Alinos Perch の実際の
Pause → Settings → Renderer → Apply に、順に3つの **合成例外** を注入する。

| 段階 | 注入点 | エラー |
|---|---|---|
| 0 | incoming window 生成前 | Unsupported / startup wrapper、synthetic native code=321 |
| 1 | 本物の device / swapchain 生成後 | DeviceLost。Vulkan -4 / GL 0x0507 |
| 2 | shader / transient と最初の texture を本物の GPU に作成・upload した直後 | OOM。Vulkan -2 / GL 0x0505 |

fault は消費してから投げるので、previous backend の復旧は本物の session / window / GPU を使う。
これは driver 自体を壊す試験ではない。DeviceLost / OOM の numeric code は合成例外である。
コードの定義は [Vulkan return codes](https://docs.vulkan.org/spec/latest/chapters/fundamentals.html) と
[Khronos OpenGL errors](https://wikis.khronos.org/opengl/Error_Checking) を参照した。
各復旧後に元の backend / window geometry / visible window、同じ scene と進む simulation、
切替直前直後の world / health / bomb / particle / binding の一致、105/105 effect definitions、
新旧 Lockjaw / impact 各2 particles、終了時の texture source 解放と validation error=0 を要求する。

`FRUITY_SWITCHCHECK_FAILURES=double` は3回目の partial upload OOM に加え、
previous backend の recovery window 生成前に SurfaceLost / synthetic code=999 を投げる。
診断は期待した fatal boundary で loop を終了し、本物の scene / window を破棄してから、
両方の元の typed exceptions と texture source 解放、shutdown validation=0 を要求する。
それ以前の2回の復旧・transition witness・effect 表示も必須である。
fatal 後の描画を継続して PASS にする検査ではない。

診断時だけ failure reporter が modal dialog を検査ログへ置き換える。
通常実行では既存の dialog を使い、故障注入や reporter の置き換えは動かない。
GUI dialog の文字列・タイミングはコードと reporter の呼び出しを検査したが、OS dialog の手動クリックは対象外。

### Windows での再実行手順

MSVC / vcpkg、実 display、両 backend、Khronos validation、exe 隣の game paths が必要。
通常の regression は前節までの shell / switch 手順を使い、故障試験を次で追加する。
GPU process は順番に実行し、preferences / env を復元する。

```powershell
cmd /c tools\build\build-cpp.bat msvc Release
if ($LASTEXITCODE -ne 0) { throw 'Release build failed' }
ctest --test-dir tools/build/out/msvc-Release --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'CTest failed' }

$captureRoot = "C:/tmp/r17-fault-matrix-$(Get-Date -Format yyyyMMdd-HHmmss)"
$names = @('FRUITY_SWITCHCHECK', 'FRUITY_SHOT_ROOM', 'FRUITY_SWITCHCHECK_HOLD_ACTORS', 'FRUITY_SWITCHCHECK_WITNESS_SELFTEST', 'FRUITY_SWITCHCHECK_FAILURES')
$savedEnv = @{}
foreach ($name in $names) { $savedEnv[$name] = [Environment]::GetEnvironmentVariable($name, 'Process') }
Push-Location tools/build/out/msvc-Release
$prefsPath = Join-Path $PWD 'launcher.txt'
$hadPrefs = Test-Path -LiteralPath $prefsPath
$prefsBytes = if ($hadPrefs) { [System.IO.File]::ReadAllBytes($prefsPath) }
try {
    $env:FRUITY_SWITCHCHECK = '1'
    $env:FRUITY_SHOT_ROOM = 'AD2 ALINOS PERCH'
    $env:FRUITY_SWITCHCHECK_WITNESS_SELFTEST = '1'
    $env:FRUITY_SWITCHCHECK_HOLD_ACTORS = '1'
    foreach ($mode in @('recover', 'double')) {
        $env:FRUITY_SWITCHCHECK_FAILURES = if ($mode -eq 'double') { 'double' } else { '1' }
        $expected = if ($mode -eq 'double') { 2 } else { 3 }
        foreach ($backend in @('opengl', 'vulkan')) {
            $captureDir = "$captureRoot/$mode-$backend"
            $logPath = "$captureRoot/$mode-$backend.log"
            New-Item -ItemType Directory -Path $captureDir -Force | Out-Null
            'q' | & .\FruityPrime.exe -shellshot $captureDir -rhi $backend -vkvalidation -fpscap 60 -noupdate -debuglog *> $logPath
            $exitCode = $LASTEXITCODE
            Write-Output "$mode/$backend exit=$exitCode log=$logPath"
            Select-String -Path $logPath -Pattern 'switch failure|switch witness|lifecycle FAIL|VUID|Validation Error|could not'
            if ($exitCode -ne 0) { throw "Recovery failed: $logPath" }
            if (@(Select-String -Path $logPath -SimpleMatch '[switch failure] recovery=PASS; original typed error=PASS').Count -ne $expected) { throw "Missing recovery coverage: $logPath" }
            if (@(Select-String -Path $logPath -SimpleMatch '[switch witness] PASS;').Count -ne $expected) { throw "Missing witness: $logPath" }
            if ($mode -eq 'double' -and @(Select-String -Path $logPath -SimpleMatch '[switch failure] double failure PASS;').Count -ne 1) { throw "Missing fatal boundary coverage: $logPath" }
            if (Select-String -Path $logPath -Pattern 'VUID|Validation Error|lifecycle FAIL|recovery=FAIL;|restored FAIL') { throw "Validation failed: $logPath" }
        }
    }
} finally {
    if ($hadPrefs) { [System.IO.File]::WriteAllBytes($prefsPath, $prefsBytes) }
    elseif (Test-Path -LiteralPath $prefsPath) { Remove-Item -LiteralPath $prefsPath }
    foreach ($name in $names) { [Environment]::SetEnvironmentVariable($name, $savedEnv[$name], 'Process') }
    Pop-Location
}
Write-Output "Captures and logs: $captureRoot"
```

再実行の exit 0 だけでは判定せず、各 process の recovery / witness と double failure の件数を確認する。
Khronos validation coverage はログの validation=1 も確認する。held は任意の gameplay 入力の全検査ではない。

### 証拠と残作業

- 最後の teardown 修正後: `C:/tmp/gp/architecture-r17-teardown-final-build.log` は MSVC Release PASS、
  `architecture-r17-teardown-final-ctest.log` は追加した native-dispatch failure 検査を含め13/13 PASS。
  `architecture-r17-teardown-final-conformance.log` は両 backend の conformance / async / session lifetime PASS。
- `C:/tmp/gp/architecture-r17-teardown-final-matrix.log` は MD の故障検査手順を再実行して PASS。
  `C:/tmp/r17-fault-matrix-20261002-190039/` の全4 process が exit 0、復旧・witness と
  二重失敗時の error preservation / source release / shutdown validation=0 を確認した。
- `C:/tmp/gp/architecture-r17-teardown-final-normal.log` /
  `C:/tmp/switch-witness-20261002-190220/` は同じ最終コードで通常検査手順を再実行して PASS。
  held / moving × 両開始 backend の全4 process が exit 0、各3 transition と negative controls が PASS。
  held の全切替で impact / 新旧 Lockjaw 各2 particles を保持し、validation error=0 と source release を確認した。
- `C:/tmp/gp/architecture-r17-final-build.log`: MSVC Release build PASS。
- `C:/tmp/gp/architecture-r17-final-ctest.log`: 13/13 PASS。
- `C:/tmp/gp/architecture-r17-final-conformance.log`: 両 backend の conformance / async / session lifetime PASS。
- `C:/tmp/gp/architecture-r17-final-recovery.log` / `C:/tmp/r17-recovery-20261002-183740/`:
  両開始 backend exit 0。各3段階の recovery / original typed error / transition witness PASS。
  partial texture upload failure からの復旧を含み、effect / geometry / simulation と終了時 source 解放も PASS。
- `C:/tmp/gp/architecture-r17-final-double-recovery.log` /
  `C:/tmp/r17-recovery-double-20261002-183820/`: 両開始 backend exit 0。
  各2回の復旧と witness、最後の double failure / 両方の typed errors / scene release /
  shutdown validation=0 を確認。
- 最初の double fixture build は `architecture-r17-recovery-build.log` で FAIL。
  後で宣言されていた診断用 weak model reference を前へ移し、build2 / 最終 build で修正を確認した。
- 最終 MD の code block を実行した `C:/tmp/gp/architecture-r17-final-documented-recipe.log` は PASS。
  `C:/tmp/r17-fault-matrix-20261002-184553/` の recover / double × 両開始 backend 全4 process が exit 0。
  recover は各3回、double は各2回の復旧・witness と1回の期待した fatal boundary を確認。
  13/13 CTest、PNG と mode ごとに分けたログ、source 解放、validation error=0 を保存した。
- `C:/tmp/gp/architecture-r17-normal-switch.log` / `C:/tmp/switch-witness-20261002-183954/`:
  故障なしの held / moving × 両開始 backend 全4 process の通常切替・witness・effect lifetime が PASS。
- `C:/tmp/gp/architecture-r17-final-shell-regression.log` / `C:/tmp/async-readback-20261002-184707/`:
  通常 shell loop の両開始 backend exit 0、試合前後の production hunter preview と
  screenshot / recording の全 pixel / RGB / orientation / odd-row gate が PASS。

この gate は共通 error contract と上記 fault / recovery boundary を検査する。
本物の GPU driver reset / TDR / native OOM、あらゆる allocation site、runtime loss からの自動再開、
presentation ownership の全ケースと100-cycle stress の保証ではない。
実 driver fault の coverage は R19 / Phase H の残作業として区別する。
R10 の frame slot / probe、R18 の debug labels / timestamps、R19 の残項目とレビュー全体は進行中。
Android build / 実機と remote CI は未実行。Metal / D3D12 は将来対応。

## 共通 GPU 診断と本番 FPS 計測（R18）

### 共通 API と native 実装

`GpuDiagnostics.hpp` が debug label と timestamp の契約を持つ。
`CommandList` に begin/end scope・marker・timestamp initialize/write、`GraphicsDevice` に query set 作成を追加した。
OpenGL と Vulkan は同じ conformance fixture からこれらを利用する。
debug label の有無は timestamp の有無と独立した capability とし、未対応の label は no-op、
timestamp が未対応／容量不足なら factory は null を返す。
native API の型を共通 header に置かない。

- OpenGL: `GL_ARB_timer_query` / OpenGL 3.3以降の timestamp counter を確認し、
  `glQueryCounter`・availability query・64-bit result を利用する。
  label は `GL_KHR_debug` / OpenGL 4.3以降と関数の存在を確認する。
- Vulkan: 選択した graphics queue の `timestampValidBits` と device の `timestampPeriod` を使い、
  query pool reset / `vkCmdWriteTimestamp2` / availability付き64-bit result を利用する。
  debug utils scope は内部 command buffer の送信境界で閉じ、次の buffer で開き直す。
  timestamp は ALL_COMMANDS stage で記録する。

一度限りの query set は1～64個の query を所有する。
initialize と各 index の write はそれぞれ一度だけで、未送信／実行中の pool の reset を受け付けない。
`ReadResults` は全 index の write と native availability が揃うまで Pending。
Pending / Cancelled では出力先に触らず、Ready の場合だけ全64-bit値をコピーする。
結果確認は command submission・host wait・device-wide wait を挿入しない。
duration は有効 bit 数の wrap と tick period を考慮するが、counter の一周を越える期間や
異なる GPU の時計同士の比較を扱う API ではない。

native set の容量は device ごとに8個。
frontend wrapper を解放しても native retirement が終わるまでは予約を保持する。
device shutdown が pool/query を閉じた後、残った wrapper は Cancelled を返して context にアクセスしない。
live object 統計には解放待ちを含む `TimestampSets` を追加した。
label は1～255 bytes、NULなし、有限な0～1の色、nestingは32まで。
名前はUTF-8を caller が渡す契約で、文字コードの完全な検査は追加していない。

API の根拠:
[vkCmdWriteTimestamp2](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdWriteTimestamp2.html)、
[vkGetQueryPoolResults](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetQueryPoolResults.html)。

### FPS / frame time

ユーザー報告の Vulkan 約900 FPS / OpenGL 約200 FPSを同条件で比較できるよう、
`-fpsmeasure FILE.csv` を本番 window に接続した。
HUD の FPS Counter は Off のままで利用できる。
FPS・平均frame間隔・p50/p95/p99・CPU loop/presentation 時間・描画条件を約1秒ごとに記録する。
`-gpuprofile` を追加すると64描画に1回、world/scene/HUD を GPU 上で測る。
後段の Skia launcher overlay と presentation の GPU 時間はこの値に含まない。
モデル・テクスチャの CPU pixel backup は追加していない。

設定変更、切替、scene/room、解像度、pause、focusの変更ごとに区切り、最初の2秒を除外する。
CSV用の条件文字列を毎フレーム作らず、条件が変わったときだけ生成する。
百分位の storage は1区間8192個に制限する一方、総frame数・経過時間の FPS集計は正確に続ける。
測定を指定しない通常起動では時計取得・query 作成・CSV出力を行わない。
`-fpsmeasure` と明示的 `-fpscap` の組は一時的な上限として保存操作から保持し、
`unlimited` は計測中だけループ上限を外す。通常の設定の Unlimited=500 は維持する。

詳細な起動例、列の意味、設定を復元する自動テストは
[FPS計測手順](Fruity-Prime-FPS-Measurement.md) に記載した。
描画コールバックの完了速度であり、モニターの表示frame数を測るものではない。
一時的な presentation 停止、capture のI/O、OS/driver の変動も区間に含まれ得る。
GPU query の負荷があるため、FPS-only の速度比較と GPU / validation 検査を分ける。

### 試験で見つかった問題

- 最初のCSV試験では Settings apply が CLI の500 FPSを display capへ戻した。
  measurement用 overrideを分離し、以後の保存・renderer切替でも上限を保持するようにした。
- Vulkan session shutdown fixture が、既に copy を recording 中の list に `Begin` を呼んでいた。
  その copy に未完の timestamp を続けて記録する fixture に修正した。
- uncapped 試験で、従来の240 **draw callbacks** 待ちが1秒未満になり、
  8人の spawn burst が共通200粒子のpoolを使い切った状態で one-shot probe を置いていた。
  native effect clock・emissionを追跡し、spawn count=1でも空きpoolがないことを確認した。
  C#と同じ200粒子の制限を維持し、fixture を240 **simulation ticks** 待ちに直した。
  長いFPS測定区間は新しい着弾probeを置く前に入れ、自然な寿命切れを切替失敗にしない。
  original bomb と world/clock/health/position/particle/texture binding witness の条件は緩めていない。
- GL query/storage の `glGetError` が context loss を消費しても、completion scheduler に
  最初の loss を保持するようにした。後の `glFinish` が errorなしに見えても完了と判定しない。
  native-dispatch CPU testで、このerror消費境界を検査した。

### 確認結果（2026-10-02、Windows / RTX 5070 Ti）

- `C:/tmp/gp/architecture-r18-final-build.log`: MSVC Release build PASS。
- `C:/tmp/gp/architecture-r18-final-ctest.log`: 13/13 PASS。
  bit wrap、fractional tick、one-shot write/reset、native reservation、FPS総数と有界percentileを含む。
- `C:/tmp/gp/architecture-r18-final-conformance.log`: 両 backend PASS。
  nested labels/marker、1 MiB GPU copyを挟んだ3 timestamps、partial output保持、
  結果確認の host/device wait増加0、8 native setsの制限、解放後の native count=0を検査した。
  GPU transferは OpenGL 82,016 ns / Vulkan 88,480 ns（この1回の値）。
  未完のqueryを持つshutdown/recreateも既存8回のsession fixtureに加えた。
- GPU計測付き: `C:/tmp/r18-fps-20261002-195744/`。
  両開始 renderer で各3回の設定保存・resume・world witness PASS、全bomb/effect lifecycle PASS、exit0。
  pauseなし・focusあり・room113の行で、両 backend の GPU sample >0と上限unlimitedを確認した。
- FPSのみ: `C:/tmp/r18-fps-20261002-200029/`。
  両開始 renderer で同じ切替 gate PASS、HUD FPS Counter=0、GPU sample=0・GPU時間空欄を確認した。
- Vulkan validation付き: `C:/tmp/r18-fps-20261002-200156/opengl.log`。
  OpenGL開始からfront画面1回と試合3回の切替、GPU scene sample、world witness、lifecycleがPASS。
  VUID / validation error は0件。OBS / Bandicam の古いlayer version警告はerrorと区別した。

FPS-only の条件は Alinos Perch（room113）、spawned Sylux +7 actors、bot controlsをfixtureで保持、
2560×1439、resolution scale100、cel0 / fog1、pause0 / focus1、HUD FPS Counter0、
unlimited、present requested/actual=Immediate（0/0）、Vulkan validationなし。
各開始runの対象行を total frames / total seconds で集計した値:

| 開始renderer | 測定renderer | 行数 | FPS | 平均frame間隔 |
|---|---|---:|---:|---:|
| OpenGL | OpenGL | 9 | 306.7 | 3.261 ms |
| OpenGL | Vulkan | 8 | 995.1 | 1.005 ms |
| Vulkan | OpenGL | 11 | 277.0 | 3.610 ms |
| Vulkan | Vulkan | 8 | 960.0 | 1.042 ms |

別runのGPU scene sample平均は OpenGL 1.360 / 1.576 ms、Vulkan 0.208 / 0.201 ms。
GPU計測とFPS-onlyは同時刻・同じframeの値ではない。
shellshot のcaptureを含む短いfixtureのため、通常操作で常にこの速度になる保証や
改善率の証明ではない。ユーザー報告と同方向の差を再現したが、差の原因となるpass／driver待ちの
特定とOpenGL最適化はこの変更の完了条件にはしていない。

R18のWindows OpenGL / Vulkan共通診断・本番計測を実装した。
R10のframe slot / probe分離、R19の全format / recording / presentation ownership / 100-cycle stress、
R20とレビュー全体は進行中。Android build / 実機、remote CIは未実行。
Metal / D3D12実装、macOS実機は今回の対象外。

## 不正なリソース所有者と残った view / command の検査（R19）

`RhiOwnershipCheck` を両 backend の `-rhiconformance` に追加した。
GPU を持たない公開 frontend resource を渡し、35個の API 呼び出しが `invalid_argument` で
拒否され、拒否によって submission serial が進まないことを検査する。
空の retain、buffer / texture upload・read・copy・transition、view / binding / pipeline作成、
vertex/index binding、sampled texture、rendering target、同期・非同期 color readback を含む。
native deviceを2つ同時に作る fixture ではない。

この検査に合わせ、次を修正した。

- Vulkan の公開 resource 入口で、不正な backend の object に先に `static_cast` を行う経路を除いた。
  native class、device state、終了済み session を確認してから native handle を扱う。
  sampled texture / sampler の対、Sampled usageも GPU操作前に確認する。
- OpenGL の retain、transition、texture copy、sampled binding等で所有者を確認する。
  sampled binding は imageとsamplerを検査してから GL state を変える。
  終了済み command list の GPU入口は、current GL context に触る前に拒否する。
- OpenGL のtextureに小さな lifetime tokenを追加し、viewはweak tokenを保持する。
  Vulkan のviewが既に持っていたweak lifetimeもGPU入口で検査する。
  これにより view wrapper が texture wrapper より長く残っても、
  `CanRender` / `DepthBits` / `BeginRendering` / `ReadColor` / async readback / binding作成では
  破棄された texture の参照を取得する前に拒否する。
- OpenGL の既存 binding set も bind時に全resourceを検査する。
  両 backend の実 shader / pipeline fixtureで、texture解放後に残ったview/setが
  descriptorとして再利用されず、拒否によってsubmissionが増えないことを検査した。
- 既存8回のsession終了・再作成fixtureに、終了済みcommandのviewport変更と
  texture unbindを追加した。incoming sessionへの影響を拒否する。

modelのpixelコピーやCPU描画は追加していない。lifetime tokenはmetadataのみ。
view / binding setへ渡すfrontend wrapper自体の寿命はcallerの責任であり、
解放済みwrapperの生ポインターを渡しても安全になるという契約ではない。
ここで検査するのは、生存しているview/set/command wrapperから、終了したnative resourceや
終了したtexture/sessionへアクセスしないことである。

### 再現方法と結果

```powershell
& tools/build/build-cpp.bat msvc Release
ctest --test-dir tools/build/out/msvc-Release --output-on-failure
Push-Location tools/build/out/msvc-Release
try {
    'q' | & .\FruityPrime.exe -rhiconformance -noupdate *> C:/tmp/rhi-ownership.log
    if ($LASTEXITCODE -ne 0) { throw 'RHI ownership conformance failed' }
    Select-String C:/tmp/rhi-ownership.log -Pattern 'resource ownership|session ownership|VUID|FAIL'
} finally { Pop-Location }
```

- `C:/tmp/gp/architecture-r19-ownership-closed-build.log`:
  MSVC Release build PASS。
- `C:/tmp/gp/architecture-r19-ownership-closed-ctest.log`: 13/13 PASS。
- `C:/tmp/gp/architecture-r19-ownership-closed-conformance.log`:
  両backendで新しいownership gateと既存transfer / pixel / descriptor / readback / timestampがPASS。
  解放時のhost/device wait増加0、explicit idle後のnative countがbaselineへ復帰、retired=0。
  両backend8回のshutdown/recreateもPASS、VUID / validation errorは0件。
- `C:/tmp/r18-fps-20261002-224430/`:
  [FPS計測手順](Fruity-Prime-FPS-Measurement.md) のFPS-only切替fixtureを両開始backendで再実行。
  それぞれ試合中3回の settings apply / resume、world witness、bomb/effect lifecycle、
  HUD FPS Counter=0でのCSV記録、両backendのFPS行がPASS、exit0。
- `C:/tmp/gp/architecture-r19-ownership-delivery-build.log`:
  guardの改行と重複した検査の整理後もMSVC Release build PASS（挙動の変更なし）。

この区切りはR19のresource ownership部分の拡張である。
format / subresource・recording contract・presentation ownership・100-cycle stressは引き続き必要。
R10のframe slot / probe分離、R20とレビュー全体も進行中。

## 内部送信をまたぐ Begin / End（R19）

`CommandList::Begin / End` は caller が開いた区間を表す。
別の command list、frame end、readback による内部送信ではその区間を終了しない。
二重 Begin、Begin 前／End 後の End は `logic_error` で拒否し、拒否時に送信しない。

Vulkan はこの判定に native command buffer の `_recording` を使っていた。
内部 flush 後はそれが false になるため、二重 Begin が受理され、一方で正常な End が失敗した。
区間の判定を既存の `_autoRestart` に揃え、End は既に送信済みの区間も終了できるようにした。
OpenGL にも caller の区間を保持し、同じ Begin / End の拒否を実装した。

Vulkan の UI と bounded upload stream は作成時に一度 Begin する。
送信後の継続は native buffer の再開に任せる。UI の毎回の Begin と例外の握りつぶしを除いた。
window commands も作成時に Begin し、成功した list を所有する。
Vulkan End の既存の同期互換動作は維持する。frame end / presentation の通常の flush に
新しい wait を追加していない。モデル pixel の複製も追加しない。

`-rhiconformance` は両 backend に同じ fixture を実行する。
2つの list を交互に使い、別 list の送信・frame end をまたいで64 bytesの GPU copy を行い、
最終データを照合する。6件の不正な Begin / End（debug labels 対応時）、
拒否時の submission serial 不変、label の継続、空の区間、解放後の live / retired=0を検査する。
既存 fixture は区間終了後の同期 readback と upload stream の再開も検査する。

確認結果（Windows / RTX 5070 Ti）:

- MSVC Release build PASS: `C:/tmp/gp/architecture-r19-recording-build2.log`。
- CPU CTest 13/13 PASS: `C:/tmp/gp/architecture-r19-recording-ctest.log`。
- 共通 GPU conformance PASS: `C:/tmp/gp/architecture-r19-recording-conformance2.log`。
  両 backend の interval / ownership / diagnostics / async readback / draw / session lifetime が PASS。
  Vulkan validation errors=0、release=0。
- FPS-only の両開始 backend の切替 PASS: `C:/tmp/r18-fps-20261002-230815/`。
  Alinos Perch、spawned player、各3回の world witness、bomb / particle / lifecycle gate が PASS。
  同じ計測条件で OpenGL 270.0～303.2 FPS、Vulkan 955.2～992.0 FPS。
  HUD FPS Counter=0、GPU sample=0。capture を含む同じ fixture の値である。
- validation と GPU profile を有効にした3回の切替も PASS:
  `C:/tmp/r18-fps-20261002-230940/opengl.log`。Vulkan validation errors=0、
  world witness / bomb / particle / lifecycle gate が PASS。速度比較には使わない。

この区切りは Begin / End の状態判定の統一である。
区間外の全 mutation の拒否、rendering 中の transfer / barrier と内部 readback の契約、
全 format / subresource、presentation ownership、100-cycle stress は引き続き必要。
R10 / R20 とレビュー全体も進行中。Android / macOS の実動作、remote CI は未実行。
Metal / D3D12 は今回追加しない。

## Recording mutation と描画中の transfer（R19）

OpenGL / Vulkan の recording mutation は Begin 前／End 後に `logic_error` で拒否する。
対象は rendering、pipeline / viewport / scissor / vertex / index / binding / stencil、draw、
3種の copy、buffer / texture transition、sampled texture、current-color copy、debug labels / timestamps。
同期 `ReadColor` と非同期 `EnqueueReadColor` は、区間外にも使える診断 API として維持する。
Draw / DrawIndexed と current-color copy は rendering 区間も要求する。
EndRendering は recording 中なら複数回呼べる。新しい BeginRendering は前の区間を置き換える。

Vulkan の transfer / barrier は native rendering instance を終了してから記録し、
logical target と描画内容を保持する。保留中の clear を一度だけ先に実行し、後続 draw は LOAD で再開する。
Materialize 中の attachment transition は clear を再帰的に materialize しない。
保留 clear が対象 texture を暗黙に transition した場合は、実際の layout をその後の barrier に使う。
この要件は [vkCmdCopyBuffer](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdCopyBuffer.html) の
renderpass 制約と [vkCmdPipelineBarrier2](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdPipelineBarrier2.html)
の dynamic rendering 制約に合わせている。copy / barrier のために新たな submit / wait は追加しない。

SetScissor は両 backend で clipping を有効にする。次の BeginRendering が render area に戻す。
OpenGL の Begin は recording の状態だけを開き、depth test 等の native draw state を変更しない。
draw state は pipeline と rendering setup が設定するため、診断 copy の Begin も描画へ干渉しない。
OpenGL の非同期 buffer readback も内部 list を Begin してから copy する。
native stream の送信は EnqueueBytes が一度行い、同期待ちは追加しない。

GPU fixture は以下を同じ API で検査する。

- 新しい list の Begin 前と End 後の mutation 拒否、rendering 区間外の draw、
  rendering 中の timestamp 初期化拒否。現在の実機では両 backend 各52件。
  `invalid_argument` / `out_of_range` は成功扱いにせず、recording の `logic_error` と
  拒否時の submission serial 不変を要求する。
- 保留 clear 後の current-color copy を GPU texture → buffer で確認し、黒・alpha255を照合する。
- 左半分を Draw / DrawIndexed で描き、buffer barrier / copy と image upload / readback を挟んで
  右半分を描く。左右の sampler pixel を照合し、再開時に左側が clear されないことを検査する。
- 区間終了後の同期 color readback、native error=0、既存の async readback / owner teardown と
  解放後の live / retired=0も引き続き要求する。

current-color copy は OpenGL の active unit の texture binding を変更していたため、
後続 draw の sampler が別 texture を読んでいた。元の texture / read framebuffer を保存・復元した。
buffer-to-texture copy も texture / unpack buffer を、texture-to-buffer copy も pack buffer を復元する。
CPU texture backup や CPU 描画への移行は追加していない。

確認結果（Windows / RTX 5070 Ti）:

- MSVC Release PASS: `C:/tmp/gp/architecture-r19-mutation-build7.log`。
- CPU CTest 13/13 PASS: `C:/tmp/gp/architecture-r19-mutation-final-ctest.log`。
- 共通 GPU conformance PASS: `C:/tmp/gp/architecture-r19-mutation-conformance5.log`。
  両 backend の52件の拒否・GPU pixel・ownership / diagnostics / async readback / lifetime が PASS。
  Vulkan validation errors=0、release=0。
- FPS-only の両開始 backend の切替 PASS: `C:/tmp/r18-fps-20261002-233454/`。
  同じ試合で各3回の world witness、bomb / particle / lifecycle gate とCSV条件が PASS。
- validation / GPU profile 付きの3回切替 PASS: `C:/tmp/r18-fps-20261002-233844/opengl.log`。
  Vulkan validation errors=0、world / bomb / particle / lifecycle gate が PASS。

R19 / Phase H の全体は進行中。native command buffer の内部再開後の generic draw binding の復元、
全 format / subresource と state tracking、presentation ownership、100-cycle stress、
R10 の frame slot / probe分離と R20 の確認は残る。Android / macOS実動作、remote CI は未実行。

## 内部再開と別 command list の描画後の binding 復元（R19）

Vulkan の generic draw は、診断 readback、frame submission、別 command list による送信で
native command buffer が再開すると、vertex / index buffer と descriptor の設定を失っていた。
logical recording interval に pipeline・buffer binding・binding set の snapshot を保持し、
再開後に同じ描画状態を復元する。descriptor pool は command slot が所有し、
GPU の送信完了後にだけ reset する。再開時は新しい pool generation の set を割り当て、
以前の native buffer の descriptor を流用しない。view の作り直しでも再割り当てする。

OpenGL は同じ context の別 command list が、framebuffer・pipeline・viewport・scissor・
UBO・texture・sampler を変更できる。現在の native state を設定した list を device が記録し、
別 list の描画や texture upload 後は、元の list の次の draw で保存した状態を復元する。
同じ list が続けて描く通常経路では状態全体を再設定しない。

binding set は layout の記述を所有し、適用済みの set は command list が snapshot を持つ。
元の layout は set 作成後、元の set は適用後に解放しても描画できる。
buffer / view / sampler の実体は引き続き caller が保持する契約とし、wrapper の寿命を
weak token で確認してから参照する。保存した binding が解放済み実体を指す場合は
GPU 操作・送信前に拒否する。モデルの pixel コピー、CPU 描画、追加の wait は導入しない。

共通 fixture は、同じ画像を nearest / linear の2つの sampler で描き、
前半の readback 後に別 list で別 target・別 material・色書き込みなしの pipeline・
小さい viewport / scissor を使って実際に draw する。frame も切り替えた後、
元の pipeline / vertex / index / sets / viewport を再指定せず後半を描く。
前半の保持・後半の正しい色・異なる sampler の同時利用を pixel で判定する。
buffer / view / sampler wrapper を解放した後の SetBindingSet / Draw の計6件も拒否を検査する。

確認結果（Windows / RTX 5070 Ti）:

- MSVC Release PASS: `C:/tmp/gp/architecture-r19-binding-build6.log`。
- CPU CTest 13/13 PASS: `C:/tmp/gp/architecture-r19-binding-ctest6.log`。
- 共通 GPU conformance PASS: `C:/tmp/gp/architecture-r19-binding-conformance6.log`。
  両 backend の内部再開・別 list の draw・source set / layout 解放・6件の実体寿命拒否が PASS。
  既存 ownership / async readback / GPU diagnostics / 8回の session teardown も PASS、
  Vulkan validation errors=0、最終 release=0。
- FPS-only の両開始 backend の切替 PASS: `C:/tmp/r18-fps-20261003-002012/`。
  Alinos Perch、出現済み Sylux と7 bots、2560×1439、HUD FPS Counter Off、cap unlimited。
  同じ試合で各3回の world witness・bomb / particle / lifecycle gate と CSV 条件が PASS。
  pause=0 / focus=1 の全記録行は OpenGL 20行の平均284.63 FPS、Vulkan 16行の平均979.30 FPS。
  最小～最大は OpenGL 185.63～313.05、Vulkan 848.18～1005.78。速度改善の主張には使わない。
- validation / GPU profile 付きの3回切替 PASS: `C:/tmp/r18-fps-20261003-002155/opengl.log`。
  Vulkan validation errors=0、world / bomb / particle / lifecycle gate が PASS。
  validation ON の FPS は上記の速度比較に混ぜない。

R19 / Phase H の全体は進行中。全 format / subresource と state tracking、presentation ownership、
100-cycle stress、R10 の frame slot / probe 分離と R20 の確認は残る。
Android / macOS実動作、remote CI は未実行。

## Instance / physical-device feature probe の独立（R10 / R14）

`VulkanFeatureProbe` を専用 header / translation unit に分離した。
native query は loader / instance extension / layer と physical device の properties・features・
formats・queue presentation support・memory heaps・device extensions を snapshot にする。
query dispatch には logical device生成・queue取得の関数を含めない。

snapshot からの適格性判定と device選択は Vulkan API を呼ばない pure policy とした。
API 1.3、swapchain、dynamic rendering、synchronization2、timeline semaphore、RGBA8 の
color / sample / transfer、D32 attachment、graphics / present queues を必須として維持する。
任意の validation / debug utils / memory budget / surface・swapchain maintenance と
portability、anisotropy / wireframe / depth clamp / timestamps も結果に保持する。
validation を有効にする場合だけ debug reporting を必須とする既存動作を保つ。

`ProbeFinding` は requirement / required / supported / detail を持つ。
不適格な device の理由を最初の不足条件で打ち切らず、各候補の結果として残す。
Contextはsnapshotのquery、policyによる選択、選択結果によるlogical device生成を調停する。
passive path は選択後にreturnし、実検査で device / graphics queue / present queue がnullであることを確認する。
native query がsurface lossなどで失敗した場合は、非対応featureへ置き換えずtyped errorを保持する。

CPU検査は必須条件の各不足、複数理由の保持、別queue family、unified familyの優先、
queueCount=0、任意機能なし、maintenanceのextension / feature / instance前提、
device-local heapだけのscore、discrete GPU優先、同点時の列挙順、全候補不適格、
heap合計のoverflow、loader 1.0 / 1.2、validation reporting前提を検査する。
fake native dispatchで`VK_INCOMPLETE`の再列挙、surface有無のquery経路、
旧API deviceへの1.3 feature chain回避、surface lossの分類も検査する。

追加で既存の `-vulkanresourcecheck` に、`FlushDevice` / `EndFrame` を `End` とみなす
古いfixtureが残っていることを発見した。64回のchurnを1つのlogical recording intervalにし、
各cycleの非同期送信・resize・解放を維持する。最終境界でだけintervalを閉じる。
通常churnのdevice-wide waitを0とするgateは維持した。

確認結果（Windows / RTX 5070 Ti）:

- MSVC Release PASS: `C:/tmp/gp/architecture-r10-probe-final-build.log`。
- CPU CTest 14/14 PASS: `C:/tmp/gp/architecture-r10-probe-final-ctest.log`。
  `FruityPrime.VulkanFeatureProbe` はnative Vulkan libraryをlinkせず、GPUなしで実行できる。
  `dumpbin /DEPENDENTS`でもVulkan DLL依存がないことを確認: `C:/tmp/gp/architecture-r10-probe-dependencies.log`。
- 共通 GPU conformance PASS: `C:/tmp/gp/architecture-r10-probe-final-conformance.log`。
  OpenGL / Vulkan、pixel、ownership、session再作成、async readback、diagnosticsを確認。
- `-vulkancheck` PASS: `C:/tmp/gp/architecture-r10-probe-foundation.log`。
  passive候補1、logical-device=0 / queues=0、active admission後のvalidation / teardownがPASS。
- `-vulkanpresentcheck` PASS: `C:/tmp/gp/architecture-r10-probe-present.log`。
  resize / fullscreen / minimize / restore / FIFO・Mailbox、validation errors=0。
- `-vulkanpresentfallbackcheck` PASS: `C:/tmp/gp/architecture-r10-probe-present-fallback.log`。
  maintenance無効のadmissionとpresentation、古いswapchainの解放、validation errors=0。
- `-vulkanresourcecheck` PASS: `C:/tmp/gp/architecture-r10-probe-resource2.log`。
  64回のchurn / upload reuse / admission / descriptor frame再利用、最終live=0 / retired=0、
  churn device-wide waits=0、validation errors=0。
- validation / GPU profile付きの試合中3回切替 PASS: `C:/tmp/r18-fps-20261003-003919/opengl.log`。
  world / bomb / particle / lifecycle gate、Vulkan validation errors=0。

R10 の frame slot の所有・調停、R19の全format / subresource / state tracking / presentation ownership、
Phase Hの100-cycle stress、R20の確認とレビュー全体は進行中。
Android / macOS実動作、remote CIは未実行。

## Command recording slot の所有・再利用の分離（R10）

`VulkanCommandSlots` を独立した header / translation unit とし、command listごとの2枠の
native command pool / buffer / fence、upload arena、descriptor allocatorを所有させた。
各枠は Idle / Recording / Executable / Pending を持ち、submit後にだけ次の枠へ進む。
queue submit / timeline serialは既存の`VulkanFrameScheduler`へ委譲する。
RHIのlogical Begin / End、描画状態の再適用、rendering / transfer commandの生成は
`VulkanCommandList`に残し、native bufferの交代をlogical Endとみなさない。

再利用時はfence完了とsubmission serialの完了を確認してから、upload / descriptorをresetし、
fenceとcommand poolをresetする。未完了の枠だけ従来の2秒ごとのfence waitを行う。
既にsignaledならblocking waitを呼ばない。async readbackのadmission / completion pollは
待機・reset・submitを行わず、両枠が使用中なら新しいreadbackを保留する。
通常のcloseはこのstreamの送信済み枠をdrainし、sessionが既にidleまたはdevice loss境界を
通ったcloseでは追加待機を行わない。初期化途中の失敗でも作成済みownerを解放する。
native wait失敗時のcloseは既存のbest-effort方針を維持する。実GPUのdevice lossは今回注入していない。

CPU fake dispatchは64回の交代、2枠使用中のbackpressure、timeout再試行、未送信記録を残すdrain、
fenceだけ完了しtimelineが未完了の場合のreset拒否、signaled時の待機省略、途中のpool / buffer /
fence / allocator初期化失敗、submit失敗、fence queryのDeviceLost分類、destructorのdrain、
session idle後のcloseとclose後の操作拒否を検査する。descriptor poolやcommand poolを
pending中にreset / destroyした場合と、残存native handleも失敗にする。

確認結果（Windows / RTX 5070 Ti）:

- MSVC Release PASS: `C:/tmp/gp/architecture-r10-slots-final-build.log`。
- CPU CTest 15/15 PASS: `C:/tmp/gp/architecture-r10-slots-final-ctest.log`。
  新しい`FruityPrime.VulkanCommandSlots`はfake dispatchを使い、native Vulkan libraryをlinkしない。
- 共通GPU conformance PASS: `C:/tmp/gp/architecture-r10-slots-final-conformance.log`。
  OpenGL / Vulkanの描画、binding復元、ownership、session再作成、async readbackを確認。
- `-vulkanresourcecheck` PASS: `C:/tmp/gp/architecture-r10-slots-resource.log`。
  64回のchurn / upload reuse / admission / descriptor再利用、最終live=0 / retired=0、
  通常churnのdevice-wide waits=0、validation errors=0。
- Alinos Perchの試合中3回切替 PASS: `C:/tmp/r18-fps-20261003-010632/opengl.log`。
  Settings保存・resume、8 actors、ボムと粒子のtexture binding、world / simulation / windowの
  witnessと負の対照がPASS。GPU timestamp計測を併用し、Vulkan validation errors=0。
  validation ON / cap 240の回帰検査なので、FPSの速度比較には混ぜない。

R10の最終責務監査、R19の全format / subresource / state tracking / presentation ownership、
Phase Hの100-cycle stress、R20の確認とレビュー全体は進行中。
Android / macOS実動作、remote CIは未実行。Metal / D3D12は将来対応のまま。

## 明示的transition / copyの状態契約を統一（R6 / R19）

OpenGLの`Transition`はこれまで開始stateを追跡せず、before == afterを無条件に受け入れ、
bufferにColorAttachment / depth / Presentを指定する操作も受け入れていた。
共通`IsValidResourceState`にも未知bitとwrite・readの同時指定を通す経路があった。
Vulkanと同じfixtureで誤ったstateを拒否できるよう、次を修正した。

- 共通policyで未知bitを拒否し、複数read stateは許可、write stateはexclusiveとする。
  `ShaderWrite`自体はstorageのread / writeを表す。
  この制約は将来のAPIにも写せる共通契約として採用する。
  [D3D12の公式state制約](https://learn.microsoft.com/en-us/windows/win32/direct3d12/using-resource-barriers-to-synchronize-resource-states-in-direct3d-12#readwrite-resource-state-restrictions)も確認したが、D3D12 backendの実装は追加しない。
- buffer / textureのstate種別検査を共通policyにし、両backendの初期stateと明示的transitionで利用する。
  不正な初期stateはnative storage生成前に拒否する。
- OpenGLのbuffer / texture wrapperがstateを持ち、beforeの不一致、同一state、Undefinedへの
  transition、resource種別違いをnative操作前に拒否する。
  明示的buffer / image copyは両backendともCopySrc / CopyDstとtransfer usageを要求する。
- OpenGLのconvenience操作も状態を更新する。rendering attachment、current-color copy、
  GPU-only buffer upload、texture upload、storage再確保が対象。
  CPU buffer writeは既存stateを維持し、texture uploadは既存の確定stateを維持する。
  同じextentへのresizeは画像・stateを維持し、extent変更後のtextureはUndefinedになる。
- OpenGLのasync buffer readbackはコピー前にstateを整え、確定していたsource stateを戻す。
  待機・CPU pixel cacheを追加しない。ticketのsubmission、lease、backpressureは維持する。

`CheckRhiResourceStates`はbackend-neutralな`GraphicsDevice` / `CommandList`のみを使う。
両backendに同じ25件の不正なinitial state / transition / copyを与え、native errors、
submission、host wait、device-wide wait、live native countを変えずに拒否することを検査する。
拒否後に正しいtexture copyとbuffer copyを続行し、読んだ画素・bytesが一致することを確認する。
別command listが変更したstate、valid initial texture state、同extent / 新extentのresize、
host / GPU buffer upload、texture uploadのfinal stateも確認する。
既存rendering / interleaved / async readback fixtureも引き続き実行する。

確認結果（Windows / RTX 5070 Ti）:

- MSVC Release PASS: `C:/tmp/gp/architecture-r19-states-final-build.log`。
- CPU CTest 15/15 PASS: `C:/tmp/gp/architecture-r19-states-final-ctest.log`。
- 共通GPU conformance PASS: `C:/tmp/gp/architecture-r19-states-final-conformance.log`。
  OpenGL / Vulkanとも新しい拒否25件がPASS。既存rendering / binding / session / async / GPU diagnosticsもPASS。
- `-vulkanresourcecheck` PASS: `C:/tmp/gp/architecture-r19-states-resource.log`。
  64回のchurn / persistent upload reuse / descriptor再利用、通常device-wide waits=0、
  最終live=0 / retired=0、validation errors=0。
- Alinos Perch試合中3回切替 PASS: `C:/tmp/r18-fps-20261003-012320/opengl.log`。
  Settings保存・resume、8 actors、既存ボム・粒子・texture binding・simulationのwitnessがPASS。
  Vulkan validation errors=0。GPU profile / FPS計測も有効にして回帰を確認した。

R19全体は未完了。全format / subresource、usageとformatに対する全stateの適合性、
draw・presentationを含むstate遷移全体、presentation ownership、100-cycle stressは続ける。
R10の最終責務監査、R20とレビュー全体も進行中。Android / macOS実動作、remote CIは未実行。

## 実ウィンドウの反復切替・resize stress（R19 / Phase H）

`FRUITY_SWITCHSTRESS=1..1000`で、既存のshellshot fixtureを複数の試合にわたって繰り返す。
各cycleはfront screenと試合中Settingsの2回の切替、両backendでのwindow / resolution-scale
resize、scene終了を含む。spawn済みSyluxと7 actors、105 effect definitions、着弾粒子と
新旧Lockjaw bomb、texture-only model sourceの寿命検査を維持する。
切替の直前直後は既存のworld witnessでsimulation / actors / bombs / particles / bindingsを検査する。

旧scene / UI / command list / swapchainを解放した直後と、最終window終了時のcheckpointで
cache trim / idleを行い、RHI live=0 / retired=0 / errors=0を要求する。
通常のwindow / target resizeはdevice-wide waitの増加を拒否する。
途中終了は全cycle完了・最終解放の両方が揃わないため成功にならない。
これらは診断時だけのhookで、通常の描画に待機を追加しない。

再実行用の`tools/rendering-stress.ps1`は設定ファイルの元のbytesと環境変数をfinallyで戻し、
cycleごとの7枚のPNG、全cycle完了、両backendのresize数、world witness数、validationを検査する。
`run-info.json`にsource HEAD / exeとpaths.txtのSHA-256 / 条件 / 完了状態を残す。
具体的な実行方法は[切替stress手順](Fruity-Prime-Renderer-Switch-Stress.md)に記録した。

確認結果（Windows / RTX 5070 Ti）:

- MSVC Release PASS: `C:/tmp/gp/architecture-r19-stress-final-build.log`。
- CPU CTest 15/15 PASS: `C:/tmp/gp/architecture-r19-stress-final-ctest.log`。
- 共通GPU conformance PASS: `C:/tmp/gp/architecture-r19-stress-conformance.log`。
- 最終hookを含む1 cycleの試運転は両開始backendともPASS。
  `C:/tmp/r19-stress-finalpilot-gl/run.log`と`C:/tmp/r19-stress-finalpilot-vk/run.log`。
  各2切替 / 7 PNG、両backendのresize、最終live=0 / retired=0 / validation errors=0を確認。
  保存した両backendのresize画像を目視し、Alinos Perchのworld / Sylux HUD / gun / effectを確認した。
  動的な画像の自動pixel parityを証明するものではない。
- 既存の失敗復旧回帰もPASS: `C:/tmp/r18-fps-20261003-015658/opengl.log`。
  BeforeWindow / Presentation / Resourcesの3段階でtyped errorを注入し、元backendへの復旧、
  原因の保持、同じworldの保持を確認した。実driverのdevice loss / OOMの注入ではない。
- 実行manifestを含むrunnerの1 cycleもPASS: `C:/tmp/r19-stress-manifest-pilot/run.log`。
  `run-info.json`のcomplete=true、切替2 / PNG 7、exe / paths.txtのSHA-256を確認した。

push済み`98be22e1`から再ビルドしたexeで100-cycle本試験もPASS:
`C:/tmp/r19-stress-100-98be22e1/run.log`、`run-info.json`。
100試合 / 200切替、同じ試合のworld witness 100件、両backend各100回のresizeが成功。
通常resizeのdevice-wide wait増加=0、各世代と最終終了のlive=0 / retired=0 / errors=0。
native exit=0、全ログのVUID / Validation Error=0。実行時間は約29分。
exe SHA-256は`FE5AE59F42DA021DCD307BDA4C2C8EB4C224EA630B888F2E0C0852CB535E12DE`。
保存700 PNGを別途PillowでCRC検査 / decodeし、全画像の寸法と非単色を確認した。
記録は同じ出力先の`image-verification-1-66.json`と`image-verification-67-100.json`。
これは画像の完全一致や、Skia / driver内部の全memory allocationの証拠ではない。

全format / subresource / usage-state適合性 / presentation ownership、固定frameのpixel parity、
R10の最終責務監査は続ける。R20の現行境界監査は後節に記録した。
RHI countはSkia / driver内部の全GPU確保量を測定しない。レビュー全体は進行中。

## 宣言usage / attachment formatと状態の適合性（R19）

`ResourceStatePolicy.hpp`にAPI非依存のdescriptor / state検査を追加した。
bufferのVertex / Index / Uniform / Storage / Transfer usageと、textureのSampled / Storage /
Color / DepthStencil / Transfer usageを、初期stateと明示的transitionで両backendとも検査する。
未知usage bit、色・深度attachmentのformat違い、独立確保したtextureのPresent指定も拒否する。
storage-only textureのShaderReadはVulkanのGENERAL layoutに写し、sampled image用layoutを使わない。
共通GPU fixtureにはusage / formatの拒否とstorage textureを経由した画素保持を追加した。

CPUのstatic contractとOpenGL / Vulkan / GPU fixtureのobject compilationはPASS:
`C:/tmp/gp/architecture-r19-usage-contract-compile.log`、`C:/tmp/gp/architecture-r19-usage-objects.log`。
object compilationの時点ではexeを再linkせず、100-cycle試験はpush済み`98be22e1`のexeで完了した。
本試験終了後にこの変更を含めて別ビルドし、次を確認した:

- MSVC Release PASS: `C:/tmp/gp/architecture-r19-usage-final-build.log`。
- CPU CTest 15/15 PASS: `C:/tmp/gp/architecture-r19-usage-final-ctest.log`。
- 共通GPU conformance PASS: `C:/tmp/gp/architecture-r19-usage-final-conformance.log`。
  両backendで不正操作55件をnative submission / waits / live countを変えずに拒否。
  storage-only textureのread / write / transfer stateと、拒否後のcopy画素保持もPASS。
  session teardown、async readback、binding復元の既存fixtureもPASS。
- `-vulkanresourcecheck` PASS: `C:/tmp/gp/architecture-r19-usage-resource.log`。
  64回のclear / bind / resize / releaseで通常device-wide waits=0、最終live=0 / retired=0 / validation errors=0。
- Alinos PerchのSettings保存・Resumeを含む試合中3回切替もアプリexit=0。
  `C:/tmp/r18-fps-20261003-085815/opengl.log`。world witness 3件、texture source解放、validation errors=0。
  wrapper末尾に追加した出力抑制記法の誤りでPowerShellだけexit=1となったため、
  保存ログを独立検査した。検査記録は`C:/tmp/gp/architecture-r19-usage-switch-audit.log`。

Renderer.cppの未使用BackendFactory / OpenGL固有header / GlNames importsも削除した。
`C:/tmp/gp/architecture-r1-renderer-compile.log`と全体ビルドでbackend型への依存がないことを確認。
全format / subresource、convenience draw / presentationのstateとownershipの最終監査は残る。

### 固定画像の再確認とGit provenanceの修正

push済み`b2ce899f`のRelease exeでGolden Captureの7ケースを両backendとも撮影した。
`C:/tmp/gp/architecture-r19-usage-final-golden-{opengl,vulkan}/`。
直近の固定画像`architecture-uploadarena-batched-golden-{opengl,vulkan}`と1600×900の
decoded RGBを比較し、全14画像で相違byte=0。
比較記録は`C:/tmp/gp/architecture-r19-usage-golden-comparison.txt`。
HUD / fadeの同じsimulation updateのcontrolとの差を含むnative capture gateもPASS。
これは同じbackendの画像回帰であり、Phase3/C#とのcross-revision provenanceを証明する検査ではない。

この確認でnative manifestのsource_commitがunavailableになっていたことを発見した。
MSVC wrapperがMinGW混入を避けるためPATHを整理しており、別のCMake script processがGitを
再検索すると見つからなかった。configure時の`find_package(Git QUIET)`の結果をprovenance
scriptへ渡し、Gitの絶対pathを引き継ぐよう修正した。Gitのないsource archiveのfallbackは維持する。
`C:/tmp/gp/architecture-r19-golden-provenance-build.log`でRelease buildがPASSし、生成headerの
SourceCommit=`b2ce899fa72af6db2def300bc26af22ee1a40cef`、GitHarnessState=clean、
実Cpp blobとharness SHA-256が埋め込まれたことを確認した。

修正をpushした`228ba74cab94b988e3b87505927151ec8870b167`からReleaseを再ビルドし、
同じ14ケースを再撮影した。全manifestのsource_commitがこのSHA、harness state=clean、
harness SHA-256が同一であることを確認した。HUD / fade / whiteoutのfinal-stage gateもverified。
保存先は`C:/tmp/gp/architecture-r19-provenance-final-golden-{opengl,vulkan}/`。
全14画像は同じbackendの旧画像とdecoded RGB相違byte=0。Vulkan validation error=0。
比較記録は`C:/tmp/gp/architecture-r19-provenance-golden-comparison.txt`、
exe / paths.txt / 各PNG / manifestのSHA-256は
`C:/tmp/gp/architecture-r19-provenance-golden-run-info.json`に保存した。
exe SHA-256=`487010964C3BB5FEB77F720F0C65C398D9A876BDA41B3C62DC5344128F865CB3`。
後続の文書commitと区別し、この撮影のsource SHAは上記`228ba74c`として記録する。
同じbackendの回帰確認の範囲は変わらず、Phase3/C#とのcross-revision parityは未成立。

## 現行pacingと将来optional extensionの境界監査（R20）

レビュー19.4 / 23.1は、将来Reflex / Anti-Lag / XeLL / native display timingなどを追加する場合に、
専用controllerとfeature probeへdispatch / result分類 / policyを隔離する方針を要求する。
現行Windows OpenGL / Vulkanではこれらのvendor機能を提供していない。
今回新たにcontrollerの空実装やvendor featureを追加しない。

現行の境界を確認した:

- `RenderWindow::ApplyFrameRateSettings`はlogical capからFIFO / Immediateを要求し、
  platform windowのUpdateFrequencyを設定する。native extensionを呼ばない。
- `RendererPlatform::GlfwWindow::Run`がCPU側の描画上限を処理する。
  `FrameTiming::Advance`の60 Hz simulation accumulatorと独立している。
- 実際のnative presentation modeとfallbackは各swapchainが持つ。
  requested / actual modeとpresentation capabilitiesは既存の共通semantic contractを使う。
- `GraphicsDevice` / `CommandList` / `Capabilities`にReflex、Anti-Lag、XeLL、
  Vulkan present-wait / display-timing、DXGI waitable objectのdispatchや専用policyはない。
  追加時のownerはBackendSessionのoptional controllerとする。coreにvendor名を増やさない。
- GPU timestampはR18のoptional semantic queryであり、pacingに結果待ちを追加しない。
  実GPUでのnonblocking結果確認は既存の共通conformanceで検査する。

`-frametimingcheck -noupdate`のCPU検査はPASS:
`C:/tmp/gp/architecture-r20-frametiming.log`。
60 / 144 / 240 / 165 / 40 Hz、jitter、vsync rate変化でもsimulation rateとcatch-up制限を確認した。
これはvendor低遅延機能の実測・実装の証拠ではない。R20の現行境界監査を完了し、
将来機能の実装・driver / vendor SDK検証は追加時の対応に残す。

## 兼用するread stateからのGPUコピー（R19）

ResourceStateの契約ではread-onlyの状態を組み合わせられるが、両backendのexplicit copyが
CopySrcとの完全一致を要求していた。VertexBuffer | CopySrcのbufferや
ShaderRead | CopySrcのtextureはtransitionで受理されても、コピーで拒否されていた。
先に共通fixtureを追加し、旧処理でOpenGLの拒否によるexit=1を再現した:
`C:/tmp/gp/architecture-r19-combined-before-conformance.log`。

buffer / image upload / image readbackの3経路がCopySrcを含むread stateを受理するよう修正した。
destinationのCopyDstは従来どおりexclusive。usage / ownership / 範囲検査は維持する。
Vulkan image readbackはtracked stateからnative layoutを選び、combined readのGENERALを保つ。
新しいCPU画素cache、host readbackによる転送代替、waitは追加しない。
GPU fixtureで実画素とコピー後のstate保持を確認し、別command listからのtransitionも検査する。
storage-only textureのcombined readからのreadbackも同じfixtureで検査する。

- MSVC Release PASS: `C:/tmp/gp/architecture-r19-combined-final-build.log`。
- CPU CTest 15/15 PASS: `C:/tmp/gp/architecture-r19-combined-final-ctest.log`。
- 共通GPU conformance PASS: `C:/tmp/gp/architecture-r19-combined-final-conformance.log`。
  両backendの55件の不正状態拒否と新しいcombined readコピー、最終release=0がPASS。
  Vulkan validation error=0。既存の8回session再生成とasync readbackもPASS。
- FPS-onlyの両開始rendererを再確認: `C:/tmp/r18-fps-20261003-091701/`。
  各runで試合中3回切替、world witnessとsource release、native / wrapper exit=0。
  Alinos Perch、出現済みSylux +7 actors、2560×1439、scale100、cel0 / fog1、
  pause0 / focus1、FPS Counter Off、unlimited、Immediate、validation / GPU profilingなし。

両開始runの対象行をtotal frames / total secondsで集計した結果:

| 測定renderer | 行数 | FPS | 平均frame間隔 | CPU loop平均 | presentation CPU平均 |
|---|---:|---:|---:|---:|---:|
| OpenGL | 20 | 283.8 | 3.524 ms | 3.518 ms | 0.090 ms |
| Vulkan | 16 | 973.3 | 1.027 ms | 1.025 ms | 0.086 ms |

集計は同出力先の`summary.json`。captureを含むfixtureで差を再確認したもので、
今回のstate修正によるFPS改善や、差の原因を特定した証拠ではない。
[FPS計測手順](Fruity-Prime-FPS-Measurement.md)のCLI / CSVを継続して利用できる。
R19の全format / subresource / draw-state / presentation ownershipとR10の最終責務監査は残る。

## Vulkanのnativeリソース生成policyの独立（R10 / Phase C）

レビューの`VulkanResources.hpp/.cpp`を追加した。buffer / imageのdescriptor検査、
用途とformat / extent / mip / layer / sample countのdevice適合性、VMA allocationの要求、
samplerのanisotropy上限、viewのrange / type / depth aspect / RGB alpha swizzleをこのfactoryへ移した。
通常のbuffer / texture / sampler / viewとpersistent upload pageが同じfactoryを使う。
pipeline / interopもformat mappingをこのownerへ委譲する。

VMA allocatorとbudget / admissionは引き続きVulkanMemoryが所有する。
factoryは成功したnative valueをwrapper / upload arenaへ渡し、public handle・tracked state・
native owner registration・GPU completion後のretirementはsession側の既存契約を使う。
factoryのdispatchはsessionのallocator/contextを借り、session終了時にVMAより先に閉じる。
factory自体はrecording / submit / fence / device waitを持たない。
これは同じtranslation unitへ別断片をincludeするだけの変更ではなく、独立したclassと.cppである。

従来はimage / sampler / viewの生成後にdebug namingが失敗すると、constructorからの例外で
native objectを残し得た。factoryはこの段階で失敗したobjectを解放してから例外を返す。
persistently mapped bufferのmappingが得られない場合もstorageを解放する。
sampled viewのdimensionはtextureに合わせた2D / array / 3Dを選び、全mip / layerを保持する。
モデル画素の複製、CPUへの描画移動、通常churnでのdevice-wide waitは追加していない。

`TestVulkanResources`はGPU・VMA実allocatorを起動せず、同じfactoryのnative / allocation
dispatchを置き換えて検査する。invalid descriptor / device limit / unsupported formatを確保前に拒否し、
mapping・allocation・native create・debug namingの各失敗で所有数とretained resourceを確認する。
viewのrange / aspect / swizzle / dimension、sampler上限、close後のnative call拒否も含む。
driverのOOMやresetを注入する検査ではない。

- MSVC Release PASS: `C:/tmp/gp/architecture-r10-resources-build.log`。
- CPU CTest 16/16 PASS: `C:/tmp/gp/architecture-r10-resources-ctest.log`。
- OpenGL / Vulkan共通GPU conformance PASS:
  `C:/tmp/gp/architecture-r10-resources-conformance.log`。
  resource state / binding / copies / session再生成 / async readbackを含み、Vulkan validation error=0。
- Vulkan resource check PASS: `C:/tmp/gp/architecture-r10-resources-resourcecheck.log`。
  upload64回、clear / bind / resize / release64回、失敗resizeの旧画素・view・handle保持、
  通常churnのdevice-wide wait=0、最終live=0 / retired=0 / validation error=0。
- Alinos PerchのSettings保存・Resumeを含む試合中3回切替PASS:
  `C:/tmp/r18-fps-20261003-093043/opengl.log`。spawned Sylux +7 actors、world witness 3件、
  texture-only source解放、validation error=0、native / wrapper exit=0。
  GPU profileと240 FPS cap / validation ONの回帰検査で、FPS速度比較には混ぜない。

R10の最終責務監査は続ける。GraphicsDeviceのtranslation unitにはまだcommand list / binding / shader /
resource wrapperとsession調停が同居する。R19の全format / subresource / draw-state / presentation ownership、
レビュー全体のcompletion auditも残る。Metal / D3D12は将来対応、Android / macOS実動作とremote CIは未実行。

push済み`5aadf2708570cb386df13523c2821ed4dc000153`からReleaseを再ビルドして、
Golden Captureの7ケースを両backendとも確認した:
`C:/tmp/gp/architecture-r10-resources-final-golden-{opengl,vulkan}/`。
全14manifestのsource SHA / clean harnessとHUD / fade / whiteout final-stage gateがPASS。
同じharness / fixture入力の旧画像`architecture-uploadarena-batched-golden-{opengl,vulkan}`と
1600×900のdecoded RGBを比較し、全14画像で相違byte=0。Vulkan validation error=0。
比較記録は`C:/tmp/gp/architecture-r10-resources-golden-comparison.txt`、
exe / paths.txt / PNG / manifestのSHA-256とgateは
`C:/tmp/gp/architecture-r10-resources-golden-run-info.json`に記録した。
これは同じbackendの固定画像回帰の確認で、Phase3/C#とのcross-revision parityは未成立。
後続の文書commitと区別し、この撮影のsource SHAは上記`5aadf270`である。

## フレームfenceとdescriptor世代の独立owner（R10 / Phase C）

session状態に残っていたframe用fence / descriptor allocator / 診断command poolとlayoutのretentionを
`VulkanFrameSlots.hpp/.cpp`へ移した。native queueの送信番号はVulkanFrameScheduler、
recording streamのnative command / upload枠はVulkanCommandSlotsが引き続き所有する。
VulkanFrameSlotsは2つのframe世代、active状態、frame番号と完了frame番号を所有する。
binding setはframeのprivate vectorsへアクセスせず、descriptorの確保とtransient送信を委譲する。

再利用の前にfenceとqueue completion serialの両方を確認する。
既にsignaledならhost waitを呼ばず、未完了の場合だけ2秒刻みでfenceを待つ。
その待ちをHostWaitsへ記録する。従来のframe fence待ちはこのcounterへ反映していなかった。
descriptor resetが成功してから診断transientを解放する。
transientのretention用vectorはsubmit前に予約し、submit失敗ではnative objectをadoptしない。
frame Endのsubmit失敗でもactive状態とframe番号を維持し、再試行できる。

session終了は既存の明示的idle / device-loss境界でGPUをdrainしてからCloseAfterDrainを呼ぶ。
close自体にはsubmit / host wait / device-wide waitを追加しない。
終了後のrelease witnessはdescriptor pagesに加え、このownerのfence / command pool / layoutも0を要求する。
GPU生成policy、model画素の保持、描画pass、simulationは変更しない。

GPUなしの単独テスト`TestVulkanFrameSlots`では次を検査する:

- 64回の2-slot rotationとdescriptor overflow、fence前のpool reset / native release拒否。
- fenceがsignaledでもtimelineが遅れている場合の再利用拒否。
- timeout後の再確認、ready fenceでの非blocking処理、正しいframe番号と完了frame番号。
- fence / allocator生成、submit、fence / descriptor resetの失敗後の状態保持と再試行。
- typed device loss、二重close、close後のnative call拒否。
- frame End marker未送信でもsessionのidle境界で済んだactive frameを、追加submit / waitなしに解放。

検証:

- MSVC Release PASS: `C:/tmp/gp/architecture-r10-frame-slots-build.log`。
- CPU CTest 17/17 PASS: `C:/tmp/gp/architecture-r10-frame-slots-ctest.log`。
  active-frame closeケース追加後も対象テストPASS:
  `C:/tmp/gp/architecture-r10-frame-slots-final-unit.log`、Release再ビルドPASS:
  `C:/tmp/gp/architecture-r10-frame-slots-final-build.log`。
- OpenGL / Vulkan共通GPU conformance PASS:
  `C:/tmp/gp/architecture-r10-frame-slots-conformance.log`。
  各backendの8回session再生成・native解放、async readback等がPASS、Vulkan validation error=0。
- Vulkan resource check PASS: `C:/tmp/gp/architecture-r10-frame-slots-resourcecheck.log`。
  64回のchurn、descriptor overflow / reuse / GPU bind、failed resizeの旧native保持がPASS。
  通常device-wide wait増加=0、最終live=0 / retired=0 / validation error=0。
- FPS-only両開始rendererで試合中各3回切替PASS:
  `C:/tmp/r18-fps-20261003-095148/`。各runのworld witness 3件、source release、native / wrapper exit=0。
  Alinos Perch、spawned Sylux +7 actors、2560×1439、scale100、cel0 / fog1、pause0 / focus1、
  FPS Counter Off、unlimited、Immediate、validation / GPU profilingなし。
  対象行のtotal frames / total secondsはOpenGL 20行で287.2 FPS、Vulkan 16行で963.8 FPS。
  集計は`summary.json`。同じfixtureの再確認であり、最適化効果の証明ではない。

frame fencing policyの独立検査を実装した。R10の最終責務監査、R19の全format / subresource /
draw-state / presentation ownership、レビュー全体のcompletion auditは引き続き残る。


push済み`00c97827efc7bc3907aed57da7d14d0a7d851fdd`からReleaseを再ビルドし、
Golden Captureの7ケースを両backendで確認した:
`C:/tmp/gp/architecture-r10-frame-slots-final-golden-{opengl,vulkan}/`。
全14manifestのsource SHA / clean harnessとHUD / fade / whiteout final-stage gateがPASS。
同じharness / fixture入力の`architecture-uploadarena-batched-golden-{opengl,vulkan}`と
1600×900のdecoded RGBを比較して、全14画像で相違byte=0。Vulkan validation error=0。
比較記録は`C:/tmp/gp/architecture-r10-frame-slots-golden-comparison.txt`、
exe / paths.txt / PNG / manifestのSHA-256とgateは
`C:/tmp/gp/architecture-r10-frame-slots-golden-run-info.json`。
同じbackendの固定画像回帰の確認であり、Phase3/C#とのcross-revision parityは未成立。
撮影のsource SHAは後続の文書commitと区別して上記`00c97827`で記録する。


## sampled descriptorとcombined read stateの一致（R10 / R19）

共通fixtureでsampled imageを`ShaderRead | CopySrc`にすると、Vulkanの画像はGENERALだが
sampled descriptorがSHADER_READ_ONLYを固定しており、drawでlayout不一致が発生した。
修正前の共通GPUテストはexit=1:
`C:/tmp/gp/architecture-r19-sampled-state-before-conformance.log`。
同じframebufferの左右をnearest / linearの独立samplerで描く既存fixtureへ、
combined read、同じnative command buffer中のlayout変更、readback / frame / 他list後の再描画を加えた。

barrier / sampled descriptor / interopのmappingを`VulkanSynchronization.hpp/.cpp`のpure policyへ独立させた。
device・dispatch・native ownerを必要とせず、CPUテストで8192件のknown-bit / resource typeの組み合わせ、
read-onlyのwrite access不在、exclusive write、depth / sampled / storage-only / combined readを検査する。
Vulkan generic bindingはtracked image stateに合わせたlayoutをdescriptorへ書き、
view / layoutが変わったら新しいsetへ更新する。すでにGPUへ記録したsetは上書きしない。
Scene側のsampled bindingもcombined readを維持し、descriptor cacheにlayoutを含める。

bindingは読み取りtransitionより先に準備できる。Undefined / transfer / write stateでの準備は
後続の通常sampled layoutをdescriptorへ設定し、draw時にはCommonまたはShaderReadを含む状態を要求する。
OpenGL / Vulkanとも読み取りできないstateでのgeneric sampled drawをnative draw前に拒否する。
拒否後の再transition・draw、native allocation / submit / host / device waitの非増加も共通fixtureで確認した。
モデル画素のCPU複製、CPUでの描画、host readbackによる転送代替は追加していない。

native descriptorと画像のlayoutを一致させる条件は
[Khronos Vulkan drawing specification](https://docs.vulkan.org/spec/latest/chapters/drawing.html#VUID-vkCmdDrawIndexed-imageLayout-00344)に従う。
この変更はsampled imageを対象とし、全storage image format / subresource / draw-stateの完了を意味しない。

- MSVC Release PASS: `C:/tmp/gp/architecture-r19-sampled-state-final-build.log`。
- CPU CTest 18/18 PASS: `C:/tmp/gp/architecture-r19-sampled-state-final-ctest.log`。
- OpenGL / Vulkan共通GPU conformance PASS:
  `C:/tmp/gp/architecture-r19-sampled-state-final-conformance.log`。
  sampled / copy combined read、同じcommand bufferのlayout変更、non-readable draw拒否、
  readback / frame / 他list後の再描画、両backend各8回のsession再生成・最終release=0を確認。
  Vulkan validation error=0。
- Vulkan resource check PASS: `C:/tmp/gp/architecture-r19-sampled-state-final-resourcecheck.log`。
  descriptorの描画前準備 / overflow / 再利用 / bind、64回churn、failed resizeの旧native保持がPASS。
  通常device-wide wait増加=0、最終live=0 / retired=0 / validation error=0。

R10の最終責務監査、R19の全format / subresource / draw-state / presentation ownershipと
レビュー全体のcompletion auditは残る。Metal / D3D12は将来対応。


FPS-onlyの両開始rendererでAlinos PerchのSettings保存・Resumeを含む各3回切替がPASS:
`C:/tmp/r18-fps-20261003-101346/`。spawned Sylux +7 actors、各runのworld witness 3件、
source release、native / wrapper exit=0。2560×1439、scale100、cel0 / fog1、pause0 / focus1、
FPS Counter Off、unlimited、Immediate、validation / GPU profilingなし。
対象行のtotal frames / total secondsはOpenGL 20行で290.7 FPS、Vulkan 16行で946.0 FPS。
集計は同出力先の`summary.json`。captureを含むfixtureの再確認であり、差の原因・最適化効果の証明ではない。


validation ON / GPU profile / 240 FPS capの別runでも試合中3回切替がPASS:
`C:/tmp/r18-fps-20261003-101634/opengl.log`。
world witness 3件、source release、Vulkan validation error=0、native / wrapper exit=0。
このrunのFPSはvalidation OFFの速度比較へ混ぜない。


push済み`c40f7b91e885e44838502361629f39dd5b6bd013`からReleaseを再ビルドし、
Golden Captureの7ケースを両backendで確認した:
`C:/tmp/gp/architecture-r19-sampled-state-final-golden-{opengl,vulkan}/`。
全14manifestのsource SHA / clean harnessとHUD / fade / whiteout final-stage gateがPASS。
同じharness / fixture入力の`architecture-uploadarena-batched-golden-{opengl,vulkan}`と
1600×900のdecoded RGBを比較し、全14画像で相違byte=0。Vulkan validation error=0。
比較記録は`C:/tmp/gp/architecture-r19-sampled-state-golden-comparison.txt`、
exe / paths.txt / PNG / manifestのSHA-256とgateは
`C:/tmp/gp/architecture-r19-sampled-state-golden-run-info.json`。
同じbackendの固定画像回帰の確認であり、Phase3/C#とのcross-revision parityは未成立。
撮影のsource SHAは後続の文書commitと区別して上記`c40f7b91`で記録する。


## logical RGB8のGPU buffer / image copy（R10 / R19）

共通fixtureでRGB8の3-byte pixels、buffer offset 11 / 7、独立row pitchを使うと、
OpenGLがPASSする一方、Vulkanは4-byte texel alignmentを要求して拒否していた。
Vulkanは描画可能なRGBA8をRGB8の内部storageとして使うが、explicit copyがその物理形式を
frontendへ露出していた。修正前の再現はexit=1:
`C:/tmp/gp/architecture-r19-rgb-copy-before-conformance.log`。

public `CopyBufferToTexture` / `CopyTextureToBuffer`をlogical RGBの3-byte packingへ合わせた。
VulkanはGPU-only scratch上で、bufferコピーのgather / scatterによりRGBとRGBAを詰め替える。
GPUのfillでalpha=255を用意し、3-byte RGBコピーはalphaへ触れない。
転送対象の画素をCPUへ読み戻さず、CPU画素cache・モデル画素の複製も追加しない。
CPUに作るのはcopy offset / sizeだけで、command metadataを256 regionsずつ渡す。
byte-range transferには[Khronos VkBufferCopy](https://docs.vulkan.org/refpages/latest/refpages/source/VkBufferCopy.html)を使う。

`VulkanRgbTransfer`はformat / aspect / sample count、mip / layer / 3D、bounds、row / slice pitch、
buffer rangeとoverflowをGPU mutation前に検査するaddress-only policy。
`VulkanTransferScratch`は初期64 KiBと必要なoversized GPU-only pagesを所有し、
command slotのfenceとsubmission completionが済んでからsuballocationを再利用する。
native allocationは既存VulkanResources / VulkanMemoryのadmissionを使い、Buffers統計へ含める。
通常copyにはsubmit / host wait / device-wide waitを追加しない。
frame / command slotの完了確認とsession teardownは既存境界を維持する。

同期・非同期のscreenshot/readbackは従来のphysical RGBA stagingとoutput leaseのpackingを維持し、
public logical RGB copyとは内部helperで区別する。RGB / RGBA / BGRAの既存async fixtureもPASS。

検証:

- MSVC Release PASS: `C:/tmp/gp/architecture-r19-rgb-copy-final-build.log`。
- CPU CTest 19/19 PASS: `C:/tmp/gp/architecture-r19-rgb-copy-final-ctest.log`。
  new policyの6150 pixels / 25 metadata batches、mip / layer / volume / overflow / paddingを検査。
  fake dispatchでscratchの64回completion / reuse、allocation failure / malformed page rollback / closeを検査。
  command slotsの64回rotationでもscratch再利用、初期化失敗 / null allocatorのrollbackを確認。
- OpenGL / Vulkan共通GPU conformance PASS:
  `C:/tmp/gp/architecture-r19-rgb-copy-final-conformance.log`。
  両backendの3-byte packing、offset / pitch、subrectangle、untouched pixels / buffer padding、opaque alpha、
  combined read state、各14件のinvalid copy拒否と拒否後の正常copyがPASS。
  invalid copyのallocation / submit / host / device wait増加=0。
  両backendで4097×5 pixelsのcopyを64回繰り返し、batched transfer / oversized pages / buffer count安定を確認。
  Vulkanはnonbase mip・array layer・3D volumeとrow / slice paddingもPASS。
  既存session ownership・async readback、最終release=0 / retired=0もPASS。Vulkan validation error=0。
- Vulkan resource check PASS: `C:/tmp/gp/architecture-r19-rgb-copy-final-resourcecheck.log`。
  64回の既存churn、通常device-wide wait増加=0、最終live=0 / retired=0 / validation error=0。
- Alinos PerchのSettings保存・Resumeを含む試合中3回切替PASS:
  `C:/tmp/r18-fps-20261003-104043/opengl.log`。
  spawned Sylux +7 actors、world witness 3件、source release、validation error=0、native / wrapper exit=0。
  validation ON / GPU profile / 240 FPS capの回帰検査で、FPS速度比較には混ぜない。

これはRGB8のexplicit GPUコピーの修正と検査であり、全format / subresource / draw-stateの完了や
FPS改善の証明ではない。OpenGLのmip / array / 3D、packed depth/stencil copy、storage image format、
presentation ownershipとR10の最終責務監査、レビュー全体のcompletion auditは残る。
Metal / D3D12は将来対応。Android / macOS実動作とremote CIは未実行。

push済み`b2dcbdca3f21c1015fc666ec2e5f3056c7b643d6`からReleaseを再ビルドし、
Golden Captureの7ケースを両backendで確認した:
`C:/tmp/gp/architecture-r19-rgb-copy-final-golden-{opengl,vulkan}/`。
全14manifestのsource SHA / clean harnessとHUD / fade / whiteout final-stage gateがPASS。
同じharness / fixture入力の`architecture-uploadarena-batched-golden-{opengl,vulkan}`と
1600×900のdecoded RGBを比較し、全14画像で相違byte=0。Vulkan validation error=0。
比較記録は`C:/tmp/gp/architecture-r19-rgb-copy-golden-comparison.txt`、
exe / paths.txt / PNG / manifestのSHA-256とgateは
`C:/tmp/gp/architecture-r19-rgb-copy-golden-run-info.json`。
同じbackendの固定画像回帰の確認であり、Phase3/C#とのcross-revision parityは未成立。
撮影sourceは後続の文書commitと区別して上記`b2dcbdca`で記録する。

## queue schedulerのnative dispatch検査（R10）

`VulkanFrameScheduler`はqueue submitとtimeline completionを所有し、frame / command slotの
fence・pool・descriptor世代とは独立した境界を維持する。このownerへ直接fake native dispatchを
渡すCPU testを追加した。実GPUのconformanceを通じた間接検査とは区別する。
不完全なdispatch、countに対してnullのsubmit配列と不正なsubmit typeをnative操作前に拒否する。
成功を返す空のtimelineも初期化失敗として拒否する。
正常なqueue submissionの処理には待機や再送を追加しない。

検査は64個のsuccessful serialとexternal-work markerを通し、callerのwait / signal semaphore、
command buffer、extension pointer / flags / fenceを保持したまま、末尾へALL_COMMANDSのtimeline
signalを加えることを確認する。native submitのOOM / device loss、counter取得のdevice lossを
注入し、失敗でsubmitted / completedを更新しないこととnative errorの分類・code保持を検査する。
遅れて返るcounterでcompletionが戻らず、未送信serialのcompletionは拒否される。
ここでのfaultはfake dispatchへの注入であり、実driver resetや実OOMからの復旧の証明ではない。

検証:

- MSVC Release PASS: `C:/tmp/gp/architecture-r10-scheduler-build.log`。
- CPU CTest 20/20 PASS: `C:/tmp/gp/architecture-r10-scheduler-ctest.log`。
- OpenGL / Vulkan共通GPU conformance PASS:
  `C:/tmp/gp/architecture-r10-scheduler-conformance.log`。
  sampled / combined state、RGB copy / scratch reuse、session再生成 / async readbackを含み、
  最終release=0 / retired=0、Vulkan validation error=0。
- Alinos PerchのSettings保存・Resumeを含む試合中3回切替PASS:
  `C:/tmp/r18-fps-20261003-105450/opengl.log`。
  spawned Sylux +7 actors、bomb / particle / bindingのworld witness 3件、source release、
  Vulkan validation error=0、native / wrapper exit=0。
  validation ON / GPU profile / 240 FPS capの検査であり、FPS-onlyの速度比較へ混ぜない。

R10のレビュー全体に照らした最終責務監査、R19のformat / subresource / draw / presentation契約と
completion auditは続ける。Metal / D3D12は将来対応。Android / macOS実動作、remote CIは未実行。

## テクスチャ形式ごとの作成・GPUコピー契約（R19）

作成できても、後のGPU操作で失敗する形式があった。これを作成時の契約と実装の両方で揃えた。

- **Storage 用途:** 現在の shader image 契約で表せない形式（RGB、sRGB、BGRA、depth / stencil）は、
  作成時に拒否する（`IsStorageTextureFormat`）。それ以外の用途では、これらの形式はこれまでどおり使える。
- **OpenGL の depth:** 転送用途（TransferSrc / TransferDst）の depth 画像は、コピーできない
  renderbuffer ではなく texture として作る。
- **Vulkan の RGB32Float:** このGPUでは native 形式を使えない。そこで RGBA32Float を内部形式にし、
  12 byte の論理 texel を 16 byte の native texel に GPU 上で詰め替える。RGB8 と同じ
  address-only copy plan で、alpha は 1.0。scratch は texel 境界に揃えて確保する。
- **Vulkan の transfer scratch page:** command slot が再利用のために保持する内部 cache である。
  そのため、caller が作った `Buffers` / `LiveObjects` には数えず、`TransferScratchPages` として
  別に数える。upload page と同じ扱いで、再利用の検査はこの数が増え続けないことで行う。
  以前は前の fixture が残した page が `Buffers=1` として次の fixture の漏れに見えていた。
  漏れ検査の失敗は、残った種類ごとの数も示す。

両 backend で、色と単独深度の16形式について次を GPU コピーで照合する: 画素・余白・部分領域、
transfer 専用用途、コピー中の待機0。packed depth / stencil は別の契約として残す。

検証:

- MSVC Release PASS: `C:/tmp/gp/architecture-r19-formats-build.log`。
- CPU テスト 19/19 PASS。
- OpenGL / Vulkan 共通 GPU conformance PASS: `C:/tmp/gp/architecture-r19-formats-conformance.log`。
  16形式 ×2 backend、async readback と session 再生成を含む。最終 release=0、Vulkan validation error=0。
- 試合中の切替検査（MP10 OVERLOAD、`FRUITY_SWITCHCHECK=1`）PASS。

packed depth / stencil のコピー、全 subresource / draw-state / presentation ownership の網羅は R19 で続ける。

## packed depth / stencil のコピー契約（R6 / R19）

packed depth / stencil（D24S8 / D32FS8）の buffer コピーを、意味の上での capability
`Capabilities::supportsPackedDepthStencilTransfer` として定義した。

- **Vulkan（true）:** aspect ごとにコピーする。depth は 1 texel 4 byte で、D24 の download では
  上位 8 bit が未定義。stencil は 1 texel 1 byte。
- **OpenGL（false）:** GL_DEPTH_STENCIL texture の片方の aspect だけを書く手段がない。GPU 上で
  合成する経路を持たないので、transfer 用途で作る時点で拒否する。後のコピーで失敗はさせない。

本番 renderer は packed depth / stencil を attachment と sampled depth view にしか使わない。
そのため、この拒否で本番経路は変わらない。

conformance:

- Vulkan では、D24S8 / D32FS8 の depth と stencil を個別に upload / download する。
  部分領域・余白・未変更領域を照合し、D24 の未定義 byte だけを比較から外す。
- OpenGL では、作成が拒否されることを検査する。

検証: 両 backend の conformance PASS（`C:/tmp/gp/architecture-r19-packed-ds-conformance.log`）、
CPU テスト 19/19 PASS。

## subresource の capability 化（R6 / R19）

OpenGL は単一 level の 2D 画像しか作らないが、それを契約に表す手段がなかった。そのため caller には、
例外が出るまで分からなかった。`Capabilities` に `maxTextureMipLevels` と `maxTexture3DDimension` を
追加し、既存の `maxTextureArrayLayers` と合わせて作成時の判定に使う。

| | mip levels | array layers | 3D |
|---|---|---|---|
| OpenGL | 1 | 1 | 0（作らない） |
| Vulkan（RTX 5070 Ti） | 16 | 2048 | 16384 |

conformance では、上限以内の作成が通り、上限を超える作成が拒否されることを両 backend で検査する。
OpenGL で mip / array / 3D の画像そのものを実装するのは、本番 renderer が使わないので将来対応とする。

検証: 両 backend の conformance PASS（`C:/tmp/gp/architecture-r19-subresource-caps-conformance.log`）、
CPU テスト 19/19 PASS。

## native pipeline cache の効果測定（R11）

`VulkanPipelineCache::Statistics::CreationNanoseconds` に、`vkCreateGraphicsPipelines` の中で
過ごした wall time を加算し、session 終了時に `create-ms` として出す。

測定条件:
- **場面:** `-maptest "MP2 HARVESTER" -players 4 -seconds 6 -rhi vulkan`、本番 scene の 14 pipelines。
- **cache:** `FRUITY_VK_PIPELINE_CACHE_DIR` で cache の置き場所を分ける。cold は毎回空の
  directory、warm は直前の run が保存した 140994 byte を読み込む。
- **driver cache:** `__GL_SHADER_DISK_CACHE=0` で NVIDIA driver 自身の disk cache を止める。
  driver cache が効くと cold も warm になり、このアプリの cache を測れないため。

| run | 生成時間（14 pipelines） |
|---|---|
| cold ×3 | 11.95 / 9.17 / 9.32 ms |
| warm ×3 | 0.84 / 0.88 / 0.87 ms |

warm は cold の約 11 倍速い。RTX 5070 Ti / driver 617.14 での1台の測定である。他の GPU や driver
cache を有効にした通常の環境での効果は、この数字からは言えない。

## 全形式・subresource の非同期 readback（R16）

`EnqueueReadColor` は screenshot / recording 用の契約のままにする: 8bit color target から RGB8 / RGBA8
を、base mip / layer で出す。任意の形式と subresource は、GPU コピー（texture → buffer）の後に
`EnqueueReadback`（buffer）で読む。この2段がその経路であり、新しい API は足さない。

conformance:

- **16形式:** 両 backend で、format copy の結果 buffer を非同期 readback でも読み、同期読みと
  byte 単位で一致することを確認する（32件）。
- **subresource（Vulkan）:** 2 mip × 2 layer の画像の mip 1 / layer 1 だけに書いて読み戻し、
  base subresource が変わらないことを確認する。
- **subresource（OpenGL）:** `Capabilities` が単一 level 2D のため、検査しない。

検証: 両 backend の conformance PASS（`C:/tmp/gp/architecture-r16-format-readback-conformance.log`）、
CPU テスト 19/19 PASS。recording の長時間 stress は R19 の stress（98be22e1、700 PNG）の範囲に留まる。

## OpenGL release marker の集約（R4）

resource release は、自分専用の `glFenceSync` を入れずに、次に挿入される marker の serial を受け取る
（`SubmitForRetirement`）。その marker は、次の submission（list End / frame end）、poll、その serial
への wait、finish のいずれかで挿入される。それまでの release はすべてその1個を共有する。

これで正しい理由: GL の context stream では、その marker は解放された object の最後の使用より後に
ある。retire に必要なのはそれだけである。完了前に破棄しないこと（serial > completed の間は保持）、
`Finish` / teardown で漏れなく完了すること、release だけが続く区間でも poll で marker が入ることは、
既存の lifetime / conformance / session 検査が覆う。ARB_sync のない GL 2.1 診断 context は、従来どおり
その場で同期する。

| Alinos Perch 40 cycles（OpenGL） | 以前 | 集約後 |
|---|---|---|
| submissions（marker） | 69,640 | 2,440 |
| 最後の解放後の live / retired | 0 / 0 | 0 / 0 |
| `Completed=Submitted` | yes | yes |
| 各 cycle 末 2 frame の host wait | 0 | 0 |

同時に、`-shellshot` の RHI 画像出力検査が OpenGL で `Begin` を省いていたのを直した。recording scope
を強制した 4d457ed4 以降、その検査は "command list is not recording" で失敗していた。

検証: CPU テスト 19/19、両 backend の conformance、`-shellshot` 両 backend（28 shots、validation 0）、
試合中切替（OpenGL 開始）PASS。lifetime: `C:/tmp/gp/architecture-r4-marker-aggregation-lifetime40.log`。
upload / map の driver 内部の暗黙 wait は計測対象外のまま。

## presentation の共通 conformance（R9）

`-presentconformance` は、共通の session / swapchain を通して、同じ場面を両 backend で検査する。
Vulkan の native 検査（`-vulkanpresentcheck`）と同じ観点を、OpenGL にも初めて当てた。

場面:
- 可視 window で、本番 loop と同じく window target に描いて `Present` する。Vulkan は Present の中で
  acquire するので、明示的な acquire は使わない。
- window の resize と、swapchain の追従。
- surface が提供する present mode をすべて要求し、要求がそのまま報告されること。
- 最小化した window の frame が 250 ms 以内に返り、loss を報告しないこと。その後の restore と再描画。
- shutdown。Vulkan は validation 付き。

| backend | frames | present modes | 最小化 |
|---|---|---|---|
| OpenGL | 18 | 2（Immediate / FIFO） | temporarily unavailable、待機なし |
| Vulkan | 21 | 3（Immediate / Mailbox / FIFO） | temporarily unavailable、待機なし |

検証: `C:/tmp/gp/architecture-r9-present-conformance.log`、Vulkan validation error 0。
実際の surface loss / device loss は注入していない。

## 最終画像 gate（R19）

f6e41211 の MSVC Release で、同じ画像検査を両 backend と C# 版に対して取り直した。

| 検査 | 結果 |
|---|---|
| Golden Capture 7候補、OpenGL（以前の OpenGL 撮影との比較） | 7/7 byte 単位で一致 |
| Golden Capture、OpenGL 対 Vulkan | 7/7 PASS |
| renderprobe 4 map（TEST ARENA / MP2 HARVESTER / MP10 OVERLOAD / AD2 ALINOS PERCH）、OpenGL 対 Vulkan | 8/8・4/4・8/8・6/6 PASS |
| 同じ renderprobe、C# 版対 OpenGL | TEST ARENA・MP10 は全一致。MP2・Alinos は spawn03 の1枚だけ不一致 |
| 実 window の HUD 付き（`-hudshots`）、OpenGL 対 Vulkan | TEST ARENA 20/20、MP10 OVERLOAD 25/25 PASS |

C# 版との不一致は、spawn03 の pickup 1個の icon（7×7 / 3×3 px）である。分かっていることは次のとおり:
- C# は実行ごとに同じ結果になる。
- C++ の両 backend は互いに一致する。
- spawn00〜02 は一致する。
- この作業より前の build（f8444609）でも同じ差が出る。

したがって描画ではなく、移植のゲームロジック（item の出現 timing、RNG の消費、または probe 中の
取得）か probe harness の進め方の差である。rendering architecture の範囲外として別 task にした。
再現手順は、spawn03 の画像を `tools/validate-cross-backend-parity.py` で比較すること。

## Vulkan 責務分離の最終監査（R10）

レビュー §11.3 の推奨構造と、現在の owner の対応:

| 推奨 | 現在 | 状態 |
|---|---|---|
| VulkanDevice | `VulkanGraphicsDevice.cpp`（device state、生成の窓口、owner の調停） | 3,652 → 2,165 行 |
| VulkanResources | `VulkanResources`（native 生成・検査・rollback）、`VulkanMemory`（VMA・admission）、`VulkanResourceWrappersInternal.inc`（公開 wrapper） | 分離済み |
| VulkanDeferredDeletion | device state の retirement queue（共通 `RetirementQueue` + `SubmissionSerial`） | 共通型で実装。独立ファイルにはしていない |
| VulkanFrameScheduler | `VulkanFrameScheduler`（queue submit / timeline）、`VulkanFrameSlots`（fence / descriptor 世代） | 分離済み |
| VulkanDescriptorAllocator | `VulkanDescriptorAllocator` | 分離済み |
| VulkanUploadContext | `VulkanUploadArena`、`VulkanTransferScratch`、`VulkanRgbTransfer` | 分離済み |
| VulkanPipelineLibrary | `VulkanPipelineCache`（native library）、`VulkanPipelineInternal.inc`（semantic cache） | 分離済み |
| VulkanFeatureProbe | `VulkanFeatureProbe` | 分離済み |
| VulkanSwapchain | `VulkanSwapchain` | 分離済み |
| VulkanDiagnostics | `VulkanGpuDiagnosticsInternal.inc` | 分離済み（同じ TU） |
| （command list） | `VulkanCommandListInternal.inc`、`VulkanCommandSlots`（native pool / buffer） | 今回 device 本体から分けた |

今回の分割は、command list（1,376 行）と公開 resource wrapper（114 行）を、既存の `*Internal.inc` と
同じ形で device 本体から別ファイルへ移したものである。同じ translation unit のままで、挙動は変えていない。

検証: CPU テスト 19/19、`-rhiconformance` / `-presentconformance` 両 backend PASS、試合中3回切替
（Vulkan 開始、validation 0）PASS。

残るもの:
- deferred deletion は、独立した型ではなく device state の queue である。共通の
  `RetirementQueue` / `SubmissionSerial` で表しており、責務としては分かれている。
- command list と device state は、同じ TU の内部型を共有したままである。ヘッダを持つ別 TU に分けるのは、
  内部型の公開範囲を決め直す作業になり、将来の backend 追加のときに行う。
