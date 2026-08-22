#version ogre_glsl_ver_330

// RoR temporal AA: camera-reprojection motion vectors. See
// RoRTemporalAa.material for the contract and
// Metal/RoRTaaMotionVectors_ps.metal for the full derivation comment. This
// source is the byte-identical GLSL and Vulkan-GLSL sibling of that shader.

vulkan_layout( location = 0 )
out vec4 fragColour;

vulkan_layout( location = 0 )
in block
{
	vec2 uv0;
} inPs;

vulkan_layout( ogre_t0 ) uniform texture2D depthTexture;
vulkan( layout( ogre_s0 ) uniform sampler samplerDepth );

vulkan( layout( ogre_P0 ) uniform Params { )
	uniform vec4 taaReproject0;
	uniform vec4 taaReproject1;
	uniform vec4 taaReproject2;
	uniform vec4 taaReproject3;
	uniform vec4 taaJitter;
	uniform vec4 taaExtent;
vulkan( }; )

void main()
{
	float fDepth = texture( vkSampler2D( depthTexture, samplerDepth ),
							inPs.uv0 ).x;

	vec2 pixelCur = inPs.uv0 * taaExtent.xy;
	vec2 pixelCurUnjittered = pixelCur - taaJitter.xy;
	vec2 ndcUnjittered = vec2(
			pixelCurUnjittered.x * taaExtent.z * 2.0 - 1.0,
			1.0 - pixelCurUnjittered.y * taaExtent.w * 2.0 );

	vec4 current = vec4( ndcUnjittered.x, ndcUnjittered.y, fDepth, 1.0 );
	vec4 previousClip = vec4(
			dot( taaReproject0, current ),
			dot( taaReproject1, current ),
			dot( taaReproject2, current ),
			dot( taaReproject3, current ) );
	if( !(previousClip.w > 1.0e-8) )
	{
		fragColour = vec4( 0.0, 0.0, 0.0, 0.0 );
		return;
	}

	vec2 previousNdc = previousClip.xy / previousClip.w;
	vec2 previousPixel = vec2(
			( previousNdc.x * 0.5 + 0.5 ) * taaExtent.x,
			( 0.5 - previousNdc.y * 0.5 ) * taaExtent.y );

	vec2 motion = previousPixel - pixelCurUnjittered;
	fragColour = vec4( motion.x, motion.y, 0.0, 0.0 );
}
