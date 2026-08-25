# Custom maps: the generator and the Quake 3 importer

Everything lives in `src/MphRead/Mods/MapGen/`. Two upstream files carry a
change and both are one token: `RepackCollision` gained the word `partial`,
and `Metadata`'s two room tables are wrapped in a call that appends the custom
rooms. `Scene` was already `partial`, so the preview camera needed nothing.

## What a room is, and what had to be built

A room is four files: `_Model.bin` (geometry, materials and, here, the
textures inline), `_Anim.bin`, `_Collision.bin`, and `_Ent.bin` under
`levels/entities`. Upstream already had writers for all three real ones -- they
existed to round-trip the game's own files in a test -- so the work was
feeding them structures that never came from a file:

| Piece | Where | Note |
|---|---|---|
| `Repack.PackModel` | upstream | takes `Node`/`Material`/`Mesh` objects, which only have constructors from the raw structs |
| `RawStructs.cs` | new | writes those raw structs byte by byte and marshals them back, so no upstream constructor had to be widened |
| `MapCollisionPacker.cs` | new | writes the collision file. Upstream's packer produces the same bytes but is quadratic twice over -- it scans the point list it is building, and asks every grid cell about every face. Fine for a hundred faces, hopeless for the six thousand a converted level has |
| `Repack.PackEntities` | upstream, was private | reached through the partial class |

## Traps that cost time, in the order they bite

- **Display lists are packed four opcodes to a word.** A list whose length is
  not a multiple of four cannot be written at all; pad with `NOP`.
- **A room needs a node whose name starts with `rm`**, or `RoomEntity.Setup`
  finds no room part and asserts. It must also *have a child*: entities
  reference a node by name, and `GetNodeRefByName` asserts `ChildIndex != -1`.
  Hence two nodes -- `rmMain` with no meshes, and `geo1` under it with all of
  them.
- **A node name starting with `_` is layer-filtered.** Anything else is in
  every layer, which is what a custom map wants.
- **`Node.MeshId` is a byte offset**, not an index: it is the first mesh times
  two.
- **Collision `LayerMask` is not just a layer mask.** Its low two bits are the
  normal's primary axis, which the point-on-face test reads to choose its
  projection plane; bit 2 means "in every layer". Get the axis wrong and the
  surface is there but nothing stands on it.
- **A jump pad with no `TriggerFlags` never fires.** It needs `PlayerBiped`,
  `PlayerAlt`, and `IncludeBots` or the bots ignore it. Its volume is
  entity-local and its `BoxPosition` is the *corner*, not the centre.
- **Camera limits are inherited from the room metadata**, so a custom map
  leaves them zero rather than borrowing another room's box.
- **The collision grid is indexed with 16 bits.** Cell size is fixed at 4
  units (the run-time lookup divides by four), so a big level means a lot of
  cells, and each face is listed in every cell it touches. Past 65535 listings
  the format cannot express it -- `MapCollisionPacker` says so by name instead
  of writing something that half works.
- **A Quake level is sealed inside a shell of sky brushes.** Importing it puts
  hundreds of units of empty grid around the level: wrackdm17 measured
  372x244x294 units with the shell and 157x77x176 without, which is the
  difference between overflowing that 16-bit index and fitting comfortably.
- **Vertices are 16-bit fixed point**: model space is +/-8 units, multiplied by
  the model scale of 2^`scaleFactor`. Texture coordinates are 1.11.4 and run
  out at 2047 texels, which is why both the brush projection and the importer
  rebase UVs per face.

## Textures

Two ways, and the second is the better one.

**Baked from the level's own art.** `tools/bake-textures.py` decodes the
shaders a level draws with, scales them to 64x64 and quantises each to a
256-colour palette, into a `.tex` pack the packer turns into `TextureInfo` and
`PaletteInfo` directly -- one material per shader. Ahead of time, because the
conversion runs on the machine that plays the game and the Android head has no
JPEG decoder: its STB natives are desktop builds, left out of the APK on
purpose. Baking is what lets a converted map look like itself, and it takes
the cartridge out of the loop entirely -- a room generated this way contains
nothing from the dump.

