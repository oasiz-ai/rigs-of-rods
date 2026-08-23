#include <metal_stdlib>
using namespace metal;

// RoR stage-3 screen-space shade evaluation. See RoRScreenShade.material for
// the pass contract.
//
// One fragment per shade texel derives, from the exported D32 opaque depth
// alone:
//   .x  ambient obscurance (Alchemy-style spiral estimator over a
//       depth-reconstructed view-space normal), in [0, 1], 1 = open.
//   .y  screen-space sun visibility (raymarch toward the sun with a
//       thickness heuristic), in [0, 1], 1 = lit.
//   .z  the pixel's linear view depth, carried for the depth-aware blur.
//
// Fail-closed rules:
//  * A sky texel (depth keeps the exact 1.0 clear) is (1, 1, far).
//  * Non-positive strengths, sample counts, or radii disable their term by
//    construction (the loops do not run; both channels stay exactly 1), so
//    the canonical identity binding turns the whole node into a pass-through
//    that the apply pass resolves as a bit-exact scene copy.
//  * Every division has a clamped or guarded denominator; out-of-range
//    samples contribute nothing instead of NaN.
//
// shadeProj          = (m00, m05, m08, m09) of the canonical [0,1] RH
//                      perspective projection (column-major elements 0, 5,
//                      8, 9): ndc.x = m00*x/viewZ - m08, and the inverse
//                      x = (ndc.x + m08)*viewZ/m00.
// shadeDepthLin      = (A, B, aoTargetWidth, aoTargetHeight) with
//                      viewZ = B / (d - A), non-reversed [0, 1] depth.
// shadeAoParams      = (radius m, strength, sample count, power)
// shadeAoKernel      = (max screen radius px, angle bias, 1/aoWidth,
//                      1/aoHeight)
// shadeContactParams = (ray length m, strength, step count, thickness m)
// shadeSunDirView    = view-space unit direction toward the sun, w = enable

struct PS_INPUT
{
	float2 uv0;
};

struct Params
{
	float4 shadeProj;
	float4 shadeDepthLin;
	float4 shadeAoParams;
	float4 shadeAoKernel;
	float4 shadeContactParams;
	float4 shadeSunDirView;
};

inline float LinearDepth( float fDepth, constant const Params &p )
{
	return p.shadeDepthLin.y / ( fDepth - p.shadeDepthLin.x );
}

inline float3 ViewPosition( float2 uv, float fDepth, constant const Params &p )
{
	float viewZ = LinearDepth( fDepth, p );
	float2 ndc = float2( uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f );
	return float3( ( ndc.x + p.shadeProj.z ) * viewZ / p.shadeProj.x,
				   ( ndc.y + p.shadeProj.w ) * viewZ / p.shadeProj.y,
				   -viewZ );
}

inline float InterleavedGradientNoise( float2 pixel )
{
	return fract( 52.9829189f *
				  fract( 0.06711056f * pixel.x + 0.00583715f * pixel.y ) );
}

