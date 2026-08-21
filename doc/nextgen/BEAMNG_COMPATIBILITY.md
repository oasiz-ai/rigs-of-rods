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

The first product admission path is now stricter than the metadata tool. Mod
cache discovery treats `*.jbeam` only as a hint, hashes the complete ZIP,
creates an immutable authenticated snapshot, decodes and resolves every
`slotType: "main"` candidate, and publishes the bundle atomically only when
all candidates fit the supported structural/hydro subset. Each root is exposed
as a virtual cache entry tied to the same physical ZIP, full SHA-256, exact
byte count, and case-exact root name; a virtual entry is never reopened as a
standalone RigDef file. Loading remints and verifies the snapshot before
resource-group creation, mounts those exact bytes through `EmbeddedZip`, and
rebuilds every root before publishing any `RigDef::Document`. Actor creation
then revalidates the current cache pointer, root, resource-group generation,
mounted archive pointer, immutable snapshot owner, SHA-256, size, and importer
receipt before reserving an actor ID or publishing network or graphics state.
An admitted JBeam ZIP is one cache transaction: ordinary sibling vehicle
entries are not published, and auto-executable OGRE material, GPU-program,
compositor, particle, overlay, font, or generic Ogre-script members reject the
package before resource-group creation. BeamNG Lua remains inert data and is
never invoked.

This closes product package discovery and source-authority wiring for the
allowlisted subset. A project-original structural/hydro package now also
executes that exact product path through cache discovery, immutable ZIP mount,
`RigDef` lowering, `ActorManager` spawn, and 120,000 fixed `0.5 ms` physics
steps. One-worker and eight-worker traces contain 120,000 records and compare
equal after the authenticated worker-count metadata difference; the hydro and
compression-only support beam each accept exactly 120,000 finite steps with no
latched fault. The support spring is active for 119,931 of those steps. Before
the first step, the product actor reports the exact six 20 kg nodes, sixteen
native beams (fourteen normal, one compression-only support, and the hydro),
five `NORMALTYPE` cab triangles admitted to the native collision-cab set, six
ground/static-contact-enabled nodes, zero explicit self-collision contacters,
120 kg total mass, and the expected translation/rotation-invariant
center-of-mass relation. The step-zero-only impact transaction then translates
the actor upward by exactly 2 m and gives every movable node a `-4 m/s`
vertical velocity. The product run records a 1.9985843151807785 m minimum
center-of-mass drop, a 7.3981828689575195 m/s peak center-of-mass speed, an
upward terrain-contact response, and zero broken beams. Both worker counts end
at state digest
`c77f90c5488f2cbba10135b33722fdea45ab33393583d3c3236a7fdb3b1fa579`.
The clean source revision is
`f8dc6298ac04f9d361624157ec22ce5a1032d483`; the executable SHA-256 is
`a6c7fbb878bc6e2013ae3abca7fb80110471945ad2e0626846c05c9d27c8d89f`
and the exact report SHA-256 is
`0c15d65bafe312b96527b6c612d5be8984bc72988be97143decc684b3d117758`.
The separate authenticated inter-actor gate then spawns two copies of that
same exact archive above the terrain with stable actor IDs, 0.01 m vertical
separation, and a 1 m/s closing speed. Its canonical 2,000-step trace records
343 external node-to-`NORMALTYPE`-cab keys across 299 steps (maximum 20 in one
step, first contact at step zero), a 7.234161376953125 m/s change in relative
vertical velocity, 0.6013336181640625 m maximum separation, and zero broken
beams. One-worker and eight-worker state digests match at every step after the
authenticated worker-count metadata difference. The executable SHA-256 is
`f0524e9dcd5b85b1b585c7e54e40a606fb2aaf935da656db080eb4612608ad88`
and the exact report SHA-256 is
`9fe46da17c90432f022097ceefdd5bc4367cad61242139d85e880001b716c6e2`.
This is not
evidence that a third-party vehicle spawned, drove, rendered correctly,
replayed, or synchronized over multiplayer.
Configuration selection (`.pc`), powertrain/electrics, visual resources, and
every unsupported active section remain fail-closed. Pressure wheels are
admitted only by the narrow J3 `Wheel2` approximation described below; this is
not pressure-tyre, brake, propulsion, spawn, or driveability evidence.
The native application integration test now executes the production cache
scanner over a generated ZIP, proves exact virtual-root metadata and idempotent
rescan, and proves that an uppercase hostile `.MATERIAL` sibling publishes no
cache entry. `tools/run_jbeam_spawn_soak.py` supplies the separate executable
spawn/physics gate and preserves its logs, exact trace files, executable/source
digests, and bounded telemetry. The importer/archive suite covers all current
JBeam stages, but no third-party package is bundled or required by public
tests.

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
- authored [`variables`][variables] range tables with numeric default/min/max
  values and retained tuning-menu metadata;
