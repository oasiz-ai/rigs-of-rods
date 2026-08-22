#version ogre_glsl_ver_330

// RoR stage-3 shade apply. See RoRScreenShade.material for the contract and
// Metal/RoRScreenShadeApply_ps.metal for the derivation comment. This source
// is the byte-identical GLSL and Vulkan-GLSL sibling of that shader.

vulkan_layout( location = 0 )
out vec4 fragColour;

vulkan_layout( location = 0 )
in block
{
	vec2 uv0;
} inPs;

vulkan_layout( ogre_t0 ) uniform texture2D rt0;
vulkan( layout( ogre_s0 ) uniform sampler samplerPoint );

vulkan_layout( ogre_t1 ) uniform texture2D shadeTexture;
vulkan( layout( ogre_s1 ) uniform sampler samplerShade );

void main()
{
	vec4 vSample = texture( vkSampler2D( rt0, samplerPoint ), inPs.uv0 );
	if( vSample.w >= 1.0 )
	{
		fragColour = vec4( vSample.xyz, 1.0 );
		return;
	}

	vec2 shade = texture( vkSampler2D( shadeTexture, samplerShade ),
						  inPs.uv0 ).xy;
	float indirectFraction = clamp( vSample.w, 0.0, 1.0 );
	float factor = indirectFraction * shade.x +
				   ( 1.0 - indirectFraction ) * shade.y;
	fragColour = vec4( vSample.xyz * factor, 1.0 );
}
