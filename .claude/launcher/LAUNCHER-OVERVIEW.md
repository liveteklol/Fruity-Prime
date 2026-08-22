# Launcher — overview

This file summarises the launcher features (Windows, Linux, text) and where its code lives.

Basics

`MphRead -launcher` opens a front screen, not a settings dialog: a map picture on the left, four things you can do on the right. Everything that is not a per-session choice lives in the settings window, which is one of the four and is also what the pause menu opens mid-match.

| Entry | What it does |
|---|---|
| Play online | name, hunter, `host` or `host:port`, and a live line saying what that server is running. **Find a server** opens the browser below. |
| Play offline | map, mode, 0-7 bots and their skill, hunter, and straight into the match |
| Host a game | the same choices plus a port. **Runs the dedicated server in this process** and joins it over the loopback |
| Settings | display, audio, controls, match rules, features, cheats, bugfixes, preview generation |
| Game files | where the .nds goes. Shown first, and everything else greyed out, when there is nothing set up yet |

Key implementation notes

- Every control is painted by this code (`LauncherTheme`, `MenuButton`, `ChoiceRow`, `HunterPicker`, `FieldBox`, `SplashPanel`).
- The picture is a map preview out of `thumbnails/`, rendered from the user's own files -- no art is shipped. A `splash.png` beside the exe replaces the home picture.
- Choices live in `launcher.txt` beside the exe (`LauncherPrefs`) and keys in `controls.txt` (`InputSettings`).
- Three launcher frontends coexist: WinForms, Avalonia (non-Windows), and a text launcher (`Mods/Launcher/`, `Mods/Launcher/Gui/`, `Mods/Launcher/Portable/`).
- Windows keeps WinForms and never sees Avalonia; packages are referenced only when `MphReadAvalonia` is set.
- `-launcher -text` forces the text launcher; the code falls back to text when no DISPLAY is present.

First-run behaviour and progress

- First run shows only the game-files card until extraction completes.
- The progress bar is milestone-driven: `SetupProgress` classifies output into phases rather than counting files first.

See also: .claude/launcher/LAUNCHER-DESIGN.md, .claude/launcher/LAUNCHER-SETTINGS.md, .claude/launcher/LAUNCHER-FIRSTRUN.md
