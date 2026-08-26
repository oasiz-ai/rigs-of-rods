/*
--------------------------------------------------------------------------------
This source file is part of SkyX.
Visit http://www.paradise-studios.net/products/skyx/

Copyright (C) 2009-2012 Xavier Verguín González <xavyiy@gmail.com>

This program is free software; you can redistribute it and/or modify it under
the terms of the GNU Lesser General Public License as published by the Free Software
Foundation; either version 2 of the License, or (at your option) any later
version.

This program is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public License along with
this program; if not, write to the Free Software Foundation, Inc., 59 Temple
Place - Suite 330, Boston, MA 02111-1307, USA, or go to
http://www.gnu.org/copyleft/lesser.txt.
--------------------------------------------------------------------------------
*/

// --------------------- SkyX moon material ------------------------

// ---------------------------- HLSL -------------------------------

Texture2D uMoon     : register(t0);
Texture2D uMoonHalo : register(t1);

SamplerState uMoonSampler     : register(s0);
SamplerState uMoonHaloSampler : register(s1);

void main_vp(
    // IN
	float4 iPosition	        : POSITION,
	float2 iUV                  : TEXCOORD0,
	// OUT
	out float4 oPosition		: SV_Position,
	out float4 oUVYLength       : TEXCOORD0,
	// UNIFORM
	uniform float4x4 uWorldViewProj,
	uniform float4x4 uWorld,
	uniform float3   uSkydomeCenter)
{
    // Clip space position
	oPosition   = mul(uWorldViewProj, iPosition);
	// World position
	float3 ObjectSpacePosition = mul(uWorld, iPosition).xyz - uSkydomeCenter;

    // UV
    oUVYLength.xy = iUV;
    // Y
    oUVYLength.z  = ObjectSpacePosition.y;
    // Length
    oUVYLength.w  = length(ObjectSpacePosition);
}

void main_fp(
    // IN
    float4 iUVYLength       : TEXCOORD0,
	// OUT 
	out float4 oColor		: SV_Target,
	// UNIFORM
	uniform float3    uMoonPhase,
	uniform float3    uMoonHalo1,
	uniform float3    uMoonHalo2,
	uniform float     uMoonHaloFlip)
{
    // Output color
    oColor = uMoon.Sample(uMoonSampler, iUVYLength.xy);
	
	// Moon phase + halo
	float radius = abs(uMoonPhase.x);
	float2 center = float2(uMoonPhase.y, 0.5);
	float dist = length(iUVYLength.xy - center);
	float att = saturate((radius-dist+0.015)*40);
	
	if (uMoonHaloFlip > 0.5)
	{
		iUVYLength.x = 1-iUVYLength.x;
	}
	
	float2 haloUV = float2(iUVYLength.x/4, iUVYLength.y/2);
	float2 halo1UV = float2(uMoonHalo1.x + haloUV.x, uMoonHalo1.y + haloUV.y);
	float2 halo2UV = float2(uMoonHalo2.x + haloUV.x, uMoonHalo2.y + haloUV.y);
	
	float haloIntensity = uMoonHalo.Sample(uMoonHaloSampler, halo1UV).w*uMoonHalo1.z + uMoonHalo.Sample(uMoonHaloSampler, halo2UV).w*uMoonHalo2.z;
	
	haloIntensity = pow(haloIntensity, uMoonPhase.z);
	
	if (uMoonPhase.x > 0)
	{
		oColor.rgb *= 0.16 + (1-0.16)*(1-att);
		oColor.rgb += saturate(haloIntensity-oColor.r)*(1-(1-att)*oColor.a);
	}
	else
	{
		oColor.rgb *= 0.16 + (1-0.16)*att;
		oColor.rgb += saturate(haloIntensity-oColor.r)*(1-att*oColor.a);
	}	

	oColor.rgb += (1-oColor.a)*1.4*(1-pow(oColor.a,2*haloIntensity)); // Anti-alias at moon edges hack
	oColor.rgb = saturate(oColor.rgb);
	
	oColor.a = max(oColor.a, haloIntensity);
	
	// Transparency at horizon
	oColor.w *= saturate((iUVYLength.z/iUVYLength.w)*10);
}
