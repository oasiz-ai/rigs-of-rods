
// RoR stage-3 shade apply. See RoRScreenShade.material for the contract and
// Metal/RoRScreenShadeApply_ps.metal for the derivation comment. This source
// is the HLSL sibling of that shader.

Texture2D<float4> rt0			: register(t0);
SamplerState samplerPoint		: register(s0);

Texture2D<float4> shadeTexture	: register(t1);
SamplerState samplerShade		: register(s1);

float4 main
(
	in float2 uv : TEXCOORD0
) : SV_Target
{
	float4 vSample = rt0.Sample( samplerPoint, uv );
	if( vSample.w >= 1.0f )
		return float4( vSample.xyz, 1.0f );

	float2 shade = shadeTexture.Sample( samplerShade, uv ).xy;
	float indirectFraction = saturate( vSample.w );
	float factor = indirectFraction * shade.x +
				   ( 1.0f - indirectFraction ) * shade.y;
	return float4( vSample.xyz * factor, 1.0f );
}
