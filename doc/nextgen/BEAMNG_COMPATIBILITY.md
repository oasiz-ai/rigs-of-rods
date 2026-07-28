# BeamNG.drive Vehicle-Format Compatibility Specification

This document converts BeamNG's published vehicle-modding documentation into
an independently implemented, testable import contract for Rigs of Rods. It
does not describe binary compatibility, reuse BeamNG code or stock assets, or
promise identical results from different physics engines.

The initial documentation profile is:

```text
profile: beamng-docs-0.38.5.0-2026-07-27
scope: user-supplied vehicle packages
execution: data only; imported Lua is never executed
```

The profile name records the version identified by BeamNG's
[JBeam section catalog][sections] and the date on which the linked documentation
was reviewed. BeamNG states that its documentation is incomplete and under
development, so a newer importer must introduce a new profile rather than
silently changing an existing conversion.

## Competitive hypotheses

The project is deliberately aiming beyond format compatibility. The following
are informed engineering bets about where this architecture can be better than
existing simulators. They guide design now, but remain hypotheses until a public
like-for-like fixture produces evidence.

| Hypothesis | Why the design can win | Evidence required |
| --- | --- | --- |
| Multithreaded replay is more reproducible | Stable contact keys, per-task buffers, ordered reductions, counter-based noise, and per-step state hashes make scheduling observable and removable. | Thirty one-worker and eight-worker runs with identical ordered hashes; equivalent external replay telemetry where available. |
| Bad content fails more safely | Finite guards, energy-bounded damping, hostile-input quotas, source-span diagnostics, and no imported code execution establish explicit failure boundaries. | Fuzz/sanitizer corpus, 120,000-step soaks, and zero silent drops in the compatibility report. |
| Soft-body materials are easier to calibrate | Versioned elastic/plastic/damage laws expose yield, hardening, plastic strain, and fracture energy instead of relying only on opaque tuning. | Published tension, compression, cyclic, and impact curves with step-sensitivity and energy accounting. |
| Mod interoperability is more transparent | A normalized IR preserves unknown fields and assigns every feature a native, approximate, disabled, unsupported, or rejected status. | Canonical report stability across platforms and complete source-field accounting for representative packages. |
| Visual deformation scales better | CPU-authoritative nodes plus GPU vertex reconstruction avoid uploading the full deformed mesh every frame while retaining a CPU oracle. | Position/normal error bounds and frame-time comparisons at 250,000 or more visible vertices. |
| Cross-platform behavior is easier to verify | Dependency-free kernels, declared floating-point modes, deterministic fixtures, and native Windows/Linux/macOS CI make platform drift a first-class result. | Compiler/platform matrix with recorded first-divergence steps and declared numeric tolerances. |
| Save, replay, and imported-config identity are stronger | Source hash, slot tree, variables, importer schema, RNG streams, and dynamic state are part of one versioned identity. | Save/load continuation and network/replay hashes that survive pause, resume, and worker-count changes. |

An internal gate may justify wording such as "more deterministic in our
published fixture." A general statement that the simulator is better requires
the same vehicle intent, inputs, versions, hardware, capture method, and
published raw measurements on both systems. When exact external state is not
available, the comparison is limited to observable telemetry and is labeled as
such.

## Compatibility vocabulary

Every package, selected configuration, and discovered feature receives one of
these statuses:

| Status | Meaning |
| --- | --- |
| `native` | RoR implements the documented state, units, inputs, and behavior, and the conformance fixture passes. |
| `approximated` | A declared, measured mapping exists, but one or more documented semantics differ. |
| `preserved-but-disabled` | The importer retains the source data and origin in its IR but does not activate it. |
| `unsupported` | The feature is recognized but has no safe mapping. |
| `rejected` | The input is unsafe, malformed, ambiguous, or requires a missing dependency. |

No field may disappear silently. An import reaches the lowest tier of any
required feature in the selected configuration.

## Package and identity contract

BeamNG's [mod-packing documentation][packing] places supported roots such as
`vehicles/`, `levels/`, `art/`, `assets/`, `lua/`, `scripts/`, and `ui/`
directly at the ZIP root. The first implementation indexes only vehicle
packages, while reporting other roots without loading them.

The canonical package identity contains:

```text
source SHA-256
normalized sorted path manifest
declared documentation profile
main part
selected .pc configuration
resolved slot tree and variables
importer schema and implementation version
import options
```

