#include <metal_stdlib>
using namespace metal;

// RoR shadow-protected auto-exposure metering. See RoRMetering.material.
//
// Byte-for-byte the pinned upstream DownScale01_SumLumStart_ps with one
// change: each sample's log luminance is winsorized at meteringCeiling
// (natural log of luminance * 1024) before it joins the frame's log-average.
// Extreme emitters - the sun disc at 24x sun colour, mirror-like specular
// glints - therefore contribute at most the ceiling instead of dragging the
// whole average up and crushing shadow detail. Ordinary scene content below
// the ceiling meters exactly as upstream, so a frame with no outliers is
// numerically unchanged.
//
// The presenter binds meteringCeiling every frame with a _readRawConstants
// readback, so a silent binding drift fails closed there. The CPU exposure
// oracle is unaffected: it consumes the reduced log-average read back from
// the GPU, not a re-derivation of this pass.

//Morton Order. Table generated in Python:
//def CompactBy1( x ):
//	x &= 0x55555555
//	x = (x ^ (x >>  1)) & 0x33333333
//	x = (x ^ (x >>  2)) & 0x0f0f0f0f
//	x = (x ^ (x >>  4)) & 0x00ff00ff
//	x = (x ^ (x >>  8)) & 0x0000ffff
//	return x
//def printMorton( val ):
//	x = CompactBy1( val )
//	y = CompactBy1( val >> 1 )
//	print( "\tfloat2( %s, %s )," % (x, y) )
//
//for x in range(0, 16):
//	printMorton(x)

constexpr constant float2 c_offsets[16] =
{
	float2( 0, 0 ), float2( 1, 0 ), float2( 0, 1 ), float2( 1, 1 ),
	float2( 2, 0 ), float2( 3, 0 ), float2( 2, 1 ), float2( 3, 1 ),
	float2( 0, 2 ), float2( 1, 2 ), float2( 0, 3 ), float2( 1, 3 ),
	float2( 2, 2 ), float2( 3, 2 ), float2( 2, 3 ), float2( 3, 3 )
};

//Luminance coefficient taken from the DX SDK Docs
constexpr constant float3 c_luminanceCoeffs = float3(0.2125f, 0.7154f, 0.0721f);

struct PS_INPUT
{
	float2 uv0;
};

struct Params
{
	float4 tex0Size;
	float4 viewportSize;
	float meteringCeiling;
};

fragment float4 main_metal
(
	PS_INPUT inPs [[stage_in]],
	texture2d<float>	rt0				[[texture(0)]],
	sampler				samplerState	[[sampler(0)]],

	constant Params &p [[buffer(PARAMETER_SLOT)]]
)
{
	//Compute how many pixels we have to skip because we can't sample them all
	//e.g we have a 4096x4096 viewport (rt0), and we're rendering to a 64x64 surface
	//We would need 64x64 samples, but we only sample 4x4, therefore we sample one
	//pixel and skip 15, then repeat. We perform:
	//(ViewportResolution / TargetResolution) / 4
	float2 ratio = p.tex0Size.xy * p.viewportSize.zw * 0.25f;

	float3 vSample	= rt0.sample( samplerState, inPs.uv0 ).xyz;
	float sampleLum	= dot( vSample, c_luminanceCoeffs ) + 0.0001f;
	float fLogLuminance = min( log( sampleLum * 1024.0f ), p.meteringCeiling );

	for( int i=1; i<16; ++i )
	{
		vSample		= rt0.sample( samplerState, inPs.uv0 + ((c_offsets[i] * ratio) * p.tex0Size.zw) ).xyz;
		sampleLum	= dot( vSample, c_luminanceCoeffs ) + 0.0001f;
		fLogLuminance += min( log( sampleLum * 1024.0f ), p.meteringCeiling );
	}

	fLogLuminance *= 0.0625f; // /= 16.0f;

	return fLogLuminance;
}