- exact array-valued `$=$components.path` insertions as structural table rows
  or complete recognized structural tables, including deep-merge overrides
  from selected parts;
- numeric variables and deterministic `$=` expressions;
- string and Boolean slot variables;
- `$prefix`, `$suffix`, and `$.name` namespace expansion;
- source spans for every part, section, default row, data row, and field.

The dependency-light expression-evaluator core now accepts a documented pure
scalar subset behind the mandatory `$=` prefix: finite decimal arithmetic,
comparisons, Lua-style `and`/`or`/`not`, Boolean and positive-integer-selector
`case` forms, string concatenation/length, numeric `abs`, `square`, `round`,
`floor`,
`ceil`, the clamped `smoothstep`/`smootherstep`/`smootheststep` family,
`frexp` mantissa, `modf` integral part, `rad`, `deg`, integer-exponent `pow`,
exact-significand `fmod`, subnormal-aware integer-exponent `ldexp`, `clamp`,
one-to-64-argument `min`/`max`, and exact `pi`/documented-`FLT_MAX` `huge`
constants, typed `$variables`, and flattened scalar
[`$components`][components] paths. Scalar-call arguments are eager and
numeric-only. `clamp` rejects reversed bounds, `square` rejects overflow,
`round` resolves exact halves away from zero, and all rounding and interpolation
operations use pinned IEEE-754 binary64 operation order rather than host Lua.
Missing variables evaluate to `nil`. Component row insertion is deliberately
not a general table-expression evaluator: the complete table entry must be one
exact component path, and a component-backed section must likewise be one exact
path. Expression bytes, tokens, recursion,
function arguments, deterministic work, strings, output, and environment size
are bounded. Non-finite input or output fails closed even under the game's
fast-math mode, and canonical values are independent of source spelling.

This core is not a Lua interpreter and exposes no host, filesystem, network,
clock, or random functions. Every function outside the two deterministic
`case` signatures and the eighteen-name deterministic numeric allowlist,
numeric-to-string concatenation, computed table-component operators, indexing,
and method calls remain unsupported. In particular, transcendental,
host-libm-dependent, and random functions stay fail-closed until their
cross-platform numeric and state contracts are versioned.

The functions above are an independent implementation of public format
behavior; they do not reuse BeamNG code or assets. `ParseJBeam` and
`JBeamPartResolver` retain authored
expression strings as inert source values so syntax and graph identity never
depend on execution. The J2 structural semantic pass now constructs a bounded
environment from the selected graph's active authored tuning defaults,
effective configuration/slot overrides, and deterministically merged scalar
component leaves. Active authored rows are collected in resolved-part preorder;
later duplicate rows are effective, `.pc` values override them globally, and
slot values override them for that child subtree. Declared `range` overrides
must remain numeric and inside the active authored min/max bounds. It
evaluates standalone variables, `$=` expressions, and `$.name` namespace
strings only for explicitly supported scalar node, beam, surface, and refNodes
fields before applying the field's required type.

