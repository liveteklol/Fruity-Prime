# Fruity-Prime C++ `HudOverlay` → `AdventureMapLegend` Rename / UI Clarification Instructions

## 1. 対象

- Repository: `Zection6V/Fruity-Prime`
- Branch: `develop3_rendering`
- 調査時 HEAD: `edf9bb3547069916cce0f81899836bbf7e8afe23`
- 対象: **C++版のみ**
- C#版は比較仕様として維持し、この作業では変更しない

## 2. 目的

現在の C++ 版では、`HudOverlay` という入力アクション名が使用されている。

しかし実際の機能は一般的な「HUD の表示切替」ではない。  
この入力は、アドベンチャーモードのポーズ中ナビゲーションマップで、通常の3Dマップ表示の代わりにマップ凡例を表示するための **hold 操作** である。

現在の名称では以下の誤解が起きる。

- 通常プレイ中の HP / ammo / reticle 等の HUD を表示・非表示にする機能に見える
- `Mods::Render::UiOverlay` のような UI compositing 機能と意味が衝突する
- `PlayerControls::HudOverlay()` だけを見ても、Adventure map 専用の入力であることが分からない
- 設定画面に `Hud overlay` と表示され、ユーザーが用途を推測できない

このため、C++ 内部のドメイン名とユーザー向け表示を以下へ変更する。

### 採用名称

- C++ logical / API name: **`AdventureMapLegend`**
- C++ member: **`_adventureMapLegend`**
- local / constructor parameter: **`adventureMapLegend`**
- Settings UI label: **`Adventure map legend`**
- Default binding: **Left Shift のまま**
- 操作方式: **hold のまま**
- 既存設定ファイルの永続化キー: **`HudOverlay` のまま維持**

`Overlay` という語は使用しない。  
この機能は描画レイヤーや HUD 全般ではなく、「Adventure map の legend を表示する入力」として命名する。

---

## 3. 現在の実装から確認できる事実

### 3.1 デフォルトキーは Left Shift

`src/MphRead.Native/Entities/Players/PlayerInput.cpp`

現在:

```cpp
std::shared_ptr<Keybind> hudOverlay = key(Keys::LeftShift);
```

したがって rename 後も Left Shift を維持すること。

---

### 3.2 hold 操作であり toggle ではない

`src/MphRead.Native/Entities/Players/PlayerPause.cpp`

現在は以下のように `IsDown()` を参照している。

```cpp
if (Controls().HudOverlay().IsDown())
```

したがって入力意味は:

> Hold to show the Adventure map legend.

である。

`IsPressed()` ベースの toggle に変更してはならない。

---

### 3.3 Shift 中は通常のナビマップ描画を抑止する

`DrawPauseMenuBackground()`:

```cpp
if (!RequireReference(_scene).NavMapRoomSymbols() || Controls().HudOverlay().IsDown())
{
    return;
}
```

`GetPauseMapRenderItems()`:

```cpp
if (!_navMapModelEnabled || _drawPauseState != 1 || Controls().HudOverlay().IsDown())
{
    return;
}
```

つまりこの入力は単なる追加 HUD ではなく、ポーズ中の Adventure navigation map 表示モードを **map → legend** へ一時的に切り替える役割を持つ。

---

### 3.4 実際に `MapLegendInfo` を描画している

同じ `PlayerPause.cpp` 内で `Controls().HudOverlay().IsDown()` のときに `_mapLegendInfo` を列挙し、武器・記号等の説明を描画している。

また `PlayerPause.hpp` 側の型名は既に:

```cpp
class MapLegendInfo final
```

となっている。

したがって `AdventureMapLegend` は既存のドメイン語彙とも一致する。

---

## 4. 重要: `controls.txt` 互換を壊さないこと

### 4.1 現在の問題

`InputSettings` の binding descriptor の `Name` は、現在3つの役割を兼ねている。

1. C++ logical action name
2. UI row name / humanize 元
3. `controls.txt` の persisted key

現在:

```cpp
{"HudOverlay", [](Entities::PlayerControls& c) -> Entities::Keybind&
{
    return c.HudOverlay();
}}
```

さらに Save は:

```cpp
lines.push_back(std::string(property.Name) + "=" + value);
```

Load は:

```cpp
return item.Name == key;
```

で一致させている。

そのため `Name` だけを `AdventureMapLegend` に rename すると、既存:

```text
HudOverlay=Key:LeftShift
```

が読み込まれなくなる。

既存ユーザーが HudOverlay を別キーへ rebind している場合、その設定が silently lost するため不可。

---

## 5. 必須設計: logical name と persisted config key を分離する

