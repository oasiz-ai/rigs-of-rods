#include <metal_stdlib>
using namespace metal;

// RoR stage-3 shade blur. See RoRScreenShade.material for the pass contract.
//
// One separable 9-tap Gaussian pass over the (AO, sun visibility) shade
// buffer, weighted per tap by the relative linear-depth difference the AO
// pass stored in .z so occlusion never bleeds across silhouettes. The same
// program serves the horizontal and vertical materials; only the bound
// direction differs.
//
// shadeBlurParams = (step u, step v, depth reject scale, unused). A zero
// direction makes every tap resample the centre texel, which the normalized
// weights resolve to the exact centre value - the canonical identity
// binding is therefore a pass-through, not an approximation.

struct PS_INPUT
{
	float2 uv0;
};

struct Params
{
	float4 shadeBlurParams;
};

constant float kWeights[5] = { 0.2270270270f, 0.1945945946f, 0.1216216216f,
							   0.0540540541f, 0.0162162162f };

fragment float4 main_metal
(
	PS_INPUT inPs [[stage_in]],
	texture2d<float>	shadeTexture	[[texture(0)]],
	sampler				samplerPoint	[[sampler(0)]],

	constant Params &p [[buffer(PARAMETER_SLOT)]]
)
{
	float4 centre = shadeTexture.sample( samplerPoint, inPs.uv0 );
	float centreDepth = max( centre.z, 1.0e-3f );
	float rejectScale = p.shadeBlurParams.z;

	float2 accum = centre.xy * kWeights[0];
	float weightSum = kWeights[0];
	for( int i = 1; i < 5; ++i )
	{
		float2 offset = p.shadeBlurParams.xy * float( i );
		for( int s = -1; s <= 1; s += 2 )
		{
			float2 uvS = inPs.uv0 + offset * float( s );
			if( uvS.x < 0.0f || uvS.x > 1.0f ||
				uvS.y < 0.0f || uvS.y > 1.0f )
				continue;
			float4 tap = shadeTexture.sample( samplerPoint, uvS );
			float relative =
				fabs( tap.z - centre.z ) / centreDepth;
			float weight = kWeights[i] /
						   ( 1.0f + rejectScale * relative );
			accum += tap.xy * weight;
			weightSum += weight;
		}
	}

	return float4( accum / weightSum, centre.z, 1.0f );
}
