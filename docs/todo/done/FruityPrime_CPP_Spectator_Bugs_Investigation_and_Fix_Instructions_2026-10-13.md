# Fruity-Prime C++版：観戦モードの3件の不具合に関する調査・修正指示

- 調査日：2026-10-13
- 対象リポジトリ：`Zection6V/Fruity-Prime`
- 調査対象ブランチ：`develop11_netCode`
- 調査基準コミット：`ca12c121d8e45f97a3fd7c9f389819f935cba60c`
- 調査方式：GitHub上のC++実装の静的解析。ゲーム起動・実機再現・ビルド・テストは未実施。
- **本書は修正指示書であり、実装修正・コミット・プッシュは行っていない。**

## 1. 報告された症状

1. 観戦対象のプレイヤーが試合から退出すると、視点を動かせなくなる。
2. 観戦モード中に死亡すると、射撃音が聞こえなくなる。
3. 観戦者が攻撃を受けて死亡することがある。

### 調査の結論

| ID | 優先度 | 判定 | 静的解析による要因 |
| --- | --- | --- | --- |
| SPEC-01 | P0 | **構造的な原因を確認** | 観戦対象が退出してスロットが無効化されても、`MainPlayerIndex`を再評価・退避させない。カメラは無効になった対象を参照し続ける。 |
| SPEC-02 | P1 | **音声経路は確認、直接原因は未確定** | 射撃SFXは発射者の`SoundSource`、音声リスナーは現在のカメラ／`Main`に依存する。死亡と対象切替でこれらの状態が変化するため、再生イベント欠落・音量ミュート・リスナー不整合を切り分ける必要がある。 |
| SPEC-03 | P0 | **防御漏れを確認** | 観戦者の除外はプレイヤー相互衝突に限定され、共通の`TakeDamage()`、ネットワークのダメージ経路、サーバーの被弾Claim判定には漏れがある。 |

**重要：** 3つが同じ単一の不具合であるとは断定しない。特にSPEC-02の原因を、検証なしに「死亡時に全音声を停止している」と決めつけないこと。

## 2. SPEC-01：観戦対象が退出すると視点を動かせない

### 2.1 確認できた処理経路

1. `SpectatorMode::Start()`は、観戦時に`FindNextActiveSlot()`から対象スロットを選び、`Switch()`を実行する。
2. `SpectatorMode::Switch(slot)`は`PlayerEntity::SetMainPlayerIndex(slot)`を実行し、観戦対象の`CameraInfo.NodeRef`を同期する。
3. 一方、`NetSlotManager::Deactivate()`は、対象スロットの`Active`と`Spawned`を解除して`Health`をゼロにするが、観戦先を更新しない。
4. `SessionRosterSynchronizer::ApplyAccepted()`は退出したスロットのoccupantをゼロにし、`NetPlayerLifecycle::SetOccupant()`がスロット解放処理へつなぐ。この一連の経路にも観戦対象の有効性確認がない。
5. `Scene::TransformCamera()`は`CameraMode::Player`のとき`PlayerEntity::Main()->CameraInfo()->ViewMatrix`を使用する。退出後も`MainPlayerIndex`が元のままなら、非アクティブの対象カメラを参照し続ける。
6. フリーカメラの移動は`Scene::MoveRoamCamera()`の`CameraMode::Roam`経路でのみ成立する。追従モードから自動的にRoamへ移る処理は見当たらない。
7. `SpectatorMode::CycleNext()`／`CyclePrevious()`は次の対象が見つからなければ単に戻る。**最後の観戦可能プレイヤーが退出した場合、フリーカメラへの自動退避がない。**

### 2.2 根拠となる実装