`InputBindingProperty` に persisted key の概念を追加する。

推奨形:

```cpp
struct InputBindingProperty final
{
    std::string_view Name;
    Entities::Keybind& (*GetValue)(Entities::PlayerControls&);
    std::string_view ConfigKey{};

    [[nodiscard]] std::string_view PersistedName() const noexcept
    {
        return ConfigKey.empty() ? Name : ConfigKey;
    }
};
```

### 意図

通常の binding は従来どおり:

```cpp
{"MoveLeft", getter}
```

とし、`ConfigKey` が空なら `Name` をそのまま保存キーとして扱う。

今回だけ:

```cpp
{
    "AdventureMapLegend",
    [](Entities::PlayerControls& c) -> Entities::Keybind&
    {
        return c.AdventureMapLegend();
    },
    "HudOverlay"
}
```

とする。

これにより:

- C++ 内部名: `AdventureMapLegend`
- Qt row id: `key.AdventureMapLegend`
- UI action label: `Adventure map legend`
- persisted controls key: `HudOverlay`

を同時に成立させる。

### なぜ保存キーを旧名のままにするか

この rename の目的はコードとUIの意味改善であり、設定ファイルフォーマットの破壊的変更ではない。

`HudOverlay` を persisted legacy key として維持すれば:

- 既存 C++ `controls.txt` がそのまま読める
- rename 後に保存しても旧 C++ build から読める
- C# 比較版の既存 `PlayerControls.HudOverlay` / `InputSettings` と共有可能
- migration 用の二重キー・重複行・unknown-key preservation 問題を作らない

将来 `controls.txt` 自体の format migration を行うなら別タスクで実施すること。

---

## 6. ファイル別修正指示

## 6.1 `src/MphRead.Native/Entities/Players/PlayerInput.hpp`

以下を rename。

### Constructor parameter

Before:

```cpp
std::shared_ptr<Keybind> hudOverlay
```

After:

```cpp
std::shared_ptr<Keybind> adventureMapLegend
```

### Accessor

Before:

```cpp
MPHREAD_CONTROL_ACCESSOR(HudOverlay, _hudOverlay)
```

After:

```cpp
MPHREAD_CONTROL_ACCESSOR(AdventureMapLegend, _adventureMapLegend)
```

### Member

Before:

```cpp
std::shared_ptr<Keybind> _hudOverlay{};
```

After:

```cpp
std::shared_ptr<Keybind> _adventureMapLegend{};
```

`_all` の要素数は変更しない。  
35 bindings のまま維持する。

---

## 6.2 `src/MphRead.Native/Entities/Players/PlayerInput.cpp`

constructor parameter / initializer / `_all` を rename。

Before:

```cpp
std::shared_ptr<Keybind> hudOverlay
```

After:

```cpp
std::shared_ptr<Keybind> adventureMapLegend
```

Before:

```cpp
_hudOverlay(std::move(hudOverlay))
```

After:

```cpp
_adventureMapLegend(std::move(adventureMapLegend))
```

Before:

```cpp
_omegaCannon, _affinitySlot, _pause, _hudOverlay
```

After:

```cpp
_omegaCannon, _affinitySlot, _pause, _adventureMapLegend
```

default binding も rename する。

Before:

```cpp
std::shared_ptr<Keybind> hudOverlay = key(Keys::LeftShift);
```

After:

```cpp
std::shared_ptr<Keybind> adventureMapLegend = key(Keys::LeftShift);
```

最後の constructor argument も rename。

### 注意

- `Keys::LeftShift` は変更しない
- binding count / ordering を変更しない
- `ClearAll()` / `ClearPressed()` が `_all` 経由で従来通り作用すること

---

## 6.3 `src/MphRead.Native/Entities/Players/PlayerPause.cpp`

すべての:

```cpp
Controls().HudOverlay()
```

を:

```cpp
Controls().AdventureMapLegend()
```

へ変更する。

少なくとも調査時 HEAD では以下3経路が対象。

1. `DrawPauseMenuBackground()`
2. `DrawPauseMenuForeground()` 内の legend 描画条件
3. `GetPauseMapRenderItems()`

ロジック自体は変更しない。

特に以下を維持する。

```cpp
Controls().AdventureMapLegend().IsDown()
```

`IsDown()` を `IsPressed()` 等へ変更しない。

---

## 6.4 `src/MphRead.Native/Mods/InputSettings.hpp`

`InputBindingProperty` に persisted config key を追加する。

推奨:

