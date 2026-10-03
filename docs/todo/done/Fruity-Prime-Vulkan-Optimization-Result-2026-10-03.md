# Vulkan optimization implementation result — 2026-10-03

対象: [更新後レビュー v2](Fruity-Prime-Vulkan-Optimization-Review-Updated-v2.md)。
開始時の `develop3_rendering` HEAD は `dc1ffe004dadeba8f938d5519eb154206b667f06`。
P1-1〜5、P2-1〜3の generic policy とユーザー設定を実装した。
Metal/DX12 backend の実装は今回の依頼から除外。P2-4/P3は元文書の条件に従って下記の判断とした。

## 実装と所有権

| 要件 | 結果 |
|---|---|
| Small Draw Constants | backend-neutral `CommandList::SetSmallConstants`。`mat_alpha` / `alpha_test` は16 bytes。Vulkan push constants、OpenGL compact uniforms。`mtx_stack` 等の大きいデータは従来のbufferに残す。生成・reflection・ABI drift検査にもsmall blockを接続。 |
| Redundant scene descriptor binds | command-list内でnative setを追跡。compatible layout prefixとpush rangeを検査し、必要なgroupだけbind。command reset、generic path、resource destruction/resizeで無効化。 |
| Frame-local semantic cache | programのstable identity、logical group、実際のuniform buffer/offset/range、texture identity/generation/view、sampler identity/native handle、image layoutをkeyとする。command-slot recycleで消去。uniform sliceの再利用も同じgenerationの範囲内。各mapは16,384 entries上限。 |
| Fixed ABI slots | program admission時に非空groupごとに512 sets/command slotをbatch preallocate。poolの型別descriptor数は実際のlayoutから計算。64 layouts/slot上限。完成してからcursorを再利用し、native pool reset/free-set flagは使わない。超過分は既存generic allocatorにfallback。 |
| Persistent material/texture cache | Main materialのimmutable uniform bytesとresource identity/generationをkeyとする256 entries/command-list上限。bufferはVMA経由。cache hitではupload sliceのcopyを省く。in-flight entryは上書き/evictionしない。満杯時はframe-local/fixed/generic path。forget/resizeではrecorded workをflushしたserialでbuffer/poolをretire。 |
| Presentation Scheduler | backend-neutralなcap/refresh/present mode/deadline/accepted present ID/estimated target time/authority。simulation accumulator、command slot、swapchain retirement fenceを別責務のまま維持。成功したpresentだけID/deadlineを進め、unavailableではdeadlineを破棄。 |
| CPU pacing | Windowsはhigh-resolution waitable timer（利用不可時は通常timer/sleep）。deadlineはframe admissionにanchorし、blocking FIFO presentが消費した期間を再度sleepしない。Native authorityの場合generic sleepを行わない。 |
| Optional work admission | `QueryReadiness` / `TryBegin` / `TryPrepareOptionalWork` を既存command slotsへ接続。launcherのstand-alone hunter previewはBusy時にstep/collect/upload前にskipし、通常のportrait/cardを使う。primary matchはこのskip経路に入れない。 |
| Low Latency | SettingsにOff / On / On + Boost、共通の`low_latency=off/on/onboost`保存。requested/effective/provider/boost capability/fallback reasonを分離。runtime toggleでdevice/session/shaderを再生成しない。backend切替後もrequested modeを保持。 |
| One-frame budget | Onは**最新のsubmitted serial**が完成してから次のprimary frameを受け入れる。2msで打ち切るtimeline/GLsync waitの間にもeventsを処理。Busy時はsimulation/drawの記録前に入場を延期し、経過時間を次回へ保持する。GLのpending retirement markerをbudget waitが新規submitしないことも検査。 |

OpenGL/Vulkanの現在のproviderはGeneric、Boost capabilityはfalse。
On + Boostの選択は保持し、effective=Onと理由をUI/diagnosticsで公開する。
GPU clock boost、Reflex、Anti-Lagを実装したとは扱わない。
既存の2 frames in flight、SubmissionSerial、VMA/memory admission、persistent upload arena、pipeline cache、dynamic rendering、Synchronization2、readback/timestamps、typed presentation retirementを維持した。

## Descriptor測定

`FRUITY_RENDER_METRICS=1`のみで詳細counterを有効にする。
`tools/compare-scene-descriptor-metrics.py` は最もscene drawの多いstreamを選び、draw当たりに正規化する。
baselineは開始HEADに同じcounterだけを付けたビルド。room=`MP3 PROVING GROUND`、shot simulation frame=120。
shell loopのframe数は起動/実行時間によって変わるため、総数をそのまま比較しない。

| Native操作 / scene draw | baseline | P1-1〜3 | 最終実装 |
|---|---:|---:|---:|
| descriptor allocation | 0.335533 | 0.304585 | 0.019838 |
| descriptor update call | 0.335533 | 0.304585 | 0.199093 |
| descriptor bind command | 2.920658 | 0.309071 | 0.328919 |

最終実装はbaseline比でallocation約94.1%、bind約88.7%、update約40.7%減。
allocation counterはdraw hot pathのnative allocationであり、admission時のfixed batch allocationは含めない。immutable materialの新規作成は含める。
capacity fallbackは実際に通過している。これはdescriptor churnの測定であり、FPS改善率ではない。

