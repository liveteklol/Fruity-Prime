# Build & Deploy — workflow

Build and release workflow summary.

Workflows

| Workflow | When | What |
|---|---|---|
| `.github/workflows/build.yml` | every push and PR | publishes `win-x64`, `linux-x64`, `linux-x64-server`, `linux-arm64`, `osx-x64` and `osx-arm64` on one Ubuntu runner (every target is `net9.0`, so none needs a runner of its own), plus a Windows-runner job that builds and starts the Windows dedicated server |
| `.github/workflows/release.yml` | a `v*` tag, or by hand | those six plus the Windows server: seven packages attached to a GitHub release |

Tagging

A release needs a real, **pushed** tag before it needs anything else:

```
git tag v0.36.0 && git push origin v0.36.0
```

Running it by hand from the Actions tab also needs the tag to exist first: the
workflow's first step resolves and verifies it with
`gh api .../git/ref/tags/$TAG` (prefixing a bare number with `v`) and fails
with one clear line if nothing matches. The tag comes from
`github.event.inputs.tag || github.ref_name` -- an **expressions-engine** `||`
evaluated before bash runs, not a bash `${GITHUB_REF_NAME:-...}` fallback.
`GITHUB_REF_NAME` is always set, and for a dispatch run it is whichever branch
the dropdown defaulted to (almost always `master`), not the typed tag -- a
bash fallback would silently rebuild that branch instead of failing loudly.
Hit once already: a run dispatched with nothing typed resolved to `vmaster`.

Two Windows executables, one PE header field

`FruityPrime.exe` is `WinExe` (no console; double-clicking opens the
launcher). That same property makes it useless as a server: cmd/PowerShell do
not wait for it and its exit code never reaches `%ERRORLEVEL%`.
`dotnet publish -r win-x64 -p:MphReadServer=true` publishes the same sources
without the launcher and with a console header, as `FruityPrimeServer.exe`.
`tools/check-subsystem.sh gui|console <exe>` asserts each one in both
workflows, since it comes out of a csproj condition nothing else would notice
changing.

`-p:MphReadServer=true` is published three times: `win-x64-server`,
`linux-x64-server` and `linux-arm64` (the Pi is server-only; plain x64 server
covers a VPS/spare desktop). Only the Windows server package is renamed --
Linux keeps the plain `FruityPrime` name, and the Pi's own
`deploy-server.sh` migrates a systemd unit still pointing at the old name,
`MphRead`.

Both x64 Linux builds (game's server capability, and the standalone server
package) are started on the Ubuntu release runner, since it can actually run
x64; the Windows server gets the same proof on a Windows runner. The ARM64
package is cross-compiled and never started by CI -- only the Pi, through
`deploy-server.sh`, has ever run it.
`tools/check-dedicated-server.sh` runs on the Windows runner too, in Git
Bash rather than a parallel PowerShell script: the binary is
`MphReadServer.exe`, `python3` may only be `python`, and a path has to go
through `cygpath` before .NET reads it as anything but a path on the current
drive.

Asset guard

`tools/check-no-game-assets.sh` runs twice in each workflow -- once over what
git tracks, once over what the build produced -- refusing game-file
extensions, extraction/cache directories, and (for tracked files) anything
over 2 MB. Map previews are the easy mistake: rendered locally, they look like
ordinary screenshots. Run it locally before pushing:

```bash
tools/check-no-game-assets.sh                    # the repository
tools/check-no-game-assets.sh publish/win-x64    # a build
```

Notes

- The repository was renamed from `liveteklol/MphRead` to `liveteklol/Fruity-Prime`. `Mods/Branding.cs.Repository` contains the current name; GitHub's old-slug redirect covers `gh`/API calls but should not be relied on.
- `MPHREAD_SERVER` (defined on server builds) is a different question from "has no launcher": it is what makes a bare invocation print what the binary is for, instead of falling through to upstream's setup check.
