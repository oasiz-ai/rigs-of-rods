
// RoR stage-3 shade blur. See RoRScreenShade.material for the contract and
// Metal/RoRScreenShadeBlur_ps.metal for the derivation comment. This source
// is the HLSL sibling of that shader.

Texture2D<float4> shadeTexture	: register(t0);
SamplerState samplerPoint		: register(s0);

static const float kWeights[5] = { 0.2270270270f, 0.1945945946f,
								   0.1216216216f, 0.0540540541f,
								   0.0162162162f };

float4 main
(
	in float2 uv : TEXCOORD0,
	uniform float4 shadeBlurParams
) : SV_Target
{
	float4 centre = shadeTexture.Sample( samplerPoint, uv );
	float centreDepth = max( centre.z, 1.0e-3f );
	float rejectScale = shadeBlurParams.z;

	float2 accum = centre.xy * kWeights[0];
	float weightSum = kWeights[0];
	for( int i = 1; i < 5; ++i )
	{
		float2 offset = shadeBlurParams.xy * float( i );
		for( int s = -1; s <= 1; s += 2 )
		{
			float2 uvS = uv + offset * float( s );
			if( uvS.x < 0.0f || uvS.x > 1.0f ||
				uvS.y < 0.0f || uvS.y > 1.0f )
				continue;
			float4 tap = shadeTexture.Sample( samplerPoint, uvS );
			float relative = abs( tap.z - centre.z ) / centreDepth;
			float weight = kWeights[i] /
						   ( 1.0f + rejectScale * relative );
			accum += tap.xy * weight;
			weightSum += weight;
		}
	}

	return float4( accum / weightSum, centre.z, 1.0f );
}
