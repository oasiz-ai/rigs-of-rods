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

The pack is intentionally not registered or attached yet. The runtime
integration must register `postprocess.zip` in a dedicated resource group
before looking up these names, verify that the selected backend pair is
supported, and then attach the compositor after water/weather and before
native-resolution overlays. It must detach before terrain/render teardown and
recreate viewport-dependent state after a resize. Until that transaction is
implemented, V0A causes no runtime visual change.

Remaining V0 work is runtime registration and lifecycle integration, followed
by the disabled-path pixel baseline and quality/performance captures on macOS,
Linux, and Windows. Bloom remains a separate later profile because its
half-resolution targets and blur lifecycle require their own measured gate.
