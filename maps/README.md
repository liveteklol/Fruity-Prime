# Maps

No level lives here, only recipes: the game ships the 27 multiplayer rooms of
the cartridge and nothing else. What lives here is the *hook* for custom ones —
drop a `.json` in, or a folder with a `.json` in it, and the game builds it and
lists it. A map whose source level is not on this machine is left out rather
than listed and broken.

Each `.json` describes one custom room. The binaries a room is actually made
of are generated on the machine that runs the game, from that machine's own
extracted files, because a room's textures are borrowed from a shipped room
and end up inside its model file. That is why no `.bin` is ever committed, and
why the asset guard refuses one.

- `q3dm17.json.example` — rename to `q3dm17.json` and drop your own `pak0.pk3`
  beside it to convert the original Quake III map. Nothing here downloads it
  for you: that is id Software's commercial data.
- `dust2/dust2.json` — de_dust2, by way of the DeFRaG level `df_dust2`, which
  ships beside it. A map keeps its level in a folder of its own like this; it
  is looked for beside the map file first.

A map's level travels with it, so a downloaded release has its custom maps
ready and the first launch builds them. The `.tex` does not: it is baked from
the level when it is missing, exactly like the room binaries.

What still may not be committed is somebody else's commercial data — the
cartridge dump, and id Software's `pak0.pk3` and friends, which the asset
guard refuses by name. Everything else is a judgement for whoever commits it:
publish a level you have the right to publish.

The format, the Quake 3 importer and the traps are in
`../.claude/mapgen/MAP-PIPELINE.md`. `FruityPrime -mapgen` builds every map in
this folder; `FruityPrime -mapmaterials "MP3 PROVING GROUND"` prints the
textures a shipped room can lend.