既存`-fpsmeasure` / `-gpuprofile`で、room 95、2560x1439、unlimited/Immediate、validationとmetricsを有効にしてrenderer切替中の1秒窓も取得した。
GPU sampleを持つmatch窓はOpenGL 9窓、Vulkan 16窓。weighted GPU scene meanはOpenGL 1.270 ms、Vulkan 0.219 ms。
mean loop時間の窓medianはOpenGL 3.259 ms、Vulkan 13.425 ms。Vulkanでは詳細counterの大量出力も含む。
この測定は現在の診断実行の状況を示すだけで、同条件のCPU/FPS baselineがないためthroughput改善は証明しない。

## 検証

環境: Windows、MSVC Release、RTX 5070 Ti、NVIDIA 617.14。
ローカルのbuild/log/image/CSVはignoredな`tools/build/out/optimization-final/`に保存。ゲームデータとcapture画像はcommitしない。

| Gate | 結果 |
|---|---|
| `tools/build/build-cpp.bat msvc Release` | PASS。shader生成、SPIR-V reflection、native executable link。 |
| CTest | 20/20 PASS。small constants ABI、fixed descriptor exact counts/reuse/overflow/retirement、command readiness/backpressure、latest-submission budget timeout/error/completion、no retirement submission、deadline/authority/fallbackを含む。 |
| `-vulkanresourcecheck -noupdate` | exit 0、live=0、validation=1、errors=0。small constantsとnonblocking Beginも実GPU経路で検査。 |
| `FRUITY_SWITCHCHECK=1 FRUITY_LATENCYCHECK=1 -shellshot ...` | exit 0。Off→On→OnBoostを4回、計12 checks。保存/再読込、unsupported Boost、same device/scene、simulation進行、fullscreen/windowed、display cap/144/500、front切替+match内3切替、要求mode保持を検査。 |
| `-presentconformance -noupdate` | OpenGL 18 frames/2 present modes、Vulkan 21 frames/3 modes、両方PASS。resize、minimize時のnonblocking unavailable、restore。 |
| `-gpulifetime "MP3 PROVING GROUND" -cycles 3 -frames 30 -rhi vulkan -vkvalidation` | 3/3 PASS。各release後すべてlive=0、retired=0。completed/submitted=68/68、136/136、204/204。各cycle最後15 framesのhost waits=0。 |
| OpenGL/Vulkan `-goldencapture all` | 両方exit 0、7 candidatesすべてcapture。transparent/decal/particle/trail/HUD/fade/disruptionを含む。HUD/fade fingerprint distinct。 |
| Before/after shell-match画像 | OpenGL RGB完全一致。Vulkan MAE=0.027627/255、p99=0。simulation frameは固定しているがruntime captureは完全な決定的before/after証明にはしない。 |
| Golden OpenGL/Vulkan比較 | RGB MAE=0.011809〜0.032813/255、全candidate p99=0。差8超のpixel割合は0.021042%。完全なpixel一致ではなく、局所的なraster差を残す。 |
| Validation | 成功したruntime logsにVUID/Validation Errorなし。外部OBS/Bandicam layerのAPI-version警告は別扱い。 |
| Android arm64 | 変更したVulkan/OpenGL RHI objectsがNDK 27.2でcompile PASS。既存のRendererAndroid incomplete FramePerformance問題があるためfull APK/runtimeの合格とは扱わない。 |
| Asset guard / whitespace | guardと同じtracked-path/extension/allow/size policyをPythonで検査してPASS。`git diff --check` PASS。 |

最初のlatency harnessはfullscreen往復後にtest fixtureのwindow geometryが残り、switch witnessに6 missesを出した。
probe後に元のborder/size/locationへ戻す修正で解消し、後続の統合実行はexit 0。
通常のlauncher設定ファイルとbackupはテスト終了時に元のbytesへ戻した。

requested/effective/provider/boost/authority/reasonは変更時に`[presentation]`へ記録する（enum numeric: mode 0=Off,1=On,2=OnBoost; provider 0=None,1=Generic,2=Nvidia,3=Amd; authority 0=Generic,1=Native）。
`[presentation-metrics] present_wait_count/present_wait_ns`は現行generic **submission frame-budget wait**の実呼出回数/時間。display completionの測定や`vkWaitForPresent*`の呼出回数ではない。
optional Busy counterはVulkan metricsへ記録する。通常のshipping pathでは詳細時計計測を無効とする。

## 条件付き項目と検証の限界

- **P2-4 output lease ring:** 今回導入しない。現行のscene/Skia/window presenterは同じgraphics submission順序で処理し、capture readbackは既存ticketがcopyとcompletionを所有する。独立した非同期producer/presenterや複数の同時output consumerを追加しておらず、新しいpublication ownerを必要としない。
- **P3 specialization:** 今回導入しない。上記Vulkan GPU時間は約0.219 msであり、shader ALU/branchが問題という証拠を得ていない。頻繁なwindow/material/animation値をspecialization化してpipeline variantsを増やさない。
- **P3 native/vendor extensions:** present_wait2 / present_timing / Google timing / NV low_latency2 / AMD Anti-Lagは追加しない。generic accepted-ID/deadline/target-time/authorityと最新submission budgetを先に実装。display feedback、input-to-photon、vendor APIの効果を測る段階でcapability-drivenに追加する。
- Android device、Linux/macOS実行、Metal/DX12、remote CI、input-to-photon latencyの実測はこのローカルgateに含めない。GPU counter減少とruntime correctnessを、未測定のFPS/latency改善に読み替えない。
