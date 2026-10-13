# Weavel Alt Form EU1.1 — 実装・受入記録

対象: Native C++、`develop6_weavelAlt`。
指示書: `mphCodex/mphAnalysis/Control/WeavelAltForm/Fruity-Prime-Weavel-AltForm-ROM-Parity-Implementation-Guide-EU1_1.md`。
基準HEAD: `387111e84ab642898e8768eb8e8844d8e91c996d`。

## 完了判定

追補監査（対象`c8848e55`）のP1 local snapshot reconciliationを修正し、追加production checksと修正code commitのWindows / Linux / macOS CIが合格した。ユーザーが了承したP2の近似を含む今回の対象範囲はCLOSED。
P2の落下物理・sphere sweep半径はユーザーが「近似でもいい」と明示了承したため、`_ySpeed -= 0.02F` / `0.45F`と既存の近似値テストを維持する。ROM raw演算との完全一致は主張しない。
ユーザー指定によりC# mirrorは対象外。

## 2026-10-08 追補監査への対応

入力資料: `Fruity-Prime-develop6-Weavel-AltForm-Implementation-Audit-EU1_1.md`、`Fruity-Prime-develop6-Weavel-AltForm-Followup-Fix-Instructions-EU1_1.md`、更新された`0211013C-weavel-halfturret-process-target-fire-EU1_1.md`。

| 追補要件 | 対応 / 受入証拠 |
|---|---|
| P1 local ownership policy | `NetPlayerBridge::ApplyState`のWeavel reconciliationを`!isLocal`に限定。localの`LocalHealthFor`、player divergence correction、freeze / affliction処理は従来経路を維持 |
| local morph + stale Biped | `WeavelLocalSnapshotCheck.cpp`で実EnterAltForm中に古いBiped snapshotを適用。Morphingと生存turretを保つことを検証 |
| local Alt + live turret + stale inactive | 生存・HP・位置・groundedをsnapshotで上書きしないことを検証 |
| local Alt + dead turret + stale active | dead turretを再生成しないことを検証 |
| remote active / dead / Biped | 同じsnapshotをremoteとして適用し、二重HP split / mergeなしの従来reconciliationを検証 |
| 3 production entry paths | `ApplyState(..., true/false)`直接、非authority clientの実`NetHooks::AfterInput`、実`NetHooks::AfterSimulation`。hookは`StatesApplied`増加も確認し、早期returnによる見かけの成功を除外 |
| P2 gravity / sweep | ユーザー了承済みの近似。native 30 Hz cadence・reset・着地は既存production checksを維持。raw -81/-162およびraw1843への変更は今回不要 |
| Adventure enemy extension | 対象外。既存extensionと別検証を維持 |
| projectile Wi-Fi authority | 静的証拠境界を維持。推測変更なし |
| local turret HP専用correction API | 追加しない。form intent ackのない現protocolでremote lifecycle APIをlocalへ流用しない |
| build / asset-backed / desktop CI | MSVC Release build成功。P1追加49 checks、既存Weavel421 checks、Adventure extension29 checks、Lockjaw93 checks、関連CTest4/4、Dialanche28 assertions＋独立UDP peers2件成功。修正code commitのdesktop CIも全3 jobs成功（詳細は下表） |

追補のlocal logs: ignored `tools/build/out/weavel-followup-validation/`、`weavel-followup-dialanche/`。
再現: `powershell -NoProfile -ExecutionPolicy Bypass -File tools/check-weavel-alt.ps1 -OutputDirectory tools/build/out/weavel-followup-validation`。

