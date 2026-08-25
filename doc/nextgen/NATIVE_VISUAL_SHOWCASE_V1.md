# Native visual showcase v1

The forward-native showcase is the renderer acceptance scene, not a legacy
conversion demo. Its editable sources and compiled package must be
`project_original` or a rights-cleared derivative, and every visible mesh and
material must enter through the renderer-neutral asset registry. BeamNG, legacy
RoR, CityWorld, OGRE material scripts, ODEF, and v1 meshes are separate
compatibility fixtures and cannot satisfy this gate.

The showcase is not the playable-game executable or a replacement scene loop.
Ordinary gameplay remains in `RoR-Combined`, where Ogre-Next owns the visible
window and must fail closed instead of presenting through the hidden OGRE 14
resource producer.

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

## Forward-native scene source checkpoint

`NativeVisualShowcaseSceneSource` is the renderer-neutral runtime consumer for
the current `NATIVE-A0-001` checkpoint. Its loader opens
`resources/nextgen/native/a0_road_tile_12m/rorng_a0_road_tile_12m.rornative`
once, authenticates the complete package against
`5f91c134231d5b86cd0c291d30018aa2f8aa4958c8e9267ec1c9068a0ea9bc05`,
and retains the resulting immutable package owner. A capture publishes those
exact asset payload owners and five authored static instances directly to
`GraphicsSceneFrameInput`; it does not pass through OGRE 14, an ODEF or terrain
converter, a legacy material translator, runtime asset conversion, or another
terrain system.

The source fixes a 1920x1080 camera from the checked composition's position
`(8,7,10)`, target `(0,0,-0.2)`, 50-degree vertical field of view, and
`0.1/50 m` clip planes. It publishes one D65 directional sun at `110000 lux`
with direction `(0.60,-0.64,0.48)`, an analytic sky tied to that exact light
identity, and a deterministic 60 Hz simulation clock. The only evidence pose
override translates the authored shadow gate by exactly `+1.5 m` on X; asset,
material, camera, sky, time, and every non-gate transform remain unchanged.
Capture, commit, and discard are transactional, so a rejected producer frame
does not advance the clock or gate pose.

The package's authored `TextureBinding` scale and offset fields are preserved
exactly. In this checkpoint the road uses scale `(2,4)`, the wet strip `(1,4)`,
and the lane `(1,6)`, all on UV0 with zero offset and rotation. The current
RT4/V1 frontend still rejects these non-identity transforms at
`assets.material.texture_transform`; native UV-transform support and its GPU
evidence are a named pending frontend dependency. The scene source must not
hide that gap by baking, rewriting, or otherwise converting package UVs at
runtime. The asset package remains editable: a future authored revision gets a
new reviewed digest and updates this checkpoint explicitly.

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
