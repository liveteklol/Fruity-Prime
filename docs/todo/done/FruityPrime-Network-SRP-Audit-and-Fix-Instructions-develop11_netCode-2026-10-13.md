# Fruity-Prime C++ Network SRPリファクタリング監査・修正指示書

**監査日:** 2026-10-13（JST）\
**対象ブランチ:** `develop11_netCode`\
**固定HEAD:** [`63b41be693a36ce059ada4e40e068a767d8f3b79`](https://github.com/Zection6V/Fruity-Prime/commit/63b41be693a36ce059ada4e40e068a767d8f3b79)\
**対象:** `src/MphRead.Native/Mods/Network/`、呼び出し元の `Entities/Players/PlayerDraw.cpp`、テスト、CMake、既存計画書\
**通信:** `NetConfig::ProtocolVersion = 31`（`Protocol/Packets/PacketTypes.hpp:61`）\
**実施状態:** GitHub上のソース静的監査。ローカルでのコード変更・コミット・push・ビルド・ゲーム起動なし。\
**確度:** 実装から論理的に証明できる挙動と、ログ・報告からの仮説を明確に区別する。

---

## 0. 結論

**階層化・主要ファサードのSRP分割は相当進んでいる。元の計画を丸ごとやり直さない。**

- 固定HEADの `Network` 以下は **422ファイル（197 `.cpp`、225 `.hpp`）**、**34個のファイル配置ディレクトリ**。トップレベルに `Bots/`、`Diagnostics/`、`Discovery/`、`Domain/`、`Gameplay/`、`Integration/`、`Protocol/`、`Replay/`、`Server/`、`Session/`、`Testing/`、`Transport/`、`UI/` が存在する。
- `NetSession.cpp` は **408行**、`DedicatedServer.cpp` は **493行**。旧HEAD `9bd9cb7a` のそれぞれ **2,150行、1,811行**から大幅縮小。`NetSession` は `SessionContext` と各Serviceのファサード、`DedicatedServer` はComposition Rootと薄いForwarderとして再編されている。
- `Protocol/Packets/`と`Protocol/Codec/`、`Gameplay/Claims/Hit/`と`Pickup/`、`Gameplay/Replication/`、`Gameplay/StatusEffects/`、`Testing/`への分離は概ね計画に沿っている。クラス名を変えるだけの不要な追加分割は推奨しない。
- ただし **描画のBot消失の未解決、欠落スナップショット区間の整数時刻における補間不整合、受信キューの容量制御・重要パケット保全**を確認した。受入れ用runtime matrixのFAIL/未実施も残る。SRP改善を「機能回帰なしで完了」と判定してはいけない。
- 本書は**実装済み成果を保持し、未解決箇所を小さい修正単位でcloseするための指示**である。

### 判定表

| ID | 優先度 | 監査判定 | 要点 |
|---|---|---|---|
| NET-A01 | **P0 / BLOCKER** | 実機症状は再現報告済み、根因は要追加実証 | Sic Transitで遠隔Botの本体が完全に消える / 影だけ残る問題。暫定変更はrevertされ、解消の証拠なし |
| NET-A02 | **P1 / コード上の不整合を確認** | 再現用テスト未追加 | `NetSmoothing::Sample`は欠落区間内の整数read frameでも`fraction==0`を理由に補間しない |
| NET-A03 | **P1 / コード上の境界漏れを確認** | queue saturation test未実施 | `PromoteHeldArrivals`はInbox上限をチェックせず投入できる |
| NET-A04 | **P1 / コード上の選択規則を確認** | 条件付きで重要パケットが落ちる | `ShedIndex`は全要素が重要パケットでもindex 0を返す。新着が置換可能でも既存の重要パケットを破棄する場合がある |
| NET-A05 | **P1 / 受入れ未達** | 計画書に実測FAILあり | G4通常tour・7 hunter sweepのcross-client parity / combat coverage |
| NET-A06 | **P2 / 受入れ未完** | テスト履歴上未完了 | G8残余fault・queue saturation、G9 GUI、未完のremote CIの完了判定 |
| NET-A07 | **P2 / 設計監査** | 追加の大規模SRP分割は不要 | 残った大きいファイルの行数だけを理由に再分割しない。依存方向と状態ownerを継続チェック |

---

## 1. 監査ベースラインと採用禁止事項

### 1.1 固定ソース

- `develop11_netCode` HEADは `63b41be693a36ce059ada4e40e068a767d8f3b79`。
- `PacketTypes.hpp` は **Protocol 31**。従来計画書の「Protocol 30固定」は現行HEADにそのまま適用しない。現行wire formatを保ち、Protocol 30 goldenを過去互換の比較証跡として検証する。
- CI実行 [`build_cpp #38334772669`](https://github.com/Zection6V/Fruity-Prime/actions/runs/38334772669) は今回の確認時点で **in_progress**。Linux、Windows、macOS、Androidのビルド・テスト結果はこの時点では未確定。CI結果をPASS/FAILと断定しない。過去のローカルMSVC Release / CTest 98/98 PASSは**現在の固定HEADのCI結果の代用品ではない**。
- 既存の実装・runtime記録は [`docs/todo/FruityPrime-Network-SRP-Refactoring-Plan-develop11_netCode-2026-10-11.md`](https://github.com/Zection6V/Fruity-Prime/blob/63b41be693a36ce059ada4e40e068a767d8f3b79/docs/todo/FruityPrime-Network-SRP-Refactoring-Plan-develop11_netCode-2026-10-11.md)。特に **27.7～27.13** を事実のベースラインとする。

### 1.2 改修ルール

1. **プロトコル番号・パケットサイズ・順序・エンディアン・イベント冪等性・authority基準を変えない**。必要に見える変更が出てもまず別タスクとして隔離する。
2. オリジナルゲームの `Formats/` と `Entities/` に機能ロジックを追加しない。`PlayerDraw.cpp` との既存描画境界に最小限の変更が必要な場合は、理由・スコープを別途記録する。
3. `*.hpp` と対応する `*.cpp` は同階層。`include/`専用フォルダを作らない。C++20、MSVC/GCC/Clang/Android NDKの全対象。
4. pure functionと値型のテストを先に増やし、反復実装でbehavior driftを防止する。不要なヒープ確保、lock、per-frame `std::function`、余計なパケットコピーは導入しない。
5. 修正単位ごとに**回帰テスト、実プロセス試験、GUI可視結果**を分けて報告する。テストを走らせただけでacceptanceとしない。

---

## 2. NET-A01: Sic TransitのBot消失・shadow-only（P0）

### 2.1 証拠

**実行記録:** 計画書27.9～27.13。Sic Transitの人間操作セッションで「botが影もなく消える」と報告され、`PlayerDraw::Draw`の本体・影の共通可視gateはrevertされた。**revert後のbuild/runtime/人間目視検証は未実施**と記録されている。したがって問題はCLOSEDにしない。

**コードで確認した条件:**

- [`Gameplay/Player/PlayerEntityNetCulling.cpp:137-195`](https://github.com/Zection6V/Fruity-Prime/blob/63b41be693a36ce059ada4e40e068a767d8f3b79/src/MphRead.Native/Mods/Network/Gameplay/Player/PlayerEntityNetCulling.cpp#L137-L195) `PlayerEntity::ModResolveNetworkNodeRef()`:
  - display positionから`scene.GetNodeRefByPosition(...)`を探索。
  - **有効なNodeRefであれば `scene.IsNodeRefVisible(found)` がfalseでも `NodeRef=found`、`_modNodeUnresolved=false`とする**。
  - `visible=false`はNetLog出力されるが、選択の可否に使われていない。
- [`Gameplay/Player/NetPlayerBridge.cpp:981-994`](https://github.com/Zection6V/Fruity-Prime/blob/63b41be693a36ce059ada4e40e068a767d8f3b79/src/MphRead.Native/Mods/Network/Gameplay/Player/NetPlayerBridge.cpp#L981-L994): `Move(..., true)` で上記解決を実行。
- [`Entities/Players/PlayerDraw.cpp:186-227`](https://github.com/Zection6V/Fruity-Prime/blob/63b41be693a36ce059ada4e40e068a767d8f3b79/src/MphRead.Native/Entities/Players/PlayerDraw.cpp#L186-L227): `DrawShadow()`は可視チェック前、**本体描画は `IsMainPlayer() || IsVisible(NodeRef) || ModNodeUnresolved()` の場合のみ**。

この組合せによって、`GetNodeRefByPosition()`が**隠れたpartを返し**、`_modNodeUnresolved=false`のときに遠隔Bot本体が抑制されるという**実行可能なコード経路**が存在する。ただし、報告されたBot消失の全事例がこの経路によるものと**立証したわけではない**。Spectating/HideModel/モデルLOD/room part状態も同時に計測すること。

### 2.2 修正指示（順序厳守）

1. **最初に観測機構を追加。** 一時診断`[net-culling]`として、slot、life/generation、`Position`、presentation frame、old/new NodeRef、`IsNodeRefVisible`、`_modNodeUnresolved`、`IsMainPlayer`、`HideModel`、`Spectating`、`DrawShadow called`、`body gated`を同フレームに採る。ログは状態遷移時のみ出力、毎フレーム文字列構築しない。
2. **責務を分ける。** `PlayerEntityNetCulling`がNodeRef候補の解決と信頼性判定、描画側が本体/影の可視条件を担当する。単一の `visible=false` を返した候補だけで「正しい不可視」と断定しない。room part/portal候補と位置・volumeについて検証する。
3. 位置探索の候補が見つかった場合、`visible`、現在のportal context、直前nodeとの連続性を評価した上で採否を決める。**不可視候補しかない場合の明示的なunknown/fallback policy**を実装する。`_modNodeUnresolved=false`に無条件に落とさない。
4. **影だけ残る・本体ごと消えるの両方を監視**し、影の有無だけを根拠に本体を強制表示／強制非表示する一律gateを再投入しない。合法的な遮蔽物・portal cullingまで無効化する変更は禁止。
5. `GetNodeRefByPosition`の検索に伴う負荷を計測。既存の毎フレーム4候補探索を不用意に倍増しない。

### 2.3 再現・完了ゲート

- 同じbuild・同じSic Transit・同じ視点で、**人間観測 client + server + 3 network bots（RTT 250ms）**。Botごとの姿・影・姿勢遷移を録画または連番画像で確認。
- 同時にローカルのAI observerとheadless netcheckのログを採る。`netcheck RESULT: PASS`は**描画可視性のPASSではない**ことを明記。
- portal越え、Map part切替、Morph/Unmorph、spectating、respawn、map rotation後の再joinを確認。
- **完成条件:** 「body無しshadow有り」「body無しshadow無し」の不当な消失0、合法的なcullingは維持、静的＋描画目視の再現性を証拠として保存。

---

## 3. NET-A02: NetSmoothingのsparse frame補間条件（P1）

### 3.1 確認したロジック不整合

[`Gameplay/Replication/NetSmoothing.cpp:291-355`](https://github.com/Zection6V/Fruity-Prime/blob/63b41be693a36ce059ada4e40e068a767d8f3b79/src/MphRead.Native/Mods/Network/Gameplay/Replication/NetSmoothing.cpp#L291-L355):

- `FindSnapshotBracket(lower, bracket)`は`lower=floor(_readFrame)`に対して、**`BeforeFrame < lower < AfterFrame`という欠落区間**を返しうる。
- 補間係数は `elapsed = (lower - BeforeFrame) + fraction`、`amount = elapsed / span` と適切に計算している。
- しかし分岐が **`if (fraction <= 0.0001 || span == 0 || altA != altB)`** なので、欠落区間の内部が整数frame (`fraction=0`) のときに `amount` が0より大きくても `a`を返す。
- 同一時刻の `SampleForm` は `amount <= 0.0001`を判定するため、**form progressとworld positionが異なる時刻の状態を示し得る**。

[`NetSmoothing.cpp:401-493`](https://github.com/Zection6V/Fruity-Prime/blob/63b41be693a36ce059ada4e40e068a767d8f3b79/src/MphRead.Native/Mods/Network/Gameplay/Replication/NetSmoothing.cpp#L401-L493) `SampleTrackedPosition()`も `fraction <= 0.0001`で早期returnし、同じ問題を持つ。

**反例（純粋な算術で再現可能）:**

| before frame | after frame | read frame | before position | after position | 期待position | 現行position |
|---:|---:|---:|---:|---:|---:|---:|
| 100 | 103 | **101.0** | X=0 | X=3 | **X=1** | **X=0** |
| 100 | 103 | **102.0** | X=0 | X=3 | **X=2** | **X=0** |
| 100 | 103 | 101.5 | X=0 | X=3 | X=1.5 | X=1.5 |

テスト [`src/MphRead.Native/Testing/TestNetSmoothing.cpp:152-177`](https://github.com/Zection6V/Fruity-Prime/blob/63b41be693a36ce059ada4e40e068a767d8f3b79/src/MphRead.Native/Testing/TestNetSmoothing.cpp#L152-L177) はsparse補間を検査しているが、その期待値は**約1.14（非整数時刻）**であり、この整数時刻の境界を固定していない。

### 3.2 最小修正指示

1. `Sample()`は `fraction <= 0.0001`での早期保持を廃止し、**`amount <= epsilon`**（＝時刻が真にBeforeFrame付近である場合）に変更する。`span == 0`、life mismatch、form切替、teleport境界は従来通り保持する。
2. `SampleTrackedPosition()`も同様に**BeforeFrameからの絶対経過に基づいて**判定する。今の判定が `b` 検索より前にあるので、適切な境界ガードの後ろへ移す。
3. 計算の共有が妥当なら、`ReadFrameInterpolationFraction(before,after,read)`のような`constexpr`/純粋関数を導入する。ただし関数導入を目的化せず、ホットパスへの新規割当・virtual dispatchを増やさない。
4. `TestNetSmoothing.cpp`に **100,103にのみ記録された状態で readFrame=100,101,102,103** を通すケースを追加。`Sample`と`SampleForm`と`SampleTrackedPosition`を同一クロックで照合する。内部read frameをテスト用fixtureで固定、または公開動作だけで確定的に到達させる。
5. `altA != altB`、生命generation/life切替、projectile `present=false`、teleport `SnapDistance`超えについても既存保持動作が変わらないテストを追加。

**合格基準:** 線形サンプルは整数/非整数の欠落区間で同じ時間軸に乗り、不連続イベントは補間しない。`NetworkSmoothingTimeline`の既存チェックが全PASS。

---

## 4. NET-A03/A04: NetTransportの受信キュー飽和（P1）

### 4.1 容量制御の一貫性がない

[`Transport/NetTransport.cpp:125-185`](https://github.com/Zection6V/Fruity-Prime/blob/63b41be693a36ce059ada4e40e068a767d8f3b79/src/MphRead.Native/Mods/Network/Transport/NetTransport.cpp#L125-L185):

- `EnqueueInbox` は `InboxCount.fetch_add(1)` して `Inbox.push_back`する**無条件投入**。
- `PromoteHeldArrivals`は `HeldInput.TryDequeue`を繰り返して **無条件に `EnqueueInbox`**。通常受信のような `MaxQueuedPackets` チェックを行わない。
- 通常受信は [`NetTransport.cpp:309-327`](https://github.com/Zection6V/Fruity-Prime/blob/63b41be693a36ce059ada4e40e068a767d8f3b79/src/MphRead.Native/Mods/Network/Transport/NetTransport.cpp#L309-L327)で **投入前に上限チェック**するが、その後`HeldInput`へ入れるだけの場合、後のpromote時点で容量状況が変化している可能性がある。
- [`NetTransport.cpp:382-389`](https://github.com/Zection6V/Fruity-Prime/blob/63b41be693a36ce059ada4e40e068a767d8f3b79/src/MphRead.Native/Mods/Network/Transport/NetTransport.cpp#L382-L389): `Drain`のiterator開始時に一括 `PromoteHeldArrivals`。
- **結果:** `MaxQueuedPackets=2048` は遅延投入のInboxに対する強制不変条件ではない。`HeldInput`自体の別容量があっても、Inboxに移された段階の超過を防がない。

### 4.2 置換可能パケットで重要パケットを捨てうる

[`Transport/InboxShedding.hpp:11-46`](https://github.com/Zection6V/Fruity-Prime/blob/63b41be693a36ce059ada4e40e068a767d8f3b79/src/MphRead.Native/Mods/Network/Transport/InboxShedding.hpp#L11-L46):

- `Superseded`はIntent/SlotIntent/Snapshot等を置換可能と分類。これは妥当な方向。
- `ShedIndex`は置換可能な既存パケットが**0件の場合でも `return 0`**する。
- 受信側は**新着パケットの種類を判定する前に** `TryShedInbox()`し、新着を投入する。つまり**「重要イベントで満杯の受信キューに新しいPingが来た」ときでも古い重要イベントを破棄してPingを追加し得る**。この逆転は現行保護ポリシーに反する。

重要な注意: 有限容量では全受信を必ず保存することは不可能であり、UDPの再配送保証をここだけで実現することはできない。重要パケットの消失時に必要なアプリケーション側再送/ACKは**別途**保証する。

### 4.3 修正指示（SRP・低オーバーヘッド）

1. **単一のcapacity-aware enqueue**（例 `State::TryEnqueueInbox(ReceivedPacket)`）を定義し、**通常受信、held promotion、demo playback**の全投入経路に適用。`InboxLock`下で `Inbox.size()` を真のサイズとして扱い、victim選択・削除・push・カウンタ更新を一つのクリティカルセクションにする。
2. 新着も含めて優先度を判断。満杯時:
   - 既存に置換可能なパケットがあれば、同じstream/slotの古いものを優先（実装に必要な解析が重い場合は最初は種類単位の古いものを選択）。
   - 既存が全て重要で、新着が置換可能なら**新着を棄却**し、重要イベントを守る。
   - 既存も新着も重要なら**明示されたoverflow policy**で処理。重要イベントを無限保持しようとしない。削除とメトリクスを必ず一致させる。
3. `ShedIndex`の戻り値を `std::optional<size_t>`などにして「置換可能な既存要素が0」の状態を表現し、無条件0を廃止。drop原因は `incoming replaced` / `existing superseded` / `critical overflow`で区別する。
4. `InboxCount`をlock内でサイズと一致させるか、lock-free fast readが必要なら外部読み取り専用キャッシュとして最終判定はmutex保護のsizeとする。`TotalPacketsDropped`などの既存公開値の意味を維持し、重複加算を避ける。
5. **held scheduler自体の容量**とInboxの容量を別々にメトリクス化。`NetLag`の意図的loss/dropと、本番受信queueのoverflowを別集計する。
6. 順序保持が必要なイベントと、最新だけが必要なstreamで公平性・レイテンシのバランスを検証。毎パケットheap allocationや余分なcopyは追加しない。

### 4.4 最低限のテスト

| テスト | 期待値 |
|---|---|
| Inbox容量2048、2048件重要 + 置換可能Ping着信 | 重要は残り新規Pingを棄却 |
| Inbox容量2048、置換可能 + Welcome/Verdict着信 | 古い置換可能パケットを退避、重要を保存 |
| HeldInputから2048件一括promotion、Inboxに100件既存 | Inboxは必ず2048件以下、計数とdrop原因が一致 |
| 通常受信とheld promotionが競合 | `Inbox.size() == InboxCount`、順序・iterator安全性維持 |
| 5% loss/10% reorder/5% duplicate + queue saturation | 生存/authority/claim verdictの到達性・再送/ACKを実証 |
| reconnect/roster/map change/full queue | 重要パケットに起因する復帰不能、永久pending、二重grantなし |

---

## 5. NET-A05: gameplay parity / runtime受入れ（P1）

現行計画書の記録から、**テストを実施したことと合格したことを分ける**。

### 5.1 確認できる実測値

- **27.4**: seed 2192、faulted 60秒tourはクライアントexit 0でも、pair比較 **FAIL 3項目**（movement / spawn / damage-taken）。
- **27.7**: seed 4782の3 client form matrixはクライアントexit 0でも、pair比較 **FAIL 5項目**（spawn、damage-taken、shot abandoned）。
- **27.10**: seed 6011/6012、8 slot・6 server bots + 2 client、180秒複合faultは両client `RESULT: PASS`。shot 401 received/401 fired等は良好。ただし**queue saturationや残余fault matrixではない**。
- **27.11**: seed 6121、7 hunter sweepの `compare-reports.py` は **252 checks / 186 agreement / 30 mismatch**。**全7 clientがexit 1**で、少なくともcombat被弾coverage不足。unmorph観測0のためunmorph合格判定不可。
- **27.13**: Sic Transit human runで遠隔Bot消失報告。反映前buildでの実行だったため、その後revertしたコードの受入れ証拠ではない。

### 5.2 修正指示・再実行順

1. `TEST ARENA`でまず1対1、0ms・無lossのcontrolを確立。片側の入力/authority snapshot/remote renderingを同一world frameで比較。
2. **Kanden→Spire movement**、spawn/timing、damage-taken不一致をそれぞれ分離。接触/被弾の観測窓、life/generation、authority epoch、snapshot delayを統一し、**計測の定義の違いとゲーム状態の不一致を別扱い**する。
3. 7 hunterケースは被弾・死亡・リスポーンを発生させる決定的なfixtureでcoverし、no-hitのまま `-netcheck`をPASSと解釈しない。
4. all weapons × biped/alt × morph/unmorph × rejoin/deathの必要組合せを固定表にし、failed/missingを別列にする。現行のshot ledger/claim、pickup grant、freeze/burn/disruptは独立して一致を確認する。
5. 高負荷でのみ出るmismatchはauthority simulationの遅延、ローカルCPU contention、受信queue深度を記録した上で再比較する。`movement travel`の違いが即ゲーム不具合とは限らないため、必ずstate/source別に立証する。
6. 項目の定義、閾値、failureの原dataをartifactへ保存。コード変更したら同じseedとcontrolを同時に走らせる。

**完了条件:** G4で事前定義した組合せをすべて実行し、説明不能なmismatch 0、必要なcombat coverageを満たし、未評価項目をPASSへ換算しない。

---

## 6. NET-A06: CI / GUI / fault / 品質ゲート（P2）

### 6.1 分離して追跡するゲート

| ゲート | 監査時点 | 完了条件 |
|---|---|---|
| G1 CMake / source discovery | `GLOB_RECURSE CONFIGURE_DEPENDS`による再配置検出を確認 | Windows/GCC/Clang/Android NDKのリンクが全てPASS |
| G2 protocol golden | `NetworkProtocol31Golden`と`NetworkProtocol30GoldenPreserved`をCTestへ登録済み | 現行31のbyte/sizeと旧30 golden両方PASS、意図せぬwire変更なし |
| G3 ユニット | ローカル履歴でCTest 98/98 PASS | 固定HEADのbuildで再実行し、追加のsmoothing/queue/cullingテストを含め全PASS |
| G4 combat parity | FAIL・coverage未達が残る | §5のmatrix全項目を結果別に判定 |
| G8 fault | 一部180秒複合fault PASS | 残余fault matrix + **queue saturation** + reconnect・claim delivery成功 |
| G9 GUI | human visible acceptance不足 | Sic Transitで本体/影/フォーム/ボム/AltAttackを録画等で確認 |
| G10 performance | 元計画で**対象外**指定 | 今回は受入れゲート化しない。ただしホットパスの計算量/割当数増分は目視監査 |
| Remote CI | [`#38334772669`](https://github.com/Zection6V/Fruity-Prime/actions/runs/38334772669)を監査時確認中 | 実行がterminalになった後に**各jobの正確な結果**を記録。ローカルPASSと混同しない |

### 6.2 Test/Buildの順番

1. 依存ヘッダとフォーマット静的確認 → compile全プラットフォーム → protocol golden。
2. `FruityPrime.NetworkSmoothingTimeline` と新しいTransport容量・sheddingテスト（欠けていれば追加）。
3. CTest全件、`-netlobbytest`、`-netcombatcheck`、PowerUp Simulation、Server Simulation。
4. 低遅延control → faulted 1v1 → 3 clients → 7 hunters → 8 slots → queue saturation / rejoin。
5. Sic Transit human GUIを**最後だけでなくP0修正直後にも**走らせる。
6. 変更後にartifact、使用SHA、build種、結果、比較表、実験PID cleanupを保存する。

---

## 7. SRP・構造監査：どこは完了、どこは保持するか

| 領域 | コード上の状態 | 判定・指示 |
|---|---|---|
| `Protocol/Packets`, `Protocol/Codec` | 旧`NetProtocol`一枚岩をPacket型/codecへ分割。`NetProtocol.cpp`は4行 | **維持**。codecのserialization byte goldenを最優先 |
| `Session` | `SessionContext` owner、`SessionRuntimePump`、`SessionPacketDispatcher`等へ分離。`NetSession.cpp`408行 | **維持**。旧API facadeからの参照を無理に全廃しない |
| `Server` | `DedicatedServer`493行、server runtime・lobby・match・channelsが独立 | **維持**。現時点で更なる大規模coordinator抽出不要 |
| `Gameplay/Claims` | Hit outbox/ledger/validation/rate、Pickup outbox/replayなど抽出済み | **維持**。claim裁定結果・冪等性をfixtureで保証 |
| `Gameplay/Replication` | form, snapshot capture, smoothingが分離済み | **NET-A02**を修正し、同一presentation clockを保持 |
| `Gameplay/Player` | `NetPlayerBridge`999行、input/history/confirmation/form分割あり | **P0 cullingのみ重点修正**。行数だけで更に砕かない |
| `Gameplay/StatusEffects` | freeze/burn/disruptのpolicyとsyncが分離済み | 変更不要。snapshot-age計算とrejoin testは維持 |
| `Transport` | UDP・fault・shedding分離済み | **NET-A03/A04**。境界管理を一本化 |
| `Testing` | Network用test群をサブフォルダ化。ただし `NetFeatureCheck.cpp` 約83KB | **テスト用であるため行数だけではP0にしない**。今後修正する機能に合わせたfixture抽出 |

**追加推奨:** `NetSmoothing`や`NetTransport`のpure invariantsをテスト側の専用fixtureに切り、内部private/static stateを直接書き換える例外を無制限に広げない。Diagnostic用テレメトリはRelease常時の出力ではなくopt-inとする。

---

## 8. 推奨修正PR分割（既存コードの大規模再配置は繰り返さない）

| 順序 | 作業内容 | 対象 | Done |
|---|---|---|---|
| **FIX-01** | Sic Transit cullingの原因をログで立証して最小修正 | `PlayerEntityNetCulling.cpp` / 必要なら既存`PlayerDraw.cpp`接合部 | GUI human 1+3 botsで本体/影が正常、合法的なportal culling保持 |
| **FIX-02** | sparse補間の整数時刻誤判定を修正 | `NetSmoothing.cpp` / `TestNetSmoothing.cpp` | 100→103の欠落区間、101.0/102.0およびForm/Tracked同期PASS |
| **FIX-03** | Inboxの単一bounded enqueue・critical保護 | `NetTransport.cpp` / `InboxShedding.hpp` / テスト | 2048上限、critical保全、受信・held・playbackの全経路PASS |
| **FIX-04** | G4 controlとmismatch原因を修正 | combat/replication/harnessの**確認した箇所のみ** | 全指定matrix coverage + parity PASS |
| **FIX-05** | G8/G9/CI受入れ | fault scripts / GUI evidence / CI | 未評価なしの結果表、failing conditionの根拠保存 |
| **FINAL-AUDIT** | 依存方向・ホットパス・wire互換を再確認 | Network全体 | 重大回帰0、静的/動的双方のエビデンス確定 |

原則として各FIXは**異なる原因を混ぜず**、症状、根因、修正、テストを一対一に紐づける。FIX-01はP0だが、調査ログの追加が先であり、確証なしに単純な`DrawShadow` gateを戻さない。

---

## 9. 実装完了報告テンプレート

各FIXで以下を必ず報告する。

```text
Fix ID:
Base SHA:
Result SHA:
Modified files:
Reproduction before fix:
Confirmed root cause (not hypothesis):
Behavioral delta (must be strictly scoped):
Protocol version / wire golden before and after:
Unit/CTest (exact command + count + exit code):
Runtime fault setup (map, slots, RTT, loss, jitter, reorder, duplicate, seed, duration):
Authority/client logs and artifact paths:
GUI proof (if rendering issue):
Remaining unverified points:
Commit/push status:
```

---

## 10. ソース参照（固定SHA）

- [`Network`全体](https://github.com/Zection6V/Fruity-Prime/tree/63b41be693a36ce059ada4e40e068a767d8f3b79/src/MphRead.Native/Mods/Network)
- [`Gameplay/Replication/NetSmoothing.cpp`](https://github.com/Zection6V/Fruity-Prime/blob/63b41be693a36ce059ada4e40e068a767d8f3b79/src/MphRead.Native/Mods/Network/Gameplay/Replication/NetSmoothing.cpp)
- [`Testing/TestNetSmoothing.cpp`](https://github.com/Zection6V/Fruity-Prime/blob/63b41be693a36ce059ada4e40e068a767d8f3b79/src/MphRead.Native/Testing/TestNetSmoothing.cpp)
- [`Gameplay/Player/PlayerEntityNetCulling.cpp`](https://github.com/Zection6V/Fruity-Prime/blob/63b41be693a36ce059ada4e40e068a767d8f3b79/src/MphRead.Native/Mods/Network/Gameplay/Player/PlayerEntityNetCulling.cpp)
- [`Entities/Players/PlayerDraw.cpp`](https://github.com/Zection6V/Fruity-Prime/blob/63b41be693a36ce059ada4e40e068a767d8f3b79/src/MphRead.Native/Entities/Players/PlayerDraw.cpp)
- [`Transport/NetTransport.cpp`](https://github.com/Zection6V/Fruity-Prime/blob/63b41be693a36ce059ada4e40e068a767d8f3b79/src/MphRead.Native/Mods/Network/Transport/NetTransport.cpp)
- [`Transport/InboxShedding.hpp`](https://github.com/Zection6V/Fruity-Prime/blob/63b41be693a36ce059ada4e40e068a767d8f3b79/src/MphRead.Native/Mods/Network/Transport/InboxShedding.hpp)
- [`Protocol/Packets/PacketTypes.hpp`](https://github.com/Zection6V/Fruity-Prime/blob/63b41be693a36ce059ada4e40e068a767d8f3b79/src/MphRead.Native/Mods/Network/Protocol/Packets/PacketTypes.hpp)
- [`既存計画書・実施記録`](https://github.com/Zection6V/Fruity-Prime/blob/63b41be693a36ce059ada4e40e068a767d8f3b79/docs/todo/FruityPrime-Network-SRP-Refactoring-Plan-develop11_netCode-2026-10-11.md)
- [`HEADのビルドCI`](https://github.com/Zection6V/Fruity-Prime/actions/runs/38334772669)

**最終監査判定:** **SRP構造改善は達成が進んでいるが、リファクタリングの機能受入れは未完了（BLOCKED）。** P0 cullingの再現原因の立証と修正、P1 sparse interpolation/queueの修正・回帰テスト、G4/G8/G9/CIの完了をもってcloseする。**現時点ではfix実施・ビルド成功・runtime再現解消は主張しない。**
