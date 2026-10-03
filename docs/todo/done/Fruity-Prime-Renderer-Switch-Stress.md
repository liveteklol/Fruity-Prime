# Windows OpenGL / Vulkan renderer切替stress

native `msvc-Release` の実GPU・実ウィンドウを使う診断。
ゲームファイルを指定する`paths.txt`をexeの横に置き、先にReleaseをビルドする。
Androidのライブ切替、Metal / D3D12、macOSは今回の対象に含めない。

リポジトリのルートから実行する:

```powershell
./tools/rendering-stress.ps1 -Cycles 1 -Backend opengl
./tools/rendering-stress.ps1 -Cycles 1 -Backend vulkan
./tools/rendering-stress.ps1 -Cycles 100 -Backend opengl -OutputDirectory C:/tmp/rendering-stress-100
```

同時には実行しない。既存の出力ディレクトリは上書きしない。
`-BuildDirectory`で別のビルド先、`-Room`で別のroom keyを指定できる。
標準の部屋はAlinos Perch（`AD2 ALINOS PERCH`）。
環境変数と`launcher.txt`の元のbytesは、正常終了・検査失敗時とも`finally`で戻す。
PowerShell自体を強制終了した場合には`finally`の実行は保証されない。

## 1 cycleで確認すること

1. front screenを描き、rendererを切り替える。
2. Play画面でthumbnailと実際のhunter previewを描き、新しい試合を読み込む。
3. Syluxを出現させ、7体の非main actorも残す。actorの操作を固定して、通常の死亡と切替不具合を区別する。
4. textureのみを使っていたmodel、105件のeffect definitions、着弾粒子、新旧Lockjaw bombを用意する。
5. windowを1280/1400×800に変更し、resolution scaleを75へ変える。
   scene targetのreadbackサイズが変更後のサイズに合い、通常のresizeでRHI device-wide waitが増えないことを確認する。
6. pause → Settings → Renderer → apply → Resumeを通して試合中に切り替える。
   実際の切替の直前直後で、同じscene、simulation / actor / bomb / particle / bindingをwitnessで検査する。
7. 切替後のbackendでも同じwindow / render target resizeを通す。
8. 試合を終了し、sceneとtexture sourceが解放されることを確認する。

1 cycleはfront screenと試合中の**2回のrenderer切替**を含む。
100 cyclesなら100試合、200切替、両backend各100回のresizeを通す。
rendererは毎回OS window / GPU resourcesを作り直し、試合中の切替ではsimulationを保持する。

旧scene / UI / commands / swapchainの解放後、sessionを閉じる前の診断checkpointで
RHIのlive resource count=0、retired=0、graphics / validation errors=0を確認する。
この明示的transition境界でだけcache trim / device idleを実行する。
最後のwindow破棄にも別のcheckpointを設け、最後のbackendも検査する。
これはRHIが数えるresourcesのgate。Skiaやdriver内部の全確保量を測定するものではない。

## 出力と成功条件

出力先に`run.log`、`run-info.json`と`captures/cycle-N/`を作る。
`run-info.json`はsource HEAD、実行exeと`paths.txt`のSHA-256、条件、時刻、完了状態を記録する。
各cycleは7枚のPNGを保存する。実際のwindowを読み、world / HUD / UIを含む。
PNGの保存数とheader、全cycle完了、最終資源解放、切替witness数、両backendのresize数をスクリプトが検査する。
途中でwindowを閉じた場合は成功にしない。

ログの成功行は次の形になる:

```text
[render stress] PASS; cycles=100; renderer switches=200; misses=0
[render stress] final release PASS; live=0; retired=0; errors=0
Renderer stress PASS: cycles=100; switches=200; captures=700; log=...
```

PNGは比較用の証拠として保存する。画像同士の自動pixel parityは、このスクリプトでは判定しない。
固定frameのGolden Captureとの比較と、全format / subresourceの共通conformanceは別のgateとして残る。
このstressはvalidationを有効にするため、速度比較用のFPSには使わない。
FPSは[別の計測手順](Fruity-Prime-FPS-Measurement.md)を使う。
