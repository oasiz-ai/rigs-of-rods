
// RoR temporal AA: history resolve. See RoRTemporalAa.material for the
// contract and Metal/RoRTaaResolve_ps.metal for the full derivation
// comment. This source is the HLSL sibling of that shader.

Texture2D<float4> rt0				: register(t0);
SamplerState samplerPoint			: register(s0);

Texture2D<float> depthTexture		: register(t1);
SamplerState samplerDepth			: register(s1);

Texture2D<float2> motionTexture		: register(t2);
SamplerState samplerMotion			: register(s2);

Texture2D<float> prevDepthTexture	: register(t3);
SamplerState samplerPrevDepth		: register(s3);

Texture2D<float4> historyTexture	: register(t4);
SamplerState samplerHistory			: register(s4);

Texture2D<float> reactiveTexture	: register(t5);
SamplerState samplerReactive		: register(s5);

float3 RoRTaaToYCoCg( float3 colour )
{
	return float3(
			0.25f * colour.x + 0.5f * colour.y + 0.25f * colour.z,
			0.5f * colour.x - 0.5f * colour.z,
			0.5f * colour.y - 0.25f * colour.x - 0.25f * colour.z );
}

float3 RoRTaaFromYCoCg( float3 colour )
{
	return float3( colour.x + colour.y - colour.z,
				   colour.x + colour.z,
				   colour.x - colour.y - colour.z );
}

float3 RoRTaaClampHdr( float3 colour )
{
	return clamp( colour, 0.0f, 65504.0f );
}

float4 main
(
	in float2 uv : TEXCOORD0,
	uniform float4 taaReproject0,
	uniform float4 taaReproject1,
	uniform float4 taaReproject2,
	uniform float4 taaReproject3,
	uniform float4 taaJitter,
	uniform float4 taaExtent,
	uniform float4 taaBlend,
	uniform float4 taaDepthPolicy
) : SV_Target
{
	float4 vCentre = rt0.Sample( samplerPoint, uv );
	if( !(taaDepthPolicy.z > 0.5f) )
		return float4( RoRTaaClampHdr( vCentre.xyz ), 1.0f );

	float fDepth = depthTexture.Sample( samplerDepth, uv ).x;
	float2 motion = motionTexture.Sample( samplerMotion, uv ).xy;
	float motionLength = length( motion );
	float reactive = reactiveTexture.Sample( samplerReactive, uv ).x;

	float2 pixelCur = uv * taaExtent.xy;
	float2 pixelCurUnjittered = pixelCur - taaJitter.xy;
	float2 previousPixel = pixelCurUnjittered + motion;
	float2 previousUv = previousPixel * taaExtent.zw;

	float2 ndcUnjittered = float2(
			pixelCurUnjittered.x * taaExtent.z * 2.0f - 1.0f,
			1.0f - pixelCurUnjittered.y * taaExtent.w * 2.0f );
	float4 current = float4( ndcUnjittered.x, ndcUnjittered.y, fDepth, 1.0f );
	float4 previousClip = float4(
			dot( taaReproject0, current ),
			dot( taaReproject1, current ),
			dot( taaReproject2, current ),
			dot( taaReproject3, current ) );

	bool offscreen = previousUv.x < 0.0f || previousUv.x > 1.0f ||
					 previousUv.y < 0.0f || previousUv.y > 1.0f ||
					 !(previousClip.w > 1.0e-8f);
	if( offscreen )
		return float4( RoRTaaClampHdr( vCentre.xyz ), 1.0f );

	float expectedPreviousDepth =
			clamp( previousClip.z / previousClip.w, 0.0f, 1.0f );
	float storedPreviousDepth =
			prevDepthTexture.Sample( samplerPrevDepth, previousUv ).x;
	float depthError = abs( expectedPreviousDepth - storedPreviousDepth );
	float depthTolerance = taaDepthPolicy.x +
			taaDepthPolicy.y *
					max( expectedPreviousDepth, storedPreviousDepth );

	bool depthRejected = depthError > depthTolerance;
	bool motionRejected = motionLength >= taaBlend.z;
	bool reactiveRejected = reactive >= 1.0f;
	if( depthRejected || motionRejected || reactiveRejected )
		return float4( RoRTaaClampHdr( vCentre.xyz ), 1.0f );

	float3 neighbourhood[9];
	float3 minimum = float3( 65504.0f, 65504.0f, 65504.0f );
	float3 maximum = float3( -65504.0f, -65504.0f, -65504.0f );
	float3 colourSum = float3( 0.0f, 0.0f, 0.0f );
	int neighbourIndex = 0;
	for( int y = -1; y <= 1; ++y )
	{
		for( int x = -1; x <= 1; ++x )
		{
			float2 offsetUv = uv +
					float2( float( x ), float( y ) ) * taaExtent.zw;
			float3 neighbour = RoRTaaToYCoCg(
					rt0.Sample( samplerPoint, offsetUv ).xyz );
			neighbourhood[neighbourIndex] = neighbour;
			++neighbourIndex;
			minimum = min( minimum, neighbour );
			maximum = max( maximum, neighbour );
			colourSum += neighbour;
		}
	}
	float3 mean = colourSum / 9.0f;
	float3 squaredError = float3( 0.0f, 0.0f, 0.0f );
	for( int index = 0; index < 9; ++index )
	{
		float3 difference = neighbourhood[index] - mean;
		squaredError += difference * difference;
	}
	float3 varianceRadius = taaBlend.y * sqrt( squaredError / 9.0f );
	float3 lower = max( minimum, mean - varianceRadius );
	float3 upper = min( maximum, mean + varianceRadius );

	float3 history = historyTexture.Sample( samplerHistory, previousUv ).xyz;
	float3 rescaledHistory = history * taaBlend.w;
	float3 clippedHistory = RoRTaaFromYCoCg(
			clamp( RoRTaaToYCoCg( rescaledHistory ), lower, upper ) );

	float motionFactor = max( 0.0f, 1.0f - motionLength / taaBlend.z );
	float weight = taaBlend.x * ( 1.0f - reactive ) * motionFactor;

	float3 resolved = RoRTaaClampHdr(
			vCentre.xyz * ( 1.0f - weight ) +
			RoRTaaClampHdr( clippedHistory ) * weight );
	return float4( resolved, 1.0f );
}
