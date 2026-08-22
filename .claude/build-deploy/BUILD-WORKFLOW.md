# Build & Deploy — workflow

Build and release workflow summary.

Workflows

| Workflow | When | What |
|---|---|---|
| `.github/workflows/build.yml` | every push and PR | publishes `win-x64`, `linux-x64`, `linux-x64-server` and `linux-arm64` on one Ubuntu runner |
| `.github/workflows/release.yml` | a `v*` tag, or by hand | those four plus the Windows server: five packages, two zips and three tarballs, attached to a GitHub release |

Tagging

A release needs a real pushed tag before it triggers the release workflow:

```
git tag v0.36.0 && git push origin v0.36.0
```

Notes

- The repository was renamed from `liveteklol/MphRead` to `liveteklol/Fruity-Prime`. `Mods/Branding.cs.Repository` contains the current name.
- `-p:MphReadServer=true` produces the server package; Windows server is `FruityPrimeServer.exe` (console subsystem).
- `tools/check-no-game-assets.sh` runs in CI to prevent Nintendo assets being published.
