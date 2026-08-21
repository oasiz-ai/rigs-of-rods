#version ogre_glsl_ver_330

// RoR shadow-protected auto-exposure metering. See RoRMetering.material and
// the Metal sibling for the full contract: upstream DownScale01_SumLumStart
// with each sample's log luminance winsorized at meteringCeiling.

vulkan_layout( location = 0 )
out float fragColour;

vulkan_layout( location = 0 )
in block
{
	vec2 uv0;
} inPs;

const vec2 c_offsets[16] = vec2[16]
(
	vec2( 0, 0 ), vec2( 1, 0 ), vec2( 0, 1 ), vec2( 1, 1 ),
	vec2( 2, 0 ), vec2( 3, 0 ), vec2( 2, 1 ), vec2( 3, 1 ),
	vec2( 0, 2 ), vec2( 1, 2 ), vec2( 0, 3 ), vec2( 1, 3 ),
	vec2( 2, 2 ), vec2( 3, 2 ), vec2( 2, 3 ), vec2( 3, 3 )
);

//Luminance coefficient taken from the DX SDK Docs
const vec3 c_luminanceCoeffs = vec3(0.2125f, 0.7154f, 0.0721f);

vulkan_layout( ogre_t0 ) uniform texture2D rt0;
vulkan( layout( ogre_s0 ) uniform sampler samplerState );

vulkan( layout( ogre_P0 ) uniform Params { )
	uniform vec4 tex0Size;
	uniform vec4 viewportSize;
	uniform float meteringCeiling;
vulkan( }; )

void main()
{
	//(ViewportResolution / TargetResolution) / 4
	vec2 ratio = tex0Size.xy * viewportSize.zw * 0.25;

	vec3 vSample	= texture( vkSampler2D( rt0, samplerState ), inPs.uv0 ).xyz;
	float sampleLum	= dot( vSample, c_luminanceCoeffs ) + 0.0001;
	float fLogLuminance = min( log( sampleLum * 1024.0 ), meteringCeiling );

	for( int i=1; i<16; ++i )
	{
		vSample		= texture( vkSampler2D( rt0, samplerState ),
							   inPs.uv0 + ((c_offsets[i] * ratio) * tex0Size.zw) ).xyz;
		sampleLum	= dot( vSample, c_luminanceCoeffs ) + 0.0001;
		fLogLuminance += min( log( sampleLum * 1024.0 ), meteringCeiling );
	}

	fLogLuminance *= 0.0625; // /= 16.0;

	fragColour = fLogLuminance;
}