Array-valued component leaves can supply a complete recognized structural
table or enter one through an exact row reference; nested scalar expressions
inside either form then use the normal bounded evaluator. Inserted rows are
fully measured before copying, whole tables use the same bounded normalizer,
and non-exact, missing, wrong-type, or over-budget references fail before
partial geometry is published. Expression-valued components and computed
table operations remain preserved with
diagnostics instead of entering the scalar environment. Unknown sections and
fields, including legacy `rails.id`, are never evaluated. Per-expression
limits are supplemented by aggregate evaluation, work, component-node,
component-depth, environment-count, environment-string, and structural
retained-byte limits.
An absent variable is a valid `nil` expression operand, but a final `nil` in a
field that requires a number, Boolean, or string fails that field closed with
its source span.

The local, opt-in FormulaCOUPE v0.9.7 audit found no scalar built-in calls in
its 84 `.jbeam` files. `FC-A7-01` resolution therefore does not depend on this
function slice; the broader function allowlist improves documented-format
coverage without turning that third-party archive into project content or a
public test dependency.

The [part/slot system][slots] is a recursive tree, not a flat list. `slotType:
"main"` identifies a root part. The resolver applies the chosen `.pc` parts and
variables, follows `slots` and `slots2`, propagates slot variables to
descendants, inventories selected authored `range` tables, applies components
and node transforms, and detects cycles, duplicate resolved names, missing
required parts, invalid tuning rows/overrides, and optional references.
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
refnode landmarks. The supported structural/hydro product importer now routes
its node positions, reference frame, triangles, and hydro-node joins through
this boundary. Cab normals, camera direction, wheel placement, general
rotations, forces outside the admitted hydro path, and inertia still require
their own conversion before those features can be enabled.

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

The bounded native node slice now parses all three documented switches with
their official defaults: `collision=true`, `selfCollision=false`, and
`staticCollision=true`. A row-level `collision=false` sets RoR's
no-ground-contact bit and prevents that node from being automatically admitted
to inter-vehicle point collision. Because BeamNG defines `collision` as the
highest-priority switch, this remains exact even if that disabled row retains
otherwise ineffective self/static values. With collision enabled, the adapter
admits only the default self/static mode: `selfCollision=true` fails because a
generic RoR contacter would incorrectly omit BeamNG's group-aware triangle
rules, and `staticCollision=false` fails because RoR cannot currently disable
terrain/static contact while preserving external dynamic contact. The adapter
does not silently approximate either mode.
The original six-node fixture proves a real downward drop and upward terrain
response through six ground-enabled nodes while retaining zero explicit
self-collision contacters. This is node-to-terrain execution
evidence only. The structural IR also recognizes the documented triangle
`NORMALTYPE` default and `NONCOLLIDABLE` value. `NORMALTYPE` lowers to a native
contact cab, `NONCOLLIDABLE` remains a visual/non-contact cab, and any other
active triangle type is rejected rather than approximated. This preserves
contact exclusion but does not implement BeamNG's anti-clip repulsion for
`NONCOLLIDABLE`; it is not full triangle-behavior parity. The current
single-actor gate proves that five authored default surfaces enter the native
collision-cab set while the default `selfCollision=false` produces zero
explicit self-contact nodes. The paired gate executes two actors in that state
and records the exact external contact-key stream plus finite collision
response. It closes the bounded default node-to-`NORMALTYPE`-triangle product
path, not self-collision, arbitrary topology/dynamic-contact completeness,
static-mesh collision semantics, ground models, or collision-force parity.

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
| `SUPPORT` | Native J2 compression-only response and `beamLongBound` extension break; coupled triangle/mesh break propagation remains unsupported. |
| `HYDRO` | Lower through the dedicated hydro contract below. |
| `ANISOTROPIC` | Preserve until separate compression/extension and transition behavior is native. |
| bounded, pressured, and L-beams | Preserve until dedicated J5 kernels exist. |