補足のliveハーネス`check-dialanche-network.ps1 -SpireSeconds 8 -TargetSeconds 10`は、今回のbinaryでSpireがAltへ入らず`events=0`のため失敗。同じcommandを保存済み`FruityPrime-SrpCheck.exe`（2026-10-07 19:11:36生成、本修正前、SHA256 `C69ACD669A29476C00D334D53DF43CDE7A697B16B1C32BD085C5580831B05D7B`）で実行しても`events=0`で失敗した。ここはP1成功・regressionなしの証拠に含めない。logs: `weavel-followup-network/`、`weavel-followup-network-baseline/`。一方、上記asset-backed Dialanche28 assertionsと独立UDP peersは今回のbinaryで成功している。

追補の修正code commit: [`0c9ceaa987061782a1ed2f61b8ff2701f2eed2ca`](https://github.com/Zection6V/Fruity-Prime/commit/0c9ceaa987061782a1ed2f61b8ff2701f2eed2ca)。CI: [build_cpp run 37709271668](https://github.com/Zection6V/Fruity-Prime/actions/runs/37709271668)、全15 jobs成功。runのhead SHAが修正code commitと一致することを確認。以下の旧CI runを本修正の合格証拠として流用しない。

| 追補CI job | 実行ログで確認した結果 |
|---|---|
| [Windows / MSVC](https://github.com/Zection6V/Fruity-Prime/actions/runs/37709271668/job/113091068311) | build成功、WeavelAltFormParityを含むCTest 7/7合格 |
| [Linux / GCC](https://github.com/Zection6V/Fruity-Prime/actions/runs/37709271668/job/113091068327) | build成功、WeavelAltFormParityを含むCTest 6/6合格 |
| [macOS / Clang](https://github.com/Zection6V/Fruity-Prime/actions/runs/37709271668/job/113091068476) | build成功、WeavelAltFormParityを含むCTest 6/6合格 |

CI logs: ignored `tools/build/out/weavel-followup-ci-{windows,linux,macos}.log`。この受入記録の追記は文書のみで、上記検証対象コードは変更していない。

### 今回のP1修正のSRP確認

`NetPlayerBridge::ApplyState`はsnapshotをlocal / remoteのどちらへ適用するかという同期方針を担当し、Weavelの明示的なreconciliationをremoteへ限定する。フォーム・turret lifecycleの変更は既存の`PlayerEntity::ModApplyWeavelState`（`PlayerWeavel.cpp`）、turret HP / position / groundedの反映は`HalfturretEntity::ApplyNetworkState`が担当する。bridgeにゲームの状態遷移ロジックは追加していない。

追加のlocal prediction / remote reconciliation検証は専用`WeavelLocalSnapshotCheck.cpp`に分離し、通常のWeavel production checksとAdventure enemy extension checksから独立している。今回の変更について責務の混在は確認されず、追加リファクタリングは不要と判断した。実際の3 entry pathsと49 checksのローカル実行結果を判断根拠とし、desktop CIのCTestとは別の証拠として扱う。

以下のPhase 1～8表とCI記録は**追補前の受入履歴**。local stale snapshotを含む現在の完了判定は上記追補gateを基準にする。

| 要件 | 受入を証明する検証 | 現状 |
|---|---|---|
| Phase 1 raw factor / damage / floor | 6144、6083、5412、3948、2867、巨大damage | production damage hook / helperのCTest合格 |
| Phase 2 fresh spawn / EquipInfo | freeze・burn・target・velocity・Story overrideを設定し再enter。defaultへreset | CTestと実assetのexit/re-enter合格。burn effect unlinkも確認 |
| Phase 3 recovery / thresholds / decision | 54 native ticks、freeze中停止、15/15/13/10/7、odd stepで発射なし | CTestとproduction gameplay統合検証合格 |
| Phase 4 lunge edge / cooldown | cooldown1受理、2拒否、拒否edge消費、hold、odd edge保持、bot/remote共通入口 | production ProcessInputとremote press historyの統合検証合格 |
| Phase 5 form lifecycle | 100/101 split、alive merge、dead継続、重複forceでHP不変 | local force、通常enter/exit、morph/unmorph途中の復旧すべて合格 |
| Phase 6 explicit turret state | active/dead/bipedのround trip、remote activationでsplitなし、HP/position/grounded適用 | wire round trip、production publisher/receiver/replica合格。重複・逆順snapshotと古いlifeを拒否 |
| Phase 6 protocol | version17、旧16拒否、PlayerStateサイズ、複数player境界、MaxPacketSize | 128 bytes/player。実UDP serverが旧16をReasonProtocolで拒否。8人＋56 health spawnersの全配信合格 |
| Phase 7 timers / physics | target30、freeze75/15、burn150/every8、Story65/<60、shot timer、native physics/sweep | native timer、owner u8 shot timer、native gravity/sweep、実地形への着地合格 |
| Phase 8 fixed constants | Story lunge 1228/4096、1843/4096 | production Story bot入力・移動で確認。通常移動のgravity/damping適用後の速度も検証 |
| Windows / Linux / macOS CI | 今回の最終code commitのjobsとtest logs | `1686cf5e7619ca57a4a3f6408f82b09c68a370ce`で全3 jobs成功。Weavelを含むCTestはWindows 7/7、Linux 6/6、macOS 6/6合格 |

projectile authorityの推測変更、LoS追加、touch clock流用は行わない。
手動操作・実機ROM比較、ローカルbuild、asset-backed checks、CIはそれぞれ別の証拠として扱う。

## 追補前のCIと完了監査（履歴）

検証した最終code commit: [`1686cf5e7619ca57a4a3f6408f82b09c68a370ce`](https://github.com/Zection6V/Fruity-Prime/commit/1686cf5e7619ca57a4a3f6408f82b09c68a370ce)。
[build_cpp run 37665965149](https://github.com/Zection6V/Fruity-Prime/actions/runs/37665965149)は全15 jobs成功。
この追記は検証記録のみで、検証対象コードは変更していない。

| CI job | 実行ログで確認した結果 |
|---|---|
| [Windows / MSVC](https://github.com/Zection6V/Fruity-Prime/actions/runs/37665965149/job/112945139223) | build、WeavelAltFormParityを含むCTest 7/7、配布artifact成功 |
| [Linux / GCC](https://github.com/Zection6V/Fruity-Prime/actions/runs/37665965149/job/112945138963) | build、WeavelAltFormParityを含むCTest 6/6、配布artifact成功 |
| [macOS / Clang](https://github.com/Zection6V/Fruity-Prime/actions/runs/37665965149/job/112945139283) | build、WeavelAltFormParityを含むCTest 6/6、配布artifact成功 |
| Android | arm64-v8a / x86_64 native build、APK、emulator startup smoke API 28 / 30 / 35成功。実機のgameplay検証ではない |
| 静的監査 | legacy GL、shader interface、frontend GL、GL分類、RHI隔離、Android build contract成功 |

指示書の番号ごとに現行コードと検証内容を照合した結果:

| 指示書項目 | 実装と受入証拠 |
|---|---|
| §1～3 対象・順序・成果物 | 基準HEADを祖先に持つ`develop6_weavelAlt`。Native C++、CMake / CTest、3 desktop CI、再現scriptと本記録を追加 |
| §4～5 raw factor / final damage | `HalfturretFireRate`とproduction `OnTakeDamage`。6144 / 6083 / 5412 / 3948 / 2867、巨大damageのoverflow防止をCTestで確認 |
| §6～8 fresh spawn / EquipInfo / split | `ResetForSpawn`を`InitializeSpawn`冒頭で実行。burn effect unlink、全transientとStory overridesのdefault復元、実exit/re-enter、100/101のsplitを確認 |
| §9～12 recovery / threshold / native phase | raw +61、54 native ticks、above-normal truncate、15/15/13/10/7。freeze中停止、odd step発射なし、scene共通phaseをCTestとproduction checksで確認 |
| §13～16 input edge / cooldown / hold | `WeavelLungeInput`とproduction timer advance / `ProcessInput`。1受理・2拒否・拒否edge消費・hold・odd edge保持、Story botとremote press historyの共通経路を確認 |
| §17～19 force lifecycle | `ModForceWeavelState` / `FinalizeWeavelForm`。split life、turret生死、morph / unmorphを別に扱い、重複HP不変、alive merge、dead Alt継続、active lunge終了を確認 |
| §20～22 wire / version | 使用済み8-bit Flagsを変更せず独立14-byte fieldsを追加。PlayerState 128 bytes、protocol17、3状態のround trip、旧114-byte layoutと実UDP旧16拒否を確認 |
| §23～25 publish / apply / physical report | production publisher / receiver / `NetPlayerBridge`。authority HP、splitなしreplica activation、HP / position / grounded、古いlife・重複・逆順・非finite reportの拒否を確認 |
| §26 native timers | target30、freeze75/15、burn150 / every8、Story65 / gate<60、owner u8 shot timer。turretが先に処理されてもframe guardで1回のみadvanceすることを確認 |
| §27 native physics | gravity / velocity / position / sphere sweepをnative tickに1回実行。odd hold、2-step位置と実地形への着地をproduction checksで確認。renderはnative stateを保持 |
| §28 fixed constants | Story encounter1で1228/4096と1843/4096を使用。通常60Hz移動のdamping / gravityを含む結果を1e-6 toleranceで確認 |
| §29 tests | 独立CTestは179 checks＋health sync assertions。実assetsのproduction checksは383件。8人＋56 health spawnersを全員省略せずpacket容量内で巡回配信するケースも確認 |
| §30 C# mirror | ユーザーのNative C++限定指定により対象外。C#変更・buildなし |
| §31 禁止変更 | float canonical / Alt boolによるdead turret復活 / replica二重split / touch clock共有 / 単なるtimer割算 / projectile authority推測変更なし |
| §32～33 gate / SRP | 全受入gate合格。raw arithmetic、cadence、input latch、player lifecycle、turret mechanics、wire replicationに責務を分離 |

CI logsのローカル保存先はignored `tools/build/out/weavel-ci-{windows,linux,macos}.log`。
手動ROM比較と実機でのプレイは実施していない。指示書が証拠境界を残すROM Wi-Fi projectile同期について、byte-perfect parityは主張しない。

## 2026-10-08 最終実装とローカル受入

責務はraw fire arithmetic、scene共通native cadence、edge latch、player form/life、turret mechanics、wire replicationに分離。
`PlayerWeavel.cpp`でform lifecycleとnative owner timerを扱い、turretはlocal spawnとsnapshot activationを分ける。
snapshot activationはauthority HPを採用し、split/mergeを再実行しない。Altでturretが死んだ状態も保持する。
sceneではturretがownerより先に処理されるため、owner native timerをframe guard付きで共通advanceし、両者の呼出しで二重decrementしない。

protocol 17は既存114 bytesの後ろにflags/HP/positionの14 bytesを追加。8人を全員送った上でhealth-spawner tailをpacket容量内で巡回配信し、受信cacheは同じmatchの他spawnerを保持する。56個を4 snapshotsで全配信するケースをproduction publisherで検証。

| 検証 | 結果 |
|---|---|
| MSVC Release game / parity targets | build成功 |
| CTest WeavelAltFormParity / DialancheNativeCollision / IntentTouchPayload / NativeTouchState | 4/4成功、Weavelは179 checks＋NetHealthSync assertions |
| `tools/check-weavel-alt.ps1` | 実assetsのproduction checks 383件成功、旧16の実UDP拒否成功、scene step failures=0 |
| `tools/check-dialanche-combat.ps1` | 28 production assertions＋独立UDP peers 2件成功 |
| `tools/check-dialanche-network.ps1 -SpireSeconds 8 -TargetSeconds 10` | authority / 2 clientsでevents=4、HP=97一致、native phase / base8 / 同tick重複body eventなし |
| `-lockjawenemycheck "MP3 PROVING GROUND"` | 93 checks成功 |
| Android arm64関連7 translation units | compile成功。既存warningsあり。APK/device未検証 |

ローカル再現コマンド:

```powershell
ctest --test-dir tools/build/out/msvc-Release -C Release -R 'FruityPrime\.(WeavelAltFormParity|DialancheNativeCollision|IntentTouchPayload|NativeTouchState)$' --output-on-failure --no-tests=error
powershell -NoProfile -ExecutionPolicy Bypass -File tools/check-weavel-alt.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File tools/check-dialanche-combat.ps1 -OutputDirectory tools/build/out/weavel-final-dialanche
powershell -NoProfile -ExecutionPolicy Bypass -File tools/check-dialanche-network.ps1 -SpireSeconds 8 -TargetSeconds 10 -OutputDirectory tools/build/out/weavel-short-network
```

実asset検証には実行binary隣の`paths.txt`とEU1.1 game assetsが必要。asset-free CTestを3 desktop CIで実行する。
旧16拒否はNative `NetTransport`と`DedicatedServer`を実UDPで接続して検証する。PowerShell UDP probeでは応答timeoutが発生したため採用していない。
ダイアランチの既定16秒/18秒の再実行は17 damage events後の死亡・再出現でlatest-eventが0になり、fixtureの累計event-id比較に失敗した。今回の回帰証拠は8秒/10秒の成功run。scriptへ時間指定とWindows process handle保持を追加し、検証対象の判定自体は緩和していない。
ignored logs: `tools/build/out/weavel-validation`、`weavel-final-dialanche`、`weavel-short-network`、`weavel-final-android.log`、`weavel-protocol-android.log`。
下記checkpointは中間時点の履歴であり、現在の未完了項目を示すものではない。

## 2026-10-08 checkpoint

ユーザーのcommit/push指示に対する中間保存。指示書全体の完了を意味しない。

### 実装

- `HalfturretFireRate`にraw factor damage / recovery / thresholdの算術を分離。
- `ResetForSpawn`を`Initialize`冒頭で実行し、前lifeのtarget、affliction、velocity、timer、Story weapon overrideを除去。
- `NativeGameplayClock`はscene frameの非zero偶数を使用。input/touchのresetでphaseが変わらない。
- turretのdecision / 内部timer / physicsをnative tickで1回実行。非native stepではstateを保持。
- ownerの共通`TimeSinceShot`は現時点では60Hzのまま。threshold comparisonで2倍換算しており、u8 native timerへの移行は残件。
- protocolは16のまま。明示turret snapshot / force lifecycle / lunge latchは次の変更で扱う。

### 実行結果

- MSVC Release: `fruity_prime`と`fruity_weavel_alt_form_parity_tests`をbuild成功。
- CTest: WeavelAltFormParity、DialancheNativeCollision、IntentTouchPayload、NativeTouchStateの4/4成功。
- WeavelAltFormParity: 154 checks成功。実objectのdamage hook、reset、EquipInfo default復元を含む。ゲームassetsは使用しない。
- `-lockjawenemycheck "MP3 PROVING GROUND"`: 93 checks成功。
- `check-dialanche-combat.ps1`: 28 production assertionsと独立UDP peers 2件成功。
- `check-dialanche-network.ps1`: authority / clientsでevents=4、HP=67一致。
- Android arm64: HalfturretEntity translation unit compile成功、既存の24 warnings。APK build / device testは未実施。
- `-simcheck "MP3 PROVING GROUND" -players 8 -seconds 5`: `Bad optional access`で失敗、spawned 0/8。同じassetsを使用した保存済み`FruityPrime-SrpCheck.exe`（2026-10-07 19:11生成）でも同じ失敗。今回のWeavel acceptance成功の証拠には含めない。

ローカルlogsはignored `tools/build/out/weavel-*` に保存。
