# Desktop renderer switching: disappearing window fix (2026-10-01)

## Problem and repair

With `fd717d49`, switching either way could appear to close the application.
The replacement GLFW window is created with `StartVisible = false`, but
`RenderWindow::_startedHidden` remained false after revealing the original
window. Consequently `Reveal()` never showed the replacement, even though
the existing match continued simulating and rendering into it. Screenshot
readback and scene/frame identity alone did not detect this.

Reset the reveal lifecycle when recreating the platform window. Preserve its
current client size, location, border, maximized state and fullscreen floating
state directly, including when preference-based geometry recording is disabled.
The scene, player objects, network session and launcher screens survive.

The outgoing OpenGL device is now destroyed while its context is still current,
after scene/UI resources have been released. Previously `unique_ptr::release()`
abandoned the device allocation and its caches on every switch.

## Texture recovery and GPU rendering

Model textures now retain shared references and texture/palette/recolor IDs,
rather than a second expanded pixel image for each uploaded model texture.
On switching, reconstruct each texture from the original model asset under
the existing handle. The temporary expanded upload data is freed afterwards.
Model unload and scene release also remove these recovery records.

The first local fix used weak references and was committed as `095cdc29`
before push. A real settings switch exposed `A texture's source model expired
during renderer switching`: some textures outlive the uploading model instance,
and releasing the GPU mesh cache can release the last remaining model owner.
The recovery record must own the original model for the lifetime of the
scene-owned texture. Shared ownership preserves that asset without creating
a duplicate expanded pixel image. Model data contains no reference to the
scene, and model unload/scene release remove these owners.

HUD/dynamic uploads whose callers supply temporary pixel arrays still retain
recovery data. In the tested Alinos Perch match with Sylux and seven bots,
638 of 689 textures were restored from original model data; the remaining
51 textures retained 4,567,808 bytes (4.36 MiB). This is recovery storage,
not CPU rendering. Ordinary scene drawing remains on the selected GPU backend;
the change introduces no per-frame GPU readback or CPU rendering fallback.
Cross-API GPU memory sharing and eliminating all HUD recovery storage are not
implemented by this fix.

## Regression coverage and results

`FRUITY_SWITCHCHECK=1 -shellshot` now spawns the main player with seven active
bots, opens the pause menu, clicks Settings, changes the Renderer row, clicks
Apply, then clicks Resume. It repeats that settings path three times in the
same match. It checks the actual `GLFW_VISIBLE` attribute, backend change,
scene identity, advancing simulation frames, unchanged window geometry and
that the pause UI is hidden after Resume. Screenshots are taken after resuming.

The follow-up regression also uploads a texture from an uncached HUD model,
without creating a mesh, then releases the uploading model instance before
switching. The weak-reference version fails deterministically with the same
exception (`C:/tmp/gp/switch-source-before.log`, exit 1). The repaired version
must keep that source alive through every switch and resumed frame.
The final matrix passed all six source-retention checks and the source-release
check after ending the match through the normal production queue, in each
of the four configurations below. The synthetic probe adds one model texture
(690 total, 639 restored from original model data); recovery bytes remain
4,567,808, with no added expanded pixel copy.

Before the repair, the visibility assertions failed for the front-screen switch
and all three match switches (`switchfix-visible-before.log`, exit 1), while
the scene/frame assertions passed. After the repair:

| Offline Alinos Perch, Sylux + 7 bots | Result |
|---|---|
| Windowed, OpenGL start | exit 0, 3 settings switches + resumes |
| Windowed, Vulkan start | exit 0, 3 settings switches + resumes |
| Borderless fullscreen, OpenGL start | exit 0, 3 settings switches + resumes |
| Borderless fullscreen, Vulkan start | exit 0, 3 settings switches + resumes |

Each run also switched once on the front screen. All four runs passed visibility,
backend, scene continuity and geometry checks; no Vulkan validation messages.
Six PNGs per run, 24 total. Resumed game screenshots were visually inspected.

Build: `tools\build\build-cpp.bat msvc Release` passed. CTest: 5/5 passed.
Executable: `tools/build/out/msvc-Release/FruityPrime.exe`.
Final test logs and PNGs: `C:/tmp/gp/switch-source-v2-{windowed,borderless}-{opengl,vulkan}`
(logs have the same prefix plus `.log`). Runtime launcher preferences were
restored after the matrix.

