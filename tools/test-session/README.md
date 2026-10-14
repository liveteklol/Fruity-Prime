# テストセッション

`start.bat` をダブルクリックするとゲーム画面まで起動します。
`stop.bat` は、このランチャーが起動したゲーム・bot・サーバーをまとめて終了します。
起動処理の本体は `scripts/` にまとめています。

既定値は **データシュライン、カンデン、bot 7体（AIレベル2）、botのRTT 250ms**。
あなたの接続には遅延を加えません。botは独立したAIクライアントです。
接続先は `127.0.0.1:27888`（使用中なら空きポート）、botの稼働時間は2時間です。
ゲーム画面を閉じた後も、全プロセスの終了には `stop.bat` を使ってください。

リポジトリ直下のターミナルから、設定を変えて起動する例：

```bat
tools\test-session\start.bat -Bots 3 -BotPingMs 250 -Hunter Kanden
```

使用する実行ファイルは `tools/build/out/msvc-Release/FruityPrime.exe`。
先に `tools/build/build-cpp.bat msvc Release` でビルドしておいてください。
ログと終了用のプロセス情報は `tools/build/out/` に保存します。
