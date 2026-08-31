# Build & Deploy — workflow

Build and release workflow summary.

Workflows

| Workflow | When | What |
|---|---|---|
| `.github/workflows/build.yml` | every push and PR | publishes `win-x64`, `linux-x64`, `linux-x64-server`, `linux-arm64`, `osx-x64` and `osx-arm64` on one Ubuntu runner (every target is `net9.0`, so none needs a runner of its own), plus a Windows-runner job that builds and starts the Windows dedicated server |
| `.github/workflows/release.yml` | a `v*` tag, or by hand -- naming a tag or picking a bump that creates one | those six plus the Windows server: seven packages attached to a GitHub release |

Tagging

Two ways in, both landing on the same `resolve the tag` step, which runs
before anything is checked out (`gh api` needs no local repo, and this is
where a typo becomes one clear line instead of `actions/checkout` retrying a
fetch for a ref that never existed).

**Build a tag that exists.** Push it, or type it into the dispatch form:

```
git tag v0.2.0 && git push origin v0.2.0
```

A bare number typed into the form is `v`-prefixed for you; a tag that was
never pushed fails with the `git tag ... && git push ...` line spelled out.

**Bump.** Leave the tag box empty and pick `patch`/`minor`/`major`. The step
lists `refs/tags`, keeps only `vX.Y.Z`, sorts with `sort -V` (the API returns
ref order, where `v0.10.0` sorts *before* `v0.9.0` -- this is why the sort is
not `tail -1` on the raw list), increments, and creates the ref on
`github.sha`, which is the tip of whichever branch the dropdown picked. With
no release tag at all it starts at `v0.1.0` rather than bumping an imagined
`v0.0.0` to `v0.0.1`. Both boxes empty is the one combination that errors.

Three things worth not rediscovering:

- **The bump lives inside `release.yml` on purpose.** A tag pushed by a
  separate workflow using the default `GITHUB_TOKEN` does not trigger another
  workflow -- GitHub's anti-recursion rule -- so a `tag.yml` would need a PAT
  or a GitHub App just to make the release run. One workflow that mints its
  own tag needs no second credential.
- **`INPUT_TAG` and `PUSHED_TAG` are separate variables** for the same reason
  the old `github.event.inputs.tag || github.ref_name` was an
  expressions-engine `||` and not a bash `${GITHUB_REF_NAME:-...}` fallback.
  `GITHUB_REF_NAME` is always set, and for a dispatch run it is whichever
  branch the dropdown defaulted to (almost always `master`). Hit once already:
  a run dispatched with nothing typed resolved to `vmaster`. A single
  `||`-chained variable breaks again the moment the tag box is allowed to be
  empty, which is exactly what the bump does -- hence
  `PUSHED_TAG: ${{ github.event_name == 'push' && github.ref_name || '' }}`,
  empty for every event that is not a tag push.
- **Nothing auto-tags on a push to master**, deliberately: every push would
  be a release. Nothing derives a version from commit messages either -- the
  history here is not conventional-commits shaped, and the human gate already
  exists downstream, since the release comes out as a draft either way.

Release notes

A standing block (beta, bring your own cartridge dump, which package is which,
a link to the README) with GitHub's own changelog appended under a `---`:
`gh api -X POST .../releases/generate-notes -f tag_name=$TAG --jq .body`,
which picks the previous tag itself. Appended rather than substituted, and
never fatal -- a rate limit or a tag with no predecessor leaves the standing
block and a `::warning::` in the log. Before this the notes were the same
words on every release and said nothing about the build being downloaded.

Rerunning the same tag updates the draft instead of failing: the
create-vs-upload arms are chosen by `gh release view`, so the notes are
regenerated and the assets `--clobber`ed.

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
