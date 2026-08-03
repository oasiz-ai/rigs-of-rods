/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

Texture2D uScene : register(t0);
SamplerState uSceneSampler : register(s0);

float4x4 uWorldViewProj;
float4 uInvTextureSize;

static const float ROR_V0A_EXPOSURE = 1.08;
static const float ROR_V0A_CONTRAST = 1.04;
static const float ROR_V0A_SATURATION = 1.03;
static const float ROR_V0A_SHOULDER = 0.12;
static const float ROR_V0A_FXAA_EDGE_THRESHOLD = 1.0 / 8.0;
static const float ROR_V0A_FXAA_EDGE_THRESHOLD_MIN = 1.0 / 24.0;
static const float ROR_V0A_FXAA_BLEND_LIMIT = 0.75;
static const float ROR_V0A_LUMA_RED = 0.2126;
static const float ROR_V0A_LUMA_GREEN = 0.7152;
static const float ROR_V0A_LUMA_BLUE = 0.0722;

struct RorV0AVertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

RorV0AVertexOutput V0A_vs(float4 position : POSITION)
{
    RorV0AVertexOutput output;
    output.position = mul(uWorldViewProj, position);

    // Match OGRE's standard compositor quad convention on Direct3D.
    float2 inPosition = sign(position.xy);
    output.uv = (float2(inPosition.x, -inPosition.y) + 1.0) * 0.5;
    return output;
}

float rorV0ALuma(float3 color)
{
    return color.r * ROR_V0A_LUMA_RED
        + color.g * ROR_V0A_LUMA_GREEN
        + color.b * ROR_V0A_LUMA_BLUE;
}

float3 rorV0AColorCurve(float3 inputColor)
{
    float3 exposed = inputColor * ROR_V0A_EXPOSURE;
    float luminance = rorV0ALuma(exposed);
    float3 saturated = luminance
        + (exposed - float3(luminance, luminance, luminance))
            * ROR_V0A_SATURATION;
    float3 contrasted =
        (saturated - float3(0.5, 0.5, 0.5)) * ROR_V0A_CONTRAST
        + float3(0.5, 0.5, 0.5);

    float3 positive = max(contrasted, float3(0.0, 0.0, 0.0));
    float3 resolved = (1.0 + ROR_V0A_SHOULDER) * positive
        / (float3(1.0, 1.0, 1.0) + ROR_V0A_SHOULDER * positive);
    return clamp(
        resolved,
        float3(0.0, 0.0, 0.0),
        float3(1.0, 1.0, 1.0));
}

float3 rorV0AResolveFxaa(
    float3 center,
    float3 north,
    float3 south,
    float3 east,
    float3 west)
{
    float centerLuma = rorV0ALuma(center);
    float northLuma = rorV0ALuma(north);
    float southLuma = rorV0ALuma(south);
    float eastLuma = rorV0ALuma(east);
    float westLuma = rorV0ALuma(west);
    float minimumLuma = min(
        centerLuma,
        min(min(northLuma, southLuma), min(eastLuma, westLuma)));
    float maximumLuma = max(
        centerLuma,
        max(max(northLuma, southLuma), max(eastLuma, westLuma)));
    float lumaRange = maximumLuma - minimumLuma;
    float edgeThreshold = max(
        ROR_V0A_FXAA_EDGE_THRESHOLD_MIN,
        maximumLuma * ROR_V0A_FXAA_EDGE_THRESHOLD);
    if (lumaRange <= edgeThreshold)
    {
        return center;
    }

    float northSouthGradient = abs(
        northLuma + southLuma - 2.0 * centerLuma);
    float eastWestGradient = abs(
        eastLuma + westLuma - 2.0 * centerLuma);
    float3 neighborAverage;
    if (northSouthGradient >= eastWestGradient)
    {
        neighborAverage = (north + south) * 0.5;
    }
    else
    {
        neighborAverage = (east + west) * 0.5;
    }

    float blend = clamp(
        (lumaRange - edgeThreshold) / lumaRange,
        0.0,
        ROR_V0A_FXAA_BLEND_LIMIT);
    float3 resolved = center + (neighborAverage - center) * blend;
    return clamp(
        resolved,
        float3(0.0, 0.0, 0.0),
        float3(1.0, 1.0, 1.0));
}

float4 V0A_ps(RorV0AVertexOutput input) : SV_Target
{
    float2 texel = uInvTextureSize.xy;
    float4 centerSample = uScene.Sample(uSceneSampler, input.uv);
    float3 center = rorV0AColorCurve(centerSample.rgb);
    float3 north = rorV0AColorCurve(
        uScene.Sample(
            uSceneSampler,
            input.uv + float2(0.0, -texel.y)).rgb);
    float3 south = rorV0AColorCurve(
        uScene.Sample(
            uSceneSampler,
            input.uv + float2(0.0, texel.y)).rgb);
    float3 east = rorV0AColorCurve(
        uScene.Sample(
            uSceneSampler,
            input.uv + float2(texel.x, 0.0)).rgb);
    float3 west = rorV0AColorCurve(
        uScene.Sample(
            uSceneSampler,
            input.uv + float2(-texel.x, 0.0)).rgb);

    return float4(
        rorV0AResolveFxaa(center, north, south, east, west),
        centerSample.a);
}
