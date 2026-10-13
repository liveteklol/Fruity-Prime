# Aim implementation and acceptance evidence

Work branch: `develop7_1to1Aim`. Starting Fruity commit:
`62419cd23e9e0657f3dbcb951fffaec893f64aa7`. The implementation is currently uncommitted.

Implementation and acceptance specifications remain the two canonical documents in
`mphCodex/mphAnalysis/Control/AimAnalysis/FruityPrime/`. This file records the
implementation, review and evidence; it does not replace either specification.
Research revision inspected: `938a9e07332844240068665213d42bae36d2e266`, including
the additional zero-input analysis in `5062410`, `70eb992`, and `69569d5`.

## User-approved PC precision policy

On 2026-10-08 the user explicitly requested SRP and PC precision, and rejected
AngleLut and reproduction of DS precision limitations. ROM control conditions,
source ownership, contact semantics and operation order remain the reference.
Numerical bit equality with the DS is therefore not a completion requirement for
these intentional PC differences:

| Operation | PC implementation | DS limitation avoided |
|---|---|---|
| Rotation and normalization | float sin/cos and vector math | LUT angular steps, Q12 vectors, DIV/SQRT truncation |
| Dual maximum / step / initial speed | 8 degrees / 1% / 40%, calculated in float | step327, floor13107/13104 quantization |
| Dual release | 40% per native period; square root of 40% per PC half-period | repeated `0x666` rounding |
| Facing envelope / follow | exact 15-degree sine/cosine, follow0.1 | rounded3956/1060/409 coefficients |
| Zoom | affine native-control policy in float; PC mouse retains FOV ratio | intermediate Q12 rounding |
| NonExact target maintenance | float60% approach, separate from rotation | `2457/4096` rounding |
| Touch HUD motion | fractional movement with an8-pixel limit | signed16 wrap and ASR14 stair steps |

Stored sensitivity values retain their calibration. Their multiplication does
not re-quantize the resulting angle. InputSlot counters, pressed/released bits,
ActionSpec priority and native Touch sampling remain discrete input semantics.
No late latch or render-only gameplay state writer has been added.

## Responsibility review

| File | Responsibility and state writer |
|---|---|
| `PlayerInput.cpp` | Host snapshots, suspension, gameplay input integration and turn animation |
| `PlayerAimInput.cpp` | Per-player owner/source selection and local source dispatch |
| `PlayerAimMouse.cpp` | Direct relative mouse application at simulation cadence |
| `PlayerAimGamepad.cpp` | Gamepad sensitivity and existing controller assist |
| `PlayerAimTouch.cpp` | Consume the existing Touch sample/contact/SUM4; Pitch then Yaw |
| `PlayerAimDual.cpp` | Consume old velocity Yaw then Pitch; produce the next velocity and conditional extra Pitch |
| `PlayerAimFreeCamera.cpp` | Camera-specific same-step producer then consumer |
| `PlayerAimRotation.cpp` | Rotation, Follow, Zoom Snap, basis rebuilding and target projection |
| `NativeDualAim.hpp` | Pure float velocity rules and control gates; no clocks or entity writes |
| `NativeInputSlot.hpp` | Pure input history and ActionSpec selection; no aim state writes |
| `NativeAimControl.hpp` | Configuration only |
| `AimNumericPolicy.hpp` | PC numerical constants and zoom calculation |
| `PlayerEntityNetAim.cpp` | Validated remote aim application and post-camera reprojection |
| `AimTrace.hpp` / `AimCheck.cpp` | Disabled fixed-size counters / isolated diagnostic serialization |

The UI source tracker is updated by host capture and gamepad capture. Gameplay
owner selection and controller-assist eligibility use the player's frame, not
that global tracker. Bot and remote slots do not receive local mouse movement.
Dual phase uses the scene gameplay clock; Touch uses its existing resettable
sample clock. This port policy does not prove the original global InputSlot
producer's actual frequency.

Normal aim application introduces no heap allocation, log formatting, file I/O,
container growth or render-dependent packet/shot generation. This is a source
review finding, not an allocation or performance measurement.

## Evidence and remaining acceptance

Local environment: Windows x64, MSVC Release, asset-backed `TEST ARENA`.
Logs are under `tools/build/out/aim-validation/`; generated binaries and logs
are intentionally untracked. Pure fixtures also passed with MinGW GCC C++20.

Before modification, the saved diagnostic baseline reached Follow counts
0/2/2/4 for Mouse/Keyboard masks00/01/10/11. The corrected source selection
reaches0/2/2/2. Zero Pitch and Yaw each retain their intentional Follow, Zoom
Yaw retains Snap, and released Touch does not behave as held zero-delta Touch.

The latest final rebuild and regression results must be recorded below before
delivery. Initial local coverage: 7,961 pure checks and43 asset-backed production
checks. Synthetic render schedules at60/120/240/540/1000 preserve60 simulation
steps and a30-degree total yaw. These schedules are not GPU presentation tests.