fragment float4 main_metal
(
	PS_INPUT inPs [[stage_in]],
	depth2d<float>		depthTexture	[[texture(0)]],
	sampler				samplerDepth	[[sampler(0)]],

	constant Params &p [[buffer(PARAMETER_SLOT)]]
)
{
	float fDepth = depthTexture.sample( samplerDepth, inPs.uv0 );
	if( fDepth >= 1.0f )
		return float4( 1.0f, 1.0f, 65504.0f, 1.0f );

	float viewZ = LinearDepth( fDepth, p );
	float3 vPos = ViewPosition( inPs.uv0, fDepth, p );
	float2 invAoSize = p.shadeAoKernel.zw;
	float2 pixel = inPs.uv0 * p.shadeDepthLin.zw;
	float ign = InterleavedGradientNoise( floor( pixel ) );

	// Depth-reconstructed view-space normal: probe one texel each way per
	// axis and keep the side with the smaller depth discontinuity so
	// silhouettes do not smear the normal.
	float2 dxUv = float2( invAoSize.x, 0.0f );
	float2 dyUv = float2( 0.0f, invAoSize.y );
	float3 pRight = ViewPosition(
		inPs.uv0 + dxUv, depthTexture.sample( samplerDepth, inPs.uv0 + dxUv ), p );
	float3 pLeft = ViewPosition(
		inPs.uv0 - dxUv, depthTexture.sample( samplerDepth, inPs.uv0 - dxUv ), p );
	float3 pUp = ViewPosition(
		inPs.uv0 + dyUv, depthTexture.sample( samplerDepth, inPs.uv0 + dyUv ), p );
	float3 pDown = ViewPosition(
		inPs.uv0 - dyUv, depthTexture.sample( samplerDepth, inPs.uv0 - dyUv ), p );
	float3 ddx = ( fabs( pRight.z - vPos.z ) < fabs( vPos.z - pLeft.z ) )
					 ? ( pRight - vPos )
					 : ( vPos - pLeft );
	float3 ddy = ( fabs( pUp.z - vPos.z ) < fabs( vPos.z - pDown.z ) )
					 ? ( pUp - vPos )
					 : ( vPos - pDown );
	float3 normal = cross( ddy, ddx );
	float normalLength = length( normal );
	normal = normalLength > 1.0e-8f ? normal / normalLength
									: float3( 0.0f, 0.0f, 1.0f );
	if( dot( normal, -vPos ) < 0.0f )
		normal = -normal;

	// Ambient obscurance: bounded cosine * range-falloff spiral estimator.
	float ao = 1.0f;
	float radius = p.shadeAoParams.x;
	float aoStrength = p.shadeAoParams.y;
	int sampleCount = int( p.shadeAoParams.z );
	if( radius > 0.0f && aoStrength > 0.0f && sampleCount > 0 )
	{
		float screenRadius =
			radius * p.shadeProj.y * 0.5f * p.shadeDepthLin.w / viewZ;
		screenRadius = min( screenRadius, p.shadeAoKernel.x );
		if( screenRadius >= 1.0f )
		{
			float occlusion = 0.0f;
			for( int i = 0; i < sampleCount; ++i )
			{
				float alpha = ( float( i ) + 0.5f ) / float( sampleCount );
				float theta = alpha * 43.9822971503f +
							  ign * 6.28318530718f;
				float2 offset = float2( cos( theta ), sin( theta ) ) *
								( alpha * screenRadius );
				float2 uvS = inPs.uv0 + offset * invAoSize;
				if( uvS.x <= 0.0f || uvS.x >= 1.0f ||
					uvS.y <= 0.0f || uvS.y >= 1.0f )
					continue;
				float dS = depthTexture.sample( samplerDepth, uvS );
				if( dS >= 1.0f )
					continue;
				float3 vSample = ViewPosition( uvS, dS, p ) - vPos;
				float vv = dot( vSample, vSample );
				float vn = dot( vSample, normal ) /
						   ( sqrt( vv ) + 1.0e-4f );
				float falloff = max( 0.0f, 1.0f - vv / ( radius * radius ) );
				occlusion += max( 0.0f, vn - p.shadeAoKernel.y ) * falloff;
			}
			occlusion = aoStrength * occlusion / float( sampleCount );
			ao = pow( clamp( 1.0f - occlusion, 0.0f, 1.0f ),
					  p.shadeAoParams.w );
		}
	}

	// Screen-space sun contact shadow: march toward the sun; a sample whose
	// stored surface lies in front of the ray point within the thickness
	// window occludes it.
	float sunVisibility = 1.0f;
	float contactLength = p.shadeContactParams.x;
	float contactStrength = p.shadeContactParams.y;
	int contactSteps = int( p.shadeContactParams.z );
	if( p.shadeSunDirView.w > 0.5f && contactLength > 0.0f &&
		contactStrength > 0.0f && contactSteps > 0 )
	{
		float3 sunDir = p.shadeSunDirView.xyz;
		float occluded = 0.0f;
		for( int i = 0; i < contactSteps; ++i )
		{
			float t = contactLength * ( float( i ) + ign ) /
					  float( contactSteps );
			float3 q = vPos + sunDir * t;
			float viewZq = -q.z;
			if( viewZq < 0.05f )
				break;
			float2 ndcQ = float2(
				p.shadeProj.x * q.x / viewZq - p.shadeProj.z,
				p.shadeProj.y * q.y / viewZq - p.shadeProj.w );
			float2 uvQ = float2( ndcQ.x * 0.5f + 0.5f,
								 0.5f - ndcQ.y * 0.5f );
			if( uvQ.x <= 0.0f || uvQ.x >= 1.0f ||
				uvQ.y <= 0.0f || uvQ.y >= 1.0f )
				break;
			float dS = depthTexture.sample( samplerDepth, uvQ );
			if( dS >= 1.0f )
				continue;
			float viewZs = LinearDepth( dS, p );
			float delta = viewZq - viewZs;
			float bias = 0.02f + 0.02f * t;
			if( delta > bias &&
				delta < p.shadeContactParams.w + 0.1f * t )
			{
				occluded = 1.0f;
				break;
			}
		}
		sunVisibility = 1.0f - contactStrength * occluded;
	}

	return float4( ao, sunVisibility, min( viewZ, 65504.0f ), 1.0f );
}
