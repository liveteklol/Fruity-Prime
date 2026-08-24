# Launcher — overview

This file summarises the launcher features and where its code lives.

Basics

`MphRead -launcher` opens a front screen, not a settings dialog: a map picture
on the left, the things you can do on the right. Everything that is not a
per-session choice lives in the settings window, which is one of the entries and
is also what the pause menu opens mid-match.

| Entry | What it does |
|---|---|
| Adventure | save slot, hunter, continue or start over |
| Play online | name, hunter, `host` or `host:port`, and a live line saying what that server is running. **Find a server** opens the browser below. |
| Play offline | map, mode, 0-7 bots and their skill, hunter, and straight into the match. **See every map** opens the picture grid |
| Host a game | the same choices plus a port. **Runs the dedicated server in this process** and joins it over the loopback |
| Settings | display, audio, controls, match rules, launcher preferences, features, cheats, bugfixes |
| Game files | where the .nds goes. Shown first, and everything else greyed out, when there is nothing set up yet |

Key implementation notes

- **One launcher, in Avalonia, on every platform.** Windows, Linux and macOS run
  the same screens; there is no second toolkit and no per-platform launcher any
  more. `Mods/Launcher/Gui/` is the whole of it.
- Every control is painted by this code (`GuiTheme`, `MenuEntry`, `ChoiceRow`,
  `SliderRow`, `KeyRow`, `SplashView`); only the text boxes and scroll bars are
  stock, under Fluent dark.
- The picture is a map preview out of `thumbnails/`, rendered from the user's own
  files -- no art is shipped. A `splash.png` beside the exe replaces the home
  picture.
- Choices live in `launcher.txt` beside the exe (`LauncherPrefs`) and keys in
  `controls.txt` (`InputSettings`).
- Two front screens coexist: the window (`Mods/Launcher/Gui/`) and the text one
  (`Mods/Launcher/Portable/TextLauncher.cs`), over shared logic in
  `Mods/Launcher/Portable/`.
- `-launcher -text` forces the text launcher; the code falls back to text when
  there is no display.

Windows, and the loop

- A bare invocation opens the launcher on Windows and macOS -- the platforms
  where a program is normally started by double-clicking it. On Linux it still
  opens upstream's console menu, which is the screen people there are already
  using; `-launcher` is how they ask for the window.
- The toolkit is set up **once per process, on the game's own thread**, and each
  visit to the launcher is a nested dispatcher loop
  (`GuiLauncher.EnsureSetup`/`Ask`). One launcher, then a match, then the
  launcher again; "Quit" and closing the window are what end the program.

First-run behaviour and progress

- First run shows only the game-files card until extraction completes.
- The progress bar is milestone-driven: `SetupProgress` classifies output into
  phases rather than counting files first.

macOS and Android

- **macOS** publishes like any other desktop target (`osx-x64`/`osx-arm64`,
  cross-compiled on the Linux runner), and OpenAL ships with it
  (`libopenal.1.dylib`, keyed on RID rather than a Windows/Linux special
  case). **Nobody has started one.** Both packages are cross-compiled and
  unrun; the thing to watch is GLFW and AppKit sharing a process and a main
  thread, which the one-thread launcher arrangement is designed for and no
  Mac has confirmed.
- **Android** is `src/MphRead.Android/`, a head project compiling the same
  sources with `ANDROID` defined. What runs today is a front screen only --
  the server directory's list with a live status query per server -- because
  the engine draws through OpenTK and reads input from GLFW, neither of which
  exists on Android. The value today is that the head **stops building** the
  moment shared code grows something desktop-only; it already forced out
  `LauncherPrefs.Directory` (an Android package's directory is read-only, so
  the head points `launcher.txt` at the app's data directory) and the
  `ANDROID` guard in `GuiLauncher` (Android stands the toolkit up from its
  activity, with no desktop backend to detect).
- Building Android needs the workload and an SDK:
  ```bash
  dotnet workload install android
  dotnet build src/MphRead.Android/MphRead.Android.csproj -c Debug \
    -p:AndroidSdkDirectory=$HOME/android-sdk
  ```
  `EnableAvaloniaXamlCompilation=false` is deliberate: there is no XAML in
  this project (every screen is C#), and with AvaloniaResource items present
  the XAML compiler runs anyway and disagrees with the Android SDK about
  where it left the assembly, failing the build after a clean compile. The
  `avares://` assets are embedded by a different target and are unaffected.

See also: .claude/launcher/LAUNCHER-DESIGN.md, .claude/launcher/LAUNCHER-SETTINGS.md, .claude/launcher/LAUNCHER-FIRSTRUN.md
