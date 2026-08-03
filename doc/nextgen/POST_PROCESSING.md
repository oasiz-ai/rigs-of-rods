# V0 LDR Post-Processing Contract

V0 is an opt-in, renderer-independent LDR post-processing profile. It is a
scene-only pass: OGRE overlays and Dear ImGui remain at native framebuffer
resolution and are composited after V0. Hydrax underwater and Caelum
precipitation remain scene inputs rather than later passes.

`source/main/gfx/PostProcessMath.h` is the dependency-free CPU oracle for the
first shader profile. It locks:

- normalized compositor samples and strict finite/range validation;
- fixed exposure, saturation, contrast, and a rational highlight shoulder;
- soft-knee bright extraction followed by a half-resolution blur;
- bounded bloom composition before the display curve; and
- the luma threshold, edge orientation, and blend limit for scene-only FXAA.

The profile deliberately avoids `pow`, transcendental functions, renderer
defaults, temporal state, wall time, random samples, and automatic exposure.
Malformed input or configuration fails transactionally. The V1 HDR boundary
will replace the normalized LDR sampling contract with explicit linear color,
transfer functions, tone mapping, and PBR material rules.

The reference profile is:

| Parameter | Value |
| --- | ---: |
| Exposure | 1.08 |
| Contrast | 1.04 |
| Saturation | 1.03 |
| Bloom threshold | 0.72 |
| Bloom soft knee | 0.18 |
| Bloom strength | 0.08 |
| FXAA relative edge threshold | 1/8 |
| FXAA absolute edge threshold | 1/24 |
| FXAA blend limit | 0.75 |

## V0A portable shader resources

`resources/postprocess` is packaged as `postprocess.zip` by the existing
resource build. It contains only the `RoR/PostProcess/V0A/` namespace:

- a GLSL 330 core vertex/fragment pair for OGRE 14 GL3Plus;
- a Shader Model 4 vertex/fragment pair for D3D11;
- unified OGRE program declarations that delegate only to those pairs;
- one LDR material named `RoR/PostProcess/V0A/LdrFxaa`; and
- one compositor with the same name.

V0A deliberately implements the locked color curve followed by bounded FXAA,
not the bloom oracle. It reads exactly the center, north, south, east, and west
scene texels. The curve is applied independently to all five samples before
the edge decision, the center alpha is retained, and the material uses
point-filtered, clamped sampling. `inverse_texture_size 0` supplies texel size;
there is no viewport guess in either shader.

The shader graph has no Cg fallback, time, random values, automatic exposure,
transcendental transfer function, derivative sampling, or unbounded loop.
Unsupported program pairs therefore leave the material unsupported instead of
silently selecting a different visual path.

The V0A runtime seam is now present but opt-in. `gfx_postprocess_mode = 0` is
the default and does not register the pack or acquire compositor state.
`gfx_postprocess_mode = 1` requests V0A for simulation scenes. Any other value
fails closed.

For an opt-in scene, the runtime:

- classifies only the exact OGRE GL3Plus/GLSL 330 or D3D11/Shader Model 4
  renderer/program pair;
- registers `postprocess.zip` in `PostProcessRG`, verifies the six source
  resources, loads the selected and unified programs, and requires one
  supported material and compositor technique before attachment;
- accepts only the main render target's viewport zero, after its camera exists;
  custom mirror, video-camera, environment-map, survey-map, and capture
  viewports cannot receive V0A;
- attaches after Terrain has initialized Hydrax/Caelum and appends V0A as the
  last scene compositor before scene overlays are created;
- revalidates that ordering before every frame and re-appends transactionally
  if a Hydrax reload or resource hot-load changes the chain;
- leaves the main viewport overlay flag unchanged. OGRE's compositor RTT
  viewport excludes overlays, while the main output quad is issued before
  `RENDER_QUEUE_OVERLAY`, so OverlaySystem and Dear ImGui stay at native
  backing resolution;
- recreates only when the actual backing-pixel extent changes, detaches while
  either extent is zero, and resumes only at a nonzero extent;
- verifies rather than toggles the compositor before a main-window screenshot;
  and
- detaches as the first scene-unload action and again defensively before the
  main viewport/render target is destroyed.

All adapter exceptions are reduced to one bounded diagnostic line, detach V0A,
and suppress retries for the rest of that scene. Unsupported renderers and
program/resource failures therefore retain the unprocessed scene path.

V0A is not V0 completion. It is only the locked color curve plus five-tap
FXAA: there is no bloom, HDR, PBR, ray tracing, or AirSim-parity claim.
Native GL3Plus and D3D11 image/performance acceptance, the default-off
pixel-identity baseline, Hydrax/Caelum/screenshot captures, and cross-platform
resize/teardown soaks remain open. Bloom remains a separate later profile
because its half-resolution targets and blur lifecycle require their own
measured gate.
