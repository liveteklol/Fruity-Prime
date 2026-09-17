# Bright player skins

Display settings has a Visibility section with **Bright player skins**. It is
off by default, persists as `bright_skins=false` in `launcher.txt`, and applies
on Save/Apply without restarting. It is a client preference; no server rule,
packet, asset, texture, model instance, or rendering pass was added.

`Mods/Render/BrightSkins.cs` owns eligibility and color policy. FFA reads the
player's resolved `Recolor`, then the representative palette color cached by
`HunterSuits`. That cache samples all six palettes, including the two team
palettes; the suit picker and collision resolver still offer four FFA suits.
Team play uses `Metadata.TeamColors[TeamIndex]`, the centralized 5-bit colors
already used by objectives and the HUD. Invalid identities fall back to a
neutral color.

Colors raise the strongest channel to 0.95. If luminance remains below 0.45,
the smallest blend toward white that reaches the floor reduces saturation
without rotating hue. Simply multiplying and clamping cannot bring saturated
blue to that floor. These are rendering tuning values, not protocol constants.

The normal and alternate body paths compute color once per draw and forward it
through the existing mesh traversal, including Kanden segments and Spire's
alternate attack. Weavel's turret caches the owner's color for its body draw;
its separate ice model does not receive it. Guns, smoke, ice overlays, trails,
particles, projectiles, shadows, scan geometry and HUD draws keep their existing
paths. Neither visibility tests nor depth/culling state change.

The override is suppressed for the main player, single-player, death, damage
flash/palette override, and Double Damage. Cloak checks include both the flag
and current/target alpha: passive Trace/Prime Hunter cloak does not set that
flag, and expiration fades outlast it. Remote bodies remain eligible while the
local player spectates.

Both existing shaders apply the RGB override before cel processing and fog.
Textured geometry keeps material and texture alpha with override alpha 1.
Untextured geometry replaces alpha in those shaders, so `ForMaterial` supplies
material alpha times entity alpha for that path (the vertex shaders emit alpha
1). The Debug viewer's texture toggle also selects that untextured branch, even for
a material with a texture, so color preparation reads the scene's toggle too.
Placeholder override semantics are unchanged. There are no shader changes.

## Checks

`-brightskinscheck` runs without game assets or GL. It checks eligibility,
cloak transitions, status precedence, material alpha handling, 4096 input
colors against the luminance floor, centralized team colors, fallback, and
preference round trips in a temporary directory.

`-brightskinscheckassets` additionally requires the user's extracted files and
checks all seven multiplayer hunters: four resolved suits stay distinct after
normalization, repeat lookups agree, both team palettes are sampled, and invalid
recolors safely fall back. No proprietary data is included in these tests.

The automated checks do not replace visual acceptance on Windows and Android:
check every hunter in normal/alt form, four players sharing a hunter, teams,
turret, damage, freeze, Double Damage, active/passive cloak and fades, death and
respawn, spectators, cutouts, fog and cel shading, and wall occlusion. Use an
isolated build's `launcher.txt` to enable the preference for `-maptest`; the
headless command dispatcher loads launcher preferences too.
`-maptest` assigns alternating valid team indices in team modes, matching
launcher bot matches; FFA retains its existing per-player identities.

### Validation on Windows (2026-09-16)

- Release solution and Windows dedicated-server builds: no warnings or errors.
- Android Release APK build: succeeds with 14 XML documentation warnings in
  existing code. No Android device was connected for an ES gameplay run.
- Both brightskins checks pass, including all seven hunters' real palettes.
- Eight-player `MP3 PROVING GROUND` FFA (fog on, cel off) and team (fog and cel
  on) smoke runs complete with exit 0. Captures show bright body colors and
  orange/green team identity. The team run with the preference disabled has
  matching simulation totals (2885 frames, 7 deaths, 2818 effect particles).
- The harness's affliction sub-probes still report failures: burn in FFA and
  all three in the team run, also present in the corresponding disabled
  controls. These runs are rendering smoke coverage, not full status acceptance.
- The original baseline hit a native shutdown failure; subsequent smoke runs
  used `ALSOFT_DRIVERS=null` and exited normally. Launcher `-uishot` also completes.

The full visual matrix above remains pending, especially Android gameplay,
four simultaneous same-hunter players, spectator/cloak transitions, and every
hunter/status combination. Keep issue #32 open until that acceptance is done.
