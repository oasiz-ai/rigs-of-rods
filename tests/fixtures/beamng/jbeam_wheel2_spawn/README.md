# Authenticated JBeam Wheel2 spawn fixture

This project-original package exercises the authenticated JBeam cache, import,
RigDef lowering, ActorManager spawn, and native `RigDef::Wheel2` topology path
for one narrowly admitted `pressureWheels` row. The row is intentionally
unpropelled and unbraked, disables generated triangle collision, and uses no
Lua, external assets, Ogre scripts, network input, or third-party mod data.

The runtime receipt is limited to exact generated node/beam and rim/tyre
classification, finite positive node state sampled at step zero and every 100
fixed steps through step 20,000 (201 samples), zero broken beams, and exact
one/eight-worker state traces. The numeric state envelope is a batch-boundary
sample, not a per-step bound. Zero motion and zero contact are accepted, so
this does not prove gravity response, contact behavior, static load, or that
the structure settles. It also does not claim pressure-volume behavior,
advanced friction, braking, propulsion, steering, rolling behavior,
driveability, BeamNG source-engine parity, or playability. Static-load and
meaningful settle behavior remain future gates.