```powershell
$env:FRUITY_SWITCHCHECK = '1'
$env:FRUITY_SHOT_ROOM = 'AD2 ALINOS PERCH'
# Run from tools/build/out/msvc-Release; use a fresh output directory.
.\FruityPrime.exe -shellshot C:/tmp/switch-check -rhi vulkan -vkvalidation -fpscap 60 -noupdate -debuglog
```

Android retains its next-start renderer setting. Online switching, unavailable
backend rollback, long-duration memory behavior and other GPUs/platforms were
not exercised by this matrix. The OS window is still recreated, so a brief
window transition remains possible.

## Follow-up: new impact effects and Lockjaw bombs disappear after switching

The launcher hunter preview keeps a separate side scene after entering a match.
`SwitchRenderer` releases that preview through `LauncherHunter::ReleaseGl`.
Its `Scene::ReleaseGpuResources` also called `Read::ClearCache`, which clears
the process-wide effect definitions and particle/model caches used by the match.
`Read::GetEffect` only looks up an already loaded definition; it does not reload
one. Consequently, effects already alive could continue drawing, but later
impacts and newly placed Lockjaw bombs had no definition to instantiate.
Switching back could not restore that CPU asset cache.

`ReleaseGpuResources` now clears the shared asset cache only for a main scene.
A side scene still releases all of its GPU resources and scene-owned source
references. Ending the match retains its normal cache cleanup. This adds no
expanded texture copies and changes no rendering or simulation algorithm.

The switch regression now opens PLAY, enters Offline and selects a map before
starting the Alinos Perch fixture, and asserts that the real hunter preview drew.
The earlier switch script skipped this path and had no preview scene to release,
so it could not expose this failure.

The fixture snapshots every loaded effect using weak references, places the
Power Beam impact emitter (effect 2) and a real `BombEntity::Spawn` Lockjaw bomb,
then waits six simulation frames and checks drawable particles. It retains the
first bomb through all three switches and places a fresh impact and bomb after
each Resume. Placement runs immediately before effect processing on a frame
with a simulation step, so startup hitches and draws between simulation steps
cannot skip the burst's initial emission window. PNGs capture the actual window.
This drives the production emitter/bomb placement paths, not mouse-trigger input
or Lockjaw snare triangles.

Before the repair, `C:/tmp/gp/switch-effects-baseline.log` exits 1: 105/105
definitions and two particles for each fresh effect before switching become
0/105 definitions and zero particles for fresh effects after each switch.
The already placed bomb still has two drawable particles, isolating the failure
to new effect creation rather than GPU texture recovery.

Final MSVC Release runs both exit 0:

| Offline Alinos Perch, Sylux + 7 bots, borderless | Result |
|---|---|
| OpenGL start | 3 settings switches, 105/105 definitions, fresh impact/bomb and existing bomb draw after every switch |
| Vulkan start | 3 settings switches, 105/105 definitions, fresh impact/bomb and existing bomb draw after every switch |

Each run also switches once on the front screen. All match continuity, window
visibility/geometry and texture-source lifetime assertions pass. Each checked
fresh impact, fresh bomb and retained bomb has two drawable particles. Seven
PNGs per run (14 total); OpenGL and Vulkan post-switch images were visually
checked. No Vulkan validation errors. Logs and PNG directories:
`C:/tmp/gp/switch-effects-final-v3-{opengl,vulkan}` (logs add `.log`).
Runtime launcher preferences were restored. Build and CTest (5/5) pass.
Executable remains `tools/build/out/msvc-Release/FruityPrime.exe`.

### Repeating the effect regression on Windows

Run these commands in PowerShell from the repository root. Requirements are
the MSVC/vcpkg desktop build environment, a display, working OpenGL and Vulkan,
and extracted game files configured by `paths.txt` beside the executable.
Use the actual game assets; the Vulkan foundation checks alone do not test this
match/launcher path. Khronos validation must be installed to obtain validation
coverage; confirm `validation=1` in the run log.

```powershell
cmd /c tools\build\build-cpp.bat msvc Release
if ($LASTEXITCODE -ne 0) { throw 'Release build failed' }
ctest --test-dir tools/build/out/msvc-Release --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'CTest failed' }
```

