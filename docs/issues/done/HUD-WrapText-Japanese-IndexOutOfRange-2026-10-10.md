# HUD message wrap crashes with Language = Japanese on a non-Japanese ROM

Date: 2026-10-10. Status: **fixed in C++** (`3350280a`), **still present in C#**.

## Symptom

With the cartridge being EU 1.1 (AMHP1) and Settings → Language set to
Japanese, the game dies with `IndexOutOfRangeException: Index was outside the
bounds of the array.` the moment a HUD message is queued:

- switching to a weapon that is not owned / has no ammo (the "no ammo"
  message -- a hunter that still had ammo for it got no message and did not
  crash, which is why it was first reported as Weavel-only),
- emptying the Volt Driver (same message, from the auto-switch),
- shortly after a match starts.

## Cause

`PlayerEntity::WrapText` measures each character to decide where to break a
line, and indexed the font's width table raw:

```
index -= font.MinCharacter;
int width = font.Widths[index];
```

The Japanese string tables hold characters the Normal font has no width for
(the Kanji font is only selected on a Japanese/Korean cartridge), so the index
leaves the table. `DrawText2D` and `ModTextWidth` already clamp through
`GlyphIndex`; `WrapText` was the one path that did not.

## Origin

Not a C# → C++ porting bug. The raw lookup is upstream's (NoneGiven,
`bf628f6f`, 2024-04-29). `GlyphIndex` was added in this fork (`bb3d97d0`,
2026-09-06) to the draw paths only, and the C++ port carried both over as
they were.

## Fix

C++ (`src/MphRead.Native/Entities/Players/PlayerHud.cpp`, `WrapText`): the
width is read through `GlyphIndex(font, index)`, and a trailing lead byte no
longer reads past the end of the text.

C# (`src/MphRead/Entities/Players/PlayerHud.cs`, `WrapText`): **not changed**.
The same fix applies there if the C# build needs it; not run to confirm it
crashes, but the code is identical.
