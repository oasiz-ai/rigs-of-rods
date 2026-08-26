// Modern D3D11 port of Caelum's texture-preserving lunar phase mask.

#if defined(CAELUM_PHASE_MOON_FRAGMENT)

float phase;
Texture2D moonDisc : register(t0);
SamplerState moonDiscSampler : register(s0);

struct CaelumPhaseMoonFragmentInput
{
    float4 clipPosition : SV_Position;
    float2 uv : TEXCOORD0;
};

float CaelumMoonPhaseFactor(float2 uv, float phaseValue)
{
    float alpha = 1.0;
    float signedReferenceX = uv.x - 0.5;
    float referenceY = abs(uv.y - 0.5);
    float referenceXForY = sqrt(0.25 - referenceY * referenceY);
    float minimumX = -referenceXForY;
    float maximumX = referenceXForY;
    float firstBoundary =
        (maximumX - minimumX) * (phaseValue / 2.0) + minimumX;
    float secondBoundary =
        (maximumX - minimumX) * phaseValue + minimumX;
    if (signedReferenceX < firstBoundary)
    {
        alpha = 0.0;
    }
    else if (
        signedReferenceX < secondBoundary
        && firstBoundary != secondBoundary)
    {
        alpha = (signedReferenceX - firstBoundary)
            / (secondBoundary - firstBoundary);
    }
    return alpha;
}

float4 CaelumPhaseMoonPS(
    CaelumPhaseMoonFragmentInput input) : SV_Target
{
    float4 colour = moonDisc.Sample(moonDiscSampler, input.uv);
    float alpha = CaelumMoonPhaseFactor(input.uv, phase);
    float luminance = dot(colour.rgb, float3(0.3333, 0.3333, 0.3333));
    colour.a = min(colour.a, luminance * alpha);
    colour.rgb /= luminance;
    return colour;
}

#else
#error The Caelum phase-moon fragment selector is required
#endif