```cpp
struct InputBindingProperty final
{
    std::string_view Name;
    Entities::Keybind& (*GetValue)(Entities::PlayerControls&);
    std::string_view ConfigKey{};

    [[nodiscard]] std::string_view PersistedName() const noexcept
    {
        return ConfigKey.empty() ? Name : ConfigKey;
    }
};
```

コメントも修正し、`Name` が「C# reflection property name の機械移植」だけではなく C++ の logical action name であること、`ConfigKey` は旧設定との互換境界であることを明記する。

例:

```cpp
// Name is the native logical action name used by the UI.
// ConfigKey is optional persistent-format compatibility; when empty,
// the logical name is also the controls.txt key.
```

C# reflection の機械移植という説明が現実の責務と食い違うなら、今回ここで整理すること。

---

## 6.5 `src/MphRead.Native/Mods/InputSettings.cpp`

### `_order`

Before:

```cpp
"WeaponMenu", "ScanVisor", "Pause", "HudOverlay"
```

After:

```cpp
"WeaponMenu", "ScanVisor", "Pause", "AdventureMapLegend"
```

### binding descriptor

Before:

```cpp
{"HudOverlay",
    [](Entities::PlayerControls& c) -> Entities::Keybind&
    {
        return c.HudOverlay();
    }}
```

After:

```cpp
{"AdventureMapLegend",
    [](Entities::PlayerControls& c) -> Entities::Keybind&
    {
        return c.AdventureMapLegend();
    },
    "HudOverlay"}
```

### Load

現在:

```cpp
return item.Name == key;
```

を persisted key 経由へ変更。

After:

```cpp
return item.PersistedName() == key;
```

### Save

現在:

```cpp
lines.push_back(std::string(property.Name) + "=" + value);
```

を:

```cpp
lines.push_back(std::string(property.PersistedName()) + "=" + value);
```

へ変更する。

### ActionName

`AdventureMapLegend` は現在の camel-case humanizer で:

```text
Adventure map legend
```

になるため、原則 special case は不要。

期待UI:

```text
Adventure map legend    Left Shift
```

既存 UI の sentence case (`Alt attack`, `Scan visor` 等) に合わせる。

**`HUD overlay` / `Hud overlay` という表示を残さないこと。**

---

## 6.6 `src/MphRead.Native.Qt/Shell/SettingsModel.cpp`

原則として専用 hard-code は不要。

現在:

```cpp
key.Id = QStringLiteral("key.") + Q(bindings[i].Name);
key.Label = Q(Mods::InputSettings::ActionName(bindings[i]));
```

なので descriptor の logical `Name` を `AdventureMapLegend` に変えることで:

```text
key.AdventureMapLegend
Adventure map legend
```

になる。

### 確認事項

repository 全体で `key.HudOverlay` を直接参照している QML / C++ がないことを確認する。

存在する場合は:

```text
key.HudOverlay
```

から:

```text
key.AdventureMapLegend
```

へ変更する。

ただし persisted `controls.txt` key はこれとは別であり、`HudOverlay` のまま。

---

## 7. C#版は変更しない

以下 C# API は今回の scope 外。

```csharp
PlayerControls.HudOverlay
Controls.HudOverlay.IsDown
```

C# は比較仕様として残す。

C++ 側がより明確な native naming を採用しても、挙動 parity は維持できる。

つまり:

```text
C# source semantic name: HudOverlay
          ↓ C++ port semantic clarification
C++ logical name: AdventureMapLegend
```

という関係を許容する。

「C# と識別子まで完全一致させるため」という理由で C++ を再び `HudOverlay` に戻さないこと。

---

## 8. Persistence compatibility test

この rename では以下を必須 regression gate とする。

### Test A: old config load

入力:

```text
HudOverlay=Key:Q
```

Load 後:

```cpp
InputSettings::Current().AdventureMapLegend().Key() == Keys::Q
```

であること。

### Test B: save compatibility

`AdventureMapLegend` を例えば `Keys::Q` に rebind して Save。

出力には:

```text
HudOverlay=Key:Q
```

が存在すること。

以下は出力しないこと:

```text
AdventureMapLegend=Key:Q
```

今回の task では config format migration は行わない。

### Test C: round trip

1. old `HudOverlay=Key:Q` を Load
2. Save
3. 再 Load

で Q binding を維持すること。

### Test D: no duplicate legacy line

Save 後に:

```text
HudOverlay=
```

が複数行生成されないこと。

---

## 9. Runtime behavior regression gate

Adventure pause map で確認する。

### 通常

Left Shift を押していない:

- 通常 navigation map が描画される
- map render items が生成される
- legend は表示されない

### Hold

Left Shift を押している:

