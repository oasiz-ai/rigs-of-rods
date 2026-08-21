# Authenticated JBeam spawn-soak fixture

This project-original package exercises the product JBeam cache, immutable ZIP
mount, RigDef lowering, ActorManager spawn, native hydro adapter, and fixed-step
state trace. It intentionally uses only the allowlisted structural and hydro
subset. It is a numerical integration fixture, not a BeamNG.drive behavior or
vehicle-fidelity claim. The runtime harness forces this fixture active only for
the bounded measurement interval so RoR's ordinary inactive-vehicle sleep
policy cannot shorten the requested 120,000-step actuator audit; both success
and failure restore the normal sleep policy.

The fixture opts all six nodes into the documented terrain/static-collision
path with the supported collision mode authored explicitly
(`collision=true`, `selfCollision=false`, `staticCollision=true`) while
retaining zero explicit RoR self-collision contacters. The five authored
default `NORMALTYPE` surfaces must also become five native collision cabs;
this proves product construction, not a two-actor impact. One structural row
is an exact BeamNG `SUPPORT` beam with 1.01 precompression and a 2.0
`beamLongBound`. The acceptance receipt requires that native support beam to
accept every fixed step, enter compression response at least once, remain
finite and fault-free, and never break. At
physics step zero the product script transactionally translates the spawned
actor upward by 2 m and assigns a `-4 m/s` velocity to every movable node. The
acceptance receipt requires more than 1 m of center-of-mass descent, an upward
terrain-contact response, 120,000 accepted hydro steps, finite state, and zero
broken beams in both one-worker and eight-worker runs. This is deliberately
bounded node-to-terrain evidence; external triangle impact, self-collision,
static-collision variants, and
third-party vehicle parity remain outside the fixture's claim.
