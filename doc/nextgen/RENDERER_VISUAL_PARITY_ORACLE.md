# Paired renderer regression-floor visual oracle

`tools/compare_renderer_visual_parity.py` is the first deterministic pixel gate
for a UI-free legacy-renderer frame and an Ogre-Next frame captured from the
same scene state. OGRE14 is the regression floor, not the desired visual target:
Ogre-Next should meet or exceed its quality. This dependency-free SDR
screenshot oracle measures a bounded symmetric difference from that floor. It
does **not** establish that the candidate is better, require pixel identity, or
establish full renderer parity, HDR parity, temporal parity, FLIP parity, or
cross-platform runtime correctness.

## Accepted inputs

Both images must have the same dimensions and use this deliberately narrow PNG
profile:

- the exact PNG signature and a valid CRC on every chunk;
- one `IHDR`, one contiguous nonempty `IDAT` run, then one `IEND`, with no
  trailing bytes;
- non-interlaced, 8-bit RGB or fully opaque RGBA pixels;
- standard compression and filter methods, with scanline filters 0 through 4;
- dimensions of at least 3 by 3, at most 16,384 on either axis, and at most
  16,777,216 decoded pixels;
- no palette and no ancillary chunks. Color interpretation comes from the
  explicit metadata contract, not optional PNG chunks;
- at least two distinct RGB pixel values. Constant-color captures are rejected
  as blank.

The reference and candidate PNG paths must resolve to distinct filesystem
objects, as must their two metadata paths; direct, symbolic-link, and hard-link
aliases are rejected as likely capture wiring mistakes. Independently supplied
files may still have byte-identical contents; that is a useful zero-difference
control, not the product objective.

Each image has a separate UTF-8 JSON metadata file. Duplicate keys, non-finite
numbers, excessive nesting, and integers outside the exact interoperable JSON
range are rejected. The root has exactly these fields:

```json
{
  "schema": "ror.renderer_visual_parity_frame_metadata.v1",
  "renderer": {
    "name": "ogre14",
    "backend": "metal",
    "build_sha256": "0000000000000000000000000000000000000000000000000000000000000000"
  },
  "content": {
    "scene": "CityWorld",
    "content_sha256": "2222222222222222222222222222222222222222222222222222222222222222"
  },
  "camera": {
    "projection": "perspective",
    "position": [10.0, 2.0, -4.0],
    "orientation_xyzw": [0.0, 0.0, 0.0, 1.0],
    "vertical_fov_degrees": 60.0,
    "near_clip": 0.1,
    "far_clip": 2000.0
  },
  "exposure": {"mode": "manual", "ev100": 11.5},
  "weather": {"preset": "clear", "time_of_day_seconds": 43200},
  "resolution": {"width": 1920, "height": 1080},
  "color_space": "srgb",
  "ui_free": true
}
```

`renderer` is allowed to differ and must identify the exact build by SHA-256.
`content`, `camera`, `exposure`, `weather`, `resolution`, `color_space`, and
`ui_free` must be structurally identical across the pair. The declared
resolution must also equal both decoded PNGs. Extra fields inside renderer,
content, camera, exposure, or weather are permitted, but all non-renderer fields
remain part of the exact matching contract.

## Metrics and thresholds

The fixed profile `ror.renderer_visual_parity_metrics.global_srgb_v1` decodes
each channel from IEC sRGB to linear light and reports:

- exact changed-pixel count and fraction;
- aggregate and per-channel linear-light mean absolute error and root mean
  square error;
- deterministic global luminance SSIM using Rec. 709 luminance, population
  variance/covariance, `C1 = 0.01^2`, and `C2 = 0.03^2`;
- mean directional Sobel disagreement over interior pixels. Both 3 by 3 Sobel
  gradient vectors are subtracted and normalized by the maximum possible
  joint vector difference for a difference field in `[-1, 1]`,
  `sqrt(80) = 4 * sqrt(5)`.

The initial defaults are:

| Gate | Default |
| --- | ---: |
| linear RGB MAE maximum | 0.03 |
| linear RGB RMSE maximum | 0.06 |
| global luminance SSIM minimum | 0.98 |
| Sobel disagreement maximum | 0.05 |
| changed-pixel fraction maximum | 1.0 |

The last metric is diagnostic by default; callers can make it a gate. Every
threshold is explicit in the receipt and can be tightened with the command-line
options. Gate comparisons use the finite, unrounded metric values serialized in
the receipt; display-oriented quantization never weakens a boundary. This
global SSIM and fixed Sobel metric are an initial regression gate, not a
substitute for a validated perceptual metric such as FLIP.

These metrics are symmetric: they penalize a deliberate improvement just as
they penalize a regression. A passing receipt therefore means only that the
candidate stayed within the configured difference budget from the OGRE14
floor. A materially improved candidate may legitimately fail this comparator;
accepting it requires reviewed modern-reference or directional quality evidence
and an intentional threshold/baseline update, never forcing the renderer back
to legacy pixels.

## Receipt and exit behavior

Example:

```sh
python3 tools/compare_renderer_visual_parity.py \
  --reference ogre14-ui-free.png \
  --candidate ogre-next-ui-free.png \
  --reference-metadata ogre14-ui-free.json \
  --candidate-metadata ogre-next-ui-free.json \
  --output renderer-visual-parity-receipt.json
```

The canonical `ror.renderer_visual_parity_receipt.v1` JSON binds the exact PNG
and metadata byte lengths and SHA-256 values, tool source SHA-256, matched
capture contract and its SHA-256, dimensions, validation profile, metrics,
thresholds, individual gate results, final `passed` boolean, and explicit
comparison semantics: reference role `regression_floor`, candidate goal
`meet_or_exceed_reference_quality`, symmetric difference budget, no required
pixel identity, and no improvement claim from symmetric metrics. It contains
no timestamp or absolute path, so the same tool and exact inputs produce the
same bytes under normal and optimized Python.

The receipt is written to a temporary file in the destination directory,
flushed, and atomically replaced. Exit `0` means every gate passed. Exit `1`
means valid inputs were compared and an atomic fail receipt was written. Exit
`2` means the PNG, metadata, or output contract was invalid; in that case an
existing output receipt is left untouched.

This oracle should be one gate in a larger visual-parity suite. A renderer
fallback may only be removed after representative scene coverage, live runtime
lifecycle and performance evidence, transparent/material semantics, sky,
reflection, terrain, UI, and platform-specific behavior are independently
closed.
