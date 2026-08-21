# Authenticated JBeam inter-actor collision fixture

This gate mounts the project-original JBeam package through the real cache and
immutable ZIP path, spawns two instances while physics is paused, and places
their default `NORMALTYPE` surfaces within the native 0.02 m collision range.
The two actors receive equal and opposite vertical velocities before fixed
step zero.

Acceptance requires a nonempty native contact-key stream in the canonical
state trace, a finite relative-velocity response, separation after contact,
zero broken beams, exact topology, and identical 2,000-step one-worker and
eight-worker traces. The gate retains `selfCollision=false`, so those keys are
external actor node-to-triangle contacts rather than self-contact.

This is bounded RoR execution evidence for the documented BeamNG node and
`NORMALTYPE` triangle mapping. It is not a collision-force parity claim, does
not exercise `selfCollision=true` or `staticCollision=false`, and does not
qualify arbitrary third-party vehicles.
