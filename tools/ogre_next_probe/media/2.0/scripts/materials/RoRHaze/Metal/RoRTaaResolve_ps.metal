#include <metal_stdlib>
using namespace metal;

// RoR temporal AA: history resolve.
// See RoRTemporalAa.material for the pass contract.
//
// This shader is the GPU sibling of the CPU oracle
// EvaluateOgreNextTaaPixel (OgreNextTaaContract.cpp): the same YCoCg
// variance neighbourhood clipping, previous-depth disocclusion, motion
// rejection, reactive rejection, pre-exposure rescale, and [0, 65504]
// HDR clamps, in the same operation order. The oracle validates the math
// on synthetic pixels; this shader applies it to the live frame.
//
// Inputs (all at the same extent):
//  rt0              linear pre-tonemap HDR scene colour, this frame,
//                   rendered with the jittered projection
//  depthTexture     this frame's D32 opaque depth (portable non-reversed
//                   [0, 1] convention; sky is exactly 1.0)
//  motionTexture    previous-pixel-minus-current-pixel motion in pixels,
//                   jitter removed (RoRTaaMotionVectors_ps)
//  prevDepthTexture last frame's depth, stored by RoRTaaDepthStore_ps
//  historyTexture   last frame's resolved output (centered, unjittered),
//                   sampled bilinearly at the reprojected position
//  reactiveTexture  reactive coverage in [0, 1]; 1 forces full rejection
//
// Constants:
//  taaReproject0..3 rows of M = VP_prev * inverse(VP_cur), unjittered
//  taaJitter        (jitter_x, jitter_y, prev_jitter_x, prev_jitter_y) px
//  taaExtent        (width, height, 1 / width, 1 / height)
//  taaBlend         (history_weight, variance_clip_gamma,
//                    full_motion_rejection_pixels, history_exposure_ratio)
//  taaDepthPolicy   (disocclusion_absolute_depth,
//                    disocclusion_relative_depth, history_available, 0)
//
// Fail-closed rules, in the order they are tested:
//  * history_available == 0 (initial frame, camera cut, extent change, or
//    a CPU-side degrade) returns the current sample: a reset frame is the
//    current frame, never a stale blend.
//  * A reprojection that leaves the previous viewport, lands behind the
//    previous camera, fails the depth tolerance, moves at or beyond the
//    full-motion limit, or carries reactive == 1 also returns the current
//    sample. Deforming soft-body geometry takes these paths instead of
//    ghosting.
//  * The blended result is clamped to the finite RGBA16 range with alpha
//    exactly one, so no in-contract input can propagate a non-finite or
//    out-of-range pixel into the history stream.

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
	float4 taaBlend;
	float4 taaDepthPolicy;
};

inline float3 RoRTaaToYCoCg( float3 colour )
{
	return float3(
			0.25f * colour.x + 0.5f * colour.y + 0.25f * colour.z,
			0.5f * colour.x - 0.5f * colour.z,
			0.5f * colour.y - 0.25f * colour.x - 0.25f * colour.z );
}

inline float3 RoRTaaFromYCoCg( float3 colour )
{
	return float3( colour.x + colour.y - colour.z,
				   colour.x + colour.z,
				   colour.x - colour.y - colour.z );
}

inline float3 RoRTaaClampHdr( float3 colour )
{
	return clamp( colour, 0.0f, 65504.0f );
}

