# Known gaps — claims not yet verified

What's below is unproven or partially proven, not broken. Say so rather than
claiming coverage that isn't there.

- **The one launcher has never run on Windows or macOS.** Same code on all
  three desktops now, but the only machine that's shown it is this WSL box
  (front screen, settings, map grid, pause menu — driven and screenshotted
  over X11). Windows changes two things this can't check: it's a GUI binary
  with no console, and GLFW/Avalonia share a message queue instead of two X
  connections.
- **Nobody has played a match from the launcher window.** It starts one and
  the launcher window goes away when it does (checked), but this box can't
  show a GLFW window at all (`Scene.OnRenderFrame` never produces a frame
  under its GL), so "Escape opens the pause menu over a running match" is
  proven on the menu's side (flags, windows, the pump) and unproven on the
  game's.
- **macOS is cross-compiled and unrun.** See `.claude/launcher/LAUNCHER-OVERVIEW.md`.
- **The Android match runs on an emulator; how it *looks* there proves
  nothing.** With the game files copied onto the device, an emulator (API 30,
  x86_64, software CPU and SwiftShader) has been driven front screen → offline
  match → first person with the HUD, from a cold start in portrait. So
  `Mods/Render/GlEs.cs` — immediate mode, display lists, the current colour,
  the alpha test — does load a room and draw it. But SwiftShader puts vertical
  streaks through every surface in that build, with cel shading on and off
  alike, so every picture from it is good for "it ran" and for nothing else.
  Rendering is judged on the desktop. `.claude/android/ANDROID-PORT.md` lists
  what to watch on a first run, in order.
- **A phone is still a different machine** — the emulator is x86_64 with
  SwiftShader, a phone is arm64 with a real driver. That is the ABI and the GL
  implementation both differing from what is tested here.
- **The update check has never seen a release of this repository.** Tested
  against upstream NoneGiven/MphRead instead, which has releases: the check,
  version comparison, "update available" line and page URL were all
  exercised that way. Not covered: an asset name actually matching this
  project's — the "no matching asset" path got tested, the matching one only
  by unit test.
- **No browser has actually been opened.** `OpenPage` was only exercised
  where it correctly declined (headless, no `DISPLAY`). `xdg-open` on a real
  desktop and `UseShellExecute` on Windows are untried.
- **The rename leaves an unrun migration on the Pi.** `deploy-server.sh`
  rewrites an `ExecStart` still naming `MphRead` and deletes the old binary,
  but that code path hasn't run against the real box yet. Check
  `systemctl cat mphread-server` after the first deploy following the rename.
- **The ARM64 server package has never been started by CI** — cross-compiled
  on an x64 runner, so `check-dedicated-server.sh` can't run it there.
  `linux-x64-server` (same build config, a processor the runner actually has)
  is the nearest CI gets; the Pi via `deploy-server.sh` is the real test.
- **The Windows dedicated server is started in CI, but only there.** Checked
  on every push via the `windows-server` job, but nobody has run it on a real
  Windows machine behind a real firewall for a long session, unlike the Linux
  server on the Pi.
- **Late joiners and bursty features skew the tour's numbers**, not the
  replication. Clients start ~3 s apart; a client that joins a bursty phase
  (bombing, unmorphing) late reports a fraction of what the subject did, and
  time-normalisation can't fix a burst it wasn't there for. Judge against
  clients that were present, not the raw tally.
- **Alt-attack presses read ~60% on every observer.** Not loss (loss would
  differ per observer) — two presses inside one intent window arrive as one,
  since the edge history is ORed into a single mask per packet. The bombs
  those presses would have laid still land 79-99%.
- Kanden and Spire show lower fidelity than other hunters on `unmorph` and
  projectile lifetime. Not explained.
- The scoreboard rows tighten to fit past four players, down to 19 px; beyond
  eight would need a second column.
- The First Hunt "biodefense chamber" rooms are listed as multiplayer but
  carry no player spawn points — survival rooms, kept out of the launcher's
  map list and out of any Battle rotation rather than "fixed".
- `zoom` and `double damage` are usually `untested`, since nothing in the
  tour reliably picks either up. `double damage` is probably fine — item
  pickup is simulated from replicated positions and three clients in a 90 s
  match agreed exactly (`12`, `12`, `12`) — but "probably fine" isn't
  "measured".
