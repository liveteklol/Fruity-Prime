# Debugging logs

One switch, in the bottom right corner of the launcher's front card, under the
version. Off. Switched on, the program writes everything it can say about
itself to a file.

It exists for one kind of report, and the report is the design: *"it crashes
when the map loads"*, from a machine nobody here can plug in, sent by somebody
with no console window to copy anything out of. The Windows build is a GUI
binary and deliberately opens no console (`Mods/ConsoleWindow.cs`), so
`GuiLauncher`'s own `catch` -- which prints the exception and returns -- was
printing into nothing: from the player's side the game simply disappears while
a map is loading. The log is the only thing that can be read afterwards.

## Where it lives

| Path | What |
|---|---|
| `Mods/DebugLog.cs` | the whole of it: the file, the console tee, the hooks |
| `Mods/Launcher/Gui/HomeView.cs` | `BuildDebugSwitch`, the corner control |
| `Mods/Launcher/Portable/LauncherPrefs.cs` | `debug_logs` in `launcher.txt` |
| `Mods/ModEntry.cs` | `DebugLog.Attach()`, before anything else runs |
| `logs/FruityPrime-<yyyyMMdd-HHmmss>.log` | the file, beside the executable |

On Android the file goes to the app's data directory, because
`LauncherPrefs.Directory` is pointed there by the head before anything reads --
a package's own directory is read-only. The switch is the same control on the
same screen: the launcher is one Avalonia view on every platform.

## What is in it

Most of the value costs nothing: **`Console.Out` is teed into the file**, so
every line the program already prints is captured with no call site added --
`[net]` joining and slot assignment, `[render]`'s summary of what the options
came out as, the launcher's own failures, the exception `GuiLauncher` prints
where nobody can see it.

On top of that, the things a log needs and a terminal does not:

- the build, the data format, the protocol version, the machine, the .NET
  version, the architecture and the command line;
- `GL_VENDOR`, `GL_RENDERER`, `GL_VERSION` and the shading language version,
  read once in `Renderer.OnLoad` where a context is certainly current;
- **every model actually read off disk**, by name -- a map is a hundred of
  these, and the last line before a crash is the file it died on;
- the room being loaded, and how long each load took;
- the stack of anything that kills the process
  (`AppDomain.UnhandledException`), of an unobserved task, and of the
  match-start failure the launcher catches.

Eight files are kept; the oldest is deleted on the way in. The file is opened
`FileShare.ReadWrite`, so it can be read while the game is still running --
which is the only way to read the tail of one that is about to crash.

## Turning it on without the launcher

`FruityPrime -debuglog` forces it for one run, for the case where the launcher
is what will not start. `DebugLog.Attach()` is called from
`ModEntry.TryHandleHeadless`, which runs for **every** invocation -- the game,
the server, the harness -- so a command line path that never opens a launcher
still writes one.

Switching it on in the launcher starts the file immediately rather than at the
next start: being asked to restart first is where a report like this is
usually lost.

## Traps

- **It is not free.** A lock on every line the program prints, a file handle,
  and a directory that grows. That is why it is a switch rather than something
  on by default, and why the control is the smallest thing on the screen.
- **`Console.SetOut` is process-wide.** Turning the switch off puts the
  original writer back; anything that captured `Console.Out` in between keeps
  the tee. Nothing in this build does.
- **It is not the net log.** `netlog-<name>.txt` (`Mods/Network/NetLog.cs`) is
  written for every client session whether or not this is on, and holds the
  per-slot roster dumps that compare two machines' view of the same frame.
  Both are useful; they answer different questions.
