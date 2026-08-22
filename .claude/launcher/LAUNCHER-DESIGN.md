# Launcher — design and UI

This document describes UI components, the logo handling, and differences between WinForms and Avalonia.

Painting and controls

- The launcher draws its own controls for a consistent dark theme. WinForms stock controls do not theme correctly.
- Controls: `LauncherTheme`, `MenuButton`, `ChoiceRow`, `HunterPicker`, `FieldBox`, `SplashPanel`.

Logo and assets

One source image, chroma-keyed and cropped into four files under `src/MphRead/Assets/`. All four allow-listed in `tools/asset-guard-allow.txt` (PNG and JPEG are otherwise banned extensions).

| File | What | Used by |
|---|---|---|
| `fruity-prime-logo.png` | the wordmark, cherry and text together | the game-files card and the README |
| `fruity-prime-mark.png` | the cherry alone | the Avalonia window icon |
| `fruity-prime.ico`, `fruity-prime-server.ico` | ICO frames for Windows | `ApplicationIcon` |

Notes on ICOs: 256×256 is the ICO format's ceiling; the source crop carries detail up to ~460 px so 256 is a downsample.

WinForms vs Avalonia

- WinForms: borderless window, custom-drawn controls. Avalonia: framed window (so it can be moved under many WMs) and uses stock text boxes/scroll bars while drawing other controls.
- Both share logical settings structures (`LauncherPrefs`, `GameFiles`, `MatchStart`) so behaviour is consistent.

Pause menu

- Runs on its own STA thread and message loop; communicates via volatile flags to the game.
- Centres on the game window, not the desktop; settings dialogs are TopMost while in-game to avoid z-order problems.

Implementation pitfalls

- `ApplicationConfiguration.Initialize()` throws if a form already exists in the process; code swallows that to allow the pause menu to appear.
- `Commit` saving settings writes the file and applies them; it catches exceptions and turns them into footer messages rather than letting an exception kill the menu thread.

