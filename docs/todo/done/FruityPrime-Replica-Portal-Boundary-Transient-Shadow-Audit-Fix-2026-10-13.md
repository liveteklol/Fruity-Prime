# Fruity-Prime C++ — オンラインReplicaがMap Part境界で一瞬「影だけ」になる問題

**静的監査・最小修正指示書（DRY / SRP / KISS）**  
作成日: 2026-10-13（JST）  
対象: [`Zection6V/Fruity-Prime`](https://github.com/Zection6V/Fruity-Prime/tree/develop11_netCode) / `develop11_netCode`  
監査対象SHA: [`c01aabc77af6a03646f83b9298130aba52212444`](https://github.com/Zection6V/Fruity-Prime/commit/c01aabc77af6a03646f83b9298130aba52212444)  
前提: オフラインBOT正常、オンラインReplicaは大幅改善済み、**マップ境界で一瞬だけ本体が消え影が残る**。影の表示は変更しない。原則としてゲーム本体の `Entities` / `Renderer` / `Formats` に新規ビジネスロジックを入れず、`Mods/Network` 側で対応する。

## 1. 結論と確度

- **確定（コード）**: `PlayerEntity::Draw()` は `DrawShadow()` を本体の可視判定より先に実行する。`IsMainPlayer() == false`、`IsVisible(NodeRef) == false`、`ModNodeUnresolved() == false` であれば本体の描画ブロックに入らない。`HideModel`、Spectating、Alpha等の別条件は除外して考える必要がある。
- **確定（コード）**: `ModResolveNetworkNodeRef()` は `ReplicaRoomPlacement` がひとつのPartへ `Resolved` を返すと、**そのPartが現在のカメラから可視かどうかと無関係に** `NodeRef=found`, `_modNodeUnresolved=false` とする。これが「正しく位置が解決されたのに影だけ描画する」経路を作る。
- **確定（コード）**: 現行の純粋ルールテスト `TestReplicaRoomPlacement.cpp` は、Portal半空間・Bounds・曖昧性・無効値を検証するが、「**前Partから次Partへ切り替わる際に一時的に非表示にしない**」という描画の連続性は検証しない。
- **高い可能性（現象との対応）**: 身体中心による所属Partが境界を跨いだ瞬間、**身体の一部が前の可視Partに残る**のに新Partの可視フラグだけで本体全体をカリングすることがある。60Hzの表示位置更新と高FPS描画の組み合わせで短時間の症状に見える。
- **未確定**: 上記が唯一の原因かどうか。実行時トレースや各画面のスクリーンショットでの確証は未取得。ステルス・復活・透明度、カメラ側Part更新の瞬間的な変化にも注意。

**推奨:** 現行の部屋判定を維持し、**実際の隣接Portalを跨ぐ短時間だけ、本体の可視性に直前のPartを追加で使う**。ゲーム本体の描画規則・ネット同期・影を作り直さない。まずユニットテストで境界可視性の連続性を固定する。

## 2. 根拠となる実装

| ファイル / 位置（監査SHA） | 事実 |
|---|---|
| `src/MphRead.Native/Entities/Players/PlayerDraw.cpp` L186–230 | `DrawShadow()` が先。`IsVisible(NodeRef) || ModNodeUnresolved()` で本体をカリング。 |
| `src/MphRead.Native/Mods/Network/Gameplay/Player/PlayerEntityNetCulling.cpp` L139–209 | ネットワーク専用Part探索。成功時 `_modNodeUnresolved=false`。 |
| `src/MphRead.Native/Mods/Network/Gameplay/Player/ReplicaRoomPlacement.cpp` L14–47 | Portal半空間とメッシュ境界によるPart分類。 |
| `src/MphRead.Native/Mods/Network/Gameplay/Player/ReplicaRoomPlacementRules.hpp` | `Resolved / Ambiguous / Unknown`。分類テストは今回のコミットで追加。 |
| `src/MphRead.Native/Entities/RoomEntity.cpp` L1264–1442, L1709–1715 | カメラに応じて更新する `_activeRoomParts` による可視性。 |
| `src/MphRead.Native/Entities/Players/PlayerProcess.cpp` L1328–1348 | 通常プレイヤーは従来のPortal通過でNodeRefを更新。 |
| `src/MphRead.Native/Mods/Network/Gameplay/Player/NetPlayerBridge.cpp` L451–490, L981–996 | 補間位置・フォーム確定後にReplica位置とNodeRefを適用。 |
| `src/MphRead.Native/Renderer.cpp` L1888–1935, L3718–3760 | Map Part可視フラグを描画フレームで作り、本体描画へ進む。 |

オンラインのみを対象にし、**オフラインBOT、通常の敵、アイテム、アーティファクト、`EntityBase::IsVisible()` と `DrawShadow()` は変更しない**。

## 3. 修正を小さくするための実装仕様（推奨）

### 3.1 必要なのは「所属Part」と「境界での表示候補Part」の分離

- `NodeRef` はこれまで通り**位置・モデル所属**を表す。可視Partを優先して `NodeRef` を書き換えることはしない（音声、スロット、衝突、アイテムなどへの波及を避ける）。
- `ReplicaRoomPlacement::Find()` の判定式や `Resolved / Ambiguous / Unknown`、フォールバック探索は保持する。
- **表示補助用の短命な前Part候補**を `Mods/Network` 側に保持する。別Room、Respawn、スロット再利用、テレポート、切断、フォーム変更の境界では失効させる。
- 候補は「以前の `NodeRef`」を漫然と使わない。**前後のPartが実際に接続された有効なPortal**であり、プレイヤーの身体球がその**有限のPortal開口付近にある**と確認できたときだけ許可する。`Portal.Plane` という無限平面への距離だけでは不十分。
- `Formats::CollisionDetection::CheckPortBetweenPoints()` は `Portal.Points/Planes` に基づく開口検査を行う既存のヘルパー。前後の身体中心線分が横断するケースにはこれを再利用可能。移動の瞬間に平面上から始まる場合など、線分判定の境界条件（`dotPrev > 0 && dotCur <= 0`）を認識すること。許容補助判定を加える場合も**有限開口内**を必須とする。
- 有効なPortalで `previousPart -> newPart` への遷移が発生した場合に限り、補助候補は**最大1シミュレーションステップ（60Hzなら約16.7ms）**有効。描画FPSに依存させない。視覚的に効果が足りないことがテストで示されるまでは期間を拡大しない。
- 判定は「現在Partが不可視、かつ直前のPartが可視」のときだけ本体可視を補助する。現在Partが可視なら既存の通常描画を優先する。
- `RoomEntity::IsNodeRefVisible(previousPart)` に相当する**既存の可視フラグ参照は描画フレーム時点で照合**する。境界候補の幾何学判定はネットReplica更新時だけ行い、毎描画フレームにPortal全走査はしない。

### 3.2 ネットワーク層に閉じる構成

推奨する実装先:

- `Mods/Network/Gameplay/Player/ReplicaRoomPlacement.*`: `Find()` は維持。必要であれば `FindBoundaryBridge(...)` のような**小さな独立関数**を追加し、接続Portal・有限開口・身体球近傍を判定する。既存の `RoomEntity` `friend` 宣言を流用して内部構造を**読み取り専用**で参照。
- `Mods/Network/Gameplay/Player/PlayerEntityNetCulling.*`: 前部屋候補の短命ステート、適用条件、リセットを保持。新しい判定の責務はここに限定。
- `Mods/Network/Gameplay/Player/NetPlayerBridge.cpp`: 直前身体中心を必要とする場合、`Move()` が元々知っている旧ポーズをネット更新側に受け渡す。通常ゲーム側の `PlayerProcess` へ実装を追加しない。

**描画側の接続について:** 現在 `PlayerDraw.cpp` は `ModNodeUnresolved()` というMods側既存のフックをすでに呼ぶ。ゲーム処理にロジックを置かない契約を最優先するなら、このフックで「本体の単一Partによるカリングが安全に確定できない短時間の境界状態」も判定する。ただし内部の真の `_modNodeUnresolved` と境界救済状態は**独立フィールド**にしてログ・メトリクスを混同しないこと。`ModNodeUnresolved()` の公開名との意味のズレはコメントに明記し、将来的な命名変更は別途行う。実装上、キャッシュした**幾何候補**と描画時点の **O(1) Part可視フラグ**を分ける。

より命名を正確にしたい場合は `ModShouldBypassNetworkBodyCulling()` 等に改名して `PlayerDraw.cpp` の呼び出しを1行差し替える方法もあるが、**今回の最小変更では行わない**。

### 3.3 表示可否の期待ロジック（擬似コード）

```cpp
// PlayerDraw の既存条件式は保持する。
// 以下は Mods 側の既存フックの概念仕様であり実在APIのコピーではない。
bool ModNodeUnresolved() const
{
    if (actualNodeUnresolved) return true;
    if (!remoteReplica || !activeOneTickBoundaryBridge) return false;
    if (currentRoom != bridgeRoom || currentLife != bridgeLife) return false;
    // 幾何学的な有効Portal通過と近傍判定は60Hz側で検証済み。
    return !IsPartVisible(currentNode) && IsPartVisible(previousBridgeNode);
}
```

`IsPartVisible` は実際の描画カメラで作られたPartフラグを使う。**パートの可視性結果をネットワークTick時点で固定しない**。`noexcept`、const、null room等の既存宣言と呼出し契約に合わせること。

## 4. 禁止事項とリーク対策

1. **本体を無条件描画しない。** `NodeRef::None` を意図的に代入してカリングを回避しない。ステルス・`HideModel`・`Spectating`・Alpha・LOD・フリーズ等の判定を維持する。
2. **影は変更しない。** `DrawShadow()` の前後関係、床レイキャスト、透明度、パス構造は不変。
3. **Partの可視フラグを変更しない。** `RoomEntity::_activeRoomParts` に手を加えない。Portal Culling / GPU Depth Testも変更しない。
4. Portalの**無限平面距離だけ**で救済しない。屋外/屋内が上下に重なるSic Transitでは離れた位置でも同一平面付近と判定しうる。有限のPortal開口または既存の通過検査が必要。
5. テレポート、スナップ、マップ移動、リスポーン、死亡、スロット再割当、部屋ロード変更、現行フォームの身体体積変更では候補破棄。以前の見えていたPartを際限なく延長しない。
6. **キャラの位置同期やNet Smoothing時間軸には触れない。** Gameplay transformをRender transformとして上書きせず、NodeRefのゲーム状態も変更しない。
7. 毎描画フレームで `GetNodeRefByPosition()` や Portal/メッシュBounds走査を追加しない。新規heap確保、GPU query、ネットワークプロトコル変更は禁止。

## 5. テスト指示（新規ログ・目視診断モード不要）

テストはできるだけ `Testing/TestReplicaRoomPlacement.cpp` のテスト方針を拡張し、幾何ルールと可視連続性の条件を**純粋な関数として決定的に検証**する。新規の対話的診断セッションやロギングを修正の必須条件にしない。

| ケース | 期待結果 |
|---|---|
| 同じPart内を移動 | 追加の表示救済なし / 通常描画。 |
| 有効なPortalから可視A → 不可視Bへ進入し、身体がまだ境界に重なる | 短命の前Part候補が有効なら本体は一瞬消えない。 |
| Portal中心から離れた同一平面上を通過 | 救済しない（壁越し表示を防止）。 |
| 無関係のPartにテレポート/大きいスナップ | 救済しない。 |
| 不可視A → 不可視B | 救済しない。 |
| 現在Bが可視になった | 即座に通常描画に戻す。 |
| リスポーン / Room切替 / Slot再利用 | 前Part候補を破棄。 |
| 二階層・同じXZで異なるY / モーフ切替 | 他方の階層を誤表示しない。 |
| 境界で停止 / 境界を逆戻り | 過去のPartが永久に表示資格を得ない。 |
| `HideModel`, Spectating, クローク, 死亡 | 今まで通り。特例で描画強制しない。 |
| オフラインBOT、アイテム、敵、影 | 完全に従来通り。 |
| 60Hz / 120 / 500 / 1000+ FPS | 表示境界の救済**時間はシミュレーション時間**に基づき一定。描画時追加処理はO(1)。 |

**受入条件**: 回帰テスト合格、当該境界のちらつき解消、壁越し描画の新規発生なし、既存要件維持。静的監査だけでは実機での完全解消やFPSへの影響を**証明できない**ことを明記する。

## 6. 最新CI（監査時点）

- [GitHub Actions run 38355772348](https://github.com/Zection6V/Fruity-Prime/actions/runs/38355772348) は **failure**。
- Windows / MSVC、Linux / GCC、macOS / Clang、Linux arm64 dedicated server、Android NDKの各ジョブは **success**。
- 失敗は **Android APK Build signed APK** で、`android/app/src/main/java/fr/livetek/fruityprime/MainActivity.java:129` の `QtQmlStatusChangeListener is not a functional interface` というJavaコンパイルエラー。今回のネット可視性変更とは**直接異なるソースファイルと失敗工程**。APK問題は別チケットで対応し、表示修正に混ぜない。

## 7. 最終判断

**今回の `ReplicaRoomPlacement` を撤回しない。** オンラインの重大消失がユーザーによるSic Transitの目視検証で改善済み。`c01aabc7` の純粋ルールテストは有意義。追加修正は「所属Partを変える」のではなく、**ごく短いPortal境界での本体可視性の連続性**に限定する。静的監査からの提案であり、修正の実行・ビルド・コミット・pushは実施していない。
