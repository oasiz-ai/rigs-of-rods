
// RoR temporal AA: camera-reprojection motion vectors. See
// RoRTemporalAa.material for the contract and
// Metal/RoRTaaMotionVectors_ps.metal for the full derivation comment. This
// source is the HLSL sibling of that shader.

Texture2D<float> depthTexture	: register(t0);
SamplerState samplerDepth		: register(s0);

float4 main
(
	in float2 uv : TEXCOORD0,
	uniform float4 taaReproject0,
	uniform float4 taaReproject1,
	uniform float4 taaReproject2,
	uniform float4 taaReproject3,
	uniform float4 taaJitter,
	uniform float4 taaExtent
) : SV_Target
{
	float fDepth = depthTexture.Sample( samplerDepth, uv ).x;

	float2 pixelCur = uv * taaExtent.xy;
	float2 pixelCurUnjittered = pixelCur - taaJitter.xy;
	float2 ndcUnjittered = float2(
			pixelCurUnjittered.x * taaExtent.z * 2.0f - 1.0f,
			1.0f - pixelCurUnjittered.y * taaExtent.w * 2.0f );

	float4 current = float4( ndcUnjittered.x, ndcUnjittered.y, fDepth, 1.0f );
	float4 previousClip = float4(
			dot( taaReproject0, current ),
			dot( taaReproject1, current ),
			dot( taaReproject2, current ),
			dot( taaReproject3, current ) );
	if( !(previousClip.w > 1.0e-8f) )
		return float4( 0.0f, 0.0f, 0.0f, 0.0f );

	float2 previousNdc = previousClip.xy / previousClip.w;
	float2 previousPixel = float2(
			( previousNdc.x * 0.5f + 0.5f ) * taaExtent.x,
			( 0.5f - previousNdc.y * 0.5f ) * taaExtent.y );

	float2 motion = previousPixel - pixelCurUnjittered;
	return float4( motion.x, motion.y, 0.0f, 0.0f );
}
