#include <metal_stdlib>
#include <metal_raytracing>

using namespace metal;
using namespace raytracing;

struct SunVisibilityV2Parameters {
    float4x4 render_from_clip;
    float4 surface_to_light_and_minimum_distance;
    uint width;
    uint height;
};

/// V2's runtime compositor writes LitHdr directly on the Ogre Metal queue.
/// BaseHdr deliberately excludes the directional-sun direct term; therefore
/// an occluder never erases ambient, sky, emissive, or local-light radiance.
kernel void ror_ogre_next_metal_sun_visibility_v2(
    instance_acceleration_structure scene [[buffer(0)]],
    constant SunVisibilityV2Parameters& parameters [[buffer(1)]],
    device atomic_uint* counters [[buffer(2)]],
    texture2d<half, access::read> base_hdr [[texture(0)]],
    texture2d<half, access::read> sun_direct_hdr [[texture(1)]],
    texture2d<half, access::write> visibility [[texture(2)]],
    texture2d<half, access::write> lit_hdr [[texture(3)]],
    uint2 pixel [[thread_position_in_grid]])
{
    if (pixel.x >= parameters.width || pixel.y >= parameters.height) {
        return;
    }
    atomic_fetch_add_explicit(counters + 0, 1u, memory_order_relaxed);
    const half4 base = base_hdr.read(pixel);
    const half4 sun_direct = sun_direct_hdr.read(pixel);
    const float2 uv = (float2(pixel) + 0.5f) /
                      float2(parameters.width, parameters.height);
    const float2 ndc = float2(uv.x * 2.0f - 1.0f,
                              1.0f - uv.y * 2.0f);
    const float4 near_h = parameters.render_from_clip *
                          float4(ndc, 0.0f, 1.0f);
    const float4 far_h = parameters.render_from_clip *
                         float4(ndc, 1.0f, 1.0f);
    const float3 near_point = near_h.xyz / near_h.w;
    const float3 far_point = far_h.xyz / far_h.w;
    const float3 segment = far_point - near_point;

    ray primary;
    primary.origin = near_point;
    primary.direction = normalize(segment);
    primary.min_distance =
        parameters.surface_to_light_and_minimum_distance.w;
    primary.max_distance = length(segment);
    intersector<triangle_data, instancing> primary_tracer;
    primary_tracer.accept_any_intersection(false);
    // Every raster-visible opaque caster must carry both receiver (0x01) and
    // caster (0x02) masks. Thus this primary query hits the actual visible
    // road or gate surface instead of the road geometrically behind a gate.
    const auto receiver = primary_tracer.intersect(primary, scene, 0x01u);

    // A primary miss is sky/background. BaseHdr already contains that
    // radiance; adding a surface-only SunDirectHdr texel here would be a
    // cross-layer mismatch. Keep canonical visible R16 for diagnostics while
    // preserving BaseHdr exactly.
    if (receiver.type == intersection_type::none) {
        atomic_fetch_add_explicit(counters + 2, 1u, memory_order_relaxed);
        visibility.write(half(1.0h), pixel);
        lit_hdr.write(half4(base.rgb, half(1.0h)), pixel);
        atomic_fetch_add_explicit(counters + 4, 1u, memory_order_relaxed);
        atomic_fetch_add_explicit(counters + 5, 1u, memory_order_relaxed);
        return;
    }

    atomic_fetch_add_explicit(counters + 1, 1u, memory_order_relaxed);
    const float3 surface_to_light =
        parameters.surface_to_light_and_minimum_distance.xyz;
    const float3 hit_point =
        primary.origin + primary.direction * receiver.distance;
    ray shadow;
    // A raster-visible gate is receiver+caster. Move the secondary origin
    // toward the directional light by two minimum-distance units before using
    // the caster mask, so the gate does not shadow its own visible surface.
    shadow.origin = hit_point + surface_to_light *
                    (2.0f * primary.min_distance);
    shadow.direction = surface_to_light;
    shadow.min_distance = primary.min_distance;
    shadow.max_distance = 1000000.0f;
    intersector<triangle_data, instancing> shadow_tracer;
    shadow_tracer.accept_any_intersection(true);
    const bool blocked =
        shadow_tracer.intersect(shadow, scene, 0x02u).type !=
        intersection_type::none;

    const half v = blocked ? half(0.0h) : half(1.0h);
    atomic_fetch_add_explicit(counters + (blocked ? 3 : 2), 1u,
                              memory_order_relaxed);
    visibility.write(v, pixel);
    // Admission guarantees that this exact sum remains finite in RGBA16.
    const float3 composed =
        float3(base.rgb) + float(v) * float3(sun_direct.rgb);
    lit_hdr.write(half4(half3(composed), half(1.0h)), pixel);
    atomic_fetch_add_explicit(counters + 4, 1u, memory_order_relaxed);
    atomic_fetch_add_explicit(counters + 5, 1u, memory_order_relaxed);
}
