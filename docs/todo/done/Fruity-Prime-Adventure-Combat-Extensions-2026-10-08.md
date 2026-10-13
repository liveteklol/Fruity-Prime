# Adventure combat extensions (Native C++)

ユーザー指定の拡張動作。以下はEU1.1 ROM parityの対象外であり、ROM解析で確認した仕様として扱わない。

## Weavel Halfturret enemy targeting

`src/MphRead.Native/Mods/Combat/Extensions/HalfturretEnemyExtension`が敵の索敵・有効性判定・照準位置を担当する。

- SinglePlayerのプレイヤー所有turretに適用。Storyの敵hunter botには適用しない。
- 元のhunter索敵・retaliation targetを優先し、targetがなければ15 units未満で最も近い敵のhurt-volume centerを選ぶ。
- HP 0、CollideBeamなし、Invincible、Battlehammer effectiveness Zeroの敵は選ばない。
- 保持中の敵が死亡・sceneから削除・無敵化・範囲外になった場合は、次のnative tickでtargetを解除する。
- 敵への照準はhurt-volume center。元のhunterへの照準はPositionのまま。
- 射撃は既存のBattlehammer projectile / cooldown / freeze処理を使用し、damageは既存のbeam collision / EnemyInstanceEntity::TakeDamageに委譲する。壁への衝突も既存のprojectile処理に従う。
- extensionはnative 30 Hzの意思決定で実行。HUDの60 Hz更新は独立。

## Sylux Lockjaw enemy attacks

以前追加したアドベンチャー敵対応もextensionとして位置づける。`Mods/Combat/Extensions/LockjawEnemyExtension`へ移動し、既存の挙動を維持する。

- ワイヤー接触の20 damage、三角snare時の各bomb 60 damage設定とImpact messageをextensionが担当する。
- 敵への爆発判定はhurt volumeとのsphere overlapをextensionへ委譲する。
- `LockjawCollision`はhunter側とも共有する幾何判定だけを担当する。
- `BombEntity`はchain発動・追尾・爆発・pool lifecycleを担当する。追尾中の敵のshared ownershipはここで保持し、削除中のdangling pointerを防ぐ。
- `EnemyInstanceEntity::TakeDamage`がInvincible / NoBombDamage等のdamage protectionを担当する。
- hunterのwire/snare判定および他のbomb typeの爆発判定は従来処理を使用する。

## Validation

2026-10-08、MSVC Releaseのgame / Weavel parity / Dialanche collision targetのbuild成功。

| 検証 | 結果 |
|---|---|
| Weavel EU1.1 production checks | 421 checks成功 |
| Weavel Adventure extension checks | 29 checks成功。実Battlehammer projectileの命中・damageを含む |
| Lockjaw enemy regression | 93 checks成功 |
| CTest: WeavelAltFormParity / DialancheNativeCollision / IntentTouchPayload / NativeTouchState | 4/4成功 |
| `git diff --check` | 成功 |

実行logsはignored `tools/build/out/adventure-extension-validation/`。新規extension checksの実装は`Mods/Diagnostics/WeavelEnemyExtensionCheck.cpp`に分離し、ROM parity検証とは別に出力する。

実assetを使用する`-weavelaltcheck "MP3 PROVING GROUND"`はROM parityのproduction checksとextension checksを別々に報告する。extension checksはoffset hurt volumeに対する実projectile damage、native phase、freeze、hunter優先、敵の削除・死亡・無敵化・範囲外、SinglePlayer/bot制限を検証する。

`-lockjawenemycheck "MP3 PROVING GROUND"`は既存のwire / triangle / explosion / immunity / removed-target lifecycleの回帰検証。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/check-weavel-alt.ps1 -OutputDirectory tools/build/out/adventure-extension-validation
```

物理入力によるアドベンチャー実プレイやROMとの比較は、この自動検証の証拠に含めない。
