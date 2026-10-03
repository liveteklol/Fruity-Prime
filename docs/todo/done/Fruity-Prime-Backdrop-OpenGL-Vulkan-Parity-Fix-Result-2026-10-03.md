# Launcher backdrop OpenGL / Vulkan parity 修正結果

2026-10-03。対象は [修正指示書](Fruity-Prime-Backdrop-OpenGL-Vulkan-Parity-Fix-Instructions.md)。
開始時 HEAD は `84419c601d63f0045cc207af0013267537f6a4de`、branch は `develop3_rendering`。
Windows / NVIDIA GeForce RTX 5070 Ti / driver 617.14 で実描画を検証した。

## 修正

`BackdropNoise.hpp/.cpp` に unsigned integer arithmetic の固定 64×64 RGBA8 table と共通 RHI resource creator を追加した。
RGB は同じ hash byte、A は255。RGBA8Unorm、Sampled | TransferDst、mipLevels=1。
sampler は Nearest / Repeat / LOD 0。texture の upload は初期化時の一度だけ。

`BackdropFragmentShader` の `sin` hash を除去し、整数格子の texel 中心を読む
`lattice_noise()` と既存の手動 smooth interpolation に置き換えた。
`SceneShaderAbi.def` の `noise_tex` は既存 PostAuxTexture / PostAuxSampler、unit 1。
OpenGL の borrowed photo path を維持し、OpenGL と Vulkan の window UI が共通 creator の resource を所有・bind・unbind・解放する。
共通 shader を参照する Android の backdrop 呼び出し側にも同じ unit 1 binding を追加した。

photo crop、strength=0.62、view size による cell scale、時間係数 2.0 / -1.6、warp 強度4.4、色調式は変更していない。
world shader、vertex layout、swapchain Y flip、general upload orientation は変更していない。
Vulkan shader generator の変更も不要だった。

## 修正前・修正後の A/B

診断は shader fragment body のみを一時交換し、実際の `OpenGlWindowDraw` / Vulkan `WindowUi` で取得した。
`tools/run-backdrop-diagnostics.py` は元の source bytes を finally で戻し、production binary を再ビルドする。
production shader に診断 branch / uniform は残していない。

| 診断 | 最大 channel 差 (RGBA8) | 最大平均 channel 差 | 結果 |
|---|---:|---:|---|
| A: NoiseCoord | 1 | 0.00321181 | 座標経路一致 |
| B: fract(UV/cells) | 1 | 0.000454848 | cell scale / orientation 一致 |
| C: legacy sin hash(floor(UV)) | 245 | 11.1037 | 格子ごとの divergence を実証 |
| 固定 texture lattice | 0 | 0 | 完全一致 |
| D: 手動補間 value_noise(UV) | 1 | 0.0000333659 | 一致 |
| 最終 backdrop | 1 | 0.0000172255 | PASS |

各診断は29 capture × 2 session cycle = 58組。
1280×720 と1920×1080、time=0 / 0.25 / 0.5 / 1秒、strength=0 の photo-only control、
リサイズ復帰、monitor 全域の borderless fullscreen と復帰、0〜1秒の16-frame animation を含む。
photo-only control は両backendで完全一致。session 再生成後と復帰後の固定条件画像も一致。
animation の隣接frame平均差の backend 間の最大差は `0.00000253183` 階調。
Vulkanだけ異なるcell形状や時間変化を生じる状態は解消した。

## Regression gate

ゲームファイル不要の `-backdropparity OUTPUT_DIR -noupdate` を追加した。
両backend必須、Vulkan validation 必須。固定fixtureのcropと独立 NoiseCoord を production と同じ strip に与える。
最大 channel 差 ≤2、平均差 ≤0.1 を gate とし、animation が動くことと写真への fallback が起きていないことも検査する。
RGBA8 readback は PPM と `metrics.csv` に保存する。`-backdropobserve` は診断の観測専用で、画像差の gate だけを無効にする。
device error、validation、presentation、resize復帰、fullscreen復帰、session再生成、resource release の検査は観測時も有効。
各cycleで UI、photo、sampler、reader を解放し cache trim / WaitIdle 後の live objects=0、retired=0 を確認する。
APIごとの creator / hash 実装は追加していないため、将来backendを加える際も共通 lattice とこの固定条件を利用できる。

実行例（Windows MSVC）：

```powershell
tools\build\build-cpp.bat msvc Release
tools\build\out\msvc-Release\FruityPrime.exe -backdropparity tools\build\out\backdrop-parity-final -noupdate
python tools\run-backdrop-diagnostics.py --output tools\build\out\backdrop-diagnostics
```

runner は MSVC Release build wrapper を使う。実行中は別の build / shader 編集を行わない。
GUI binary を PowerShell で直接呼ぶ場合は `Start-Process -Wait -PassThru` で終了コードを確認する。
GPUと画面のあるCI runnerから同じコマンドを実行し、出力directoryをartifactとして保存できる。

## 検証

- MSVC Release build: PASS。
- 最終 production `-backdropparity`: exit 0、58比較、最大1階調、2階調を超えるchannelは0。
- OpenGL error=0、Vulkan validation error=0、各session解放後 live=0 / retired=0。
- 関連 CTest 5件: ShaderInterface、RhiLifetime、VulkanSceneUniforms、SceneShaderAbiDrift、VulkanResources 全て PASS。
- `FRUITY_SWITCHCHECK=1` の production `-shellshot ... -rhi opengl -vkvalidation -noupdate`: exit 0。
  front の OpenGL→Vulkan、試合中の Vulkan→OpenGL→Vulkan→OpenGL、実windowの可視性、geometry維持、Settingsからの復帰、simulation継続、effect / texture retention を確認。
  保存設定 `launcher.txt` と `.bak` は検証前の bytes に復元した。
- Android arm64: 今回影響する BackdropNoise / OpenGlLauncherPhoto / VulkanGraphicsDevice / BackdropParityCheck / Shaders の object compile は PASS。
  全native archive build は既存 `RendererAndroid.cpp` の incomplete `FramePerformance` type error で停止したため、Android全体のbuild / device runtimeは未検証。
  今回のdesktop backdrop修正とは別のため、この箇所は変更していない。

初回のshell検証では診断側が無効な表示名 `COMBAT HALL` を room key に指定して crash した。
この実行は成功の根拠に含めていない。標準fixtureで修正前baselineも修正後productionも完走した。
背景修正を理由とする general scene / window convention の変更は行っていない。

## ローカル成果物

生成物は `tools/build/out/` に置き、Gitには追加していない。

- `backdrop-parity-before/{coord,cell,legacy}/` と `summary.json`：修正前診断。
- `backdrop-parity-after/{lattice,value,final}/` と `summary.json`：修正後診断。
- `backdrop-parity-final/`：production の全PPM / metrics.csv / 比較画像 `comparison.png` / 差分PNG / `animation.gif` / `temporal.json`。
- `backdrop-final-build.log`、`backdrop-final-run.log`、`backdrop-final-errors.log`：production build / gate。
- `backdrop-switch-final-run.log`、`backdrop-switch-final-errors.log`、`backdrop-switchcheck-final/*.png`：実launcher / match switch。
- `backdrop-android-focused.log`、`backdrop-android-build.log`：Android object成功と全archiveの既存失敗を区別する証拠。

完成範囲は今回の Windows OpenGL / Vulkan backdrop parity。Metal / D3D12 と他GPUでの実描画は未実施。
