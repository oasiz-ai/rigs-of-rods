#version ogre_glsl_ver_330

// RoR temporal AA: depth history store. See RoRTemporalAa.material for the
// contract and Metal/RoRTaaDepthStore_ps.metal for the derivation comment.
// This source is the byte-identical GLSL and Vulkan-GLSL sibling.

vulkan_layout( location = 0 )
out vec4 fragColour;

vulkan_layout( location = 0 )
in block
{
	vec2 uv0;
} inPs;

vulkan_layout( ogre_t0 ) uniform texture2D depthTexture;
vulkan( layout( ogre_s0 ) uniform sampler samplerDepth );

void main()
{
	float fDepth = texture( vkSampler2D( depthTexture, samplerDepth ),
							inPs.uv0 ).x;
	fragColour = vec4( fDepth, 0.0, 0.0, 0.0 );
}
