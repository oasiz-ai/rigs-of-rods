# AirSim-Referenced Visual Fidelity Contract

This contract turns “at least AirSim-quality visuals” into a reproducible
measurement target. It does not claim that current Rigs of Rods rendering
already meets that target.

## Pinned reference

The external reference is
[Microsoft AirSim v1.8.1](https://github.com/microsoft/AirSim/tree/v1.8.1),
commit `96235148a332fe7cb3d3525a0720e26faaca99e0`, using Unreal Engine
4.27. AirSim is an Unreal/Unity simulation plugin rather than one fixed
renderer or content library, so visual quality varies with the host
environment. Its own documentation describes the bundled
[Blocks environment](https://github.com/microsoft/AirSim/blob/v1.8.1/docs/unreal_proj.md)
as intentionally basic and directs photorealistic users to external Unreal
projects.

The comparison has two reference lanes:

1. **Shared-scene lane:** one rights-cleared source scene, camera path, weather
   profile, and lighting profile are converted independently for AirSim and
   RoR. An offline path-traced render of that same source is the image-quality
   reference.
2. **Official-environment lane:** unmodified AirSim v1.8.1 release environments
   are captured only as qualitative feature and presentation references. Their
   files are never extracted into, converted for, or redistributed with RoR.

The shared-scene lane is the acceptance authority. Comparing unrelated maps
would conflate content quality with renderer quality and could not establish
non-inferiority.

## Rights and import boundary

AirSim's repository is published under the
[MIT license](https://github.com/microsoft/AirSim/blob/v1.8.1/LICENSE), but
that does not make every environment used by an AirSim binary reusable.
Microsoft's
[v1.8.1 release notes](https://github.com/microsoft/AirSim/releases/tag/v1.8.1)
state that downloadable environments use proprietary assets and that their
source projects cannot be distributed. AirSim's environment guide likewise
uses Unreal Marketplace projects as examples. Unreal `.uasset`, `.umap`,
cooked packages, Marketplace assets, and engine/template content therefore
remain outside the RoR conversion and distribution boundary unless the exact
asset has separate, verified permission for use outside Unreal.

Direct import is allowed only for source assets whose rights record explicitly
permits:

- conversion outside the source engine;
- derivative works;
- redistribution in the intended RoR package; and
- the intended commercial, research, and machine-learning uses.

Every imported source must pass `CONTENT_PROVENANCE.md`. Unknown, engine-only,
or binary-only rights fail closed. AirSim screenshots and videos may be stored
as local benchmark evidence when permitted, but are not project content.

The direct-import decision is therefore:

| Source | RoR use | Decision |
| --- | --- | --- |
| AirSim simulator source at the pinned commit | API and sensor behavior reference; MIT-licensed code may be reused with its notice | Allowed where it provides an actual RoR capability, but it does not supply Unreal visual fidelity |
| Bundled Blocks `.uasset`/`.umap` files | Basic qualitative test-map reference | Capture-only by default; the map is intentionally low-detail and is neither the fidelity target nor a portable source format |
| AirSim release environments such as AirSimNH, Africa, and LandscapeMountains | High-detail qualitative comparison | No import; the release states that proprietary assets prevent source-project distribution |
| Independently authored or separately licensed glTF source | Shared benchmark and showcase environment | Preferred import path after provenance and deterministic compiler gates pass |

RoR will not add an Unreal package reader or treat the AirSim repository's MIT
license as a blanket grant for third-party environment content. If an exact
AirSim-adjacent asset later has independently verified portable rights, it
enters through the same glTF and provenance boundary as every other source.

## Shared source scene

The benchmark scene is authored independently of both engines and contains:

- one square kilometre of terrain with macro height, blended detail layers,
  decals, and authored collision;
- urban and natural regions with opaque, masked, transparent, emissive, metal,
  dielectric, wet, vegetation, glass, and water materials;
- near, middle, and far geometry with explicit LODs and collision proxies;
- repeated geometry suitable for instancing and occlusion tests;
- one native DAF and one high-detail rights-cleared vehicle;
- fixed noon, dusk, night, rain, and wet-after-rain profiles;
- fixed driver, chase, low roadside, aerial, tunnel, and reflective-surface
  camera paths; and
- color charts, roughness/metalness spheres, shadow penumbras, thin geometry,
  sub-pixel edges, fast motion, and emissive highlights for diagnostics.

The canonical source representation is glTF 2.0 plus declared texture,
collision, terrain, weather, and provenance manifests. Engine-specific files
are generated artifacts, not authoring sources.

## RoR scene compiler boundary

Add an offline `ror_scene_compiler` rather than teaching the runtime to accept
arbitrary host formats. Its input boundary must:

- accept only a versioned glTF 2.0 profile and allowlisted extensions;
- validate portable paths, counts, sizes, finite transforms, coordinate units,
  texture dimensions/formats, graph cycles, and aggregate budgets before
  allocation;
- preserve stable source object/material IDs and deterministic traversal;
- convert the declared coordinate basis and units through one tested transform;
- generate tangents, authored LOD tables, meshlets or OGRE meshes, collision
  proxies, terrain tiles, and material records deterministically;
- transcode textures to the selected macOS/Windows/Linux runtime formats while
  retaining source hashes and color-space metadata;
- emit a canonical conversion report containing source hashes, tool revision,
  options, warnings, unsupported features, outputs, and output hashes; and
- reject packaging when any source asset lacks an accepted rights record.

The runtime loads only the bounded compiled package. It never executes imported
scripts, shaders, Blueprints, Lua, or host-engine metadata.

The first compiler profile and production fixtures are implemented for the
project-owned CityWorld tangent and 15-degree curved bridges. They lower
validated GLB render LODs and collision objects into pinned little-endian OGRE
14 meshes, emit stable material and ODEF records, preserve the curve fixture's
core-glTF emissive factor, and record both non-packaged XML-intermediate hashes
and checked runtime-output hashes. The portable CI gate revalidates the entire
source/profile/output contract without depending on Blender or a platform OGRE
tool. See
[CityWorld Next offline scene compiler](CITYWORLD_SCENE_COMPILER.md).

That first slice is executable rather than a package-only proof. The macOS
arm64 runtime gate requires all render/collision meshes to load, RTShaderSystem
programs for every bridge material, a collision-enabled three-span vehicle
traversal through both connector seams, and a fully decoded UI-free RGB frame.
This proves the asset/compiler/runtime seam only; it does not yet satisfy the
shared AirSim-reference perceptual, HDR, temporal, vegetation, reflection,
shadow, or performance gates below.

## Capture contract

Both engines render the shared camera path under equivalent conditions:

- 1920x1080 output, fixed aspect ratio and field of view;
- matched near/far planes, transforms, sun direction/intensity, exposure,
  white balance, weather, and time of day;
- fixed quality preset with dynamic resolution, vendor upscaling, film grain,
  chromatic aberration, lens dirt, and auto exposure disabled;
- scene RGB before UI, cursor, debug geometry, labels, or editor overlays;
- lossless frames with linear-HDR buffers retained where supported;
- fixed warm-up and frame count, with the first accepted frame explicitly
  identified; and
- renderer, GPU, driver, OS, executable, content, settings, and camera-path
  hashes stored with every run.

AirSim exposes scene, depth, segmentation, surface-normal, and optical-flow
image modes through its
[image API](https://github.com/microsoft/AirSim/blob/v1.8.1/docs/image_apis.md).
Those modes are feature references for future RoR sensor outputs, but only
scene RGB participates in the initial visual-fidelity score.

## Quality measurements

The offline path-traced sequence is the common reference. Measure each lighting,
weather, camera, distance, and material stratum separately before aggregation.

- linear-HDR absolute and relative luminance error;
- tone-mapped SSIM and a pinned learned perceptual metric;
- edge and thin-geometry preservation;
- normal, roughness, metal, glass, emissive, water, and vegetation region error;
- shadow position, acne, peter-panning, cascade transition, and penumbra error;
- reflection position and temporal stability;
- disocclusion, sub-pixel shimmer, specular aliasing, motion smear, and
  frame-to-frame flicker;
- clipping, banding, NaN/infinity, invalid color, and exposure pumping counts;
- visible LOD transition magnitude and texture residency failures; and
- blind side-by-side review for defects not represented by the numeric suite.

The metric implementation, model weights, color transforms, crop rules, and
aggregation weights are versioned and hashed. A learned score alone can never
pass a scene with missing geometry, invalid frames, UI contamination, or a
failed diagnostic stratum.

## Acceptance gate

RoR reaches the AirSim visual-fidelity floor only when the same candidate build
passes all of the following:

- No shared-scene diagnostic stratum is more than 2% worse than AirSim's pinned
  perceptual error relative to the offline reference.
- Aggregate perceptual error is lower than AirSim's by at least 5%, with
  bootstrap 95% confidence intervals computed over camera-path segments.
- HDR luminance, edge, shadow, reflection, transparency, vegetation, and
  temporal-stability gates each pass their separately approved absolute limits.
- No blank, stale, corrupt, non-finite, UI-contaminated, missing-material, or
  missing-geometry frame is accepted.
- A blind review does not identify a systematic regression hidden by the
  metrics. Any such regression becomes a named automated fixture before
  release.
- The native macOS arm64 high preset holds 60 FPS at 1920x1080 on the declared
  reference Apple Silicon machine, with p95 frame time at or below 18.3 ms and
  no unbounded resource growth in a ten-minute loop.
- Windows and Linux meet separately recorded budgets on their declared
  reference GPUs; renderer differences are reported rather than masked by one
  cross-platform screenshot threshold.
- The legacy fallback still loads existing content, and disabling new visual
  features remains pixel-identical to its recorded baseline.

“Exceeds AirSim” may be stated only for the pinned shared-scene version and
measured hardware when the 5% aggregate margin and every non-regression gate
pass. It is never a universal claim about arbitrary AirSim/Unreal environments.

## Implementation order

1. Finish R0 renderer stability: PSSM, shader/resource completeness, ten-minute
   starter-scene soaks, and a production macOS material path.
2. Land V0 deterministic scene-only capture, fixed exposure/tone curve, bloom,
   and anti-aliasing with UI composited afterward.
3. Land V1 linear HDR and the PBR material schema with tangent, transparency,
   reflection, and color-space tests.
4. Land the fail-closed glTF scene compiler and a small original fixture before
   importing a showcase environment.
5. Produce the shared source scene, independent offline truth, AirSim
   conversion, camera path, and versioned metric tool.
6. Optimize instancing, LOD, texture streaming, occlusion, shadows, vegetation,
   reflections, atmosphere, water, and temporal stability until the full gate
   passes on macOS first.
7. Repeat the same immutable benchmark on Windows and Linux before publishing a
   cross-platform fidelity claim.
