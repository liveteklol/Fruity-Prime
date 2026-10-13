# Fruity-Prime C++ — リモートプレイヤー可視性修正のコミット後監査／条件付き修正指示

- 監査日: 2026-10-13（JST）
- 対象: `Zection6V/Fruity-Prime` / `develop11_netCode`
- 対象コミット: [`bc203802f07d12d5f63a472a52609e97d120280d`](https://github.com/Zection6V/Fruity-Prime/commit/bc203802f07d12d5f63a472a52609e97d120280d)
- 比較元: `fd820495db503020d8fc036acc6bd0702bbffab6`（差分1コミット）
- 方法: GitHub上のコミット差分・実コードの**静的監査**、ユーザー提供の実装・目視確認ログの突合
- 作業範囲: **調査と指示書のみ**。ソース編集・コミット・push・再ビルド・実行・ログ採取なし

## 1. 結論・処置判定

**判定: 既知の表示消失症状に対する今回の修正は暫定的に採用・維持。直ちに巻き戻す必要なし。ただし、可視判定を不当に無効化する可能性が残るため、条件付きの後続修正と軽量な自動回帰テストを推奨。**

根拠:

1. ユーザー提供ログには、Sic Transit、操作プレイヤー1人＋独立したAIネットワーククライアント7人、AI側250 ms RTT、ポイントゴール999のセッションにおいて、修正の目視結果が「なおった」「うん、治ってる」と記録されている。**この実績は否定しない。**
2. 13:54ごろの単純な「身体中心優先」修正では症状が残り、その後のPortal歩行優先版では症状が悪化。したがって、**単純に身体中心を優先する旧指示と、前回のNodeRef歩行への全面回帰を再提案しない**。
3. 最終コミットはネットワーク側に `ReplicaRoomPlacement` を配置し、`RoomEntity.cpp`、`Renderer.cpp/.hpp`、`PlayerDraw.cpp`、`EntityBase::IsVisible()`、`DrawShadow()`を変更していない。`RoomEntity.hpp`の最小限の`friend`宣言のみがコア層の差分。
4. `friend`はコンパイル時のアクセス制御であり実行時命令や仮想呼出しの追加は発生しない。ただし**配置変更前と同程度**という意味であり、**今回の探索ロジックと従来コードのCPUコストが同じ**とは言えない。
5. 現在の探索で確実にPartが解決できているか、それとも`ModNodeUnresolved()`の「必ず本体を描画」フォールバックで消失が隠れているかは、提示された目視結果だけでは区別できない。ここが残留リスク。

**判断上の重要な区別:** 修正後の見た目が正しいこと（報告済み）と、見えない壁越しに本体を描画していないこと（未検証）は別の要件。ログのないまま「この修正は間違い」「完全解決」を断定しない。

## 2. 監査したコミット内容

| ファイル | 差分 | 評価 |
|---|---:|---|
| `src/MphRead.Native/Mods/Network/Gameplay/Player/PlayerEntityNetCulling.cpp` | +21 / -7 | リモート専用の配置検索へ変更。身体中心優先。 |
| `src/MphRead.Native/Mods/Network/Gameplay/Player/ReplicaRoomPlacement.cpp` | 新規108行 | Portal半空間＋メッシュノードAABBで候補Partを選別。 |
| `src/MphRead.Native/Mods/Network/Gameplay/Player/ReplicaRoomPlacement.hpp` | 新規27行 | 判定ロジックをModsに集約。 |
| `src/MphRead.Native/Entities/RoomEntity.hpp` | +7行 | `ReplicaRoomPlacement` に対する `friend`。コア実装変更はなし。ただしコアヘッダー変更は**存在する**。 |
| `docs/howto/network-visual-session.md` | +4 / -2 | テストセッションのポイントゴールを999に更新。 |

### オンライン固有性が確認できる経路

```
NetSmoothing::Sample(slot, sampledPosition, sampledAltForm)
  → PlayerFormReplication::ApplyPresentation(...)
  → NetPlayerBridge::Move(player, InForm(...), true)
  → PlayerEntity::ModResolveNetworkNodeRef()
  → ReplicaRoomPlacement::Find(...)
  → PlayerEntity::NodeRef / _modNodeUnresolved
  → PlayerEntity::Draw() の IsVisible(NodeRef) || ModNodeUnresolved()
```

主要確認箇所:

- [`NetPlayerBridge.cpp` 450–490](https://github.com/Zection6V/Fruity-Prime/blob/bc203802f07d12d5f63a472a52609e97d120280d/src/MphRead.Native/Mods/Network/Gameplay/Player/NetPlayerBridge.cpp#L450-L490)
- [`PlayerEntityNetCulling.cpp` 139–209](https://github.com/Zection6V/Fruity-Prime/blob/bc203802f07d12d5f63a472a52609e97d120280d/src/MphRead.Native/Mods/Network/Gameplay/Player/PlayerEntityNetCulling.cpp#L139-L209)
- [`ReplicaRoomPlacement.cpp` 14–107](https://github.com/Zection6V/Fruity-Prime/blob/bc203802f07d12d5f63a472a52609e97d120280d/src/MphRead.Native/Mods/Network/Gameplay/Player/ReplicaRoomPlacement.cpp#L14-L107)
- [`PlayerDraw.cpp` 186–227](https://github.com/Zection6V/Fruity-Prime/blob/bc203802f07d12d5f63a472a52609e97d120280d/src/MphRead.Native/Entities/Players/PlayerDraw.cpp#L186-L227)

`ModResolveNetworkNodeRef()`の先頭ではフォーム反映済みの現在位置に合わせて`_volume`を更新し、リモート所有スロットかつ非Botのクライアントに限り、`ReplicaRoomPlacement::Find()`を利用している。デモ・ローカル・通常Botへの適用条件は維持していることを確認すること。

## 3. 確定した事実と潜在リスク

### F-1［確認済み］ `friend`の性能負荷はゼロ

`RoomEntity.hpp`の`friend class Mods::Network::ReplicaRoomPlacement;`はC++のアクセス権だけを変える。**それ自体に実行時のCPU/GPUコストはない**。

ただし`const auto room = replica ? scene.Room() : nullptr;`による`shared_ptr`コピーや、`Find()`が各候補で行うノード走査は**別の実行時コスト**。`friend`の有無と一緒にしない。

### R-1［重要・静的に経路を確認］ 未解決フォールバックが遮蔽をバイパスし得る

`ReplicaRoomPlacement::Find()`で複数の候補が残ると`ambiguous=true`をセットして`NodeRef::None`を返す。また、どの候補もメッシュAABB条件に合わない場合も`None`を返す。

その後`ModResolveNetworkNodeRef()`は`_modNodeUnresolved=true`にして**前のNodeRefを残す**。描画条件が`IsVisible(NodeRef) || ModNodeUnresolved()`なので、**前のNodeRefが不可視でも、モデルの描画を許可する**。

これは消失回避の意図として理解できるが、マップのPortal Cullingで壁自体が描画されていないケースではGPUのDepth Testだけでは隠せず、**壁越し表示・遠方の不要描画**が生じ得る。発生は未確認だが、この到達経路はコード上確定している。

**禁止:** 現在の目視成功を見ずに`ModNodeUnresolved()`を全面削除すること。そうすると元の「影だけ残って本体が消える」症状に戻る可能性がある。

### R-2［重要・設計の意味論］ メッシュAABBは空間所属を厳密には表さない

`GeometryBoundsContain()`は、`node.Enabled && node.MeshCount > 0 && !node.Name.starts_with("sky")`のノードの**個別メッシュAABB内**に`bodyCenter`が収まるか確認している。

これは、マップの床・壁・装飾などの描画メッシュが囲む座標範囲に身体中心が入るという**代理判定**であり、実際の部屋の空き空間を数学的に表す保証はない。上下の部屋や空洞、スカイ・不可視ノードの構成によっては偽陰性・重複ヒットが起こり得る。

`node.Enabled=false`を無視することと、`"sky"`という名前依存の例外も、データセットに対する暗黙の仮定である。なお、**現在のSic Transitで必ず誤判定しているとまでは言えない**。目視で修正成功が確認されているため、まずはリスクとして扱う。

### R-3［性能・DRY］ 既存のPart境界計算より重い候補探索

`RoomEntity::GetNodeRefByPosition()`はPortalの半空間チェック後に既存の`PartBoundsDepth`でPartの集約Boundsを評価する。新しい`Find()`は各候補に対しメッシュの`NextIndex`チェーンをたどり、複数AABBを検査する。**呼出回数が同じでも1回当たりのCPU負荷は増える可能性**がある。

`ModResolveNetworkNodeRef()`では現状最大4つのprobeについて毎回`Find()`を呼び得る。典型的に7リモート×60Hzなら約420の配置更新/秒、全4probe実行の理論上限は約1680回の`Find()`/秒。実際の回数は分岐次第で、FPSやCPU時間は未計測。

- 描画540/1000 FPSに比例して追加探索は実行されないため、**高FPSへの直接の倍率影響はない**。
- ただし重いシミュレーション処理は高FPSのためのCPU時間を圧迫し得る。
- `probe`が異なっても`GeometryBoundsContain()`の検証対象`bodyCenter`は固定。AABBによる不一致が原因なら4probeで繰り返し同じ失敗をすることになる。ここは条件分岐を簡素化できる余地があるが、根拠なくprobeを削ることはしない。
- これらの数値は**コードから求めた例示・上限**であり、実測値ではない。

### R-4［アーキテクチャ］ Mods方針は実質順守、ただし例外1か所

実装ロジックは `Mods/Network/Gameplay/Player` に集約され、`RoomEntity.cpp`・`Renderer.cpp/.hpp`にはこのコミットの差分がない。これは目的に合う。

ただし`RoomEntity.hpp`に`friend`を追加しているので、**「Coreに全く変更がない」とは書かない**。現状で性能のために変更を強制する必要はないが、privateなPortal・モデル・コネクタの内部構造にModsが依存する点は将来の変更耐性を下げる。コアを汎用API化するためだけの追加改修は**今は不要**。

### R-5［既存処理］ オフラインと他Entityの表示は変わらない

本コミットには`PlayerDraw.cpp`、`EntityBase.cpp`、`ItemInstanceEntity.cpp`、`EnemyInstanceEntity.cpp`の差分がない。`DrawShadow()`は依然として本体判定の前に呼ばれる。

ただし`_modNodeUnresolved`の結果がオンラインの描画判定に影響する点は**意図した変更**であり、影の仕様に変更がないこととは別に扱う。

## 4. 推奨する後続対応（修正指示／静的監査・自動テスト優先）

### 優先度P1-A: `ReplicaRoomPlacement` の判定結果を3状態として明文化

現状の`NodeRef`と`bool& ambiguous`では、「唯一の所属が確定」「複数候補」「候補ゼロ」「情報不足」が混ざる。**コード修正が必要になった場合に限り**、Mods内だけで返却結果を`Resolved / Ambiguous / Unknown`に整理し、誤って「判定不可＝見える」と扱う分岐を独立して検証できるようにする。

ただし新しい汎用Visibility Manager、Map Partキャッシュ、RenderFrame用判定、巨大な抽象基底クラスは作らない。**現行の視認成功経路は残す**。

### 優先度P1-B: 少数の合成データによる回帰テストを用意

テスト観点（ユーザーにライブ診断を要求しない。必要なら既存のテスト環境に小さなfixtureを追加）:

1. **同一XYで上下に異なるPart**: 上側Part・下側Partの身体中心がそれぞれ一意に解決できる。
2. **床・壁メッシュのAABBに身体中心が含まれないがPortal上は合法**: `None`への分類と最終的な可視性が仕様通りであること。誤って常時可視にしない。
3. **隣接Partとの重複AABB**: 複数候補時の処理が仕様として定義され、壁越し表示を起こさない。
4. **部屋端・Portal境界**: 一意確定時に正しいNodeRef。境界が曖昧な場合でも無条件に遠方Partを表示しない。
5. **Alt Form / Morph / リスポーン**: 現在のフォームの球中心が使われ、過去のノード・Lifeを引き継がない。
6. **不完全なGeometry／非有限値／コネクタ**: nullや配列境界を守り、意味不明な`NodeRef`を採用しない。
7. **正常なオンライン＋オフライン**: 既知成功シナリオで本体が消えない。アイテム・敵・影・通常Botの処理は変化なし。

※ 実コードに到達できる形の小さなfixtureが作れないなら、テストのために本体設計を大きく改造しない。最低限、純粋な分類規則を切り出すのは選択肢。

### 優先度P1-C: 残留する壁越し表示を防ぐ（上記テストで必要性が証明された場合）

`ambiguous`/`unknown`に対して`_modNodeUnresolved=true`を立てる既存ルールは消失回避に寄せた**可視性フォールバック**である。検証により描画漏れではなく壁越し描画を誘発すると分かったケースに限定し、ネットワーク側の候補解決規則を補正する。

- 旧来の`NodeRef`歩行を一律優先に戻すことは禁止（過去に悪化）。
- メッシュAABBだけで物理空間所属を断定しない。Portal半空間と部屋モデルの特性を考慮する。
- 根拠なく`IsVisible(NodeRef)`をバイパスしたり`_modNodeUnresolved`を消すだけの修正は禁止。
- 正常ケースの描画は既存`NodeRef`可視性で完結させる。

### 優先度P2: 低負荷化は必要と分かったときだけ

性能を追う場合は、`Find`呼出回数、平均/最大候補数、メッシュチェーン走査数、実行時間、`ambiguous`と`unknown`の件数を**既存自動テストで**確認する。目視の再現診断を新設する必要はない。

改善の候補は**再計算回数と重複走査の削減だけ**。事前構築の重い空間インデックス、キャッシュ世代の複雑化、GPU Occlusion Queryの追加、毎描画フレームの幾何判定を導入しない。`friend`の置換を性能チューニングと称して行わない。

### 優先度P2: パス境界・依存関係の維持

- 将来の変更も `Mods/Network/Gameplay/Player/` 内に限定することを原則にする。
- `RoomEntity.hpp` のfriendは承認済みの小さな接合部として現状維持。撤去が必須なら、同等の安全な読み取りインターフェースが**既存の**コアAPIにあるか先に確認する。
- Coreゲーム処理の関数本体（`RoomEntity.cpp`、`PlayerProcess.cpp`、`Renderer.cpp/.hpp`）、影、共通`EntityBase::IsVisible()`、敵、アイテムを変更しない。
- ネットワークプロトコル／同期サイクル／NetSmoothingを変更しない。

## 5. 必須の非回帰条件

| 条件 | 合格要件 |
|---|---|
| 既存の修正効果 | Sic Transitの屋外/屋内重畳エリア、天井付近、マップ端で相手本体が意図せず消えない |
| 遮蔽の正しさ | 見えないPart/壁の奥にいる別プレイヤーが壁越しに描画されない |
| 影 | `DrawShadow()`の分岐・順序・出力を変更しない |
| 対象限定 | オフラインBOT、アイテム、敵、アーティファクトに描画回帰なし |
| 性能 | 追加の幾何探索が描画フレーム数に比例せず、実行時間・FPSに目立つ後退なし |
| ビルド | MSVC、GCC/Clangの通常構成でコンパイル・関連自動テスト成功 |
| アーキテクチャ | Mods内のネットワーク描画補正として閉じ、Core本体の新規変更なし |

既知実績（提供ログ）: MSVC Releaseビルド成功、該当セッションの表示が目視で正常化。**自動テストは未実施**。本監査ではログ実体・バイナリ・CPUプロファイルは検証していない。

## 6. CI状況（確認時点）

対象コミットのCI: [`build_cpp` run 38353683347](https://github.com/Zection6V/Fruity-Prime/actions/runs/38353683347)。監査時点では `in_progress`。静的監査ジョブの一部はSUCCESSだが、Windows MSVC・Linux arm64などのビルドジョブは進行中だった。**全体PASSと記載しない**。再実行・キャンセル・変更は行っていない。

## 7. 実装者への最終指示

> **このコミットは、提示されたSic Transitオンライン実機相当セッションで症状解消の目視実績があるため、現状の修正を維持してください。** 静的監査で見つかった`GeometryBoundsContain()`の空間所属としての厳密性不足、および`ambiguous/unknown`による`ModNodeUnresolved()`常時描画の可能性は、直ちに新しい可視性エンジンに作り替える理由にはなりません。最小限の自動回帰テストで経路の正しさを確保し、テストで問題が再現した箇所だけをMods側で修正してください。最初から旧Portal歩行優先に戻したり、影・共通IsVisible・Coreレンダラーを書き換えたり、FPSが落ちたと断定したりしないでください。

### 変更実施の優先順位

1. **KEEP**: `bc203802` の目視で成功した現行実装。
2. **VERIFY**: 独立した合成幾何とPart可視性の回帰テスト。特に「None→本体常時描画」の隠れた経路。
3. **FIX ONLY IF PROVEN**: 重複候補・未解決判定の局所的な改善。
4. **OPTIMIZE ONLY IF MEASURED**: 同一bodyCenterに対する重複走査とmesh chain探索。
5. **DO NOT**: Coreの汎用可視性、DrawShadow、敵、アイテム、ネットプロトコル、レンダラーパイプラインを変更。

---

*この文書は静的監査結果とユーザー提供の作業ログに基づく指示書。対話内の目視成功は利用者報告として記録し、直接検証済みと混同しない。*
