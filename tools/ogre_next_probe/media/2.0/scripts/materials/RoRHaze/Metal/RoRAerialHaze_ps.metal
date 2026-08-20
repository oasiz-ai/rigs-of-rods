#include <metal_stdlib>
using namespace metal;

// RoR aerial perspective (haze). See RoRAerialHaze.material for the contract.
//
// Single-albedo exponential-with-height atmosphere: extinction equals
// scattering, so a fully extinguished pixel converges exactly onto
// hazeInscatter, which the presenter binds as the same
// horizon_radiance * environment_intensity product the sky dome uses for its
// horizon ring. The seam is therefore closed by construction, not by a
// tolerance.
//
// Fail-closed rules, in the order they are tested:
//  * Sky pixels keep the scene node's exact 1.0 depth clear (the dome renders
//    with depth check and depth write off; particles never write depth), so
//    d >= 1.0 returns the scene sample unmodified. RGBA16F -> RGBA16F point
//    copy is bit exact.
//  * A non-positive extinction is the canonical "exactly no haze" state and
//    takes the same bit-exact early-out. The test is spelled !(sigma > 0) so a
//    NaN that somehow reached the binding also fails closed instead of
//    propagating into the frame.
//  * Every exponent argument is clamped, so out-of-contract constants can only
//    saturate transmittance toward zero. No input can produce a NaN pixel.
//
// hazeCoefficients   = (extinction /m, inverse scale height /m,
//                       camera height above the haze base in m, 0)
// hazeInscatter      = horizon_radiance * environment_intensity
// hazeProjection     = (A, B, 0, 0) with view_z = B / (d - A), non-reversed
//                      [0, 1] depth
// hazeRayForward     = camera forward, unit length
// hazeRayRightScaled = camera right  * tan(fovX / 2)
// hazeRayUpScaled    = camera up     * tan(fovY / 2)

struct PS_INPUT
{
	float2 uv0;
};

struct Params
{
	float4 hazeCoefficients;
	float4 hazeInscatter;
	float4 hazeProjection;
	float4 hazeRayForward;
	float4 hazeRayRightScaled;
	float4 hazeRayUpScaled;
};

fragment float4 main_metal
(
	PS_INPUT inPs [[stage_in]],
	texture2d<float>	rt0				[[texture(0)]],
	depth2d<float>		depthTexture	[[texture(1)]],
	sampler				samplerPoint	[[sampler(0)]],
	sampler				samplerDepth	[[sampler(1)]],

	constant Params &p [[buffer(PARAMETER_SLOT)]]
)
{
	float4 vSample = rt0.sample( samplerPoint, inPs.uv0 );
	float fDepth = depthTexture.sample( samplerDepth, inPs.uv0 );

	float sigma = p.hazeCoefficients.x;
	if( fDepth >= 1.0f || !(sigma > 0.0f) )
		return vSample;

	float invH = p.hazeCoefficients.y;
	float relHeight = p.hazeCoefficients.z;

	float viewZ = p.hazeProjection.y / ( fDepth - p.hazeProjection.x );

	float2 ndc = float2( inPs.uv0.x * 2.0f - 1.0f, 1.0f - inPs.uv0.y * 2.0f );
	float3 ray = p.hazeRayForward.xyz +
				 ndc.x * p.hazeRayRightScaled.xyz +
				 ndc.y * p.hazeRayUpScaled.xyz;
	float rayLength = length( ray );
	float slant = viewZ * rayLength;
	float dirY = ray.y / rayLength;

	// tau = sigma * integral of exp( -(h - base) * invH ) along the ray.
	// The guard is on the product, not on dirY alone: a zero inverse scale
	// height is a legal homogeneous slab and must take the level branch
	// instead of dividing by zero.
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
				   p.hazeInscatter.xyz * ( 1.0f - transmittance ),
				   vSample.w );
}