The [SUPPORT-beam contract][support-beams] is implemented through a generic
internal compression-only beam capability rather than a JBeam-specific spawner
branch. The importer preserves the finite nonnegative `beamLongBound` value
(default `1`) and keeps the geometric spawned length separate from the
precompressed activation length. Below that activation length the native force
loop applies the authored spring and damping; at or above it both are exactly
zero. Extension breaks only when
`current_length / spawned_length - 1 > beamLongBound`, so a zero bound is
preserved and breaks only after extension beyond the spawned length. Invalid
runtime state or counter exhaustion latches a fault and disables the beam
before force publication. Dependency-free hostiles cover equality boundaries,
zero bounds, extreme finite ratios, non-finite inputs, narrowing, conflicting
legacy flags, and unsupported source types. The authenticated product soak
then proves one such beam was actually constructed and accepted all 120,000
steps, including 119,931 compression-active steps, with zero faults and equal
one/eight-worker state traces. This closes the bounded response kernel and
source-to-runtime path, not BeamNG-wide deformation, break-group, triangle,
flexbody, or mesh-removal coupling.

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

The first advanced-structure semantic pass is a bounded inventory of
`hydros`, legacy `rails`, `rails2`, `slidenodes`, `thrusters`, and
`torsionbars`. It locks the official defaults and dependent defaults in this
documentation profile, including hydro input/rate behavior, rail cap/loop
flags, slidenode attachment flags, the thruster factor and `FLT_MAX` limit, and
torsion-bar secondary spring/damping inheritance. Exact source values,
modifier order, unknown fields, source spans, and disabled expressions remain
identity material. Static rail geometry can be classified as ready for a
future native adapter. A dedicated fail-closed admission step may now copy one
literal, diagnostic-free hydro row into the actual deterministic
`HydroActuatorConfig` response contract. It preserves the exact node pair,
input source, steering lock, factor/limit/scaling fields, and rate fields, then
reruns the response-kernel configuration validator before admission. Unknown
fields, expressions, invalid source IR, invalid limit ordering, or a row-local
diagnostic reject the admission transaction. This is configuration admission,
not runtime lowering: it does not execute electrics, resolve live inputs,
construct a beam, apply forces, create constraints, publish replay state, or
authorize vehicle spawning. Rails, slidenodes, thrusters, torsion bars, and
all other force-producing paths remain inventory-only.

A second, separately queried receipt admits only the physical standard-beam
properties that the existing RigDef construction boundary can represent
without legacy defaults. It resolves the documented spring, damping,
deformation, strength, and precompression defaults or exact row overrides;
normalizes `NORMAL`/`|NORMAL`; maps `FLT_MAX` to exact binary32 maximum; and
rejects negative, non-finite, subnormal, or overflowing binary32 values. Any
special beam type, long/short bound, or break-group behavior rejects because
this receipt cannot preserve those semantics. The two receipts deliberately
remain separate: even both being valid does not authorize input wiring,
physical construction, or spawning.

The dependency-free runtime carrier owns the admitted response state. It
validates configuration and initial length, advances the ratio kernel with a
checked `uint64` step counter, resolves rest length in binary64, verifies the
exact positive-normal binary32 value handed to the beam solver, and
permanently latches the first runtime fault without advancing state. The
internal adapter can now publish a complete plan set into one unpublished
`RigDef::Document`; `ActorSpawner` initializes that immutable plan on the
corresponding native hydro beam, and the force loop advances it from RoR's
steering-direction command. The product importer now wraps that document in an
opaque receipt for the exact immutable ZIP/root/resolved graph, and
`ActorManager` requires the same currently mounted snapshot immediately before
spawn construction. Save/replay authority, explicit source-electrics lineage,
and source-engine calibration remain open. The clean-room product spawn-soak
proves the admitted native carrier is constructed and advanced for every
requested step; it does not establish source-engine behavioral parity.

