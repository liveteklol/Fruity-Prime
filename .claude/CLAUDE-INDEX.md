# CLAUDE Index

CLAUDE.md is the always-loaded top-level file: identity, paths, environment,
commands, and short pointers into the topic files below. The topic files hold
the depth — read the one you need for the area you're touching rather than
loading everything.

- KNOWN-GAPS.md — claims not yet verified, so you don't re-prove or re-claim them
- android/ANDROID-PORT.md — the GL ES renderer, the touch controls, building the APK
- launcher/LAUNCHER-OVERVIEW.md — entries, platforms (incl. macOS/Android), threading
- launcher/LAUNCHER-DESIGN.md — UI components, logo/assets, pitfalls
- launcher/LAUNCHER-SETTINGS.md — settings window layout and toggles
- launcher/LAUNCHER-FIRSTRUN.md — extraction flow and progress bar
- multiplayer/NETWORK-BROWSER.md — server discovery, directory, hosting
- multiplayer/NETWORK-MATCHEND.md — match end, rotation, the double-counted-kill bug
- multiplayer/NETWORK-DIAGNOSTICS.md — the full damage-bug postmortem, traps, diagnostics
- mapgen/MAP-PIPELINE.md — custom maps: the generator, the Quake 3 importer, the format traps
- testing/TEST-HARNESS.md — netcheck/maptest, map sweeps, the world and affliction probes
- testing/TEST-METRICS.md — reading results, common traps, last verified status
- build-deploy/BUILD-WORKFLOW.md — CI workflows, tagging, binaries, asset guard
- build-deploy/DEPLOY-SERVERS.md — deploy script and publish commands

Usage: these are the token-optimised detail store for CLAUDE.md. Keep them
current as the code changes; when a fact changes, fix it here rather than
letting CLAUDE.md's summary and a topic file disagree.
