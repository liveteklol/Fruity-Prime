# Launcher — design and UI

This document describes the UI components, the logo handling, and the shape of
the windows.

One toolkit

The launcher is Avalonia everywhere. It used to be two: a WinForms front screen
for Windows and an Avalonia one for everything else, over shared logic. That
split cost a second implementation of every screen, and the two halves were not
equal -- the settings window, the map grid and the pause menu existed only in
WinForms, so a Linux player was told to go and use the console menu instead.
Everything is now in `Mods/Launcher/Gui/`:

| File | What |
|---|---|
| `GuiLauncher.cs` | setup, the launcher-then-match loop, and `Pump` |
| `HomeWindow.cs` | the front screen and its cards |
| `SettingsWindow.cs` | the rail of sections and every setting |
| `MapPickerWindow.cs` | every map at once, as pictures |
| `PauseMenuWindow.cs` | what Escape shows during a match |
| `SplashView.cs`, `MenuEntry.cs`, `Rows.cs`, `SliderRow.cs`, `KeyRow.cs`, `ProgressRow.cs`, `UpdateBadge.cs`, `TrackedText.cs`, `GuiTheme.cs` | the painted controls and the palette |

Painting and controls

- The launcher draws its own controls for a consistent dark theme; only the text
  boxes and scroll bars are stock, under Fluent dark.
- `GuiTheme` holds the palette and the display face. Inter is embedded in the
  build rather than looked up on the system: there is no font list every
  platform has, and a launcher that renders in whatever fontconfig happens to
  pick looks different on every distribution.

Windows have frames

The WinForms screen was borderless and dragged by its picture. Every window here
is an ordinary decorated one: an undecorated window that a given window manager
will not let you move is a trap, and there are many window managers.

Logo and assets

One source image, chroma-keyed and cropped into four files under
`src/MphRead/Assets/`. All four allow-listed in `tools/asset-guard-allow.txt`
(PNG and JPEG are otherwise banned extensions).

| File | What | Used by |
|---|---|---|
| `fruity-prime-logo.png` | the wordmark, cherry and text together | the game-files card, the Android screen and the README |
| `fruity-prime-mark.png` | the cherry alone | the window icon |
| `fruity-prime.ico`, `fruity-prime-server.ico` | ICO frames for Windows | `ApplicationIcon` |

Notes on ICOs: 256×256 is the ICO format's ceiling; the source crop carries
detail up to ~460 px so 256 is a downsample.

Threading, and why there is only one thread

The toolkit is set up once per process **on the game's own thread**, and both
the launcher and the pause menu are windows on it:

- Avalonia allows one application per process, so a launcher that stood one up
  per visit worked exactly once and fell back to the text screen on the way back
  from the first match.
- macOS accepts windows only on the main thread, which rules out the private UI
  thread the WinForms launcher used.
- The pause menu needs the toolkit *during* a match, on the thread the render
  loop runs on.

A visit to the launcher is `Dispatcher.UIThread.PushFrame`, ended by the
window's `Closed` event. `GuiLauncher.Pump` is the other half: a nested frame
that runs until a background-priority job it posted comes back, which processes
everything pending and returns. `PauseMenu.Poll` calls it once a frame while the
menu is up -- which is why the match keeps drawing behind it -- and `Ask` calls
it once after the launcher closes, because on X11 the window's destroy request
would otherwise sit unflushed in the connection's buffer for the whole match and
leave a launcher painted over the game.

Pause menu

- `Escape` in a match opens it on every platform now (`Mods/PauseMenu.cs` +
  `Gui/PauseMenuWindow.cs`): Resume, Fullscreen/Windowed, Settings, Leave match,
  Quit.
- It talks to the game through volatile flags. GLFW window calls -- closing it,
  changing its border -- belong to the thread that created the window, so the
  menu asks and `PauseMenu.Poll` does it on the game's own thread.
- **It is the size of the game window and laid straight over it**, so it reads
  as the game's own pause screen rather than as a dialog the game opened. It was
  a 340x392 box centred on the game before, which is the shape of a settings
  prompt and not of pressing Escape in a game. `PauseMenuWindow.CoverGameWindow`
  takes the rectangle from `PauseMenu.WindowX/Y/Width/Height`, which
  `HandleEscape` fills in from the GLFW window; those are client *pixels* and
  Avalonia's `Width`/`Height` are device-independent, so `RenderScaling` has to
  come back out of them or the menu overhangs the game by that factor.
- The entries are a 420-wide panel centred in it -- the same shape the Android
  overlay already used -- because a column of entries stretched across a 3840
  window is a menu you have to hunt across.
- The fill is a scrim (`GuiTheme.ScrimBrush`, `Ink` at alpha 196) with
  `TransparencyLevelHint` asking for `Transparent` and falling back to `None`.
  The match is still running behind it and that is the point; a compositor that
  will not give a window an alpha channel renders it opaque, which loses the
  view and nothing else. The panel carries a 1px `EdgeBrush` border, because
  panel and scrim are otherwise two shades of the same dark.
- **The settings window does the same when it is opened from here**
  (`SettingsWindow`, `view.InGame`): same rectangle, no decorations. A fixed
  980x660 dialog centred on its owner was two different rectangles in two
  different places for one screen -- and on a game window smaller than that, a
  dialog hanging off the edges of the thing it belongs to. From the front screen
  it is still an ordinary 980x660 dialog.
- Both are topmost so they clear a borderless-fullscreen game, and the menu
  steps out of the topmost band while the settings are up so the two are not
  left arguing about which is in front.
- Its title is "<name> - paused", not the product name: the game window carries
  that, and two windows with one title is what an alt-tab list cannot tell apart.

Server browser (`Gui/ServerRow.cs`)

- **`FormattedText` wraps; `Trimming` alone does not stop it.** A
  `MaxTextWidth` with a breakable string breaks at the space rather than
  ellipsizing, so "MP3 PROVING GROUND" became two lines in a 30-pixel row and
  drew over the server beneath it. `MaxTextHeight = size * 1.6` is what forces
  one line and lets the trimming apply. A `PushClip` per cell goes with it,
  because trimming cannot help a single unbreakable word wider than its column
  -- which "PLAYERS" is at 51 pixels in a 43-pixel heading, and it simply
  overflowed into "PING".
- **Columns are pixels from the right, not fractions of the width.**
  `ServerRow.Columns` fixes ping (34), players (52) and mode (66) and gives
  what is left to the two columns that hold prose. Fractions of the launcher's
  400-pixel panel put the map column at 89 pixels, which is not a room name.
- The browse card widens the panel to 600 while it is up
  (`HomeView.PanelWidth`), because that card is a five-column table and the
  others are not. The picture beside it is decoration; the list is the thing
  being read.
- `-uishot` renders a `serverbrowser` screen at both 600 and 400 with sample
  rows, which is how the wrap and the overlap were seen and how the fix was
  checked. Neither needs a directory or a server to be up.

Implementation pitfalls

- `ScrollViewer.Padding` is not taken off the width its content is measured
  with. Every wrapped note in the settings window ran off the right edge of the
  window by exactly that much; the inset is the page's `Margin` instead.
- A `DockPanel` fills with its *last* child, so docking the Save/Cancel footer
  first put it first in the tab order -- the first Tab in the settings window
  was one press away from closing it. It is a two-row `Grid` now.
- Each window focuses its own first control when it opens. Without that a
  keyboard user tabs blindly into whatever the tree happens to offer first.
