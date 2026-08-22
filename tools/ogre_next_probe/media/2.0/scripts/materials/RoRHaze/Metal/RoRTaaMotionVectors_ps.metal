#include <metal_stdlib>
using namespace metal;

// RoR temporal AA: camera-reprojection motion vectors.
// See RoRTemporalAa.material for the pass contract.
//
// One full-screen quad after the single-evaluation HDR scene pass. It
// consumes only the scene's exported D32 opaque depth and this frame's
// reprojection constants, and writes previous-pixel-minus-current-pixel
// motion in output pixels (+X right, +Y down) with temporal jitter removed
// (kOgreNextTaaMotionVectorConvention). Soft-body vehicles deform every
// vertex every frame, so this camera-only reprojection is exact for the
// static world and deliberately wrong for deforming geometry; the resolve
// pass rejects that geometry through its previous-depth disocclusion test
// and variance clipping instead of ghosting it.
//
// taaReproject0..3 = rows of M = VP_prev * inverse(VP_cur), both projections
//                    unjittered, in the portable non-reversed [0, 1] depth
//                    convention (kOgreNextTaaDepthConvention).
// taaJitter        = (jitter_x, jitter_y, previous_jitter_x,
//                    previous_jitter_y) in pixels, +X right and +Y down.
// taaExtent        = (width, height, 1 / width, 1 / height).
//
// Fail-closed rules:
//  * A non-positive reprojected clip w means the point falls behind the
//    previous camera; motion is exactly (0, 0), which the contract defines
//    as "newly visible or unavailable motion".
//  * Every arithmetic input is a readback-verified finite constant or a
//    [0, 1] depth sample, so no operation here can manufacture a NaN from
//    in-contract inputs.

struct PS_INPUT
{
	float2 uv0;
};

struct Params
{
	float4 taaReproject0;
	float4 taaReproject1;
	float4 taaReproject2;
	float4 taaReproject3;
	float4 taaJitter;
	float4 taaExtent;
};

fragment float4 main_metal
(
	PS_INPUT inPs [[stage_in]],
	depth2d<float>		depthTexture	[[texture(0)]],
	sampler				samplerDepth	[[sampler(0)]],

	constant Params &p [[buffer(PARAMETER_SLOT)]]
)
{
	float fDepth = depthTexture.sample( samplerDepth, inPs.uv0 );

	float2 pixelCur = inPs.uv0 * p.taaExtent.xy;
	float2 pixelCurUnjittered = pixelCur - p.taaJitter.xy;
	float2 ndcUnjittered = float2(
			pixelCurUnjittered.x * p.taaExtent.z * 2.0f - 1.0f,
			1.0f - pixelCurUnjittered.y * p.taaExtent.w * 2.0f );

	float4 current = float4( ndcUnjittered.x, ndcUnjittered.y, fDepth, 1.0f );
	float4 previousClip = float4(
			dot( p.taaReproject0, current ),
			dot( p.taaReproject1, current ),
			dot( p.taaReproject2, current ),
			dot( p.taaReproject3, current ) );
	if( !(previousClip.w > 1.0e-8f) )
		return float4( 0.0f, 0.0f, 0.0f, 0.0f );

	float2 previousNdc = previousClip.xy / previousClip.w;
	float2 previousPixel = float2(
			( previousNdc.x * 0.5f + 0.5f ) * p.taaExtent.x,
			( 0.5f - previousNdc.y * 0.5f ) * p.taaExtent.y );

	float2 motion = previousPixel - pixelCurUnjittered;
	return float4( motion.x, motion.y, 0.0f, 0.0f );
}
