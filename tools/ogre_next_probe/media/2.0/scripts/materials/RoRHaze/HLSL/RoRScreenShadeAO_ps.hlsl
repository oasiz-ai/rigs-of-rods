
// RoR stage-3 screen-space shade evaluation. See RoRScreenShade.material for
// the contract and Metal/RoRScreenShadeAO_ps.metal for the full derivation
// comment. This source is the HLSL sibling of that shader.

Texture2D<float> depthTexture	: register(t0);
SamplerState samplerDepth		: register(s0);

float4 main
(
	in float2 uv : TEXCOORD0,
	uniform float4 shadeProj,
	uniform float4 shadeDepthLin,
	uniform float4 shadeAoParams,
	uniform float4 shadeAoKernel,
	uniform float4 shadeContactParams,
	uniform float4 shadeSunDirView
) : SV_Target
{
	float fDepth = depthTexture.Sample( samplerDepth, uv );
	if( fDepth >= 1.0f )
		return float4( 1.0f, 1.0f, 65504.0f, 1.0f );

	float viewZ = shadeDepthLin.y / ( fDepth - shadeDepthLin.x );
	float2 ndc = float2( uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f );
	float3 vPos = float3(
		( ndc.x + shadeProj.z ) * viewZ / shadeProj.x,
		( ndc.y + shadeProj.w ) * viewZ / shadeProj.y,
		-viewZ );
	float2 invAoSize = shadeAoKernel.zw;
	float2 pixel = uv * shadeDepthLin.zw;
	float ign = frac( 52.9829189f *
					  frac( 0.06711056f * floor( pixel.x ) +
							0.00583715f * floor( pixel.y ) ) );

	float2 dxUv = float2( invAoSize.x, 0.0f );
	float2 dyUv = float2( 0.0f, invAoSize.y );
	float dRight = depthTexture.Sample( samplerDepth, uv + dxUv );
	float dLeft = depthTexture.Sample( samplerDepth, uv - dxUv );
	float dUp = depthTexture.Sample( samplerDepth, uv + dyUv );
	float dDown = depthTexture.Sample( samplerDepth, uv - dyUv );
	float zRight = shadeDepthLin.y / ( dRight - shadeDepthLin.x );
	float zLeft = shadeDepthLin.y / ( dLeft - shadeDepthLin.x );
	float zUp = shadeDepthLin.y / ( dUp - shadeDepthLin.x );
	float zDown = shadeDepthLin.y / ( dDown - shadeDepthLin.x );
	float2 ndcR = float2( ( uv.x + dxUv.x ) * 2.0f - 1.0f, ndc.y );
	float2 ndcL = float2( ( uv.x - dxUv.x ) * 2.0f - 1.0f, ndc.y );
	float2 ndcU = float2( ndc.x, 1.0f - ( uv.y + dyUv.y ) * 2.0f );
	float2 ndcD = float2( ndc.x, 1.0f - ( uv.y - dyUv.y ) * 2.0f );
	float3 pRight = float3(
		( ndcR.x + shadeProj.z ) * zRight / shadeProj.x,
		( ndcR.y + shadeProj.w ) * zRight / shadeProj.y, -zRight );
	float3 pLeft = float3(
		( ndcL.x + shadeProj.z ) * zLeft / shadeProj.x,
		( ndcL.y + shadeProj.w ) * zLeft / shadeProj.y, -zLeft );
	float3 pUp = float3(
		( ndcU.x + shadeProj.z ) * zUp / shadeProj.x,
		( ndcU.y + shadeProj.w ) * zUp / shadeProj.y, -zUp );
	float3 pDown = float3(
		( ndcD.x + shadeProj.z ) * zDown / shadeProj.x,
		( ndcD.y + shadeProj.w ) * zDown / shadeProj.y, -zDown );
	float3 ddxV = ( abs( pRight.z - vPos.z ) < abs( vPos.z - pLeft.z ) )
					  ? ( pRight - vPos )
					  : ( vPos - pLeft );
	float3 ddyV = ( abs( pUp.z - vPos.z ) < abs( vPos.z - pDown.z ) )
					  ? ( pUp - vPos )
					  : ( vPos - pDown );
	float3 normal = cross( ddyV, ddxV );
	float normalLength = length( normal );
	normal = normalLength > 1.0e-8f ? normal / normalLength
									: float3( 0.0f, 0.0f, 1.0f );
	if( dot( normal, -vPos ) < 0.0f )
		normal = -normal;

	float ao = 1.0f;
	float radius = shadeAoParams.x;
	float aoStrength = shadeAoParams.y;
	int sampleCount = int( shadeAoParams.z );
	if( radius > 0.0f && aoStrength > 0.0f && sampleCount > 0 )
	{
		float screenRadius =
			radius * shadeProj.y * 0.5f * shadeDepthLin.w / viewZ;
		screenRadius = min( screenRadius, shadeAoKernel.x );
		if( screenRadius >= 1.0f )
		{
			float occlusion = 0.0f;
			// The trip count is a validated runtime tier. Keep it dynamic on
			// D3D11: FXC otherwise attempts a speculative 501-iteration
			// unroll and rejects this shader before the Ogre-Next frontend can
			// create its visible window.
			[loop]
			for( int i = 0; i < sampleCount; ++i )
			{
				float alphaS = ( float( i ) + 0.5f ) / float( sampleCount );
				float theta = alphaS * 43.9822971503f +
							  ign * 6.28318530718f;
				float2 offset = float2( cos( theta ), sin( theta ) ) *
								( alphaS * screenRadius );
				float2 uvS = uv + offset * invAoSize;
				if( uvS.x <= 0.0f || uvS.x >= 1.0f ||
					uvS.y <= 0.0f || uvS.y >= 1.0f )
					continue;
				float dS = depthTexture.Sample( samplerDepth, uvS );
				if( dS >= 1.0f )
					continue;
				float zS = shadeDepthLin.y / ( dS - shadeDepthLin.x );
				float2 ndcS = float2( uvS.x * 2.0f - 1.0f,
									  1.0f - uvS.y * 2.0f );
				float3 vSample = float3(
					( ndcS.x + shadeProj.z ) * zS / shadeProj.x,
					( ndcS.y + shadeProj.w ) * zS / shadeProj.y,
					-zS ) - vPos;
				float vv = dot( vSample, vSample );
				float vn = dot( vSample, normal ) /
						   ( sqrt( vv ) + 1.0e-4f );
				float falloff =
					max( 0.0f, 1.0f - vv / ( radius * radius ) );
				occlusion += max( 0.0f, vn - shadeAoKernel.y ) * falloff;
			}
			occlusion = aoStrength * occlusion / float( sampleCount );
			ao = pow( clamp( 1.0f - occlusion, 0.0f, 1.0f ),
					  shadeAoParams.w );
		}
	}

	float sunVisibility = 1.0f;
	float contactLength = shadeContactParams.x;
	float contactStrength = shadeContactParams.y;
	int contactSteps = int( shadeContactParams.z );
	if( shadeSunDirView.w > 0.5f && contactLength > 0.0f &&
		contactStrength > 0.0f && contactSteps > 0 )
	{
		float3 sunDir = shadeSunDirView.xyz;
		float occluded = 0.0f;
		// Contact-shadow tiers are runtime-selected too; preserve the loop
		// instead of asking FXC to infer and unroll an artificial upper bound.
		[loop]
		for( int i = 0; i < contactSteps; ++i )
		{
			float t = contactLength * ( float( i ) + ign ) /
					  float( contactSteps );
			float3 q = vPos + sunDir * t;
			float viewZq = -q.z;
			if( viewZq < 0.05f )
				break;
			float2 ndcQ = float2(
				shadeProj.x * q.x / viewZq - shadeProj.z,
				shadeProj.y * q.y / viewZq - shadeProj.w );
			float2 uvQ = float2( ndcQ.x * 0.5f + 0.5f,
								 0.5f - ndcQ.y * 0.5f );
			if( uvQ.x <= 0.0f || uvQ.x >= 1.0f ||
				uvQ.y <= 0.0f || uvQ.y >= 1.0f )
				break;
			float dS = depthTexture.Sample( samplerDepth, uvQ );
			if( dS >= 1.0f )
				continue;
			float viewZs = shadeDepthLin.y / ( dS - shadeDepthLin.x );
			float delta = viewZq - viewZs;
			float bias = 0.02f + 0.02f * t;
			if( delta > bias &&
				delta < shadeContactParams.w + 0.1f * t )
			{
				occluded = 1.0f;
				break;
			}
		}
		sunVisibility = 1.0f - contactStrength * occluded;
	}

	return float4( ao, sunVisibility, min( viewZ, 65504.0f ), 1.0f );
}