A shader with no image of its own (a light or an effect, defined in a
`.shader` script rather than a file -- four of wrackdm17's twenty-two) has its
surfaces dropped rather than painted with somebody else's texture.

**Borrowed from a shipped room**, the older way, still used by the hand-built
maps. A map borrows them from a room the player already has: `textureSource` names
it, and each material says which of that room's materials to take the texture
and palette from. They are copied as a pair, so a material cannot end up with
someone else's palette, and they are written *inline* in the model file, so a
room is one file with no separate `_Tex.bin`.

`-mapmaterials "MP3 PROVING GROUND"` prints the menu.

## The Quake 3 importer

`Q3Bsp.cs` reads the lumps (IBSP 46, from a `.bsp` or straight out of a
`.pk3`); `Q3Import.cs` converts. Three things are translated rather than
copied:

- **Axes.** Quake is Z-up, this engine is Y-up. `(x, y, z) -> (x, z, -y)`
  keeps the handedness, so no surface ends up inside out.
- **Scale.** The number that matters is the one that keeps the level's routes
  intact. Samus leaves the ground at 1228/4096 per frame against 77/4096 of
  gravity: 2.39 units up, about 7.7 across at her walking cap. A Quake player
  leaves at 270 u/s under 800 u/s^2: 45.6 up, about 216 across. So dividing by
  less than 216/7.7 = **28.2** makes the world too big for its own jumps --
  at 22, which this defaulted to when it was chosen by feel, a full-length
  Quake jump is 9.8 units against her 7.7 and the route is simply gone.
  `unitsPerUnit` now defaults to **28**. 35 would match the architecture
  exactly (a 56-unit Quake player against Samus's 1.6); anything above 28 only
  makes jumping easier than the author intended, anything below breaks routes.
- **Jump pads.** Quake solves a pad's launch velocity at runtime from where it
  points, so the arc is re-solved here under this game's gravity
  (`bipedGravity`, -77/4096 per frame squared at 30 fps). Carrying the velocity
  across would land players nowhere near the target.

Collision comes from the brushes, not the drawn surfaces: each brush side is
recovered by starting with a plane-sized sheet and clipping it against every
other plane of the brush. Bezier patches are skipped and counted -- they are
control points rather than triangles, and their collision lives with the patch,
so a skipped one leaves a hole rather than an invisible wall.

### Verified against a real level

OpenArena's `wrackdm17` -- its homage to The Longest Yard, freely licensed --
converts and plays: 804 surfaces to 3075 triangles, 993 brushes to 6122
collision faces, 25 spawns, 13 jump pads, 61 item spawns, eight players
spawning and dying in the void at the kill height. Quake 3's own `pak0.pk3` is
not in this repository and is not fetched by anything here; it is id Software's
commercial data, and the same rule the project applies to the cartridge applies
to it.

`tools/make-test-bsp.py` writes a synthetic IBSP 46 level -- a floor, walls, a
platform, spawns, a push trigger and items -- so the importer can be exercised
with no game data at all. That is how it was tested before any real level was
available.

## Commands

```bash
FruityPrime -q3maps  path/to/pak.pk3      # what levels are in there
FruityPrime -q3shaders path/to/pak.pk3 -map wrackdm17   # what it draws with
FruityPrime -mapgen                       # generate every map in maps/
FruityPrime -mapgen "LONGEST YARD"        # just one
FruityPrime -mapmaterials "MP3 PROVING GROUND"   # what textures can be borrowed
FruityPrime -maptest "LONGEST YARD" -players 8 -seconds 22
FruityPrime -thumbnail "LONGEST YARD"     # the launcher's picture
```

Nothing has to be generated by hand: `ModEntry.TryHandle` builds any map whose
binaries are missing or older than its source, on every startup, whichever
entry point was used. A map that cannot be built prints one line and leaves the
room registered but unloadable -- see below.

`maps/*.json` is copied next to the binary by the build and read at startup;
the generated `.bin` files land in the player's own extracted files under
`_archives/<name>/`. **The JSON is the thing that belongs in git.** The
binaries are derived from the player's cartridge dump (the textures) and, for
an imported map, from their copy of another game -- the asset guard rejects
both, correctly.

## On Android

Three things that are all the same problem: none of the desktop's assumptions
about files hold in an APK.

- **The map files ship as `AndroidAsset`** and are unpacked on first launch by
  `AndroidMaps.cs`. Assets in a package are not files -- only `AssetManager`
  opens them, so `Directory.EnumerateFiles` finds nothing however the path is
  spelled. They land in the external directory the extracted game files
  already use, which is also where a player can drop one in over USB; only the
  names that came out of the package are overwritten on an update.
- **`CustomRooms.MapDirectory` is settable** for that reason: the package's own
  directory is read only.
- **The level a converted map is made from is never in the package.** It is not
  ours to ship, and the generated room binaries are worse -- the borrowed
  textures are inlined in the model file, so a `.bin` carries cartridge data.
  So `import.source` takes a bare name and is looked for beside the map files
  and then beside the game files: the player puts their own `.pk3` where they
  already put their own files, and the device converts it there. Proven: an
  emulator built `wrackdm17` from a pushed `pak1-maps.pk3` into the same
  202,764 and 621,332 bytes the desktop produces.
- **`ChooseRoot` probes before it commits.** `GetExternalFilesDir` can return a
  path that cannot be written to -- it did, on a clean install, on every launch
  after the first -- and the old code trusted a non-null answer. One launch
  wrote internally, the next looked externally, and the game appeared to lose
  the files the player had copied. Now whichever root already holds a
  `paths.txt` wins, else the first that a write probe succeeds in.
- **`AndroidConsole` sends `Console` to logcat.** Mono does that for a debug
  build and not for release, so the one class of bug that only appears in
  release also had no diagnostics. Every wrong turn above was invisible until
  this existed.
- **Nothing calls `ModEntry.TryHandle`** on this head -- the entry point is an
  activity, not `Main` -- so the missing-binary build runs from
  `AndroidMaps.EnsureBuilt`, off the UI thread at startup and again before a
  match and before previews.
- **The map types are trimmer roots.** A release APK is trimmed, the map files
  are read with reflection-based JSON, and the trimmer cannot see that. What
  it produces is not a crash but a map that loads into an object of defaults:
  rooms with no name, no geometry and no spawns, in release only.

Verified on an emulator with a release APK: the three map files are unpacked,
all three are built on the device from the player's own extracted files -- byte
for byte what the desktop generates, `wrackdm17` included once its `.pk3` is
in the maps folder -- and all three are listed in the map picker. A map whose
source level is absent is left out rather than listed and crashing.

## Not done yet

- **No navigation mesh.** `nodePath` is null, so bots have no node data.
  `PlayerAi` handles that without crashing, but they wander.
- **Bezier patches are dropped**, so a converted level is missing its curves.
- **A map is left out when its source level is missing**, which is the case
  that happens (the map file travels with the repository, the Quake level it
  was made from does not). A map that fails to build for any *other* reason
  still appears in the room list and crashes when picked: registration happens
  in `Metadata`'s static initialiser, before the game files are known, so that
  is as much as it can check.
- **Nothing hashes the map** in the network handshake: two clients on the same
  build with different `maps/` will disagree silently.
