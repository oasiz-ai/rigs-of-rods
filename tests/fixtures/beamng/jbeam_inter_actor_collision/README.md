# Authenticated JBeam inter-actor collision fixture

This gate mounts the project-original JBeam package through the real cache and
immutable ZIP path, spawns two instances while physics is paused, and places
their default `NORMALTYPE` surfaces within the native 0.02 m collision range.
The two actors receive equal and opposite vertical velocities before fixed
step zero.

Acceptance requires exact topology, zero broken beams, identical 2,000-step
one-worker and eight-worker traces, and a schema-3 conservation receipt whose
isolated-contact and whole-step shared-node energy identities are exact. The
version-2 fixture profile also supplies a strict, digest-bound numerical
regression envelope: 128–256 contacts, 5–8 m/s maximum relative-velocity
change, 0.5–1 m maximum separation, at most `1e-6` normalized linear impulse
residual, 1–2 N·m·s per-contact angular impulse delta, 4–8 N·m·s summed
angular-delta magnitude, and bounded whole-step work, kinetic delta,
integration delta, and shared-node cross term. Missing, extra, non-finite,
boolean, fractional-count, reversed, or out-of-range values fail closed. The
summed angular magnitude must also satisfy the per-contact triangle bound. The
accepted envelope is serialized as recursively key-sorted, exponent-free
decimal JSON under `ror-contact-acceptance-sorted-decimal-json-v1` and emitted
with its canonical SHA-256 in the report and package inventory. The gate retains
`selfCollision=false`, so its keys are
external actor node-to-triangle contacts rather than self-contact.

The envelope is a scenario-specific numerical regression contract derived
from RoR's clean-room fixture. It is not physical calibration or BeamNG force
parity, does not exercise `selfCollision=true` or `staticCollision=false`, and
does not qualify arbitrary third-party vehicles.