The first source-to-runtime plan now rebuilds the advanced and structural IRs
inside one transaction from the same resolved part graph. For a selected
hydro it requires both admissions above, a valid ref-frame-bearing structural
IR, at most 65,535 runtime nodes, two unique node-name joins within the
`uint16` runtime index range, the exact documented `steering_input` route, a
finite nondegenerate geometric length, finite precompression, and successful
runtime-state initialization including binary32 rest-length proof. Custom
electrics remain unsupported. The plan deliberately carries no package
authority itself; the product importer attaches it to an unpublished RigDef
document and separately retains the opaque archive/root authority receipt.
Only the outer current-cache/current-mounted-snapshot transaction may authorize
spawn. The plan alone remains an internal value rather than a content receipt.

Vehicle-level planning now builds those two IR views once and emits every
hydro plan in stable source order under an all-or-none transaction. A failed
advanced/structural build, allocation, construction step, or single row clears
the public plan vector and reports the exact rejected row and plan code. An
exact graph with no hydros validly yields an empty admitted set. The internal
RigDef adapter likewise publishes every plan or no document, preventing a
later importer from silently dropping one unsupported actuator. The product
loader now supplies package authority and the importer call site, while actor
creation revalidates that authority before publication. The project-original
spawn-soak closes finite product spawn, bounded node-to-terrain impact,
settle, and hydro execution for this one structural/hydro fixture. Replay
qualification, source-electrics identity, representative-vehicle and full
triangle/self/static collision coverage, and third-party vehicle evidence
remain open.

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

The pressure-wheel source pass remains the duplicate-preserving inventory for
all documented rows, `scale*` process modifiers, controller/powertrain
sections, and Lua. A second all-or-none J3 transaction may now lower only a
strict literal subset to the existing native `RigDef::Wheel2` generator. It
requires one exact resolved graph, unique wheels, a centred tyre, `hasTire`, no
stabilizer node, 10-through-20 even rays, zero source brake/parking/propulsion,
external node collision with generated triangle collision disabled, explicit
positive tyre/hub node weights and spring/damping values, finite nondegenerate
axle geometry, an off-axis reaction arm, and authored tyre/hub widths that
match the exact binary32 axle length. Unknown fields, pressure/friction/brake
behavior, controllers, powertrain, Lua, scaling sections, nonzero offset, and
every other collision mode reject the whole plan.

The native adapter then revalidates every plan against the final transformed
binary32 structural geometry and combined structural/hydro/generated
ActorSpawner node/beam ceilings before allocating a `RigDef::Document`. It
publishes ordinary unpropelled, unbraked `Wheel2` rows with no BeamNG-specific
runtime branch. The opaque importer receipt version 2 retains the canonical
plan SHA-256, plan count, and declared ignored-semantics mask. Authenticated ZIP
tests prove admitted publication and fail-closed nonzero-offset rejection; the
full macOS product target compiles and links this path. No actor spawn, static
load, rolling, contact, pressure, steering, braking, propulsion, settle,
driveability, replay, or third-party vehicle evidence exists yet, so the J3
behavioral gate remains open.

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
- [Tuning variables][variables]
- [Coordinate systems][coordinates]
- [RefNodes][refnodes]
- [Nodes][nodes]
- [Beams][beams]
- [Support beams][support-beams]
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
[support-beams]: https://documentation.beamng.com/modding/vehicle/sections/beams/support/
[thrusters]: https://documentation.beamng.com/modding/vehicle/sections/thrusters/
[torsionbars]: https://documentation.beamng.com/modding/vehicle/sections/torsionbars/
[triangles]: https://documentation.beamng.com/modding/vehicle/sections/triangles/
[variables]: https://documentation.beamng.com/modding/vehicle/sections/variables/
[vehicle-controller]: https://documentation.beamng.com/modding/vehicle/vehicle_system/controller/main/vehiclecontroller/
[wheels]: https://documentation.beamng.com/modding/vehicle/sections/wheels/
