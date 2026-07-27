# Clean-room structural JBeam fixture

This directory is original Rigs of Rods test content for the locked
`beamng-docs-0.38.5.0-2026-07-27` interoperability profile. It deliberately
uses only a small documented subset: a main part, one default slot, nodes,
one six-node reference frame, normal beams, triangles, and a quad.

The geometry is synthetic and intentionally plain. It is not copied or
converted from BeamNG, FormulaCOUPE, or another vehicle. Public CI may parse,
resolve, lower, spawn, and redistribute it under `GPL-3.0-or-later`.

`fixture-profile.json` records the expected structural invariants and source
checksums. Update the profile and its checksums in the same commit whenever a
fixture source changes; never silently replace behavior under the existing
fixture ID.
