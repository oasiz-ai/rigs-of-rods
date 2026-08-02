# Ogre-Next Vulkan RT6 KHR ray-dispatch and N4A checkpoint

RT6 is the first Linux checkpoint that may report Vulkan KHR ray tracing. A
pass requires a real hardware ray dispatch and an exact nonzero primary-hit
readback plus the bounded directional-shadow semantic readbacks described
below. Compiling the KHR types, creating placeholder objects, or running on
lavapipe is not a pass.

This remains an opt-in standalone proof around the pinned Ogre-Next project. It
does not wire ray tracing into the shipping OGRE 1.14 renderer, composite the
ray image into an Ogre frame, implement dynamic vehicle updates, or touch any
recorder/world-model path.

## Device admission

RoR owns the Vulkan 1.2+ instance, physical device, logical device, graphics
queue, and timeline semaphore. Admission accepts only integrated or discrete
hardware and requires all of the following before device creation:

- `VK_KHR_deferred_host_operations`;
- `VK_KHR_buffer_device_address` and `bufferDeviceAddress`;
- `VK_KHR_acceleration_structure` and `accelerationStructure`;
- `VK_KHR_ray_tracing_pipeline` and `rayTracingPipeline`;
- a graphics queue with compute capability, plus timeline-semaphore support;
- storage and transfer-source support for `VK_FORMAT_R32G32B32A32_UINT`;
- valid shader-group and acceleration-structure scratch alignment properties.

The create info enables exactly those four device extensions, every supported
core feature Ogre may observe, timeline semaphores, buffer device addresses,
acceleration structures, and ray-tracing pipelines. CPU, virtual, known
software, incomplete, or Vulkan 1.1 devices return explicit unsupported exit
77. In particular, the hosted Linux lavapipe job must produce unsupported
evidence and must have zero dispatch/readback claims.

## Dispatch proof

The owner creates one six-vertex buffer containing a three-vertex receiver and
a distinct three-vertex elevated occluder. It builds one BLAS per triangle and
a two-instance TLAS with receiver mask `0x01`/ID `1` and occluder mask
`0x02`/ID `2`. The original 1x1 RGBA32_UINT primary-hit storage image remains
unchanged. A separate std430 storage buffer carries two semantic samples as
R16 binary16 visibility encodings packed into 32-bit words, R32_UINT lineage,
and RGBA16 binary16 pairs packed into `uvec2` words. These are exact encodings,
not typed `VK_FORMAT_R16_SFLOAT` or `VK_FORMAT_R16G16B16A16_SFLOAT` images.
Three runtime-compiled SPIR-V 1.4 shaders, a KHR ray pipeline, and aligned
ray-generation/miss/hit shader-binding-table records complete the dispatch.
Scratch and SBT device addresses are aligned using the selected device's
reported limits.

One primary command buffer on the shared graphics-and-compute queue:

1. builds both BLAS with an intervening acceleration-structure barrier;
2. makes both BLAS and reused scratch writes available to the TLAS build;
3. builds the two-instance TLAS and makes it visible to ray shaders;
4. transitions a 1x1 integer image to general layout;
5. executes `vkCmdTraceRaysKHR(1, 1, 1)`;
6. copies the image to coherent host-visible memory; and
7. signals the owner timeline at value 2.

The ray starts above the fixed triangle and points through its center. The
closest-hit shader must produce exactly:

```text
[1381250561, 324508639, 610839776, 1]
```

A miss, zero output, different output, failed host wait, or omitted dispatch is
a hard error, not an unsupported skip and not a pass.

The same dispatch also traces one receiver ray and one +Z directional
visibility ray at each of two controlled sample positions. The first sample
must miss the occluder and return visibility `0x3c00` with lineage `1`; the
second must hit it and return `0x0000` with lineage `3`. GPU-packed raster and
hybrid RGBA16 words must match the platform-neutral sample oracle exactly.
This closes only the N4A semantic submilestone: the report fixes
`semantic_probe_only=true`, `exact_ogre_rgba16_source=false`, and
`hybrid_ogre_image_composite=false`.

## Ogre adoption and ownership

After readback, the same instance is passed to `VulkanPlugin` through
`external_instance` and the same physical device, logical device, graphics
queue, queue family, and queue index are passed to the first null window through
`external_device`. RT6 compares every handle and requires Ogre's external-owner
flag.

The accepted timeline/lifecycle sequence is owner value 1, ray dispatch value
2, Ogre attachment and shutdown, owner value 3, ray-resource destruction,
timeline destruction, device destruction, then instance destruction. The pure
C++ contract rejects attachment before dispatch and teardown while Ogre is
attached.

## Evidence and commands

`tools/ogre_next_probe/run_vulkan_rt6.py` validates and attests these exact
artifacts:

- `ror-ogre-next-vulkan-rt6-report.json`;
- `ror-ogre-next-vulkan-rt6-attestation.json`;
- `bin/ror_ogre_next_vulkan_rt6_smoke`.

The attestation hashes the report and executable and binds them to the RoR
source manifest and pinned Ogre-Next source/archive/license identity. Validate
the offline contract, including its optimized-Python-safe checks, with:

```sh
python tools/ogre_next_probe/run_vulkan_rt6.py --validate-contract-only
python -O tools/ogre_next_probe/run_vulkan_rt6.py --validate-contract-only
```

On Linux x86_64, run against an already configured owned probe build with:

```sh
python tools/ogre_next_probe/run_vulkan_rt6.py \
  --build-dir /absolute/isolated/build \
  --reuse-build-dir
```

A hardware pass proves the fixed same-device primary-hit path and bounded N4A
directional-shadow semantics. Exact Ogre RGBA16 image input/compositing, shared
live scene extraction, resize/soak, validation-layer cleanliness, performance,
and production renderer registration remain later gates.
