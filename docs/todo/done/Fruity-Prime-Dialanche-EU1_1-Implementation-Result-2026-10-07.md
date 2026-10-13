# Dialanche EU1.1 実装・検証結果 — 2026-10-07

対象ブランチ: `develop5_morphBall`。基準HEAD: `17da22216f686c930da07b40a4ab410650b77c76`。
指示書と配下の10件の補足資料（EU1.1アドレス照合、入力、初期化、pose、overlap、damage、終了処理を含む）を確認した。
受入完了。2026-10-07のユーザー確認により、未実装のアドベンチャーモードmultiplayerを
前提とするEnemy / Doorネット同期は今回の受入対象外とする。両対象のproduction攻撃判定は検証済み。

## 実装

Player / Halfturret / Enemy / DoorのDialanche overlapは、共通の
`DialancheHitsVolume`を通す。scene frameが非ゼロの偶数の場合だけ判定し、
native tickは`frame / 2`とする。左右の半径はともに0.5、左右ORで1回のreactionとする。

`DialancheNativeCollision`は初期位置と2世代のsampleをPlayerごとに保持する。
攻撃開始時に両岩とhistoryをPositionへresetする。continuous岩位置のheadless更新は継続し、
偶数frameだけhistoryへ記録する。判定はcurrent tickより古い最新sampleを選び、
current tickのRecord前後で結果が変わらない。同tickの再Recordもpreviousを破壊しない。
64bitのhelperは104 bytesで、追加heap allocation、lock、動的target container、frameごとのlogはない。

Door拡張は、従来の扉のapertureと深さ±1.25のcontact領域をcylinderとして表し、
保存した左右の岩sphereでoverlapする。現在のPlayer bodyが領域外でも保存sampleが重なれば判定する。
bodyの物理collisionは従来の処理を継続する。hit後のShotOpen、palette8の
`Unlock(true, true)`、専用hit SFXは既存処理を使う。

入力press edge、NoLoop、attack basis、base damage8、Story bot2/3/5、
NoSfx / NoDmgInvuln / Halfturret、turret radius0.45、TakeDamageの倍率・無敵・team処理、
Spireの終了処理は維持した。LoS、cooldown、全target共通hit latchは追加していない。
Intent / PlayerState / DamageEvent / replayの形式とProtocolVersion16は変更していない。
新しいROMアドレス表記はEU1.1のみ。

## 実行結果

| 検証 | 結果 |
|---|---|
| MSVC Release `fruity_prime`と関連test targets | build成功 |
| 関連CTest6件 | 6/6 PASS（DialancheNativeCollision、NativeTouchState、IntentTouchPayload、RawMouseMotion、WindowsRawMouseInput、VulkanNvidiaReflex） |
| GCC C++20 pure helper test | PASS |
| Android arm64 Clang | helper、collision、input、process、combat check、spire pose checkの6 translation unitsのcompile成功 |
| source gate `tools/check-spire-alt-pose.ps1` | 8/8 PASS |
| asset-backed `-dialanchecheck` | 28 production assertions PASS |
| `tools/check-dialanche-combat.ps1` | authority + 独立したUDP clientプロセス2つ、両方PASS |
| `tools/check-dialanche-network.ps1` | 通常のDedicatedServerへのjoin、Spire/Samusの2 clients、両方exit0。authority/両clientsで11 events・最終HP11一致 |
| `-spireposecheck "AD2 ALINOS PERCH"` | headless PASS、active44 frames、native samples22、左右ともmoving22、same-tick非公開/odd不更新PASS |
| `-maptest "AD2 ALINOS PERCH" -players 8 -hunter Spire -bots -seconds 8` | exit0、480frames、spawn8/8、simulation継続 |
| 最新code commitのGitHub CI | 全15 jobs成功。Windows / MSVC、Linux / GCC、macOS / Clang、Android NDK両ABI、APK、API28 / 30 / 35 startup smokeを含む |

