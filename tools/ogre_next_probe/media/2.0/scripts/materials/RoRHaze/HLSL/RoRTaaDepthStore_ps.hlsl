
// RoR temporal AA: depth history store. See RoRTemporalAa.material for the
// contract and Metal/RoRTaaDepthStore_ps.metal for the derivation comment.
// This source is the HLSL sibling of that shader.

Texture2D<float> depthTexture	: register(t0);
SamplerState samplerDepth		: register(s0);

float4 main
(
	in float2 uv : TEXCOORD0
) : SV_Target
{
	float fDepth = depthTexture.Sample( samplerDepth, uv ).x;
	return float4( fDepth, 0.0f, 0.0f, 0.0f );
}
