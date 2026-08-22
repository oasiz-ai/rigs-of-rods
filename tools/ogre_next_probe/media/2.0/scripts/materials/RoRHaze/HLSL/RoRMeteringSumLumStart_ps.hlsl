
// RoR shadow-protected auto-exposure metering. See RoRMetering.material and
// the Metal sibling for the full contract: upstream DownScale01_SumLumStart
// with each sample's log luminance winsorized at meteringCeiling.

static const float2 c_offsets[16] =
{
	float2( 0, 0 ), float2( 1, 0 ), float2( 0, 1 ), float2( 1, 1 ),
	float2( 2, 0 ), float2( 3, 0 ), float2( 2, 1 ), float2( 3, 1 ),
	float2( 0, 2 ), float2( 1, 2 ), float2( 0, 3 ), float2( 1, 3 ),
	float2( 2, 2 ), float2( 3, 2 ), float2( 2, 3 ), float2( 3, 3 )
};

//Luminance coefficient taken from the DX SDK Docs
static const float3 c_luminanceCoeffs = float3(0.2125f, 0.7154f, 0.0721f);

Texture2D<float3> rt0		: register(t0);
SamplerState samplerState	: register(s0);

float main
(
	in float2 uv : TEXCOORD0,
	uniform float4 tex0Size,
	uniform float4 viewportSize,
	uniform float meteringCeiling
) : SV_Target
{
	//(ViewportResolution / TargetResolution) / 4
	float2 ratio = tex0Size.xy * viewportSize.zw * 0.25f;

	float3 vSample	= rt0.Sample( samplerState, uv ).xyz;
	float sampleLum	= dot( vSample, c_luminanceCoeffs ) + 0.0001f;
	float fLogLuminance = min( log( sampleLum * 1024.0f ), meteringCeiling );

	for( int i=1; i<16; ++i )
	{
		vSample		= rt0.Sample( samplerState, uv + ((c_offsets[i] * ratio) * tex0Size.zw) ).xyz;
		sampleLum	= dot( vSample, c_luminanceCoeffs ) + 0.0001f;
		fLogLuminance += min( log( sampleLum * 1024.0f ), meteringCeiling );
	}

	fLogLuminance *= 0.0625f; // /= 16.0f;

	return fLogLuminance;
}
