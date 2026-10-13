# ネットワーク対戦の目視確認セッション

MSVC Release版で、操作するプレイヤー1人と、遅延を付けたAIネットワーククライアントを同じローカル対戦に参加させる手順。専用サーバー、AIクライアント、操作用ゲーム画面は別プロセスで起動する。

## 前提

- リポジトリのルートでPowerShellを開く。
- `tools/build/out/msvc-Release/FruityPrime.exe` と、その隣の `paths.txt` およびゲームデータを用意する。
- サーバーとクライアントは同じビルドを使う。
- `-netlag MS` は各クライアントに設定する往復遅延（RTT）。`250`なら片道およそ125msずつを遅らせる。

この例はCombat Hall、操作プレイヤー1人、AIネットワーククライアント3人の4枠構成。AIクライアントはSylux、Samus、Kandenで、各クライアントに250ms RTTを設定する。専用サーバーの `-bots` は使わない。サーバー内Botは独立したネットワーククライアントではなく、Botごとの `-netlag` を適用できないため。

目視確認ではポイントゴールを999にする。rotationの列は `ルーム | モード | 制限時間（分） | ポイントゴール`。

## 起動

次のブロックを同じPowerShellで実行する。補助のサーバーとAIクライアントは非表示で起動し、最後に操作用ウィンドウを表示する。

```powershell
$ErrorActionPreference = 'Stop'
$outDir = (Resolve-Path 'tools/build/out/msvc-Release').Path
$exe = Join-Path $outDir 'FruityPrime.exe'
$port = 27888
$sessionDir = Join-Path $outDir ('manual-network-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $sessionDir -Force | Out-Null

# ルームローテーションを1部屋に固定する。
$rotation = Join-Path $sessionDir 'rotation.txt'
Set-Content -LiteralPath $rotation -Value 'MP3 PROVING GROUND | Battle | 7 | 999' -Encoding ascii

if (Get-NetUDPEndpoint -LocalPort $port -ErrorAction SilentlyContinue) {
    throw "UDP port $port is already in use. Change `$port and use the same port for every process."
}

# 専用サーバー。-nomasterで公開サーバー一覧へ登録しない。
$server = Start-Process -FilePath $exe -WorkingDirectory $outDir -WindowStyle Hidden -PassThru `
    -ArgumentList @('-server','-port',"$port",'-players','4','-rotation',$rotation,
        '-nomaster','-servername','VisualCheck','-noupdate') `
    -RedirectStandardOutput (Join-Path $sessionDir 'server.stdout.txt') `
    -RedirectStandardError (Join-Path $sessionDir 'server.stderr.txt')

for ($attempt = 0; $attempt -lt 20; $attempt++) {
    if ($server.HasExited) { throw "Server exited; inspect $sessionDir" }
    if (Get-NetUDPEndpoint -LocalPort $port -ErrorAction SilentlyContinue) { break }
    Start-Sleep -Seconds 1
}
if (-not (Get-NetUDPEndpoint -LocalPort $port -ErrorAction SilentlyContinue)) {
    throw "Server did not bind UDP port $port; inspect $sessionDir"
}

# 各Botは実際にサーバーへ接続するAIクライアント。-secondsは自動終了までの秒数。
$botSpecs = @(
    @{ Name = 'BotSylux'; Hunter = 'Sylux' },
    @{ Name = 'BotSamus'; Hunter = 'Samus' },
    @{ Name = 'BotKanden'; Hunter = 'Kanden' }
)
$bots = @()
foreach ($spec in $botSpecs) {
    $stdout = Join-Path $sessionDir ($spec.Name + '.stdout.txt')
    $stderr = Join-Path $sessionDir ($spec.Name + '.stderr.txt')
    $bots += Start-Process -FilePath $exe -WorkingDirectory $outDir -WindowStyle Hidden -PassThru `
        -ArgumentList @('-netcheck','127.0.0.1','-port',"$port",'-name',$spec.Name,
            '-hunter',$spec.Hunter,'-ai','1','-headless','-seconds','86400',
            '-netlag','250','-noupdate') `
        -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    Start-Sleep -Seconds 2
    if ($bots[-1].HasExited) { throw "$($spec.Name) exited; inspect $sessionDir" }
}

# 操作用クライアント。ここには -netlag を付けない。
$human = Start-Process -FilePath $exe -WorkingDirectory $outDir -WindowStyle Normal -PassThru `
    -ArgumentList @('-connect','127.0.0.1','-port',"$port",'-name','Player',
        '-hunter','Noxus','-noupdate')

Start-Sleep -Seconds 8
"Server PID: $($server.Id)"
"Bot PIDs:   $($bots.Id -join ', ')"
"Game PID:   $($human.Id)"
"Session logs and rotation: $sessionDir"
Get-Content (Join-Path $sessionDir 'server.stdout.txt') -Tail 20
```

サーバーログに4枠目の `Player` が参加したことが出て、ゲームウィンドウのタイトルが `Combat Hall` になれば起動完了。各AIは24時間で自動終了する設定なので、手動確認後は下記で止める。

## 終了

起動に使った同じPowerShellで実行する。ウィンドウは通常の方法で閉じ、非表示のBotとサーバーは保存したプロセスIDだけを停止する。

```powershell
$null = $human.CloseMainWindow()
$bots | Stop-Process -ErrorAction SilentlyContinue
$server | Stop-Process -ErrorAction SilentlyContinue
```

別のPowerShellを使う場合は起動時に表示された各PIDを指定する。`Get-Process FruityPrime | Stop-Process` のようにプロセス名だけで停止すると、別のFruity Primeプロセスまで終了するので使わない。

## 変更例

- ポートが使用中なら `$port` を空いている値に変更する。サーバー・AI・操作クライアントは同じ値を使う。
- 部屋を変更する場合はrotationの `MP3 PROVING GROUND` を `-rooms` が表示する内部キーに置き換える。Combat Hallの内部キーは `MP3 PROVING GROUND`。
- Sic Transitの内部キーは `MP12 SIC TRANSIT`。rotationの例は `MP12 SIC TRANSIT | Battle | 7 | 999`。
- Botの遅延を変える場合はAI起動引数の `'-netlag','250'` を変更する。全Botを同じ条件にするなら各AIクライアントの値をそろえる。
- AIクライアントではなくサーバー内AIを使う構成は `-server -bots N` で作れるが、サーバー内Botにはクライアント別の `-netlag` を設定できない。
