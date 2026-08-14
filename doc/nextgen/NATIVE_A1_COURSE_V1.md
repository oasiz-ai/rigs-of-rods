# Forward-Native A1 Visual Course v1

`NATIVE-A1-001` is a project-original, renderer-neutral 60 metre visual course
authored as a sibling of `NATIVE-A0-001`. It uses the backward-compatible
`.rornative` v2 extension for its explicit thin-slab transmission material and
does not silently reinterpret the A0 lighting coupon.

## Checked inputs and products

| Artifact | SHA-256 |
| --- | --- |
| `content-source/native_render/a1_native_course_60m/rorng_a1_native_course_60m.native.json` | `f13af91e56670bec17aa286d3b57e1a52d343f1bc307a90dafb9142f21430556` |
| `content-source/native_render/a1_native_course_60m/rorng_a1_native_course_60m.glb` | `7b0648cde63053385d9a7ec66f56da470cfe8bf3465ef5d6c52cc0c9702b7801` |
| `content-source/native_render/a1_native_course_60m/rorng_a1_native_course_60m.composition.json` | `db7cbacdf1228d9e9836b32afc1c7d587151d61b4661715bd4132373f3403980` |
| `content-source/native_render/a1_native_course_60m/rorng_a1_native_course_60m.alignment.json` | `ef6764702e6c70375b4bd8e897e83e3191bd3a52778323538f87c8a4f81a1078` |
| `resources/nextgen/native/a1_native_course_60m/rorng_a1_native_course_60m.rornative` | `6399101c63ca8d5eff25ab499db215c45d89a4ce91cba08145692d025401505d` |

The checked package contains 38 assets, nine static batch instances, and 48
records: nine meshes, eight materials, nineteen textures, and two explicit
samplers. The geometry contains 1,176 vertices, 1,764 indices, and 588
triangles. All vertex streams include finite positions, unit normals,
orthonormal tangents with explicit handedness, UV0, non-degenerate indexed
triangles, and winding consistent with the authored normal hemisphere.

The generator is deterministic and project-owned. It uses a hash-pinned A0
authoring utility for generic GLB packing, tangent derivation, box construction,
TGA writing, and mip filtering; A1 geometry, texture values, materials, and
placements are generated separately. No A0 or legacy geometry, texture,
material-script, shader, or other simulator asset bytes are copied.

## Visual course contents

The visual road spans `x=[-4,4]`, `z=[-30,30]` with a 60 metre centerline.
It contains:

- a high-frequency 1024 px dry-asphalt PBR set with sRGB base color, linear
  normal, and canonical metallic/roughness data;
- a separate 1024 px low-roughness wet-asphalt specular overlay with shared
  deterministic film and flow structure across base, normal, and specular
  channels;
- 512 px gravel/soil shoulder PBR textures on both sides;
- worn center dashes, edge lines, and start/finish lines;
- red/white curbs, joined guardrails and posts, ten emissive/specular static
  calibration markers, and a project-original shadow-calibration gate;
- a project-original 4 m by 3 m by 0.08 m closed glass slab with IOR 1.52,
  0.96 transmission, and explicit Beer-Lambert attenuation parameters;
- explicit clamp and repeat samplers with trilinear filtering, 4x anisotropy,
  zero LOD bias, and complete authored mip chains through 1x1.

The dry and wet texture sets contain eleven mips, shoulder sets ten, barrier
and curb sets nine, lane sets eight, and calibration sets five. The package
contains 39,675,196 texture bytes and is 39,758,410 bytes total.

Repeated structures are deterministically emitted into joined static batches.
`.rornative` v2 retains the v1 one-node/mesh-to-one-static-instance rule, so
shared-geometry multi-placement instancing would require a package/compiler
contract change. This bounded slice avoids that schema expansion.

The v1 composition validator also freezes the three package-local shadow-role
IDs `rorng_a0_road_surface_mesh`, `rorng_a0_wet_asphalt_mesh`, and
`rorng_a0_road_shadow_gate_mesh`. A1 retains those role tokens inside its own
package rather than changing the shared validator and invalidating A0's
embedded validator digest. All other A1 IDs are package-native `rorng_a1_*`.

## Alignment contract and nonclaims

`rorng_a1_native_course_60m.alignment.json` is a strict
`ror-native-course-alignment-v1` record for later RoR physics work. It carries:

- the exact centerline, nominal visual road width, driveable visual bounds,
  and start/finish centers;
- dry road, wet overlay, left/right shoulder, and separate raised left/right
  curb-top polygons, each bound to an exact decoded GLB mesh component, height
  range, visual height, and zero `dy/dx`, `dy/dz` slope;
- exact road-curb boundaries at `x=-4,4` and curb-shoulder boundaries at
  `x=-4.15,4.15`, including their adjacent surface heights, full decoded curb
  vertical-face range `y=[-0.02,0.12]`, and shared `z=[-30,30]` span;
- 50 named component placements with batch mesh, category, position,
  orientation, and dimensions;
- `collision_binding: null` and `physics_material: null` for every surface,
  plus `collision_binding: null` for every placement.

The alignment validator rejects unsupported formats, noncanonical JSON,
duplicate identifiers, invalid or out-of-range course geometry, missing
barrier/curb/lane/calibration coverage, invalid seams, and any collision or
physics-binding claim. It decodes the hash-pinned GLB and rejects any surface,
curb placement, boundary, height, slope, polygon, or vertical face that does
not reconcile with the authored triangle geometry. The bounded A1 oracle also
requires every manifest mesh ID to name the same GLB node, exact identity
`render_from_object`, and complete surface-component coverage; transform-aware
alignment is deliberately deferred instead of silently comparing object-space
and rendered-space coordinates. Each rectangular top and curb vertical face
must contain exactly two consistently outward-wound triangles, four perimeter
edges used once, and one valid diagonal used twice. This rejects matching-area
overlap-plus-hole topology rather than trusting only bounds and summed area.

This milestone is **visual-only and collision-pending**. The explicit native
showcase selector can publish the package through the renderer-neutral scene
source, but package staging or source validation alone is not live render
evidence. It supplies no RoR terrain, collision mesh, driveability, vehicle
spawn, gameplay, native terrain, AO, LOD, performance, or playability evidence.
Those claims require an explicit, tested RoR physics binding and separate live
RoR acceptance.

The checked PPM is only an authoring-layout preview. It is not renderer output
and is not visual-quality evidence.

## Reproduction

```sh
python3 tools/blender/native_render/generate_a1_native_course.py --repo-root .
python3 tools/validate_native_render_asset.py \
  content-source/native_render/a1_native_course_60m/rorng_a1_native_course_60m.native.json \
  --repo-root .
python3 tools/validate_native_course_alignment.py \
  content-source/native_render/a1_native_course_60m/rorng_a1_native_course_60m.alignment.json \
  --repo-root .
python3 tools/compile_native_render_asset.py \
  content-source/native_render/a1_native_course_60m/rorng_a1_native_course_60m.native.json \
  --repo-root . --validate-checked
python3 tests/tools/test_native_a1_course.py
```
