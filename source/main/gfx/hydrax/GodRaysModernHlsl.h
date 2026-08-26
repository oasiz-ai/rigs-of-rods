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

#ifndef _Hydrax_GodRaysModernHlsl_H_
#define _Hydrax_GodRaysModernHlsl_H_

namespace Hydrax
{
namespace ModernHlsl
{
    inline const char* godRaysVertexSource(bool objectsIntersections)
    {
        if (objectsIntersections)
        {
            return R"HYDRAX_HLSL(
void main_vp(
    float4 iPosition : POSITION,
    out float4 oPosition : SV_Position,
    out float3 oPositionObject : TEXCOORD0,
    out float4 oProjUV : TEXCOORD1,
    uniform float4x4 uWorld,
    uniform float4x4 uTexViewProj,
    uniform float4x4 uWorldViewProj)
{
    oPosition = mul(uWorldViewProj, iPosition);
    float4 worldPosition = mul(uWorld, iPosition);
    oPositionObject = worldPosition.xyz;
    oProjUV = mul(uTexViewProj, worldPosition);
}
)HYDRAX_HLSL";
        }

        return R"HYDRAX_HLSL(
void main_vp(
    float4 iPosition : POSITION,
    out float4 oPosition : SV_Position,
    uniform float4x4 uWorldViewProj)
{
    oPosition = mul(uWorldViewProj, iPosition);
}
)HYDRAX_HLSL";
    }

    inline const char* godRaysFragmentSource(
        bool objectsIntersections,
        bool causticsDepthChannel)
    {
        if (objectsIntersections && causticsDepthChannel)
        {
            return R"HYDRAX_HLSL(
Texture2D uDepthMap : register(t0);
SamplerState uDepthMapSampler : register(s0);

void main_fp(
    float3 iPositionObject : TEXCOORD0,
    float4 iProjUV : TEXCOORD1,
    out float4 oColor : SV_Target,
    uniform float3 uLightPosition,
    uniform float uLightFarClipDistance)
{
    float2 projectionUv = iProjUV.xy / iProjUV.w;
    float depth = uDepthMap.Sample(uDepthMapSampler, projectionUv).r;
    float rayDepth = saturate(
        length(iPositionObject - uLightPosition) / uLightFarClipDistance);
    oColor = depth < rayDepth
        ? float4(0.0, 0.0, 0.0, 1.0)
        : float4(0.0, 0.0, 0.1, 1.0);
}
)HYDRAX_HLSL";
        }
        if (objectsIntersections)
        {
            return R"HYDRAX_HLSL(
Texture2D uDepthMap : register(t0);
SamplerState uDepthMapSampler : register(s0);

void main_fp(
    float3 iPositionObject : TEXCOORD0,
    float4 iProjUV : TEXCOORD1,
    out float4 oColor : SV_Target,
    uniform float3 uLightPosition,
    uniform float uLightFarClipDistance)
{
    float2 projectionUv = iProjUV.xy / iProjUV.w;
    float depth = uDepthMap.Sample(uDepthMapSampler, projectionUv).r;
    float rayDepth = saturate(
        length(iPositionObject - uLightPosition) / uLightFarClipDistance);
    oColor = depth < rayDepth
        ? float4(0.0, 0.0, 0.0, 1.0)
        : float4(0.0, 0.1, 0.0, 1.0);
}
)HYDRAX_HLSL";
        }
        if (causticsDepthChannel)
        {
            return R"HYDRAX_HLSL(
void main_fp(out float4 oColor : SV_Target)
{
    oColor = float4(0.0, 0.0, 0.1, 1.0);
}
)HYDRAX_HLSL";
        }

        return R"HYDRAX_HLSL(
void main_fp(out float4 oColor : SV_Target)
{
    oColor = float4(0.0, 0.1, 0.0, 1.0);
}
)HYDRAX_HLSL";
    }

    inline const char* godRaysDepthVertexSource()
    {
        return R"HYDRAX_HLSL(
void main_vp(
    float4 iPosition : POSITION,
    out float4 oPosition : SV_Position,
    out float3 oPositionObject : TEXCOORD0,
    uniform float4x4 uWorld,
    uniform float4x4 uWorldViewProj)
{
    oPosition = mul(uWorldViewProj, iPosition);
    oPositionObject = mul(uWorld, iPosition).xyz;
}
)HYDRAX_HLSL";
    }

    inline const char* godRaysDepthFragmentSource()
    {
        return R"HYDRAX_HLSL(
void main_fp(
    float3 iPositionObject : TEXCOORD0,
    out float4 oColor : SV_Target,
    uniform float3 uLightPosition,
    uniform float uLightFarClipDistance)
{
    float depth = saturate(
        length(iPositionObject - uLightPosition) / uLightFarClipDistance);
    oColor = float4(depth, 0.0, 0.0, 0.0);
}
)HYDRAX_HLSL";
    }
}
}

#endif
