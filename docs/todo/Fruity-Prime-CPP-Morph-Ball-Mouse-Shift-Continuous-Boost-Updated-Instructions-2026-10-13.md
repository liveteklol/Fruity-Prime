# Fruity-Prime C++ モーフボール操作・Shift連続Boost 実装修正指示（現行コード対応版）

- 更新日：2026-10-13
- 監査対象：`Zection6V/Fruity-Prime` / `develop11_netCode`
- 確認したHEAD：[`aadb028d421cba0bfeb291569cac4b7dff8f8772`](https://github.com/Zection6V/Fruity-Prime/commit/aadb028d421cba0bfeb291569cac4b7dff8f8772)
- 対象：**C++版 `src/MphRead.Native` のみ**
- 原文：`Fruity-Prime-CPP-Morph-Ball-Mouse-Shift-Continuous-Boost-Implementation-Instructions-2026-10-05(1).md`
- 作業種別：現行コード静的調査に基づく**修正指示書**。本書作成に伴うソース変更、ビルド、ゲーム実行、コミット、プッシュはしていない。
- 基準となる仕様：原文の「Samusのマウスによる移動、方向指定フリックBoost、Space手動Boost、Shift長押しでの自動連続Boostを共存させる」を維持する。

> **最重要：2026-10-05版のコード断片をそのまま適用しないこと。** 現行版はタッチ入力とBoost処理がSRPに沿って再構成されている。旧 `MouseFlick` 専用実装や `_boostAimLock` を前提とした修正は不要・不適切である。

## 1. 要約・変更方針

### 1.1 現在実装済みの機能と未完了の機能

| 項目 | 現行状況 | 今回の指示 |
|---|---|---|
| SamusのWASDロール移動 | 実装済み | 維持・回帰確認 |
| マウスからのMorph Ballステアリング | **実装済み**。マウス相対移動をDSタッチに変換し `ApplyTouchRoll` に渡す | 再実装しない。操作感と30Hzサンプリングを検証 |
| マウス方向のフリックBoost | **実装済み**。DSタッチの4サンプル合計で判定し `TouchBoost` を発火 | 旧MouseFlick方式を追加しない。回帰テスト |
| Spaceを押してチャージし、離してBoost | 実装済み。`ShoulderBoost` の経路 | 維持 |
| Shiftを押し続ける自動チャージ・自動解放・再チャージ | **未実装**。35個のbindingに専用actionなし | 主実装対象 |
| Shift中のマウス移動とBoost共存 | 既存経路の競合調停が必要 | TouchRoll継続、TouchBoostとShoulderBoostの二重発火を禁止 |
| Shiftとアドベンチャーマップラベル表示のキー競合 | **未解消**。`AdventureMapLegend` の既定値はLeftShift | キー追加と旧設定migration |
| オンラインでのBoost入力転送 | `IntentButtons::Boost` に通常Boost入力を転送 | 自動Boostの実効入力・releaseも送信側とauthority側で一致させる |
| Native touch / boostの自動テスト | `FruityPrime.NativeTouchState` あり | 新規状態とネットワーク経路のケースを追加 |

この結果、旧文書の主題のうち、**マウスステアリングとフリックBoostは新アーキテクチャで既に実現している**。今回追加すべきものは独立したShift操作、Boost調停、設定互換、オンライン同期、および回帰テストである。

### 1.2 実装の境界

1. **入力の取得**：`PointerDevice`・`PlayerInput`・`TouchInputAdapter`。
2. **DSタッチ表現**：`NativeTouchState`、`NativeTouchSample`、`MouseMotionTouchSource`。
3. **ロール物理の純粋計算**：`MorphBallTouchRules`。
4. **Boost状態遷移**：`MorphBallBoostStateMachine`。
5. **実際の速度・ダメージ・演出**：`PlayerMorphBall.cpp` の `PlayerEntity` メソッド。
6. **設定・永続化**：`PlayerControls` と `InputSettings`。
7. **オンライン転送**：`NetPlayerIntent`、`NetPlayerBridge`、`NetInputHistory`。

これらの責務を混ぜず、既存コードを再利用する。特に `PlayerInput.cpp` の `ProcessAlt()` を巨大化させない。

## 2. 現行実装の確定事項（ソース監査）

### 2.1 マウス入力は既にDSタッチへ変換される

関連ファイル：

- [`Entities/Players/PlayerInput.cpp`](https://github.com/Zection6V/Fruity-Prime/blob/develop11_netCode/src/MphRead.Native/Entities/Players/PlayerInput.cpp#L359-L384)：`PointerDevice::TakeDelta()` または `PointerInput::Filter()` から `_mouseDeltaX/Y` を取得し、`_touch.Step(...)` に渡す。
- [`Mods/Input/TouchInputAdapter.cpp`](https://github.com/Zection6V/Fruity-Prime/blob/develop11_netCode/src/MphRead.Native/Mods/Input/TouchInputAdapter.cpp)：`HostTouch`、ペン／StylusZone、相対マウスの優先順位を管理。
- [`Mods/Input/MouseMotionTouchSource.hpp`](https://github.com/Zection6V/Fruity-Prime/blob/develop11_netCode/src/MphRead.Native/Mods/Input/MouseMotionTouchSource.hpp)：`DsUnitsPerPixel = 0.25F`、`IdleTicksBeforeLift = 2`。
- [`Mods/Input/NativeTouchClock.hpp`](https://github.com/Zection6V/Fruity-Prime/blob/develop11_netCode/src/MphRead.Native/Mods/Input/NativeTouchClock.hpp)：60Hzシミュレーション2ステップごとに30Hzのネイティブタッチサンプルを生成。
- [`Mods/Input/NativeTouchSample.hpp`](https://github.com/Zection6V/Fruity-Prime/blob/develop11_netCode/src/MphRead.Native/Mods/Input/NativeTouchSample.hpp)：30Hzの同一サンプルを60Hzの2ステップへ配布し、`RollShare()` によりロール量を按分。

**注意**：マウスの物理サンプル取得が60Hzのシミュレーションで行われても、モーフボールへ渡すタッチ入力の更新は30Hzである。原文にあった「continuous steeringが未実装」という断定は撤回する。高リフレッシュレート環境での視覚上の滑らかさは別途ゲーム内で検証すること。

### 2.2 ロールとフリックは別実装ではなく同じタッチ経路

[`Entities/Players/PlayerMorphBall.cpp`](https://github.com/Zection6V/Fruity-Prime/blob/develop11_netCode/src/MphRead.Native/Entities/Players/PlayerMorphBall.cpp) に以下がある。

- `ApplyTouchRoll()`：`_input.Touch()` の `Delta4X/Y` からロールを加算。
- `ProcessBoost()`：タッチとSpace入力を `MorphBallBoostStateMachine::AdvanceSample()` へ渡す。
- `ApplyTouchBoost()`：タッチのスワイプ方向をカメラ基準のBoost impulseへ変換。
- `ApplyShoulderBoost()`：既存の前向きショルダーBoost。
- `ApplyBoostCommon()` と `PlayBoostSideEffects()`：クールダウン、ダメージ、HUD、音、振動、エフェクトを共有。

[`MorphBallTouchRules.hpp`](https://github.com/Zection6V/Fruity-Prime/blob/develop11_netCode/src/MphRead.Native/Entities/Players/MorphBallTouchRules.hpp) の現行定数：

```cpp
inline constexpr float TouchRollPerDsPixel = 5.0F / 4096.0F;
inline constexpr std::int32_t TouchBoostThresholdSquared = 8100;
```

閾値は「**4サンプル分のDS座標差の合計ベクトルの長さが90を厳密に超える**」である。旧文書の「120度フリック判定」や `MouseFlick::Check()` を基準としない。4サンプルが30Hzなので履歴期間の目安は約133.3msである。

タッチを使用した場合のBoost分岐：

- `Shoulder`：通常のSpaceチャージ／解放を進める。
- `TouchBoost`：一度だけフリックBoostを発動する。
- `SkipShoulder`：タッチ接触が継続中だが閾値未満などの状況でショルダー側を実行しない。

**特に重要**：`SkipShoulder` は `AdvanceShoulder()` 自体を飛ばす。このため、Shiftの仮想押下を追加するだけでは、マウスを動かし続けた際にチャージや自動解放が止まる可能性がある。

### 2.3 Boostは既存の単一状態機械へ移行済み

[`MorphBallBoostStateMachine.hpp`](https://github.com/Zection6V/Fruity-Prime/blob/develop11_netCode/src/MphRead.Native/Entities/Players/MorphBallBoostStateMachine.hpp)・[`.cpp`](https://github.com/Zection6V/Fruity-Prime/blob/develop11_netCode/src/MphRead.Native/Entities/Players/MorphBallBoostStateMachine.cpp) の構造：

```cpp
struct State
{
    bool Boosting = false;
    bool CanTouchBoost = true;
    std::uint16_t Charge = 0;
};

struct Inputs
{
    bool TouchDown = false;
    bool TouchContinued = false;
    std::int16_t TouchDelta4X = 0;
    std::int16_t TouchDelta4Y = 0;
    bool ShoulderHeld = false;
};

enum class Fired : std::int32_t
{
    None,
    TouchBoost,
    ShoulderBoost
};
```

`AdvanceSample()` は `SampleLatch` により30Hzタッチサンプルの分岐を保持し、ショルダー側は60Hzの各ステップで処理する。`AdvanceShoulder()` は押下中に `Charge` を増加し、解放時に `Charge > Min` なら発動してチャージを0にする。

ここに**もう一つ独立したBoost発射処理を作らない**。Shiftはショルダー入力の**調停ロジック**として統合する。

### 2.4 入力設定の現在値

[`PlayerInput.hpp`](https://github.com/Zection6V/Fruity-Prime/blob/develop11_netCode/src/MphRead.Native/Entities/Players/PlayerInput.hpp)・[`PlayerInput.cpp`](https://github.com/Zection6V/Fruity-Prime/blob/develop11_netCode/src/MphRead.Native/Entities/Players/PlayerInput.cpp)：

- `PlayerControls::_all` は `std::array<..., 35>`。
- `Boost` の既定は `Space`。
- `AdventureMapLegend` の既定は `LeftShift`。
- `HoldMorphBoost` は存在しない。

[`InputSettings.hpp`](https://github.com/Zection6V/Fruity-Prime/blob/develop11_netCode/src/MphRead.Native/Mods/InputSettings.hpp)・[`InputSettings.cpp`](https://github.com/Zection6V/Fruity-Prime/blob/develop11_netCode/src/MphRead.Native/Mods/InputSettings.cpp)：

- `_bindings` / `Bindings()` / `FindBindings()` は35要素。
- `_order` は16要素。
- `AdventureMapLegend` は内部名が変わっているが、保存名は**互換性のため `HudOverlay` のまま**。
- 保存ファイルは `controls.txt`。`aim_style=modern|classic` が存在する。
- `ClassicAim()` の現在の実装では通常時の狙い入力は60Hzの各ステップに適用され、厳密なROM周期の例外は `-nativeaim` による `RomCadence` である。**Aimの設定とMorph Ballの30Hzタッチサンプルは別物**なので混同しない。

### 2.5 オンライン同期は旧文書で想定していたより重要

現行経路：

1. [`NetPlayerIntent.cpp`](https://github.com/Zection6V/Fruity-Prime/blob/develop11_netCode/src/MphRead.Native/Mods/Network/Gameplay/Player/NetPlayerIntent.cpp) は `controls.Boost().IsDown()` を `IntentButtons::Boost` に格納。
2. [`NetInputHistory.cpp`](https://github.com/Zection6V/Fruity-Prime/blob/develop11_netCode/src/MphRead.Native/Mods/Network/Gameplay/Player/NetInputHistory.cpp) は `Boost().IsPressed()` を履歴に記録。
3. [`NetPlayerBridge.cpp`](https://github.com/Zection6V/Fruity-Prime/blob/develop11_netCode/src/MphRead.Native/Mods/Network/Gameplay/Player/NetPlayerBridge.cpp) は受信した `IntentButtons::Boost` をリモートのBoostキー状態へ反映。`RecordPresses()` は `Boost().IsReleased()` を見てチャージ情報をラッチ。
4. `IntentPacket` はタッチの `TouchReport()` も送る。

したがって、**ローカル `ProcessBoost()` だけで仮想Shift解放を発生させてもオンラインの正しい実装にはならない**。サーバーが見ているBoostの論理押下・解放・チャージ情報も一致させる必要がある。

### 2.6 旧実装との差分

| 2026-10-05版の記述 | 2026-10-13版の扱い |
|---|---|
| `MouseFlick.cpp` / `PlayerEntityMouseFlick.cpp` を編集 | そのファイル群は現行 `Mods/Input` に存在しない。`PlayerMorphBall` / `MorphBallTouchRules` を使う |
| モーフ中はマウス移動でsteerできない | 現行のタッチ経由でsteer可能。**新設不要** |
| `_swipeBoostX/Y` や `_boostAimLock = 18` を変更 | 現行の該当Boost経路に存在しない。**新設・移植しない** |
| `HudOverlay` というactionを新名称へ変更 | 内部名 `AdventureMapLegend` は既に存在。**保存名 `HudOverlay` は残す** |
| 一律30HzのAim処理を想定 | 現行Aimは通常60Hz、Morphのネイティブタッチは30Hz。別々に検証 |
| `Boost` を仮想ボタンにするだけ | `SkipShoulder` 調停とネットワークのrelease再現まで必要 |
| Nativeテストが存在しない | `FruityPrime.NativeTouchState` 等の既存テストへ追加 |

## 3. 完成後の操作仕様（変更しない要求）

| 入力 | Samus Morph Ball中の効果 |
|---|---|
| WASD | 従来のロール移動 |
| マウス移動 | カメラ基準のタッチロール相当のステアリング |
| マウス高速移動 | Shiftを押していない場合、既存のタッチフリックBoost |
| Space押下 | 通常のショルダーBoostチャージ |
| Space解放 | 通常のショルダーBoost発動（条件を満たす場合） |
| Shift長押し | 自動チャージ→自動解放→再チャージ→再発動を継続 |
| Shift＋マウス移動 | 自動Boostを継続しながらロール操作可能。フリックBoostは抑止 |
| Space＋Shift | Spaceの手動チャージを優先。Shiftが勝手に途中解放しない |
| Shift解放 | 自動サイクルを中止。**Shiftを離しただけで新たなBoostを誤発射しない** |
| 他HunterのAlt Form | 変更なし |

- Shift長押しは**Samus専用**。Kanden、Trace、Sylux、Noxus、Spire、Weavelへ適用しない。
- 現行Boostのダメージ、速度、衝突判定、チャージ閾値、演出、クールダウン値は維持する。
- Pause、メニュー、フォーカス喪失、ブロック付きカメラシーケンス、死亡、リスポーン、Spectateなどで自動入力を持ち越さない。
- OSのキーリピートではなく、各シミュレーションステップで保持状態を評価する。

## 4. P0：`HoldMorphBoost` actionの追加と互換設定

### 4.1 変更対象

- `src/MphRead.Native/Entities/Players/PlayerInput.hpp`
- `src/MphRead.Native/Entities/Players/PlayerInput.cpp`
- `src/MphRead.Native/Mods/InputSettings.hpp`
- `src/MphRead.Native/Mods/InputSettings.cpp`
- 必要ならQt側の入力binding画面（既存 `Bindings()` を列挙している場合は重複登録しない）

### 4.2 新しい独立アクション

推奨する内部名は `HoldMorphBoost`。`Boost`（Space）を改名・流用しない。

作業内容：

1. `PlayerControls` に `_holdMorphBoost` と `HoldMorphBoost()` accessorを追加。
2. コンストラクター引数、initializer、`_all` に1要素追加。
3. 現時点の35要素固定箇所を36へ更新。要素順・重複・初期化漏れをテスト。
4. `PlayerControls::CreateDefault()` に `HoldMorphBoost = LeftShift` を追加。`Boost = Space` は不変。
5. `InputSettings` の35要素固定を36へ、表示順 `_order` の16要素を17へ更新。
6. `FindBindings()` に保存名 `HoldMorphBoost` を追加、`_order` で `Boost` の直後へ配置。
7. `ActionName()` の表示は **`Hold morph boost`** またはより分かりやすい **`Hold for repeated morph boost`** とする。表示名と保存名を混同しない。
8. `Load` / `Save` / `Reset` / `Rebind` / `Apply` / `ApplyToPlayers` の全経路を確認する。

**将来改善案**：固定サイズ `35`/`36` や `16`/`17` の分散を減らすため、共通定数または `std::to_array` 相当で重複する個数をなくす。ただし本作業で入力設定基盤を広く再設計しない。

### 4.3 `AdventureMapLegend` とLeftShiftの衝突

現在の内部名は `AdventureMapLegend`、旧保存名は `HudOverlay`。原文の `HudOverlay` を新しい内部名として復活させない。

新規設定時：

```text
Boost                = Key:Space
HoldMorphBoost       = Key:LeftShift
HudOverlay           = Key:Unknown   # AdventureMapLegend の互換保存名
```

`AdventureMapLegend` は削除せず、ユーザーが自由に再割り当てできるようにする。

### 4.4 旧 `controls.txt` の1回限りmigration

新しい `HoldMorphBoost` 保存行が**存在しない**旧ファイルのみ対象とする。

| 旧設定 | 変換後 |
|---|---|
| `HudOverlay=Key:LeftShift` かつ `HoldMorphBoost` 行なし | `HoldMorphBoost=LeftShift`、`AdventureMapLegend=Unknown` |
| `HudOverlay` が別キーにカスタマイズ済み | その設定を維持し、HoldMorphBoostの新既定値LeftShiftを採用 |
| 既に `HoldMorphBoost` 行がある | migrationしない。明示設定を優先 |
| `HoldMorphBoost=Key:Unknown` | 無効化という意思を尊重し、Shiftに戻さない |
| 設定ファイルが存在しない | 新規既定値を採用 |

実装上の注意：

- `Load()` が途中で値を処理するだけでなく、**該当行の有無**を保持してファイル全体を読んだ後に判定する。
- `HudOverlay` の値は `AdventureMapLegend` の現在の保存別名として判定する。
- 設定書き出し時に旧名の重複行を増やさない。現在の「未知キーを保存時に引き継ぐ」仕組みとの相互作用を確認。
- 再起動後にmigrationを繰り返さない。
- Shiftと他のキーの競合を一般的に黙って上書きする仕組みは導入しない。

## 5. P0：SRPに沿ったShift Boost調停

### 5.1 追加する責務は「自動発射」ではなく「実効ショルダー入力の決定」

`MorphBallBoostStateMachine::AdvanceShoulder()`、`ApplyShoulderBoost()`、`ApplyBoostCommon()` は既に完成したBoost経路である。これらを複製しない。

推奨する小さな純粋ルール：

```text
src/MphRead.Native/Mods/Input/HoldMorphBoostRules.hpp
src/MphRead.Native/Mods/Input/HoldMorphBoostRules.cpp  # 実装が必要な場合のみ
```

役割：手動Boost押下、Shift保持、チャージ、クールダウン、入力許可フラグから**その60Hzシミュレーションステップの実効ショルダー押下状態**を決定する。PlayerEntityの物理、SE、HUD、ネットワーク送信、ファイルI/Oは行わない。

概念インターフェース（**設計案。現行コードに存在するAPIではない**）：

```cpp
struct HoldMorphBoostInput
{
    bool ManualHeld = false;
    bool ShiftHeld = false;
    bool Allowed = false;
    std::uint16_t Charge = 0;
    std::uint16_t ChargeMinimum = 0;
    std::uint16_t Cooldown = 0;
    bool Boosting = false;
};

struct HoldMorphBoostDecision
{
    bool ShoulderHeld = false;
    bool AutoRelease = false;
    bool PreferShoulderBranch = false;
};
```

**設計要件**：

- `ManualHeld=true` の間は必ず `ShoulderHeld=true`。Shiftは自動解放できない。
- `Allowed=false` の時はShift側の自動入力を生成しない。通常のSpace経路も従来のブロック規則に従わせる。
- Shift保持中、チャージ不足なら `ShoulderHeld=true`。
- チャージが最小閾値を**厳密に超過**し、必要なbusy/cooldown解除条件が満たされれば、**1シミュレーションステップだけ** `ShoulderHeld=false` を出す。
- Shift保持が続き、チャージが既存処理で0になった次のステップは再度 `ShoulderHeld=true`。独自のBoost物理タイマーや二重のchargeを作らない。
- Shiftを離したとき、**Shiftによる仮想押下を解除するだけで閾値超過チャージが不意に発射されないようにする**。特に前ステップで仮想押下していた場合のrelease edgeを明示的に処理すること。必要なら「Shiftでチャージした分をcancelする」または明確に定義した遷移状態を使い、テストで固定する。
- `Boosting` と `_altAttackCooldown` は異なる概念。`Boosting` は [`PlayerProcess.cpp`](https://github.com/Zection6V/Fruity-Prime/blob/develop11_netCode/src/MphRead.Native/Entities/Players/PlayerProcess.cpp#L771-L775) で速度条件により解除される。自動発射のbusy条件へ無検証で流用しない。
- 閾値は `Charge > 2 * _values.BoostChargeMin`。`>=` と書き換えない。
- 数値が足りない場合の自動発射間隔は、実ゲーム比較で決定する。`_altAttackCooldown` を候補にするが、手動Boostの物理仕様を変更しない。

### 5.2 原文の疑似コードをそのまま使わない理由

原文は以下の概念だった：

```cpp
// 旧設計の概念。現行コードへそのまま貼らない。
const bool release = shiftHeld && !spaceHeld && chargeEnough && cooldown == 0;
const bool effectiveHeld = spaceHeld || (shiftHeld && !release);
```

これは**物理的なShift解放時の誤発射**と、現行 `AdvanceSample()` の **`SkipShoulder` 分岐**を扱っていない。

新設ルールは少なくとも次の状態遷移まで定義すること：

```text
Inactive
  ├─ Shift押下かつ合法なSamus AltForm → Charging
  └─ Space押下 → 手動Boost（既存経路）
Charging
  ├─ Space同時保持 → ManualOverride（自動解放禁止）
  ├─ チャージ不足またはbusy → Charging
  ├─ チャージ十分かつ発射可能 → ReleaseOneStep
  └─ Shift解放／入力喪失 → CancelAutoCharge
ReleaseOneStep
  ├─ 既存ShoulderBoost処理で1回だけ発動 → Charging
  └─ Shiftが離されていれば → Inactive
ManualOverride
  ├─ Spaceを保持 → 手動チャージを保持
  └─ Spaceを離す → 明示的な手動解放規則に従う
CancelAutoCharge
  └─ 非意図的なboostを出さず、次回の新しい入力を待つ
```

名前は実装都合で変更可能。**状態が必要なら最小のローカル状態だけ**を保持し、ゲーム共通のチャージ・クールダウンをコピーしない。

### 5.3 Samus限定の入力ゲート

既存の `PlayerEntity` 入力ゲートに沿って、以下を考慮する。

- HunterがSamus。
- 実際のAltForm中で、Morphing/Unmorphingなど不適切な移行中ではない。
- 生存中・行動可能・フリーズなど既存制約に違反しない。
- ローカルプレイヤー本人の物理入力からのみShiftの新規actionを読む。
- Botと受信側リモートプレイヤーはShiftキーを直接解釈しない。
- ポーズ、チャット・メニュー、フォーカス喪失、カメラシーケンスの `BlockInput`、FrameAdvance、Spectateなどの既存制約に従う。
- WeaponMenuOpen / NoAimInput の扱いは、既存の手動Boostより意図せず強くならないよう比較。

`PlayerInput::Suspend()`、`PlayerControls::ClearAll()`、リスポーン・ルーム移行・スロット世代のリセット点も確認すること。自動サイクルの状態が次のlifeへ漏れてはならない。

## 6. P0：`MorphBallBoostStateMachine` とタッチ分岐の優先順位

### 6.1 変更を加える正しい場所

- `PlayerMorphBall.cpp::ProcessBoost()`：入力調停の**結合点**。
- `MorphBallBoostStateMachine.hpp/.cpp`：`Shoulder` / `TouchBoost` / `SkipShoulder` の純粋な分岐処理。
- `MorphBallTouchRules.hpp/.cpp`：現在のDSタッチ由来の判定を維持。閾値は変更しない。

### 6.2 優先順位（必須）

1. `HoldMorphBoost` が有効でない場合、タッチ分岐を**完全に現状どおり**処理。
2. Shiftサイクル中は `PreferShoulderBranch=true` とし、`TouchBoost` と `SkipShoulder` が自動チャージ／解放を妨げないようにする。
3. `PreferShoulderBranch=true` は**Boostの分岐だけ**に作用し、`ApplyTouchRoll()` を停止しない。
4. Shift中のMouse FlickはBoostを重複発射しない。Shiftを離した後は既存のTouchBoostを再び利用できる。
5. Space＋Shiftの場合はSpaceが優先であり、ショルダー側のチャージを途中発射しない。
6. タッチに触れていないフレームで `CanTouchBoost` を再armする既存挙動を残す。
7. `SampleLatch` が30Hzサンプルの2ステップで二重発火を防ぐ性質を保持する。Shiftがサンプルの中間ステップで押されたり離されたりするケースも処理する。

実装例は以下の**意味論**を満たすこと。これはそのまま置換できる完成コードではない。

```text
local owner input
    manual Space / hold Shift
           │
           ▼
HoldMorphBoostRules（60Hz、必要なら最小遷移状態）
    effective shoulder held
    prefer shoulder branch
           │
           ▼
MorphBallBoostStateMachine::AdvanceSample
    │         │
    │         └─ 通常タッチ時だけTouchBoost / SkipShoulder
    └─ 自動Boost時はShoulder経路
           │
           ▼
PlayerMorphBall::ApplyShoulderBoost / ApplyTouchBoost
           │
           └─ 既存ApplyBoostCommon / SE / HUD / damage

NativeTouchState ──► ApplyTouchRoll（Shift保持中も維持）
```

`AdvanceSample` の引数を追加する場合は `Inputs` 側に意図の分かる `PreferShoulderBranch` などを追加し、外部から条件を暗黙的に隠さない。無関係なHunterの既存振る舞いを変更しない。

### 6.3 二重Boostの抑止

同一シミュレーションステップに `TouchBoost` と `ShoulderBoost` を両方返してはならない。`Fired` は現行の単一enum値を維持する。

次の規則をテストする。

```text
Shift OFF ＋ 大きなタッチスワイプ        → TouchBoost 1回
Shift ON  ＋ 大きなタッチスワイプ        → TouchBoost 0回、自動ShoulderBoostのみ
Shift ON  ＋ ゆっくり継続的なタッチ入力 → Shoulderのchargeが止まらない
Shift OFF ＋ ゆっくり継続的なタッチ入力 → 既存SkipShoulderと等価
Shift ON  ＋ Space保持                  → manual charge優先
Shift OFF直後の同じnative sample       → 過去のTouchBoostを再発火しない
```

## 7. P0：ネットワーク整合性の確保

### 7.1 オーナーの実効Boost入力とauthorityのBoost入力を揃える

**禁止**：ローカルだけ `HoldMorphBoostRules` を使い、`NetPlayerIntent::CaptureState()` では未加工の `controls.Boost().IsDown()` を送る構成。

最低限、以下の**同一論理入力**が参照されるようにする。

- ローカル `ProcessBoost()` の `ShoulderHeld`
- `NetPlayerIntent::CaptureState()` の `IntentButtons::Boost`
- `NetInputHistory::Pressed()` で記録する論理的な押下
- `NetPlayerBridge::RecordPresses()` の解放とチャージラッチ
- authorityが `NetPlayerBridge::ApplyIntent()` で再生するBoost入力

推奨：ローカルの1シミュレーションステップにつき一度だけ実効入力を決定し、**読み取り専用のステップスナップショット**としてゲームプレイとネットワークの双方から参照する。物理キーバインド自体をネットワーク都合で上書きしないこと。既存のモジュール境界に合う最小限の `Mod` アクセサー等を用いる。

責務分離：

- 実際にShiftが押されているか：ローカル入力層。
- そのステップの実効Boost入力：共通調停ルール。
- 実際のBoost発生：既存状態機械。
- 送信パケット化：既存ネットワーク層。

**順序を必ず検証**：入力決定 → ローカル物理 → release時のチャージ記録 → 送信。現在の呼び出し順と一致しているか、コードとテストで確認すること。単に双方が同じ関数を呼ぶだけでは更新順序競合が起こり得る。

### 7.2 既存のpress historyだけでreleaseを保証できると決めつけない

現在の `NetInputHistory` はBoostの**press edge**を保存する。一方、チャージBoostの発動は**release edge**が重要である。

Shift自動サイクルの仮想解放が60Hzの**1フレームのみ**だと、パケット欠落・送信間引き・古いpacketの置き換えによって `down → up → down` の `up` が受信側で消える可能性がある。この場合、authorityで発射しないか、ローカルと時刻がずれる。

これをP0として検証し、**解放が損失環境でも再現できることを完了条件にする**。

推奨手順：

1. 現在の通信フレーム周期、Intent収集順序、サーバー適用順序、欠落処理を確認する。
2. 60Hzの仮想releaseを1個落としたケースでも発射・チャージ・ダメージが一致するか、ネットワーク統合テストで検証する。
3. 現行payloadだけでは保証できない場合は、**Boost解放イベントを連番／life世代／発射フレーム／使用チャージ付きで冪等転送する方式**を検討し、必要な最小限のprotocol変更を別途明記して実装する。
4. イベントを導入した場合、順序逆転・重複・再送・旧lifeの遅延・authority再適用を含むテストとprotocol golden更新を行う。

既存の `IntentButtons::Boost` があるというだけの理由で「protocol変更不要」と宣言してはならない。**ただし、検証なしにむやみに新しい通信フィールドも増やさない**。

### 7.3 タッチ入力とショルダー入力が別々に届く場合

現行 `IntentPacket` は `Boost` と `TouchReport()` を別情報として運んでいる。サーバー側でも `PreferShoulderBranch` の選択とタッチの `SampleSequence` を揃える必要がある。

- Shift専用キーはサーバーへそのまま送信する必要はない。**発射意図を忠実に再現できるデータ**を送る。
- 可能なら実効Boostレベルと分岐意図を既存情報から再現する。
- それが不可能なら、実効入力やBoostイベントを送信データへ追加する仕様を明記する。
- `CanTouchBoost` / `Boosting` / `Charge` をネットワークで安易に二重管理しない。
- `NetSmoothing` はリモート表示の補間であり、Boostのauthority判定・発射イベントを描画FPSに紐付けない。

## 8. P1：現行マウスステアリングの品質・感度を確認

### 8.1 二重実装をしない

現行経路：

```text
PointerDevice / PointerInput
    → PlayerInput::_mouseDeltaX/Y
    → TouchInputAdapter
    → MouseMotionTouchSource（0.25 DS unit/host px）
    → NativeTouchState（30Hz、4サンプル合計）
    → MorphBallTouchRules::TouchRollStep（60Hzへ按分）
    → PlayerMorphBall::ApplyTouchRoll
    → speedDelta
```

現行 `ProcessAlt()` にはすでに `ApplyTouchRoll(speedDelta)` がある。原文の `MorphBallMouse` / `PlayerEntityMorphBallMouse` をそのまま追加して **`_input.MouseDeltaX/Y` を別途 `speedDelta` へ加算すると二重移動する**。これは禁止。

### 8.2 30Hz由来の操作感は実測する

- 60Hzの各シミュレーションステップへロール量を半分ずつ振り分けている。
- タッチの**新しい方向情報**が入るのは30Hzのnative tick。
- レンダリングFPSが240/540/1000Hzでも、入力物理のnative sampleはそのまま30Hzである。

高FPSでマウス追従が不自然な場合のみ、**モダン入力専用の60Hzモーフステアリング**を追加するか検討する。その際は：

1. Classic/ROM互換の30Hzタッチロールは残す。
2. 追加の60Hz入力と既存 `ApplyTouchRoll` は**排他的な選択**にする。加算して二重入力しない。
3. TouchBoostの4サンプル履歴・発火閾値は維持する。
4. Mouse sensitivityとInvertX/Yの適用位置を明確化。物理スワイプ方向とユーザー設定の反転は混同しない。
5. オーナーとauthorityで同じ移動量になるよう、送信データと物理計算の整合をテストする。
6. 30/60/120/240/540Hzの描画、60Hzのゲームプレイ、異なるマウスポーリングレートを区別して検証する。

**今回のP0には60Hz新方式の導入を含めない**。原文の「マウス継続操作」を既存実装で満たすか実ゲームで先に確認し、必要になった場合だけ別フェーズで追加する。

### 8.3 Invert設定・方向の注意

`MouseMotionTouchSource` は現状、生の相対移動をDSタッチへ変換しており、`InvertMouseX/Y` をこの経路で直接読まない。Invertを反映させる場合、ロールとスワイプBoostの両方の方向が意図せず反転しないよう**原文の「ステアリングだけ反転、フリックBoostはphysical swipe directionを維持」**をテストで分離する。既存の30Hzタッチ入力の意味をむやみに書き換えない。

## 9. P1：性能、SRP、DRY、設計品質

### 9.1 SRP

- `HoldMorphBoostRules`：純粋な押下／解放判断のみ。
- `MorphBallBoostStateMachine`：既存のcharge / branch / fired eventのみ。
- `PlayerMorphBall`：PlayerEntityと純粋ルールの結合、速度・SE等の既存処理。
- `InputSettings`：バインド保存・読み込み・migrationのみ。
- `NetPlayerIntent`：ゲーム操作の転送表現のみ。

### 9.2 DRY

- Boost速度・ダメージ・音・HUD・コントローラー振動をShift用にコピーしない。
- `_boostCharge` と `_altAttackCooldown` の第二のコピーを設けない。
- TouchRollのカメラbasis計算を新規複製しない。
- `HoldMorphBoost` を `Boost` と別設定actionとして保存するが、物理は既存Boostを共有する。

### 9.3 Hot path制約

`ProcessAlt()` / `ProcessBoost()` / 入力調停の毎ステップ経路は：

- `noexcept` 化できるpure helper、小さいstruct・scalar、決定的な分岐を優先。
- 新規動的メモリ確保、mutex、ファイルI/O、platform API、常時文字列フォーマットを禁止。
- `DebugLog` は有効時のみ。
- フレームレートに比例したチャージ増加を導入しない。60Hzシミュレーション固定で計算する。
- C++20規約に従い、対応する `.hpp` は `.cpp` と同じディレクトリに配置する。
- 既存 `CMakeLists.txt` は `src/MphRead.Native` の `.cpp/.hpp` を `GLOB_RECURSE CONFIGURE_DEPENDS` で収集する。既存source collectionを尊重する。

## 10. テスト指示（実装時に必須）

### 10.1 純粋ルールのテーブルテスト

最低限：

| Space | Shift | Charge | Cooldown | 期待結果 |
|---|---|---:|---:|---|
| OFF | OFF | 0 | 0 | 自動入力なし |
| ON | OFF | 0 | 0 | 手動チャージ |
| OFF | ON | 0 | 0 | 自動チャージ開始 |
| OFF | ON | Min以下 | 0 | 自動チャージ継続 |
| OFF | ON | Min超 | 正値 | 設計したbusy条件に従って待機 |
| OFF | ON | Min超 | 0 | 有効なreleaseを1回発生 |
| ON | ON | Min超 | 0 | Space優先、勝手にreleaseしない |
| OFF | ON | Max到達 | 正値 | オーバーフローなし、待機 |
| OFF | ON→OFF | Min超 | 0 | **キーを離しただけで誤発射しない** |
| OFF | ON保持 | 発射後0 | 正値 | 再チャージへ戻り、3回以上継続 |

これに加え、Shiftの押下と解放が**30Hzタッチサンプルの第1・第2ステップのどちらで起こっても**正しく動くことを検証する。

### 10.2 既存NativeTouchのテスト拡張

既存ファイル：[`Testing/TestNativeTouchState.cpp`](https://github.com/Zection6V/Fruity-Prime/blob/develop11_netCode/src/MphRead.Native/Testing/TestNativeTouchState.cpp)

既存CTest名：`FruityPrime.NativeTouchState`

追加するケース：

- Shiftなしの既存 `TouchRoll`、`ShoulderBoost`、`TouchBoost`、`SkipShoulder` が完全一致。
- Shift中は連続タッチ入力でも `Shoulder` のチャージが止まらない。
- Shift中のflickによるTouchBoostは0回、Roll量は従来と一致。
- 1つのnative sampleでBoostが2回発火しない。
- TouchBoostとShoulderBoostの `ChargeSpent`、`CanTouchBoost`、`Boosting` の遷移が合法。
- 30Hzサンプルを2つの60Hzステップへ配布したときに二重適用しない。
- MouseMotionTouchSourceの小数carry、mouse idle lift、サンプル世代変更の既存テストを維持。
- `Charge > Min` の厳密な閾値境界。

### 10.3 設定と入力UIのテスト

- 35→36個の全アクションが重複・欠落なし。
- `Boost=Space`、`HoldMorphBoost=LeftShift`、`AdventureMapLegend=Unknown` の新規設定。
- 旧 `HudOverlay=Key:LeftShift` の移行。
- `HoldMorphBoost` が明記済みの新設定では再移行しない。
- カスタムマップラベルキーは保持。
- `Key:Unknown` を明示保存した場合も尊重。
- Save→Load→Save のround tripで設定が変わらない。
- Reset / Rebind / ApplyToPlayers / Qt画面で表示と実体が一致。
- 通常のキー押下履歴、フォーカス喪失からの復帰、キーリピート無しの保持操作。

### 10.4 オンライン・リプレイのテスト

既存の関連CTest：

- `FruityPrime.IntentTouchPayload`
- `FruityPrime.NetworkInputHistory`
- `FruityPrime.NetworkInputPressReplay`
- `FruityPrime.RemotePlayerChannel`
- `FruityPrime.NetworkProtocol31Golden`（**現行CMakeでの名前**）
- `FruityPrime.NativeTouchState`

必須統合ケース：

1. オフラインSamusでShift長押しBoostを3回以上発動。
2. onlineのowner、authority、remote observerで発射数・発射フレーム・Boost方向・ダメージが一致。
3. Shift保持中、マウス移動とBoostが共存し、remoteにも方向が反映。
4. 50ms / 150ms / 250ms程度の遅延、jitter、複数パケット欠落・順序逆転。
5. **1フレームしかない自動releaseを含むIntentを意図的にドロップ**し、イベントが失われないこと。
6. 同じreleaseを重複受信してもBoostは1回だけ。
7. モーフ直後、死亡直後、リスポーン、部屋切替、ライフ世代更新。
8. Shift＋Space、Shift＋高速フリック、Shift解放時、入力フォーカス喪失。
9. spectator／bot／専用サーバーがShiftキーを自律解釈しない。
10. 既存のネットコード・戦闘・タッチプロトコルのgoldenが壊れない。protocol追加時のみ設計に合わせて更新。

**注意**：原文の「ネットワークプロトコルへ触らない」は、現行のowner/authority同期を考えると絶対条件にはできない。まず既存形式で解決できるか検証し、必須なら仕様を明記した最小変更を許可する。未検証のまま「オンライン対応完了」にしない。

### 10.5 実ゲーム受入試験

**A. Samusの基本操作**

1. WASDロール操作が従来どおり。
2. マウス上下左右でカメラ基準に正しくロール。
3. マウスを止めた後に入力が残留しない。
4. Shiftを押さないフリックBoostの方向・速度・再armが従来どおり。
5. Space手動チャージ／解放が従来どおり。

**B. Shift連続Boost**

1. モーフ直後にShiftを押し続けてBoost可能。
2. 3回以上の自動発射。
3. Shift中にマウスステアリング可能。
4. マウスを高速に振っても二重発射しない。
5. Space＋ShiftでSpaceの手動チャージが勝手に発射されない。
6. Shiftを離すだけでは最後のチャージが不意に発射されない。
7. キーリピート無効でも作動。
8. 60/120/240/540Hz描画設定、VSync ON/OFFで**60Hzシミュレーションに対して同じ発射周期**。

**C. Isolation**

1. Samus以外の6ハンターのAltForm操作と、全ハンターの既存AltAttackの挙動が不変。
2. Biped状態、Bot、remote player、Spectate、メニュー等で新actionが誤動作しない。
3. Windows Raw Input / Qt fallback / touch・pen inputで同一ルールを使い、プラットフォーム固有のBoost物理を作らない。

### 10.6 ビルド・回帰判定

- CMake configure成功。
- `FruityPrime.NativeTouchState` および関連CTest成功。
- 全CTest成功。
- Windows MSVC Release成功。
- Linux GCC/Clangビルド成功。
- Androidを入力action共通化が通る変更として壊していないことを確認。入力のplatform依存分岐を新設する場合はAndroidビルドも必須。
- オンラインのauthorityを含む実機または統合試験でBoost回数一致。
- 未実行項目は「未検証」と明記し、PASSに含めない。

## 11. 推奨変更ファイル（優先順）

| 優先 | パス | 責務 |
|---|---|---|
| P0 | `src/MphRead.Native/Entities/Players/PlayerInput.hpp` | `HoldMorphBoost` action追加 |
| P0 | `src/MphRead.Native/Entities/Players/PlayerInput.cpp` | 既定キー／全action初期化 |
| P0 | `src/MphRead.Native/Mods/InputSettings.hpp` | 配列サイズと宣言 |
| P0 | `src/MphRead.Native/Mods/InputSettings.cpp` | 設定、migration、UI表示順 |
| P0 | `src/MphRead.Native/Mods/Input/HoldMorphBoostRules.hpp` | 新規：自動押下・解放のpure policy（仮称） |
| P0 | `src/MphRead.Native/Mods/Input/HoldMorphBoostRules.cpp` | 必要な場合のみ実装 |
| P0 | `src/MphRead.Native/Entities/Players/PlayerMorphBall.cpp` | 既存Boost state machineへの接続 |
| P0 | `src/MphRead.Native/Entities/Players/MorphBallBoostStateMachine.hpp/.cpp` | SkipShoulder／TouchBoostとの優先順位 |
| P0 | `src/MphRead.Native/Mods/Network/Gameplay/Player/NetPlayerIntent.cpp` | 実効Boost入力の送信 |
| P0 | `src/MphRead.Native/Mods/Network/Gameplay/Player/NetPlayerBridge.cpp` | Boost releaseのラッチ／受信 |
| P0 | `src/MphRead.Native/Mods/Network/Gameplay/Player/NetInputHistory.cpp` | Boost入力の送信履歴と再現性 |
| P0 | `src/MphRead.Native/Testing/TestNativeTouchState.cpp` | Boost状態機械の回帰テスト |
| P0 | `src/MphRead.Native/Testing/` のネットワーク入力テスト | 60Hz release lossとauthorityの整合 |
| 条件付き | `src/MphRead.Native/Mods/Network/` のIntent/protocol関連 | 既存wire formatでrelease保証不能な場合に限る |
| 条件付き | `src/MphRead.Native/Mods/Input/MouseMotionTouchSource.*` | モダン60Hzステアリングを別フェーズで決めた場合のみ |
| 条件付き | `CMakeLists.txt` | 新しいtest targetを追加する場合のみ |

**原則変更しないもの**：Vulkan/OpenGL/RHI/Reflex/Swapchain/Renderer pacing、shader、asset、Hunter physics metadata、C#版の元コード、汎用ネットワーク同期設計の無関係な部分。

## 12. 実装手順（推奨順序）

1. **Baseline固定**：対象ブランチ／SHAを記録し、`PlayerMorphBall`、Boost state machine、InputSettings、NetPlayerIntentを確認。
2. **設定のみ実装**：`HoldMorphBoost` action、保存・読み込み・移行と単体テスト。
3. **pure policy実装**：手動優先、Shiftサイクル、Shift解放の安全性をテーブルテスト。
4. **Boost state machineへ統合**：既存タッチの `SkipShoulder` と両立させる。先にunit testを通す。
5. **オフライン実ゲーム試験**：3回以上の発射、方向、WASD、touch/flick回帰。
6. **ネットワーク経路への統合**：実効入力を送受信・ラッチ・履歴に一貫して反映。
7. **欠落／順序逆転試験**：releaseが消える場合は冪等イベント方式を追加し、テストとprotocol goldenを更新。
8. **CMake/CTest/Windows/Linux等の回帰確認**。
9. **最終監査**：SRP・DRY・hot path・Hunter isolation・サーバー権威・再現性。

この順序で進めると、先に既存タッチの再実装やrenderer改修を行う無駄を避けられる。

## 13. Definition of Done

- [ ] Shiftの独立アクション `HoldMorphBoost` を追加。
- [ ] `Boost=Space` を保持、`HoldMorphBoost=LeftShift` を既定化。
- [ ] `AdventureMapLegend` と競合せず、保存キー `HudOverlay` の互換性を維持。
- [ ] 旧 `controls.txt` の条件付きmigrationと往復テストが成功。
- [ ] SamusのみShift長押しで自動Boostが3回以上継続。
- [ ] Shiftを離しただけで意図しないBoostを発射しない。
- [ ] Space＋Shiftでmanual操作を優先。
- [ ] Shift中も既存MouseMotionTouchSource→ApplyTouchRoll経路でロール可能。
- [ ] Shift中は `TouchBoost` と `ShoulderBoost` が重複発火しない。
- [ ] Shiftを押していない時、既存のDSタッチロール・フリックBoost・手動Boostと等価。
- [ ] `SampleLatch` の同一30Hzサンプルを二重発火・二重加速させない。
- [ ] 60Hzシミュレーションの発射周期が描画FPSに依存しない。
- [ ] Owner、authority、remote observerのBoost回数・方向・状態が一致。
- [ ] Boostの1フレームreleaseがpacket lossで消えない（必要なら冪等イベント）。
- [ ] リスポーン、世代変更、Pause、メニュー、Focus lossで入力状態が漏れない。
- [ ] Bot、Spectate、Samus以外のAltFormに影響なし。
- [ ] Hot pathにallocation／mutex／platform API／ログ文字列生成を追加しない。
- [ ] NativeTouch、InputSettings、network input/history、protocolの自動テスト成功。
- [ ] Windows MSVC Release、LinuxのビルドとCTest成功。
- [ ] 影響する場合はAndroidビルドも成功。
- [ ] 実ゲーム検証・未検証項目を正確に報告。

## 14. 参考ソースリンク

- [現行C++ `PlayerMorphBall.cpp`](https://github.com/Zection6V/Fruity-Prime/blob/aadb028d421cba0bfeb291569cac4b7dff8f8772/src/MphRead.Native/Entities/Players/PlayerMorphBall.cpp)
- [Boost状態機械](https://github.com/Zection6V/Fruity-Prime/blob/aadb028d421cba0bfeb291569cac4b7dff8f8772/src/MphRead.Native/Entities/Players/MorphBallBoostStateMachine.cpp)
- [TouchRoll・TouchBoostの純粋計算](https://github.com/Zection6V/Fruity-Prime/blob/aadb028d421cba0bfeb291569cac4b7dff8f8772/src/MphRead.Native/Entities/Players/MorphBallTouchRules.cpp)
- [MouseMotionTouchSource](https://github.com/Zection6V/Fruity-Prime/blob/aadb028d421cba0bfeb291569cac4b7dff8f8772/src/MphRead.Native/Mods/Input/MouseMotionTouchSource.cpp)
- [TouchInputAdapter](https://github.com/Zection6V/Fruity-Prime/blob/aadb028d421cba0bfeb291569cac4b7dff8f8772/src/MphRead.Native/Mods/Input/TouchInputAdapter.cpp)
- [InputSettings](https://github.com/Zection6V/Fruity-Prime/blob/aadb028d421cba0bfeb291569cac4b7dff8f8772/src/MphRead.Native/Mods/InputSettings.cpp)
- [NetPlayerIntent](https://github.com/Zection6V/Fruity-Prime/blob/aadb028d421cba0bfeb291569cac4b7dff8f8772/src/MphRead.Native/Mods/Network/Gameplay/Player/NetPlayerIntent.cpp)
- [NetPlayerBridge](https://github.com/Zection6V/Fruity-Prime/blob/aadb028d421cba0bfeb291569cac4b7dff8f8772/src/MphRead.Native/Mods/Network/Gameplay/Player/NetPlayerBridge.cpp)
- [NativeTouch自動テスト](https://github.com/Zection6V/Fruity-Prime/blob/aadb028d421cba0bfeb291569cac4b7dff8f8772/src/MphRead.Native/Testing/TestNativeTouchState.cpp)
- [CMake/CTest](https://github.com/Zection6V/Fruity-Prime/blob/aadb028d421cba0bfeb291569cac4b7dff8f8772/CMakeLists.txt)
- 原文の参考：[`melonPrimeDS`](https://github.com/ag-advania/melonPrimeDS) のMorph Ball連続Boost仕様。

---

**最終方針**：現在ある`TouchInputAdapter`、`MorphBallTouchRules`、`MorphBallBoostStateMachine`を正として使用する。モーフ操作を旧版へ戻すのではなく、**独立したShift入力を既存のショルダーBoost状態機械へ統合し、オンラインで消えない発射意図として届ける**。コミット・プッシュはこの指示書の作成範囲外とする。
