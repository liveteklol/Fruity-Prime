# Fruity Prime — overview

This document is a compact project overview extracted from CLAUDE.md to make onboarding faster.

Important notes

- The project is Fruity Prime. The code is still `namespace MphRead`, and stays that way. Upstream is NoneGiven/MphRead and every pull from it is a fast-forward only while the 221 files that declare that namespace and the 271 that import it are untouched; renaming it would put a conflict in all of them for a string only a developer ever reads. The rename is the product, the binaries, the window title and the release artifacts. `Mods/Branding.cs` is where the name lives — nothing else should spell it out.

Build binaries

| Build | Binary |
|---|---|
| Windows game | `FruityPrime.exe` |
| Windows server | `FruityPrimeServer.exe` |
| Linux game, Linux and ARM64 server | `FruityPrime` |

Where things are

| Path | What |
|---|---|
| `~/MphRead-dev` | the source. Upstream is NoneGiven/MphRead; everything added lives under `src/MphRead/Mods/` so pulling upstream stays a fast-forward |
| `src/MphRead/Mods/Network/` | the whole multiplayer feature |
| `src/MphRead/Mods/Launcher/` | the Windows front screen, and the settings window behind it |
| `~/mph-net-test/` | the test rig: a copy of the build in `bin/`, extracted game files, `run-check.sh`, `compare-reports.py` |
| `C:\Users\livetek\Desktop\MPH\MphRead-develop\` | the Windows deliverable |
| `france-mining.com:27888` | the dedicated server on the user's Pi (systemd unit `mphread-server`) |

Environment recipe (WSL)

Three things will waste an hour each if you do not know them:

```bash
export PATH="$HOME/.dotnet:$PATH"          # dotnet is not on PATH
export MESA_GL_VERSION_OVERRIDE=4.5COMPAT  # else Mesa hands out a Core profile
export ALSOFT_DRIVERS=null PULSE_SERVER=   # else ALSA retries stall frames
```

- If `~/.dotnet` is empty, the SDK is not installed at all:
  `curl -sSL https://dot.net/v1/dotnet-install.sh | bash -s -- --channel 9.0` puts it there.
- The Avalonia launcher needs `libICE` and `libSM`.
- `export DOTNET_SYSTEM_GLOBALIZATION_INVARIANT=1` can be used temporarily to avoid installing libicu.

Notes and pointers

- Commands have been moved to build-deploy/COMMANDS.md.
- Launcher, multiplayer, testing and build/deploy details are split into category files in this folder.
