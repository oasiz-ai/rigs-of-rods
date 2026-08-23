#version ogre_glsl_ver_330

// RoR stage-3 screen-space shade evaluation. See RoRScreenShade.material for
// the contract and Metal/RoRScreenShadeAO_ps.metal for the full derivation
// comment. This source is the byte-identical GLSL and Vulkan-GLSL sibling of
// that shader.

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
	uniform vec4 shadeProj;
	uniform vec4 shadeDepthLin;
	uniform vec4 shadeAoParams;
	uniform vec4 shadeAoKernel;
	uniform vec4 shadeContactParams;
	uniform vec4 shadeSunDirView;
vulkan( }; )

float LinearDepth( float fDepth )
{
	return shadeDepthLin.y / ( fDepth - shadeDepthLin.x );
}

vec3 ViewPosition( vec2 uv, float fDepth )
{
	float viewZ = LinearDepth( fDepth );
	vec2 ndc = vec2( uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0 );
	return vec3( ( ndc.x + shadeProj.z ) * viewZ / shadeProj.x,
				 ( ndc.y + shadeProj.w ) * viewZ / shadeProj.y,
				 -viewZ );
}

float InterleavedGradientNoise( vec2 pixel )
{
	return fract( 52.9829189 *
				  fract( 0.06711056 * pixel.x + 0.00583715 * pixel.y ) );
}

void main()
{
	float fDepth = texture( vkSampler2D( depthTexture, samplerDepth ),
							inPs.uv0 ).x;
	if( fDepth >= 1.0 )
	{
		fragColour = vec4( 1.0, 1.0, 65504.0, 1.0 );
		return;
	}

	float viewZ = LinearDepth( fDepth );
	vec3 vPos = ViewPosition( inPs.uv0, fDepth );
	vec2 invAoSize = shadeAoKernel.zw;
	vec2 pixel = inPs.uv0 * shadeDepthLin.zw;
	float ign = InterleavedGradientNoise( floor( pixel ) );

	vec2 dxUv = vec2( invAoSize.x, 0.0 );
	vec2 dyUv = vec2( 0.0, invAoSize.y );
	vec3 pRight = ViewPosition(
		inPs.uv0 + dxUv,
		texture( vkSampler2D( depthTexture, samplerDepth ),
				 inPs.uv0 + dxUv ).x );
	vec3 pLeft = ViewPosition(
		inPs.uv0 - dxUv,
		texture( vkSampler2D( depthTexture, samplerDepth ),
				 inPs.uv0 - dxUv ).x );
	vec3 pUp = ViewPosition(
		inPs.uv0 + dyUv,
		texture( vkSampler2D( depthTexture, samplerDepth ),
				 inPs.uv0 + dyUv ).x );
	vec3 pDown = ViewPosition(
		inPs.uv0 - dyUv,
		texture( vkSampler2D( depthTexture, samplerDepth ),
				 inPs.uv0 - dyUv ).x );
	vec3 ddx = ( abs( pRight.z - vPos.z ) < abs( vPos.z - pLeft.z ) )
				   ? ( pRight - vPos )
				   : ( vPos - pLeft );
	vec3 ddy = ( abs( pUp.z - vPos.z ) < abs( vPos.z - pDown.z ) )
				   ? ( pUp - vPos )
				   : ( vPos - pDown );
	vec3 normal = cross( ddy, ddx );
	float normalLength = length( normal );
	normal = normalLength > 1.0e-8 ? normal / normalLength
								   : vec3( 0.0, 0.0, 1.0 );
	if( dot( normal, -vPos ) < 0.0 )
		normal = -normal;

	float ao = 1.0;
	float radius = shadeAoParams.x;
	float aoStrength = shadeAoParams.y;
	int sampleCount = int( shadeAoParams.z );
	if( radius > 0.0 && aoStrength > 0.0 && sampleCount > 0 )
	{
		float screenRadius =
			radius * shadeProj.y * 0.5 * shadeDepthLin.w / viewZ;
		screenRadius = min( screenRadius, shadeAoKernel.x );
		if( screenRadius >= 1.0 )
		{
			float occlusion = 0.0;
			for( int i = 0; i < sampleCount; ++i )
			{
				float alpha = ( float( i ) + 0.5 ) / float( sampleCount );
				float theta = alpha * 43.9822971503 +
							  ign * 6.28318530718;
				vec2 offset = vec2( cos( theta ), sin( theta ) ) *
							  ( alpha * screenRadius );
				vec2 uvS = inPs.uv0 + offset * invAoSize;
				if( uvS.x <= 0.0 || uvS.x >= 1.0 ||
					uvS.y <= 0.0 || uvS.y >= 1.0 )
					continue;
				float dS = texture( vkSampler2D( depthTexture,
												 samplerDepth ), uvS ).x;
				if( dS >= 1.0 )
					continue;
				vec3 vSample = ViewPosition( uvS, dS ) - vPos;
				float vv = dot( vSample, vSample );
				float vn = dot( vSample, normal ) /
						   ( sqrt( vv ) + 1.0e-4 );
				float falloff = max( 0.0, 1.0 - vv / ( radius * radius ) );
				occlusion += max( 0.0, vn - shadeAoKernel.y ) * falloff;
			}
			occlusion = aoStrength * occlusion / float( sampleCount );
			ao = pow( clamp( 1.0 - occlusion, 0.0, 1.0 ),
					  shadeAoParams.w );
		}
	}

	float sunVisibility = 1.0;
	float contactLength = shadeContactParams.x;
	float contactStrength = shadeContactParams.y;
	int contactSteps = int( shadeContactParams.z );
	if( shadeSunDirView.w > 0.5 && contactLength > 0.0 &&
		contactStrength > 0.0 && contactSteps > 0 )
	{
		vec3 sunDir = shadeSunDirView.xyz;
		float occluded = 0.0;
		for( int i = 0; i < contactSteps; ++i )
		{
			float t = contactLength * ( float( i ) + ign ) /
					  float( contactSteps );
			vec3 q = vPos + sunDir * t;
			float viewZq = -q.z;
			if( viewZq < 0.05 )
				break;
			vec2 ndcQ = vec2(
				shadeProj.x * q.x / viewZq - shadeProj.z,
				shadeProj.y * q.y / viewZq - shadeProj.w );
			vec2 uvQ = vec2( ndcQ.x * 0.5 + 0.5,
							 0.5 - ndcQ.y * 0.5 );
			if( uvQ.x <= 0.0 || uvQ.x >= 1.0 ||
				uvQ.y <= 0.0 || uvQ.y >= 1.0 )
				break;
			float dS = texture( vkSampler2D( depthTexture,
											 samplerDepth ), uvQ ).x;
			if( dS >= 1.0 )
				continue;
			float viewZs = LinearDepth( dS );
			float delta = viewZq - viewZs;
			float bias = 0.02 + 0.02 * t;
			if( delta > bias &&
				delta < shadeContactParams.w + 0.1 * t )
			{
				occluded = 1.0;
				break;
			}
		}
		sunVisibility = 1.0 - contactStrength * occluded;
	}

	fragColour = vec4( ao, sunVisibility, min( viewZ, 65504.0 ), 1.0 );
}