The following runs both starting backends sequentially and preserves the
launcher's settings. Do not run these tests concurrently: both processes write
the same `launcher.txt`. The unique directory keeps captures from different
runs separate. The test inherits the saved window mode, size and position.

```powershell
$captureRoot = "C:/tmp/switch-effects-$(Get-Date -Format yyyyMMdd-HHmmss)"
$savedSwitchCheck = $env:FRUITY_SWITCHCHECK
$savedShotRoom = $env:FRUITY_SHOT_ROOM
Push-Location tools/build/out/msvc-Release
$prefsPath = Join-Path $PWD 'launcher.txt'
$hadPrefs = Test-Path -LiteralPath $prefsPath
$prefsBytes = if ($hadPrefs) { [System.IO.File]::ReadAllBytes($prefsPath) }
try {
    $env:FRUITY_SWITCHCHECK = '1'
    $env:FRUITY_SHOT_ROOM = 'AD2 ALINOS PERCH'
    foreach ($backend in @('opengl', 'vulkan')) {
        $captureDir = "$captureRoot/$backend"
        $logPath = "$captureRoot/$backend.log"
        New-Item -ItemType Directory -Path $captureDir -Force | Out-Null
        'q' | & .\FruityPrime.exe -shellshot $captureDir -rhi $backend `
            -vkvalidation -fpscap 60 -noupdate -debuglog *> $logPath
        $exitCode = $LASTEXITCODE
        Select-String -Path $logPath -Pattern `
            'preview|effect definitions|match kept|backend changed|source released|VUID|Validation Error'
        if ($exitCode -ne 0) { throw "${backend}: switch regression failed; inspect $logPath" }
    }
} finally {
    if ($hadPrefs) { [System.IO.File]::WriteAllBytes($prefsPath, $prefsBytes) }
    elseif (Test-Path -LiteralPath $prefsPath) { Remove-Item -LiteralPath $prefsPath }
    $env:FRUITY_SWITCHCHECK = $savedSwitchCheck
    $env:FRUITY_SHOT_ROOM = $savedShotRoom
    Pop-Location
}
Write-Output "Captures and logs: $captureRoot"
```

The `q` input lets an error that falls back to the text launcher terminate
instead of leaving the unattended test waiting at its menu. It does not drive
the normal graphical regression. `-noupdate` avoids checking for updates.

Expected sequence and acceptance checks:

1. Switch on the front screen, then click PLAY, enter Offline and select a map.
   `pre-match production hunter preview yes` is required: skipping this step
   removes the side scene responsible for the original failure.
2. Start Alinos Perch with Sylux and seven bots, spawn the main player, and
   upload an uncached, texture-only model to cover source ownership.
3. Record the loaded effect definitions and place an impact plus a Lockjaw bomb.
   Before switching, expect `effect definitions 105/105`, `impact particles 2`
   and `Lockjaw particles 2`. The first `existing Lockjaw particles 0` is normal:
   that placement becomes the retained bomb for the subsequent checks.
4. Repeat Pause -> Settings -> Renderer -> Apply -> Resume three times in the
   same match. Every switch must change backend, keep the scene, advance its
   simulation frames, preserve window geometry and leave the window visible.
5. After every Resume, create a fresh impact and bomb. Require 105/105 identical
   definitions, two drawable particles for each fresh effect and two for the
   retained bomb. The texture-only model source must remain alive.
6. End the match through the normal shell queue. Require
   `texture-only source released with scene yes`. Both processes exit 0, with
   no failed-step message, `Validation Error` or `VUID` errors. On other fixture
   maps/builds, the definition count can differ; the retained count must equal
   the pre-switch count.

Each backend produces seven PNGs. Inspect `switch-effects-0-before.png`, then
`switch-3-match.png`, `switch-4-match.png` and `switch-5-match.png`: the fixture
places a yellow Power Beam impact to the left and a blue Lockjaw core to the
right of the crosshair. The earlier retained core may overlap the fresh core
or appear separately as the camera moves. The remaining PNGs cover the front
screen and match baseline. `-debuglog` also writes detailed logs into `logs/`
beside the executable, in addition to the redirected per-backend run logs.

The reported bug's characteristic failure is a healthy pre-switch count that
becomes 0/105 after the first switch, with empty fresh impact/bomb emitters,
even while an older bomb remains drawable. A successful build or continuing
simulation alone is not acceptance evidence for this regression.