- [`SpectatorMode.cpp`：開始・巡回・切替](https://github.com/Zection6V/Fruity-Prime/blob/ca12c121d8e45f97a3fd7c9f389819f935cba60c/src/MphRead.Native/Mods/SpectatorMode.cpp#L62-L173)
- [`NetSlotManager.cpp`：退出時のDeactivate](https://github.com/Zection6V/Fruity-Prime/blob/ca12c121d8e45f97a3fd7c9f389819f935cba60c/src/MphRead.Native/Mods/Network/Session/NetSlotManager.cpp#L176-L186)／[同ファイル：フラグ解除](https://github.com/Zection6V/Fruity-Prime/blob/ca12c121d8e45f97a3fd7c9f389819f935cba60c/src/MphRead.Native/Mods/Network/Session/NetSlotManager.cpp#L231-L254)
- [`NetPlayerLifecycle.cpp`：SetOccupant](https://github.com/Zection6V/Fruity-Prime/blob/ca12c121d8e45f97a3fd7c9f389819f935cba60c/src/MphRead.Native/Mods/Network/Gameplay/Player/NetPlayerLifecycle.cpp#L156-L180)
- [`SessionRosterSynchronizer.cpp`：ApplyAccepted](https://github.com/Zection6V/Fruity-Prime/blob/ca12c121d8e45f97a3fd7c9f389819f935cba60c/src/MphRead.Native/Mods/Network/Session/SessionRosterSynchronizer.cpp#L48-L123)
- [`Renderer.cpp`：カメラ要求の反映](https://github.com/Zection6V/Fruity-Prime/blob/ca12c121d8e45f97a3fd7c9f389819f935cba60c/src/MphRead.Native/Renderer.cpp#L1784-L1816)
- [`Renderer.cpp`：SetFreeCamera](https://github.com/Zection6V/Fruity-Prime/blob/ca12c121d8e45f97a3fd7c9f389819f935cba60c/src/MphRead.Native/Renderer.cpp#L4903-L4935)
- [`Renderer.cpp`：TransformCamera](https://github.com/Zection6V/Fruity-Prime/blob/ca12c121d8e45f97a3fd7c9f389819f935cba60c/src/MphRead.Native/Renderer.cpp#L2505-L2525)

### 2.3 必須修正

**A. 観戦ターゲットのライフサイクルを一元管理する。**

- `SpectatorMode`に「現在の追従対象スロット」と「そのスロットの世代（slot generation）」の有効性を判定する純粋なヘルパーを設ける。現行の`MainPlayerIndex`を唯一の状態として使う場合も、必ずローカルスロットと明示的に区別する。
- ターゲット有効条件：スロット範囲内、プレイヤー実体が存在、ローカル本人以外、`LoadFlags::Active`かつ`LoadFlags::Spawned`、`Health()>0`、ネットワーク対戦時は現行rosterの占有状態／世代と一致。現在の仕様に従い、生存していないプレイヤーへの追従は許可しない。
- `FindNextActiveSlot()`と`FindPreviousActiveSlot()`を同一の有効性判定に統一する。空のplayersリスト、負の`fromSlot`、スロットの再利用で例外や誤ったターゲットを選ばないようにする。
- 対象が退出・死亡・スロット世代変更となったときに状態遷移を一度だけ処理する。**理想的にはネットワークのロスター更新に対する通知で即時処理し、毎シミュレーションフレームの軽量な検証を保険として併用する。**

**B. 退出時のフォールバック。**

- 他に有効なプレイヤーがいるなら、その中から次の追従対象へ自動切替する。
- 有効なプレイヤーがいなければ、自動的にフリーカメラへ移行する。観戦モード自体は継続し、自由移動・視点回転できること。
- フリーカメラ移行時は**最後の正常なカメラ姿勢（位置、向き、上方向）**を継承する。既に無効化された対象の`CameraInfo`から改めて取得しない。`Scene::SetFreeCamera(true)`が参照する`Main()`は事前に安全性を検証し、必要なら事前にスナップショットした姿勢をScene側に適用する。
- ロスター適用順、ビューの描画順、`NetSlotManager::Deactivate()`からの呼び出し順が異なってもフリーカメラへ退避できるようにする。ロスター処理でレンダラーを直接操作する密結合は避ける。
- `_cameraRequest`に`false`（追従に戻す要求）が積まれた状態でターゲットが消えた場合、無効な追従復帰をキャンセルする。旧世代ターゲットの要求は新世代スロットへ適用しない。

**C. SRP/DRY。**

- `SpectatorMode`が観戦可否とターゲット変更を担当。
- `NetSlotManager`／`NetPlayerLifecycle`はスロット／生命状態を担当し、必要最小限のイベントを発行するだけにする。
- `Scene`は実際のカメラ姿勢、Player/Roamモード遷移を担当。
- 重複したスロット可否条件は1つのルールにまとめ、`Scene`が`MainPlayerIndex`の値だけを信じてアクセスしない構造を目指す。

### 2.4 回帰テスト

- A、B、Cが対戦中、ローカルはAを観戦。Aが退出するとBまたはCへ自動切替。
- Aだけが観戦可能な状態でAが退出するとフリーカメラへ移行し、マウス・WASD・ゲームパッドで自由移動できる。
- 切替直前・直後にマウス右クリック、プレイヤー切替、観戦解除が重なっても、カメラが無効なスロットを参照しない。
- 同じスロットへ別プレイヤーが再入室しても、旧世代の観戦ターゲット状態が勝手に復活しない。
- ロスター更新とスナップショット受信の順序を逆転させても、無効カメラ・クラッシュ・HUDの誤表示がない。
- 観戦解除でローカル自身の視点へ戻り、`Spectating`フラグが必ず解除される。

## 3. SPEC-03：観戦者が被弾・死亡する

### 3.1 確定した防御漏れ

**観戦フラグは描画とプレイヤー同士の衝突回避に使用されているが、無敵・被ダメージ不可能を保証していない。**

- `PlayerCollision.cpp::CheckPlayerCollision()`は、`PlayerFlags2::Spectating`が立っていればreturnし、相手が観戦中でも接触計算から除外する。
- しかし`PlayerEntity.cpp::TakeDamage()`は`NetDamage::Suppress()`とHP、各種無敵タイマーしか確認せず、`PlayerFlags2::Spectating`を検査しない。
- `PlayerProcess.cpp`には酸・溶岩・マップ境界・落下判定から`TakeDamage()`を呼ぶ経路がある。観戦を開始した瞬間に身体を消しただけでは、それらの判定は除外されない。
- `NetDamage.cpp::Refuses()`はホスト・クライアントのネットワークダメージを振り分けるが、観戦プレイヤーであることを理由に拒否する条件がない。
- `PlayerEntityNetAim.cpp::ModSetSpectating()`はフラグだけをセットする。同じファイルに`ModInPlay()`（観戦状態を除外）と`ModIsInPlay()`（観戦状態を無視）が共存している。
- **`NetHitClaims.cpp`は`victim.ModIsInPlay()`を利用しており、生存中の観戦者に対する被弾Claimを通し得る。** 観戦者はサーバーによるダメージ適用からも保護する必要がある。
- `NetPlayerIntent.cpp`は`Spawned && Health>0`なら観戦状態に関係なく`IntentButtons::InPlayState`を送信する。`SpectatingState`と`InPlayState`が同時に真となるため、サーバー側の`NetPlayerBridge::ApplyIntent()`が操作入力を通常のプレイヤーとして処理し得る。

### 3.2 根拠となる実装

- [`PlayerCollision.cpp`：観戦者の相互衝突除外](https://github.com/Zection6V/Fruity-Prime/blob/ca12c121d8e45f97a3fd7c9f389819f935cba60c/src/MphRead.Native/Entities/Players/PlayerCollision.cpp#L115-L130)
- [`PlayerEntity.cpp`：TakeDamage入口](https://github.com/Zection6V/Fruity-Prime/blob/ca12c121d8e45f97a3fd7c9f389819f935cba60c/src/MphRead.Native/Entities/Players/PlayerEntity.cpp#L1752-L1798)
- [`PlayerProcess.cpp`：地形・落下ダメージ](https://github.com/Zection6V/Fruity-Prime/blob/ca12c121d8e45f97a3fd7c9f389819f935cba60c/src/MphRead.Native/Entities/Players/PlayerProcess.cpp#L1349-L1378)
- [`NetDamage.cpp`：Refuses](https://github.com/Zection6V/Fruity-Prime/blob/ca12c121d8e45f97a3fd7c9f389819f935cba60c/src/MphRead.Native/Mods/Network/Gameplay/Combat/NetDamage.cpp#L179-L255)
- [`PlayerEntityNetAim.cpp`：ModSetSpectating / ModInPlay / ModIsInPlay](https://github.com/Zection6V/Fruity-Prime/blob/ca12c121d8e45f97a3fd7c9f389819f935cba60c/src/MphRead.Native/Mods/Network/Gameplay/Player/PlayerEntityNetAim.cpp#L327-L350)
- [`NetHitClaims.cpp`：Claim一次検証](https://github.com/Zection6V/Fruity-Prime/blob/ca12c121d8e45f97a3fd7c9f389819f935cba60c/src/MphRead.Native/Mods/Network/Gameplay/Claims/Hit/NetHitClaims.cpp#L598-L614)／[同ファイル：確定前チェック](https://github.com/Zection6V/Fruity-Prime/blob/ca12c121d8e45f97a3fd7c9f389819f935cba60c/src/MphRead.Native/Mods/Network/Gameplay/Claims/Hit/NetHitClaims.cpp#L1279-L1302)
- [`NetPlayerIntent.cpp`：プレイ状態と観戦状態の同時送信](https://github.com/Zection6V/Fruity-Prime/blob/ca12c121d8e45f97a3fd7c9f389819f935cba60c/src/MphRead.Native/Mods/Network/Gameplay/Player/NetPlayerIntent.cpp#L35-L46)
- [`NetPlayerBridge.cpp`：受信入力のInPlay分岐](https://github.com/Zection6V/Fruity-Prime/blob/ca12c121d8e45f97a3fd7c9f389819f935cba60c/src/MphRead.Native/Mods/Network/Gameplay/Player/NetPlayerBridge.cpp#L205-L220)

### 3.3 必須修正

**A. プレイ状態の意味を統一する。**

- 「セッション参加・スロット占有」と「ゲーム中の戦闘対象」は別の状態。観戦中でもセッションには参加したままでよいが、戦闘対象としては`NotInPlay`とみなす。
- `ModIsInPlay()`を見直し、少なくとも`Active && Spawned && Health>0 && !Spectating`を満たすときだけ真にする。`ModInPlay()`との名称・責務・重複を整理する。ただし従来`ModIsInPlay()`の用途を列挙してから変更し、意図せぬリスポーン・観戦復帰への副作用を回帰テストする。
- `NetPlayerIntent::CaptureState()`で観戦時は`InPlayState`を立てない。`SpectatingState`は独立に伝え、**サーバーはSpectating=trueかつInPlay=trueの矛盾する入力を安全側でNotInPlayに正規化**する。既存プロトコルで表現可能なため、単純な条件変更だけなら不要なプロトコルバージョン更新はしない。
- 観戦開始時には操作の押下状態をクリアし、後続のショット／モーフ入力を発火させない。`PlayerEntity::ProcessInput()`の単純なcontinueだけに依存しない。再参加時も古い押下履歴を再利用しない。

**B. 被ダメージ入口に最終防壁を設ける。**

- `PlayerEntity::TakeDamage()`の入口付近で、観戦状態の対象への戦闘ダメージ・地形ダメージ・即死ダメージを拒否する。死亡やスロット解放に必要な管理処理を誤って抑止しないよう、戦闘ダメージと管理上の非戦闘状態遷移は責務を分ける。
- ネットワークの`NetDamage::Refuses()`にも同様のガードを設ける。ただし**ここだけに依存しない**。`NetDamage`が無効／権威側のリプレイ中でも共通の`TakeDamage()`が守る必要がある。
- サーバーの`NetHitClaims`で、検証開始時と適用直前の双方について`victim.ModIsInPlay()`を判定し、観戦者のClaimを拒否する。古いClaim、スロット世代変更、観戦へ切り替わる直前の発射を含むケースを考慮する。
- 観戦フラグの複製が遅れる可能性があるため、観戦移行からサーバー承認までの間に被弾Claimが来た場合の規則を明示する。クライアント側の見た目だけで無敵と見せず、**サーバーが受け入れた状態に基づいて被弾判定する**。移行中はログで競合を確認できるようにする。

**C. スナップショットによる死亡リプレイを守る。**

- `NetPlayerBridge::ApplyState()`は現状`NetDamage::Replay()`を実行した後で`ModSetSpectating(state.Flags & FlagSpectating)`を実行する。受信スナップショットが観戦状態なら、リプレイ時に旧フラグを参照する可能性がある。
- `PlayerState::FlagSpectating`を考慮して死亡・被弾イベントを判定するか、フラグ反映順序を既存のライフサイクル保証と整合させる。途中で矛盾するHP=0のスナップショットが来ても、観戦モードの継続と音声・カメラへの副作用が安全であること。
- `NetPlayerLifecycle::StateOf()`は既に`FlagSpectating`を`NetworkPlayerState::Spectating`として扱う。状態遷移と被弾拒否のポリシーを一致させる。

**D. 観戦終了時。**

- `SpectatorMode::Rejoin()`の`ModSetSpectating(false)`をサーバーへ確実に通知し、リスポーンフローと整合させる。無敵状態が通常プレイへ持ち越されない。
- 観戦中の残存弾、爆弾、ホーミング対象、AIターゲットから観戦者を適切に除外する。発射済み弾の寿命／責任者の扱いは既存のネットコード規約を維持し、不要な弾全消去はしない。

### 3.4 回帰テスト

- 観戦者への通常弾、チャージ弾、Omega、ミサイル、爆風、モーフボール爆弾、Alt攻撃、近接接触、追尾弾がすべてダメージ0・死亡0・被弾Claim確定0になる。
- 酸・溶岩・落下・KillHeight・境界即死判定でも観戦者は死亡しない。
- 観戦移行と同フレーム／直前／直後に到着した古いダメージClaimは、定義した境界規則どおり処理され、サーバーとクライアントのHP・死亡数が矛盾しない。
- 観戦者が保持していた射撃ボタンは観戦開始後にサーバー／他クライアントで発砲しない。
- 観戦解除後は普通に被弾・死亡・リスポーンでき、永続無敵化がない。
- 途中退出／再接続／スロット再利用で古い観戦フラグが次のプレイヤーへ伝播しない。
- 通常プレイヤー対通常プレイヤー、BOT対人間、同時キル、環境死、凍結中の被弾は既存仕様を維持する。

## 4. SPEC-02：観戦中の死亡後、射撃音が聞こえなくなる

### 4.1 静的解析で確認した音声経路

- 発射成功時の`PlayerEntity::TryFireWeapon()`は`PlayBeamShotSfx()`を呼び出す。これは発射者の`_soundSource.PlaySfx()`を呼び出す。
- リモートプレイヤーのショットイベントは`NetShotEvents::FireReady()`から`ModFireShotEvent()`、`TryFireWeapon()`につながる。したがって、ネットワークのショット再現が止まれば射撃音も鳴らない。
- `SfxInstance::GetListenerPosition()`はシーンのカメラ位置を使用し、Playerカメラモードの上方向・向きは`PlayerEntity::Main()->CameraInfo`を使用する。観戦対象の変更・退出・死亡でリスナーが不正になる可能性がある。
- 各`PlayerEntity`の`_soundSource.Update()`は、`IsMainPlayer() && !IsAltForm()`であれば非減衰相当の設定、その他のプレイヤーでは距離減衰付きの設定を使用する。
- `PlayerEntity::TakeDamage()`の死亡分岐は**死亡者自身の**`_soundSource.StopAllSfx(true)`を呼ぶ。実装上、これは`SoundSource::StopAllSfx()`から`SfxInstance::StopSoundFromSource(this)`を呼び、原則としてそのSoundSourceに紐付いた音だけを止める。**他人の射撃音を一括停止したと断定できない。**
- `PlayerEntity::StopAllSfx()`という別のメソッドは、全体の`SfxInstance::StopAllSound(false)`を呼ぶ。上記の死亡分岐で実際に使用されるのはこのメソッドではないため、混同しない。
- `Sfx::SfxMute`は`SfxInstance::SoundInstance::UpdateParameters()`で、ソースがある音のゲインを0にする。`StopLongSfx()`がこのフラグを立てるが、観戦者の死亡で必ず呼ばれるという証拠はない。
- `Scene::UpdateScene()`は**現在の`Main()`がActiveのときだけ**そのプレイヤーの`UpdateTimedSounds()`を実行する。観戦先が退出して`Main()`が非アクティブになると、この更新経路も停止する。

### 4.2 根拠となる実装

- [`PlayerInput.cpp`：発射成功からSFX呼出](https://github.com/Zection6V/Fruity-Prime/blob/ca12c121d8e45f97a3fd7c9f389819f935cba60c/src/MphRead.Native/Entities/Players/PlayerInput.cpp#L1305-L1395)
- [`PlayerSound.cpp`：PlayBeamShotSfx](https://github.com/Zection6V/Fruity-Prime/blob/ca12c121d8e45f97a3fd7c9f389819f935cba60c/src/MphRead.Native/Entities/Players/PlayerSound.cpp#L141-L173)
- [`NetShotEvents.cpp`：FireReady](https://github.com/Zection6V/Fruity-Prime/blob/ca12c121d8e45f97a3fd7c9f389819f935cba60c/src/MphRead.Native/Mods/Network/Gameplay/Weapons/NetShotEvents.cpp#L125-L149)
- [`PlayerEntityNetAim.cpp`：ModFireShotEvent](https://github.com/Zection6V/Fruity-Prime/blob/ca12c121d8e45f97a3fd7c9f389819f935cba60c/src/MphRead.Native/Mods/Network/Gameplay/Player/PlayerEntityNetAim.cpp#L915-L927)
- [`Sfx.cpp`：リスナー位置・方向](https://github.com/Zection6V/Fruity-Prime/blob/ca12c121d8e45f97a3fd7c9f389819f935cba60c/src/MphRead.Native/Sound/Sfx.cpp#L911-L944)
- [`PlayerProcess.cpp`：自分／他人の音源の更新](https://github.com/Zection6V/Fruity-Prime/blob/ca12c121d8e45f97a3fd7c9f389819f935cba60c/src/MphRead.Native/Entities/Players/PlayerProcess.cpp#L458-L475)
- [`PlayerEntity.cpp`：死亡時のソース別SFX停止](https://github.com/Zection6V/Fruity-Prime/blob/ca12c121d8e45f97a3fd7c9f389819f935cba60c/src/MphRead.Native/Entities/Players/PlayerEntity.cpp#L2079-L2110)
- [`Sfx.cpp`：SoundSource::StopAllSfx](https://github.com/Zection6V/Fruity-Prime/blob/ca12c121d8e45f97a3fd7c9f389819f935cba60c/src/MphRead.Native/Sound/Sfx.cpp#L534-L537)／[SfxInstance::StopSoundFromSource](https://github.com/Zection6V/Fruity-Prime/blob/ca12c121d8e45f97a3fd7c9f389819f935cba60c/src/MphRead.Native/Sound/Sfx.cpp#L1250-L1270)
- [`PlayerSound.cpp`：全音声停止とLongMuteの違い](https://github.com/Zection6V/Fruity-Prime/blob/ca12c121d8e45f97a3fd7c9f389819f935cba60c/src/MphRead.Native/Entities/Players/PlayerSound.cpp#L538-L619)
- [`Sfx.cpp`：ミュート係数](https://github.com/Zection6V/Fruity-Prime/blob/ca12c121d8e45f97a3fd7c9f389819f935cba60c/src/MphRead.Native/Sound/Sfx.cpp#L833-L851)
- [`Renderer.cpp`：Main対象のUpdateTimedSounds呼出](https://github.com/Zection6V/Fruity-Prime/blob/ca12c121d8e45f97a3fd7c9f389819f935cba60c/src/MphRead.Native/Renderer.cpp#L3660-L3675)

### 4.3 調査・修正手順（原因を決め打ちしない）

**手順1：SPEC-03の死亡防止とSPEC-01のカメラ退避を先に実装する。** その状態で射撃音消失が再現するか確認する。観戦者の不正な死亡が消えればSPEC-02も同時に解消される可能性はあるが、検証前に「解決済み」としない。

**手順2：最小限の一時診断ログを追加し、発生前後を比較する。**

- ローカルスロット、`MainPlayerIndex`、観戦対象スロット／世代、`SpectatorMode::IsSpectating()`、`FreeCamera()`、Sceneの`CameraMode`、被弾／死亡したスロットとその観戦フラグ。
- 各発射イベント：`NetShotEvents::FireReady()`の呼出有無、対象スロット、Shot sequence、`TryFireWeapon()`の成否、`PlayBeamShotSfx()`のSFX ID。
- 音声生成：`SfxInstance::SetUpInstance()`の結果、選択した音声ソース、`SoundInstance`の割当状態、`Source->Volume`、`Sfx::SfxMute`／`TimedSfxMute`／`LongSfxMute`。
- リスナー：`SfxInstance::GetListenerPosition()`、Facing／Up、射撃音源の位置、距離、ゲインの有限性。NaNや無効なカメラ参照がないこと。
- ログはデバッグ時のみ出力し、高FPS時の各音声チャンネルごとの常時文字列生成や大量ログは避ける。

**手順3：症状を3種類に分類する。**

- 「発射イベント自体がない」：ネットワークShot受信／再現と、観戦中の`Main`切替による入力／シミュレーション停止を修正する。
- 「発射イベントはあるがSFX生成に失敗」：SoundSourceやSfxInstanceの発声割当・寿命・ソース別停止の仕様を調査し修正する。
- 「SFXは再生されているが聞こえない」：`SfxMute`、ゲイン、距離減衰、リスナー位置／方位の更新を修正する。観戦中でもカメラ位置へリスナーを追従させる。

**禁止する対症療法：** 観戦開始時に音量を強制最大化する、死亡処理全体から`StopAllSfx`を無条件削除する、すべての射撃音を`PlayFreeSfx`に置換して距離減衰をなくす、リモートショットの再生タイミングを無根拠に変更する。いずれも別のオーディオ不具合や重複発射を誘発し得る。

### 4.4 回帰テスト

- ローカルが正常に生存したままフリーカメラで観戦している状態でも、全プレイヤーの通常射撃音・チャージ音・連射音が聞こえる。
- 他人視点を観戦中、観戦対象が死亡／退出／リスポーンしても、残存プレイヤーの射撃音が継続する。
- 再現用にローカル観戦者の死亡を発生させる旧挙動シナリオでは、修正後に被ダメージが防止され、他人の射撃音が消えない。
- 観戦先を切り替えた直後も音声リスナーは現行カメラ位置に追従し、左右定位と距離減衰が破綻しない。
- `NetShotEvents`で発射が1回なら射撃SFXも原則1回。ロストパケットや重複Intentで二重再生が増えない。
- Match End、Pause、Rejoin、Map Rotationで本来停止する音・継続する音が以前の仕様を維持する。

## 5. 共通実装方針

### 5.1 推奨する責務境界

| 責務 | 管轄 | 要求 |
| --- | --- | --- |
| 観戦状態・ターゲット選定・退避 | `Mods/SpectatorMode` | 唯一のターゲット有効性判定と切替イベントの管理 |
| スロットの世代・退出・再接続 | `Mods/Network/Gameplay/Player/NetPlayerLifecycle`、`Mods/Network/Session/NetSlotManager` | 世代／生存変更の事実を通知し、UIやカメラは直接操作しない |
| カメラ姿勢・Player/Roam切替 | `Renderer.cpp`の`Scene` | 無効ターゲット参照ゼロ、直前姿勢の安全な引継ぎ |
| 生存・被弾資格のポリシー | `Mods/Network/Gameplay/Player`に置く共通ルール | `ModInPlay`／`ModIsInPlay`／Claim／Damage／Intentが同じ基準を使用 |
| 最終的なHP・死亡制御 | `PlayerEntity::TakeDamage` | 戦闘対象外の観戦者を、環境・爆発・弾・直接呼出から保護 |
| 音声イベントと音量・位置の監視 | `PlayerSound`、`NetShotEvents`、`Sound/Sfx` | 発射通知／発声／ミキシング／カメラリスナーを別々に検証 |

### 5.2 手を入れない、または慎重に扱う部分

- 現行の被弾Claim・予測・権威サーバー規則を変更せず、**「対象が観戦中なら戦闘不能」**という適格性条件を入口に統一して加える。
- `Format`／`Entities`にネットワーク専用の大規模ロジックを埋め込まない。必要最小限の汎用ダメージ防壁以外は`Mods`に閉じる。
- 音声のOpenALバックエンドを作り直さない。SPEC-02の正しい原因が特定された箇所だけを直す。
- 既存の`SpectatorMode::Rejoin()`のスコア調整規則を維持する。観戦解除のタイミングが変わっても正の得点だけを0にする現行ポリシーを壊さない。
- 観戦者を「非接続プレイヤー」とみなさない。Roster参加、観戦のためのスナップショット受信、チャット、スコアボード表示は維持する。
- ネットワークの60Hzシミュレーションと高FPS描画を混同しない。ターゲット無効化は即時状態遷移で処理し、描画フレームごとの重い全プレイヤースキャンを追加しない。

## 6. 実装の優先順序

1. **P0-1：** サーバーの`ModIsInPlay`、`NetHitClaims`、`TakeDamage`、`NetDamage`、`Intent`の観戦資格判定を統一する。観戦開始／終了時の入力状態をクリアし、観戦者死亡を防止する。
2. **P0-2：** 観戦対象の退出／死亡／世代変更を`SpectatorMode`が認識し、自動再ターゲットまたは安全なフリーカメラ退避を行う。
3. **P1：** 射撃イベント・SFX割当・ミュート／リスナーの診断を行い、SPEC-02が残る場合のみ原因箇所を修正する。
4. **P1：** 検証したシナリオをC++ユニットテスト＋ネットワーク統合テストへ追加する。従来の戦闘／ショット回帰テストを維持する。

## 7. 完了判定（Definition of Done）

- [ ] 観戦対象の退出でフリーカメラまたは別プレイヤーへ自動切替できる。
- [ ] 有効な追従対象が0人になってもカメラ操作を継続できる。
- [ ] スロットの再利用、死亡、離脱、再接続で古い対象への参照が残らない。
- [ ] 観戦者に対するすべての戦闘／環境／即死ダメージが適切に拒否される。
- [ ] 観戦者への被弾Claimは権威側で確定せず、HP・死亡数が変化しない。
- [ ] 観戦中は操作・発砲・再生される旧入力の残留がない。
- [ ] 観戦解除後は通常の被弾・発砲・リスポーンが可能である。
- [ ] 観戦中やターゲット死亡／退出時、他人の射撃音が継続する。
- [ ] SFX生成ログと音声ゲイン／リスナー検証でSPEC-02の根本原因が確認され、再現テストが通る。
- [ ] Windows、Linuxなど対象環境のビルド・既存ユニットテスト・ネットワーク統合テストで回帰しない。
- [ ] デバッグ用ログは設定OFFで実行時オーバーヘッドを持たず、不要なログは削除される。

## 8. 調査の限界・確認済み範囲

本書ではGitHub上の基準コミットのソースを直接読み、処理経路と防御漏れを調べた。ソースの存在・分岐・呼出関係に関する記述は静的解析による直接の観察である。実機上の再現や、SPEC-02で射撃音が消失する直接の音声チャンネル状態までは観測していない。したがって、SPEC-01とSPEC-03はコード上の構造的欠陥として扱い、SPEC-02だけはログ取得による因果の確認を修正の前提とする。

**本書の作成時点では、GitHubへのファイル作成、コミット、プッシュ、ワークフロー再実行は実施していない。**
