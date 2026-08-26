/*
--------------------------------------------------------------------------------
This source file is part of Hydrax.

Copyright (C) 2008 Xavier Verguin Gonzalez

This program is free software; you can redistribute it and/or modify it under
the terms of the GNU Lesser General Public License as published by the Free
Software Foundation; either version 2 of the License, or (at your option) any
later version.
--------------------------------------------------------------------------------
*/

#ifndef _Hydrax_GpuNormalMapModernHlsl_H_
#define _Hydrax_GpuNormalMapModernHlsl_H_

namespace Hydrax
{
namespace ModernHlsl
{
    inline const char* vertexTarget()
    {
        return "vs_4_0";
    }

    inline const char* fragmentTarget()
    {
        return "ps_4_0";
    }

    inline const char* gpuNormalMapVertexSource()
    {
        return R"HYDRAX_HLSL(
void main_vp(
    float4 iPosition : POSITION,
    out float4 oPosition : SV_Position,
    out float3 oPositionObject : TEXCOORD0,
    out float4 oWorldUV : TEXCOORD1,
    out float oScale : TEXCOORD2,
    out float3 oCameraToPixel : TEXCOORD3,
    uniform float4x4 uWorldViewProj,
    uniform float4x4 uWorld,
    uniform float3 uCameraPos,
    uniform float uScale)
{
    oPosition = mul(uWorldViewProj, iPosition);
    oPositionObject = iPosition.xyz;
    float2 scaledWorldXZ = uScale * mul(uWorld, iPosition).xz * 0.0078125;
    oWorldUV.xy = scaledWorldXZ;
    oWorldUV.zw = scaledWorldXZ * 16.0;
    oScale = uScale;
    oCameraToPixel = iPosition.xyz - uCameraPos;
}
)HYDRAX_HLSL";
    }

    inline const char* perlinGpuNormalMapFragmentSource()
    {
        return R"HYDRAX_HLSL(
Texture2D uNoise0 : register(t0);
SamplerState uNoise0Sampler : register(s0);
Texture2D uNoise1 : register(t1);
SamplerState uNoise1Sampler : register(s1);

void main_fp(
    float3 iPosition : TEXCOORD0,
    float4 iWorldCoord : TEXCOORD1,
    float iScale : TEXCOORD2,
    float3 iCameraToPixel : TEXCOORD3,
    out float4 oColor : SV_Target,
    uniform float uStrength,
    uniform float3 uLODParameters)
{
    float distanceToCamera = length(iCameraToPixel);
    float attenuation = saturate(distanceToCamera / uLODParameters.z);
    float derivation = lerp(uLODParameters.x, uLODParameters.y, attenuation);
    derivation *= iScale;
    derivation *= 1.0 / abs(normalize(iCameraToPixel).y);

    float2 dx = float2(derivation * 0.0078125, 0.0);
    float2 dy = float2(0.0, dx.x);

    float3 p_dx = float3(
        iPosition.x + derivation,
        uNoise0.Sample(uNoise0Sampler, iWorldCoord.xy + dx).x +
            uNoise1.Sample(uNoise1Sampler, iWorldCoord.zw + dx * 16.0).x,
        iPosition.z);
    float3 m_dx = float3(
        iPosition.x - derivation,
        uNoise0.Sample(uNoise0Sampler, iWorldCoord.xy - dx).x +
            uNoise1.Sample(uNoise1Sampler, iWorldCoord.zw - dx * 16.0).x,
        iPosition.z);
    float3 p_dy = float3(
        iPosition.x,
        uNoise0.Sample(uNoise0Sampler, iWorldCoord.xy + dy).x +
            uNoise1.Sample(uNoise1Sampler, iWorldCoord.zw + dy * 16.0).x,
        iPosition.z + derivation);
    float3 m_dy = float3(
        iPosition.x,
        uNoise0.Sample(uNoise0Sampler, iWorldCoord.xy - dy).x +
            uNoise1.Sample(uNoise1Sampler, iWorldCoord.zw - dy * 16.0).x,
        iPosition.z - derivation);

    float strength = uStrength * (1.0 - attenuation);
    p_dx.y *= strength;
    m_dx.y *= strength;
    p_dy.y *= strength;
    m_dy.y *= strength;

    float3 normal = normalize(cross(p_dx - m_dx, p_dy - m_dy));
    oColor = float4(saturate(1.0 - (0.5 + 0.5 * normal)), 1.0);
}
)HYDRAX_HLSL";
    }

    inline const char* fftGpuNormalMapFragmentSource()
    {
        return R"HYDRAX_HLSL(
Texture2D uFFT : register(t0);
SamplerState uFFTSampler : register(s0);

void main_fp(
    float3 iPosition : TEXCOORD0,
    float4 iWorldCoord : TEXCOORD1,
    float iScale : TEXCOORD2,
    float3 iCameraToPixel : TEXCOORD3,
    out float4 oColor : SV_Target,
    uniform float uStrength,
    uniform float3 uLODParameters)
{
    float distanceToCamera = length(iCameraToPixel);
    float attenuation = saturate(distanceToCamera / uLODParameters.z);
    float derivation = lerp(uLODParameters.x, uLODParameters.y, attenuation);
    derivation *= iScale;
    derivation *= 1.0 / abs(normalize(iCameraToPixel).y);

    float2 dx = float2(derivation * 0.0078125, 0.0);
    float2 dy = float2(0.0, dx.x);

    float3 p_dx = float3(
        iPosition.x + derivation,
        uFFT.Sample(uFFTSampler, iWorldCoord.xy + dx).x,
        iPosition.z);
    float3 m_dx = float3(
        iPosition.x - derivation,
        uFFT.Sample(uFFTSampler, iWorldCoord.xy - dx).x,
        iPosition.z);
    float3 p_dy = float3(
        iPosition.x,
        uFFT.Sample(uFFTSampler, iWorldCoord.xy + dy).x,
        iPosition.z + derivation);
    float3 m_dy = float3(
        iPosition.x,
        uFFT.Sample(uFFTSampler, iWorldCoord.xy - dy).x,
        iPosition.z - derivation);

    float strength = uStrength * (1.0 - attenuation);
    p_dx.y *= strength;
    m_dx.y *= strength;
    p_dy.y *= strength;
    m_dy.y *= strength;

    float3 normal = normalize(cross(p_dx - m_dx, p_dy - m_dy));
    oColor = float4(saturate(1.0 - (0.5 + 0.5 * normal)), 1.0);
}
)HYDRAX_HLSL";
    }
}
}

#endif
