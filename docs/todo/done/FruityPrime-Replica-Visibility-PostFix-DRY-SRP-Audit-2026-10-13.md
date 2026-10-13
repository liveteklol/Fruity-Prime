# Fruity-Prime C++ — リモートプレイヤー「影だけ」修正後の監査（DRY / SRP / KISS）

- 監査日：2026-10-13（JST）
- 対象：[develop11_netCode](https://github.com/Zection6V/Fruity-Prime/tree/develop11_netCode)
- 監査HEAD：[`2784d0025b71c02a789dfca4456da0e7cc2880f1`](https://github.com/Zection6V/Fruity-Prime/commit/2784d0025b71c02a789dfca4456da0e7cc2880f1)（ドキュメント追加）
- 表示修正コミット：[`2c65aea55db12a94834abfafdccc65caca167ffc`](https://github.com/Zection6V/Fruity-Prime/commit/2c65aea55db12a94834abfafdccc65caca167ffc)
- 比較基点：[`c01aabc77af6a03646f83b9298130aba52212444`](https://github.com/Zection6V/Fruity-Prime/commit/c01aabc77af6a03646f83b9298130aba52212444)
- 方法：GitHubにあるコミット差分と現行C++ソースの静的監査。ローカル実行・自動テスト実行・計測は実施していない。
- 利用者による実機・目視情報：「影だけになる問題は解消」「いい感じ」。**この成功実績を前提として、正常動作を不用意に壊す修正は指示しない。**
- 制約：原則`Mods/Network`内、共通の`EntityBase::IsVisible()`、`DrawShadow()`、通常BOT・敵・アイテム・既存Renderer/Room本体は変更しない。コミット・pushしない。

## 1. 結論

**評価：症状修正は受容・維持。ただしDRYには明確な重複、SRP/設計には責務の混同が残る。加えて、Portal境界でない不可視Partにいるプレイヤーまで描画候補にする、可視性の過剰救済リスクがある。**

現行実装は「Portal通過直後だけ旧可視Partからの描画を許可」ではなく、「**影として地面投影できるオンラインリモートプレイヤーなら、その本体のPartカリングを回避可能**」である。影を描く条件とキャラクターの可視性は同じ概念ではない。壁がPortalカリングによって描画対象から除かれれば、Depth Testだけでは壁越し描画を止められない場合がある。

このリスクは**ソース上の到達可能経路**として確認した。報告された実機環境で壁越し表示が実際に発生したことを確認したわけではない。したがって直ちにロールバック・広範なリファクタリングを行うのではなく、現行の成功を保ちつつ、再現可能な回帰ケースを先に固定する。

## 2. コミットの評価

| 変更 | 判断 | 根拠 |
|---|---|---|
| `Mods/Network/Gameplay/Player/ReplicaShadowVisibility.*` 新設 | △ 条件付き改善 | 可視性の応急救済として動作するが、Shadowへの依存が過剰 |
| `PlayerEntityNetCulling.cpp` の`ModNodeUnresolved()`拡張 | △ SRP/命名混同 | 「NodeRef未解決」と「影があるから本体表示」が同じ真偽値で返る |
| `PlayerEntityNetCulling.hpp` にネットワーク専用キャッシュを保持 | ○ スコープ適切 | 新機能状態をネットワーク側フックに配置、コアの実装追加なし |
| `NetPlayerBridge::BeginRemoteLife()`で`Move(..., true)` | △ 回帰対象 | ネットワーク初期Spawnの位置検索を厳密化。ただし**ローカルスロットにも**適用される |
| `PlayerDraw.cpp` / `DrawShadow()` を未変更 | ◎ 契約遵守 | 既存の影描画を維持 |
| `RoomEntity.cpp` / `Renderer.cpp` / `EntityBase::IsVisible()` を未変更 | ◎ 契約遵守 | 既存の世界カリングに手を加えない |
| 既存`TestReplicaRoomPlacement.cpp`拡張 | △ 部分的 | `ShouldForceBody`の真偽テストはあるがPortal境界・部屋遮蔽・実際の呼出順をカバーしない |

`RoomEntity.hpp`の`friend class ReplicaRoomPlacement`は**前のコミットから存在する最小限のコア接合部**であり、今回追加した差分ではない。`friend`自体のランタイムコストはゼロ。

## 3. 指摘事項（重要度順）

### P1-01：Portal境界に限定されない本体強制表示【確定：実装条件】

**場所：**
- [`PlayerEntityNetCulling.cpp` L225–267](https://github.com/Zection6V/Fruity-Prime/blob/2784d0025b71c02a789dfca4456da0e7cc2880f1/src/MphRead.Native/Mods/Network/Gameplay/Player/PlayerEntityNetCulling.cpp#L225-L267)
- [`ReplicaShadowVisibility.cpp` L14–66](https://github.com/Zection6V/Fruity-Prime/blob/2784d0025b71c02a789dfca4456da0e7cc2880f1/src/MphRead.Native/Mods/Network/Gameplay/Player/ReplicaShadowVisibility.cpp#L14-L66)
- [`PlayerDraw.cpp` L185–229](https://github.com/Zection6V/Fruity-Prime/blob/2784d0025b71c02a789dfca4456da0e7cc2880f1/src/MphRead.Native/Entities/Players/PlayerDraw.cpp#L185-L229)

`ReplicaShadowVisibility::ShouldForceBody()`は、リモート・地面投影あり・投影距離10未満・影Alpha>0を確認する。一方、`NodeRef`が前Partから次Partに移動したか、接続Portalを横断したか、有限開口内か、現在カメラから旧Partが可視か、経過tick数、といった条件は含まない。

`PlayerDraw.cpp`の `IsMainPlayer() || IsVisible(NodeRef) || ModNodeUnresolved()` は、現在Part不可視でも、上記地面条件が成立すれば本体描画に入る。**地面があることは、壁のない見通しが成立することを意味しない**。特にPortalカリング済みのマップ形状が深度バッファにないシーンでは、本体が壁越しに見えうる。

**修正方針：** 現行の目視改善を壊さず、先に「現在Part不可視かつ有効地面ありだが、Portalに隣接せず遮蔽される」合成ケースを回帰仕様として追加する。これが現行ロジックで強制表示されることは再現可能。ただし現実の描画が必ず壁越しになるかは別。誤表示が認められるか、可視性仕様上不適切と判断されたときだけ、`Mods/Network`に**有限Portal開口に制限された短命の境界救済**を追加・置換する。単なる旧Part固定、無限平面距離、長い猶予時間で代用しない。ネットワークプロトコル変更不要。

### P1-02：影の地面レイキャスト・フェード計算の重複【確定：DRY】

**場所：**
- [`PlayerDraw.cpp` L604–645](https://github.com/Zection6V/Fruity-Prime/blob/2784d0025b71c02a789dfca4456da0e7cc2880f1/src/MphRead.Native/Entities/Players/PlayerDraw.cpp#L604-L645)
- [`ReplicaShadowVisibility.cpp` L14–65](https://github.com/Zection6V/Fruity-Prime/blob/2784d0025b71c02a789dfca4456da0e7cc2880f1/src/MphRead.Native/Mods/Network/Gameplay/Player/ReplicaShadowVisibility.cpp#L14-L65)

双方が**同じ身体中心→10ユニット真下の衝突検査**（`CollisionDetection::CheckBetweenPoints`、`TestFlags::None`、法線Y閾値）と、**同じ影の距離フェード、死亡/リスポーン時減衰**を保持している。後日影の仕様を変更した場合、片方だけ修正され挙動が乖離する恐れがある。

しかも新コードは`ModRefreshNodeRef()`および`ModResolveNetworkNodeRef()`から `ModRefreshShadowProjection()` を呼ぶため、リモート位置反映時に**追加の衝突レイキャスト**が発生する。従来の`DrawShadow()`は引き続き描画側でレイキャストする。仮にリモート7人・60Hzで毎tick更新されれば、理論上**最大約420回/秒の追加衝突クエリ**となり得る。これは**FPS/CPU負荷の実測値ではなく、実装経路からの呼出回数目安**。`friend`導入のコストではない。

**修正方針：** 影を変えない契約がある以上、影実装をコアに移すなどの広範な共有化は非推奨。前述の**Portal境界専用判定へ移行する場合**は、`ReplicaShadowVisibility`の投影キャッシュ・影Alpha複製・追加レイキャストを丸ごと廃止するのが最もDRYかつKISS。現状維持する場合、最低限この重複をコードコメント・テスト対象として記録し、レイキャストを最適化したと主張しない。

### P2-03：`ModNodeUnresolved()`の責務混同【確定：命名／SRP】

`_modNodeUnresolved`は本来、位置から所属Partを確定できない状態。現行公開フックは`true`を返すもう一つの理由として「地面に影を落とせる」を追加した。その結果、**フック名と意味が一致しない**。ログ・メトリクス上のNodeRef未解決と、描画上の強制表示を別扱いする必要がある。

**修正方針：** 意味上は `ModShouldDrawRemoteBodyWhenPartHidden()` または `ModMayBypassReplicaPartCulling()` のような可視性方針として分離するのがよい。ただし今すぐ`PlayerDraw.cpp`やコアを多数変更しない。現行フック名を残すなら「返却値はNodeRefの状態そのものではない」ことをコメントで明示し、内部の未解決状態は別フィールドで維持。将来の境界専用ポリシー移行時にAPI整理する。

### P2-04：`ReplicaShadowVisibility`のSRP境界【設計判断】

1クラスが (a) 物理シーンへレイを飛ばして投影を求め、(b) 影の表示Alphaを再現し、(c) **キャラ本体のPartカリング解除**を判断する。いずれも可視性救済のためではあるが、**描画する本体の可視性が影生成の内部仕様に結びつく**。SRPを「1変更理由」と定義すると、影の仕様変更だけでネットワーク描画補正へ変更が波及する設計である。

**修正方針：** 影のルールに依存しない境界可視性ポリシーを`Mods/Network`内の小さな責務へ独立させる。独立のScene管理/ECS/新Portalツリー/影の共有GPU query等は導入しない。

### P2-05：Spawn時にローカルオーナーにも位置検索方式を変更【確定：差分】

**場所：** [`NetPlayerBridge.cpp` L344–384](https://github.com/Zection6V/Fruity-Prime/blob/2784d0025b71c02a789dfca4456da0e7cc2880f1/src/MphRead.Native/Mods/Network/Gameplay/Player/NetPlayerBridge.cpp#L344-L384)

旧 `Move(player, state.Position, player.SlotIndex()!=NetSession::LocalSlot())` はオーナースロットには通常移動経路、他スロットには再検索を使った。新 `Move(..., true)` はローカルスロットにも再検索を適用。`ModResolveNetworkNodeRef()`では`IsMainPlayer()`のとき `_cameraInfo->NodeRef = found` を実施する。上下に重なるマップでリモートSpawnの誤選択を避ける意図は妥当だが、ローカル/カメラへの挙動の変更は元々の表示バグとは別のテスト観点である。

**修正方針：** 現行コードを機械的に戻さない。**ローカルSpawn、復活、二層マップ、カメラ所属Part、テレポート**の回帰テストを追加し、オーナースロットに意図しない効果がないことを確認する。もしリモートだけの問題と確証できればローカルの従来経路保持を検討する。

### P2-06：`ModIsRemoteNetworkPlayer()`判定の重複・条件差【要意図明文化】

`ModIsRemoteNetworkPlayer()`は `IsClient() || IsHost()` を許容する一方、`useReplicaPlacement`は `IsClient() && !IsAuthority()` に限定される。**影による表示補助の対象と専用Part検索の対象が一致しない**。これはホストに描画させる設計なら意図的な違いかもしれず、即バグとは断定できない。

**修正方針：** 「所有者ではない表示コピーか」「ホストにも補助が必要か」をコメントとテストで固定する。DRYのために条件式を無条件に統合することは避ける。複数箇所の`SlotIndex()!=LocalSlot`の共通化は、実際に一致が要件である場合のみ行う。

## 4. 推奨する実装順序（症状再発を避ける）

1. **KEEP**：`2c65aea5`の表示改善、`ReplicaRoomPlacement`、既存`DrawShadow()`、共通`IsVisible()`を維持。
2. **TEST**：現行救済がPortal非隣接の不可視Partでも真になることを静的単体テストで示し、仕様に照らして壁越し候補となるか評価。既存`TestReplicaRoomPlacement.cpp`の純粋ルールテストを拡充。**新しい診断ログや手動テストセッションを必須にしない。**
3. **DECIDE**：過剰描画が禁止される仕様なら、既存Portalの有限開口・接続・短命状態に限る境界専用補助へ移す。**置換は最小単位で実施し、現在の成功ケース（Sic Transitの屋内/屋外・マップ端）が落ちないテストを先に作る。**
4. **DELETE DUPLICATION**：置換時に影キャッシュと重複したレイキャスト・影Alpha再計算を削除する。重複コードをコアに移して共有化するのではなく、ネット側が影仕様を複製しない形にする。
5. **CHECK**：ローカルSpawnの位置検索変更、ホスト/クライアント対象差、リスポーン、スロット再利用、シーン交換のケースを追加確認。
6. **MEASURE IF NEEDED**：修正前後で60Hz sim CPU時間・描画FPSを比較。事前に重いキャッシュ基盤やGPUクエリを追加しない。

### テストの最低要件

| ケース | 期待 |
|---|---|
| Sic Transit上下階層、天井、マップ端・Portal移動 | 現在の「影だけ消失なし」を維持 |
| 遮蔽された非隣接Partで地面投影だけ成立 | 本体を壁越しに描画しない |
| 実際の隣接Portalの有限開口を跨ぐ | 移動中の描画連続性を確保 |
| Portal面を離れた位置で横断、閉じたDoor/Force Field | 不当な救済なし |
| 正しいPart内、接続なし | 無用なレイキャスト・補助判定なし |
| Morph/Unmorph、リスポーン、テレポート、Slot再割当・Mapロード | 旧状態を持ち越さない |
| クローク、透明度、死亡、Spectate/HideModel | 以前の描画契約を維持 |
| オフラインBOT・通常の敵・アイテム・影 | 挙動不変 |
| 高FPS（500〜1000+） | 新しい幾何走査を描画FPSに比例させない |
| クライアント・ホスト・権威サーバー | 表示コピーの対象が意図どおり |
| Windows MSVC、Linux GCC、macOS Clang＋対象CTests | 成功（実施した結果を明示） |

## 5. CI / 実証状態

- [現行HEADのbuild_cpp #38380285483](https://github.com/Zection6V/Fruity-Prime/actions/runs/38380285483) は**本監査時点では in_progress**。静的監査の一部は成功しているが、MSVC/GCC/Clangビルド全体の完了は未確認。過去runの結果を現在HEADへ流用しない。
- 修正コミット`2c65aea5`のrun #38380101471 は **cancelled**。完了PASSではない。
- ユーザーの目視報告は「影だけになる問題が直った」であり、壁越し表示なし・性能差なし・全マップでの完全正しさの証拠とは区別する。
- 今回はGitHubリモートの静的監査のみ。ローカル実行/ビルド/テスト/プロファイルの実行、ソース変更、コミット、pushは一切行っていない。

## 6. 最終判定

**維持：** 目視で改善したネットワーク表示経路、既存の影描画、コアとの境界（新規コードはMods）。

**優先課題：** `影が投影できる => 本体を強制表示してよい`という汎用的な仮定を、Portal境界以外に広げてよいか検証する。実装上はPortal隣接を無視しているため、正しさを保証できない。影ルールの二重実装は明確にDRY不足。

**改善方針：** 先に小さな回帰テストで仕様を固定し、現行動作の成功を維持できることが確認できた場合にのみ、Mods内でPortal境界用ポリシーへ置換する。SRPのための大規模抽象化・ゲーム本体改造は避ける。
