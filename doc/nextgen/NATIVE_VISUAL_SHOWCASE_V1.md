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

## Temporal anti-aliasing boundary

`OgreNextTaaContract` version 1 fixes the renderer-neutral temporal policy
before a native shader is admitted: an eight-phase Halton jitter in output
pixels with unjittered culling; previous-pixel minus current-pixel motion with
jitter removed; current and previous rigid/object-transform lineage;
non-reversed `[0,1]` depth reprojection; pre-exposed linear `RGBA16_FLOAT`
history rescaling; YCoCg variance-neighbourhood clipping; and a `[0,1]`
reactive mask for particles, emissive, water, and transparency. Camera cuts,
view or extent changes, suspend/restore invalidation, and excessive exposure
changes discard history transactionally. The ping-pong history advances once
only after the complete frame can commit, and production content/framebuffer
readbacks must remain zero. The currently executed HDR split writes raw
scene-referred values, so its TAA pre-exposure scalar is exactly `1.0`; the
separate R16 auto-exposure history is consumed by bright-pass/tone mapping and
must not be mislabeled as TAA pre-exposure.

The scalar shader oracle fixes operation order and rounds every intermediate
to binary32 with contraction and fast-math disabled in every participating
test, embedded-runtime, and probe target. Native shader admission must follow
that same `ordered_binary32_no_contraction_v1` arithmetic contract; repeating
the host function alone is not accepted as GPU conformance evidence.

Every frame plan and native execution receipt also carries a non-reused
frontend lifecycle epoch. Reset and device replacement retire the preceding
epoch, so stale plans, native texture identities, and receipts cannot be
replayed into a new renderer lifetime.

The contract and CPU pixel oracle are prerequisites, not visual evidence. The
`temporal_aa` showcase pass remains incomplete until the native frontend
produces the exact depth, motion, and reactive inputs, executes a GPU TAA pass,
publishes the per-frame metadata receipt, and passes the required pass-off/on
image and 600-frame stability witnesses.
