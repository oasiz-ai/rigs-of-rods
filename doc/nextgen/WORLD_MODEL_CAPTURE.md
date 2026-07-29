# Native World-Model Capture Contract

Status: experimental schema `1.0`

This contract defines the RoR-native source of truth for synchronized gameplay
episodes. It is intentionally upstream of Beam Cloud: the game creates and
validates a complete local artifact first; upload and training ingestion may
only consume that completed artifact.

## Timing and transaction boundary

Physics remains fixed at `1/2000 s`. Public observations are emitted at exactly
48 Hz using the cumulative boundary

```text
C(n) = floor(n * 125 / 3)
```

so successive transitions advance `41, 42, 42` physics steps. Wall-clock time
never chooses a boundary. An observation transaction is:

```text
capture issued/resolved input
  -> apply controls at each fixed-step start
  -> advance exactly C(n+1)-C(n) steps
  -> join all physics work
  -> copy immutable post-physics telemetry
  -> update graphics from that boundary
  -> render one UI-free RGB8 frame
  -> append the observation and transition records
```

Observation zero is the post-reset baseline at completed step zero. Transition
`n` links observation `n` to observation `n+1`.

## Record contract

Telemetry chunks contain length-delimited, CRC32C-protected records.

| Type | Schema | Meaning |
|---:|---|---|
| 1 | `org.rigsofrods.worldmodel.observation@1.0` | One immutable post-physics observation |
| 2 | `org.rigsofrods.worldmodel.transition@1.0` | Input lineage and outcome linking two observations |
| 3 | reserved | Applied-control trace or provenance extension |

RGB chunks contain:

| Type | Encoding | Meaning |
|---:|---|---|
| 1 | tightly packed `RGB8` | Canonical lossless driver-camera pixels |
| 2 | PNG | Optional lossless derivative |

JPEG is not a canonical schema-1 source. Every observation that has pixels
records `width`, `height`, `row_stride = width * 3`, `pixel_format = rgb8`,
`color_space = srgb`, `row_origin = top-left`, `rgb_record_id`, and the
SHA-256 of the uncompressed bytes. The dedicated render texture has hardware
sRGB conversion enabled; render-system-native bottom-left readback is
explicitly row-flipped before hashing and serialization.

An observation records at minimum:

- episode, observation, frame, and stable target IDs;
- nominal time `n/48`;
- completed physics boundary `C(n)` and previous boundary;
- exact interval substep count;
- state, camera/calibration, render, and validity metadata;
- deterministic state and raw-RGB hashes.

A transition records at minimum:

- source and target observation IDs;
- the half-open effective physics-step range;
- raw device samples when applicable;
- issued, resolved, and final applied controls;
- stable source/profile/control IDs and override ancestry;
- contact summaries, events, task outcome, and terminal/reset markers.

JSON payloads are canonical strict UTF-8: duplicate keys, non-finite numbers,
implicit NaNs, and unspecified units or coordinate frames are rejected.

## Runtime integration boundary

The capture session reaches live physics through this explicit adapter chain:

```text
ActorManagerFixedStepRuntime
  -> RuntimeCaptureBackend
  -> typed RuntimeCaptureProvider
  -> EpisodeCaptureSink
```

`ActorManagerFixedStepRuntime` delegates exact-step advancement and joining to
`ActorManager`. `RuntimeCaptureBackend` enforces begin/advance/join/capture
ordering and forwards one applied-control callback at every solver-step start.
The game-owned `RuntimeCaptureProvider` is responsible for copying the resolved
input lineage and immutable post-join state, updating graphics from that exact
boundary, and returning the UI-free RGB8 frame.