fragment float4 main_metal
(
	PS_INPUT inPs [[stage_in]],
	texture2d<float>	rt0					[[texture(0)]],
	depth2d<float>		depthTexture		[[texture(1)]],
	texture2d<float>	motionTexture		[[texture(2)]],
	texture2d<float>	prevDepthTexture	[[texture(3)]],
	texture2d<float>	historyTexture		[[texture(4)]],
	texture2d<float>	reactiveTexture		[[texture(5)]],
	sampler				samplerPoint		[[sampler(0)]],
	sampler				samplerDepth		[[sampler(1)]],
	sampler				samplerMotion		[[sampler(2)]],
	sampler				samplerPrevDepth	[[sampler(3)]],
	sampler				samplerHistory		[[sampler(4)]],
	sampler				samplerReactive		[[sampler(5)]],

	constant Params &p [[buffer(PARAMETER_SLOT)]]
)
{
	float4 vCentre = rt0.sample( samplerPoint, inPs.uv0 );
	if( !(p.taaDepthPolicy.z > 0.5f) )
		return float4( RoRTaaClampHdr( vCentre.xyz ), 1.0f );

	float fDepth = depthTexture.sample( samplerDepth, inPs.uv0 );
	float2 motion = motionTexture.sample( samplerMotion, inPs.uv0 ).xy;
	float motionLength = length( motion );
	float reactive = reactiveTexture.sample( samplerReactive, inPs.uv0 ).x;

	float2 pixelCur = inPs.uv0 * p.taaExtent.xy;
	float2 pixelCurUnjittered = pixelCur - p.taaJitter.xy;
	float2 previousPixel = pixelCurUnjittered + motion;
	float2 previousUv = previousPixel * p.taaExtent.zw;

	float2 ndcUnjittered = float2(
			pixelCurUnjittered.x * p.taaExtent.z * 2.0f - 1.0f,
			1.0f - pixelCurUnjittered.y * p.taaExtent.w * 2.0f );
	float4 current = float4( ndcUnjittered.x, ndcUnjittered.y, fDepth, 1.0f );
	float4 previousClip = float4(
			dot( p.taaReproject0, current ),
			dot( p.taaReproject1, current ),
			dot( p.taaReproject2, current ),
			dot( p.taaReproject3, current ) );

	bool offscreen = previousUv.x < 0.0f || previousUv.x > 1.0f ||
					 previousUv.y < 0.0f || previousUv.y > 1.0f ||
					 !(previousClip.w > 1.0e-8f);
	if( offscreen )
		return float4( RoRTaaClampHdr( vCentre.xyz ), 1.0f );

	// Disocclusion: the surface's expected previous-frame depth against the
	// depth that was actually stored there last frame, in the shared
	// non-reversed [0, 1] convention.
	float expectedPreviousDepth =
			clamp( previousClip.z / previousClip.w, 0.0f, 1.0f );
	float storedPreviousDepth =
			prevDepthTexture.sample( samplerPrevDepth, previousUv ).x;
	float depthError = abs( expectedPreviousDepth - storedPreviousDepth );
	float depthTolerance = p.taaDepthPolicy.x +
			p.taaDepthPolicy.y *
					max( expectedPreviousDepth, storedPreviousDepth );

	bool depthRejected = depthError > depthTolerance;
	bool motionRejected = motionLength >= p.taaBlend.z;
	bool reactiveRejected = reactive >= 1.0f;
	if( depthRejected || motionRejected || reactiveRejected )
		return float4( RoRTaaClampHdr( vCentre.xyz ), 1.0f );

	// YCoCg 3x3 neighbourhood statistics for variance clipping, using the
	// oracle's two-pass mean/squared-error form rather than the
	// E[x^2] - E[x]^2 shortcut, whose cancellation the oracle refuses.
	float3 neighbourhood[9];
	float3 minimum = float3( 65504.0f, 65504.0f, 65504.0f );
	float3 maximum = float3( -65504.0f, -65504.0f, -65504.0f );
	float3 sum = float3( 0.0f, 0.0f, 0.0f );
	int neighbourIndex = 0;
	for( int y = -1; y <= 1; ++y )
	{
		for( int x = -1; x <= 1; ++x )
		{
			float2 offsetUv = inPs.uv0 +
					float2( float( x ), float( y ) ) * p.taaExtent.zw;
			float3 neighbour = RoRTaaToYCoCg(
					rt0.sample( samplerPoint, offsetUv ).xyz );
			neighbourhood[neighbourIndex] = neighbour;
			++neighbourIndex;
			minimum = min( minimum, neighbour );
			maximum = max( maximum, neighbour );
			sum += neighbour;
		}
	}
	float3 mean = sum / 9.0f;
	float3 squaredError = float3( 0.0f, 0.0f, 0.0f );
	for( int index = 0; index < 9; ++index )
	{
		float3 difference = neighbourhood[index] - mean;
		squaredError += difference * difference;
	}
	float3 varianceRadius = p.taaBlend.y * sqrt( squaredError / 9.0f );
	float3 lower = max( minimum, mean - varianceRadius );
	float3 upper = min( maximum, mean + varianceRadius );

	float3 history = historyTexture.sample( samplerHistory, previousUv ).xyz;
	float3 rescaledHistory = history * p.taaBlend.w;
	float3 clippedHistory = RoRTaaFromYCoCg(
			clamp( RoRTaaToYCoCg( rescaledHistory ), lower, upper ) );

	float motionFactor = max( 0.0f, 1.0f - motionLength / p.taaBlend.z );
	float weight = p.taaBlend.x * ( 1.0f - reactive ) * motionFactor;

	float3 resolved = RoRTaaClampHdr(
			vCentre.xyz * ( 1.0f - weight ) +
			RoRTaaClampHdr( clippedHistory ) * weight );
	return float4( resolved, 1.0f );
}
