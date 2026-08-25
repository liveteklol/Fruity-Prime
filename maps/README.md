# Maps

Each `.json` here describes one custom room. The binaries a room is actually
made of are generated on the machine that runs the game, from that machine's
own extracted files, because a room's textures are borrowed from a shipped
room and end up inside its model file. That is why no `.bin` is ever
committed, and why the asset guard refuses one.

- `longestyard.json` — an arena in the spirit of Quake's Longest Yard, written
  here from scratch as boxes and jump pads. Ours, MIT with the rest.
- `testbox.json` — the smallest room that exercises the generator.
- `wrackdm17.json` — a conversion of the level below.
- `q3dm17.json.example` — rename and drop your own `pak0.pk3` beside it to
  convert the original Quake III map. Nothing here downloads it for you: that
  is id Software's commercial data.

## wrackdm17.bsp and wrackdm17.tex — third-party, GNU GPL v2

`wrackdm17.bsp` and `wrackdm17.tex` are **not ours**. They are OpenArena's
level `wrackdm17` ("Never Ending Yard"), its homage to Quake III's *The
Longest Yard*, and the textures that level is built from, taken from:

    OpenArena 0.8.8, baseoa/pak1-maps.pk3, maps/wrackdm17.bsp
    OpenArena 0.8.8, baseoa/pak0.pk3 and baseoa/pak4-textures.pk3 (the images)
    http://openarena.ws  ·  https://sourceforge.net/projects/oarena/

**Licence: GNU General Public License, version 2**, the licence the OpenArena
0.8.8 package is distributed under; its `COPYING` is beside this file as
`LICENSE-openarena.txt`. OpenArena's own README records that its ports of the
Quake maps rest on levels John Romero released under the GPL v2.

**It has been modified.** Only the lumps this project's importer reads were
kept; the light grid, the lightmaps, the BSP tree, the leaf arrays and the
visibility data are emptied, which takes the file from 5,586,720 to 647,608
bytes without changing a single triangle of what gets converted. The change is
mechanical and reproducible:

    tools/strip-bsp.py path/to/pak1-maps.pk3 wrackdm17 maps/wrackdm17.bsp

`wrackdm17.tex` is likewise derived: the 18 textures the level's drawn
surfaces reference, decoded from their JPEGs, scaled to 64x64 and quantised to
a 256-colour palette, which is the form the DS hardware takes. Also
mechanical, also reproducible:

    tools/bake-textures.py maps/wrackdm17.bsp - pak0.pk3,pak4-textures.pk3 \
        maps/wrackdm17.tex 64

Baking them is what lets a converted map wear its own art instead of textures
borrowed from a room of Metroid Prime Hunters -- which means the room files it
generates contain nothing from the cartridge either.

The corresponding source, in the sense the GPL means it, is the level as
OpenArena distributes it at the addresses above; OpenArena publishes map
sources at `http://openarena.ws/svn/source/`.

The rest of this repository is MIT (see `../LICENSE`). A data file under the
GPL sitting beside it is aggregation, not linking: it does not put the code
under the GPL, and the GPL still governs this one file.
