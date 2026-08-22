#include <metal_stdlib>
using namespace metal;

// RoR stage-3 shade apply. See RoRScreenShade.material for the pass contract.
//
// Modulates the linear HDR scene radiance by the blurred shade terms using
// the per-pixel indirect-luminance fraction the PBS indirect-alpha piece
// wrote into the scene alpha:
//
//     rgb *= a * AO + (1 - a) * sunVisibility
//
// so ambient occlusion attenuates exactly the indirect share of the pixel
// and the screen-space sun contact shadow attenuates exactly the direct
// share. Alpha >= 1.0 is the reserved untagged value (the scene clear, the
// Unlit sky dome, emissive Unlit surfaces): those pixels return the scene
// sample bit-exactly. A shade buffer bound to the canonical identity state
// carries (1, 1), which multiplies by exactly 1.0 and is therefore also a
// bit-exact RGBA16F point copy. The output alpha is re-canonicalized to 1.0
// for every downstream consumer.

struct PS_INPUT
{
	float2 uv0;
};

fragment float4 main_metal
(
	PS_INPUT inPs [[stage_in]],
	texture2d<float>	rt0				[[texture(0)]],
	texture2d<float>	shadeTexture	[[texture(1)]],
	sampler				samplerPoint	[[sampler(0)]],
	sampler				samplerShade	[[sampler(1)]]
)
{
	float4 vSample = rt0.sample( samplerPoint, inPs.uv0 );
	if( vSample.w >= 1.0f )
		return float4( vSample.xyz, 1.0f );

	float2 shade = shadeTexture.sample( samplerShade, inPs.uv0 ).xy;
	float indirectFraction = clamp( vSample.w, 0.0f, 1.0f );
	float factor = indirectFraction * shade.x +
				   ( 1.0f - indirectFraction ) * shade.y;
	return float4( vSample.xyz * factor, 1.0f );
}
