
// RoR aerial perspective (haze). See RoRAerialHaze.material for the contract
// and Metal/RoRAerialHaze_ps.metal for the full derivation comment. This
// source is the HLSL sibling of that shader.

Texture2D<float4> rt0			: register(t0);
SamplerState samplerPoint		: register(s0);

Texture2D<float> depthTexture	: register(t1);
SamplerState samplerDepth		: register(s1);

float4 main
(
	in float2 uv : TEXCOORD0,
	uniform float4 hazeCoefficients,
	uniform float4 hazeInscatter,
	uniform float4 hazeProjection,
	uniform float4 hazeRayForward,
	uniform float4 hazeRayRightScaled,
	uniform float4 hazeRayUpScaled
) : SV_Target
{
	float4 vSample = rt0.Sample( samplerPoint, uv );
	float fDepth = depthTexture.Sample( samplerDepth, uv ).x;

	float sigma = hazeCoefficients.x;
	if( fDepth >= 1.0f || !(sigma > 0.0f) )
		return vSample;

	float invH = hazeCoefficients.y;
	float relHeight = hazeCoefficients.z;

	float viewZ = hazeProjection.y / ( fDepth - hazeProjection.x );

	float2 ndc = float2( uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f );
	float3 ray = hazeRayForward.xyz +
				 ndc.x * hazeRayRightScaled.xyz +
				 ndc.y * hazeRayUpScaled.xyz;
	float rayLength = length( ray );
	float slant = viewZ * rayLength;
	float dirY = ray.y / rayLength;

	float heightFactor = exp( -clamp( relHeight * invH, -80.0f, 80.0f ) );
	float verticalRate = dirY * invH;
	float pathIntegral = ( abs( verticalRate ) > 1.0e-8f )
			? ( 1.0f -
				exp( -clamp( slant * verticalRate, -80.0f, 80.0f ) ) ) /
			  verticalRate
			: slant;
	float tau = clamp( sigma * heightFactor * pathIntegral, 0.0f, 80.0f );
	float transmittance = exp( -tau );

	return float4( vSample.xyz * transmittance +
				   hazeInscatter.xyz * ( 1.0f - transmittance ),
				   vSample.w );
}
