#version ogre_glsl_ver_330

// RoR temporal AA: history resolve. See RoRTemporalAa.material for the
// contract and Metal/RoRTaaResolve_ps.metal for the full derivation
// comment. This source is the byte-identical GLSL and Vulkan-GLSL sibling
// of that shader.

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

vulkan_layout( ogre_t2 ) uniform texture2D motionTexture;
vulkan( layout( ogre_s2 ) uniform sampler samplerMotion );

vulkan_layout( ogre_t3 ) uniform texture2D prevDepthTexture;
vulkan( layout( ogre_s3 ) uniform sampler samplerPrevDepth );

vulkan_layout( ogre_t4 ) uniform texture2D historyTexture;
vulkan( layout( ogre_s4 ) uniform sampler samplerHistory );

vulkan_layout( ogre_t5 ) uniform texture2D reactiveTexture;
vulkan( layout( ogre_s5 ) uniform sampler samplerReactive );

vulkan( layout( ogre_P0 ) uniform Params { )
	uniform vec4 taaReproject0;
	uniform vec4 taaReproject1;
	uniform vec4 taaReproject2;
	uniform vec4 taaReproject3;
	uniform vec4 taaJitter;
	uniform vec4 taaExtent;
	uniform vec4 taaBlend;
	uniform vec4 taaDepthPolicy;
vulkan( }; )

vec3 RoRTaaToYCoCg( vec3 colour )
{
	return vec3(
			0.25 * colour.x + 0.5 * colour.y + 0.25 * colour.z,
			0.5 * colour.x - 0.5 * colour.z,
			0.5 * colour.y - 0.25 * colour.x - 0.25 * colour.z );
}

vec3 RoRTaaFromYCoCg( vec3 colour )
{
	return vec3( colour.x + colour.y - colour.z,
				 colour.x + colour.z,
				 colour.x - colour.y - colour.z );
}

vec3 RoRTaaClampHdr( vec3 colour )
{
	return clamp( colour, vec3( 0.0 ), vec3( 65504.0 ) );
}

void main()
{
	vec4 vCentre = texture( vkSampler2D( rt0, samplerPoint ), inPs.uv0 );
	if( !(taaDepthPolicy.z > 0.5) )
	{
		fragColour = vec4( RoRTaaClampHdr( vCentre.xyz ), 1.0 );
		return;
	}

	float fDepth = texture( vkSampler2D( depthTexture, samplerDepth ),
							inPs.uv0 ).x;
	vec2 motion = texture( vkSampler2D( motionTexture, samplerMotion ),
						   inPs.uv0 ).xy;
	float motionLength = length( motion );
	float reactive = texture(
			vkSampler2D( reactiveTexture, samplerReactive ), inPs.uv0 ).x;

	vec2 pixelCur = inPs.uv0 * taaExtent.xy;
	vec2 pixelCurUnjittered = pixelCur - taaJitter.xy;
	vec2 previousPixel = pixelCurUnjittered + motion;
	vec2 previousUv = previousPixel * taaExtent.zw;

	vec2 ndcUnjittered = vec2(
			pixelCurUnjittered.x * taaExtent.z * 2.0 - 1.0,
			1.0 - pixelCurUnjittered.y * taaExtent.w * 2.0 );
	vec4 current = vec4( ndcUnjittered.x, ndcUnjittered.y, fDepth, 1.0 );
	vec4 previousClip = vec4(
			dot( taaReproject0, current ),
			dot( taaReproject1, current ),
			dot( taaReproject2, current ),
			dot( taaReproject3, current ) );

	bool offscreen = previousUv.x < 0.0 || previousUv.x > 1.0 ||
					 previousUv.y < 0.0 || previousUv.y > 1.0 ||
					 !(previousClip.w > 1.0e-8);
	if( offscreen )
	{
		fragColour = vec4( RoRTaaClampHdr( vCentre.xyz ), 1.0 );
		return;
	}

	float expectedPreviousDepth =
			clamp( previousClip.z / previousClip.w, 0.0, 1.0 );
	float storedPreviousDepth = texture(
			vkSampler2D( prevDepthTexture, samplerPrevDepth ),
			previousUv ).x;
	float depthError = abs( expectedPreviousDepth - storedPreviousDepth );
	float depthTolerance = taaDepthPolicy.x +
			taaDepthPolicy.y *
					max( expectedPreviousDepth, storedPreviousDepth );

	bool depthRejected = depthError > depthTolerance;
	bool motionRejected = motionLength >= taaBlend.z;
	bool reactiveRejected = reactive >= 1.0;
	if( depthRejected || motionRejected || reactiveRejected )
	{
		fragColour = vec4( RoRTaaClampHdr( vCentre.xyz ), 1.0 );
		return;
	}

	vec3 neighbourhood[9];
	vec3 minimum = vec3( 65504.0 );
	vec3 maximum = vec3( -65504.0 );
	vec3 colourSum = vec3( 0.0 );
	int neighbourIndex = 0;
	for( int y = -1; y <= 1; ++y )
	{
		for( int x = -1; x <= 1; ++x )
		{
			vec2 offsetUv = inPs.uv0 +
					vec2( float( x ), float( y ) ) * taaExtent.zw;
			vec3 neighbour = RoRTaaToYCoCg(
					texture( vkSampler2D( rt0, samplerPoint ),
							 offsetUv ).xyz );
			neighbourhood[neighbourIndex] = neighbour;
			++neighbourIndex;
			minimum = min( minimum, neighbour );
			maximum = max( maximum, neighbour );
			colourSum += neighbour;
		}
	}
	vec3 mean = colourSum / 9.0;
	vec3 squaredError = vec3( 0.0 );
	for( int index = 0; index < 9; ++index )
	{
		vec3 difference = neighbourhood[index] - mean;
		squaredError += difference * difference;
	}
	vec3 varianceRadius = taaBlend.y * sqrt( squaredError / 9.0 );
	vec3 lower = max( minimum, mean - varianceRadius );
	vec3 upper = min( maximum, mean + varianceRadius );

	vec3 history = texture( vkSampler2D( historyTexture, samplerHistory ),
							previousUv ).xyz;
	vec3 rescaledHistory = history * taaBlend.w;
	vec3 clippedHistory = RoRTaaFromYCoCg(
			clamp( RoRTaaToYCoCg( rescaledHistory ), lower, upper ) );

	float motionFactor = max( 0.0, 1.0 - motionLength / taaBlend.z );
	float weight = taaBlend.x * ( 1.0 - reactive ) * motionFactor;

	vec3 resolved = RoRTaaClampHdr(
			vCentre.xyz * ( 1.0 - weight ) +
			RoRTaaClampHdr( clippedHistory ) * weight );
	fragColour = vec4( resolved, 1.0 );
}