| Acceptance | Local evidence | Remaining requirement |
|---|---|---|
| A01–A02 | Source-mask, zero-input, Zoom asymmetry checks | FixedCrosshair/Alt/full input replay matrix |
| A03 | Float acceleration/floor/reversal/release and old→new fixtures | Asset-backed Alt/held-key duration matrix |
| A04 | Control/ActionSpec/84E/AutoPitch gate fixtures | All reachable runtime gates and owner conditions |
| A05 | All counter0..255 windows, other11 invalidation, scan order, release/repress | Original global→shadow lifecycle runtime |
| A06 | Existing Touch unit tests; held-zero/contact-release checks | Edge/swipe and integrated Morph Ball regression |
| A07 | Separate NonExact rotation/basis/target maintenance checks | Movement/Alt/camera replay matrix |
| A08 | Float HUD policy and Zoom source review | Actual HUD/camera captures |
| A09 |4slot aim/projection isolation and nonfinite rejection | Packet trust/ACK/age/generation/respawn scenarios |
| A10–A11 | NOT_RUN | Gun/Muzzle/Shot/Effect/Projectile and actual hits |
| A12 | Local Windows build and fixtures | Linux/macOS/Android CI; ROM control trace; real4slot session |
| S01–S04, S06–S08 | Responsibility review and focused fixtures | Full reachable branch review |
| S05 | Native Dual scene tick differs from reset Touch sibling phase | Scene-wide phase/drop/stall measurement |
| S09–S10 | NOT_RUN | Shot separation and before/after performance/latency/allocation data |
| S11–S12 | Explicit branch/base, baseline and evidence boundaries | Reviewable commits, CI URLs and rollback record |
| F01–F07 | Synthetic schedules, cumulative mouse, catch-up5, suspension | Actual GPU/input/network matrix |
| F08 | OFF | No optional interpolation/late-latch path selected |
| F09–F10 | NOT_RUN | Available VSync/Reflex modes,1%low, latency and PC/Touch experience |

The overall gate remains **NOT_RUN / incomplete** until its remaining execution
requirements are satisfied. Partial local checks must not be promoted to ROM,
online, presentation, performance or full-platform acceptance.

## Aim style (Modern / Classic), 2026-10-08

- `aim_style=modern|classic` in `controls.txt`, Settings → Keyboard → **Classic DS aim**.
  Modern is the default. `-nativeaim` still overrides it for one run.
- **Modern**: mouse, keys (Dual curve at 60 Hz half-steps), pad, and Android/pen
  pointer deltas all apply every 60 Hz simulation step. The touch producer is
  never the aim source, so Android and stylus-zone players keep the pre-branch
  pointer path.
- **Classic**: the DS's aim *rules* (Native zoom sensitivity, Exact/NonExact,
  Dual curve, AutoPitch) on the 60 Hz simulation. Every pointer delta (mouse,
  touch, pen) and the keys apply on the step they arrive in: the Native Touch
  30 Hz sample is touch compatibility, never the aim rate (guide §1, §3).
  Dual and AutoPitch use half-step rates so the curve takes the same real time.
- **Strict ROM cadence** (`NativeAim::Control::RomCadence`): `-nativeaim` only.
  Touch reads the 30 Hz native sample and Dual waits for the native tick.
  A diagnostic, not a player option.
- **FreeCamera** is not gameplay: keys run the Dual curve every 60 Hz step
  in either style, and a pointer always steers it directly.
- Checks: `-aimcheck` 1896 PASS (adds FreeCamera-per-step, modern host-touch and
  Classic-every-step cases), CTest 30/30, `tools/check-bomb-network.ps1` 12/12 PASS.

## Audit response (mphCodex `Aim-Implementation-Audit-develop7_1to1Aim-EU1_1.md`, audited HEAD 192f3139)

| ID | Resolution |
|---|---|
| A1 P0 | `GameView.cpp` no longer reads the removed `FrameTiming::MaxCap`. Display rate and Unlimited do not sleep (eglSwapBuffers paces Display); a numeric cap sleeps 1/cap with no ceiling and no 30 fps floor. Local NDK arm64-v8a build: PASS. x86_64, APK and launch smoke: CI only. |
| A2 P1 | Not a regression: `PlayerInput.cpp` still applies `_buttonAimX/Y` for bots outside the Local-only block. Now proven by `-aimcheck` "bot Biped aim turns the bot". |
| A3/A4 P1/P2 | The ActionSpec selectors (AimAction 0x10, EnableAction 0x20, touch fallback bits 2/4/5) could never be true on PC. Dual now reads a typed `NativeAim::AimButtons` (Left/Right/Up/Down/Enable/Aim/TouchDown). Enable and Aim stay false, because no PC button stands behind them, so the default Control (0x28, unconditional) is unaffected. Every gate of the conditional configuration is covered exhaustively in `FruityPrime.NativeAimRules`. |
| A5 P2 | Pad only (mouse and key aim off) now selects the Gamepad source, and a centred stick runs the zero-input Follow on each axis. `-aimcheck`: "pad-only centred stick still follows on each axis". |
| A6/A7 P2 | `NativeInputSlot.hpp` (an incomplete 0x48-byte mirror with no +0x06 held, one counter bank, no +0x0C consumer and an unverified clock) is removed, together with the Player slot and shadow. A strict ROM InputSlot, if ever needed, should be a separate complete `RomInputSlot`. |
| A8 Gate | Unchanged: real-ROM trace, WiFi 4-slot, measured high-FPS acceptance and full Android CI remain NOT_RUN. |

Checks after the response: `-aimcheck` 1901 PASS, NativeAimRules 149 PASS, CTest 30/30.
