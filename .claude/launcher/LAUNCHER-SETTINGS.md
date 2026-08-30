# Launcher — settings

Details about the settings window: layout, saving, and key toggles.
It is `Mods/Launcher/Gui/SettingsWindow.cs`, and it is the same window on every
platform and from both places that open it (the front screen, and the pause menu
during a match).

Layout

- A rail of sections down the left -- Display, Audio, Controls, Match rules,
  Launcher, Features, Cheats, Bugfixes -- and one page at a time on the right.
- The selected section is marked with the accent on its bar and label
  (`MenuEntry.Selected`), which is deliberately not the same as the hover fill:
  "this is where you are" and "this is what the pointer is over" are two
  different things.
- Rows stretch to the page: Avalonia measures them with the width the panel
  gives them, so nothing here needs the manual pass the WinForms window did.
  The page's inset is its own `Margin` and not the `ScrollViewer`'s `Padding`,
  which is not taken off the measured width.
- The footer's button says **Save and close** from the launcher and **Apply**
  from a match.

Sections

| Section | What is on it |
|---|---|
| Display | window mode; performance and cel shading; **Pro mode HUD**; the helmet switch with its two opacity sliders; crosshair, modern HUD and weapon-list size; fixed weapon |
| Audio | sound-effect and music volume; the game's text language |
| Controls | mouse sensitivity, invert either axis, and every key binding, plus reset to defaults |
| Match rules | point goal, time limit, damage level, team play, friendly fire, hunter radar, affinity weapons |
| Launcher | your name and hunter, the default server, the server directory, and whether to check for updates. These live in `launcher.txt`, not `settings.json` |
| Features / Cheats / Bugfixes | every `public static bool` on those three classes, by reflection, so the list cannot drift |

Saving and applying

- Saving writes the file **and applies it**. `Mods.GameSettings.Apply` makes the
  volumes and the language take effect at once -- which is what makes the music
  slider work *during* a match, since this window also opens from the pause menu
  -- and `ApplyMatchRules` applies match rules from the renderer after
  `GameState.Setup` has chosen the mode's defaults.
- `Commit` runs inside a `try`; a failure becomes a line in the footer rather
  than an exception thrown out of a window that may be sitting over a match.
- The launcher writes two files beside the exe: `controls.txt` and
  `launcher.txt`. Both catch every exception, not only `IOException`: an install
  under Program Files raises `UnauthorizedAccessException`, which is not one.
  `LauncherPrefs.Directory` is settable for platforms whose package directory is
  read-only -- the Android head points it at the app's data directory.

Notable toggles

- **Fog, lighting and texture filtering are read, not copied.** They live in
  `Mods.RenderOptions` and `Renderer` reaches them through the `FogOn`,
  `LightingOn` and `FilteringOn` properties every time it needs them. They used
  to be fields initialised from `RenderOptions` when the scene was built, and
  since this window opens from the pause menu *during* a match, turning fog off
  there did nothing at all until the next room -- while the debug key G, which
  wrote to the field, worked. Now the key writes to the same place the settings
  do, so the two can no longer disagree.
- **A render default has to be changed in two places.** `RenderOptions` holds
  the engine's; `MenuSettings` in `Menu.cs` holds the settings file's, and that
  one wins wherever settings are loaded, which is every path a player takes.
  `CelEdge` was 1 in one and "75" in the other for exactly as long as it took
  to notice the outline was not the strength it was supposed to be.
- Window modes: windowed or borderless fullscreen. `Mods.WindowMode` owns it;
  F11/Alt+Enter toggle at any time. Escape opens the pause menu instead of
  leaving fullscreen.
- Helmet opacity: one switch over both `HelmetOpacity` and `VisorOpacity`, with a
  slider each behind it. Clearing only the shell is what leaves a tinted pane
  with nothing behind it; `-nohelmet` zeroes both.
- **Pro mode HUD is an override, not a preset.** `Features.ProHud` makes
  `ModernHud`, `FixedWeapon`, `FixedCrosshair`, `CustomCrosshair`,
  `WeaponListScale` (1.7) and both helmet opacities (0) *answer* as the pro
  layout while it is on; each keeps a `*Setting` companion holding what the
  player chose, and that is what the settings rows are seeded from, what
  `Features.Commit` writes, and what comes back when it goes off. Writing the
  overridden values to disk instead would turn one night of pro mode into a
  permanent change to six settings, which is the bug this shape exists to
  avoid.
- **Everything it answers for disappears from the page while it is on**, headings
  and explanations included -- `BuildDisplay` records the range of the page's
  children it built after the Pro mode row and `UpdateHudRows` flips
  `IsVisible` over that range, so a row added to one of those sections later
  cannot be left behind on its own. A range rather than a hand-written list for
  that reason; hidden rather than greyed because six rows showing values that
  are not what the game is drawing are six chances to conclude the switch is
  broken. It also draws its own energy, ammo and score
  (`Mods/Render/PlayerEntityProHud.cs`) in place of the game's, which are
  suppressed in `DrawHudObjects` and `DrawModeScore`.
- **HUD readouts opacity is no longer a setting.** `Features.HudOpacity` stays
  at 1 and is out of `Load`/`Commit`; the slider is gone. It is still read
  throughout the HUD, so it remains the hook for anything that wants to fade
  the readouts.
- Controls: `Mods.InputSettings` holds the canonical `PlayerControls` and writes
  it to `controls.txt`. A rebind made from the pause menu also goes through
  `ApplyToPlayers`, because the players in a running match already hold their
  own copies. `KeyRow` maps the toolkit's key enumeration to GLFW's, and refuses
  anything unmapped rather than binding it to whatever key shares its number.