`RoRRuntimeCaptureProvider` is the production implementation for schema-1
ground vehicles. It reads raw/issued inputs at transition begin, observes the
canonical resolved Actor/Engine controls immediately before the exact-step
batch, observes applied controls at every 2 kHz step start, and captures
post-join vehicle, engine, terrain, camera, contact and deterministic-state
telemetry. The session owns the ActorManager scheduler through an opaque token,
joins before every boundary read, and rejects a stale manager/player binding.
It performs a complete single-actor graphics refresh using the exact authored
transition duration and renders the deterministic `driver/main` cinecam-0 pose
into a dedicated OGRE render texture whose viewport has overlays disabled;
mutable display camera/user-look state, ImGui, and dashboard overlays are
therefore not part of the canonical RGB bytes. Schema 1 fixes perspective
projection to a 60-degree vertical field of view with 0.1 m/2000 m near/far
planes in `ror.world.rh-y-up`.

`InspectCurrentRoRRuntimeResourceIdentity()` hashes canonical manifests of the
loaded vehicle and terrain resource-group bytes. The provider derives
content-addressed `ror.vehicle/<sha256>` and `ror.terrain/<sha256>` identifiers
and rejects caller metadata that does not exactly match those live bytes.
`CreateCurrentRoRWorldModelRuntime()` is the minimal game binding for the
currently loaded ActorManager and player Actor. Episode reset, provenance,
rights, output path and capture lifecycle remain explicit caller-owned policy.
`InspectCaptureDescriptor()` supplies the provider-authored ordered control
surface plus controller and camera profile hashes; live activation compares
that surface to its requested schema policy and copies those identities into
provenance instead of maintaining a second set of profile constants.

Schema 1 deliberately reports `maximum_penetration_m = 0`: RoR retains the
resolved per-node contact force but not a reliable post-resolution penetration
depth. Its canonical state digest includes all live Actor/node/beam state and
surface-contact counts. The optional deterministic trace writer's transient
inter-actor contact-key buffer is not shared with this provider and is not
claimed by the digest; target-vehicle contact begin/end events and summaries
remain present as separate typed telemetry.

## Crash-safe artifact

The writer uses:

```text
episode-<uuid>.partial/
  manifest.open.json
  provenance.json
  chunks/
    chunk-000000.bin
  rgb/
    chunk-000000.bin
  checksums.sha256
  manifest.json
  COMPLETE.json
```

Committed chunks are append-only. A chunk is written to a temporary path,
flushed, `fsync`ed, renamed, and followed by a directory `fsync`. The sealed
manifest and completion marker are written last. Only after validation does
the writer rename the directory to remove `.partial`.

`provenance.json` is canonical and its SHA-256 is sealed into
`manifest.json`. It authenticates the deterministic root/reset seeds; engine
commit and branch; build, OS, GPU, driver, and configuration identity; vehicle,
terrain, controller, camera, and reset-state content hashes; matrix order,
coordinate frame, color space, pixel format, and the fixed 2000/48 Hz rates.
The same object is also the dataset rights boundary: the rights-manifest hash,
data source, participant-release identifier, and allowed-use identifier are
mandatory. Unknown or unapproved rights metadata faults the episode instead of
silently widening its permitted use.

The integrity verifier rejects partial directories, temporary files, missing
artifacts, non-monotonic record IDs, checksum mismatches, and a completion
marker that does not bind the final manifest and counts. The semantic refinery
additionally rejects invalid joins, skipped boundaries, and malformed payloads.

The bundled offline command is deliberately named for its limited scope:

```text
ror_worldmodel_episode verify-integrity episode-<uuid>
```

It verifies crash-safe artifact framing, inventory, checksums, and monotonic
record IDs. An `integrity-pass` result is not semantic validation and does not
make an episode training-ready. The gameplay-data refinery remains the
fail-closed semantic admission gate for schemas, joins, cadence, controls,
camera/RGB references, provenance, and training-row export.

## Training pipeline handoff

The Oasiz gameplay-data refinery validates the native artifact before exporting
training rows. Its primary transition view is:

```text
obs_t, action_t, obs_t_plus_1
```

