RaytracingAccelerationStructure Scene : register(t0);
RWTexture2D<float> Visibility : register(u0);
RWTexture2D<uint> RayLineage : register(u1);
RWTexture2D<float4> Hybrid : register(u2);

static const uint kReceiverMask = 0x01;
static const uint kOccluderMask = 0x02;
static const uint kReceiverInstanceId = 1;
static const uint kOccluderInstanceId = 2;

struct ProbePayload
{
    float hit_t;
    uint instance_id;
};

[shader("raygeneration")]
void RayGen()
{
    const uint2 pixel = DispatchRaysIndex().xy;
    const float sample_x = pixel.x == 0 ? -0.5 : 0.5;
    const float4 raster = pixel.x == 0
        ? float4(0.25, 0.5, 0.75, 1.0)
        : float4(0.75, 0.5, 0.25, 0.5);

    RayDesc primary_ray;
    primary_ray.Origin = float3(sample_x, 0.0, -2.0);
    primary_ray.Direction = float3(0.0, 0.0, 1.0);
    primary_ray.TMin = 0.001;
    primary_ray.TMax = 100.0;
    ProbePayload primary_payload;
    primary_payload.hit_t = -1.0;
    primary_payload.instance_id = 0;
    TraceRay(Scene, RAY_FLAG_FORCE_OPAQUE, kReceiverMask,
             0, 0, 0, primary_ray, primary_payload);

    uint lineage = 0;
    bool blocked = false;
    if (primary_payload.instance_id == kReceiverInstanceId &&
        primary_payload.hit_t > 0.0)
    {
        lineage = 1;
        RayDesc visibility_ray;
        visibility_ray.Origin = primary_ray.Origin +
                                primary_payload.hit_t * primary_ray.Direction +
                                float3(0.0, 0.0, 0.001);
        visibility_ray.Direction = float3(0.0, 0.0, 1.0);
        visibility_ray.TMin = 0.001;
        visibility_ray.TMax = 100.0;
        ProbePayload visibility_payload;
        visibility_payload.hit_t = -1.0;
        visibility_payload.instance_id = 0;
        TraceRay(Scene,
                 RAY_FLAG_FORCE_OPAQUE |
                     RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH,
                 kOccluderMask, 0, 0, 0, visibility_ray,
                 visibility_payload);
        blocked = visibility_payload.instance_id == kOccluderInstanceId;
        if (blocked)
        {
            lineage = 3;
        }
    }

    Visibility[pixel] = blocked ? 0.0 :
        (lineage == 1 ? 1.0 : -1.0);
    RayLineage[pixel] = lineage;
    Hybrid[pixel] = blocked ? float4(0.0, 0.0, 0.0, raster.a) : raster;
}

[shader("miss")]
void Miss(inout ProbePayload payload)
{
    payload.hit_t = -1.0;
    payload.instance_id = 0;
}

[shader("closesthit")]
void ClosestHit(inout ProbePayload payload,
                in BuiltInTriangleIntersectionAttributes attributes)
{
    payload.hit_t = RayTCurrent();
    payload.instance_id = InstanceID();
}
