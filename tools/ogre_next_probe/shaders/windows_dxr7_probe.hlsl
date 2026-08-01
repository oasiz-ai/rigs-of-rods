RaytracingAccelerationStructure Scene : register(t0);
RWByteAddressBuffer Output : register(u0);

struct ProbePayload
{
    uint value;
};

[shader("raygeneration")]
void RayGen()
{
    RayDesc ray;
    ray.Origin = float3(0.0, 0.0, -2.0);
    ray.Direction = float3(0.0, 0.0, 1.0);
    ray.TMin = 0.001;
    ray.TMax = 100.0;
    ProbePayload payload;
    payload.value = 0xbaadf00d;
    TraceRay(Scene, RAY_FLAG_NONE, 0xff, 0, 0, 0, ray, payload);
    Output.Store(0, payload.value);
}

[shader("miss")]
void Miss(inout ProbePayload payload)
{
    payload.value = 0x0badcafe;
}

[shader("closesthit")]
void ClosestHit(inout ProbePayload payload,
                in BuiltInTriangleIntersectionAttributes attributes)
{
    payload.value = 0xd1ceb00b;
}
