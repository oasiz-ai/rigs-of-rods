# Playable frame-time budget gate

The CityWorld visual milestone and the Ogre-Next combined-runtime milestone
both declare a sustained frame-rate budget, and neither had a measurement seam:
the runtime counted completed frames but never retained a frame-interval
distribution, so no milestone could be closed against its own number. This
document defines that seam, its acceptance rules, and what a recorded result
does and does not prove.

The gate measures one already built executable. It does not change renderer
selection, does not claim parity with another simulator, and does not assert
anything about a platform it has not been executed on.

## Contract

| Fact | Value |
| --- | --- |
| Receipt format | `ror-frame-time-budget-v1` |
| Run report format | `ror-playable-performance-run-v1` |
| Kernel | `source/main/system/FrameTimeBudget.{h,cpp}` |
| Driver | `tools/run_playable_performance_scene.py` |
| Strict tests | `tests/system/FrameTimeBudgetTests.cpp` |
| Contract tests | `tests/tools/test_frame_time_budget_contract.py` |
| Driver tests | `tests/tools/test_run_playable_performance_scene.py` |
| Failure exit code | 75 (73 and 74 remain the renderer child's) |

The recorder is armed per launch through explicit command-line options and
never through `RoR.cfg`. Its CVars are deliberately non-archived, so a
measurement contract cannot be silently restored into a later, unrelated
session:

```
-frame-budget <off|measure|gate>
-frame-budget-receipt <absolute file that must not exist>
-frame-budget-scenario <canonical id>
-frame-budget-sustained-ms <positive milliseconds>
-frame-budget-percentile <1..100>
-frame-budget-percentile-ms <positive milliseconds>
-frame-budget-warmup <frames excluded from the budget>
-frame-budget-minimum <frames required for a verdict>
-frame-budget-frames <frames after which the run exits>
```

`measure` records and reports. `gate` additionally fails closed and sets the
process exit code. `off` leaves the render loop untouched.

## What is measured

The recorder samples the render loop's own committed delta time — the exact
interval the simulation was advanced by — at the single point where the loop
computes it. It owns no clock, allocates nothing after construction, and adds
one integer compare and one histogram increment per frame.

Frames are retained in 8,192 bins of exactly 1/64 ms covering `[0, 128) ms`,
plus one saturating bin that keeps every longer stall inside the ranking.
Percentiles are nearest-rank and report the bin's **upper** edge, so a binned
answer can never understate the measured interval. The minimum, maximum, sum,
and over-budget count are exact integers in nanoseconds.

Warm-up frames are observed and reported but excluded from the distribution:
shader compilation, streaming, and first-light residency belong to load, not to
the playable budget.

## Fail-closed rules

A gated run fails, rather than reporting a number, when:

- the declared limits are invalid, including a percentile ceiling below the
  sustained mean budget, which no real distribution can satisfy;
- a frame-rate limiter was active, because the distribution then describes the
  limiter;
- any frame interval was non-finite, non-positive, or beyond ten seconds — such
  an observation is counted and permanently invalidates the run instead of
  being clamped into the distribution;
- the scene changed while recording, so a map reset or actor change cannot be
  averaged into one distribution;
- the measured loop does not present its own frames, so its interval describes
  how fast it produced scenes for another process;
- fewer frames were recorded than the declared minimum;
- the mean interval missed the sustained budget, or the ranked percentile
  exceeded its ceiling;
- the receipt could not be created exclusively. An existing receipt is refused,
  never overwritten.

Optimized game builds enable `-ffast-math` globally, which permits a compiler
to assume no NaN or infinity exists and to fold `std::isfinite` to a constant
true. The non-finite rejection above is therefore defended twice: the
translation unit is compiled `-fno-fast-math -ffp-contract=off`, and the kernel
classifies intervals by inspecting the IEEE-754 bit pattern rather than by
calling a floating-point predicate. Its strict test binary links with the
matching opt-out.

The driver additionally refuses a receipt that does not describe the exact
requested run: scenario, terrain, resolution, percentile, budgets, and frame
counts must all match, and the run must have been windowed with VSync off. A
renderer fault in the log (`RenderingAPIException`, `GL_INVALID_`, a sampler
validation failure, or a crash signature) fails the run. Content diagnostics —
missing materials, unlocatable resources, unsupported techniques — are always
counted and reported, and are gated only under `--require-clean-content`,
because the pinned baseline content emits some of them and making them fatal by
default would measure the fixture instead of the renderer.

## Which renderer is measured, and which process

The gate measures whichever executable it is given, and the receipt records
which one that was. `RoR-Ogre14` reports `renderer: "ogre14"` and a build
configured with `ROR_OGRE_NEXT_COMBINED_RUNTIME` reports
`renderer: "ogre-next-combined"`. A number recorded from one is not evidence
about the other, and the run report must be read with that field in view.

Which *process* was measured matters just as much. In the two-process bridge
the game loop does not present: it produces scenes for a separate presentation
child, and its inter-frame interval is a producer cadence, not a frame rate.
Measured directly it reports absurd numbers — a real bridged CityWorld run on
this machine recorded a 0.065 ms mean, or 15,418 FPS — which would otherwise
sail through both budgets as a pass.

The receipt therefore carries `presents_frames`, taken from the runtime's own
`legacy_frame_presentation_enabled` ownership fact. A gated run whose loop does
not present fails closed as `fail-not-presenting`, and the driver refuses the
receipt as well. Measuring the Ogre-Next frame rate consequently requires the
combined runtime, where one process simulates and presents, rather than the
two-process bridge.

## Graphics presets

A budget measured against whatever the configuration happened to default to is
not reproducible. Every run names an explicit preset, writes every one of its
settings, and re-reads the effective values from the runtime. `high` is the
preset the macOS arm64 CityWorld budget is declared against: PSSM shadows at
ultra quality, full vegetation, reflection plus refraction water, anisotropic
filtering at 8x, and dynamic reflections every frame.

Writing a setting is not the same as it taking effect, and the difference is
silent. `AppConfig`'s `ParseHelper` maps enum-valued graphics settings from
their exact display strings and substitutes the first enumerator for anything
it does not recognize, so `gfx_shadow_type=1` disables shadows instead of
selecting PSSM. A run configured that way renders with no dynamic shadows at
all while still reporting a "high" preset — and measures far faster than the
preset it claims.

Two mechanisms close that hole. The preset table carries both the string the
config parser accepts and the effective value expected back, and the runtime
states its own effective graphics settings in one authoritative line when the
budget is armed:

```
[RoR|Perf] Graphics: gfx_shadow_type=1 gfx_shadow_quality=3 ...
```

The driver parses that statement — not the per-CVar assignment lines, since a
setting already at its target value is never logged as a change — and refuses
the run when any effective value differs from the preset. A missing, repeated,
or partial statement is refused as well.

## First Ogre-Next measurement

`evidence/OGRE_NEXT_COMBINED_PERFORMANCE_M5_2026-08-17.json` records the first
frame-time measurement of the Ogre-Next path, on the same Apple M5, the same
CityWorld/AlexisSaber scene, and the same `high` preset as the OGRE 14
baseline.

| Renderer | Resolution | mean | FPS | p95 | p99 |
| --- | --- | ---: | ---: | ---: | ---: |
| OGRE 14 | 1920x1080 | 12.19 ms | 82 | 18.08-18.95 ms | 20.0-21.5 ms |
| Ogre-Next combined | 1920x1080 | 39.41 ms | 25.4 | 46.67 ms | 47.41 ms |
| Ogre-Next combined | 1280x720 | 39.64 ms | 25.2 | 46.42 ms | 47.48 ms |

The Ogre-Next combined runtime costs about 3.2x the OGRE 14 frame time and
misses the declared 60 FPS budget by a wide margin. Both runs are measure-mode;
gated runs would fail on both budgets.

The most useful fact is the one the two resolutions give away: 720p and 1080p
land within 0.6 percent of each other, so this cost is resolution-independent.
The path is CPU or producer bound, not raster bound, which means the raster
work is not what to optimize first. The combined runtime still retains a hidden
OGRE 14 scene and resource producer by design — the roadmap lists its removal
as open work — and that producer is the first suspect to measure.

Two separate faults surfaced in the same session and remain open: the combined
runtime reports `failed_dispatch` (frontend=2) while unloading the terrain at
the generation boundary during shutdown, and the `simple2` terrain fails
earlier with `failed_dispatch` (frontend=9) on its first scene submission and
never renders at all.

## Building the combined runtime to measure it

`ROR_OGRE_NEXT_COMBINED_RUNTIME` is mutually exclusive with the launcher and
child-package options, so it needs its own build tree. Two properties of that
tree are easy to trip over:

- The pinned OGRE-Next probe refuses to reconfigure an existing build
  directory, so the tree cannot be re-configured in place; recovery is a fresh
  directory.
- The embedded-namespace audit pins the source commit captured at configure
  time and fails closed when the checkout has moved. Committing between
  configuring and building therefore fails the build with "audited checkout
  differs from the expected source commit". Freeze the tree across both steps,
  or configure and build in one invocation.

```sh
cmake -S . -B /tmp/ror-combined-build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 \
  -DCMAKE_PROJECT_TOP_LEVEL_INCLUDES=cmake/conan_provider.cmake \
  -DROR_OGRE14=ON -DROR_OGRE_NEXT_COMBINED_RUNTIME=ON \
  -DROR_RENDERER_PUBLIC_LAUNCHER=OFF \
  -DROR_OGRE_NEXT_PRODUCTION_PACKAGE=OFF \
  -DROR_OGRE_NEXT_DEMO_ADMISSION=OFF \
  -DROR_CREATE_CONTENT_FOLDER=ON &&
cmake --build /tmp/ror-combined-build --config Release
```

The measurable executable is `bin/RoR-Combined`.

## Presentation pacing

A distribution whose median sits on a display refresh interval with a tight
tail is very likely paced by the compositor rather than by the renderer, even
when the run requested VSync off. The driver reports this as
`presentation_pacing` with the suspected refresh rate. It never changes a pass
into a fail — pacing only ever understates the renderer — but it stops a
recorded number from later being read as renderer headroom.

## Running it

```sh
python3 tools/run_playable_performance_scene.py \
  --executable /absolute/path/to/RoR-Ogre14 \
  --artifact-dir /tmp/ror-playable-performance \
  --scenario-id playable-cityworld-alexis-1080p-high \
  --terrain CityWorld.terrn2 \
  --actor AlexisSaber.truck \
  --mod-archive "$HOME/Library/Application Support/Rigs of Rods/mods/CityWorld.zip" \
  --mod-archive "$HOME/Library/Application Support/Rigs of Rods/mods/AlexisSaber.zip" \
  --graphics-preset high \
  --width 1920 --height 1080 \
  --warmup-frames 120 --minimum-frames 600 --frames 1800
```

The artifact directory must not already exist. The run stages an isolated
profile inside it, resolves the same per-platform user-directory layout the
game resolves — including the macOS split between a packaged `.app` and a
development build — symlinks the named mod archives so the user's own archives
stay byte-identical and unmoved, and retains the receipt, the run report, the
runtime log, and the console output.

## What a passing run proves

It proves that this executable, on this machine, with this preset, resolution,
terrain, and actor, held the declared mean and percentile budgets across the
recorded frames, with no renderer fault, no limiter, no rejected interval, and
no scene change.

It does not prove a frame rate on another machine, another preset, another
scene, or another platform. It does not measure input latency, load time, or
memory. It is a fixed-camera-free but also driver-free measurement: nothing
drives the vehicle, so it does not cover traversal, streaming under motion, or
physics load at speed. Those remain separate scenarios.

## First recorded result on the reference machine

`evidence/PLAYABLE_PERFORMANCE_M5_2026-08-17.json` records five attempts at the
CityWorld/AlexisSaber playable scene, 1920x1080, `high` preset, on one Apple
M5, using the OGRE 14 compatibility executable. PSSM shadows were verified
active from the runtime's own statement: three cascades at 4096/3072/2048,
quality 3.

- The sustained 60 FPS budget is met comfortably: 11.62–12.46 ms mean, 80–86
  FPS.
- The 18.3 ms p95 ceiling is **marginal**: three recorded runs landed between
  18.08 and 18.27 ms and one exceeded it at 18.95 ms. The CityWorld visual
  milestone's frame-time criterion is therefore not yet met reliably on this
  machine.
- One of the five attempts crashed during terrain load with `SIGBUS` inside
  `Ogre::LodOutputProvider::bakeSecondPass`, reached through
  `MeshLodGenerator::generateAutoconfiguredLodLevels` from
  `MeshObject::createEntity` while `TerrainObjectManager` loaded a legacy v1.40
  CityWorld object mesh. That is the roadmap's open "generated LOD safety"
  concern for this fixture, now with a stack. Automatic LOD generation is
  reached whenever a mesh ships no manual or custom LOD and `gfx_auto_lod` is
  set.

An earlier result set recorded before the preset verification described above
existed is superseded and was not retained: it had been measured with
`gfx_shadow_type` silently reinterpreted to "no shadows", so its numbers
describe a scene with no dynamic shadows at all.

A single run remains evidence of a measurement rather than a stable acceptance
threshold. Gate over a repeated set whose worst run passes.
