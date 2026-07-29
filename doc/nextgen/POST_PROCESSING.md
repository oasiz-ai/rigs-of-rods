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

The current kernel is an oracle, not renderer completion. The remaining V0
work is to implement matching GLSL and HLSL materials, attach one final
half-resolution compositor after water/weather and before UI, validate resize
and hot-load lifecycles, and record the disabled-path pixel baseline plus
quality/performance captures on macOS, Linux, and Windows.
