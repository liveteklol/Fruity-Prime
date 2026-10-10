# The classic DS UI -- how the game draws its menus, and how to check against it

The launcher's debug switch puts the DS game's own front end in place of
Fruity Prime's (`src/MphRead.Native/Mods/ClassicMenu/`, ported from Second
Hunt). Everything below was measured against the real game running in the
MPH recomp, frame by frame; the numbers are the game's, not guesses.

## The comparison rig

| Piece | What |
|---|---|
| `FruityPrime -classicframes DIR "SCRIPT"` | the menus one tick at a time with no window, both screens as the DS shows them (`DIR/tNNNNN.png`, 256 x 384). Steps: `tick:N`, `cap:N`, `touch:X:Y` (touch-screen pixels), `a`/`b`/`start`, `up`/`down`/`left`/`right`, `page:P`, `shot:NAME`, `describe` (every item's state, code and what it drew), `bench:W:H[:N]` (the one screen at a window size, timed). Runs with no host: the launcher's live values (servers, arenas, the match being set up) show as the ROM's placeholders |
| `tools/classic-ui/recomp.py` | drives the MPH recomp's `nds_runner --serve` (v0.7.6+, US rev 0 ROM, free BIOS, generated firmware): `frames` writes the same 256 x 384 pictures, `polys` every polygon ID's alpha each VBlank, `gx` the 3D command stream of a frame (lights, colours, POLY_ATTR...) |
| `tools/classic-ui/compare.py` | port ticks against recomp frames, offset fitted or given, with a difference map |
| `tools/classic-ui/menudump.py` | the menu graph: every page's items, looks, links and touch points |

A menu tick is two DS frames, and each DS screen is redrawn every other frame
(the 3D engine is captured for one screen while drawing the other): take the
top screen from frame F and the touch screen from F + 1. Savestates do not
work (the game's 3D state is refused), so each run boots: ~2480 VBlanks to the
title, a minute.

## What the game does

- **Lighting.** Widget materials marked lit are lit by two white lights
  pointing into the screen (LIGHT_VECTOR 0,0,-1; DIF_AMB diffuse 25,
  ambient/specular/emission 0). At a NORMAL command the DS works the colour
  out (GPU3D::CalculateLighting): 25 x 31 x 512 >> 14 = 24 a light, two lights
  = 48, clamped to 31. A widget facing the screen is white, not 25/31. With
  no NORMAL the colour stays the diffuse.
- **Two layers.** The Samus pictures are a 2D bitmap (BG3). The tint, the
  grooves and every item are 3D (BG0), blended among themselves -- a
  translucent pixel over nothing is written as is, over something mixed by
  (a + 1) / 32 keeping the larger alpha, never twice over one translucent
  polygon ID -- and then laid over the bitmap by their own alpha
  (BLDCNT 0x2C41, ColorBlend5).
- **Tint.** One 3D quad at polygon alpha 20, flat over the top screen and
  fading to half (>> 1) down the touch screen. Keys green 0,31,0 -> teal
  4,16,16 -> orange 19,9,6, 170 ticks each, from the title's first tick.
- **Grooves.** `frontend2d/slots` (polygon alpha 21) and `lines` (fading in
  to 24 over 10 ticks on a page, out over 10 leaving it); lit like widgets.
- **Vertices** are truncated to the pixel they fall in, and a polygon
  thinner than a pixel is still drawn a pixel wide a row (a flat one, one
  row): the BEGIN GAME bar's squares collapse into black lines.
- **Text** is drawn at polygon alpha 30 at most. A text style's format bytes
  are size, letter spacing (added to every advance: "BEGIN GAME" 3,
  "PLAYER" 4), line spacing (signed, added between lines: descriptions +1,
  two-line labels -1) and alignment.
- **Leaving a page.** Items with a way to hide play it. An item with no hidden
  look and no hide transition stays as it is until the next page comes in if
  it is a widget or an animating text ("SELECT A GAME MODE" keeps pulsing);
  a still text goes at once (the icons' labels). A text already hiding is cut.
- **Entering a page from another.** Items the new page has unchanged at the
  same index (the logo, description box, black band and back arrow on 18, 21,
  24, 25...) carry on as they were -- no entry animation.
- **Create or join (page 26)** shows empty match panels and an empty game
  list until a game is picked.

## What is not copied, on purpose

- **Loading pauses.** Most page changes stall the game 0.5-2 s on a still
  frame (one dropped frame, the screens swap parity) while it reads the
  cartridge. The tint goes on running through them, so after a few pages the
  two colour cycles are out of step: compare page by page.
- **Touch latency.** The game answers a tap two ticks later than the port.
- The Wi-Fi/wireless icons, the multi-master tabs (MODE / ARENA / HUNTER) on
  the offline flow, and the match panels' values, which are the launcher's.
