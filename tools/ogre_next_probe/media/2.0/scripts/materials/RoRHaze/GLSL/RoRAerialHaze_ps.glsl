#version ogre_glsl_ver_330

// RoR aerial perspective (haze). See RoRAerialHaze.material for the contract
// and Metal/RoRAerialHaze_ps.metal for the full derivation comment. This
// source is the byte-identical GLSL and Vulkan-GLSL sibling of that shader.

vulkan_layout( location = 0 )
out vec4 fragColour;

vulkan_layout( location = 0 )
in block
{
	vec2 uv0;
} inPs;

vulkan_layout( ogre_t0 ) uniform texture2D rt0;
vulkan( layout( ogre_s0 ) uniform sampler samplerPoint );

vulkan_layout( ogre_t1 ) uniform texture2D depthTexture;
vulkan( layout( ogre_s1 ) uniform sampler samplerDepth );

vulkan( layout( ogre_P0 ) uniform Params { )
	uniform vec4 hazeCoefficients;
	uniform vec4 hazeInscatter;
	uniform vec4 hazeProjection;
	uniform vec4 hazeRayForward;
	uniform vec4 hazeRayRightScaled;
	uniform vec4 hazeRayUpScaled;
vulkan( }; )

void main()
{
	vec4 vSample = texture( vkSampler2D( rt0, samplerPoint ), inPs.uv0 );
	float fDepth = texture( vkSampler2D( depthTexture, samplerDepth ),
							inPs.uv0 ).x;

	float sigma = hazeCoefficients.x;
	if( fDepth >= 1.0 || !(sigma > 0.0) )
	{
		fragColour = vSample;
		return;
	}

	float invH = hazeCoefficients.y;
	float relHeight = hazeCoefficients.z;

	float viewZ = hazeProjection.y / ( fDepth - hazeProjection.x );

	vec2 ndc = vec2( inPs.uv0.x * 2.0 - 1.0, 1.0 - inPs.uv0.y * 2.0 );
	vec3 ray = hazeRayForward.xyz +
			   ndc.x * hazeRayRightScaled.xyz +
			   ndc.y * hazeRayUpScaled.xyz;
	float rayLength = length( ray );
	float slant = viewZ * rayLength;
	float dirY = ray.y / rayLength;

	float heightFactor = exp( -clamp( relHeight * invH, -80.0, 80.0 ) );
	float verticalRate = dirY * invH;
	float pathIntegral = ( abs( verticalRate ) > 1.0e-8 )
			? ( 1.0 -
				exp( -clamp( slant * verticalRate, -80.0, 80.0 ) ) ) /
			  verticalRate
			: slant;
	float tau = clamp( sigma * heightFactor * pathIntegral, 0.0, 80.0 );
	float transmittance = exp( -tau );

	fragColour = vec4( vSample.xyz * transmittance +
					   hazeInscatter.xyz * ( 1.0 - transmittance ),
					   vSample.w );
}
