# Commands — CLI reference

This file reproduces the Commands table from CLAUDE.md with common invocations.

| Command | Use |
|---|---|
| `MphRead -server -port N -players 8` | dedicated relay server; needs no game files. `-servername "NAME"` is what a browser shows; it announces itself to `net.livetek.fr` unless `-nomaster` is passed |
| `MphReadServer.exe -server ...` | the same server on Windows, as its own console binary. `MphRead.exe` can also do it, but it is a GUI binary: a shell will not wait for it and its exit code never reaches `%ERRORLEVEL%`. |
| `MphRead -masterserver [-port N] [-public HOST] [-hostports A-B]` | the server directory the launcher's browser asks |
| `MphRead -hostgame "ROOM" [-mode M] [-master HOST]` | ask the directory to run a match and join it |
| `MphRead -servers [-master HOST] [-masterport N]` | print the server list the launcher's browser would show |
| `MphRead -connect HOST -port N -name X -hunter H` | join from the command line, no launcher |
| `MphRead -netcheck HOST -port N -name X -hunter H -seconds N [-shots DIR] [-size WxH]` | a real client driven by a script, which reports what it saw. Exit code 0 = pass |
| `MphRead -maptest "ROOM" -players 8 -seconds 22` | load one room with a full house, drive every player, and report what the map holds |
| `MphRead -rooms` | list every multiplayer room, one per line |
| `MphRead -mechanics` | print the catalogue below, generated from the game's own tables |
| `MphRead` (no arguments, Windows) | the front screen |
| `MphRead -menu` | the console menu |
| `MphRead -launcher [-console]` | the front screen explicitly; `-console` also gives it a terminal. Off Windows this opens the Avalonia front screen, or the text one when there is no display. |
| `FruityPrime -launcher -text` | the text front screen on a machine that has a display |
| `FruityPrime -update` | check GitHub for a newer release and open its page. Installs nothing |
| `FruityPrime -noupdate` | do none of that |
| `FruityPrime -credits` | who this is built on, from `Mods/Credits.cs` |
| `MphRead -fullscreen` / `-windowed` / `-nohelmet` | display choices for the paths that never open a launcher |
