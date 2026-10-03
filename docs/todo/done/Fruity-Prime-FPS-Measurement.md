# OpenGL / Vulkan の FPS 計測

Windows native desktop の計測機能。HUD の **FPS Counter** を Off にしていても利用できる。
描画・テクスチャ処理の CPU への移動や、モデルのピクセル複製は追加しない。

## 普段のプレイで記録する

ビルド先 `tools/build/out/msvc-Release` から起動する。
ゲームファイルの `paths.txt` はこの exe と同じディレクトリに置く。

```powershell
.\FruityPrime.exe -launcher -rhi opengl -fpsmeasure C:/tmp/opengl-fps.csv -fpscap unlimited -noupdate
.\FruityPrime.exe -launcher -rhi vulkan -fpsmeasure C:/tmp/vulkan-fps.csv -fpscap unlimited -noupdate
```

2つを同時には実行しない。同じマップ、プレイヤー数、視点、解像度、描画設定で比較する。
ウィンドウを最小化せず、フォーカスを保つ。設定画面で renderer を変えても同じ CSV に続けて記録する。
CSV は指定ファイルを新規作成／上書きし、アプリ終了時に閉じる。

`-fpsmeasure` と同時に指定した `-fpscap` は計測中の一時的な上限になる。
設定の保存・renderer 切替でも上書きされず、ユーザー設定には保存しない。
`unlimited` / `uncapped` はこの計測モードでは描画ループの上限を外して Immediate を要求する。
通常の設定画面の Unlimited は既存の500 FPS設定のまま。
`-fpscap display` はディスプレイ同期を計測したい場合に使う。
`-fpscap` を省略すると設定画面の上限をそのまま使う。

## GPU 時間も調べる

```powershell
.\FruityPrime.exe -launcher -rhi vulkan -fpsmeasure C:/tmp/vulkan-gpu.csv -fpscap unlimited -gpuprofile -noupdate
```

`-gpuprofile` は GPU timestamp で64描画に1回シーンを測る。
OpenGL でも同じ引数を使う。CPU時間から GPU時間を推測する方式ではない。
結果がまだ来ていなければ次の描画へ進み、計測結果を待つための host / device wait は挿入しない。
query は解放待ちを含め native set 8個までに制限し、枠がなければ測定をスキップする。
切替時は古い query を終了し、新しい device 上で測り直す。

計測用 query・debug label・結果確認にも負荷がある。
速度比較の基準は `-gpuprofile` を省略した FPS 計測とし、GPU 時間は別の実行で調べる。
速度比較では `-vkvalidation` を付けない。検証レイヤーを有効にした正当性の検査は別に実行する。

## CSV の読み方

設定、renderer、room、解像度、pause、focus が変わると `segment` を更新する。
各 segment の最初の2秒をウォームアップとして除き、その後約1秒ごとに1行を出す。
最後の1秒未満の区間は出力しない。

| 列 | 内容 |
|---|---|
| `backend`, `room_id`, `width`, `height` | 実際の renderer、room ID、ウィンドウの framebuffer サイズ |
| `fps_cap`, `present_requested`, `present_actual` | 計測上限、要求した同期方式、実際の同期方式。方式は0=Immediate、1=Fifo、2=Mailbox |
| `resolution_scale`, `fps_counter`, `cel`, `fog` | 描画倍率と描画設定。bool は0/1 |
| `paused`, `focused` | メニュー／ダイアログの pause とウィンドウの focus。比較では pause=0、focus=1を選ぶ |
| `frames`, `seconds`, `fps` | 完了した描画コールバックの間隔数、経過秒、`frames / seconds` |
| `mean_frame_ms`, `p50_ms`, `p95_ms`, `p99_ms` | 完了した描画の間隔の平均と百分位。CPU描画開始までの待ちも含む |
| `percentile_samples` | 百分位に使ったサンプル数。各区間の先頭8192個まで。FPSの総数・時間はこの制限を受けない |
| `mean_loop_ms` | コールバック開始から presentation 呼び出し終了までの平均。simulation、描画、UI等を含む |
| `mean_present_ms` | presentation 呼び出しにかかった CPU 時間。GPU時間ではない |
| `gpu_scene_samples`, `gpu_scene_mean_ms` | 区間中に結果が届いた GPU sample 数と平均。scene/world/HUD 描画。後段の launcher overlay・presentation は含まない |
| `gpu_dropped_samples` | query の native 容量不足でスキップした数 |