- 通常 navigation map background / render items が抑止される
- `_mapLegendInfo` の legend が表示される
- weapon / map symbol explanation が表示される

### Release

Left Shift を離す:

- legend が消える
- navigation map が即座に戻る

### 非対象状態

通常 gameplay 中に Left Shift を押しても:

- generic HUD hide/show 機能として動作しない
- unrelated `UiOverlay` visibility を変更しない

---

## 10. UI acceptance gate

Settings → Controls → Keyboard で確認。

Before:

```text
Hud overlay
```

After:

```text
Adventure map legend
```

binding は:

```text
Left Shift
```

のまま。

ユーザーが名前を見ただけで「Adventure の map legend を表示するキー」だと判断できること。

---

## 11. `UiOverlay` と混同しない

以下は今回の rename 対象外。

```text
src/MphRead.Native/Mods/Render/UiOverlay.cpp
src/MphRead.Native/Mods/Render/UiOverlay.hpp
```

これは launcher / Qt UI surface を scene output に composite する renderer-side overlay であり、Adventure map legend input とは別機能。

`UiOverlay` を `AdventureMapLegend` に rename したり、両者を統合してはならない。

---

## 12. 実装後の静的監査

C++ tree で旧 logical identifier が残っていないことを確認する。

例:

```bash
rg -n "\bHudOverlay\b|\bhudOverlay\b|\b_hudOverlay\b" src/MphRead.Native src/MphRead.Native.Qt
```

許可される `HudOverlay` は **compatibility persisted key の文字列** と、それを説明するコメント・テストのみ。

期待される許可例:

```cpp
"AdventureMapLegend",
...,
"HudOverlay"
```

禁止される残存例:

```cpp
Controls().HudOverlay()
_hudOverlay
std::shared_ptr<Keybind> hudOverlay
```

また新名称を確認:

```bash
rg -n "AdventureMapLegend|adventureMapLegend|_adventureMapLegend" \
  src/MphRead.Native src/MphRead.Native.Qt
```

---

## 13. ビルド / テスト

最低限:

1. C++ native build 成功
2. 既存 CTest 全 pass
3. InputSettings persistence regression test
4. Qt Settings keyboard row 表示確認
5. Adventure pause map hold/release 実機能確認

可能なら InputSettings の persistence test を自動化すること。

既存 test target へ無理に unrelated test を押し込むより、InputSettings 用の小さい targeted native test を追加できる構成ならその方を優先する。

---

## 14. 受入条件

以下をすべて満たしたら完了。

- [ ] C++ `PlayerControls` API が `AdventureMapLegend()` になっている
- [ ] backing member が `_adventureMapLegend` になっている
- [ ] constructor/local name が `adventureMapLegend` になっている
- [ ] `PlayerPause.cpp` が `Controls().AdventureMapLegend().IsDown()` を使用している
- [ ] default binding は Left Shift のまま
- [ ] 操作は hold のまま
- [ ] Settings UI が `Adventure map legend` と表示する
- [ ] C++ UI/runtime logical name に `HudOverlay` が残っていない
- [ ] `controls.txt` persisted key は `HudOverlay` のまま
- [ ] 既存 `HudOverlay=...` rebind を正常に読み込める
- [ ] Save 後も `HudOverlay=...` として保存される
- [ ] duplicate / stale config key を生成しない
- [ ] C#版は変更していない
- [ ] `Mods::Render::UiOverlay` は変更していない
- [ ] Adventure map の map/legend 切替挙動に regression がない
- [ ] C++ build / tests が pass

---

## 15. 禁止する実装

以下は採用しない。

### A. 文字列だけ `AdventureMapLegend` へ変更

既存 `controls.txt` rebind を破壊するため不可。

### B. 新旧両方の config key を毎回保存

```text
HudOverlay=Key:Q
AdventureMapLegend=Key:Q
```

のような二重保存は不可。

source of truth を2つ作らない。

### C. C#版まで rename

今回の対象は C++ 版。比較仕様を不要に揺らさない。

### D. `UiOverlay` と統合

全く別責務。

### E. toggle 化

`IsDown()` による hold semantics を維持する。

---

## 16. 最終設計

最終的な責務分離は以下とする。

```text
PlayerControls::AdventureMapLegend()
        │
        ├─ domain meaning:
        │    Adventure pause map legend hold action
        │
        ├─ UI:
        │    "Adventure map legend"
        │
        ├─ default:
        │    Left Shift
        │
        └─ compatibility persistence:
             controls.txt key = "HudOverlay"
```

これにより、内部コードを読んだ開発者にも設定画面を見たユーザーにも用途が明確になり、既存設定との互換性も維持できる。
