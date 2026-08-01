# Ogre-Next Vulkan RT5 external-device foundation

RT5 is the Linux ownership checkpoint before Vulkan KHR ray tracing. It is an
opt-in standalone proof inside the pinned Ogre-Next project; it is not wired
into the shipping OGRE 1.14 application and it does not claim native ray
tracing, acceleration-structure construction, or a ray-traced image.

## Contract

RoR creates and owns exactly one Vulkan 1.2 instance, physical device, logical
device, graphics queue, and timeline semaphore. The instance pointer is passed
to `Ogre::VulkanPlugin::install()` as `external_instance`; the device pointer is
passed only on Ogre's first null-window creation as `external_device`.

The proof accepts only discrete or integrated GPUs. CPU implementations,
known software rasterizers such as lavapipe/llvmpipe or SwiftShader, virtual
GPUs without a separately reviewed attestation, Vulkan versions below 1.2,
devices without a graphics queue, and devices without timeline semaphores are
explicit unsupported skips (process exit 77). They are never hardware or RT
passes.

Pinned Ogre-Next v3.0 filters a caller-provided extension list against
supported extensions and derives its core feature view from supported features;
neither operation proves what the caller enabled. RT5 closes that ambiguity by:

- enabling every supported core `VkPhysicalDeviceFeatures` bit that Ogre may
  observe;
- enabling `VkPhysicalDeviceVulkan12Features::timelineSemaphore` explicitly;
- enabling and claiming no instance or device extensions for the null-window
  proof, so the claimed set equals the create-info set exactly.

The runtime then compares Ogre's instance, physical-device, logical-device,
graphics-queue, queue-family, and queue-index handles with RoR's exact handles,
and requires Ogre's external-ownership flag. One queue submission signals and
waits on the timeline before Ogre attachment and another does so after Ogre
shutdown.

## Ownership order

The only accepted full lifecycle is:

1. RoR instance/device/queue/timeline creation.
2. Ogre external attachment and null-window initialization.
3. Ogre shutdown while every RoR-owned Vulkan object is still live.
4. Timeline destruction.
5. Logical-device destruction.
6. Instance destruction.

The pure C++ lifecycle contract rejects every out-of-order transition.

## Evidence

`tools/ogre_next_probe/run_vulkan_rt5.py` produces and validates:

- `ror-ogre-next-vulkan-rt5-report.json`;
- `ror-ogre-next-vulkan-rt5-attestation.json`;
- `bin/ror_ogre_next_vulkan_rt5_smoke`.

The attestation hashes the exact report and executable and records the RoR
repository/ref/commit/relevant-source manifest plus the exact Ogre-Next
repository/branch/commit/archive/license pin. The Linux CI job deliberately
selects lavapipe and requires an explicit unsupported report, preserving a
portable build/runtime gate without manufacturing a hardware result.

Run the offline contract on any supported development host:

```sh
python tools/ogre_next_probe/run_vulkan_rt5.py --validate-contract-only
```

Run on Linux x86_64 using an already configured owned probe build:

```sh
python tools/ogre_next_probe/run_vulkan_rt5.py \
  --build-dir /absolute/isolated/build \
  --reuse-build-dir
```
