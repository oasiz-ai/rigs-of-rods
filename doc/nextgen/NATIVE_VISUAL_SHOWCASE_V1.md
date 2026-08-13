# Native visual showcase v1

The forward-native showcase is the renderer acceptance scene, not a legacy
conversion demo. Its editable sources and compiled package must be
`project_original` or a rights-cleared derivative, and every visible mesh and
material must enter through the renderer-neutral asset registry. BeamNG, legacy
RoR, CityWorld, OGRE material scripts, ODEF, and v1 meshes are separate
compatibility fixtures and cannot satisfy this gate.

The machine-readable source of truth is
`tools/ogre_next_probe/native-visual-showcase-v1.contract.json`. The report is
verified by `tools/verify_native_visual_showcase.py`. The contract deliberately
requires two complete modes:

- `raster_high` demonstrates PBR, HDR, PSSM, atmosphere, reflection-probe IBL,
  screen-space reflections, ambient occlusion, water, volumetrics, particles,
  motion vectors, temporal anti-aliasing, bloom, exposure, tone mapping, output
  transfer, and UI composition.
- `native_rt_ultra` retains the common pipeline and replaces the raster-only
  reflection and directional-shadow evidence with native RT sun visibility,
  native RT reflections, and a temporal/spatial reflection denoiser. It must
  use the renderer's native device and queue without a fallback.

This is an end-state contract. A pass may remain unimplemented while work is in
progress, but the showcase cannot be called complete, next-generation, or
hardware-RT demonstrated until the corresponding report passes. A synthetic
triangle, shader compilation, a native API capability probe, a pass counter, or
an isolated image is not a substitute for the complete report.

## Evidence rules

Each pass records its version, completed-frame lineage, native state
verifications, GPU timestamp samples, p50/p95 time, input/output lineage, zero
production content readbacks, and an exact pass-specific visual witness. The
witness binds distinct pass-off and pass-on artifact hashes and a minimum
changed-pixel or coverage measurement. Both main modes run at least 600 frames
after 120 warm-up frames at no less than 1920x1080.

The final evidence set includes all-on raster and RT captures, a moved-caster RT
capture, water/weather and particle motion captures, and a gameplay/UI capture.
Acceptance-only image reads are allowed only in the artifact harness. The
production renderer performs zero texture-content and framebuffer readbacks.

The report also fails when it observes a visible legacy asset, legacy render
pass, legacy material fallback, missing-resource fallback, or untracked runtime
asset conversion. Source must be an exact clean commit, the native package and
contract are SHA-256 bound, A0 provenance approval is explicit, and the RT run
must record native support, admission, and same-renderer device/queue identity.

## Pass ordering

The dependency graph in the contract is normative. In broad terms it is:

1. Renderer-native PBR geometry, alpha, and emissive material evaluation.
2. Linear base and direct-sun HDR lighting.
3. Raster PSSM or native RT sun visibility, atmosphere, IBL, reflections,
   ambient occlusion, water, clouds, fog/light shafts, and particles.
4. Motion-vector-driven temporal effects and RT reflection denoising.
5. Bloom, temporal exposure, filmic tone mapping, sRGB output, and UI.

No pass may be inferred from the final image. Each must publish its own bounded
execution and witness evidence, and failures remain named rather than silently
selecting a lower-quality path.
