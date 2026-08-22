#include <metal_stdlib>
using namespace metal;

// RoR temporal AA: depth history store.
// See RoRTemporalAa.material for the pass contract.
//
// One full-screen quad after the resolve pass has consumed the previous
// frame's stored depth. It copies this frame's D32 opaque depth into the
// persistent R32 history target, in the shared portable non-reversed
// [0, 1] convention, so the next frame's resolve can run its disocclusion
// test. A pure point copy: any filtering here would blend across
// silhouettes and manufacture false depth agreements at edges.

struct PS_INPUT
{
	float2 uv0;
};

fragment float4 main_metal
(
	PS_INPUT inPs [[stage_in]],
	depth2d<float>		depthTexture	[[texture(0)]],
	sampler				samplerDepth	[[sampler(0)]]
)
{
	float fDepth = depthTexture.sample( samplerDepth, inPs.uv0 );
	return float4( fDepth, 0.0f, 0.0f, 0.0f );
}
