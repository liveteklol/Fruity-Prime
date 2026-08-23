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
- It centres on the game window, not the desktop, and is topmost so it clears a
  borderless-fullscreen game. The settings window it opens is topmost too, and
  the menu steps out of the topmost band while that dialog is up so the two are
  not left arguing about which is in front.
- Its title is "<name> - paused", not the product name: the game window carries
  that, and two windows with one title is what an alt-tab list cannot tell apart.

Implementation pitfalls

- `ScrollViewer.Padding` is not taken off the width its content is measured
  with. Every wrapped note in the settings window ran off the right edge of the
  window by exactly that much; the inset is the page's `Margin` instead.
- A `DockPanel` fills with its *last* child, so docking the Save/Cancel footer
  first put it first in the tab order -- the first Tab in the settings window
  was one press away from closing it. It is a two-row `Grid` now.
- Each window focuses its own first control when it opens. Without that a
  keyboard user tabs blindly into whatever the tree happens to offer first.