The export preserves the exact source/target observation IDs, completed-step
range, input lineage, state, events, camera reference, and RGB reference. It
must not resample actions by wall clock or infer a missing join. Raw native
artifacts remain immutable and quarantined; derived tensors, videos, or
Parquet/JSONL tables carry content hashes back to the native records.

The refinery handoff is exercised in three fail-closed stages:

```sh
python3 outputs/gameplay-data-demo/qa/refinery_cli.py verify-ror \
  --episode /capture-root/episode-<uuid>

python3 outputs/gameplay-data-demo/qa/refinery_cli.py export-ror \
  --episode /capture-root/episode-<uuid> \
  --output /training-root/ror-<uuid>

python3 outputs/gameplay-data-demo/qa/refinery_cli.py verify-ror-export \
  --package /training-root/ror-<uuid>
```

`verify-ror` is the semantic admission gate. `export-ror` writes the exact
`obs_t, action_t, obs_t_plus_1` joins into a crash-safe training package, and
`verify-ror-export` rehashes that package before use. Every exported transition
row preserves the complete native source/target telemetry and transition
payload, plus the authenticated provenance object; the training manifest also
binds that object and its source hash. Training must admit only packages whose
`verify-ror-export` verdict is `pass`.

## Opt-in live capture

Live capture is never enabled by a saved configuration. An operator must supply
every activation and rights input for each run. First create and review the
rights manifest outside RoR, then record its exact SHA-256:

```sh
RIGHTS_MANIFEST=/absolute/path/to/reviewed-rights.json
RIGHTS_SHA256="$(shasum -a 256 "$RIGHTS_MANIFEST" | awk '{print $1}')"
```

Start RoR with a fresh terrain and exactly one local truck. The controller waits
until the truck is seated at global physics step zero, then takes exclusive
ownership of device polling and the exact-step scheduler:

```sh
./RoR \
  -map <terrain-resource> \
  -truck <truck-resource> \
  -enter \
  -worldmodel-capture \
  -worldmodel-output /absolute/path/to/capture-root \
  -worldmodel-root-seed 424242 \
  -worldmodel-episode-ordinal 0 \
  -worldmodel-transitions 480 \
  -worldmodel-rights-manifest "$RIGHTS_MANIFEST" \
  -worldmodel-rights-sha256 "$RIGHTS_SHA256" \
  -worldmodel-data-source beam-cloud/canary \
  -worldmodel-participant-release canary/operator-reviewed \
  -worldmodel-allowed-use world-model-training
```

The transition count is bounded to one hour at 48 Hz. The baseline plus all
transition RGB frames must also fit the refinery's 16 GiB raw-source contract;
for example, 4096x2160 RGB8 admits at most 646 transitions (647 frames total).
RGB defaults to 1920x1080 and can be changed before activation with the
non-archived `wm_capture_rgb_width` and `wm_capture_rgb_height` CVars. Capture refuses
relative/root output paths, a missing or mismatched rights file, unknown
provenance, a scene that has already advanced, multiple or linked Actors,
multiplayer, scripts, water, dynamic sky time, non-automatic gearbox mode, or
arcade controls.

The log prints the exact `.partial` directory when capture starts. On success it
prints the final `episode-<uuid>` directory after validation and atomic sealing.
Turning `wm_capture_enabled` off, unloading/changing the world, receiving an
unsupported game message, closing RoR, or hitting any capture error explicitly
aborts the sink and logs the quarantined `.partial` path. Partial episodes are
never accepted by the verifier or training refinery.

## Initial production restrictions

Schema 1 is fail-closed to exactly one stable local truck Actor, no arcade
control remapping, automatic gearbox mode, a valid cinecam-0 driver-camera
frame, one fresh-scene episode, and a declared supported control profile. Unsupported
drivetrain/controller modes, a changed manager/player/Actor lifetime binding,
unknown content rights, missing provenance, telemetry overflow, disk failure,
or renderer failure fault and quarantine the episode rather than silently
omitting data.