Archive enumeration order, filesystem case behavior, and worker count must not
change this identity. Absolute paths, parent traversal, normalized duplicates,
case-fold collisions, symlinks, encrypted entries, unsafe compression ratios,
and quota violations are rejected before extraction. Importing performs no
network access and never executes package content.

The first J0 container boundary is implemented as a bounded, metadata-only
classic ZIP index pinned to
[PKWARE APPNOTE 6.3.10](https://pkware.cachefly.net/webdocs/APPNOTE/APPNOTE-6.3.10.TXT).
It validates the EOCD,
central and local records, optional data descriptors, record coverage, file
types, and package-manifest policy without decompressing payloads. ZIP64,
spanned, encrypted, self-extracting, symlink/special-file, overlapping,
alternate-name/stream, and hidden-gap variants fail with distinct diagnostics.
Compression methods are recorded but are not decoder claims. Use
`ror_beamng_zip_index PACKAGE.zip` to emit a compact JSON validity summary; the
tool reads an explicitly supplied local archive only.

## JBeam syntax and resolution

The [JBeam syntax][jbeam-syntax] is JSON-derived but is not strict JSON. The
front end must support and test:

- case-sensitive keys and identifiers;
- line and block comments;
- optional commas;
- top-level named part dictionaries;
- table sections with a header row;
- dictionary rows that change inherited defaults;
- row-local dictionaries that override inherited defaults;
- numeric variables and deterministic `$=` expressions;
- string and Boolean slot variables;
- `$prefix`, `$suffix`, and `$.name` namespace expansion;
- source spans for every part, section, default row, data row, and field.

The dependency-light expression-evaluator core now accepts a documented pure
scalar subset behind the mandatory `$=` prefix: finite decimal arithmetic,
comparisons, Lua-style `and`/`or`/`not`, the Boolean three-argument `case`
form, string concatenation/length, typed `$variables`, and flattened scalar
[`$components`][components] paths. Missing variables evaluate to `nil`.
Expression bytes, tokens, recursion, deterministic work, strings, output, and
environment size are bounded. Non-finite input or output fails closed even
under the game's fast-math mode, and canonical values are independent of
source spelling.

This core is not a Lua interpreter and exposes no host, filesystem, network,
clock, or random functions. Numeric-selector `case`, other built-in functions,
numeric-to-string concatenation, component tables, indexing, and method calls
remain unsupported. `ParseJBeam` and `JBeamPartResolver` retain authored
expression strings as inert source values so syntax and graph identity never
depend on execution. The J2 structural semantic pass now constructs a bounded
environment from each resolved part's effective configuration/slot variables
and the selected graph's deterministically merged scalar component leaves. It
evaluates standalone variables, `$=` expressions, and `$.name` namespace
strings only for explicitly supported scalar node, beam, surface, and refNodes
fields before applying the field's required type.

Table-valued and expression-valued components are preserved with diagnostics
instead of entering the scalar environment. Unknown sections and fields,
including legacy `rails.id`, are never evaluated. Per-expression limits are
supplemented by aggregate evaluation, work, component-node, component-depth,
environment-count, environment-string, and structural retained-byte limits.
An absent variable is a valid `nil` expression operand, but a final `nil` in a
field that requires a number, Boolean, or string fails that field closed with
its source span.

The [part/slot system][slots] is a recursive tree, not a flat list. `slotType:
"main"` identifies a root part. The resolver applies the chosen `.pc` parts and
variables, follows `slots` and `slots2`, propagates slot variables to
descendants, applies components and node transforms, and detects cycles,
duplicate resolved names, missing required parts, and optional references.
Namespace variables were added in BeamNG 0.38 and slot variables in 0.32, so
their availability is part of the declared documentation profile.

## Units and coordinate frames

BeamNG documents SI units and a Z-up vehicle frame:

| BeamNG axis | Direction | RoR axis |
| --- | --- | --- |
| `+X` | left | `+Z` |
| `+Y` | backward | `+X` |
| `+Z` | up | `+Y` |

The position transform is therefore
`(x_ror, y_ror, z_ror) = (y_beamng, z_beamng, x_beamng)`. It is a
handedness-preserving cyclic permutation. Dependency-free golden tests lock its
basis, inverse, metric and cross-product preservation, triangle winding, and
refnode landmarks. Runtime import remains disabled until nodes, cab normals,
camera direction, wheel placement, rotations, forces, and inertia all route
through this one tested boundary.

One aligned [refNodes][refnodes] set is required. `ref`, `back`, `left`, and
`up` define the vehicle frame; `leftCorner` and `rightCorner` identify its front
corners but do not redefine the axes. A missing or degenerate required frame is
rejected rather than inferred from a mesh. The structural validator also
requires `back` on `+Y`, `left` on `+X`, and `up` on `+Z`; reversed, mirrored,
or skewed frames fail before coordinate lowering.

Props use their own three-node frame: local `+X` is `idRef -> idX`, local `+Y`
is `idRef -> idY`, and local `+Z` is their cross product. This frame is converted
through the same basis transform as the physics skeleton.

## Structural and collision behavior

### Nodes

[Nodes][nodes] are dimensionless point masses. Coordinates are meters and
`nodeWeight` is kilograms. Node material controls effects such as sound and
particles rather than mass behavior.

The documented collision participants are nodes and vehicle triangles:

- nodes collide with terrain heightmaps and static world geometry;
- dynamic vehicle collision is node-to-triangle;
- self-collision and external vehicle collision are distinct;
- dynamic collision has priority over static and heightmap collision;
- beams do not collide.

J2 therefore requires first-class authored base mass per imported node. RoR's
legacy whole-vehicle dry-mass redistribution and minimum-mass pass cannot be
used for a `native` result.

### Beams

[Beams][beams] are massless spring/damper links with deformation and breaking.
The documented core parameters include endpoints, spring, damping, deformation,
strength, precompression, break/deform groups, and mesh/triangle-breaking
controls.

The initial type matrix is:

| Type | Import policy |
| --- | --- |
| `NORMAL` | J2 after spring, damping, deformation, strength, and precompression conformance tests. |
| `SUPPORT` | J2 approximation until compression-only and extension-break behavior match. |
| `HYDRO` | Lower through the dedicated hydro contract below. |
| `ANISOTROPIC` | Preserve until separate compression/extension and transition behavior is native. |
| bounded, pressured, and L-beams | Preserve until dedicated J5 kernels exist. |

When a beam breaks, BeamNG can disable adjacent or grouped triangle collision
and aero and remove affected flexbody polygons. RoR must not advertise native
breakage until these coupled topology changes are implemented.

### Triangles and quads

[Triangles][triangles] use three counter-clockwise node references. They:

- collide with nodes, not other triangles or static world geometry;
- apply aerodynamic drag and lift forces to adjacent nodes;
- calculate lift from angle of attack and reduce it beyond a stall angle;
- may define pressure volumes;
- may use break groups, ground models, and external collision bias.

Topology-only import is J2. Collision side/bias, self-collision grouping,
aerodynamics, stall, pressure, ground-model friction, and break propagation are
independent capabilities. Quads lower deterministically to two triangles with a
declared diagonal and preserved exterior winding.

### Hydros, rails, thrusters, and torsion

[Hydros][hydros] are variable-length beams. For normalized input `u`, factor
`f`, and initial length `L0`, the documentation examples imply the target:

```text
L_target = L0 * (1 + f * u)
```

The conformance fixture covers negative, zero, and positive input; limits;
centering; rate behavior; steering lock; and all inherited beam properties.

The first dependency-free actuator slice locks a versioned interpretation:
`inputInLimit`, `inputCenter`, and `inputOutLimit` map piecewise to normalized
travel `-1`, `0`, and `+1`; `factor` uses `1 + factor * input` and takes
precedence over `inLimit`, `outLimit`, and `inputFactor`; and in/out/auto-center
rates bound ratio change per second without overshoot. This is a safe native
RoR state contract, not yet a parity claim. A package remains
`preserved-but-disabled` until table lowering and behavioral comparison show
that its relied-upon source behavior fits that declared interpretation.

The first advanced-structure semantic pass is an inert, bounded inventory of
`hydros`, legacy `rails`, `rails2`, `slidenodes`, `thrusters`, and
`torsionbars`. It locks the official defaults and dependent defaults in this
documentation profile, including hydro input/rate behavior, rail cap/loop
flags, slidenode attachment flags, the thruster factor and `FLT_MAX` limit, and
torsion-bar secondary spring/damping inheritance. Exact source values,
modifier order, unknown fields, source spans, and disabled expressions remain
identity material. Static rail geometry can be classified as ready for a
future native adapter, but all actuated or force-producing rows remain
inventory-only. This pass does not execute electrics, apply forces, create
constraints, or authorize runtime lowering.

Rails/slidenodes constrain a named node to a named node-chain rail. Native
status requires matching attachment distance, spring/strength, tolerance,
caps/loops, and break behavior.

Thrusters apply force along a two-node direction and are used for systems such
as JATO. Imported thruster input remains disabled until force direction,
magnitude, control input, energy use, and break behavior have a native contract.

[Torsion bars][torsionbars] use four nodes defining two lever ends and an axis.
They can be anisotropic and can break when connecting structure breaks. They
are not approximated as axial beams.

## Pressure wheels, friction, and brakes

The documented [pressureWheel][wheels] is generated from axle/reference nodes
as a pressured node/beam/triangle tyre. RoR generated wheels may provide a J3
structural approximation, but native status requires:

- separate hub, tread, sidewall, and reinforcement topology;
- pressure-volume state and leak/deflation behavior;
- radial and lateral spring/damping behavior;
- static-to-sliding Stribeck friction transition;
- per-contact-node load sensitivity;
- tread interaction with surface roughness;
- service and parking torque;
- progressive/digressive brake input;
- rotor/pad material, thermal mass, heating, cooling, fade, and damage;
- dynamic node storage rather than RoR's fixed generated-wheel array limits.

For profile `beamng-docs-0.38.5.0-2026-07-27`, the required table fields are
`name`, `hubGroup`, `group`, `node1:`, `node2:`, `nodeS`, `nodeArm`, and
`wheelDir`. `wheelDir` accepts `1` or `-1` despite being typed as a string in
the page. `numRays` must be even; the documentation recommends 10 through 20
and calls 16 typical. Geometry fields such as radius, hub radius, widths,
offset, and `hasTire` have no published defaults, so an importer must not
invent them.

The same profile records the exact published defaults that are meaningful to
inventory: `stribeckExponent=1.75`, `treadCoef=1`, `softnessCoef=0.6`,
reinforcement/support beams disabled, support-beam sidewall ratio `0.9`,
triangle collision flags disabled, `dragCoef=100`, and `skinDragCoef=0`.
Published brake defaults include zero service and parking torque,
`brakeSpring=10 Nm/rad`, thermals disabled, `0.35 m` diameter, `10 kg` mass,
vented-disc/steel/basic material choices, split coefficients of one, ABS
disabled, target slip `0.18`, `100 Hz` update rate, and `0.04 s` in/out delay.
These values are data-profile facts, not evidence that RoR's wheel or brake
solver behaves equivalently.

The page has type/text ambiguities (`wheelDir`, `enableABS`,
`hubcapNodeMaterial`, and whether `tireWeight` is distributed over tyre or hub
nodes). Ambiguous fields are preserved with a profile diagnostic; they are not
lowered by guessing.

The tyre friction curve and brake thermals are behavioral systems, not metadata
that may be copied into a report and called supported. J3 reports the fields
that its approximation ignores; J5 validates loaded radius, vertical stiffness,
longitudinal/lateral slip, aligning behavior, heat energy, fade, and recovery.

Before any J3 wheel allocation, admission enforces an even `numRays` in the
documented 10-through-20 range, no more than 64 generated wheels, and the
expanded node/beam/triangle budgets. RoR's existing `Wheel2` changes authored
width/mass/offset and reaction semantics and its tyre-pressure control rewrites
spring stiffness rather than maintaining a pressure-volume state. It therefore
cannot be advertised as native. Unequal per-wheel brake torque, separate
brake/drivetrain reaction arms, fractional parking-brake input, and per-wheel
ABS remain disabled until dedicated native systems exist.

The first pressure-wheel implementation is deliberately an inventory boundary,
not a wheel generator. It retains duplicate-preserving source tables,
source-order `scale*` process modifiers, controller/powertrain sections, and Lua
as inert data; validates the required literal geometry and exact documented
field families; records topology reservations for a possible RoR approximation;
and labels every accepted row `inventory-only-never-lower`. Immutable
part/row/field/value-depth/work/byte/diagnostic/canonical-output ceilings apply
even when a caller requests larger limits. Its canonical identity includes the
documentation profile and is independent of archive order and container
capacity. No pressure, friction, brake, powertrain, controller, or generated
topology behavior is activated by this pass.

## Powertrain, electrics, and controllers

The official section catalog identifies a `powertrain` section, but the current
English documentation profile does not publish a linked powertrain section or
device schema; the obvious section and vehicle-system URLs return no page.
The vehicle-controller documentation is also explicitly work in progress.
Accordingly, this profile inventories and preserves powertrain data but cannot
claim exact BeamNG device-graph lowering or infer undocumented defaults.

A future project-native driveability profile may declare a simple path:

```text
combustion engine -> clutch -> gearbox -> shaft/differential -> driven wheels
```

That would be an explicitly versioned RoR approximation with its own schema and
calibration fixtures, not BeamNG compatibility. Split paths, multiple motors,
EV/hybrid storage, CVTs, converters, disconnects, rangeboxes, locking
strategies, thermals, and damage remain preserved-but-disabled until published
source behavior and corresponding native graph devices exist.

The [vehicle controller][vehicle-controller] manages input, shift logic, and
electrics. Its public page documents shifting to a gear index, shifting up/down,
starter and ignition switches, and manual clutch-ratio polarity. The documented
[electrics values][electrics-values] expose throttle, brake, clutch, parking
brake, and steering inputs. The custom [electrics section][electrics] can
evaluate Lua-like expressions and smoothing.

Imported controllers and electrics expressions are never executed. Each useful
behavior is reimplemented as an allowlisted native controller with declared
inputs, outputs, units, update rate, reset/save/replay state, and deterministic
tests. The initial safe controller vocabulary is throttle/brake/clutch/
parking-brake in `[0,1]`, steering in `[-1,1]`, gear-index/up/down, starter, and
ignition. Arcade/automatic assists and fractional-to-boolean parking-brake
conversion remain disabled unless a separately named approximation defines
them.

## Meshes, deformation, materials, and props

[Flexbodies][flexbodies] bind a named DAE mesh to one or more node groups.
BeamNG maps each vertex to nearby eligible nodes and deforms it with them.
Documented flexbody transforms use position in meters, scale, and intrinsic
Euler rotation in `+Z, +X, +Y` order. Deform groups can switch a base material
to a damaged material after associated beam deformation or breakage.

J4 separately gates:

- Collada scene parsing and coordinate conversion;
- object/material name isolation per imported vehicle;
- vertex-to-node candidate selection and maximum-distance behavior;
- normal/tangent deformation;
- mesh breakage;
- flexbody position, intrinsic rotation, and scale;
- damage-group material switching.

BeamNG [`*.materials.json`][materials] keys and array shapes are retained,
including unknown fields. `mapTo` connects a material definition to the DAE
slot. The translator maps documented metal/rough PBR base color, normal,
metallic, roughness, ambient occlusion, opacity, emissive, palette, and clear
coat inputs to V1; missing inputs get an explicit diagnostic and visible
placeholder.

Both direct root fields and the legacy four-entry `Stages` array occur in
vehicle material files; neither shape may be flattened or silently rewritten
by the inventory. BeamNG's [Texture Cooker][texture-cooker] keeps material
references pointed at source PNG names while packaged mods commonly contain
only the corresponding cooked DDS files. A same-path, same-case
`.color.png`/`.normal.png`/`.data.png` reference with its documented local DDS
substitute is therefore reported as `local-cooked-dds`, not missing. The
importer never invokes BeamNG's cooker and never guesses across a case
mismatch, package boundary, or unsupported suffix. Color inputs are sRGB;
normal, roughness, metallic, opacity, AO, clear-coat, height, and other data
inputs remain linear, and documented normal maps use OpenGL Y+ tangent space.

[Glow maps][glowmaps] select material states from an evaluated input:

| Value | State |
| --- | --- |
| `<= 0.0001` | `off` |
| `> 0.0001` and `< 0.5` | `on` |
| `>= 0.5` | `on_intense`, or `on` when no intense material exists |

Only native electrics values may drive that state. [Props][props] remain rigid,
follow their `idRef`/`idX`/`idY` three-node frame, and may translate/rotate from
allowlisted native inputs; authored prop rotations use the documented intrinsic
`-X, -Z, -Y` order. [External and chase cameras][cameras] are refnode-relative,
whereas internal cameras are physical camera nodes connected by six beams and
may carry a secondary reference frame. Those distinctions, camera type/FOV,
offsets, transforms, and source spans must survive inventory before a native
camera adapter is enabled. Sounds have a separate resource and behavior gate;
discovering any of these sections does not imply support.

## Capability priority from the official catalog

BeamNG's section page publishes occurrence counts for vanilla content in
0.38.5.0. Those counts guide parser priority, not implementation claims:

1. information/slot types, flexbodies, beams, nodes, triangles, and slots;
2. pressure wheels, variables, props, controllers, and powertrain;
3. torsion bars, slidenodes/rails, components, glow maps, and refNodes;
4. specialized controllers, energy storage, forced induction, couplers,
   thrusters, airbags, and vehicle-specific systems.

The parser inventories every section name even when the semantic validator does
not know it. Unknown sections are preserved and reported with their source span.

## Conformance and drift gates

Public tests use original, minimal fixtures written for this project. A
third-party mod may be used only as an explicit local acceptance input.

Required fixture families:

- package-path normalization and hostile ZIP cases;
- comments, optional commas, defaults, malformed tables, and source locations;
- main/slot recursion, namespace variables, `.pc` selection, cycles, and missing
  optional/required references;
- basis, refNodes, prop frames, rotations, triangle winding, and units;
- node mass, beam load cycles, deformation, fracture, and break propagation;
- node/triangle collision, aero/stall, pressure volumes, and ground models;
- hydro, rail, torsion, and thruster input/state behavior;
- pressure-tyre load/slip/heat behavior;
- simple and branched powertrain graphs plus controller reset/replay;
- DAE/material/flexbody/glow-map translation and resource namespacing.

Every fixture records its documentation-profile ID. A documentation refresh
produces a reviewed diff of field names, defaults, units, version annotations,
and behavior text. Existing profiles and golden results remain reproducible.

## Official sources

- [JBeam section catalog][sections]
- [JBeam syntax][jbeam-syntax]
- [Components][components]
- [Mod packing][packing]
- [Slots and slot variables][slots]
- [Coordinate systems][coordinates]
- [RefNodes][refnodes]
- [Nodes][nodes]
- [Beams][beams]
- [Triangles][triangles]
- [Hydros][hydros]
- [Rails and slidenodes][rails]
- [Thrusters][thrusters]
- [Torsion bars][torsionbars]
- [Pressure wheels][wheels]
- [Vehicle controller][vehicle-controller]
- [Vehicle-system electrics values][electrics-values]
- [Electrics][electrics]
- [Flexbodies][flexbodies]
- [Props][props]
- [Cameras][cameras]
- [Glow maps][glowmaps]
- [Materials JSON][materials]
- [Texture Cooker][texture-cooker]

[beams]: https://documentation.beamng.com/modding/vehicle/sections/beams/
[cameras]: https://documentation.beamng.com/modding/vehicle/sections/camera/
[components]: https://documentation.beamng.com/modding/vehicle/sections/components/
[coordinates]: https://documentation.beamng.com/modding/vehicle/coordinate_systems/
[electrics]: https://documentation.beamng.com/modding/vehicle/sections/electrics/
[electrics-values]: https://documentation.beamng.com/modding/vehicle/vehicle_system/electrics/
[flexbodies]: https://documentation.beamng.com/modding/vehicle/sections/flexbodies/
[glowmaps]: https://documentation.beamng.com/modding/vehicle/sections/glowmaps/
[hydros]: https://documentation.beamng.com/modding/vehicle/sections/hydros/
[jbeam-syntax]: https://documentation.beamng.com/modding/vehicle/intro_jbeam/jbeamsyntax/
[materials]: https://documentation.beamng.com/modding/file_formats/materials/
[texture-cooker]: https://documentation.beamng.com/modding/materials/texture_cooker/
[nodes]: https://documentation.beamng.com/modding/vehicle/sections/nodes/
[packing]: https://documentation.beamng.com/modding/mod-support/mod_packing/
[props]: https://documentation.beamng.com/modding/vehicle/sections/props/
[rails]: https://documentation.beamng.com/modding/vehicle/sections/rails/
[refnodes]: https://documentation.beamng.com/modding/vehicle/sections/refnodes/
[sections]: https://documentation.beamng.com/modding/vehicle/sections/
[slots]: https://documentation.beamng.com/modding/vehicle/sections/slots/
[thrusters]: https://documentation.beamng.com/modding/vehicle/sections/thrusters/
[torsionbars]: https://documentation.beamng.com/modding/vehicle/sections/torsionbars/
[triangles]: https://documentation.beamng.com/modding/vehicle/sections/triangles/
[vehicle-controller]: https://documentation.beamng.com/modding/vehicle/vehicle_system/controller/main/vehiclecontroller/
[wheels]: https://documentation.beamng.com/modding/vehicle/sections/wheels/