ゲームdataは`paths.txt`のAMHP1（EU1.1）を使用した。
Windows/Linux/macOS CIにはpure helper testを追加した。
最新code commit `a09a63de41bcc0981da2356c60ef7b46fef2e4be`の
[CI run](https://github.com/Zection6V/Fruity-Prime/actions/runs/37585600635)は全15 jobs成功した。
Windows / MSVCはDialancheを含むCTest6/6、macOS / ClangとLinux / GCCは各5/5を
実際のjob logsでも確認した。Android NDK arm64-v8a / x86_64、APK、
API28 / 30 / 35 emulator startup smokeと各static auditも成功した。
emulator startup smokeは起動gateの証拠であり、Android上のDialanche実操作や実機parityの証拠ではない。
ローカルAndroid検証は上記objectsのcross compileであり、ローカルAPK buildや実機操作は行っていない。
追加したHitRig、NetCheckClient、ModEntryもAndroid arm64でcompileした。
maptestは描画を伴うsmoke testであり、ROM画像とのpixel比較や操作感の実機評価ではない。

## production受入項目

| 指示書の項目 | 証拠 |
|---|---|
| 26: Reset、same tick非公開、next tick、processing order | pure unit test。duplicate/stale Record、再attack Reset、frame0/odd/evenも検証 |
| 27: Player連続接触 | `CheckPlayerCollision -> CheckAltAttackHit1 -> TakeDamage`、201/202で100→92・event1・SFX1、203/204で92→84・event2・SFX2 |
| 28: 非native siblingだけの接触 | damage/SFX0。native接触では8 damage |
| 29/31: 複数target、左右OR | 同tickの2 targetに各8。両岩が同じvolumeへ重なっても1 event/channel |
| 30/34: body / Halfturret | 同native tickに2 events / SFX2、owner100→88、turret100→96。0.949でhit、0.951でmiss（0.5+0.45） |
| 32/33: 共通damageとStory bot | Double Damageで1 event・16 damage。bot level0/1/2で2/3/5 damage・SFX1 |
| 35: SFX周期 | recording audio backendで実際の専用PlaySfx要求を計数。非native stepは0 |
| 36: Enemy拡張 | 既存CheckAltAttackHitEnemy1 / EnemyInstanceEntity::TakeDamage / EnemyTakeDamageを通し100→92→84、nativeごとにreaction/SFX1 |
| 37: Door拡張 | 既存AltAttackHitDoor / Unlockを通しShotOpen・Unlocked・SFX1。siblingは0。current contact sampleを隠し、次tickで公開してreaction1 |
| 38: headless更新 | SpireAltPoseCheckで実入力intentからmorph / AltAttack。44 active frames中22 native samples |
| 共通geometry / processing order | PlayerがRecord前、Halfturret・Enemy・DoorがRecord後でも同じprevious contact sampleへhit。新current sampleは遠方に置いて検出 |
| EndAltAttack | Spireのflag clear、cooldown0 |
| 39: Player network | production BroadcastSnapshotから3 snapshotsをUDP送信し、2独立プロセスのclient handler / NetPlayerBridge / NetDamage::Replayで100→92→84、events0→1→2 |
| 39: 通常join経路 | DedicatedServer、Spire client、Samus clientを起動。Welcome / intent / prediction / damage replayを通し、authorityの11 event identitiesと両clientsの11 replays、最終HP11が一致 |
| 18/39: Enemy / Door network | 対象外。アドベンチャーモードmultiplayerは未実装で、2026-10-07のユーザー確認により今回の受入要件から除外。36/37のproduction判定検証は維持 |

UDP検証の受信側は、既存playback入口へ受信packetを投入し、通常のclient snapshot handlerを実行する。
これは独立プロセス・実UDP・既存packet serializer/reader・damage replayの検証であり、
DedicatedServerへの通常のjoin/Welcome/intent交換を再現するテストではない。
duplicate snapshotsとdamage historyの再送も重複damageを生成しなかった。

通常join検証の`-hitrig dialanche`は既存HitRigから移動・morph・AltAttackの入力を送る。
forced form、pose書換え、damage注入は行わない。default prediction / claimsも有効のまま。
authority-hitの全11件はweapon None・damage8で、native位相に一致し、同frame/victimの二重body eventはなかった。
連続contactはauthority frame255/257/259/261/263/265でも各1件を確認した。
NetFrameはgameplay前、Scene.FrameCountはgameplay後に進むため、logのauthorityFrameは奇数になる。
targetのspawn HP99から11回×8 damageで11となった。event数は接近・入力timingで変わるため、
scriptは2件以上、全event identity一致、native位相、base8、各tick/bodyの最大1件、最終HP一致を検証する。
Enemyの計数は既存reaction callback、Doorの計数はShotOpenと実際のSFX要求で行い、
架空のEnemy/Door用NetDamage eventは追加していない。

再実行:

```powershell
ctest --test-dir tools/build/out/msvc-Release -R '^FruityPrime\.(DialancheNativeCollision|NativeTouchState|IntentTouchPayload|VulkanNvidiaReflex|RawMouseMotion|WindowsRawMouseInput)$' --output-on-failure --no-tests=error
./tools/check-spire-alt-pose.ps1
./tools/check-dialanche-combat.ps1
# TEST ARENAのrecipeを実行binaryのmaps/arena/へ配置して実行する。
./tools/check-dialanche-network.ps1
# ゲームdataを参照するpaths.txtは実行binaryの横へ配置する。
./tools/build/out/msvc-Release/FruityPrime.exe -spireposecheck "AD2 ALINOS PERCH" -noupdate
```

詳細logsはignoredの`tools/build/out/dialanche-validation/{authority,peer-0,peer-1}.log`、
`dialanche-build.log`、`dialanche-android-compile.log`、`dialanche-spirepose.log`、
`dialanche-maptest.log`にある。
通常joinのlogsは`tools/build/out/dialanche-live/`のserver / spire / target logsと
`netlog-{server,DialancheLiveSpire,DialancheLiveTarget}.txt`に保存した。
最新CIのdesktop test logsは`tools/build/out/dialanche-ci-latest-{windows,macos,linux}.log`に保存した。

## 指示書の前提差と確定した受入条件

指示書18/39は「既存Enemy damage/state同期」「既存Door state同期」を前提としている。
基準HEADにはその経路がない。`NetSession::BroadcastSnapshot`はPlayerState、match time、
health pickup spawn stateを送信する。`HandleSnapshot`もその構成を受信する。
`NetDamage`はPlayerEntityのslot/life単位、`WorldEvents`はjump pad / teleport計数であり、
Enemy HP / Door stateの同期ではない。Door自体もSinglePlayerを前提に構築される。

2026-10-07にユーザーから「アドベンチャーモードのマルチプレイはまだないからそれはいいや」と
確認を得た。これにより、指示書18/39のEnemy / Doorネット同期は今回の受入対象外とする。
Enemy / Doorのproduction攻撃判定、共通native cadence / sampled pose、既存damage / open / unlock
reactionの維持は引き続き受入対象であり、上記36/37とprocessing order検証でPASSした。
Playerの2-client / authority network parityもPASSし、新packet fieldやProtocolVersion変更は加えていない。
この確定した受入条件に対する実装・検証・commit / pushは完了した。

## C++版のSRP整理 — 2026-10-07

ダイアランチの姿勢履歴、幾何判定、攻撃の適用を以下の責務に分けた。

- `DialancheNativeCollision`: Playerごとの固定サイズの2世代履歴とnative tickの定義。
- `Mods/Combat/DialancheHitTest`: 保存済みの左右の岩とvolumeのoverlap、扉のcontact volumeの構築。
  Scene、Player、damage、SFX、networkの状態を読み書きしない。
- `Entities/Players/PlayerDialanche.cpp`: native tickに対応する履歴の選択と、Player / Halfturretへの
  reaction。Story botのdamage選択は`DialanchePlayerDamage`へ分離した。
- `PlayerCollision.cpp`: ハンターごとのdispatchと既存Enemy / Doorのreaction。
  ダイアランチの共通幾何判定・Player damage計算を内包しない。

RockRadiusは幾何判定側へ移した。履歴のデータ構造、30Hz位相、previous poseの選択、
左右OR、damage倍率、Story botの2/3/5、SFX、既存TakeDamage、ProtocolVersion16は維持した。
追加のheap allocation、lock、cooldown、hit latch、packet fieldはない。
source gateも抽出先を追うよう更新した。

| 整理後の検証 | 結果 |
|---|---|
| MSVC Release game / native collision test | build成功 |
| `FruityPrime.DialancheNativeCollision` | CTest PASS |
| `check-spire-alt-pose.ps1` | source gate 8/8 PASS |
| `check-dialanche-combat.ps1` | production assertions 28件と独立UDP peers 2つがPASS |
| `check-dialanche-network.ps1` | 通常joinでauthority / 両clientsの4 events、最終HP67が一致。native phase / base8 / 同tick最大1件がPASS |
| `spireposecheck` | active44 frames / native samples22 / moving L/R22/22、headless PASS |
| Android arm64 Clang | PlayerCollision、PlayerDialanche、DialancheHitTestの3 translation unitsをcompile成功 |

整理後のlogsは`tools/build/out/dialanche-srp-{build.log,android.log,pose.log}`、
`dialanche-srp-combat/`、`dialanche-srp-network/`に保存した。
上記のCI結果は整理前の実装に対する証拠であり、今回のSRP整理後のCIは未実施。
Android APK / 実機操作、ROM画像比較、手動プレイ確認も今回の検証には含めていない。