これはアプリの描画速度であり、モニターに表示されたフレーム数の測定ではない。
presentation が一時停止している区間も描画コールバック自体は数え得る。
GPU 時間は部分的な測定で、CPU loop 時間と足し合わせるものではない。
query 未対応・FPSのみの計測では GPU sample は0、GPU時間は空欄。

## 自動比較・切替回帰テスト

既存の `-shellshot` に `FRUITY_FPSCHECK=1` を加えると、同じ試合の各 backend で4秒間続けて描画する。
8人の出現直後は共通200粒子の枠が埋まるため、エフェクトの probe は240 **simulation ticks** 待ってから置く。
着弾エフェクトを置いた後に長い測定待ちを入れて、その自然な寿命切れを切替の不具合と判定しない。
元からあるボムの寿命・owner・effect と各切替時の world witness の検査は維持する。

```powershell
$captureRoot = "C:/tmp/fps-switch-$(Get-Date -Format yyyyMMdd-HHmmss)"
$names = @('FRUITY_SWITCHCHECK','FRUITY_SHOT_ROOM','FRUITY_SWITCHCHECK_HOLD_ACTORS',
    'FRUITY_SWITCHCHECK_WITNESS_SELFTEST','FRUITY_SWITCHCHECK_FAILURES','FRUITY_FPSCHECK')
$savedEnv = @{}
foreach ($name in $names) { $savedEnv[$name] = [Environment]::GetEnvironmentVariable($name,'Process') }
Push-Location tools/build/out/msvc-Release
$prefsPath = Join-Path $PWD 'launcher.txt'
$hadPrefs = Test-Path -LiteralPath $prefsPath
$prefsBytes = if ($hadPrefs) { [System.IO.File]::ReadAllBytes($prefsPath) }
try {
    $env:FRUITY_SWITCHCHECK = '1'
    $env:FRUITY_SHOT_ROOM = 'AD2 ALINOS PERCH'
    $env:FRUITY_SWITCHCHECK_HOLD_ACTORS = '1'
    $env:FRUITY_SWITCHCHECK_WITNESS_SELFTEST = '1'
    $env:FRUITY_SWITCHCHECK_FAILURES = $null
    $env:FRUITY_FPSCHECK = '1'
    foreach ($backend in @('opengl','vulkan')) {
        $dir = "$captureRoot/$backend"
        New-Item -ItemType Directory -Path $dir -Force | Out-Null
        'q' | & .\FruityPrime.exe -shellshot $dir -rhi $backend -fpscap unlimited -fps off `
            -fpsmeasure "$captureRoot/$backend.csv" -noupdate *> "$captureRoot/$backend.log"
        if ($LASTEXITCODE -ne 0) { throw "FPS/switch failed: $backend" }
        $witnesses = @(Select-String -Path "$captureRoot/$backend.log" -SimpleMatch '[switch witness] PASS;')
        if ($witnesses.Count -ne 3) { throw "Missing switch coverage: $backend" }
        $rows = @(Import-Csv "$captureRoot/$backend.csv" | Where-Object { $_.room_id -ge 0 -and $_.paused -eq '0' -and $_.focused -eq '1' })
        foreach ($measured in @('opengl','vulkan')) {
            $samples = @($rows | Where-Object { $_.backend -eq $measured })
            if (-not $samples.Count) { throw "Missing FPS rows: $measured" }
            if (@($samples | Where-Object { $_.fps_counter -ne '0' -or $_.fps_cap -ne 'unlimited' -or [double]$_.fps -le 0 }).Count) {
                throw "FPS conditions differ: $measured"
            }
        }
    }
} finally {
    if ($hadPrefs) { [System.IO.File]::WriteAllBytes($prefsPath,$prefsBytes) }
    elseif (Test-Path -LiteralPath $prefsPath) { Remove-Item -LiteralPath $prefsPath }
    foreach ($name in $names) { [Environment]::SetEnvironmentVariable($name,$savedEnv[$name],'Process') }
    Pop-Location
}
Write-Output "Captures, CSV and logs: $captureRoot"
```

GPU 時間の回帰確認は同じコマンドに `-gpuprofile` を追加し、両 backend に
`gpu_scene_samples > 0` の行があることも検査する。
正当性は `-rhiconformance -noupdate` と `-vkvalidation` を付けた switch run を別に実行する。
実測結果と対象外の範囲は [アーキテクチャ対応記録](Fruity-Prime-Rendering-Architecture-Implementation.md) の R18 に記録する。
