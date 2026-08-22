# Launcher — settings

Details about the settings window: layout, saving, and key toggles.

Layout

- A rail of sections down the left -- Display, Audio, Controls, Match rules, Features, Cheats, Bugfixes, Map previews -- and one page at a time on the right, using `LayoutPages` so labels measure properly after layout.

Saving and applying

- Saving writes the file and applies it. `Mods.GameSettings.Apply` makes volume and language changes take effect immediately. `ApplyMatchRules` applies match rules into the renderer after `GameState.Setup` has chosen defaults.
- The launcher writes two files beside the exe: `controls.txt`, `launcher.txt`. Code catches `UnauthorizedAccessException` and other exceptions when writing under Program Files.

Notable toggles

- Window modes: windowed or borderless fullscreen. `Mods.WindowMode` owns it; F11/Alt+Enter toggle at any time. Escape opens pause menu instead of leaving fullscreen.
- Helmet opacity: separate sliders for helmet shell layers and visor; `-nohelmet` zeroes both.
- Controls: `Mods.InputSettings` holds canonical `PlayerControls` and writes to `controls.txt`. Rebinds apply to running players via `ApplyToPlayers`.

