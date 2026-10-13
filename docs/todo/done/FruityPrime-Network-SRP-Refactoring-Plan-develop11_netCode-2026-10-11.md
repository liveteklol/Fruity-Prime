# Fruity-Prime C++ Network 階層化・SRPリファクタリング実行計画（develop11_netCode / Protocol 30 baseline・Protocol 31 timer extension）

> **作成日:** 2026-10-11（JST）<br>
> **対象リポジトリ:** [`Zection6V/Fruity-Prime`](https://github.com/Zection6V/Fruity-Prime)<br>
> **対象ブランチ:** `develop11_netCode`<br>
> **固定監査SHA:** [`9bd9cb7a3b4661fefa8fb8eddecbe1208fd690ec`](https://github.com/Zection6V/Fruity-Prime/commit/9bd9cb7a3b4661fefa8fb8eddecbe1208fd690ec)<br>
> **対象ルート:** `src/MphRead.Native/Mods/Network/`<br>
> **種別:** 実装計画・移行マニフェスト・実施記録。実装/検証の追記と未完了ゲートは15節に記録し、この計画のDoD全体が完了したことを意味しない。

## 0. 結論・実施原則

このブランチを基準に再策定する。前回のmain（`8a96a9ef` / Protocol 26）向け計画は、新たなPickup Claim、Power-up権限処理、凍結同期、Bot、ConnectionSecretが未反映のため、そのまま適用しない。
目的は**責務の変更理由が明確なモジュール境界**を作ることであり、単にファイル行数を減らすことではない。既存の通信・ゲーム挙動・負荷特性を先に固定し、再配置 → 層間依存整理 → 状態の所有権分離 → 大型クラス分割の順に進める。

**絶対条件**

- C++20。`*.hpp`と`*.cpp`は同一ディレクトリ。`include/`という共有ヘッダ専用階層は新設しない。
- `Formats` / `Entities` をリファクタリング都合で変更しない。必要な接続は `Mods/Network` 内のアダプタを使用。既存の外部呼び出し箇所のinclude更新は許すが、ゲームロジックを迂回して改造しない。
- Protocol 30 / PacketType番号 / フィールド配置 / パケットサイズ / バイト列 / 60Hz時刻意味 / 送受信順序 / Authority判定 / エラー・再送意味を維持。**構造変更だけではプロトコル番号を上げない**。
- `NetSession`などの従来APIは段階的に互換ファサードとし、すべての利用箇所を同時変更しない。
- 見た目上の分割だけで、static配列とコールバックを複数クラスから直接操作する構成にしない。所有状態と更新責務を一対一に近づける。
- ハイフリークエンシー経路での動的確保、hash操作、mutex、仮想呼び出し、std::function生成、Packet再シリアライズを増やさない。最適化前後を計測する。
- テスト合格の実測値はリファクタリング着手時に記録し、推測の件数を成功基準にしない。

## 1. 監査で確認した事実（固定SHA）

- Network直下に**181ファイル（98 stem、83個のcpp/hppペアと15個の単独ファイル）**。現時点で直下に子ディレクトリなし。
- [`NetProtocol.hpp`](https://github.com/Zection6V/Fruity-Prime/blob/9bd9cb7a3b4661fefa8fb8eddecbe1208fd690ec/src/MphRead.Native/Mods/Network/NetProtocol.hpp#L875-L884) の `NetConfig::ProtocolVersion` は **30**。
- 旧mainとの差分はGitHub比較で **9コミット先行、0コミット後退**。同じNetworkのソース群には追加、変更、削除、改名が存在。比較: [`8a96a9ef...develop11_netCode`](https://github.com/Zection6V/Fruity-Prime/compare/8a96a9efe344ec48d49afc104ef1a15d7d6d1878...9bd9cb7a3b4661fefa8fb8eddecbe1208fd690ec)。
- **大型ファイル**（実際の行数）: `NetSession.cpp` 2,150、`NetHitClaims.cpp` 2,115、`DedicatedServer.cpp` 1,811、`NetPlayerBridge.cpp` 1,370、`NetProtocol.hpp` 955、`NetProtocol.cpp` は1,329超。重要なのは行数ではなく役割の混在。
- `NetSession::Handle`にはWelcome/Authority/Intent/SlotIntent/Snapshot/HitClaim/PickupClaim/Verdict/Chat/Map系が同居。`NetSessionLobby.cpp`は既にcpp側を分離済みだが、Sessionのstatic状態に依存。
- `NetHitClaims`が送信・outbox・検証・履歴・権限裁定・ダメージ反映・Verdictを管理。`NetPickupClaims`はPower-upの接触申告と裁定、Claim再送規則をHitと共有する。
- Power-upは [`NetItemState`](https://github.com/Zection6V/Fruity-Prime/blob/9bd9cb7a3b4661fefa8fb8eddecbe1208fd690ec/src/MphRead.Native/Mods/Network/NetItemState.cpp)、`NetPickupClaims`、`NetPowerUps`、`PowerUpLedger`、`NetPowerUpPrediction`、`PredictionBook` の分担。Spawner発生時点の`NoteSpawned`、contact frame起点のgrant、履歴照合、予測settleが相互に関連。
- Protocol 29追加分: `PickupClaim=41` / `PickupVerdict=42`、`PlayerState::PowerUps`。Protocol 30追加分: `PlayerState::FrozenFrames`。当初の`NoxusIceWaveFreezeSync`は後続SRPで共有lifecycle上の`FreezeStatusEffectPolicy`へ整理した。
- `HelloPacket`は38バイトで`ClientId`に加えて`ConnectionSecret`を持ち、再bindを正当なクライアントに限定。
- `ServerBots`はサーバー上位スロットを使い、`DedicatedServer::RelayBots()` がshot historyを含む`SlotIntent`を配信。`NetBotRoles`はAI担当を決定し、`PlayerEntity` spawn前の人格セットアップにも接続。`NetPlayerBridge::StateIntent`は共通の純粋な状態取得。
- CMakeは[`file(GLOB_RECURSE ... CONFIGURE_DEPENDS)`](https://github.com/Zection6V/Fruity-Prime/blob/9bd9cb7a3b4661fefa8fb8eddecbe1208fd690ec/CMakeLists.txt#L51-L54)を使用。移動後の再configureと、列挙・除外ルール、全includeパスの検証が必須。
- `NetHealthSync.hpp`はこのSHAに存在せず、旧`NetHealthSync.cpp`は`NetItemState.cpp`へ改名されている。**古い分類表から復活させない**。`DeathaltWitness.hpp`も現行Networkには存在しない。

## 2. 今回追加された機能に合わせた設計変更

| 対象 | 前回案では不十分だった点 | 今回の分離方針 |
|---|---|---|
| Hit + Pickup Claim | 別々の送信機構だが再送規則共有 | `Gameplay/Claims/Common`に純粋な再送規則のみ。outbox、judge、verdictは各Claimが所有 |
| Server-owned Power-ups | Item出現・権限grant・予測rollbackが別ライフサイクル | `Gameplay/Pickups`内でItems/Authority/Prediction/Rulesの責務を分け、必要になった時点でサブディレクトリも分割。接触時刻を維持 |
| Snapshot連携 | Protocolがゲーム側の型に依存しやすい | `Domain`の純粋な型と`Protocol/Packets`のwire DTOを分離。PlayerEntityをProtocolに入れない |
| Timed status effects | burn/freeze/disruptの残り時間を追加同期するにはProtocol fieldが必要 | `Gameplay/StatusEffects`にtimer captureと共通`Start/Update/End`を置き、権威damage/source・freeze prediction/input stop・disrupt HUDを各policyが保持 |
| Server bots + AI netcheck | ロール・Bot動作・サーバーrelayが交差 | `Bots`に暫定同居、後続で依存を整理しStateIntentを再利用 |
| Connection secret / queue shedding | UDP経路・入力整形・認証の混在リスク | Protocol型、Transportキュー方針、Sessionピア認証を分離 |
| Shell `-connect` | ランチャー経路と通信セッションの強結合 | `Integration/Launch`へ切り出し、Shell経由でjoinする現在の意味を維持 |

## 3. 推奨ディレクトリ階層（責務図）

```text
Mods/Network/
├── Domain/             # プロトコルとゲームで共有する純粋な値型
├── Protocol/           # PacketType, wire DTO, Codec
├── Transport/          # UDP、送受信キュー、障害注入
├── Discovery/          # Master server、server status
├── Session/            # peer、epoch、rejoin、snapshot、client lobby
├── Server/
│   ├── Core/           # DedicatedServer/host lifecycle
│   ├── Lobby/          # lobby rules / commands
│   └── Match/          # rotation / authority simulation
├── Bots/               # NetBotRoles / ServerBots
├── Gameplay/
│   ├── Player/         # state、aim、HUD、lifecycle
│   ├── Replication/    # interpolation / reconciliation
│   ├── Combat/         # hit、damage、prediction、unlagged
│   ├── Weapons/        # shot ledger / phase / muzzle guard history
│   ├── Bombs/          # remote bombs
│   ├── Claims/
│   │   ├── Common/     # 共有の純粋ルールのみ
│   │   ├── Hit/        # hit claim
│   │   └── Pickup/     # pickup claim
│   ├── Pickups/        # item spawn、grants、prediction、rules
│   └── StatusEffects/ # shared timer lifecycle + per-effect policies
├── Replay/             # demo clip / file / playback / recorder
├── Diagnostics/        # logging、timing、probes
├── Integration/
│   ├── Hooks/          # scene/gameplayへの接続
│   └── Launch/         # shell -connect / host session
├── UI/Voting/          # game UIの投票表示・入力
└── Testing/            # harness / combat / maps / lobby / pickups / simulation
```

**注意:** これはPR-01の初期配置（30フォルダ）。ファイル数の少ない領域へ過度にサブフォルダを増やさず、PR-02以降に実際の責務境界が確定した箇所だけ追加分割する。PR-01では循環依存の解消や新クラス導入と同時に実施しない。`Gameplay`等の名称はfeature ownershipを示し、Transport→Gameplayへ逆流するincludeやコールバックを許す意図ではない。

## 4. 依存関係と状態所有の規則

### 4.1 依存の方向

```text
              Domain (leaf)
                ↑
              Protocol (pure packet encoding)
                ↑
              Transport (PacketType参照だけ可)
                ↑
       Session / Server / Discovery (orchestration)
                ↕ explicit narrow port / function context
              Gameplay features
                ↑
           Game integration / UI / launch / testing
```

これは所有境界のイメージ。実装ではコンパイル依存グラフをDAGにし、SessionとGameplayの相互参照を狭いポートへ整理する。上図の↕はヘッダ相互includeを認めるものではない。

- **Protocol**は`Scene`、`PlayerEntity`、`NetSession`、`DedicatedServer`に依存しない。Wire形式の`PlayerState`はwire DTOであり、Entityの所有者ではない。
- **Transport**は`PacketType`程度の情報に限定し、Player、Match、PowerUpの裁定を行わない。Queue sheddingは優先度の規則として分離し、認証とは混在させない。
- **Session**はpeer / epoch / match / connectionのライフサイクルを所有し、Gameplay内部の配列を直接操作しない。リセットイベントまたは厳密な内部コンテキストで通知する。
- **Server**が独立したAuthority割当、Client認証、試合進行、Bot占有スロットを統括。権限判定の実体は必ず1か所に寄せる。
- **Gameplay**のHit、Pickup、Freeze等の裁定・予測は自身の状態を所有し、snapshotのapplyとauthority側grantを混在させない。
- **Testing**には`NetCombatCheck`や`NetCheckClient`等を維持。Production本体へのテスト用friend追加は増やさず、純粋ルールの直接テストを増やす。

### 4.2 各集約の所有する状態（理想）

| Aggregate | 所有するmutable state | 外部へ出すもの |
|---|---|---|
| `SessionContext`（新規） | role、transportハンドル、peer、match/epoch、rejoin、snapshot counters | 現行`NetSession` facadeからの薄いAPI |
| `PeerRegistry`（新規） | slot / generation / endpoint / connection secret / timeout | validated peer lookup、identity update |
| `HitClaimState`（新規） | outgoing / pending / seen / rescued / verdict / shot witness | Submit、Receive、Tick、Reset、stats |
| `PickupClaimState`（新規） | pickup outbox / remembered verdicts / contact frame | Submit、Judge、Settle、Tick |
| `PowerUpAuthority` | grant ledger、expire、sequence | Grant、RanAt、Capture |
| `PowerUpPrediction` | local predicted contacts / rollback book | Predict、Settle、ApplySnapshot |
| `ItemSpawnRegistry` | spawns、spawn count、appearance frame、availability | Register、NoteSpawned、Find、Snapshot |
| `BotRoleRegistry` | local AI / server bot配置 | LevelFor、Holds、SpawnPreparation |

**段階導入:** ここに示す新クラスは提案名であり現在存在することを主張しない。まず所有状況をテストで固定し、必要になったものだけ導入する。

## 5. 実施フェーズ／PR分割計画

| PR | Scope | 実施内容 | Merge gate |
|---|---|---|---|
| PR-00 | 固定SHAとベースライン | 入力一覧181件・互換条件・現行ビルド/CTests/ネット専用checkの実測値・wire goldenを保存。パケットレイアウトとネットフレームの意味、include依存を棚卸し。 | ドキュメントとテスト追加のみ。プロダクションの動作変更なし。 |
| PR-01 | 階層移動だけ | 後掲移動マニフェストでディレクトリ整理、相対includeの再計算、CMakeの再configure、他の使用箇所を全面検索。ネームスペース・クラス名・メンバー・処理順・通信は変更しない。 | 移動前後のビルド、CTest、wire golden、主要起動検証が一致。 |
| PR-02 | Protocol分割 | NetProtocol.hpp/cppの型定義とread/writeをHandshake / Session / Realtime / Combat / Pickup / Directory等へ段階分割。全体用 `NetProtocol.hpp` を互換facadeにする。識別子は一箇所で定義。 | 全PacketType値、Size、Serialize->Deserialize->Serialize、破損payload時の結果一致。 |
| PR-03 | Transport分割 | Recv queue / socket / bounded inbox / drop policy / ping / fault simulationを分割。`InboxShedding`の基準を単独テスト。同期境界は現状どおり。 | loss / rebind / overflow / control packet優先 / thread stopが同一。 |
| PR-04 | Session責務分離 | `NetSession`はfacade。peer registry、connection secret検証、rejoin、match epoch、authority transition、snapshot dispatch、Lobby stateを所有者別に分離。 | タイムアウト→再参加、slot再利用、epoch変更、owner migration、map transition一致。 |
| PR-05 | DedicatedServer分割 | `DedicatedServer`はサーバー寿命の管理。packet dispatch、peers、lobby commands、vote、rotation、world profile、snapshot relay、bot relayを別サービスへ。 | 3人/8slotの実人数判定、試合境界world profile固定、vote/rotation、Bot slot占有一致。 |
| PR-06 | 共通Claim基盤 | `ClaimResend`のような純粋ルールだけ共有し、HitとPickupが各々独立outbox/seen/verdictを所有。バッファ上限/送信時刻/sequenceの意味を固定。 | 再送・期限切れ・重複/順序逆転で異なる結果を生まない。 |
| PR-07 | Hit Claim内部SRP | 命中申告、shot witness、射線とcharge検証、`ClaimDamageBound`、kill cause、damage settle、verdictを責務別に分離。最後にstatic状態をaggregateに収容。 | 既存の `-netcombatcheck` とspawn/death、同フレーム相討ち、Headshot、Death Alt、burn、muzzle guard一致。 |
| PR-08 | Pickup/PowerUp内部SRP | `ItemState`、pickup arbitration、ledger grant、local prediction、settle/rollback、snapshot timerを分離。権限でのgrantとown-clientの予測を混同しない。 | Power-up simulation、先着、respawn、grant once、claim再送、damage倍化の権限検証一致。 |
| PR-09 | Player replication/Status effect | `NetPlayerBridge`のStateIntent/CaptureIntent、ApplyIntent、ApplyState、Aim、Shot confirm、form reconcile、freeze capture/applyを段階切離し。 | Weavel parity、Noxus freeze duration、local vs remote aim、bot shot、60Hz frame同期一致。 |
| PR-10 | Bot / replay / integration境界 | `ServerBots`と`NetBotRoles`の責務を整理。spawn hook依存を狭める。demo、launcher、`-connect`/`-netcheck -ai`の接続を独立性維持で検証。 | 実サーバーAI、観測者へのshot配信、demo再生、ESC pause、CLIモード互換。 |
| PR-11 | staticと依存の最終監査 | 残るpublic static配列、reverse include、unused wrapper、複数reset、診断による循環参照を削減。hot-path allocation/profileと全プラットフォームbuildを比較。 | 既定の全ゲートPASS、定量性能非退行、Protocol 30、raw wire parity維持。 |

### 5.1 分割順の理由

1. Wire仕様とビルド結果を固定せずに「ついでに」APIを変更すると、Protocol 30の回帰が構造変更に紛れる。
2. 行数の多い`NetHitClaims`/`NetSession`は、まずプロトコルとメッセージdispatchの責務を固定してから着手する。
3. `PowerUpLedger`等の既存純粋部品は温存し、SRPが既に成立している箇所まで再設計しない。
4. 途中で開発ブランチに新機能が入ったら、再度HEADを固定し、差分をPRへリベースしてから続行。混在したまま全移動しない。

## 6. Protocol 30の非交渉互換条件

| 守るもの | 具体的な検証 |
|---|---|
| PacketType | `Hello=1`、`HitClaim=30`、`HitVerdict=31`、`PickupClaim=41`、`PickupVerdict=42`等、定義済み識別子の数値をすべて固定。宣言順序をwire識別子と混同しない |
| ConnectionSecret | 128bit secret、`HelloPacket::Size == 38`、client identity・rebind時の照合、旧/短いhello取り扱いを変えない |
| Intent / Shot history | `StateIntent`から`CaptureIntent`の合成順、重複shotの処理、botsの`SlotIntent`とweapon/charge/rayの保持 |
| PlayerState | `PowerUpOffset`、`FrozenOffset`、`Size`、`FrozenFrames`、Weavel flags、snapshot単位のbyte一致 |
| Claim | matchId/epoch/generation/lifeId、ShotKey、claimId、resend、見えない/古いshotの裁定とbound維持 |
| Pickup claim | `SpawnId + SpawnCount`、`ContactFrame`、先着/再出現、local prediction、同一Verdict再送時にgrant once |
| Power-ups | 5bit sequenceと11bit remainingのpack、authority ledgerの開始/終了、double damage / cloak / Death Altのタイマー・因果判定 |
| Freeze | Noxus ice waveの残りframe同期、予測されたfreezeの待ち方、authorityの0への遷移と音・表示タイミング |
| Room lifecycle | 実参加人数でworld profileを決定し、match開始後固定。map変更でitem state/claimsをreset |
| Diagnostic/demo | Log内容の意味、demo record/replay packet互換、ビルド時にテストのコンパイル対象を外さない |

**ProtocolCodec goldenの作り方:** 変更前に各Packetの定数、代表境界値・正常値でのWrite結果のhex/sha256、Read→Writeバイト一致、短い入力/破損bit/最大個数の拒否・失敗挙動をテスト資産として保存する。`PlayerState`と`Intent`は複数slot・世代・life・Shot・PowerUp・FrozenFramesを含む固定入力を必須とする。

**後続のユーザー追加要件:** burn/freeze/disruptの残り時間をすべてwire同期する要求は構造だけの変更ではない。Protocol 30のraw-wire契約は履歴goldenとして保存し、新しいtimer fieldを持つ実装はProtocol 31とする。Protocol 31はfreeze byteのoffsetを維持し、8-player snapshotのpickup余地を残すためburn 9-bit / disrupt 7-bit / freeze 8-bitで各frame数を正確に格納する（現行最大は300/120/150 frames）。

## 7. 責務分離の具体案（メソッド対応）

### 7.1 `NetSession`

| 既存メソッド・状態 | 分離先候補 |
|---|---|
| `StartClient/StartHost/StartServerAuthority/Stop/RebindSocket` | `SessionLifecycle` / `ConnectionManager` |
| `Handle` のswitch、`HandleHello`、`SendHello` | `SessionPacketDispatcher` / `HandshakeHandler` |
| `HandleIntent/AcceptSlotIntent/HandleSlotIntent` | `RemoteIntentChannel` |
| `HandleSnapshot/BroadcastSnapshot` | `SnapshotChannel`（wire処理とゲーム状態取得は分離） |
| `ApplyRoster/FindPeer/ForgetSlot/NextFreeSlot` | `PeerRegistry` / `SlotRegistry` |
| `ApplyMatchState/CurrentMatchId/AuthorityEpoch/MatchesStream` | `MatchIdentity` / `AuthorityController` |
| `PumpLobby/ApplySessionState/SendLobbyCommand` | `LobbyClientSession` |
| `SendChat/SendVote/SendMapPick` | `ChatChannel` / `VoteChannel`（UI表示ロジックはUIに残す） |
| `StartPlayback/InjectPlaybackPacket` | `ReplaySessionAdapter` |

`NetSession`のstatic stateは一回の`Stop/Start`で寿命を通して初期化される。Authority譲渡・再入室・Map変更の各reset順序をテストで固定してからowner移管。

### 7.2 `DedicatedServer`

| 既存機能 | 分離先候補 |
|---|---|
| Run/Stop/Shutdown | `DedicatedServer` facade/runner |
| HandleHello/Identify/Pong/Bye、peer removal、ConnectionSecret | `ServerPeerRegistry` / `ServerHandshake` |
| HandleVote/Tally/ResolveVote/MapPick | `MapVoteService` |
| LobbyCommandsとready/start/owner token | `ServerLobbySession` |
| AdvanceMap/FreezeMapWorldProfile/BuildState | `MatchRotationController` / `WorldProfileSnapshot` |
| StartSimulation/SyncSimulationState/SendSnapshot | `ServerSimulationController` |
| Bots/RelayBots | `ServerBotController` / `BotIntentRelay` |
| HandleHitClaim/HandlePickupClaim/SendVerdicts | `AuthorityClaimRouter`（裁定はGameplay Claimに委譲） |

### 7.3 `NetHitClaims`

| 現在の動作 | 目標責務 |
|---|---|
| Declare/Compose/TickOutbox | `HitClaimOutbox` |
| Receive/Seen/Remember/Park/FlushVerdicts | `HitClaimInbox` / `HitClaimLedger` / `HitVerdictOutbox` |
| ImpactPlausible/ShotPlausible/NamedShotAgrees/RatePlausible | `HitClaimValidator`（pure rulesは依存注入） |
| `ClaimDamageBound`、charge、affinity、double damage履歴 | `ClaimDamagePolicy`（authority inputのみ） |
| ApplyOne/FinishLethal/CauseVouched | `AuthorityHitResolver` |
| `NoteRescued/AlreadyRescued` | `RescuedHitLedger` |
| DescribeAgreement/counters | `HitClaimMetrics` |

`NetHitClaims::VerdictSink`の寿命と重複裁定のexactly-once性を維持し、権限のないクライアントによるダメージ自己申告は信用しない。ダメージ処理と同じ責務にロギングを埋め込まない。

### 7.4 Pickup/PowerUp

```text
Entity spawn → NetItemState::NoteSpawned (authorityでappearance frame記録)
             ↓
Contact(local owner) → NetPowerUpPrediction → NetPickupClaims::Declare
                                       ↓ resend / dedupe
Authority Receive → Spawn/Life/Frame/Reach検証 → Itemを消費 → NetPowerUps::Grant
                                                ↓ PowerUpLedger
                                        PickupVerdict / PlayerState snapshot
Client ApplyVerdicts + snapshot → PredictionBookのSettle / rollback
```

純粋な`PickupReach`、`ItemAuthority`、`PowerUpSync`、`PredictionMatch`はEntityを含まないルールとして維持。authority側の`Grant`経路を`Observe`の順序に依存させず、Spawnerの瞬間記録を維持する。Item healthはPower-upと同じ「spawn authority」の責務でも効果付与は異なるので、pickup命令のルールとtimerロジックを混ぜない。

### 7.5 `NetPlayerBridge` / Freeze / Bot

- `StateIntent`（基礎プレイヤー状態）と`CaptureIntent`（ローカル入力・Shot追加）を別責務とし、ServerBotsの既存StateIntent利用を維持。
- `ApplyState`はSnapshot由来のplayer stateを適用する薄いオーケストレーションにし、Health/Damage、PowerUp、Freeze、Weavel form、Remote aimの順序を変更しない。
- 遠隔発砲と命中確認は`ShotConfirmation`、移動状態は`PlayerReplica`、Aim補正は`AimReplication`を候補に分離。
- Freeze専用の残りtimer/lifecycle判定は共通`TimedEffectSync`へ移し、`FreezeStatusEffectPolicy`は予測acknowledgementとPlayerEntity freeze timer hookへの適用を保持する。入力停止・alt attack終了・氷表示は既存PlayerEntity freeze処理が実行。Burn/Disruptも同じtimer lifecycleを使い、authority-only DOT/sourceとHUD歪みの開始規則はeffect policyに残す。
- `NetBotRoles`と`ServerBots`は一旦`Bots/`にまとめるが、ServerBots→NetPlayerBridge（Gameplay）→NetSessionとの循環は、抽象化の前に呼び出し元を特定して、必要な小さな関数/`Intent`アダプタに置き換える。

## 8. プロジェクト全域の変更監査

- `rg -n "#include [<"](.*Mods/Network|.*NetProtocol|.*NetSession|.*DedicatedServer)" src` で外部参照の移動先を更新する。
- `../../Entities/...`等は移動階層に応じて深さが変わる。必要に応じて`MphRead.Native`の公開include root起点の`"Entities/..."`や`"Mods/Network/..."`へ統一する（既存`target_include_directories`を確認）。
- `file(GLOB_RECURSE ... CONFIGURE_DEPENDS)`が再帰検出しても、CMakeの個別パス固定、プラットフォーム除外regex、CI/テストスクリプト、clang-format、ドキュメント内のパスを一括照合する。
- MSVC・GCC・Clangで、新たにheaderを分けたことによるODR、inline static、constexpr、多重定義、未解決シンボルを確認。
- `Network`の親ディレクトリから内部ファイルへの逆参照、`Renderer`や`Scene`からNetworkへの接続点を記録する。`Formats/Entities`に新規Network依存を逆流させない。
- Pure helperを先に単体テスト対象として切り出し、必要以上の抽象インターフェース、virtual dispatch、DIコンテナは導入しない。

## 9. 回帰テストマトリクス

| Gate | 試験内容 | 失敗条件 |
|---|---|---|
| G0 Build | Win/MSVC、Linux GCC/Clang、macOS、Androidを利用可能なCIでbuild | パス未解決/OS限定のヘッダ依存/リンクerror |
| G1 Pure unit | Packet encode/decode、ClaimResend、ClaimDamageBound、PickupReach、PowerUpSync、PredictionBook、TimedEffectSync、Bot slot rules | goldenとの差異、失敗すべき入力の成功 |
| G2 Network unit | `-netlobbytest`、`-netcombatcheck`、`-powerupsimtest`ほか現行実装で使用されるネットワークcheck | assertionの削除、成功件数の意味の変更、exit非0 |
| G3 Session | UDP join、rebind secret、authority migration、slot generation、timeout/rejoin、multi-round、map vote | 再join先の誤スロット、旧sessionのpacketを許可 |
| G4 Combat | Same-frame kill、headshot、Weavel morph、Shot event out-of-order、charge、Death Alt、burn、Sylux muzzle | 予測とauthorityの相違、phantom shot、二重確定 |
| G5 Pickup | 出現時刻、二重pickup、接触フレーム、Respawn、同じClaim再送、cancel/rollback、health出現 | phantom item、grant二重付与、効果の早期失効 |
| G6 Freeze | Noxusの凍結duration、Clientでの予測、Snapshotの残りframe、解凍 | authorityと各画面で持続時間が違う |
| G7 Bot | `-server -bots N [-botlevel L]`、`-netcheck -ai [LEVEL]`、7 bot、Bot shot event、join/leave | Botがshotを送らない、リアル人間とslot衝突 |
| G8 Bad network | 0ms/150±20ms/250±40ms、loss 0/5%、reorder、duplication、queue saturation | Hang、重要制御packet喪失、unanswered増大 |
| G9 Replay/UX | demo record/playback、`-connect` shell、ESC pause、map vote HUD、spectate | GUI起動経路の退行、録画の互換破壊 |
| G10 Performance | 同等シナリオでframe time p50/p95/p99、alloc/frame、network bytes、packets/frame、heap/locks、CPU使用率 | 未承認の大幅劣化・hot pathで新規alloc |

**測定上の注意:** 現行のCI未実行なのでこの文書は各GateがPASSしたとは主張しない。`-netcombatcheck`等の存在とコマンドの引数は着手時に現行CLIで確認する。Androidで実サーバーを起動できない場合はパケットgolden/compileとPC多クライアント検証を分けて扱う。

## 10. 受入れ基準（Definition of Done）

- [x] 初期マニフェスト181項目の旧パスは全て除去済み。180ファイルはowner別の配置へ移し、旧Noxus freeze rule header 1件は共通`ReconcileTimedEffect`へ統合して削除。freeze syncの2ファイルは`FreezeStatusEffectPolicy`へ改名している。
- [x] `NetProtocol.hpp`は純粋な互換facadeに縮小し、ゲーム実体依存なし。新Codecは全フィールドのbinary parityを満たす。
- [x] `NetSession`と`DedicatedServer`は接続管理facadeに縮小し、Lobby、Map、Bot、Claim、Gameplay実装を持たない。
- [x] `NetHitClaims`/`NetPickupClaims`は各自のinbox/outboxと裁定状態を持ち、共通化するのは意味が同じ再送ルールなどだけ。
- [x] Power-upはAuthority grant / spawn state / prediction book / snapshot reconcileの所有者が明確。
- [x] freeze policy/Botの意味、元の入力・freeze・shot送受信順序、再join防御を保持。Freeze prediction ledgerのslot cleanup/resetをfocused CTestで固定し、動的bot/human slot競合もNetworkLobbyで検証。実serverのAfflict phaseでは再join occupantのfreeze thawとauthority duration差を受入れチェッカーで確認。
- [x] 公開static mutableメンバーの横断直接アクセスを棚卸しし、所有コンテキストの公開メソッドへ移行。
- [x] Protocol 30 raw-wire goldenを保持し、Protocol 31 timer wire goldenも一致。MSVC Release CTest 98/98 PASS（2026-10-13 current build）。
- [ ] CI build回帰なし（remote CI未実行）。
- [x] G10の前後性能比較はユーザー指定により今回の対象外。性能測定は未実施。
- [ ] 1 PR = 1つの責務変更とし、フォルダ移動だけのPRと動作変更PRを混在させない。
- [ ] 実装差分・診断結果・保留したリスクをPRごとに記録し、未測定項目は明確に未確認とする。

## 11. 移動マニフェスト（初期フラット配置181項目）

以下は**Phase PR-01時点での提案移動先**。パケット等の巨大ファイルはこの段階ではリネームしない。PR-02以降で内部クラスやファイルをさらに分割する。後続のStatusEffect統合により、#144のpure ruleは共通`ReconcileTimedEffect`へ統合して削除、#145–146は`FreezeStatusEffectPolicy`へ改名した。

| # | 現在のファイル（すべて`Mods/Network/`直下） | 推奨先（`Network/`相対） |
|---:|---|---|
| 1 | `ClaimDamageBound.hpp` | `Gameplay/Claims/Hit/ClaimDamageBound.hpp` |
| 2 | `ClaimResend.hpp` | `Gameplay/Claims/Common/ClaimResend.hpp` |
| 3 | `ConnectionSecret.hpp` | `Protocol/ConnectionSecret.hpp` |
| 4 | `ContinuousWeaponPhase.cpp` | `Gameplay/Weapons/ContinuousWeaponPhase.cpp` |
| 5 | `ContinuousWeaponPhase.hpp` | `Gameplay/Weapons/ContinuousWeaponPhase.hpp` |
| 6 | `DedicatedServer.cpp` | `Server/Core/DedicatedServer.cpp` |
| 7 | `DedicatedServer.hpp` | `Server/Core/DedicatedServer.hpp` |
| 8 | `DemoClip.cpp` | `Replay/DemoClip.cpp` |
| 9 | `DemoClip.hpp` | `Replay/DemoClip.hpp` |
| 10 | `DemoFile.cpp` | `Replay/DemoFile.cpp` |
| 11 | `DemoFile.hpp` | `Replay/DemoFile.hpp` |
| 12 | `DemoInfo.cpp` | `Replay/DemoInfo.cpp` |
| 13 | `DemoInfo.hpp` | `Replay/DemoInfo.hpp` |
| 14 | `DemoLibrary.cpp` | `Replay/DemoLibrary.cpp` |
| 15 | `DemoLibrary.hpp` | `Replay/DemoLibrary.hpp` |
| 16 | `DemoPlayback.cpp` | `Replay/DemoPlayback.cpp` |
| 17 | `DemoPlayback.hpp` | `Replay/DemoPlayback.hpp` |
| 18 | `DemoRecorder.cpp` | `Replay/DemoRecorder.cpp` |
| 19 | `DemoRecorder.hpp` | `Replay/DemoRecorder.hpp` |
| 20 | `DialancheCombatCheck.cpp` | `Testing/Combat/DialancheCombatCheck.cpp` |
| 21 | `DialancheCombatCheck.hpp` | `Testing/Combat/DialancheCombatCheck.hpp` |
| 22 | `FormReconciliation.cpp` | `Gameplay/Replication/FormReconciliation.cpp` |
| 23 | `FormReconciliation.hpp` | `Gameplay/Replication/FormReconciliation.hpp` |
| 24 | `HealthSimulationTest.cpp` | `Testing/Combat/HealthSimulationTest.cpp` |
| 25 | `HealthSimulationTest.hpp` | `Testing/Combat/HealthSimulationTest.hpp` |
| 26 | `HitLocation.cpp` | `Gameplay/Combat/HitLocation.cpp` |
| 27 | `HitLocation.hpp` | `Gameplay/Combat/HitLocation.hpp` |
| 28 | `HitRig.cpp` | `Testing/Combat/HitRig.cpp` |
| 29 | `HitRig.hpp` | `Testing/Combat/HitRig.hpp` |
| 30 | `HostPool.cpp` | `Server/Core/HostPool.cpp` |
| 31 | `HostPool.hpp` | `Server/Core/HostPool.hpp` |
| 32 | `InboxShedding.hpp` | `Transport/InboxShedding.hpp` |
| 33 | `ItemAuthority.hpp` | `Gameplay/Pickups/ItemAuthority.hpp` |
| 34 | `LobbyCommands.cpp` | `Server/Lobby/LobbyCommands.cpp` |
| 35 | `LobbyRules.cpp` | `Server/Lobby/LobbyRules.cpp` |
| 36 | `LobbyRules.hpp` | `Server/Lobby/LobbyRules.hpp` |
| 37 | `LocalServer.cpp` | `Server/Core/LocalServer.cpp` |
| 38 | `LocalServer.hpp` | `Server/Core/LocalServer.hpp` |
| 39 | `LocalShotLog.cpp` | `Gameplay/Weapons/LocalShotLog.cpp` |
| 40 | `LocalShotLog.hpp` | `Gameplay/Weapons/LocalShotLog.hpp` |
| 41 | `MapAudit.cpp` | `Testing/Maps/MapAudit.cpp` |
| 42 | `MapAudit.hpp` | `Testing/Maps/MapAudit.hpp` |
| 43 | `MapAuditTeams.cpp` | `Testing/Maps/MapAuditTeams.cpp` |
| 44 | `MapRotation.cpp` | `Server/Match/MapRotation.cpp` |
| 45 | `MapRotation.hpp` | `Server/Match/MapRotation.hpp` |
| 46 | `MapVote.cpp` | `UI/Voting/MapVote.cpp` |
| 47 | `MapVote.hpp` | `UI/Voting/MapVote.hpp` |
| 48 | `MatchDefinition.cpp` | `Domain/MatchDefinition.cpp` |
| 49 | `MatchDefinition.hpp` | `Domain/MatchDefinition.hpp` |
| 50 | `MechanicsDump.cpp` | `Diagnostics/MechanicsDump.cpp` |
| 51 | `MechanicsDump.hpp` | `Diagnostics/MechanicsDump.hpp` |
| 52 | `MuzzleObstructionHistory.hpp` | `Gameplay/Weapons/MuzzleObstructionHistory.hpp` |
| 53 | `NetBombs.cpp` | `Gameplay/Bombs/NetBombs.cpp` |
| 54 | `NetBombs.hpp` | `Gameplay/Bombs/NetBombs.hpp` |
| 55 | `NetBotRoles.cpp` | `Bots/NetBotRoles.cpp` |
| 56 | `NetBotRoles.hpp` | `Bots/NetBotRoles.hpp` |
| 57 | `NetCheckClient.cpp` | `Testing/Harness/NetCheckClient.cpp` |
| 58 | `NetCheckClient.hpp` | `Testing/Harness/NetCheckClient.hpp` |
| 59 | `NetCombatCheck.cpp` | `Testing/Combat/NetCombatCheck.cpp` |
| 60 | `NetCombatCheck.hpp` | `Testing/Combat/NetCombatCheck.hpp` |
| 61 | `NetConnectCommand.cpp` | `Integration/Launch/NetConnectCommand.cpp` |
| 62 | `NetConnectCommand.hpp` | `Integration/Launch/NetConnectCommand.hpp` |
| 63 | `NetDamage.cpp` | `Gameplay/Combat/NetDamage.cpp` |
| 64 | `NetDamage.hpp` | `Gameplay/Combat/NetDamage.hpp` |
| 65 | `NetDiagnostics.cpp` | `Diagnostics/NetDiagnostics.cpp` |
| 66 | `NetDiagnostics.hpp` | `Diagnostics/NetDiagnostics.hpp` |
| 67 | `NetFaultQueue.cpp` | `Transport/NetFaultQueue.cpp` |
| 68 | `NetFaultQueue.hpp` | `Transport/NetFaultQueue.hpp` |
| 69 | `NetFeatureCheck.cpp` | `Testing/Harness/NetFeatureCheck.cpp` |
| 70 | `NetFeatureCheck.hpp` | `Testing/Harness/NetFeatureCheck.hpp` |
| 71 | `NetHitClaims.cpp` | `Gameplay/Claims/Hit/NetHitClaims.cpp` |
| 72 | `NetHitClaims.hpp` | `Gameplay/Claims/Hit/NetHitClaims.hpp` |
| 73 | `NetHitPrediction.cpp` | `Gameplay/Combat/NetHitPrediction.cpp` |
| 74 | `NetHitPrediction.hpp` | `Gameplay/Combat/NetHitPrediction.hpp` |
| 75 | `NetHooks.cpp` | `Integration/Hooks/NetHooks.cpp` |
| 76 | `NetHooks.hpp` | `Integration/Hooks/NetHooks.hpp` |
| 77 | `NetHostSession.cpp` | `Integration/Launch/NetHostSession.cpp` |
| 78 | `NetHostSession.hpp` | `Integration/Launch/NetHostSession.hpp` |
| 79 | `NetHudHealth.cpp` | `Gameplay/Player/NetHudHealth.cpp` |
| 80 | `NetHudHealth.hpp` | `Gameplay/Player/NetHudHealth.hpp` |
| 81 | `NetItemState.cpp` | `Gameplay/Pickups/NetItemState.cpp` |
| 82 | `NetItemState.hpp` | `Gameplay/Pickups/NetItemState.hpp` |
| 83 | `NetItemStateTest.cpp` | `Testing/Pickups/NetItemStateTest.cpp` |
| 84 | `NetItemStateTest.hpp` | `Testing/Pickups/NetItemStateTest.hpp` |
| 85 | `NetLag.cpp` | `Transport/NetLag.cpp` |
| 86 | `NetLag.hpp` | `Transport/NetLag.hpp` |
| 87 | `NetLaunch.cpp` | `Integration/Launch/NetLaunch.cpp` |
| 88 | `NetLaunch.hpp` | `Integration/Launch/NetLaunch.hpp` |
| 89 | `NetLifecycleTracker.cpp` | `Session/NetLifecycleTracker.cpp` |
| 90 | `NetLifecycleTracker.hpp` | `Session/NetLifecycleTracker.hpp` |
| 91 | `NetLobbyTest.cpp` | `Testing/Lobby/NetLobbyTest.cpp` |
| 92 | `NetLobbyTest.hpp` | `Testing/Lobby/NetLobbyTest.hpp` |
| 93 | `NetLog.cpp` | `Diagnostics/NetLog.cpp` |
| 94 | `NetLog.hpp` | `Diagnostics/NetLog.hpp` |
| 95 | `NetMaster.cpp` | `Discovery/NetMaster.cpp` |
| 96 | `NetMaster.hpp` | `Discovery/NetMaster.hpp` |
| 97 | `NetMatchEnd.cpp` | `Session/NetMatchEnd.cpp` |
| 98 | `NetMatchEnd.hpp` | `Session/NetMatchEnd.hpp` |
| 99 | `NetMatchSync.cpp` | `Session/NetMatchSync.cpp` |
| 100 | `NetMatchSync.hpp` | `Session/NetMatchSync.hpp` |
| 101 | `NetMatchTimeSync.cpp` | `Session/NetMatchTimeSync.cpp` |
| 102 | `NetMatchTimeSync.hpp` | `Session/NetMatchTimeSync.hpp` |
| 103 | `NetPickupClaims.cpp` | `Gameplay/Claims/Pickup/NetPickupClaims.cpp` |
| 104 | `NetPickupClaims.hpp` | `Gameplay/Claims/Pickup/NetPickupClaims.hpp` |
| 105 | `NetPlayerBridge.cpp` | `Gameplay/Player/NetPlayerBridge.cpp` |
| 106 | `NetPlayerBridge.hpp` | `Gameplay/Player/NetPlayerBridge.hpp` |
| 107 | `NetPlayerLifecycle.cpp` | `Gameplay/Player/NetPlayerLifecycle.cpp` |
| 108 | `NetPlayerLifecycle.hpp` | `Gameplay/Player/NetPlayerLifecycle.hpp` |
| 109 | `NetPlayerSetup.cpp` | `Gameplay/Player/NetPlayerSetup.cpp` |
| 110 | `NetPlayerSetup.hpp` | `Gameplay/Player/NetPlayerSetup.hpp` |
| 111 | `NetPowerUpPrediction.cpp` | `Gameplay/Pickups/NetPowerUpPrediction.cpp` |
| 112 | `NetPowerUpPrediction.hpp` | `Gameplay/Pickups/NetPowerUpPrediction.hpp` |
| 113 | `NetPowerUps.cpp` | `Gameplay/Pickups/NetPowerUps.cpp` |
| 114 | `NetPowerUps.hpp` | `Gameplay/Pickups/NetPowerUps.hpp` |
| 115 | `NetProbe.cpp` | `Diagnostics/NetProbe.cpp` |
| 116 | `NetProbe.hpp` | `Diagnostics/NetProbe.hpp` |
| 117 | `NetProtocol.cpp` | `Protocol/NetProtocol.cpp` |
| 118 | `NetProtocol.hpp` | `Protocol/NetProtocol.hpp` |
| 119 | `NetRoomChange.cpp` | `Session/NetRoomChange.cpp` |
| 120 | `NetRoomChange.hpp` | `Session/NetRoomChange.hpp` |
| 121 | `NetScoreboard.cpp` | `Gameplay/Player/NetScoreboard.cpp` |
| 122 | `NetScoreboard.hpp` | `Gameplay/Player/NetScoreboard.hpp` |
| 123 | `NetSession.cpp` | `Session/NetSession.cpp` |
| 124 | `NetSession.hpp` | `Session/NetSession.hpp` |
| 125 | `NetSessionLobby.cpp` | `Session/NetSessionLobby.cpp` |
| 126 | `NetShotDiagnostics.cpp` | `Diagnostics/NetShotDiagnostics.cpp` |
| 127 | `NetShotDiagnostics.hpp` | `Diagnostics/NetShotDiagnostics.hpp` |
| 128 | `NetShotEvents.cpp` | `Gameplay/Weapons/NetShotEvents.cpp` |
| 129 | `NetShotEvents.hpp` | `Gameplay/Weapons/NetShotEvents.hpp` |
| 130 | `NetSlotManager.cpp` | `Session/NetSlotManager.cpp` |
| 131 | `NetSlotManager.hpp` | `Session/NetSlotManager.hpp` |
| 132 | `NetSmoothing.cpp` | `Gameplay/Replication/NetSmoothing.cpp` |
| 133 | `NetSmoothing.hpp` | `Gameplay/Replication/NetSmoothing.hpp` |
| 134 | `NetStatus.cpp` | `Discovery/NetStatus.cpp` |
| 135 | `NetStatus.hpp` | `Discovery/NetStatus.hpp` |
| 136 | `NetTestScript.cpp` | `Testing/Harness/NetTestScript.cpp` |
| 137 | `NetTestScript.hpp` | `Testing/Harness/NetTestScript.hpp` |
| 138 | `NetTimingDiagnostics.cpp` | `Diagnostics/NetTimingDiagnostics.cpp` |
| 139 | `NetTimingDiagnostics.hpp` | `Diagnostics/NetTimingDiagnostics.hpp` |
| 140 | `NetTransport.cpp` | `Transport/NetTransport.cpp` |
| 141 | `NetTransport.hpp` | `Transport/NetTransport.hpp` |
| 142 | `NetUnlagged.cpp` | `Gameplay/Combat/NetUnlagged.cpp` |
| 143 | `NetUnlagged.hpp` | `Gameplay/Combat/NetUnlagged.hpp` |
| 144 | `NoxusIceWaveFreezeRules.hpp` | 共通`ReconcileTimedEffect`へ統合・削除 |
| 145 | `NoxusIceWaveFreezeSync.cpp` | `Gameplay/StatusEffects/FreezeStatusEffectPolicy.cpp` |
| 146 | `NoxusIceWaveFreezeSync.hpp` | `Gameplay/StatusEffects/FreezeStatusEffectPolicy.hpp` |
| 147 | `PickupReach.hpp` | `Gameplay/Pickups/PickupReach.hpp` |
| 148 | `PlayerColors.cpp` | `Gameplay/Player/PlayerColors.cpp` |
| 149 | `PlayerColors.hpp` | `Gameplay/Player/PlayerColors.hpp` |
| 150 | `PlayerEntityNetAim.cpp` | `Gameplay/Player/PlayerEntityNetAim.cpp` |
| 151 | `PlayerEntityNetAim.hpp` | `Gameplay/Player/PlayerEntityNetAim.hpp` |
| 152 | `PlayerEntityNetHud.cpp` | `Gameplay/Player/PlayerEntityNetHud.cpp` |
| 153 | `PlayerEntityNetHud.hpp` | `Gameplay/Player/PlayerEntityNetHud.hpp` |
| 154 | `PowerUpLedger.cpp` | `Gameplay/Pickups/PowerUpLedger.cpp` |
| 155 | `PowerUpLedger.hpp` | `Gameplay/Pickups/PowerUpLedger.hpp` |
| 156 | `PowerUpSimulationTest.cpp` | `Testing/Pickups/PowerUpSimulationTest.cpp` |
| 157 | `PowerUpSimulationTest.hpp` | `Testing/Pickups/PowerUpSimulationTest.hpp` |
| 158 | `PowerUpSync.hpp` | `Protocol/PowerUpSync.hpp` |
| 159 | `PowerUps.hpp` | `Domain/PowerUps.hpp` |
| 160 | `PredictionBook.cpp` | `Gameplay/Pickups/PredictionBook.cpp` |
| 161 | `PredictionBook.hpp` | `Gameplay/Pickups/PredictionBook.hpp` |
| 162 | `PredictionMatch.hpp` | `Gameplay/Pickups/PredictionMatch.hpp` |
| 163 | `RemoteBombState.cpp` | `Gameplay/Bombs/RemoteBombState.cpp` |
| 164 | `RemoteBombState.hpp` | `Gameplay/Bombs/RemoteBombState.hpp` |
| 165 | `RemoteShotQueue.cpp` | `Gameplay/Weapons/RemoteShotQueue.cpp` |
| 166 | `RemoteShotQueue.hpp` | `Gameplay/Weapons/RemoteShotQueue.hpp` |
| 167 | `ServerBots.cpp` | `Bots/ServerBots.cpp` |
| 168 | `ServerBots.hpp` | `Bots/ServerBots.hpp` |
| 169 | `ServerSim.cpp` | `Server/Match/ServerSim.cpp` |
| 170 | `ServerSim.hpp` | `Server/Match/ServerSim.hpp` |
| 171 | `ServerSimCheck.cpp` | `Testing/Simulation/ServerSimCheck.cpp` |
| 172 | `ServerSimCheck.hpp` | `Testing/Simulation/ServerSimCheck.hpp` |
| 173 | `SessionProtocol.cpp` | `Protocol/SessionProtocol.cpp` |
| 174 | `SessionProtocol.hpp` | `Protocol/SessionProtocol.hpp` |
| 175 | `ShotEventLedger.cpp` | `Gameplay/Weapons/ShotEventLedger.cpp` |
| 176 | `ShotEventLedger.hpp` | `Gameplay/Weapons/ShotEventLedger.hpp` |
| 177 | `SimulationFixture.hpp` | `Testing/Harness/SimulationFixture.hpp` |
| 178 | `SpireAltPoseCheck.cpp` | `Testing/Combat/SpireAltPoseCheck.cpp` |
| 179 | `SpireAltPoseCheck.hpp` | `Testing/Combat/SpireAltPoseCheck.hpp` |
| 180 | `WeaponDps.cpp` | `Testing/Combat/WeaponDps.cpp` |
| 181 | `WeaponDps.hpp` | `Testing/Combat/WeaponDps.hpp` |

**全件照合:** 181件の旧ファイル名は重複なし。stem数 98。単独ヘッダ 12、単独cpp 3。`NetHealthSync`/`DeathaltWitness`を旧ブランチから誤混入しない。

## 12. 推奨着手手順（作業担当者向け）

1. **固定:** 作業ベースが`9bd9cb7a3b4661fefa8fb8eddecbe1208fd690ec`か確認。すでに先に進んでいれば差分を更新してから計画に従う。
2. **Baseline:** `CMake`, compiler別build、CTest、3つのnetwork専用check、`-netcheck -ai`/server bots、Protocol binary goldenを取得。成功したテスト件数をそのまま記録。
3. **PR-01:** 全ファイルの階層移動・include修正のみ実施。namespaceは`MphRead::Mods::Network`のまま。`git diff --find-renames`で予期しないロジック変更がないか確認。
4. **PR-02～05:** Packet/transportとSession/Serverの分割。動作を維持したまま内部委譲先を置換する。
5. **PR-06～09:** Claim/Pickup/Player replicationを順次分離し、純粋rule単体テストを増やす。
6. **PR-10～11:** Bot / replay / launcher境界、static状態、パフォーマンスを最終監査し、受入れ条件をすべて照合。

## 13. 実装時に避けるアンチパターン

- `NetHitClaimsCore`, `NetHitClaimsUtil`, `NetHitClaimsHelper`など、名前だけで責務を規定できない分割。
- `NetSession`の巨大static stateを新しいファイルに`extern`して使い回すだけの分割。
- ClaimとPickupの異なる裁定規則を、複雑な継承階層で無理に共通化すること。
- PacketCodec内部からEntityやSceneを参照すること。
- `std::function`/`shared_ptr`をシミュレーションtickごとに作り直すこと。
- リファクタリングのついでにProtocol 31に上げること。別の仕様変更PRに分離。
- Code moveとゲームplay挙動変更を同時に行い、回帰原因を追跡できなくすること。
- `-netcombatcheck`の期待値が落ちた時、理由を検証せずassertionを削除して通すこと。

## 14. 参考URL

- [固定コミット `9bd9cb7a3b4661fefa8fb8eddecbe1208fd690ec`](https://github.com/Zection6V/Fruity-Prime/commit/9bd9cb7a3b4661fefa8fb8eddecbe1208fd690ec)
- [対象Networkディレクトリ（固定SHA）](https://github.com/Zection6V/Fruity-Prime/tree/9bd9cb7a3b4661fefa8fb8eddecbe1208fd690ec/src/MphRead.Native/Mods/Network)
- [NetProtocol.hpp](https://github.com/Zection6V/Fruity-Prime/blob/9bd9cb7a3b4661fefa8fb8eddecbe1208fd690ec/src/MphRead.Native/Mods/Network/NetProtocol.hpp)
- [NetSession.cpp](https://github.com/Zection6V/Fruity-Prime/blob/9bd9cb7a3b4661fefa8fb8eddecbe1208fd690ec/src/MphRead.Native/Mods/Network/NetSession.cpp)
- [NetHitClaims.cpp](https://github.com/Zection6V/Fruity-Prime/blob/9bd9cb7a3b4661fefa8fb8eddecbe1208fd690ec/src/MphRead.Native/Mods/Network/NetHitClaims.cpp)
- [NetPickupClaims.cpp](https://github.com/Zection6V/Fruity-Prime/blob/9bd9cb7a3b4661fefa8fb8eddecbe1208fd690ec/src/MphRead.Native/Mods/Network/NetPickupClaims.cpp)
- [NetPowerUps.cpp](https://github.com/Zection6V/Fruity-Prime/blob/9bd9cb7a3b4661fefa8fb8eddecbe1208fd690ec/src/MphRead.Native/Mods/Network/NetPowerUps.cpp)
- [NetItemState.cpp](https://github.com/Zection6V/Fruity-Prime/blob/9bd9cb7a3b4661fefa8fb8eddecbe1208fd690ec/src/MphRead.Native/Mods/Network/NetItemState.cpp)
- [NoxusIceWaveFreezeSync.cpp](https://github.com/Zection6V/Fruity-Prime/blob/9bd9cb7a3b4661fefa8fb8eddecbe1208fd690ec/src/MphRead.Native/Mods/Network/NoxusIceWaveFreezeSync.cpp)
- [ServerBots.cpp](https://github.com/Zection6V/Fruity-Prime/blob/9bd9cb7a3b4661fefa8fb8eddecbe1208fd690ec/src/MphRead.Native/Mods/Network/ServerBots.cpp)
- [CMakeLists.txt](https://github.com/Zection6V/Fruity-Prime/blob/9bd9cb7a3b4661fefa8fb8eddecbe1208fd690ec/CMakeLists.txt)

---
文書検証: `181 mapped files / 98 stems / 30 target directories`。変更実施なし。

## 15. 実行記録（2026-10-12）

以下はこの作業ツリーに対する確認結果。Definition of Done全体が完了したという意味ではなく、未実施ゲートは未チェックのまま残す。ビルド・CTest・実サーバーnetcheck・未実施の手動/CI/性能受入れを別々に記録する。

### 実装・静的検証

- 移動マニフェストを現行パスと照合: **181/181配置済み、旧ファイル名の直下パスなし**。Git indexでも **181件すべてR100** として保持。
- `NetProtocol.hpp`はPacket family別DTOの互換include、`NetProtocol.cpp`は互換translation unit。Protocol領域からEntity/Scene/Session/DedicatedServerへのincludeなし。全PacketType・wire golden・round-tripを含む `FruityPrime.NetworkProtocol30Golden` PASS。
- Session packet入口を小さな責務へ分離: handshake/admissionは`SessionAdmissionChannel`、Hit/Pickup claim転送は`SessionClaimChannel`、snapshot applyは`SessionSnapshotReceiver`、client intent送信とclaim順序は`SessionIntentPublisher`、host match-state/roster送信は`SessionHostRosterPublisher`。`PeerRegistry::FindSlotByEndpoint`を使って送信元slotを解決し、`SessionPacketDispatcher`からNetSessionのmutable stateへの直接参照を除去。
- Hit/Pickup claimはそれぞれのinbox/outbox/replay・裁定状態を維持し、共通再送規則は`ClaimResend`に限定。Item spawn、PowerUp authority ledger、local prediction/reconcileも既存ownerへ分離。
- `ServerPacketDispatcher`はprivateな`DedicatedServer::Handle*`入口だけを呼び、serverの`_transport`/`_mapVoting`へ直接アクセスしない。map-vote policyは既存の`ServerMapVoting`が所有。
- server roster構築とrevision連番を`ServerRosterBuilder`へ移し、`DedicatedServer::BuildRoster`をowner呼び出しに縮小。peer/slot/match値は入力として受け取り、生成状態はbuilderが所有する。
- Identify payloadのhunter/color/name検証、ASCII decode、trim、wire長制限を`ServerPeerIdentity`へ移し、peer mutationやlobby通知との境界を分離。`FruityPrime.ServerPeerIdentity` testで短いpayload・不正hunter/color・空名・trim・最大長を固定。
- Lobby packetから`NetSession` match/lobby stateへの投影を`SessionLobbySynchronizer`へ移し、`NetSession::ApplySessionState`はtransition適用に縮小。`FruityPrime.SessionLobbySynchronizer`のfocused testを追加。
- `ClientLobbyState`にphase/revision/match view、owner/edit policy、freeze/load eligibilityのqueryを移し、`NetSession`は呼出しをdelegate。無効local slotを含むpolicyを`FruityPrime.ClientLobbyState`で検証。
- `ServerLobbyState`からmutable reference accessorを廃止し、policy/match snapshot/revision/load-barrierを値読み取りとsemantic state operationで管理。slot maskの範囲確認もownerへ置き、`FruityPrime.ServerLobbyState`でrevision、invalid slot、barrier complete/removal/resetを検証。
- `ServerSim`から`NetSession`のinclude・直接呼び出しを除去。authority起動時のsnapshot callback・roster/session適用・停止を`ServerSimulationSessionPort`へ集約し、NetSession adapterはSession側で実装。DedicatedServerと各simulation診断callerがportを明示して注入し、開始順序・snapshot sink・match-end callbackを保持する。`FruityPrime.NetSessionSimulationAdapter`でserver role、初期session投影、停止後のOffline/resetを検証。
- `NetSession`内部のrole/frame/authority/snapshot handlerへ触れていた3つのdiagnostic friendを外し、保存・復元とfixture操作を`NetSessionFixture`へ集約。診断コードはowner APIまたはfixture経由となり、`NetSession`側に診断クラス別friendを持たない。
- `ServerSim`診断用の5 friendを、存在検査付きの読み取り専用`Scene&`だけを返す`ServerSimFixture`へ集約。`NetHitClaims`の診断用private操作も`NetHitClaimsFixture`へ集約し、本体に診断クラス別friendを残さない。
- `ServerPeer`に混在していたLobbyReady/PostMatchReady/TeamIndexを`ServerLobbyPeerState`へ、command idと再送結果のmap/dequeをboundedな`LobbyCommandHistory`へ分離。peer上の状態はowner API経由になり、team移動時のLobbyReady解除・2種類のready resetの独立性・重複commandの元結果保持・64件FIFO上限を`FruityPrime.ServerLobbyPeerState`で固定。
- `ServerPeer`に直接置かれていたName/Hunter/Colorを`ServerPeerProfile`へ分離。validated identityの適用は`Changed`/`FirstName`/`AppearanceChanged`を返すため、初回join告知とappearance変更時のready解除をconsumer側で繰り返し比較せず、chat/roster/map-voteはprofileの読み取りAPIだけを使う。`FruityPrime.ServerPeerProfile`で同値再送・rename・appearance変更を固定。
- 新規focused unit sourceの`TestSessionLobbySynchronizer.cpp`、`TestServerPeerIdentity.cpp`、`TestNetSessionSimulationAdapter.cpp`をNative product source listから除外し、個別CTest executableだけでコンパイルする。
- MSVC全体ビルドで既存`TestMisc.cpp`の`SIGTRAP`がWindowsで未定義と判明したため、MSVCでは`__debugbreak()`を使うplatform branchを追加。MSVCがMSYS2のGNU `libopenal.a`を選ぶとfocused test linkで`LNK1143`になる問題も確認し、MSVCではvcpkg tripletの`OpenAL32.lib`を優先する。CMakeCacheに旧値が残る環境でも実リンク行がvcpkg import libraryになることを確認。
- Windows/MSVC CTestに選択中Qt kitのbin directoryを`ENVIRONMENT_MODIFICATION`で追加。Qt DLL pathを手動設定しないCTestで`0xc0000135`だったSession testが通常のCTest呼び出しで起動することを確認。
- Session lifecycle、match reset/transition、runtime pump、playback reset、client handshake、slot cleanup、peer maintenanceを専用ownerへ分離。`NetSession::Update/PumpMapTransfer`等はdelegateに縮小し、frame ordering/packet dispatch/keepalive cadenceを`SessionRuntimePump`に保持。
- `NetSession`のIPv4解決、client id/secret生成、拒否状態、match-end packetとroster/match-state ingressを、それぞれ`SessionConnection`、`SessionClientHandshake`、`ClientConnectionHealth`、`SessionMatchEndChannel`、`SessionRosterSynchronizer`へ移管。`SessionPacketDispatcher`はprivate packet action adapterだけを呼び、`SessionAdmissionChannel`に`NetSession`依存を残さない。
- `SessionRuntimePump`から`NetSession.hpp`依存を除去し、SessionRuntimeState、Connection、Authority/Livenessのowner参照とpacket/lobby/peer-maintenance操作portを受け取る形へ変更。map-transferはConnection・packet handler・clockだけの専用`MapTransferPorts`を受け取り、必要以上に広いruntime依存を持たない。playback時刻選択、record-before-dispatch、frame cadence、timeout/reannounce/identify順は維持。
- `SessionLifecycle`は`NetSession` private static fieldへ直接触れず、状態owner参照と送信/resolve/reset callbackを`Ports`で受け取る。`NetSession::LifecyclePorts`が互換facade側で組み立て、StartAuthority/Host/Client/Playback、Rebind、Stopの遷移をLifecycle ownerに委譲。`SessionPeerMaintenance`もroleとbroadcast/stop effectをportで受け、`NetSession` include/friendなしでleave/timeout処理を行う。
- `DedicatedServer::Run`の周期処理とイベント順序を`ServerRuntimePump`へ分離。bot設定適用、packet drain、peer timeout/load barrier、simulation/bot relay、match timeout/rotation、session/roster/vote/authority broadcast、host reap/update shutdown、診断、sleep cadenceを専用pumpが調整し、`DedicatedServer`はsocket・起動/停止・owner wiring・packet adapterを担う。
- `PeerRegistry`のmembershipとPeer state fieldをprivateにし、slot/identity/liveness/latest-intentのread APIと意味単位の更新APIをPeerに集約。registryのlookup/add/remove/slot generation/reset操作をpublic owner APIに揃えてcollaborator friendを廃止。`SessionRoster`もslot値の配列はprivateのまま、semantic mutatorをowner APIとして公開してfriendを廃止。新規`FruityPrime.PeerRegistry` testでregistration、slot、generation、intent frame reset、remove/clearを固定。
- `NetTransport`のpacket countersをprivate mutable stateにし、読み取り専用static accessorだけを公開。DedicatedServerのMatchStatePacket生成を`ServerMatchStateBuilder`へ移し、clock/barrier/match identity/rules/ending flagsを`FruityPrime.ServerMatchStateBuilder`で固定。
- lobby commandのowner/revision/phase認可、match/team policy、ready invalidationを`ServerLobbyCommandProcessor`へ移し、serverが実行するstart/kick効果はactionとしてDedicatedServerへ返す。`ServerLobbyLoadBarrier`がMatchLoaded ACKの受理と開始deadline判定を所有し、`ServerLobbyOwnershipPolicy`がowner token claim・transfer・disconnect successionを所有する。純粋policyのfocused testを追加し、packet I/O・map load・disconnect・revision broadcastはserver adapterに残す。
- SessionState wire snapshotのowner slot、rule flags、load mask、phase別WorldProfile projectionを`ServerSessionStateBuilder`へ分離。slot 0/no-owner sentinel、lobby rules、frozen/continuous profileを専用testで固定。
- burn/disrupt/freezeのsnapshot timer判定とStart/Update/End dispatchを`TimedEffectSync`へ共通化し、発火元/DOT、freeze予測/入力停止、disrupt HUD/audioを各effect policyに保持。freeze policyとEntity非依存の予測slot/frame ledgerを分離し、awaiting window、RTT延長、invalid slot、frame wrap、slot cleanup、session resetを専用testで固定。pending予測がない通常snapshotではNetSession/ping queryへ進まない。
- 既存`netcheck-freeze`報告を受け入れ判定する独立ツール`tools/netcheck/freeze_acceptance.py`を追加。指定observer/slotについてoccupant generation/slot変更、applied/authority双方の完了・remaining=0・open=0、episode accounting、新規適用snapshot時のapplied-vs-authority remaining差（既定4 frame以内）を判定する。episode min/max durationは診断表示のみで比較しない。未観測/継続中はexit 2、timer差・accounting不一致はexit 1。通常の`-netcheck`判定やゲーム処理順序は変更しない。合成報告のPASS/継続中/未観測/timer不一致/rejoin条件/CLI parseをPython testで固定。
- Network配下190 headerのclass member宣言とfriend利用を棚卸し: 検出したmutable static data 462件は全てprivate、public/protected宣言は0件。Lifecycle/runtime pump/peer maintenanceは明示Portsを使用し、`PeerRegistry`と`SessionRoster`はowner API経由。残る`NetSession` friendはprivate packet actionを呼ぶdispatcherとfixtureのみで、両者ともstatic stateを直接読書きしない。`PeerRegistry::Peer`のstate fieldもprivateで、registryだけがconstructする。
- 旧Network直下パス参照監査で見つけた`tools/check-phase11-gl-boundaries.py`の3参照を新しいTesting階層へ更新。同ツール最新実行結果: **1369 C++ files / A=462 / B=0 / C=119 / D=0**（PeerRegistry focused testを含む）。
- focused unit sourceをNative product source listから除外し、それぞれ専用CTest executableとして登録。PeerRegistry test追加後の`git diff --check`とindex側check PASS。staged rename検出は**181 R100**のまま。

### ビルド・自動テスト

- Windows MinGW/GCC: session/lifecycle ownersと`ServerMatchStateBuilder`を含む最新NativeソースでCMakeフルビルドPASS。CTest **76/76 PASS**（`FruityPrime.NetworkLobby`約70秒、Protocol 30 golden、Client/ServerLobbyState/ServerLobbyPeerState、ServerPeerIdentity/Profile、ServerMatchStateBuilder、SessionLobbySynchronizer、NetSessionSimulationAdapterを含む）。
- Windows MSVC: Visual Studio 2022 Developer環境でNativeとfocused test executableをbuild PASS。CTest **6/6 PASS**（ServerPeerProfile、ServerMatchStateBuilder、ServerLobbyState/PeerState、SessionLobbySynchronizer、NetSessionSimulationAdapter）。CTestは`-C Debug`を指定。MSVC link lineはvcpkg `OpenAL32.lib`を選択し、Qt DLL pathはCTest propertyで解決。既存test-objectの重複`main`に対する`LNK4006`警告は残る。
- Android Native `fruity_mphread_native` target: PeerRegistry/SessionRoster owner API変更を含む最新ソースで`arm64-v8a`と`x86_64` build PASS（各ABIのCMake/Ninja終了コード0）。NDK既存`offsetof` warningsとQt `QTP0002` dev warningあり。Android app package/runtime/device起動まではこのtarget結果に含まない。
- 最新`FruityPrime.exe`で`-netcombatcheck "MP3 PROVING GROUND"` **98 assertions PASS / exit 0**、`-powerupsimtest "MP4 HIGHGROUND"` PASS / exit 0。いずれも`Start-Process -Wait`でstdoutと終了コードを回収。
- 今回の最終SRP差分後、MinGW/GCC CTest **81/81 PASS**（`FruityPrime.NetworkLobby` 65.6秒を含む）。MSVC Developer環境で追加focused CTest **5/5 PASS**（Server Lobby command/load barrier/ownership/session snapshot、Noxus freeze prediction ledger）。MSVCのNative static libraryとfocused test executable、およびAndroid arm64-v8a/x86_64 Native targetはbuild PASS。MinGW `fruity_prime` app targetもbuild PASS。
- MSVC `fruity_prime` GUI app targetはC++ compile後、generated `.rc` ruleで`RC1107: invalid usage`となりlink前に停止。これはNative library/focused-test buildの成功とは別の未解決build gateとして記録する。MSVC test targetに残る既存`LNK4006 main`重複定義warningも継続。
- 今回のSession RuntimePump/Lifecycle/PeerMaintenance port差分後にMinGW `fruity_prime` target build PASS。全CTest **81/81 PASS**を確認し、さらに関連実行ファイルを更新linkした`NetworkLobby`と`NetSessionSimulationAdapter`も**2/2 PASS**。MSVCでは`fruity_mphread_native`と`fruity_net_session_simulation_adapter_tests` build PASS、adapter CTest **1/1 PASS**。MSVC test linkに既存`LNK4006 main`重複定義warningあり。
- PeerRegistry/SessionRoster owner API化の後にMinGW全target build PASS、全CTest **82/82 PASS**（NetworkLobby 65.7秒、PeerRegistry focused testを含む）。MSVCではNetwork/Session 41翻訳単位を再compileしてNative library、NetworkLobby、SessionLobbySynchronizer、NetSessionSimulationAdapterを更新linkし、関連CTest **3/3 PASS**。追加のMSVC PeerRegistry target buildとCTestも**1/1 PASS**。Android Native targetは同じ最新ソースでarm64-v8a/x86_64 build PASS。既存aggregate test executableの`LNK4006 main`重複定義warningは残る。
- 2026-10-12最終MinGW run: HitRig探索変更を戻した後の全target CMake build PASS、CTest **82/82 PASS**（`FruityPrime.NetworkLobby` 65.77秒）。`git diff --check`とcached diff check PASS。indexは**181 R100 / 他0**を維持。
- 2026-10-12 snapshot ingress責務分離後: `ServerSnapshotValidator`がmatch/authority epoch、payload境界、match-time/item tail、重複slot、slot generationを副作用なしで検証し、適用可能なslot/life projectionを返す。`DedicatedServer`はauthority gate・stream acceptance・life適用・peer relayを調整し、streamが受理した後にだけlifeを更新する。新規validator test sourceはNative product source listから除外し、独立CTest targetに登録。MinGW `FruityPrime.exe`を含む全target build PASS、全CTest **83/83 PASS**。同exeで`-netcombatcheck "MP3 PROVING GROUND"` **98 assertions / exit 0**、`-powerupsimtest "MP4 HIGHGROUND"` **PASS / exit 0**。MSVC Native libraryとvalidator target build PASS、snapshot stream/validator/slot identity CTest **3/3 PASS**。Android Native `fruity_mphread_native` arm64-v8aとx86_64 (`build/native-android-x86_64-srp`) build PASS。古い`build/native-android-x86_64`はQt6 SDK path未設定でCMake configureが止まったため採用していない。既存のNDK `offsetof`、Qt policy、MSVCの重複test-mainなどのwarningは残る。
- 2026-10-12 MSVC GUI再検証: 直近の`fruity_prime` targetではRC1107は再現せず、resource compile後のlinkで`/WHOLEARCHIVE`によりNative static archive内の複数`Testing/Test*.cpp`の`main()`が強制pullされ、`LNK2005`/`LNK1169`で停止することを確認。CMake source ownershipを整理し、test entrypoint群をNative product sourcesからパターン除外、歴史的なdiagnostic/support translation unitsのみ明示的に保持。結果としてMSVC `fruity_prime` Debug target build PASS、focused snapshot/slot CTest **3/3 PASS**。この修正後、MinGW全target build PASS、全CTest **83/83 PASS**（NetworkLobby 65.81秒）。
- 同じ時点のPhase 11静的境界監査: **1372 C++ files / A=462 / B=0 / C=119 / D=0**。新規ownerとtest追加後もcross-field公開アクセスのB判定は0件。
- 2026-10-12 MSVC server SRP追補: チャットの入力検証、rate-limit判断、team-only recipient選択、packet relay、system message serializationを`ServerChatChannel`へ分離し、`DedicatedServer`は接続中peer/transport/scratchとteam mode、loggerを渡すadapterに縮小。既存の`NetworkLobby`統合試験が同じserver chat経路を通り、MSVC CTest **1/1 PASS**（65.87秒）。MSVC `fruity_prime`とNetworkLobby targetも再build PASS。次のMSVC全CTest結果は別途記録する。
- 2026-10-12 CMake product/test ownership追補: `Testing/Test*.cpp`をNative product libraryのsource globから一括除外し、runtime診断で製品にも必要な既存support translation unitだけをallow-listで戻す。これによりGUIの`/WHOLEARCHIVE` linkでstandalone test `main()`が混入する`LNK2005`/`LNK1169`を解消し、増殖する個別除外一覧をなくした。MSVC GUI target build PASS。
- 2026-10-12 session境界追補（この追補時点）: `DedicatedServer`、`LobbyCommands`、`ServerClaimChannel`、server bot-intent pathから`NetSession` static API参照を取り除き、Session adapterが作る`DedicatedServerSessionPort`へ集約。simulation lifecycle、frame/clock、match/authority identity、session projection、roster、slot-intentをserverへportとして渡し、claim channelには読み取り専用identity portだけを渡す。`ServerBots::IntentFor`はmatch/epoch/frameを引数で受け取り、server stateのsingleton参照を除去。`TestNetSessionSimulationAdapter`にportのcomplete wiring/read/applyを追加。この時点では`DedicatedServer`のpacket/lobby/match event coordinationを分離中で、DoD 3は未完了。後続の`ServerRuntimePump`追補後のSRP audit結果は下の「残る受入れゲート」に記録。
- 2026-10-12 最終Windows validation: `tools\build\build-cpp.bat msvc Release`成功、MSVC/Release/Ninjaで`tools\build\out\msvc-Release`を使用。CTest **90/90 PASS**（`FruityPrime.NetworkLobby` 65.57秒、`FreezeTimeline`、`SessionSimulationCallbacks`、`NetSessionSimulationAdapter`を含む）。Phase 11 boundary audit **1397 C++ files / A=462 / B=0 / C=119 / D=0**。MinGWは追加検証していない。
- 2026-10-12 `ServerRuntimePump`/session-owner最終差分後のMSVC validation: `tools\build\build-cpp.bat msvc Release`成功、出力は`tools\build\out\msvc-Release\FruityPrime.exe`。CTest **90/90 PASS**（`FruityPrime.NetworkLobby` 66.37秒、`FreezeTimeline`、`NoxusFreezePredictionLedger`、`ClientConnectionHealth`を含む）。NetworkLobby内の動的bot数変更・human slot競合/解放後再割当シナリオもPASS。Phase 11 boundary audit **1421 C++ files / A=462 / B=0 / C=119 / D=0**。MinGWは実行していない。
- 2026-10-12 form replication integration後のMSVC再検証: 指定された`tools\build\build-cpp.bat msvc Release`成功。`tools\build\out\msvc-Release`を使用し、CTest **90/90 PASS**（`FruityPrime.NetworkLobby` 66.24秒、`FormReconciliation`、`WeavelAltFormParity`、`FreezeTimeline`、`NoxusFreezePredictionLedger`を含む）。
- 2026-10-12 MSVC Release CLI受入れ追試: `-netcombatcheck "TEST ARENA"` **98 assertions PASS / exit 0**、`-powerupsimtest "MP4 HIGHGROUND"` **PASS / exit 0**。`TEST ARENA`はpower-up spawnを持たないため同checkでは想定どおりSKIP / exit 2となり、pickup確認の根拠には使わない。`-netlobbytest`単独起動は約66秒後に終了したがGUI exeの直接起動で終了コードを回収できなかったため、PASS根拠は既に記録したMSVC CTest **90/90**とする。チェック用ゲームプロセスは終了済み。

- freeze診断フィールド（authority-frame射影とraw local duration）追加後のMSVC Release build: `tools\\build\\build-cpp.bat msvc Release` PASS、CTest **90/90 PASS**（`FruityPrime.FreezeTimeline`を含む）。`py -3 -m unittest -v test_freeze_acceptance.py` **8/8 PASS**。MSVC compile warningとして`NetFeatureCheck.cpp`の既存`NOMINMAX`重複定義が1件。

### 実サーバーruntime試験

- G7: 固定rotation `MP3 PROVING GROUND`、7 server bots、AI clientの60秒local dedicated-server netcheckが**exit 0 / RESULT: PASS**し、8 slot全てのspawn、全7 botのshot events、damage/death/scoreboardを確認。なお旧記録の`level 4`は要求値で、`ServerBots::MaxLevel == 3`により実効levelは3。2026-10-12のjoin/leave行列では7 botsをslots 1–7に固定し、人間Aがslot 0へjoin、2人目Bを`session full`で拒否、Aのleave後に人間Cがslot 0へ再joinするのをserver logで確認。botsは人間の入退室中もslots 1–7に残り、同じslotへ重複割当されなかった。A/CのnetcheckはBot Traceが無被弾の受入れ判定で各1 check FAILだが、接続・拒否・slot reuseは成立。今回のMSVC `NetworkLobby` CTestでは、humanがbot予約外slotを占有する間のbot枠拡張拒否、slot維持、human leave後の新generationでのbot再割当を確認した。
- G8 150±20ms: 7 bot、60秒、`-netlag 150:20`でPASS。hit claim 26件、unanswered 0、pickup unanswered 0。
- G8 250±40ms + loss/reorder/duplicate: 2026-10-12に45秒試行をfault seed 1801/1802/1803で3回実施。2回は`RESULT: PASS`、seed 1801はBot Traceが一度も被弾しないcombat-coverage判定だけで`RESULT: FAIL`。3走行ともstates applied=5400/drop=0、shot eventの未着・未解決0、snapshot late=371/403/384、claim unanswered=4/2/0で、通信処理のhangや制御state dropは観測しなかった。seed 1802の最初の起動は直前clientのtimeout cleanup中にsession-full拒否となったため集計から除外し、server logでslot解放を確認した後、同じseedで完走したrun2bを採用。queue saturationは`FruityPrime.NetFaultQueue` unit test PASS。
- 2026-10-12 G8 repeatability follow-up: MSVC Release local server on `TEST ARENA`, 6 level-3 bots, two `-netcheck -ai 3` clients, 45 seconds, each client configured for RTT 250±40ms, 5% loss, 10% reorder and 5% duplication with independent seeds (1804/2804, 1805/2805, 1806/2806). All six clients exited 0 with `RESULT: PASS`; each applied 5400 states and dropped 0. Across the three fresh server runs the shot-event streams reported received=fired with 0 never-arrived, 0 waiting/overdue and 0 abandoned events. Client-reported unanswered hit claims ranged 0–5 per client and remained within each netcheck's passing threshold. This removes the prior Bot Trace coverage miss for this configured repeated scenario; it does not cover the rest of the G8 latency/loss matrix or live queue saturation.
- G9 demo: 実netcheckでProtocol 30 demoを45.2秒記録。netcheck自体はBot Noxusが一度も被弾しない受入れ条件でFAILしたが、生成demoは`-demoinfo -replay`で終了コード0。23,035 records、frame 1–2710、snapshot active frame 2,689/2,689を再生した。
- G9 spectate/rejoin: `-spectate 10 -rejoin 20 -seconds 60`、7 bot実サーバーで終了コード0 / `RESULT: PASS`。spectatingは600 frames記録され、後続の通常プレイも進行。
- G6 Noxus: 先行の7 server bots・通常tour試験は`RESULT: PASS`でも全slotのfrozen frameが0だったため、凍結受入れには数えない。従来の専用server・2 client phase試験も発生は確認したがdurationをassertしなかった。2026-10-12のMSVC Release実server行列では、Noxus monitorを115秒、旧occupantを38秒、新occupantを65秒接続し、旧clientが離れた直後に同一slot 1へ新clientをjoin。monitorのslot 0はapplied 11 episodes/11 complete、duration 124–151 frames、authority 11 thaws/125–150 frames。slot 1では旧occupant interruption 1回、occupant changes 4回、新client generation 2を確認したが、その時点の新clientはslot 1でapplied/authority remaining=24のactive freezeを持っており、受入れは未完了だった。
- 同日16:51のtargeted runtimeでは、MSVC Release serverを`TEST ARENA | Battle | 7 | 99`で起動し、通常の2 headless `-netcheck` clientを使用。`MPHREAD_PHASE=Afflict`で固定ツアーのaffliction phaseを固定し、Noxus monitorを125秒、旧occupantを30秒、新occupantを120秒動かした。全clientがslot 0/1へ参加しexit 0、monitorはslot 1 generation 2・occupant changes 3を記録。再join clientのslot 1 generation 2ではapplied **9/9 complete, 29–151 frames**、authority **9/9 thawed, 30–150 frames**、remaining=0/open=0。monitorのslot 1もapplied **12/12**, 31–153 frames、authority **12/12**, 30–150 frames、remaining=0/open=0。`freeze_acceptance.py`を両reportに適用し、duration tolerance 4 framesで **2/2 PASS**。先行の`-ai`付き150秒試行はNetTestScriptのAfflict phaseを無効にしてfreezeが0件だったため`INCOMPLETE`として除外。
- 2026-10-12 freeze/rejoin under compound fault: `MPHREAD_PHASE=Afflict`を固定し、slot 1の旧clientを30秒でleaveさせて同じslotへgeneration 2の新clientをjoin。新clientだけをRTT 250±40ms、5% loss、10% reorder、5% duplicate（seed 4771）で120秒実行。rejoin本人のreportは`RESULT: PASS`、applied **9/9 complete, 31–152 frames**、authority **9/9 thawed, 31–151 frames**、remaining/open=0で、`freeze_acceptance.py --require FreezeFaultNew,1,2,0`は**1/1 PASS / exit 0**。monitorもgeneration 2・occupant changes 3を観測したが、monitorを125秒で終了したためslot 1にapplied open=83 / authority remaining=70のepisodeが残り、monitorを含むcheckerはINCOMPLETE（exit 2）。server shot-eventは155 fired、never-arrived=0、waiting/overdue=0、abandoned=0。従って新generation clientのloss/reorder下でのfreeze/thaw durationは確認済みだが、monitor側のepisode完了まではこのrunで主張しない。
- 2026-10-12 freeze/rejoin extended-monitor follow-up: 同じAfflict/rejoin/fault構成でmonitorを180秒に延長し、新clientの終了後までslot 1を観測。monitor/new clientともexit 0、generation 2、applied/authority episode数は各25/25・18/18で、open/remaining=0、shot-event 151/151・never-arrived=0。freeze episodeの完了自体は両reportで確認したが、strict `freeze_acceptance.py` ±4-frame comparisonは**2/2 FAIL**: monitor applied 28–144 vs authority 29–151、new client applied 27–144 vs authority 28–151（最大差7 frames）。従って複合faultでのrejoin・最終解凍は再現した一方、継続時間の厳密な受入れは反復安定していない。checker閾値は変更せず、この結果を未PASSとして扱う。診断上、適用側はlocal simulation samples、authority側はsnapshot server-frame差を数えており、遅延/揺らぎ時の観測timebase差を含む。旧実装のfreeze単独apply経路は廃止し、現在は`StatusEffectSynchronization`が共通timer lifecycleを通してfreeze policyへ適用するが、この試験だけでは時間差が許容可能な表示遅延か挙動差か確定できない。記録は`tools/build/out/msvc-Release/srp-g6-monitor-extended-20261012/`。
- freeze authority-clock診断 follow-up（2026-10-12）: 上記のprojection/raw-local計測を含むRelease成果物で、旧occupant30秒、rejoin occupant120秒（seed 4771、RTT 250±40ms、5% loss、10% reorder、5% duplicate）、monitor180秒を実行。serverと全clientはexit 0、通常netcheckは`RESULT: PASS`。しかしcheckerはFAIL: monitorのslot 1はapplied28/28に対しauthority27/27、authority-frame射影duration 23–152対30–151（±4超過、raw local 22–144）。monitor終了時のgeneration値は0でoccupant changes 3となり、slot退室後に最終値へ戻る報告ではgeneration 2を保持できない。rejoin本人はgeneration 2だがapplied17開始/16完了・open126/remaining21、authority16開始/15 thaw/open1/remaining21で、終了時の未解凍によりchecker不合格。従って時計を射影しただけではduration差は解決せず、G6は未PASS。証拠は`tools/build/out/msvc-Release/srp-g6-clock-followup-20261012/`。
- snapshot ingress分離後のserver integration smoke（2026-10-12）: local dedicated server + 2 headless Noxus clients、45秒。両clientともserver mapへjoinし2700 frame前後を実行、server snapshotを2690/2689回受信し、remote movementとshot event relayも観測。netcheckは双方が被弾しなかった1条件でFAILしたためPASS扱いにはしない。DedicatedServerはserver simulation中であり、この実行はserver→client snapshot送信の回帰 smokeで、追加したserver authority snapshot validatorの呼出しを証明するものではない。server/client processは終了確認済み。
- form transition同期（2026-10-12）: Protocol 30の既存PlayerState 138-byteレイアウトを維持し、既存`WeavelFlags` byteのWeavel低位flagをそのままに、Weavel以外の全hunter（Samus/Syluxを含む）でmorph/unmorph方向と0–15段階のanimation phaseを送る。snapshot capture、wire validation、remote transition start/correction、進行度適用は各ownerに分離。最新snapshotの位相へanimationを巻き戻さず、authority完了報告まで終端frameに留める。スムージングのplayout clockやキャラクター表示遅延は変更しない。`FruityPrime.WeavelAltFormParity`はphase 0/7/15のwire round-tripとsize 138を確認済み。
- form sync runtime: 複合fault（RTT 250±40ms、各方向loss 5%、reorder 10%、duplicate 5%）+ 6 server bots + 2 human clients。Noxus MorphA seed 2101/2102は両client `RESULT: PASS`、seed 2103は双方`Bot Weavel`が被弾しなかったcombat-coverage条件でのみFAIL。3 runとも各clientでstates applied=5400 / dropped=0。Noxus全phase tour seed 2104（60秒、3秒/phase）とSamus tour seed 2106（同条件）は両client個別netcheck PASS、各7200 states / dropped=0。両tourでMorphA/MorphB/Unmorphが実行され、alt-form-total / unmorph値はほぼ一致。Samusの汎用compareはalt-form-total 18 frame差（許容15）、zoomとdamage-takenも差異を報告したため、全体parity PASSとは数えない。
- Syluxのalt-only bomb検証: AltAttackA seed 2105の45秒fault runは両client個別netcheck PASS、authority playerが54 bombを設置しremote observerは53を観測。observerのslot 0はauthority alt=2466 framesに対しalt=2474 frames、form disagreement 14 frames / 最長連続11。`compare-reports.py`はowner local predictionとremote観測のalt-form-totalに84 frame差を検出し、parity判定FAIL。bombの大部分が届いたことと各clientの終了成功は確認したが、複合fault時のaggregate form-duration一致、および目視上の爆弾出現時にMorph animationが見えることの手動確認は未完了。証拠reportは`tools/build/out/msvc-Release/srp-form-sync-20261012/`。
- 現行フォーム適用経路のMSVC Release再検証（seed 2110–2112）: Samus 60秒Tourは2 clientとも`RESULT: PASS`、各7200 states applied / dropped=0。比較したalt-form-total差は13/3 frames（許容15）、unmorph差は1 frameずつ。比較全体はmovementとdamage-takenの別条件でFAIL。
- Sylux AltAttackA 45秒は、seed 2111で両clientの`RESULT: FAIL`理由がBot Weavel未被弾のみ、seed 2112は両client`RESULT: PASS`。両seedで各client 5400 states applied / dropped=0。seed 2111はowner/observerのalt-form-total 2523/2490 frames、bombs-laid 52/52、seed 2112は2610/2592 frames、59/58個。対象form/bomb metricsは`compare-reports.py`の許容内。seed 2111の比較はbot側report不在でINCOMPLETE、seed 2112の全体比較はmovement条件のためFAIL。従って対象hunterのフォーム/ボムの複合fault parityは改善を確認したが、全指標合格やゲーム画面でのanimation・bomb位置の目視確認を意味しない。
- 2026-10-12 bomb owner/observer follow-up: `tools/check-bomb-network.ps1 -Seconds 60`をMSVC Release成果物で実行し、Samus/Kanden/Sylux/Samus(lossy)の4 clientがすべてexit 0。各ownerと残る3 observerを同一ownerで照合した12/12 pairがPASS。各ownerの観測寿命は174/178/306/339 frames、remoteは各ownerの0.6–1.5倍内（実測177–340）。BombLossだけにRTT 100±20 msと5% lossを適用。これはowner別のbomb存在・寿命のruntime確認であり、XYZ誤差の定量閾値や画面上のform animationとの同時目視確認ではない。実行後にclient/server processの終了を確認。2026-10-12の追加XYZ runtime計測はMSVC Releaseを用いた`tools/check-bomb-network.ps1 -Seconds 60`で4 client全てexit 0、owner/observer lifetime 12/12 PASS。XYZ telemetryは各pair samples 173–1083、mean/maxは`0.###`表示で全て0。checkerは全pairに有限値の測定行と正のsample数を要求するが、絶対error thresholdは配置bomb runだけでは決めず、移動中/複合fault条件を追加する。artifactは`tools/build/out/msvc-Release/bomb-network-xyz-20261012/`。
- 座標同期の経路監査: `NetShotEvents::Fired`は実弾生成時の`FiredShot::Origin`/`Direction`とlaunch frameをevent化し、`IntentCodec`がrayをwire化する。Bombは`NetBombs::Attach`がownerのworld positionをIntentへ載せ、受信側はその位置で生成し、移動bombはowner報告位置へ追従する。`TestIntentTouchPayload`はshot/bomb wire fieldとowner位置更新を確認する。従ってshot/bomb座標はsmoothed puppet位置から再計算しない設計だが、上記runtime scriptはbomb XYZをowner/observer間で比較しない。継続するactor display/playout delayは今回の要求範囲外として変更していない。

### 残る受入れゲート

- G0: WindowsではMSVC Native app/CTestおよびAndroid arm64-v8a/x86_64 Native targetを確認済み。以降のWindows build/test確認はユーザー指定によりMSVCに限定する。Linux GCC/Clang、macOS、CIは未実施。このWindows環境にWSL distributionはなく、macOS実機/remote CI runもこの未push作業ツリーからは確認できない。
- 責務面: `ServerSim`から`NetSession`の直接依存はSession portへ置き換え、map voting、peer identity parsing/profile/roster、packet dispatch、session/lobby状態、peer単位のlobby値/history、lobby command policy/load barrier/ownership policy/session packet projection、authority snapshot validation、server chat validation/rate limit/relayなどのownerを抽出済み。Session lifecycle/runtime pump/peer maintenanceもstate owner参照とeffect callbackを使うPortsへ移し、PeerRegistry/SessionRosterのdataもowner APIに限定した。mutable static fieldは全てprivateでpublic/protectedは0件、friend collaboratorからの横断field accessなし。`NetSession`は互換APIのfacade、`DedicatedServer`はserver lifecycle/composition rootで、周期処理順序は`ServerRuntimePump`が所有するためDoD 3を完了とした。残るpacket/lobby/claim入口は専用ownerを呼ぶadapter。`SessionPacketDispatcher`のfriendはprivate packet actionだけを呼び、fixture friendはtest専用。`ServerSim`、`NetHitClaims`のdiagnostic friendは専用fixtureへ集約済み。
- G3 Session: ローカルMSVC受入れは統合済み。CTest 91/91中の`FruityPrime.NetworkLobby`は実UDP rigで二試合、owner disconnect後のauthority移行、socket rebind、loss中のlobby command再送、continuous rotation、map voteによる別map遷移を実行。`SessionMatchIdentity`/`ClientConnectionHealth`/`SessionConnection`/`ServerIntentValidator`各testがstale epoch、timeout境界、socket再bind、epoch/slot世代拒否を補完する。remote CIは別ゲート。
- G4 Combat: `-netcombatcheck "TEST ARENA"`は98 assertions PASS（mutual kill ordering、damage bound、projectile lifecycle/respawn、charged projectileなど）。CTest 91/91にはheadshot/impact、death state、out-of-order intent、Sylux muzzle identity等のfocused testsも含む。各責務のunit/authority checkは通過したが、計画に列挙した全武器・フォーム・状態をclient/authority間で同時に照合するruntime matrixは未完了。
- G5 Pickup: `-powerupsimtest "MP4 HIGHGROUND"` PASS（double damage claim、contact-time timer、再送grant once、respawn後の再出現、death終了、snapshot publication）と`-healthsimtest "MP4 HIGHGROUND"` PASS（7 spawner、authority health pickup/respawn、replica convergence）。`TestPredictionBook`にprior timer rollback、同種連続予測のclaim応答順前後、権威sequence先行、死亡時の取消し抑止を追加し、`TestPickupPredictionFaults`では実outbox/replay ledger/prediction bookを組み合わせ、claim datagram drop、verdict drop、同一claim再送、重複verdict、B→A逆順refusalを通す。2026-10-12 MSVC Release全CTest **93/93 PASS**で両testも実行済み。real-client harness `run-pickup-rollback.ps1` は2人目のhumanがjoinするまで両clientのpickup steeringを待ち、同じspawnへ向かうよう調整したが、120秒・RTT 250±40ms/loss 5%/reorder 10%/duplicate 5%のseed 4803/4804はともに未PASS。両runでcontrolは1 pickup予測/権威grant、faulted clientは予測0・claim0で、server側のpickup grantは1件のみ。この2 runでは実clientの権威拒否とprediction rollbackは発生せず未完了だった。2026-10-12 seed 4807は再生成したTEST ARENAで実施し、controlはpredicted/granted/refused/rollback=9/9/0/0、faultedは4/2/2/2、claim未回答0、authority refusal reasonあり。専用acceptance checkerは6/6 PASS / exit 0でG5 runtime acceptanceを完了。fixtureの`maps/arena/arena.json`にDoubleDamageを追加し、MSVC出力の`maps/arena/arena.json`へ同期してから`FruityPrime -mapgen "TEST ARENA"`を実行。受入れコマンドは`tools/netcheck/run-pickup-rollback.ps1 -Seconds 60 -Seed 4807 -Room "TEST ARENA"`。汎用netcheckのcontrol/faultedは被弾不足のcombat-coverage assertionで各exit 1だが、pickup gateとは独立。artifacts: `tools/build/out/msvc-Release/srp-g5-pickup-fault-seed-4803-20261012-215749/`, `tools/build/out/msvc-Release/srp-g5-pickup-fault-seed-4804-20261012-220243/`、`tools/build/out/msvc-Release/srp-g5-pickup-fault-seed-4807-20261012-225729/`。
- G6: freeze rule/prediction ledger unit testとfaultなしAfflict scenarioのduration/thaw/slot rejoin（2/2）は受入れ済み。Protocol 30時点のcompound-fault repeat / authority-frame projection follow-upにはstrict gate失敗があり、その履歴は残す。Protocol 31 controlled repeat（250±40ms RTT、5% loss、10% reorder、5% duplicate）では、seed 4772のrejoin fault runがstrict checker monitor/rejoined 2/2 PASS（applied/authority thaw 30/30・24/24、remaining max delta 0 over 2176・1212 authority-frozen samples）。別packet placementのseed 4774はmonitor側へfaultを設定し、monitorはslot 1 generation-max=2 / occupant-changes=3 / thaw 21/21、再join `FreezeFaultNew`はgeneration-max=2 / thaw 17/17。両方remaining max delta 0（1548・1681 samples）でstrict checker 2/2 PASS。duration diagnosticはmonitor 9..151 / 9..151、rejoin 10..150 / 10..150 framesで、短いepisodeもauthority値と一致した。`TestFreezePredictionLedger`はarrival slack/RTT、invalid slot、frame wrap、ForgetSlot、Resetを、`TestFreezeTimeline`はepisode完了と再joinを検証する。seed 4775のrejoin fault runはmax delta 28でstrict gateが落ちたが、計測実装が新snapshot後も毎local frameに古いauthority timerを比較していたため、snapshot到着間隔をtimer driftとして数える計測不備を特定した。新snapshot適用時だけ比較するC++修正後、seed 4776のrejoin fault runでfreeze acceptance monitor/rejoinedが2/2 PASS（remaining max deltaは両方0、tolerance 4、1473/737 authority-frozen snapshots）し、G6 runtime gateを満たした。artifact: `tools/build/out/msvc-Release/srp-g6-fault-monitor-seed-4774-20261012-221642/`、`tools/build/out/msvc-Release/srp-g6-fault-rejoined-seed-4775-20261012-222501/`、`tools/build/out/msvc-Release/srp-g6-fault-rejoined-seed-4776-20261012-224136/`。
- G7: 7 botsの全slot spawn/shotと、bot予約slotを除いたhuman admission/full rejection/leave/rejoin slot reuseを実serverで確認。MSVC `NetworkLobby` CTestでは動的bot縮小/拡張、humanとのslot競合、slot解放後のgeneration更新を検証済み。netcheck combat coverageは試行によりBot Trace未被弾でFAILすることがある。
- G8: 0ms/150±20ms既存runtimeに加え、250±40ms+5%loss+10%reorder+5%duplicateの`-netcheck -ai 3`を独立seedで3反復し、全6 clientがPASS、state dropとshot-event未着は0。これは今回の優先受入れシナリオの反復PASS根拠。フォーム個別の以前のtour runにはBot未被弾やmovement/damage-taken比較差が残り、Syluxの一比較はbot report不在でINCOMPLETE。bombは4-client owner/observer寿命を12/12照合し、MSVC Release実server実測でも12/12 pairでXYZ誤差値を計測した。60秒中BombLossだけにRTT 100±20ms/5% lossを適用し、各pairのmean/maxは0.001単位の表示で全て0。配置後の位置一致の実測であり、移動bombの長時間/複合fault下の閾値合意とform animationを含む画面上の同時目視確認は未実施。actor display/playout delayは今回の要求に従い変更していない。G8全マトリクス（他latency/lossの組合せ、長時間run、実server queue saturation）は未完了。
- G9: CLI demo record/playbackとspectate/rejoinは確認。2026-10-13の現行MSVC GUI `-shellshot` はexit 0で、実windowのhome→match→ESC pause→homeを自動操作・撮影し、4枚を目視確認した（`tools/build/out/msvc-Release/srp-g9-shellshot-20261013-0250/`）。GUIからの`-connect`とmap-vote HUDは引き続き未確認で、手動操作の証拠とは扱わない。
- G10: 変更前後の同条件ベンチマーク、frame time分位点、allocation/frame、bytes/packets、heap/locks、CPUの比較は未実施。ユーザー指定により本作業では対象外。
- 変更は未commit・未push。責務別PR分割とCI上の受入れも未完了。作業ツリーは未stageの実装差分を維持し、181件のstaged moveはGit上でrename検出される状態を保持している。

### 17. ユーザー追加要件: timed status effect synchronization

burn / freeze / disrupt の共有範囲を、timer captureと`Start/Update/End`の共通lifecycleに限定し、各effect固有の挙動はpolicyへ残す。

- `TimedEffectSync`はauthority remaining timerとlocal timerから共通actionを決定する純粋ルール。`StatusEffectSynchronization`がPlayerStateのcapture/applyを協調し、PlayerEntity/packet詳細を各policyへ渡す。
- `BurnStatusEffectPolicy`は発火元を`ApplyAuthoritativeHit`で保持し、DOTをauthority-onlyとして分類する。NetHitPredictionはtickを予測せず、NetDamageは同tickをremote shooter claim判定から除外し、従来どおりauthority combat pathでsource credit付きに適用する。timer同期は残り時間とburn presentationのみを扱い、burner参照を書き換えない。
- `FreezeStatusEffectPolicy`は共有lifecycle上のfreeze policyとしてprediction ledger acknowledgementを保持し、PlayerEntityのfreeze timer hookへauthority値を適用する。既存PlayerEntity freeze処理がaction/input stop・alt attack終了・ice presentationを実行する。
- freeze専用クラスが残るのはpolicy境界であり、timer差分の判定とStart/Update/End dispatchは`TimedEffectSync`が共有する。freeze policyには、権威応答前に予測freezeを誤解凍しないprediction acknowledgementを残す。
- `DisruptStatusEffectPolicy`は共有lifecycleからPlayerEntityのdisrupt timer hookへ開始・更新・終了を伝え、HUD distortion/audioの既存開始処理を維持する。
- wireはProtocol 31。Protocol 30の`network-protocol-v30.hex`は変更せず、v31 PlayerState goldenと3 timerのencode/decodeを別ゲートで検証する。PlayerStateは140 bytes、最大8人snapshotでも24 bytesをpickup sectionに残し、3つの7-byte item entryを運べる。
- Freezeのaccepted timer数値は現行上限内でuint16内部timerと同じframe精度。wireはburn 9-bit/disrupt 7-bit/freeze 8-bitに範囲を割り当て、serializer/capture双方で上限を明示する。
- 実装・ビルド・runtime結果: `tools/build/build-cpp.bat msvc Release`は指定の`tools/build/out/msvc-Release`へ成功。full CTestは91/91 PASS、freeze acceptance parserは9/9 PASS、Protocol31 compound-fault/rejoin strict checkerはmonitor/rejoinedの2/2 PASS。標準netcheckはcombat-coverage assertion（被弾不足）でexit 1だが、freeze固有のstrict gateは独立に完了・解凍・generationをPASSした。G3/G4/G5/G8/G9/G10など上の残るゲートは別途未完了。
- Protocol31 controlled repeat seed 4772: `TEST ARENA`のAfflict scenarioで旧occupant退室後に同一slotへgeneration 2を再joinし、rejoin側だけRTT 250±40ms・5% loss・10% reorder・5% duplicateを適用。monitorはslot 1 generation-max=2 / occupant-changes=2 / applied-thaw 30/30 / authority-thaw 30/30 / remaining max delta 0 over 2176 authority-frozen snapshots、duration 29..151 / 29..151 frames。rejoined clientはgeneration-max=2 / occupant-changes=0 / applied-thaw 24/24 / authority-thaw 24/24 / remaining max delta 0 over 1212 snapshots、duration 28..151 / 28..151 frames。strict `freeze_acceptance.py`はmonitor/rejoinedの2/2 PASS / exit 0。4つの通常netcheckは全てexit 1だが、各ログの理由は攻撃者`FreezeFaultNoxus`から一発も被弾しないcombat-coverage assertionで、freeze専用gateとは独立。全process終了済み。artifact: `tools/build/out/msvc-Release/srp-g6-protocol31-controlled-afflict-seed4772-20261012/`。
- 2026-10-12 policy ownership follow-up: freeze synchronization owner/ledgerを`FreezeStatusEffectPolicy`/`FreezePredictionLedger`へ命名変更し、BurnStatusEffectPolicyにauthority-only DOT判定を持たせ、NetDamage/NetHitPredictionが共有policyを参照。`TimedEffectSync`/`FreezePredictionLedger`/`BurnStatusEffectPolicy`とPickup prediction fault testsを登録。指定`tools\build\build-cpp.bat msvc Release`でMSVC Release build PASS、出力は`tools\build\out\msvc-Release\FruityPrime.exe`。CTest **93/93 PASS**（`NetworkLobby` 66.37秒を含む）。一度目のbuildで既存`PredictionBook::Tick`のcaptureless lambda→function pointer→bool警告を確認し、空callback判定を`operator bool`専用に直して再buildした。Phase 11 boundary audit **1431 C++ files / A=462 / B=0 / C=119 / D=0**。このpolicy ownership checkpoint時点ではreal client prediction cancel/rollback fault runtime matrixなど残る受入れゲートは未完了で、後続seed 4807のG5結果は残る受入れゲート項を参照。
- 2026-10-12 timed-effect policy分離後のfreeze runtime再確認: 現行MSVC Release exeで専用local server、旧occupant30秒、monitor210秒、同一slotへ再joinするclient150秒を実行し、rejoin clientだけをRTT 250±40ms・5% loss・10% reorder・5% duplicate（seed 4772）に設定。`freeze_acceptance.py --require FreezeFaultMonit,1,2,2 --require FreezeFaultNew,1,2,0`は**2/2 PASS**。monitorはgeneration-max=2 / occupant changes=2 / applied-thaw 30/30 / authority-thaw 30/30 / remaining max delta 0 over 2171 authority-frozen snapshots、duration 29..150 / 29..150 frames。rejoined clientはgeneration-max=2 / applied-thaw 24/24 / authority-thaw 24/24 / remaining max delta 0 over 1217 snapshots、duration 29..151 / 29..151 frames。両reportのraw-local duration診断は28..143 frames。通常netcheckは4 processすべてexit 1だが、理由はNoxus attacker（rejoined reportではmonitorも）から一発も被弾しないcombat-coverage assertionで、freeze専用checkerとは独立。server/client process終了を確認。既存artifactを上書きせず保存: `tools/build/out/msvc-Release/srp-g6-post-policy-20261012/`。この記録時点では補正後計測runは未実施だった。後続のseed 4776 rejoin-target run（section 18）で計測修正後のstrict gateを再判定した。

### 18. G6 snapshot timer measurement correction

- 旧`FreezeTimeline`は一度受信した`authorityRemaining`を、次snapshotを受信するまでの全local simulation frameで比較していた。lost/reordered datagram中にlocal timerだけ進む差をauthority timer driftへ誤計上するため、過去runの`authority-remaining-delta-max` PASS/FAILはsnapshot到着時の同期証拠として扱わない。episode数・終了時open/remaining・slot generationの各値は別々の測定であり、この補正で書き換わらない。
- G6の残りtimer差は、新しく適用された各snapshot時点で一度だけ比較する。`freeze_acceptance.py`の4-frame gateはこのsnapshot timer差を判定し、episode min/max durationは開始・終了観測の診断表示に限る。seed 4775 runは補正前計測なのでtimer drift判定には使わない。補正後のMSVC buildを使ったseed 4776 runでrejoin fault runtimeを再判定し、monitor/rejoinedの両reportがstrict PASSした（詳細はG6項）。

### 19. 最終MSVC確認とG4再検証（2026-10-12）

- ユーザー指定で今回のG10は対象外。前後性能比較は実施していない。
- 最終のNative計測補正では、`NetFeatureCheck`の集計を公開snapshotと同じ`PlayerEntity::ModIsInPlay()`で判定する。`LoadFlags::Spawned`は死亡アニメーション中も残る場合があり、それだけで数えると死亡中のalt-formアニメーション時間がowner/observerの合計へ誤混入していた。Freeze episodeの計測条件は独立したまま維持する。
- 最終ビルド: `tools\build\build-cpp.bat msvc Release`が`tools\build\out\msvc-Release`へ成功。`ctest --test-dir tools/build/out/msvc-Release -C Release --output-on-failure`は**94/94 PASS**。`NOMINMAX`再定義警告はguardを追加して解消した。
- G4の7 hunter・faultなしフォームtour（seed 2118、60秒、3秒/phase）: 全7 clientがexit 0。`alt-form`、`alt-form-total`、`unmorph`、`halfturret`、`zoom`と測定対象のdamage項目はpairwise許容差内。全体比較は**射撃回数の11比較のみ**でexit 1。ボム数が不足してbomb metricは未実行のため、このtourはボム受入れに数えない。artifact: `tools/build/out/msvc-Release/srp-g4-form-matrix-seed-2118-20261012-232332/`。
- G4の7 hunter・複合fault tour（seed 2119、60秒、3秒/phase、SyluxのみRTT 250±40ms、loss 5%、reorder 10%、duplicate 5%）: 全7 clientがexit 0。form duration、unmorph、halfturretは許容差内。全体比較はspawn、shots、zoom、damage-taken、終了時に未処理となった1–2 shot eventでexit 1となり、form durationの不一致は報告しなかった。artifact: `tools/build/out/msvc-Release/srp-g4-form-matrix-seed-2119-20261012-232500/`。
- 最終MSVC実行ファイルで`tools\check-bomb-network.ps1 -Seconds 60`を実行し、Samus/Kanden/Syluxとlossy observerを含むowner/observer **12/12組**がPASS。各組173–783位置サンプルでXYZ誤差のmean/maxは0、寿命比較もPASS。scriptはexit 0でprocessを終了した。
- 現行`NetHooks::RemoteShotOrigin`はauthorityだけでなくobserverでも有効な`NetShotEvents::FiringRay`を優先し、描画projectileの発射位置をsmoothed puppetのmuzzle位置から独立させる。event rayが使えないobserverはlocal muzzleを維持し、authorityだけが従来intent fallbackを行う。この差分は94/94 CTest後のMSVC Release buildに含めた。ボム座標には実試合telemetryがあるが、shot projectile originそのものをevent originと数値比較するruntime受入れは未実施。
- 現行バイナリG4再試験（seed 2124、7 hunter、faultなし）では全client exit 0。Kandenは2 deathsを記録し、比較でalt-form-totalが6 observerに対してowner 115 frames / observer 63–64 framesとなり許容差を超過、Weavel shot-event 1件は終了時lifeとともにabandoned。Kandenのraw表示はlocal 270 / observer authority 221 frames、observerのpresentation disagreementは4 framesだった。alive-window差とフォーム差をこのrunだけで分離しきれず、G4全体PASSとはしない。artifact: `tools/build/out/msvc-Release/srp-g4-form-matrix-seed-2124-20261012-234445/`。
- seed 2125（7 hunter、SyluxだけRTT 250±40ms/loss 5%/reorder 10%/duplicate 5%）は全client exit 0、state drop 0。form比較はSyluxのowner 290 / observer 274 framesが2比較で許容差を1 frame超過し、他のform行は許容内。observerのauthority stateとのform disagreementは0–1 frame。Sylux bomb entity時間はowner 109 / observer 90–91 framesで許容差を超過したが、bomb-laid数はowner 4 / 各observer 4。動的位置telemetryは各observer 56–58 samplesでmean 0.039–0.042 / max 0.265–0.287 units、他の比較pairは表示精度で0。位置telemetryは記録したが絶対誤差閾値は判定していない。総合compareは38項目でFAILし、shooting/damage、bomb lifetime、shot数、終了時abandoned shot eventを含むため合格扱いにしない。artifact: `tools/build/out/msvc-Release/srp-g4-form-matrix-seed-2125-20261012-234714/`。
- seed 2126（Samus/Sylux、45秒、MorphA固定、faultなし）ではSamus alt-form-total 1231/1233 frames、Sylux側からのSamus form disagreement 0 framesでフォーム項目は許容内。Samus reportは「他playerが移動していない」combat coverageでFAIL、比較はnot-run項目多数のINCOMPLETEであり、フォーム専用の証拠に限る。artifact: `tools/build/out/msvc-Release/srp-g4-form-matrix-seed-2126-20261012-234938/`。
- G4フォーム時間とボム複製には一部実行時証拠があるが、死亡を含むowner/authorityフォーム差と複合fault下のSylux総時間・bomb lifetimeに未解決比較がある。morph animationとボム生成位置を同一GUI画面で同時に目視する確認、shot-origin runtime座標telemetry、G4/G8のより広い武器・障害組合せ、GUIのみのG9受入れ、外部Linux/macOS/CI確認は未完了。G10は今回明示的に対象外。

## 20. 2026-10-13 ray/form focused validation

- `ShotRayValidation`をwire DTOとremote shot ownerが共有するpure policyとして追加。ray directionはfiniteかつ既存decoderと同じ `0.25 < lengthSquared < 4.0` に制限し、origin・shooter position・offsetのfinite検査と6-unit muzzle boundを同ownerに置く。`IntentCodec`のdecode、`ShotEvent::HasRay`、`NetShotEvents::FiringRay`で同じ規則を使う。observerの`RemoteShotOrigin`に続き`RemoteShotDirection`も有効なshot eventを全replicaで参照し、平滑化されたbody aimで飛翔線が変わらないようにした。target-local hit presentation向け`DrawnRemoteShot` retarget policyは従来どおり別経路である。
- `MPHREAD_PHASE=FormCycle`専用phaseを追加。idle移動・射撃を含めず120 scripted framesごとにsettled状態からmorph inputを出し、observerがauthority transition directionとquantized animation progressを記録する。既定tour順序は変更しない。`run-form-matrix.ps1`は選んだHunterのjoin/slot順を保持し、`-Phase FormCycle`を明示できる。
- `tools\\build\\build-cpp.bat msvc Release`の初回は新規unit testが非inline `Vector3::Zero`を直接参照してlink失敗したため、test fixtureをconstructorへ変更して再実行。MSVC Release buildは成功し、成果物は`tools/build/out/msvc-Release`。`ctest --test-dir tools/build/out/msvc-Release -C Release --output-on-failure`は**95/95 PASS**（新規`ShotRayValidation`を含む）。一度目のlink失敗以外に当該ray/form変更のcompile errorなし。
- Phase 11 boundary auditは**1435 C++ files / A=462 / B=0 / C=119 / D=0**、未分類依存なし。`tools/test-phase11-gl-boundaries.py`は2/2 PASS。form matrix PowerShell ASTもPASS。MinGWは今回実行していない。G10免除により性能比較は未実施。
- FormCycle faultなし（seed 2130、45秒、Kanden/Samus/Sylux）では6 directed observer pairが各1052–1113 samples、direction mismatchは全て0、mean progress error 0.79–0.84/15、maxは2/15。通常netcheckは無射撃phaseなので全clientが「never took a single hit」でexit 1。form-duration pair comparisonはKandenは許容内だが、Samus/Sylux owner側759 framesに対しobserver側785–791 framesとなる比較があり、全G4合格とはしない。artifact: `tools/build/out/msvc-Release/srp-g4-form-matrix-seed-2130-20261013-001139/`。
- FormCycle複合fault（SyluxのみRTT 250±40ms、loss 5%、reorder 10%、duplicate 5%）はseed 2131/2132を反復。各runで6 directed pairすべてのdirection mismatchは0。seed 2131は1046–1118 samples、mean progress error 0.78–0.91、max 3/15。seed 2132は1051–1110 samples、mean 0.81–0.91、max 2/15。全clientのexit 1はこの射撃なしphaseのhit coverage不足で、専用form animation telemetryとは別判定。alt-form-totalは両seedとも少なくとも1比較で許容差を超過（seed 2131は2比較、owner 759 vs observer 783/787 frames。seed 2132は1比較、759 vs 786）。artifacts: `tools/build/out/msvc-Release/srp-g4-form-matrix-seed-2131-20261013-001400/`, `tools/build/out/msvc-Release/srp-g4-form-matrix-seed-2132-20261013-001639/`。
- Syluxをslot 0にした通常tour（seed 2133、60秒、Sylux/Samus、faultなし）は双方のclientがexit 0。aggregate compareはdamage-taken 27/21の1項目でexit 1。bomb-laidはSamus 6/6、Sylux 4/4。shot eventは相手側で受信分を全て発射し、stale/pushed-out/abandoned/never-arrivedは0。bomb positionは各observerで522/1308 samples、mean 0、max 0/0.036 units。form animationは各方向101/102 samples、direction mismatch 0、progress max error 2/15。artifact: `tools/build/out/msvc-Release/srp-g4-form-matrix-seed-2133-20261013-001750/`。
- 同tourのSylux-only複合fault（seed 2134、60秒、RTT 250±40ms/loss 5%/reorder 10%/duplicate 5%）も双方client exit 0。aggregate compareはspawn/shots/zoom/damageなど8項目でexit 1のため合格扱いにしない。shot eventsの受信分は両clientとも発射済みでloss/abandonは0。観測できたbomb positionは521 samples、mean/max 0 units。form animationは102/103 samples、direction mismatch 0、progress max error 2/15。artifact: `tools/build/out/msvc-Release/srp-g4-form-matrix-seed-2134-20261013-001908/`。
- ここまででauthority transition direction/progress、bomb位置、shot event queueの実行時証拠を得たが、projectileの最終spawn origin/direction自体をruntimeでevent rayと数値照合するtelemetryと、ボムをlayした瞬間のformを同一画面で目視する受入れは未実施。フォーム滞在時間は一部repeatでcomparator許容差を超えたため、death/rejoinを含む7-hunterのG4全matrixはopenのまま。G8の全障害組合せ・長時間・queue saturation、G9 GUI manual、remote CI/Linux/macOSも未完了。全processは終了し、FruityPrime server/clientは起動していない。

## 21. 2026-10-13 interpolation toggle and form comparison

- `-nointerp`でNetSmoothing（remote puppet interpolation）を切った`FormCycle`を実行（seed 2138、45秒、Samus/Sylux、faultなし）。states applied=5400 / dropped=0。ownerのalt-formは各759 frames、owner authority stateとの不一致はSamus 36/2700・Sylux 38/2700。observer alt-formは784/788 frames、authority formとのpresentation disagreementは10/13 frames。transition direction mismatch=0、各方向1108 samples、mean progress error=0.80–0.82、max=2/15。
- 補間OFFでもowner/observerのalt-form-total差は25–29 framesで残り、直前の補間ON seed 2135–2137（observer 785–788、authority disagreement 9–13）から実質変わらなかった。今回のフォーム時間差はNetSmoothing位置補間を無効化しても消えない。FormCycleは意図的に射撃しないため、netcheck/compareのexit 1はcombat coverage不足とフォーム時間比較だけによる。G4全体の合格証拠には数えない。artifact: `tools/build/out/msvc-Release/srp-g4-form-matrix-seed-2138-20261013-005300/`。
- 補間は既定でON、既存の`-nointerp`でそのrunだけOFFにできる。form matrix runnerの`-NoInterpolation`はクライアントへ`-nointerp`を渡す。
- G10前後性能比較はユーザー指定により今回免除。性能値は取得しない。

## 22. 2026-10-13 form completion-phase root cause and correction

- `[form-animation] direction-mismatch`を区間開始/終了だけ記録する診断を`NetFeatureCheck`へ追加した。authority transition/progress、適用snapshot frame、replicaのMorph/Unmorph flagsとprogressを一つのイベントに記録し、通常時のログ量を増やさず集計の不一致区間を再現できる。
- 診断付きMSVC Release run（seed 2141、Samus/Sylux、45秒、`-nointerp`、faultなし）でSylux clientがslot 0のSamusを見る1区間を特定: authorityはmorph progress 13/15・`FlagAltForm=false`、replicaはMorph animation ended・local progress 15・alt form true。authorityが安定alt snapshotを送る4 local frames前にreplica animationが完了していた。`EnterAltForm`はMorph開始時点ではmorph animationを立ち上げ、`IsAltForm`はanimation完了で`UpdateForm(true)`されるため、遅れて届く量子化progress 13が既存のhold threshold 14を越えられず、authorityの安定状態までform bitが先行していた。既存の完了animation保持policyは遷移の再再生を防ぐが、この位相先行そのものは防がなかった。
- `PlayerEntity::ModSyncFormTransitionProgress`の最終pending frame holdをprogress 14から12へ前倒しし、progress 13 snapshotの到着時にanimationがすでに終わる状況を避ける。MSVC Releaseを再ビルドし、`FruityPrime.FormReconciliation` CTest **1/1 PASS**。
- 修正後run（seed 2142、同条件）ではform-animation direction mismatchは両方向0、progress error mean 0.88/0.91・max 2/15、alt-form-total比較は783対800 framesおよび785対792 framesで両方checker許容内。remote form bitのdisagreementは各方向5 frames、最長連続1 frame（seed 2141以前は最長4）。専用FormCycleは無射撃なのでclient exit 1 / aggregate compare `INCOMPLETE`はhit-coverage不足による。これはform専用数値の確認であり、G4全体PASSではない。artifact: `tools/build/out/msvc-Release/srp-g4-form-matrix-seed-2142-20261013-012600/`。
- progress 12 holdを含むMSVC Releaseで通常tour（seed 2143、60秒、Samus/Sylux、`-nointerp`、faultなし）を実行し、両client `RESULT: PASS`。shot eventは61/61・58/58 fire済み、未着/abandoned 0。owner別bombs-laidは6/6と4/4、observer bomb-positionは522/343 samplesでmean/max 0表示。form-animation direction mismatchは両方向0、mean progress error 0.96/0.99・max 2/15。alt-form-totalは269/270 framesと269/274 framesで許容内、form disagreement最長は1/0 frame。compare全体はnot-run 2項目のため`INCOMPLETE`で、全G4 PASSとはしない。artifact: `tools/build/out/msvc-Release/srp-g4-form-matrix-seed-2143-20261013-012822/`。
- 残り: form transition中のSylux bomb authority acceptance/appearanceと発射位置を同じruntime条件で照合し、遅延・loss/reorderを含むform durationの反復比較を行う。death/rejoinを含む全G4 matrix、G8全障害・長時間・queue saturation、G9 GUI acceptance、Linux/macOS/remote CIも引き続き未完了。G10は免除のまま。

## 23. 2026-10-13 form/bomb event ordering diagnosis

- Static traceで「normal formの表示中にbombだけが出る」原因候補を、フォーム不一致とは別のイベント順序問題として特定した。ownerのPlayerInput::ProcessInputはAltForm中かつmorph animation終了後だけSpawnBombを呼ぶ。一方、NetBombs::ReceiveはIntentPacketのbomb一覧をRemoteBombStateへ保存し、NetBombs::Reconcileはreceiver側の現在フォームを検証せずにModPlaceReportedBombを即実行する。IntentPacketには同じ送信時点のAltFormStateもあるが、RemoteBombStateはform/frameを保持せず、replicaのフォーム描画は別到着のauthority PlayerStateとPlayerFormReplicationに任せている。よってbomb一覧がフォームsnapshotより先に適用された場合、receiverでbiped表示とbomb生成が一時的に並び得る。これはコード上の因果経路であり、まだ特定runtimeにおける実際の発生記録ではない。
- owner生成、dedicated authority複製、client replica複製で、sequence・net frame・local form/AltForm bit・最新authority form/progressを記録する[form-bomb]イベントをNetBombsに追加した。NetCheckClientのremote form reportには、authorityがaltを示す一方でreplicaがModIsInPlay()==falseのため滞在時間から除外されたframe数も出す。これはフォーム滞在時間差がフォームbit不一致なのか、計測不能区間なのかを分ける診断値。
- MSVC Releaseの現行バイナリでseed 2146、Samus/Sylux、60秒、SyluxのみRTT 250±40ms/loss 5%/reorder 10%/duplicate 5%、-nointerpの通常tourを実行。所有側6+4 bomb、server authority複製、各client replicaの全観測sequenceでlocal formはalt。authority/client間のbomb positionは観測範囲でmean 0、max 0/0.036 units。今回の自動runではnormal-form表示中のbombは再現していない。compare全体はzoom/damage/shots等4項目FAILのためPASS扱いにしない。artifact: tools/build/out/msvc-Release/srp-g4-form-matrix-seed-2146-20261013-014411/; net logs: tools/build/out/msvc-Release/netlog-server.txt, netlog-FMSa2146.txt, netlog-FMSy2146.txt。
- owner intentのAltForm bitとFrameもbomb生成ログへ足した後のMSVC Release run（seed 2147、45秒、同fault/-nointerp）では、Samusのbomb sequence 1–6をowner/client/serverの各ログで照合できた。ownerIntentAlt=true、serverの複製時localAlt=true、Sylux clientの複製時localAlt=trueで、今回もbiped表示中の複製は起きなかった。これは診断fieldの実出力確認で、手動症状の再現ではない。artifact: tools/build/out/msvc-Release/srp-g4-form-matrix-seed-2147-20261013-015303/。
- 同じfault条件でseed 2145の45秒FormCycleを実行。alt-form-totalがowner 759 / observer 784 framesで差25、checker tolerance 22.8を超過し1項目FAIL。direction mismatchは両方向0、progress max error 2/15、authority-alt中にreplicaがout-of-playだったframeは0。したがって滞在時間比較は引き続き未合格で、原因を単なる非in-play除外では説明できない。NetCheckClientのowner form disagreementは遅れて届くauthority snapshotと現在のローカル予測の直接比較を含むため、375/2700などの数値をそのまま可視フォーム不一致時間とは扱わない。
- 最新MSVC Releaseはtools/build/build-cpp.bat msvc Releaseで成功。FruityPrime.FormReconciliation CTest 1/1 PASS。残り: bomb/formを同じauthority frameへ結び付けるprotocol/runtime設計と、normal-form bombの実runtime再現は未完了。G4 full matrix等、前節の未完了gateも継続。
- 追加した`[form-timeline]`を使い、MSVC Releaseビルド後に同じseed 2145の45秒FormCycleを再実行。Sylux ownerのsimulation上のalt区間は11回すべて69 frameで、reportの`did=759`と一致。一方authority snapshotでownerが観測したalt区間は合計809 local frames、Samus observerのreplicaは795 frames（`saw=795`）。observer側では11回すべてauthority-snapshotのalt開始/終了とreplicaのalt開始/終了が同じsample frameまたは開始のみ1 frame差で一致し、方向mismatchも0。したがってこの`alt-form-total` FAIL（差36）は、ownerの予測状態（Morphをlocalで先行開始し、Unmorphもauthority snapshotの切替より先にlocal反映）とauthority-driven observer replicaを異なるprocess clockで直接比較した結果で、observerのフォーム遷移再生が遅れている証拠ではない。例: owner local alt開始frame183に対しauthority-alt開始snapshotはframe198、owner local alt終了frame252に対しauthority-alt終了snapshotはframe272。フォーム所有者のprediction-vs-authority時間差は実在し、checkerはこの二つの時計を分けて報告する必要がある。
- `[form-timeline]`はFormCycle試験中のみ、simulation/owner-or-replica/authority-snapshot別に遷移開始・進捗・フォーム切替・終了をsample frameとauthority frameで記録する。`[form-bomb]`と併せたseed2147では全bomb eventがalt-form状態であり、通常フォーム表示中のbombは自動試験で未再現。bomb/formのpacket-ordering改善と手動症状の直接再現は未完了。artifact: `tools/build/out/msvc-Release/srp-g4-form-matrix-seed-2145-20261013-015836/`; logs: `tools/build/out/msvc-Release/netlog-FMSy2145.txt`, `netlog-FMSa2145.txt`。
- `alt-form-total`の比較元を修正: local ownerの`did`はauthority snapshot上のalt時間、observer側の`saw`は描画replicaが適用したalt時間を使い、ownerの予測時間はNetCheckClientの別カウンターに残す。修正後にMSVC Releaseで同じ45秒seed 2145/fault/nointerpを再試験し、Samus 795/793 frames、Sylux 797/792 framesで両方向とも許容差内。Sylux ownerの予測759 vs authority797 frames、owner-state mismatch356/2700 framesは別途記録され、owner予測と権威状態が分かれる事実も隠していない。各observerのremote authority form disagreementは5/2700・9/2695、animation direction mismatchは0、最大progress errorは2/15。結果は`INCOMPLETE`（FormCycle単独のため27項目未実施）で、full acceptance PASSではない。artifact: `tools/build/out/msvc-Release/srp-g4-form-matrix-seed-2145-20261013-020419/`。
- 最新バイナリの通常tour seed 2148（45秒、Samus/Sylux、Sylux RTT 250±40ms/loss5%/reorder10%/duplicate5%、-nointerp）でbomb sequence 9件（Samus 1–6、Sylux 1–3）をowner/server/observerで照合。全owner生成はlocalAlt=true、server複製はownerIntentAlt=trueかつlocalAlt=true、各observerもownerIntentAlt=trueかつlocalAlt=true。bipedでのbomb複製は0件で未再現。form totalは両方向とも許容範囲、bombs-laid/framesも一致。compare全体はshots/zoom等4項目FAILのためG4 PASSではない。artifact: `tools/build/out/msvc-Release/srp-g4-form-matrix-seed-2148-20261013-020604/`; logs: `tools/build/out/msvc-Release/netlog-server.txt`, `netlog-FMSa2148.txt`, `netlog-FMSy2148.txt`。

### 24. 2026-10-13 ボム即時生成とフォーム遷移の加速

- 仮案だった90フレームのBombFormPresentationGate待機は、再検討して撤去した。ボム複製はremote copyのMorph完了を待たず、NetBombs::Reconcileで報告を受けたシミュレーション段に生成する。
- 新しいbomb sequenceごとに、NetBombsはowner intent / authority stateの参照をPlayerFormReplicationへ渡し、一度だけAlt-action cueを通知する。intent bitの解釈、slot/life検証、authority form decode、Morphの再開・開始/反転はPlayerFormReplicationが担当し、ボム側が持つのはsequence単位のcue重複防止だけ。固定時間の生成待ちや90フレームfallbackはない。
- MSVC Release buildは指定のtools\build\build-cpp.bat msvc Releaseで28.1秒、tools\build\out\msvc-Releaseへ成功。FruityPrime.IntentTouchPayloadとFruityPrime.FormReconciliationのfocused CTestは2/2 PASS、git diff --checkもPASS。
- 複合faultのFormBombCycle seed 2150（Samus/Sylux、45秒、-nointerp、Sylux RTT 250±40 ms、loss 5%、reorder 10%、duplicate 5%）では、両ownerが各11個を配置し、相手側も各11個を観測。alt-form-totalは799/784、794/796 frameでchecker許容範囲内。artifact: tools/build/out/msvc-Release/srp-g4-form-matrix-seed-2150-20261013-022439/; logs: tools/build/out/msvc-Release/netlog-FMSa2150.txt, netlog-FMSy2150.txt, netlog-server.txt。
- Sylux clientのsequence 4 / frame 949とSamusのsequence 6 / frame 1364では、[form-sync] alt-action advanceの後すぐに[form-bomb] replica-placedが記録され、そのsimulation frame内で[form-timeline]がAltへのcommitを確認した。Samus sequence 9はframe 2087の生成後、frame 2088でcommitし、残差は1 simulation frame（約16.7 ms）。生成ログはシミュレーション段の途中に出るため、同じframeの後続timelineがAltを確認しても、生成ログ時点のlocal form表記はbiped+morphingの場合がある。
- targeted run全体のexitは1。原因はspawn countersの差31/32が許容値30を超えた2比較で、bomb数とalt-form durationはPASS。専用phaseのため他7項目は未実行で、G4全体PASSではない。起動したtest processは終了済み。
- SRP追補: NetBombsはowner intentと最新authority stateをフォーム同期ownerへ渡すだけにし、AltForm/InPlay bit解釈、slot/life照合、authority form decodeをPlayerFormReplicationへ移した。NetBombsに残すのはsequenceごとの重複cue抑止とbomb生成/位置同期。
- このSRP追補後のMSVC Releaseを`tools/build/build-cpp.bat msvc Release`で再構築し、27.3秒で`tools/build/out/msvc-Release`へ成功。focused CTest 2/2 PASSの後、全CTest **95/95 PASS**（66.46秒、NetworkLobby含む）。`git diff --check`もexit 0（既存LF→CRLF警告のみ）。
- 最新バイナリのFormBombCycle seed 2151（45秒、Samus/Sylux、`-nointerp`、Sylux RTT 250±40 ms/loss 5%/reorder 10%/duplicate 5%）では、両方向のbombs-laidが11/11、alt-form-totalが794/786および794/789で許容内。Sylux clientのSamus replicaはframe 175にalt-action advance、同frameでbomb replica生成、同simulation frame後半にAlt commit。対象指標はPASSだが、run全体はmovement比較24.7対23.6（許容±1.0）のため両client/比較がexit 1。FormBombCycleでは他combat項目は未実行であり、G4全体PASSではない。artifact: `tools/build/out/msvc-Release/srp-g4-form-matrix-seed-2151-20261013-023222/`; logs: `tools/build/out/msvc-Release/netlog-FMSy2151.txt`, `tools/build/out/msvc-Release/netlog-FMSa2151.txt`, `tools/build/out/msvc-Release/netlog-server.txt`。
- 同じMSVC Release成果物で`FruityPrime.exe -netcombatcheck "TEST ARENA" -noupdate`を実行し、exit 0 / **98 combat assertions PASS**。標準出力redirectを使いassertion一覧と最終集計を回収した。これはauthority combat harnessの受入れであり、G4全client/authority runtime matrixやGUI目視確認の代替ではない。

## 25. 2026-10-13 NetSmoothing共通表示とremote bot attack follow-up

- NetSmoothingの同じplayout clockでremote puppet位置、form transition progress、bombの描画位置/消失時点をサンプルする。BombEntity.Positionは最新owner報告座標を保持し、collision/damage/detonationはowner/authority policyに残す。shot originは従来どおりFiredShot eventを使い、描画補間位置から攻撃座標を再計算しない。wire layout/protocol versionは変更なし。
- `FruityPrime.NetworkSmoothingTimeline`を追加し、同一fractional read frameでplayer pose/form progressを読み、bombのGone表示をplayout frameまで保留する条件を検証。RemoteBombStateのaccepted/stale/no-section receive判定もIntentTouchPayloadへ追加した。
- `tools/build/build-cpp.bat msvc Release`は`tools/build/out/msvc-Release`へ成功。MSVC Releaseの全CTestは**97/97 PASS**（71.79秒、NetworkLobby 66.41秒を含む）。focusedのIntentTouchPayload/NetworkInputHistory/NetworkSmoothingTimelineも3/3 PASS。
- interpolation既定ONの複合fault FormBombCycle（seed 2162、Samus/Sylux、45秒、Sylux RTT 250±40ms・loss 5%・reorder 10%・duplicate 5%）では、form-animation direction mismatchは両方向0、progress error max 2/15、mean 0.92/0.98。bomb-positionは2608/952 samples、mean/max 0 units。Samusのalt-form-total 793/779（差14、許容23.8）、Sylux 1321/1300（差21、許容39.6）、両方向のbombs-laid 11/11、50/50は各checker範囲内。run全体はSamusが一度も被弾せず、Sylux側は「他playerが移動していない」というnetcheck受入れ判定で両clientがexit 1。compareは**INCOMPLETE**（10 covered/agreement、22 not run）であり、G4全体PASSではない。artifact: `tools/build/out/msvc-Release/srp-g4-form-matrix-seed-2162-20261013-042501/`。
- documented server-bot経路`-server -bots 6 -botlevel 3` + `-netcheck -ai 3 -seconds 45`では、slots 2–7の6 botをjoinさせ、AI clientはexit 0 / `RESULT: PASS`。remote報告にはBot Trace/Sylux/Noxus/Spireのalt-attack press（24/15/6/108）とSylux bombs-laid 15を記録。clientのshot eventは301 received/300 fired、never-arrived 0、dedicated serverの観測区間は12/12 fired。artifact: `tools/build/out/msvc-Release/srp-bot-replica-check-20261013-042705/`。これはremote bot action eventの実行証拠で、攻撃/変形animationの画面上の目視証明ではない。
- 実行したserver/client processは全て終了。フォーム/ボムのGUI手動確認、G4全weapon/form matrix、G8残るfault/long-duration/queue saturation、G9 GUI-only acceptance、外部Linux/macOS/remote CIは未完了。G10前後性能比較はユーザー指定どおり対象外。

## 26. 2026-10-13 変形アニメーションを共通presentation clockへ統合

- `NetSmoothing::PresentationOwnsRemoteReplicas()`で、feature toggleとclient/authority roleによるpresentation ownershipをsnapshot ringの起動状態から分離した。スムージング有効なclientでは、ringのwarm-up中もrelayされたMorph入力edgeをpuppetへ再生せず、remote form state（warm-up後はbuffered sample）が遷移を開始する。authority/hostと`-nointerp`は従来の即時入力経路を保つ。
- `PlayerFormReplication::ApplyPresentation`は、remoteのMorph/Unmorph方向とphaseを毎simulation stepのNetSmoothing sampleで適用し、native clipを同phaseに保持する。遅延snapshotを順次適用する`FormTransitionPlaybackPolicy`の完了保持はnative pathに残し、presentation pathがread pointより先に完了したclipを誤って固定しないよう責務を分けた。wire/protocol変更なし。
- 変更後の`tools\build\build-cpp.bat msvc Release`は`tools\build\out\msvc-Release`へ成功。現行ビルドのCTestは**97/97 PASS**（71.88秒、`NetworkLobby` 66.42秒を含む）。`NetworkSmoothingTimeline`と`FormReconciliation`のfocused CTestも最終変更後に2/2 PASS。
- 最新runtimeのFormCycle seed 2168（45秒、Samus/Sylux、interpolation ON、SyluxだけRTT 250±40ms・loss 5%・reorder 10%・duplicate 5%）では、`netcheck-form-animation`のdirection mismatchは両方向0。sampled transition progress errorはSamusが観測するSyluxで平均/最大0、Syluxが観測するSamusで平均0.04/最大2/15。各reportのform disagreementは43/38 frames、longest runは双方4 frames。画面pixel/GUI目視ではなく、network telemetryによる確認。
- 同じrunのFormCycle `alt-form-total`比較は`793 vs 751`（差42、許容23.8）と`728 vs 684`（差44、許容21.8）で両方向FAIL。通常netcheckはフォーム周期のみのシナリオなので移動/戦闘coverage不足でexit 1、compareもexit 1である。従って変形animationの方向/phase計測改善は確認できたが、フォーム滞在時間のG4受入れは未合格であり、G4全体もPASSとしていない。artifact: `tools\build\out\msvc-Release\srp-g4-form-matrix-seed-2168-20261013-052300\`。debug logs: `tools\build\out\msvc-Release\netlog-FMSa2168.txt`, `tools\build\out\msvc-Release\netlog-FMSy2168.txt`。
- test server/clientは終了済み。GUIでのSamus/Sylux変形・攻撃・ボム目視、G4全weapon/formとdeath/rejoin matrix、G8の全fault/長時間/queue saturation、G9のGUI-only acceptance、remote CIは未完了。G10前後性能比較は引き続き対象外。
- seed 2169でalt-form-totalが一方向だけ差27 framesとなったため`netlog-FMSa2169.txt`を追跡した。ログのauthority-snapshot form commitとreplica simulation commitには3–4 local framesの差が見えるが、latest authority snapshotとdelayed presentation sampleは別の時計なので、ログだけでその差を表示遅延の根因とは断定しない。コード経路を見直し、PresentationClockのstable form bitとローカルフォームが不一致ならsnapshot用`FormReconciliation`の猶予経路を通さず即時に表示フォームへ反映するよう分離した。
- このstable-sample適用修正後、`tools\build\build-cpp.bat msvc Release`でMSVC Release build成功（26.9秒）。最終ソースのfocused `NetworkSmoothingTimeline` / `FormReconciliation` CTestは2/2 PASS、続く全CTestは**97/97 PASS**（71.88秒、`NetworkLobby` 66.43秒を含む）。FormCycle seed 2170（45秒、interpolation ON、SyluxだけRTT 250±40ms・loss 5%・reorder 10%・duplicate 5%）の`netcheck-form-animation`はdirection mismatch 0/0、mean progress error 0/0.05、最大0/2（15段階）。alt-form-totalはSamus 793/780（差13、許容23.8）、Sylux 795/794（差1、許容23.8）で両方向許容内。artifact: `tools\build\out\msvc-Release\srp-g4-form-matrix-seed-2170-20261013-053611\`、debug logs: `tools\build\out\msvc-Release\netlog-FMSa2170.txt`, `tools\build\out\msvc-Release\netlog-FMSy2170.txt`。
- seed 2170 run自体はフォーム周期専用のため両netcheckがcombat-coverage（hitなし）でexit 1。report comparisonもspawn parityが2699/2659と2659/2700で許容±30を超えexit 1。フォームanimation/durationのtarget指標は通過したが、これはG4全体受入れや画面目視ではない。G4 full weapon/form・death/rejoin matrix、G8全fault/長時間/queue saturation、G9 GUI acceptance、remote CIは引き続き未完了。

### 26.1 Weavel安定フォーム確定もNetSmoothing表示クロックへ統合

- seed 2171の7 hunter複合fault tourで、Weavelの権威Alt総時間157 framesに対しobserverの表示Altは110–113 framesとなった。Trace observerではWeavel Morph開始からstable biped/Alt mismatchの解消まで約41 framesかかり、`WeavelReplicaTransition::StallFrames`に基づく90-frame fallbackがbuffered stable sampleより優先されていた。
- `PlayerFormReplication::ApplyPresentation`をWeavelにも通し、stable sampleが現在フォームまたは遷移フラグと食い違うときはWeavelの表示フォームだけを即時確定する。Turretの生成・破棄・HP・位置は引き続き`ModApplyWeavelState`の権威policyで更新し、最新snapshot fallback時のstall policyは維持する。stable-sample判定を`FormPresentationPolicy`として抽出し、stable mismatch/stale transitionとactive phaseのunit coverageを追加した。
- `FormPresentationPolicy`の安定sample判定を`FormReconciliation`テストへ追加し、form bit mismatch、stale morph/unmorph flags、遷移中sampleを検証。MSVC Releaseは指定の`tools/build/build-cpp.bat msvc Release`で成功し、focused CTest 2/2 PASS、全CTest 97/97 PASS。Weavelのclipもpresentation sampleで駆動するようになったため、`NetFeatureCheck`のWeavel除外を解除し、shared sampleがある場合にnative clip方向/位相を記録するよう修正。計測器変更を含む増分MSVC buildも成功、focused CTest 2/2 PASS。
- 7 hunter複合fault tour seed 2171（60秒、SyluxだけRTT 250±40ms・loss 5%・reorder 10%・duplicate 5%、NetSmoothing既定ON）を最終計測器で再実行。全7 clientがexit 0。Weavelのnative form clipは6 observerすべてでdirection mismatch 0、mean/max progress error 0/15（80–93 samples）。WeavelのAlt滞在はauthority 282 framesに対しobserver 280–283 framesで全比較許容内。faulted Syluxのform disagreement logは1 frameの短い窓を2件記録し、以前のseed 2171 runで見えた41-frame mismatchは再現しなかった。artifact: `tools/build/out/msvc-Release/srp-g4-form-matrix-seed-2171-20261013-055958/`; debug logs: `tools/build/out/msvc-Release/netlog-FMSy2171.txt`等。
- フォーム・ボム複合tour seed 2172（60秒、Samus/Sylux、SyluxだけRTT 250±40ms・loss 5%・reorder 10%・duplicate 5%、NetSmoothing既定ON）では、bombs-laidが両方向15/15。bomb-positionは3629/1291 samplesでmean/max誤差とも両方向0 units。form-animation direction mismatchは0/0、progress errorはSamus観測0/0、Sylux観測mean 0.03/max 2/15。alt-form-totalはauthority/observer 1079/1073と1074/1073で許容内。専用phaseのためcompareはINCOMPLETE（23 not run）、各netcheckにも移動/戦闘coverage不足があり、G4全体PASSではない。artifact: `tools/build/out/msvc-Release/srp-g4-form-matrix-seed-2172-20261013-060424/`。
- このtourの全比較はexit 1 / 25 mismatchであり、spawn、shot数、damage-taken、終了時にabandonされたshot event等のG4差が残る。form duration/clip位相の合格はG4全体合格ではない。GUIでのフォーム・攻撃・ボム目視、G4全combat/death/rejoin matrix、G8残り障害/長時間/queue saturation、G9 GUI-only acceptance、remote CIは未完了。G10はユーザー指定どおり対象外。


### 27. 2026-10-13 ペア比較の受入れ窓と通常tour再確認

- NetFeatureCheckは各peerについてowner-side mine-withとobserver-side saw-withを同じレポートに出す。compare-reports.pyは両方がある場合にそのpair窓を比較し、旧reportのみの場合は従来のwhole-run値へfallbackする。jumpはpair窓内の高さrangeで比べ、カウンター差分とは別の指標にした。tools/netcheck/test_compare_reports.pyはpair窓を優先するケースを検証（1/1 PASS）。
- この変更後にMSVC Releaseを指定のtools/build/build-cpp.bat msvc Releaseでtools/build/out/msvc-Releaseへ構築。全CTest 97/97 PASS（71.87秒、NetworkLobbyを含む）、git diff --check PASS。これが今回の最後のbuild。
- faulted FormCycle seed 2186（45秒、Kanden/Spire、SpireのみRTT 250±40ms/loss 5%/reorder 10%/duplicate 5%、debug log無効）は、spawn 2699/2686、alt-form-total 852/840 framesと859/866 framesで対象pair差が各許容内。両clientは無被弾phaseのcombat coverage不足でexit 1、比較はINCOMPLETE（25項目not run）。これはフォームphaseの比較であって通常tourの合格ではない。artifact: tools/build/out/msvc-Release/srp-g4-form-matrix-seed-2186-20261013-064253/。
- faulted通常tour seed 2187（45秒、Kanden/Spire、同じSpire-only fault、debug log無効）は両netcheckがexit 0だが、全比較は8 checks FAIL。Kanden→Spire方向はalt-form-total 277/273、bomb lifetime 174/179、zoom 168/164、damage-taken 11/11が許容内。一方、Spire→Kandenはalt-form-total 35/36とshot event 18/18が通るものの、spawn 1553/2012、movement 87.0/89.8、shooting 486/627、zoom 19/0、damage-taken 16/21が不一致。Kanden alt-attack pressは24/14で記録されたが、この指標は現状infoでありanimation受入れではない。artifact: tools/build/out/msvc-Release/srp-g4-form-matrix-seed-2187-20261013-064359/。
- debug logを無効にした同条件runでも通常tourの複合比較差が残るため、seed 2185の値を診断ログ負荷だけでは説明できない。ただし2187にはnative timeline logがなく、各差の根因は未特定。現時点で、専用form telemetryが示す方向/phase一致と、通常tourでFAILしたspawn/zoom/damage等を同一の「フォーム同期合否」にまとめない。
- 攻撃状態は現行Intentのheld AltAttack bitとpress historyで権威/peerへ伝わり、実弾はFiredShot event、bombはsequence/owner報告位置で同期する。seed 2187のalt-attack値はpress edgeの観測数に過ぎず、alt-form攻撃animationの見た目・再生位相を証明しない。NetSmoothingではremote pose、form transition animation、tracked bomb visualを同じread frameでsampleする実装とし、ゲームプレイ上のshot origin/authority hit/detonationを補間表示座標から作り直さない。GUIでの敵AltAttack animationとbomb出現の同時目視確認は依然未完了。
- 2187のserver/client processは終了済み。G4全weapon/formとdeath/rejoin matrix、G8の残るfault条件/長時間/queue saturation、G9 GUI-only acceptance、remote CIは継続して未完了。G10前後性能比較はユーザー指定どおり対象外。

### 27.1 2026-10-13 faulted normal tour seed 2188

- seed 2188は同じ45秒・Kanden/Spire・SpireのみRTT 250±40ms/loss 5%/reorder 10%/duplicate 5%で、debug logを有効にして再実行。両netcheckはexit 0。pair比較はZoom差だけがFAIL（Kanden→Spire方向 157/135 frames、許容差を22 frames超過）し、25 covered中20がagreement、残り1判定は全比較exit 1。spawnは両方向許容内。alt-form-totalは276/275と250/245で許容内、bomb lifetimeは174/181、shot eventsは54/54および62/62で未着・abandonなし。artifact: tools/build/out/msvc-Release/srp-g4-form-matrix-seed-2188-20261013-065231/。
- AltAttack pressはKanden→Spire 24/15、Spire→Kanden 18/10だったが、pairwise infoでありcheckerは合否対象にしていない。既存debug logにもowner press frameからreplicaでのreplayまでの対応記録はなく、この差を欠落animationの根因とは断定しない。NetInputHistoryのfocused testは送信側16-frame press historyの保持だけを確認し、受信側MissedPressesのlifecycle初回処理と実際のanimation再生は未検証。
- 今回のtourで比較差はseed 2187の8項目からseed 2188のZoom 1項目へ変わった。従って通常tourの複合fault反復安定性はまだPASSではない。Zoomの計測差とAltAttack native animationの目視確認を分けて追う。test server/client processは終了済み。
### 27.2 2026-10-13 faulted 60-second normal tour and shared form presentation

- seed 2190を60秒に延長して再実行（Kanden/Spire、SpireのみRTT 250±40ms・loss 5%・reorder 10%・duplicate 5%、NetSmoothing既定ON）。両clientのnetcheckは`RESULT: PASS`。比較は25項目中20一致、未実施/n/aを除きZoom 1項目だけがFAIL。KandenをSpireが見たZoomは168/167 framesで許容内、SpireをKandenが見たZoomは141/116 framesで差25（許容10）だった。よって60秒のfaulted通常tour反復安定性はまだPASSではない。artifact: `tools/build/out/msvc-Release/srp-g4-form-matrix-seed-2190-20261013-065824/`。
- 同runのフォーム滞在はalt-form-totalが279/276 framesと217/214 framesで双方許容内。form animation診断は両方向ともtransition direction mismatch=0、progress errorはmean 0/0.05・max 0/2（15段階）。Kandenが置いたbombは2/2、位置比較173 samplesでmean/max差0。AltAttack press数の差は引き続き診断値であり、animation再生の可視受入れを証明しない。
- 変形の見た目もNetSmoothingの共通presentation clockに含まれる。`NetPlayerBridge::ApplyState`はremote poseと同じ`SampleForm` read frameを取得し、`PlayerFormReplication::ApplyPresentation`がMorph/Unmorph clipの向き・位相・安定フォーム確定をそのsampleへ合わせる。Weavelもturretの権威lifecycle policyを保ったままform clipを同clockで進める。`NETWORK-SMOOTHING.md`と`TestNetSmoothing`にこの契約を記録・検証済み。今回のruntime値はbuffered form timelineの数値整合であり、GUIでの目視受入れではない。
- 今回のZoom差の根因と、敵AltAttack animationの再生を実画面で確認する受入れは未完了。G4全weapon/form・death/rejoin matrix、G8残りfault/長時間/queue saturation、G9 GUI-only acceptance、remote CIもopen。server/client processは終了し、G10比較は引き続き対象外。

### 27.3 2026-10-13 Zoom parity計測根因の修正とform presentation再確認

- seed 2190でZoom mismatchになったowner reportは、owner側の即時予測`EquipInfo::Zoomed`とobserver側のauthority-replicated `PlayerState::FlagZoomed`を比較しており、別々の状態権威を混ぜていた。ownerのZoom parity計数をauthority snapshot基準へ合わせ、observerは従来どおり適用済みauthority状態を計測するようharnessだけを修正した。実 gameplay処理・protocolは変更していない。
- debug logではfaulted Spire ownerの予測Zoomがauthorityより先行し、local=true / authority=falseから19 local frames後にauthority=trueへ移った。これは比較器が予測状態とauthority状態を比較していた根拠であり、Zoom input/render自体の19-frame遅延を示す測定ではない。
- 修正後のMSVC Release buildは成功。全CTest 97/97 PASS、`tools/netcheck/test_compare_reports.py` 1/1 PASS。seed 2191（60秒、Kanden/Spire、SpireのみRTT 250±40ms・loss 5%・reorder 10%・duplicate 5%、NetSmoothing ON）は両netcheckがPASSし、Zoom parityもKanden→Spire 162/163 frames、Spire→Kanden 174/174 framesで両方向許容内。artifact: `tools/build/out/msvc-Release/srp-g4-form-matrix-seed-2191-20261013-071408/`。
- pair全体はPASSしていない。compareは26項目を評価し3件FAIL: double-damage 41/20 frames、Spire→Kanden shooting 1146/1304 beam-frames、Kandenのshot event 1件abandoned。alt-form-totalは277/277と317/314 frames、bomb lifetimeは174/182 framesで許容内。form-animationは両方向ともdirection mismatch 0、progress errorはmean 0/0.04・max 0/2（15段階）。AltAttackは24/14または24/16 press observationでinfo扱いのまま、画面に描画された攻撃clipの同期を証明しない。
- ユーザー確認: 表示の滑らかさにはMorph/Unmorphの変形アニメーションも含める。現在の実装はremote position、form transitionのdirection/progressとnative clip、tracked bomb visual/lifetimeをNetSmoothingの同じpresentation read frameから適用し、攻撃発射位置・権威hit・detonation位置は補間表示座標から再計算しない。既存の`TestNetSmoothing`と`NETWORK-SMOOTHING.md`がこの共有時計を検証・記述する。今回の数値はtimeline整合であり、AltAttackのGUI目視受入れは引き続き未完了。
- G4全weapon/form・death/rejoin matrix、敵AltAttack animationの実画面確認、G8残りfault/長時間/queue saturation、G9 GUI-only acceptance、remote CIは継続して未完了。試験processは終了済み。G10比較はユーザー指定どおり対象外。

### 27.4 2026-10-13 press replay SRP and shared presentation follow-up

- `NetInputHistory`は送信側のslot単位press-edge履歴、`NetInputPressReplay`は受信側のbounded history dedupe/replayと責務を分けた。初回有効intentでもそのlifeの履歴窓を回復し、slot generation/life変更、frame wrap、順序逆転・重複を専用CTestで固定。複数の発砲edgeがまとめて届いた場合の最古press rewind ageは既存意味を維持。`NetPlayerBridge`からpress cursorと履歴配列を除いた。
- remote display pose・Morph/Unmorph clipの向き/位相・bomb visual/lifetimeは`NetSmoothing`の同一read frameを使う。snapshot位置の事前復元はclient simulation/collision専用とし、post-simulation `ApplyState`でposeとform sampleを適用する。安定フォームへの切替を先に適用してからform originに対するpose補正を行い、切替tickの座標ずれを避ける。発砲origin、authority hit、bomb gameplay/detonationを補間display座標から再計算しない。
- `tools\\build\\build-cpp.bat msvc Release`は成功し、出力は`tools/build/out/msvc-Release/FruityPrime.exe`。全CTest **98/98 PASS**（71.82秒、NetworkLobby 66.39秒）、`tools/netcheck/test_compare_reports.py` **1/1 PASS**。このrunには新設`NetworkInputPressReplay`とMorph/Unmorph `NetworkSmoothingTimeline`が含まれる。
- seed 2192の60秒faulted normal tour（TEST ARENA、Kanden/Spire、Spire側のみRTT 250±40ms/loss 5%/reorder 10%/duplicate 5%、interpolation ON）で両netcheckはexit 0。フォームanimation direction mismatchは両方向0、progress errorはmean 0/0.05・max 0/2（15段階）、alt-form-totalは277/277および41/39 framesで許容内。Kandenが置いたbombの位置は178 samplesでmean/max誤差0、bomb数は2/2だが必要数3未満のためparity gateは未評価。double-damageは同一authority state基準で1707/1706 frames、許容内となり前回の予測/authority計測不一致は再現しなかった。
- 通常tourのpair比較全体は**FAIL: 3項目**（Kanden→Spire movement 140.6/211.1、Spire→Kanden spawn 2568/3029とdamage-taken 29/33）。24項目をcoverageし18がagreement。これらをフォーム/ボムtimeline合格と混同せず、G4/G8全体受入れもPASSとしていない。artifact: `tools/build/out/msvc-Release/srp-g4-form-matrix-seed-2192-20261013-075507/`。server/clientは終了し、GUI目視のAltAttack animation/bomb出現、G4全matrix、G8長時間/queue saturation、G9 GUI acceptance、remote CIは引き続き未完了。G10比較は対象外。

### 27.5 2026-10-13 session-state owner extraction and validation

- `NetSession`が個別に保持していた接続、実行時role/frame、traffic、identity、connection health、replica stream、match/authority identity、client lobby状態を`SessionContext`へ集約した。各state/serviceは引き続き各自の不変条件を持ち、`NetSession`は既存static APIを保つ互換Facadeとして委譲する。旧static state参照の残存検索は該当なし。`DedicatedServer`はsocket/lifetimeと各Serviceへの依存配線を行うcomposition rootであり、周期処理順序は`ServerRuntimePump`が所有することを再確認した。
- `tools\build\build-cpp.bat msvc Release`成功、MSVC Release/Ninja出力は`tools/build/out/msvc-Release/FruityPrime.exe`。CTest **98/98 PASS**（`NetworkLobby` 66.39秒を含む）。`NetworkSmoothingTimeline`、`FormReconciliation`、`FreezeTimeline`、`NetSessionSimulationAdapter`、`NetworkInputPressReplay`を含む。`git diff --check`とcached diff checkもexit 0（CRLF変換警告のみ）。MinGWは確認していない。
- 変形アニメーションも表示の滑らかさの対象として扱う。remote pose、Morph/Unmorphのclip位相、bomb位置/寿命はNetSmoothingの共有read frameで描画し、攻撃原点・権威hit・bomb detonationのgameplay座標は補間表示座標から作らない。form timelineの数値チェックはpassだが、AltAttack animation/bomb appearanceの実画面確認は未実施。
- 受入れは未完了。G6 extended freeze/rejoinの前回strict remaining-duration比較は±4 frame条件を2/2で満たさず最大7 frame差、G4 seed 2192の通常tour pair比較は3項目FAILのまま。bot join/leaveとhuman slot競合は`NetworkLobby`を含むCTestでpass。残りはG4全weapon/form・death/rejoin matrixとGUI確認、G8残余fault条件/長時間/queue saturation、G9 GUI-only acceptance、remote CI。今回のbuild/CTestはSRP state owner変更の検証であり、これら未完了のruntime/GUI/CIゲートを置き換えない。G10前後性能比較はユーザー指定どおり対象外。

### 27.6 2026-10-13 owner form clock、replica culling、timer snapshot age

- 前回のCombat Hall目視runの`Player.netlog.txt`では、人間slot 3に`08:43:42.253 slot 3 stalled unmorph completed from biped+unmorphing/Unmorph`が記録された。これはownerのnative clipが完了した後、遅延authority progressを適用して終端で止める経路と一致する。`PlayerFormReplication::ApplyOwnerPrediction`ではform/directionのauthority reconciliationを維持しつつ、snapshot progressをnative owner clipへseek/pauseしないよう分岐した。serverのweapon/collision用form authorityは変更していない。remote replicaは引き続きNetSmoothingのpresentation clockを使う。
- remote actorの`NodeRef`はauthorityからwire同期される値ではなく、描画時のmap-part cullingに使うローカル値。smoothed positionへ`Move`した後にportal walkingが有効だが不可視の古いnodeを返す場合、現在位置からnodeを再解決し、本体がshadow-onlyになるのを防ぐ。位置を解決できない場合は既存のunresolved-node body draw fallbackを使う。診断は`[player-node] repaired hidden replica`へ出す。今回のMSVC buildは通ったが、この表示修正後のGUI目視確認はユーザーの再テスト待ち。
- burn/disrupt/freezeの共通timer適用は、同じsnapshotをpacket gap中に再適用してlocal countdownを巻き戻さないよう、受信後の`NetSession::SnapshotAge()`分をreported timerから差し引く。年齢不明時はwire値を保持し、0未満へは進めない。`FreezeTimeline`もlocal観測時点へprojectした値で比較し、診断logにraw/projected/ageを出す。`TestTimedEffectSync`と`TestFreezeTimeline`にsnapshot age/zero clamp/age一致比較を追加。
- `.\tools\build\build-cpp.bat msvc Release`成功、出力は`tools/build/out/msvc-Release`。CTest **98/98 PASS**（72.05秒）。`py -3 -m unittest test_freeze_acceptance` **11/11 PASS**、`py -3 -m unittest test_compare_reports` **1/1 PASS**、`git diff --check`とcached diff checkはexit 0（CRLF変換警告のみ）。MinGWは実行していない。
- 新buildで`run-freeze-rejoin.ps1 -FaultTarget Rejoined -Seed 4780 -MonitorSeconds 180 -RejoinedSeconds 90 -OldOccupantSeconds 30`を実行。250±40ms RTT・loss 5%・reorder 10%・duplicate 5%のfaulted再join clientを含み、monitor/rejoined checker **2/2 PASS**。monitor slotはapplied/authority thaw 5/6・remaining max delta 0 over 611 samples、rejoined generation 2 slotは3/3・delta 0 over 212 samples。duration診断はmonitor 30..151 / 30..151、rejoined 31..150 / 31..150 frames。attacker/monitor/old/rejoined netcheckはexit 0、server/clientは終了済み。`NetworkLobby` CTestは動的bot join/leaveとhuman slot競合を含めPASS。
- 残り: owner transform末尾の停止とenemy shadow-onlyのGUI再確認、G4全weapon/form・death/rejoin matrix、G8残余fault条件/長時間/queue saturation、G9 GUI-only acceptance、remote CI。G10性能比較は今回対象外。PR分割/deliveryは未実施で、変更は未commit・未push。

### 27.7 2026-10-13 owner transform continuity、display-position culling、sparse snapshot sampling

- Combat Hall手動runで見えたownerのtransform終端停止は、local prediction自体をserver clockで再生していたのではなく、Unmorph中に届いたstable biped authority stateがowner native clipを終了させるreconciliation pathが原因と判断した。`OwnerFormPredictionPolicy`でownerがMorph/Unmorph中のstable authority correctionをclip完了まで保留し、終了後にform correctionを再開する。authorityのweapon/form/collision判定とremote replicaのNetSmoothing presentation clockは変更していない。slot transitionごとの診断logと`FormReconciliation` unit testを追加した。
- enemy shadow-onlyはremote `NodeRef`をwireで同期する問題ではなく、smoothed world positionとlocal map-part culling NodeRefのずれとして扱う。責務を`PlayerEntityNetCulling`へ分け、remote表示位置を反映した後に同位置からNodeRefを再解決する。位置から解決できない場合はstale nodeを理由にbody drawを止めない。表示位置からNodeRefへの遷移diagnosticを追加した。修正後の画面上での目視再確認はまだ行っていない。
- fault下のsnapshot欠落でもNetSmoothingが最新値へ不意にfallbackしないよう、ring上の受信済み前後frameからpose/formを補間するsparse samplingを追加し、`NetworkSmoothingTimeline`の欠落frame testを通した。
- `tools\\build\\build-cpp.bat msvc Release`成功。MSVC Release出力は`tools/build/out/msvc-Release/FruityPrime.exe`。全CTest **98/98 PASS**（72.28秒、`NetworkLobby` 66.32秒）。`git diff --check`とcached diff checkはexit 0（既存CRLF変換警告）。MinGWは確認していない。
- 現行buildのfaulted form matrixを`MP3 PROVING GROUND`（Combat Hallのinternal room name）で45秒実行。Samus/Kanden/Syluxの3 client、KandenのみRTT 250±40ms・loss 5%・reorder 10%・duplicate 5%。全clientのnetcheckはexit 0。pair reportのform/alt-form total/unmorph/bomb主要値は各許容内だったが、compare全体は**FAIL: 5 checks**（spawn 2件、damage-taken 2件、Kandenのshot event abandoned 1件）、82 covered / 65 agreement。G4全体のPASS根拠とはしない。artifact: `tools/build/out/msvc-Release/srp-g4-form-matrix-seed-4782-20261013-094208/`。
- CTestとnetwork runnerは終了済みで、FruityPrime/MphReadの残留processはない。manual GUIは新たに起動していない。ユーザーが次に目視するときはowner Unmorph末尾と、Combat Hallのportal/culling境界でremote enemy bodyがshadow-onlyにならないことを確認する。受入れ残りはG4全weapon/form・death/rejoin matrixとこのGUI確認、G8残余fault/長時間/queue saturation、G9 GUI-only acceptance、remote CI。G10性能比較はユーザー指定で対象外。変更は未commit・未push。

### 27.8 2026-10-13 Sic Transit 目視セッションと並行検証

- ユーザーの画面確認用に、MSVC Release版で`MP12 SIC TRANSIT`（表示名 Sic Transit）を固定した専用serverを起動。操作用Noxus clientは遅延なし、Sylux/Samus/Kandenの3 network AI clientは各RTT 250ms。専用serverは27888番port、`-nomaster`。session情報と停止対象PIDを`tools/build/out/msvc-Release/manual-network-sic-transit-20261013-094704/session.json`へ保存。起動後に操作slot 3の接続・Noxus identity、Bot clientからの通信をserver logで確認。最終process audit時点でserver、3 bots、操作用windowの5 PIDは全て生存し、目視結果はユーザー確認待ち。視覚症状の修正受入れをこの稼働確認だけでPASSとはしない。
- ユーザー操作中にMSVC Releaseの関連CTestを絞って実行し**15/15 PASS**（NetworkSmoothingTimeline、FormReconciliation、TimedEffectSync、FreezeTimeline、FreezePredictionLedger、server lobby/slot ownership、Protocol 30/31 golden等）。Python acceptance/parser testsは**12/12 PASS**。この確認中にbuildは再実行していない。
- 稼働serverの09:48:04 summaryではsimulation 3601 steps、0 overrun / dropped / stall、shot event 187 received / 187 fired / 0 never arrived。これはsession稼働と通信の観測値であり、画面描画の手動受入れではない。`-netcombatcheck "TEST ARENA" -noupdate`もcurrent MSVC Releaseでexit 0。GUI subsystemからassertionごとのConsole出力を保存できなかったため、このrunではcountを追加の証拠として主張しない。
- `-netcombatcheck`は引数の空白を保持したhidden `Start-Process`で再実行し、current MSVC Releaseの実server simulationで**98 assertions PASS / no simulation failures / exit 0**をstdoutまで保存した。証跡: `tools/build/out/msvc-Release/netcombatcheck-test-arena-20261013-0950.stdout.txt`。これはG4のauthority combat checkであり、client/authority全weapon/formのruntime matrixや目視確認を置き換えない。
- セッション出力: `tools/build/out/msvc-Release/manual-network-sic-transit-20261013-094704/`。手動目視の完了・停止はユーザー指示待ち。G4/G8/G9/remote CIの残りの受入れゲートも継続して未完了。変更は未commit・未push。

### 27.9 2026-10-13 Sic Transit shadow-only rendering

- ユーザー指示で27.8の手動セッションを終了。`session.json`のserver・BotPidsを照合して対象PIDだけ停止し、最後に全5プロセスの終了とUDP 27888の解放を確認した。
- 修正前の再現試験は`MP12 SIC TRANSIT`、60秒、可視Samus AI observer（RTT 0ms）+ headless Sylux/Kanden AI（各RTT 250ms）。3 clientとも`RESULT: PASS`、observerは3602 simulation frames。NetLogでpresentation positionから解決したNodeRefの`visible=false`遷移を346件確認し、位置解決失敗は11137件だった。これは実画面を撮るネットワークAI試験で、346件すべてが影として見えたという画像判定ではない。artifact: `tools/build/out/msvc-Release/network-shadow-sic-transit-20261013-095748/`。
- 原因はmap-part cullingと影描画の判定不一致。remote presentation位置から解決したNodeRefが現在の可視map partにない場合、本体は`IsVisible(NodeRef)`で描画を止める一方、従来の`PlayerEntity::Draw()`は同じ判定より前に`DrawShadow()`を無条件実行し、可視床へ投影した影だけが残り得た。wire stateやNetSmoothingの時計ではなく、`PlayerDraw`内で本体と影のculling判定を共有するよう修正した。位置解決不能時に本体を表示する既存fallbackは維持。
- 修正後に`tools\\build\\build-cpp.bat msvc Release`成功（27.3秒、`tools/build/out/msvc-Release`）。全CTest **98/98 PASS**（71.85秒）。
- 修正後の同条件35秒runでは3 client全て`RESULT: PASS`、visible observerは2102 simulation frames、remote position snaps 0。observerのNodeRef `visible=false`遷移は25件、位置解決失敗は5102件で、後者は既存fallbackにより本体を描画する。observerの実window captureを25枚保存し、代表画像を確認。session server/clientは全終了し、UDP 27888も解放済み。artifact: `tools/build/out/msvc-Release/network-shadow-sic-transit-fix-20261013-100419/`。キャプチャは自動AI視点のsampleであり、人間操作による目視受入れは未実施。
- 残りはG4全weapon/form・death/rejoin runtime matrix、G8残余fault条件/長時間/queue saturation、G9 GUI-only acceptance、remote CI。G10性能比較はユーザー指定で対象外。変更は未commit・未push。

### 27.10 2026-10-13 compound fault long-duration run

- 現行MSVC Releaseで`TEST ARENA`の180秒network runを実施。dedicated authorityは8 slot中server bot 6体（AI level 3）、client 2体（AI level 3）。両clientへ個別にRTT 250±40ms、5% loss、10% reorder、5% duplicateを適用し、seedは6011/6012。artifact: `tools/build/out/msvc-Release/srp-g8-longqueue-20261013-101017/`。
- `G8LongSamus`は10871 frames、`G8LongSylux`は10812 framesを処理し、どちらも`RESULT: PASS`。stderrはserver/clientすべて空。終了後、session記録のPID・実行path・command lineを照合して専用serverのみ停止し、FruityPrime process残留なし、UDP port 55197解放を確認。
- authorityのfinal summaryではshot event 401 received / 401 fired、stale/pushed out/abandoned/waiting/overdue/skipped/recovered late/never arrived/still awaitedはいずれも0。lag compensationは401 shots、mean 23.2 frames、worst 26、history misses 0、clamped 0。hit claim 454 received、affliction dropped 0、server stderr error 0。claims summaryのrefused 1、doubled without a grant 3は観測値として保持し、個別理由はこのrunでNetLogを有効化していないため未判定。これは複合fault下の一つの長時間runであり、残るfault matrixおよびqueue saturationの受入れを代替しない。
- G8の残りは未実施fault組合せの反復とqueue saturation。G4全weapon/form・death/rejoin runtime matrix、G9 GUI-only acceptance、remote CIも未完了。G10性能比較はユーザー指定で対象外。変更は未commit・未push。

### 27.11 2026-10-13 seven-hunter form/bomb runtime sweep

- 現行MSVC Releaseで`run-form-matrix.ps1`を7 hunter（Samus/Sylux/Kanden/Spire/Trace/Noxus/Weavel）、45秒、`FormBombCycle`（phase 3秒）で実行。SyluxだけRTT 250±40ms・loss 5%・reorder 10%・duplicate 5%。artifact: `tools/build/out/msvc-Release/srp-g4-form-matrix-seed-6121-20261013-101533/`。追加buildなし。
- 全7 clientは2700 phase framesを観測し、alt-form-totalは比較された全pairでreport checker許容内。bomb-laidも観測があったowner pairでは9/9または11/9で許容内。unmorphはこのphaseでは0で未試験。`compare-reports.py`は252 checks / 186 agreementで30 mismatchとなり、主に多client同時ローカル実行下のspawn/movement/shooting/Weavel half-turret差を報告したため、combat parity PASSとは数えない。
- 7 client全ての`-netcheck`はexit 1。各reportの失敗は`FMSa6121`が一度も被弾しないcombat-coverage条件で、別の構造エラーやstate-dropは示していない。これは負荷をかけた7 hunter sweepとして残すが、hit/death/weapon受入れを満たさずG4の全runtime matrixは未完了。G4残りは被弾/死亡を成立させるfocused scenario、全weapon/form・rejoin比較、GUIのform animation確認。

### 27.12 2026-10-13 `NetSession` / `DedicatedServer` responsibility re-audit

- 変更後の実装を再読した。`NetSession::Update` / `PumpMapTransfer`は必要なstate ownerとcallbackをPortsに束ねて`SessionRuntimePump`へ渡し、packet entryは`SessionPacketDispatcher`へ委譲する。Lobby、admission, snapshot, intent, match transition、peer cleanupも個別ownerへdelegationするため、`NetSession`はcompatibility facade/composition pointのまま。
- `DedicatedServer::Run`はsocket listen/shutdownと初期wiringを担い、周期順序は`ServerRuntimePump`が所有する。`DedicatedServer::Handle*`はpacket serviceへforwardするthin adapter。`Server`以下から`NetSession::` static API参照は0件。既存のexplicit-port wiring、private mutable state、focused CTestsを確認し、追加のcoordinator責務分離は今回不要と判断した。優先事項1（SRPの残る協調責務分離）は完了。追加source変更・buildはなし。

### 27.13 2026-10-13 Sic Transit human check: bot disappearance regression

- 新しいSic Transit目視sessionは`MP12 SIC TRANSIT`、human Noxus、Sylux/Samus/Kanden network bots（各RTT 250ms）、server UDP 27888で起動。session metadata: `tools/build/out/msvc-Release/manual-network-sic-transit-20261013-102019/session.json`。process auditではserver PID 45108、bot PIDs 48548/52744/37172、human PID 50408が実行中で、port 27888はserverがbindしていた。
- human testで「botが影もなく消える」と報告されたため、`PlayerDraw::Draw`のbodyとshadowを共通`mapPartVisible`で抑制する変更をrevert。shadowの無条件描画とbodyの元の可視判定へ戻した。remote display positionからNodeRefを再解決する`PlayerEntityNetCulling`側の変更は保持。
- `NETWORK-SMOOTHING.md`から「body/shadow共有gateでshadow-onlyを防いだ」という現状と異なる記述を除去し、現象の再受入れが必要と記録した。user session中の実行ファイルはrevert前buildをロードしていた。ユーザー指示によりserver・3 bot・humanの5 processを停止し、終了後に残processなしとUDP 27888解放を確認した。revert sourceを使うbuild/runtime確認は未実施。
- 追加の非ネットワーク診断として、revert前の既存MSVC Release成果物で`-maptest "MP12 SIC TRANSIT" -players 8 -seconds 22`を実行。出力はnode lookup 4632/18208 agreed・76 wrong part・would hide 0、remote node 16620/18200 agreed・576 wrong part・would hide 0だった。これは人間/ネットワークbotの消失を再現しておらず、終了コードも取得できていないためPASSや原因特定の根拠にしない。試験後のFruityPrime process残留なし、UDP 27888解放を確認。
- Sic Transit shadow/body visibility修正は受け入れ未達として差し戻し。次は位置由来NodeRefが見えないroom partを選ぶ場合を調査し、network presentation用のculling責務を再設計してから、build後にvisible human/bot確認を行う。影だけ表示される/本体ごと消える状態は解決扱いにしない。
