# Ogre-Next HDR numerical reference

The HDR reference has two explicit version-1 behaviors. Neither behavior is a
shipping-renderer or image-quality claim.

## Source identity

[`ogre-next-hdr-reference.lock.json`](../../tools/ogre_next_probe/ogre-next-hdr-reference.lock.json)
is the path-bound source closure for this reference. Its Ogre-Next commit must
equal the commit in the canonical
[`ogre-next.lock.json`](../../tools/ogre_next_probe/ogre-next.lock.json). The
18-entry HDR-specific lock records exact SHA-256 values for `HdrUtils.cpp`,
`HDR.material`, the
Metal/HLSL/GLSL `DownScale03`, bright-pass, horizontal/vertical blur, and final
tone-map shaders, plus `HDR.compositor`. The compositor is part of the contract
because it declares `RGBA16_FLOAT` scene color, `R16_FLOAT` luminance history,
and the `R10G10B10A2_UNORM` bloom intermediates. The material script is equally
part of the closure because it selects the cross-API shader delegates, binds
frame time, and declares point/bilinear sampling. The verifier rejects duplicate
JSON keys, unknown schema fields, and any role/path remapping.

The metadata-only check is:

```console
python3 tools/verify_hdr_reference_sources.py
```

When an exact pinned Ogre-Next tree is available, verify every source byte:

```console
python3 tools/verify_hdr_reference_sources.py \
  --ogre-source-root /absolute/path/to/pinned/ogre-next
```

## Analytic behavior v1

`EvaluateHdrAnalyticAutoExposure` and `EvaluateHdrAnalyticFinalToneMap` evaluate
the selected equations in IEEE-754 binary64. This is the ideal, unquantized
reference for calibration and easy-to-audit golden values. It deliberately
does not claim to return the exact GPU value and it must not be iterated as a
substitute for the renderer's `R16_FLOAT` exposure history.

The admitted source envelope is finite and explicit:

| Value | Admitted interval |
| --- | --- |
| Exposure and automatic-exposure limits | `[-16, 16]` |
| Frame delta | `[0, 60]` seconds |
| Log-luminance source | `[-65504, 65504]` |
| Positive R16 luminance source | `[2^-24, 65504]` |
| RGBA16 scene channels | `[0, 65504]` |
| Gamma-2 bloom sample and alpha | `[0, 1]` |

The exposure interval contains every preset in the pinned Ogre-Next HDR sample
and prevents a binary64-only success for inputs that overflow Ogre's float
`expf` parameter construction.

## Shader behavior v1

`EvaluateHdrShaderAutoExposure` and `EvaluateHdrShaderFinalToneMap` evaluate the
pinned shader equations in IEEE-754 binary32 under strict floating-point build
flags. Source scene color, alpha, and inverse luminance are rounded through the
real binary16 texture boundary. Auto-exposure returns both the pre-storage
binary32 result and the exact round-to-nearest, ties-to-even R16 bits. Feeding
`stored_inverse_luminance_r16.decoded` into the next frame reproduces the
quantized temporal recurrence, including its steady-state stall at 48 FPS.
The admitted nonnegative source contract canonicalizes negative zero to positive
zero before storage and rejects inputs above `65504`; that upper rejection is a
source-format admission rule, not the IEEE binary16 overflow midpoint.

`bloom_gamma2_encoded` is intentionally not called sRGB. The upstream bright
pass writes `sqrt(linear / 16)` to an ordinary RGB10 UNORM target, its blur
shaders decode and re-encode with `x*x`/`sqrt`, and final tone mapping uses
`x*x*16`. A filtered bloom sample remains within `[0,1]` but need not lie on a
single 10-bit UNORM step.

All invalid versions, NaN/infinity, source-domain violations, float overflow,
and R16 infinity/zero failures are transactional: the caller's output object is
unchanged.

## Comparison policy

C++ strict-FP options prevent fast-math reassociation, but C++ standard-library
`exp`/`pow`, GPU transcendental approximations, and GPU contraction policy are
not specified to be bit-identical across macOS, Windows, Linux, Metal, HLSL,
and GLSL. `CompareHdrFinalToneMapReferences` first reconstructs the analytic
input from the shader behavior's decoded RGBA16/R16 scene, inverse-luminance,
and alpha values. Thus texture quantization is tested by exact bits and is never
misclassified as equation error. Therefore:

- compare a well-conditioned primitive analytic value to its binary32
  shader-equation value using
  `abs(a-b) <= 2e-6 + 2e-5 * max(abs(a), abs(b))`;
- compare CPU R16 conversion and selected feedback golden fixtures by exact
  binary16 bits;
- compare a real GPU value near a binary16 rounding midpoint using the numeric
  equation tolerance plus one binary16 ULP, and select release fixtures away
  from a midpoint whenever an exact-bit gate is required;
- require every intermediate and captured HDR buffer to remain finite.

Adapted exposure is ill-conditioned when a small frame delta makes binary32
`pow(0.25, dt)` round to one. A single relative output tolerance is invalid in
that region. `CompareHdrAutoExposureReferences` separately gates target
inverse-luminance and previous-frame weight with the scalar rule, then uses the
following sensitivity bound. Let `Ta/Ts` be the analytic/shader targets,
`wa/ws` their weights, `P` the common decoded previous R16 value, and `Aa/As`
the adapted outputs:

```text
conditioning = |1-wa| |Ta-Ts| + |Ts-P| |wa-ws|
rounding = gamma5 (|Ts(1-ws)| + |P ws|) + 4*float_denorm_min
gamma5 = 5u / (1-5u), u = 2^-24
require |Aa-As| <= conditioning + rounding
```

This preserves a tight equation gate without rejecting a correct shader solely
because subtraction near a weight of one amplifies an otherwise valid binary32
weight difference.

These are numerical-equation gates. Backend frame captures, perceptual image
thresholds, display gamut/transfer, framebuffer clamping, dithering, bloom
spatial reconstruction, and performance acceptance remain separately versioned
roadmap work.
