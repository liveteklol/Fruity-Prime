# Fruity-Prime C++版 観戦モード修正後監査・追加修正指示

- 監査日：2026-10-14（JST）
- リポジトリ：`Zection6V/Fruity-Prime`
- ブランチ：`develop11_netCode`
- 監査コミット：`13f9e2c6e3e75f4c339ebdd585a45682f2dad069`（`Fix spectator target lifecycle and combat eligibility`）
- 比較元：`ca12c121d8e45f97a3fd7c9f389819f935cba60c`
- 変更範囲：1コミット、22ファイル（CMakeLists.txt・ヘッダー・テストを含む）
- 前回指示書：`FruityPrime_CPP_Spectator_Bugs_Investigation_and_Fix_Instructions_2026-10-13.md`
- **SPEC-02（観戦中の死亡後の射撃音消失）は保留とする。今回は音声機構の修正を要求しない。**
- 調査方法：GitHubコネクタで監査コミットのC++実装、差分、テストを読み取った静的監査。実機ゲームプレイ・ローカルフルビルドは未実施。
- GitHub Actions：[`build_cpp` run 38430244239](https://github.com/Zection6V/Fruity-Prime/actions/runs/38430244239) は監査時点で **in_progress**。一部の静的監査ジョブは成功、Windows / Linux / macOS / Androidのビルドとテストの最終成功は未確認。再実行・キャンセルはしない。
- **ソースコード変更・コミット・プッシュは行わない。**

## 1. 総合判定

**判定：要追加修正。** SPEC-01とSPEC-03の主要な設計は改善したが、SPEC-03には具体的なライフサイクル欠陥と、入力とは別に届くShot履歴の防御漏れが残る。SPEC-01にもフリーカメラ復帰における条件の不整合がある。SPEC-02について合否を判定しない。

| ID | 優先度 | 確度 | 監査結果 | 対応 |
| --- | --- | --- | --- | --- |
| AUD-SPEC-01 | **P0** | **コード経路で確定** | `ApplyState`が観戦フラグを設定した後、`BeginRemoteLife` → `Spawn`により観戦フラグが消える | **修正必須** |
| AUD-SHOT-01 | **P0** | **コード経路で確定、実際の悪用／再現は未確認** | `NetShotEvents`のShot履歴経路が観戦状態の適格性を確認せず、リモート発射処理に到達し得る | **修正必須、過去の正当な発射を壊さないこと** |
| AUD-SPEC-03 | P1 | **状態競合の経路を確認、実機影響未検証** | ローカルの観戦開始直後、遅延した権威側Snapshotが`ModSetSpectating(false)`でローカル状態を上書きし得る | 遷移境界と回帰テストが必要 |
| AUD-SPEC-04 | P1 | **条件分岐で確定、再現条件限定** | 追従対象なしで`CanUsePlayerCamera()`がローカルスロットを許可し、直接`ToggleFreeCamera()`するとプレイヤーカメラへ戻り得る | 修正＋デモ再生を含むテスト |
| AUD-SPEC-05 | P1 | **テスト範囲から確定** | 新規単体テストはポリシー中心。実際のカメラ切替・初回Spawn・Shot履歴と再接続の統合検証が足りない | テスト追加 |
| SPEC-02 | 保留 | — | 射撃音問題はユーザー判断で保留 | **変更しない** |

### 評価できる実装

- `PlayerCombatPolicy.hpp`が `Active && Spawned && Health > 0 && !Spectating` を一元化し、`ModInPlay()`と`ModIsInPlay()`が同じ規則になった。
- `PlayerEntity::TakeDamage()`に最終ダメージ防壁が追加され、`NetDamage::Refuses()`も観戦中を拒否する。
- `NetHitClaims`は初期検証と適用直前に観戦資格を再確認し、`NetUnlagged`の対象除外も共通条件を使う。
- `NetPlayerIntent::CaptureState()`は観戦中の操作ボタンと`InPlayState`を送らない。
- `NetPlayerBridge::ApplyIntent()`は矛盾する`SpectatingState`/`InPlayState`を安全側に正規化し、`NetInputPressReplay::Discard()`で過去の押下履歴の再生を避ける。
- `SpectatorTargetPolicy.hpp`はターゲットの存在、アクティブ、Spawn済み、HP、観戦フラグ、Roster、スロット世代、LifeIdを判定する。
- `SpectatorMode`に世代付きターゲット、無効化通知、再選択とフリーカメラへの退避が追加され、`Scene`に最後の有効カメラ姿勢のキャッシュが導入された。
- `TestSpectatorPolicies.cpp`と`NetCombatCheck::SpectatorInputAndDamageAreRefused()`が追加された。ポリシーの設計はSRPとDRYに概ね適合する。

## 2. AUD-SPEC-01 / P0：初回Snapshotで観戦フラグがSpawnによって消える

### 事実と再現可能な呼出経路

1. [`NetPlayerBridge.cpp` L423～430](https://github.com/Zection6V/Fruity-Prime/blob/13f9e2c6e3e75f4c339ebdd585a45682f2dad069/src/MphRead.Native/Mods/Network/Gameplay/Player/NetPlayerBridge.cpp#L423-L430) の `ApplyState()` は、`const bool fresh = ...`を求めた後、`player.ModSetSpectating(spectating)`を**先に**実行する。
2. `fresh == true` のとき、その直後に `BeginRemoteLife(player, state)` が呼ばれる。
3. [`NetPlayerBridge.cpp` L355～400](https://github.com/Zection6V/Fruity-Prime/blob/13f9e2c6e3e75f4c339ebdd585a45682f2dad069/src/MphRead.Native/Mods/Network/Gameplay/Player/NetPlayerBridge.cpp#L355-L400) では、`state.LifeId != 0` なら `player.ModNetSpawn(...)` に進む。
4. その先の[`PlayerEntity::Spawn()` L899～902](https://github.com/Zection6V/Fruity-Prime/blob/13f9e2c6e3e75f4c339ebdd585a45682f2dad069/src/MphRead.Native/Entities/Players/PlayerEntity.cpp#L899-L902) は `_flags2 = PlayerFlags2::NoShotsFired;` と代入し、`Spectating`ビットを落とす。
5. `ApplyState()`後段で`ModSetSpectating(spectating)`を再適用する箇所は、今回の修正で削除されている。

このため、**`fresh==true && state.LifeId!=0 && state.FlagsにFlagSpectating` の組合せで、正常に復帰した関数の戻り時に `player.ModIsSpectating()==false` が成立し得る。** 権威Snapshotの観戦状態と、実際のエンティティの観戦状態が食い違う。以後の新たなSnapshotで再設定されるとしても、その間は描画・被弾資格・追従資格の判定が誤る。

### 最小修正指示

- `BeginRemoteLife()`で起こる`Spawn()`の**後**、かつ`NetDamage::Replay()`およびその他の観戦可否を利用する処理の**前**に、Snapshotの観戦フラグを最終的に反映する。
- `fresh`以外に`BeginRemoteLife()`が実行されるルート（同じLifeIdでHealthが再同期されるケース）にも、同じ不変条件を保証する。
- `PlayerEntity::Spawn()`全体の`_flags2`初期化を無条件に削除しない。一般Spawnの旧仕様とHUD・アニメーションなどへの波及を避けること。
- 実装上の不変条件：**`ApplyState`正常復帰時、適用対象の観戦フラグは採用したSnapshotの観戦状態と一致する**（ローカル予測との競合については次項の例外を明示）。
- 変更後の`ModSetSpectating()`には入力履歴クリアの副作用があるため、無駄な`false→true→false`遷移を増やさない。

### 必須テスト

- 初回観戦Snapshot：`LifeId>0`、`Health>0`、`FlagSpawned`、`FlagSpectating`を設定して`ApplyState`。戻り時に`ModIsSpectating()==true`、`ModIsInPlay()==false`を検証。
- 死亡後に観戦Snapshotが初めて来るケース、再接続後の初回Snapshot、スロット世代が更新されたケースを検証。
- 同じLifeIdで通常→観戦→通常へ変わるSnapshot、`BeginRemoteLife()`の再同期ルートを検証。
- 通常プレイヤーのSpawnでは`NoShotsFired`等の既存フラグ初期化が維持されることを検証。

## 3. AUD-SHOT-01 / P0：Shot履歴経路の観戦資格チェックが欠落

**AUD-SHOT-01はShotイベントの戦闘資格に関する追加監査項目であり、保留中のSPEC-02（射撃音問題）とは無関係。**

### 事実

- [`NetPlayerBridge::ApplyIntent()`](https://github.com/Zection6V/Fruity-Prime/blob/13f9e2c6e3e75f4c339ebdd585a45682f2dad069/src/MphRead.Native/Mods/Network/Gameplay/Player/NetPlayerBridge.cpp#L189-L235) は観戦状態なら操作ボタンをクリアする。ここは改善済み。
- しかし[`RemoteIntentChannel.cpp` L76～97](https://github.com/Zection6V/Fruity-Prime/blob/13f9e2c6e3e75f4c339ebdd585a45682f2dad069/src/MphRead.Native/Mods/Network/Session/RemoteIntentChannel.cpp#L76-L97) は、通常のリモート移動入力と別に`NetShotEvents::Receive(slot, intent)`を実行する。古いIntentでもShotイベントを保持する設計である。
- [`NetShotEvents.cpp` L163～179](https://github.com/Zection6V/Fruity-Prime/blob/13f9e2c6e3e75f4c339ebdd585a45682f2dad069/src/MphRead.Native/Mods/Network/Gameplay/Weapons/NetShotEvents.cpp#L163-L179) の`Receive()`は`SpectatingState`を検査しない。
- [`NetShotEvents.cpp` L125～148](https://github.com/Zection6V/Fruity-Prime/blob/13f9e2c6e3e75f4c339ebdd585a45682f2dad069/src/MphRead.Native/Mods/Network/Gameplay/Weapons/NetShotEvents.cpp#L125-L148) の`MayFire()`/`FireReady()`にも`shooter.ModIsSpectating()`または権威側の戦闘参加資格に関するガードがない。
- `FireReady()` → `ModFireShotEvent()` → [`TryFireWeapon()`](https://github.com/Zection6V/Fruity-Prime/blob/13f9e2c6e3e75f4c339ebdd585a45682f2dad069/src/MphRead.Native/Entities/Players/PlayerInput.cpp#L1245-L1270)へ至り、`TryFireWeapon()`も観戦フラグを直接検査しない。

したがって、**操作ボタンの無効化だけではShot履歴による新規発射を保証できない**。観戦中のShot履歴を持つIntentが到着すると、射撃イベントがリモート複製上で処理される経路が残る。ゲーム内での再現・実際の権威側ダメージ確定までは今回確認していないため、そこは断定しない。

### 修正指示

- `NetShotEvents`の受信・キュー投入・発射実行の各境界を監査し、サーバーが確定した戦闘参加資格を使って、**観戦開始後に新しく発射したことになっているShotイベント**を拒否する。
- ただし現行コードには「通常プレイ中に発射され、ネットワーク遅延により観戦開始後に到着した正当な弾」への対応がある。**観戦フラグだけで未消費の過去Shotを全削除する修正は禁止**。発射時点のフレーム、世代、LifeId、既存のShot識別情報を利用し、合法な事前発射と無効な事後発射を判別する。
- ショット受信処理が古いIntentのShot情報を救済することは既存設計上の要件。単に古いIntentを丸ごと捨てない。
- サーバー側の`ApplyIntent`入力遮断に加え、`NetShotEvents::Receive/FireReady`側でも不正な観戦中Shotを発火不能にする。重複受信・遅延受信・再接続・LifeId更新をテストする。
- サーバー権威に対する不正なShot履歴を含むIntentをテストする際、`SpectatingState=false`を偽装しても、最新に承認された観戦状態との整合性をチェックする。クライアントが報告したビットだけを信頼しない。
- 判定に使うクロックがクライアント基準か権威基準かを既存`ShotKey`/`NetUnlagged`仕様と照合し、無検証のフレーム比較や新規プロトコル増設をしない。

### 必須テスト

1. 観戦中のIntentに`SpectatingState=1`、`InPlayState=1`、`ShotHistoryLength>0`を入れても**観戦開始後の新規Shot**は発射できない。
2. 観戦確定後に古いIntentが再送された場合、現在の観戦状態を通常プレイに戻したり、新たなShotを発射したりできない。
3. 観戦前に実際に撃った正当なShotが遅延して到着しても、既存の救済・冪等性ルールを維持する。
4. 正常なプレイヤーの連射、ショットロス再送、爆弾・モーフ形態への影響がない。

## 4. AUD-SPEC-03 / P1：ローカルの観戦状態が遅延Snapshotに上書きされ得る

### 事実とリスク

[`NetPlayerBridge::ApplyState()` L423～430](https://github.com/Zection6V/Fruity-Prime/blob/13f9e2c6e3e75f4c339ebdd585a45682f2dad069/src/MphRead.Native/Mods/Network/Gameplay/Player/NetPlayerBridge.cpp#L423-L430) は、`isLocal`かどうかに関係なく`player.ModSetSpectating(spectating)`を実行する。

一方、[`NetHooks::AfterInput()`](https://github.com/Zection6V/Fruity-Prime/blob/13f9e2c6e3e75f4c339ebdd585a45682f2dad069/src/MphRead.Native/Mods/Network/Integration/Hooks/NetHooks.cpp#L335-L370) はローカルで観戦中なら`ModSetSpectating(true)`を再設定する。ローカルの観戦要求を送信して権威サーバーが受理するまでの間、古いSnapshotに`FlagSpectating=0`が残る可能性がある。

そのときSnapshotで`false`へ戻してから次のローカル入力処理で`true`へ戻すため、**観戦フラグがフレーム間で揺れ、`ModSetSpectating()`の`Controls().ClearAll()`と`NetInputHistory::ForgetSlot()`が無駄に呼ばれ得る**。実際に死亡や射撃音消失を発生させたという証拠はまだない。

### 修正指示

- 「ローカルの観戦要求・即時表示」と「サーバーが受理した観戦状態」を別の責務・状態として扱い、保留中の要求を旧Snapshotが無条件に取り消さないようにする。
- ローカル本人とリモート複製ではSnapshot適用ポリシーを区別する。後から届く正式な拒否・退出・マッチ変更の処理は失わない。
- 既存の権威側被ダメージ規則を変更せず、**クライアントの表示上の観戦状態を維持することと、サーバーによる観戦承認を同一視しない**。
- 最小限の遷移状態で解決し、`PlayerEntity`へネットワーク専用の大量の状態を追加しない。既存の`SpectatorMode`やセッション状態の責務を優先する。
- 往復遅延・パケット順序入れ替え・観戦開始直後の死亡・Rejoin時を含む自動テストを追加する。

## 5. AUD-SPEC-04 / P1：追従対象なしでもプレイヤーカメラに戻れる経路

### 事実

- [`SpectatorMode::CanUsePlayerCamera()` L155～169](https://github.com/Zection6V/Fruity-Prime/blob/13f9e2c6e3e75f4c339ebdd585a45682f2dad069/src/MphRead.Native/Mods/SpectatorMode.cpp#L155-L169) は、観戦中でも `_target`が空なら、`MainPlayerIndex`がローカルスロットでエンティティがあれば`true`を返す。
- [`SpectatorMode::Update()` L210～241](https://github.com/Zection6V/Fruity-Prime/blob/13f9e2c6e3e75f4c339ebdd585a45682f2dad069/src/MphRead.Native/Mods/SpectatorMode.cpp#L210-L241) は、最後のターゲット退出時に`_target.reset()`し、メインをローカルスロットへ戻してフリーカメラを要求する。
- [`Scene::SetFreeCamera(false)` L5009～5032](https://github.com/Zection6V/Fruity-Prime/blob/13f9e2c6e3e75f4c339ebdd585a45682f2dad069/src/MphRead.Native/Renderer.cpp#L5009-L5032) は`CanUsePlayerCamera()==true`なら`CameraMode::Player`へ復帰する。
- [`RenderWindow::OnMouseDown()`](https://github.com/Zection6V/Fruity-Prime/blob/13f9e2c6e3e75f4c339ebdd585a45682f2dad069/src/MphRead.Native/Renderer.cpp#L7103-L7119) ではデモ再生中の右クリックが`Scene::ToggleFreeCamera()`を直接呼ぶ。

よって「最後の観戦対象が退出」「`_target`空」「ローカルスロットのPlayerEntityが存在」「デモ再生中などで`ToggleFreeCamera()`直呼出」という条件で、**有効な追従対象がないのに`CameraMode::Player`へ戻れる**。主要な観戦UIの`SpectatorMode::ToggleView()`とは別の入口であるため、前者だけ修正しても不十分。

### 修正指示

- `CanUsePlayerCamera()`の「本当にプレイヤー視点へ追従してよいか」と「フリーカメラ開始位置としてローカルカメラ姿勢を借りてよいか」を別の質問に分ける。
- 観戦中で有効な追従対象がなければ、UI／デモ再生／デバッグショートカットのどこから切り替えても`CameraMode::Player`へ戻らない。
- 観戦開始時に自由カメラへ移る場合の初期姿勢は、最後の正常な視点を一度だけ保持する。資格判定を厳しくした結果、開始姿勢が原点や無効値になる回帰を起こさない。
- `Scene::SetFreeCamera()`と`SpectatorMode::ToggleView()`の入口を統合できるか検討する。ただしSceneとネットワークライフサイクルのSRPは維持する。
- 「誰もいない → フリーカメラ → デモ右クリック → フリーカメラのまま操作可能」および「新しい入室者が出現 → 適切な操作で追従」が成立することを検証する。

## 6. AUD-SPEC-05 / P1：回帰テストの不足

### 既存テスト（追加済み）

- [`TestSpectatorPolicies.cpp`](https://github.com/Zection6V/Fruity-Prime/blob/13f9e2c6e3e75f4c339ebdd585a45682f2dad069/src/MphRead.Native/Testing/TestSpectatorPolicies.cpp)：候補選択、世代/LifeId、有効性の純粋ポリシーを網羅。
- [`NetCombatCheck.cpp` L325～399](https://github.com/Zection6V/Fruity-Prime/blob/13f9e2c6e3e75f4c339ebdd585a45682f2dad069/src/MphRead.Native/Mods/Network/Testing/Combat/NetCombatCheck.cpp#L325-L399)：矛盾Intent、押下履歴、ダメージ拒否、Claim再検証を追加。
- [`CMakeLists.txt` L494～500](https://github.com/Zection6V/Fruity-Prime/blob/13f9e2c6e3e75f4c339ebdd585a45682f2dad069/CMakeLists.txt#L494-L500)：`FruityPrime.SpectatorPolicies`がCTESTに登録。

### 不足しているカバレッジ

| シナリオ | 現状 | 必須の観測点 |
| --- | --- | --- |
| 初回観戦Snapshot直後のSpawn | 未テスト | 戻り時の`ModIsSpectating`、`ModIsInPlay`、HP・死亡数 |
| SPEC-01退出 → 次ターゲット切替 | ポリシーのみ | 実際の`MainPlayerIndex`、`Scene.CameraMode`、カメラ姿勢 |
| 最後のターゲット退出 → 自由カメラ | ポリシーのみ | マウス回転、WASD移動、ゲームパッド、デモ右クリック |
| ロスター世代変更とShot/Camera要求の競合 | 未テスト | 旧世代ターゲット・旧ショットの再使用なし |
| 観戦状態とShotHistoryの組合せ | 未テスト | 新規不正Shotゼロ、発射前の正当なShot維持 |
| 観戦開始 → 旧Snapshot到着 → 権威承認 | 未テスト | 観戦表示・コントロール・予測履歴の安定 |
| 観戦解除／再参加 | 一部単体のみ | 権威側の戦闘復帰、通常の発射と被弾 |
| SPEC-02射撃音 | **保留** | 本変更では音声判定を要求しない |

テストを追加する際は、単体ポリシーだけで`Scene`の実動作を確認したことにしない。観戦対象の退出、NetSession/NetPlayerBridge/NetShotEvents、カメラモードを含む結合テストも必要。

## 7. 修正順序

1. **P0:** `ApplyState`→`BeginRemoteLife`→`Spawn`の観戦フラグ初期化順序を修正。初回観戦Snapshotの回帰テストを先に追加。
2. **P0:** 新規Shot履歴に対する観戦資格判定を、正当な発射済みShotを維持する形で整備。権威受信、遅延、再送のテストを追加。
3. **P1:** ローカルの観戦要求と遅延Snapshotの優先順位を明示・統合テスト。
4. **P1:** 対象ゼロのフリーカメラからPlayerモードへ誤遷移しないよう判定分離。
5. **P1:** Windows / Linux / macOS / Androidのビルド、CTEST、`-netcombatcheck`、関連するネットワーク統合テストを既存実行方法で検証。SPEC-02の音声調査は再開しない。

## 8. 完了条件

- [ ] 初回観戦Snapshot後、Spawnを通っても`Spectating`が維持される。
- [ ] 観戦中のダメージ・死亡・被弾Claimの拒否が権威側で維持される。
- [ ] 不正な観戦中ShotHistoryでは新規発射できず、観戦開始前の合法ショットは壊れない。
- [ ] ローカル観戦移行中、旧Snapshotで観戦操作や入力履歴が不必要にリセットされない。
- [ ] ターゲット0人時、デモ再生や別のUI経路からもプレイヤーカメラへ誤復帰しない。
- [ ] ターゲット退出・死亡・世代変更・再接続でカメラが有効なビューを選ぶ。
- [ ] 通常対戦で射撃、被弾、リスポーン、Shot再送が回帰しない。
- [ ] ビルドと関連テストを完了済みのCIジョブ結果または再実行したローカルテストで検証できている。
- [ ] **SPEC-02は引き続き保留**。音声コードの変更を混入させない。

## 9. 監査上の断定範囲

本書の「コード経路で確定」とは、条件分岐・代入・呼び出しがGitHub上の対象コミットに存在することを示す。ネットワーク上の実際の発生率、サーバー側でのダメージ確定、エンドユーザーが見る操作不良の再現率は、実機・自動テストの証拠がない限り断定しない。CIは監査時点で進行中であり、成功と記録しない。

**本監査では実装を変更せず、コミット・プッシュ・ワークフロー再実行は行っていない。**
