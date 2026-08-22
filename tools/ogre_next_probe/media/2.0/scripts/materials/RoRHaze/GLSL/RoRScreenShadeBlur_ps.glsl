#version ogre_glsl_ver_330

// RoR stage-3 shade blur. See RoRScreenShade.material for the contract and
// Metal/RoRScreenShadeBlur_ps.metal for the derivation comment. This source
// is the byte-identical GLSL and Vulkan-GLSL sibling of that shader.

vulkan_layout( location = 0 )
out vec4 fragColour;

vulkan_layout( location = 0 )
in block
{
	vec2 uv0;
} inPs;

vulkan_layout( ogre_t0 ) uniform texture2D shadeTexture;
vulkan( layout( ogre_s0 ) uniform sampler samplerPoint );

vulkan( layout( ogre_P0 ) uniform Params { )
	uniform vec4 shadeBlurParams;
vulkan( }; )

void main()
{
	const float kWeights[5] = float[5]( 0.2270270270, 0.1945945946,
									    0.1216216216, 0.0540540541,
									    0.0162162162 );

	vec4 centre = texture( vkSampler2D( shadeTexture, samplerPoint ),
						   inPs.uv0 );
	float centreDepth = max( centre.z, 1.0e-3 );
	float rejectScale = shadeBlurParams.z;

	vec2 accum = centre.xy * kWeights[0];
	float weightSum = kWeights[0];
	for( int i = 1; i < 5; ++i )
	{
		vec2 offset = shadeBlurParams.xy * float( i );
		for( int s = -1; s <= 1; s += 2 )
		{
			vec2 uvS = inPs.uv0 + offset * float( s );
			if( uvS.x < 0.0 || uvS.x > 1.0 ||
				uvS.y < 0.0 || uvS.y > 1.0 )
				continue;
			vec4 tap = texture( vkSampler2D( shadeTexture, samplerPoint ),
								uvS );
			float relative = abs( tap.z - centre.z ) / centreDepth;
			float weight = kWeights[i] /
						   ( 1.0 + rejectScale * relative );
			accum += tap.xy * weight;
			weightSum += weight;
		}
	}

	fragColour = vec4( accum / weightSum, centre.z, 1.0 );
}
