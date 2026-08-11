# OGRE 14 game-host product session

Status: **implemented for the endpoint-adopted `RoR-Ogre14` role; package
admission and the default/fallback policy are unchanged**

`RendererOgre14ProductSession` is the product owner for one renderer-bridge
session. It connects the real OGRE 14 game/simulation process to the existing
renderer-neutral transport without loading Ogre-Next into that address space.
A direct `RoR-Ogre14` launch has no bridge endpoint, creates none of these
objects, preserves the original game arguments, and retains its existing OIS,
window, renderer, and shutdown behavior.

## Startup and ownership

`main.cpp` decodes and adopts the exact six-record `RendererBridgeEndpoint`
prefix before renderer initialization, CURL, or any worker thread starts. A
validated runtime-ownership plan then gives the Ogre-Next child the only
visible presentation window and all physical input devices. OGRE 14 creates a
hidden, inactive resource/scene host from the backend's creation-time `hidden`
contract; startup fails closed if that contract is unavailable. Its
`InputEngine` keeps mappings and transported state but opens no OIS, SDL HID,
or force-feedback device. After `GfxScene`, GUI, and that transport-only
`InputEngine` exist, the endpoint path creates, in dependency order:

1. one `Ogre14GraphicsSceneSource`, enabled only for bridge capture;
2. one `RendererOgre14InputEngineTarget`;
3. one `RendererOgre14ProductSession`, which owns exactly one
   `RendererOgre14GameHostSession`, input adapter, and
   `GraphicsSceneSnapshotProducer`.

The host derives its nonzero asset-registry identity from the supervisor's
session identifier. Product startup overwrites any caller-provided producer
registry identity with that derived value, so assets and scenes cannot enter a
different session. Transport input is authoritative before `Start()` returns;
there is no first-frame interval in which an endpoint-adopted host samples or
owns local physical devices. Input reinitialization preserves that ownership.
The legacy main loop never calls OGRE 14 `renderOneFrame()` or window update in
bridge mode, and any optional legacy camera window must also be created hidden.

## Frame transaction

Every game-loop iteration follows this order:

1. `InputEngine::Capture()` clears only the previous renderer-transport mouse
   and joystick deltas.
2. `PumpReverse()` drains every currently published input, cumulative ACK, and
   surface/control message before gameplay reads input state.
3. Ordered key, mouse, wheel, UTF-8 text, focus, and close transitions reuse
   the existing `AppContext` GUI/camera callbacks. One final reconciliation
   transaction replaces keyboard, mouse, and up to ten joystick/raw-device
   held states atomically.
4. Physics completes and `GfxScene::BufferSimulationData()` copies the
   simulation-owned state into graphics buffers.
5. `GfxScene::UpdateScene()` consumes those copies and joins the flex/wheel
   tasks; no bridge capture reads live solver state.
6. If the child has announced an active surface, the source captures the now
   completed joined scene and camera whose pixel extent must match that
   surface. The producer creates one immutable
   `GraphicsSceneSnapshotProduction`.

The product owner retains that complete production until its optional asset
delta and scene envelope are both accepted. Asset submission always precedes
the scene. Bounded queue or unacknowledged-lineage backpressure retries the
same owners and never recaptures a newer source frame, re-runs the producer, or
skips the retained snapshot/tick lineage. If a resize overtakes a captured
frame, its original nonzero surface revision remains attached to the host-side
submission decision; future or zero revisions are rejected, while an older
validated frame can still be sequenced for presentation-child retirement. The
child rechecks the decoded camera against its latest drawable extent even when
that resize was already announced by an idle poll, so a pre-resize scene can
never become presentable after the announcement.

## Input semantics

The portable adapter maps SDL 2 physical scan positions to the exact
OIS/DirectInput numeric identities used by existing `input.map` files. Key
repeat does not fabricate another press transition; text stays in the separate
UTF-8 event. SDL gamepad axes retain their signed 16-bit values. Raw absolute
axes, hats, buttons, and sliders are normalized into legacy OIS ranges from
their validated descriptors; relative raw-axis deltas are batch-local. Device
identity plus connection generation keeps slots stable, with the existing
ten-slot game limit enforced before callbacks run.

Each apply result exposes reverse sequence plus issued first/last event IDs,
the reconciliation's resolved-through watermark, and the successfully applied
watermark. A rejected message, duplicate sequence, capacity error, activation
failure, allocation failure, or reconciliation failure never advances applied
lineage. Unsupported physical keys remain available through text input but do
not invent an OIS key identity. Force feedback is not transported by input
version 1 and remains a separate future game-to-presentation contract.

## Shutdown

Normal loop exit invokes product shutdown before input, window integration, or
the OGRE 14 renderer is destroyed. Within one bounded deadline it:

1. drains reverse traffic and retries the exact pending production;
2. requests the game-to-child half-close only after the forward queue drains;
3. continues consuming reverse input/ACK/control traffic until peer EOF and
   worker completion;
4. closes and joins the host worker.

A timeout or stream/lineage failure closes the adopted channels and is
terminal. `REQUEST_GRACEFUL_SHUTDOWN` routes through the same idempotent
application-close sink as a native window close. Destructors are only a final
no-throw guard; the main path performs the ordered shutdown explicitly.

## Cross-platform and acceptance

The adapter and product coordinator contain no SDL, OIS, OGRE, POSIX, or Win32
headers. Native channel behavior remains in `RendererBridgeChannel`; only the
small `RendererOgre14InputEngineTarget` binds portable state to OIS and
`AppContext`. The same sources and strict tests are in the macOS, Linux, and
Windows CMake graph.

The focused acceptance targets are:

```sh
cmake -S tests -B build-render-contracts -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build-render-contracts --target \
  ror_renderer_ogre14_input_adapter_tests \
  ror_renderer_ogre14_runtime_ownership_tests \
  ror_renderer_ogre14_game_host_session_tests
ctest --test-dir build-render-contracts --output-on-failure \
  -R 'renderer_ogre14_(input_adapter|runtime_ownership|game_host_session)'
```

They cover mapping and repeat semantics, issued/resolved/applied lineage,
activation/reconciliation failures, bounded delta accumulation, gamepad/raw
device state, single visible/input ownership, real native pipes, surface
readiness and resize overtaking (including resize announced while idle),
asset-first backpressure without recapture, cumulative acknowledgements, and
forward-half-close/peer-EOF/join ordering. The full `RoR-Ogre14` application
target remains the product compile/link gate. None of these tests changes
generated package presence/readiness/admission facts or relaxes
`OGRE_NEXT_REQUIRE`, `REQUIRE_NATIVE`, or the pre-readiness-only compatibility
fallback.
