// Modern D3D11 port of the animated, PSSM-shadowed grass shader.

#if defined(GRASS_VERTEX)

float time;
float frequency;
float4 direction;
float4x4 worldViewProj;
float3 camPos;
float fadeRange;
float4x4 texWorldViewProjMatrix0;
float4x4 texWorldViewProjMatrix1;
float4x4 texWorldViewProjMatrix2;

struct GrassVertexInput
{
    float4 position : POSITION;
    float4 colour : COLOR0;
    float3 uv0 : TEXCOORD0;
};

struct GrassVertexOutput
{
    float4 clipPosition : SV_Position;
    float4 colour : COLOR0;
    float3 uv0 : TEXCOORD0;
    float4 lightPosition0 : TEXCOORD1;
    float4 lightPosition1 : TEXCOORD2;
    float4 lightPosition2 : TEXCOORD3;
};

GrassVertexOutput GrassVS(GrassVertexInput input)
{
    GrassVertexOutput output;
    float4 position = input.position;
    float distanceToCamera = distance(camPos.xz, position.xz);
    output.colour.rgb = input.colour.rgb;
    output.colour.a = 2.0 - (2.0 * distanceToCamera / fadeRange);
    float originalX = position.x;
    if (input.uv0.y == 0.0)
    {
        float offset = sin(time + originalX * frequency);
        position += direction * offset;
    }
    output.clipPosition = mul(worldViewProj, position);
    output.uv0 = input.uv0;
    output.uv0.z = output.clipPosition.z;
    output.lightPosition0 = mul(texWorldViewProjMatrix0, position);
    output.lightPosition1 = mul(texWorldViewProjMatrix1, position);
    output.lightPosition2 = mul(texWorldViewProjMatrix2, position);
    return output;
}

#elif defined(GRASS_FRAGMENT)

Texture2D diffuseMap : register(t0);
SamplerState diffuseMapSampler : register(s0);
Texture2D shadowMap0 : register(t1);
SamplerState shadowMap0Sampler : register(s1);
Texture2D shadowMap1 : register(t2);
SamplerState shadowMap1Sampler : register(s2);
Texture2D shadowMap2 : register(t3);
SamplerState shadowMap2Sampler : register(s3);
float4 invShadowMapSize0;
float4 invShadowMapSize1;
float4 invShadowMapSize2;
float4 pssmSplitPoints;

struct GrassVertexOutput
{
    float4 clipPosition : SV_Position;
    float4 colour : COLOR0;
    float3 uv0 : TEXCOORD0;
    float4 lightPosition0 : TEXCOORD1;
    float4 lightPosition1 : TEXCOORD2;
    float4 lightPosition2 : TEXCOORD3;
};

float ShadowPcf(
    Texture2D shadowMap,
    SamplerState shadowSampler,
    float4 shadowMapPosition,
    float2 offset)
{
    shadowMapPosition /= shadowMapPosition.w;
    float2 uv = shadowMapPosition.xy;
    float3 sampleOffset = float3(offset, -offset.x) * 0.3;
    float depth = shadowMapPosition.z;
    float shadow = depth <= shadowMap.Sample(
        shadowSampler, uv - sampleOffset.xy).r ? 1.0 : 0.0;
    shadow += depth <= shadowMap.Sample(
        shadowSampler, uv + sampleOffset.xy).r ? 1.0 : 0.0;
    shadow += depth <= shadowMap.Sample(
        shadowSampler, uv + sampleOffset.zy).r ? 1.0 : 0.0;
    shadow += depth <= shadowMap.Sample(
        shadowSampler, uv - sampleOffset.zy).r ? 1.0 : 0.0;
    return shadow * 0.25;
}

float4 GrassPS(GrassVertexOutput input) : SV_Target
{
    float4 diffuseSample = diffuseMap.Sample(diffuseMapSampler, input.uv0.xy);
    float shadowing;
    if (input.uv0.z <= pssmSplitPoints.y)
    {
        shadowing = ShadowPcf(
            shadowMap0, shadowMap0Sampler, input.lightPosition0,
            invShadowMapSize0.xy);
    }
    else if (input.uv0.z <= pssmSplitPoints.z)
    {
        shadowing = ShadowPcf(
            shadowMap1, shadowMap1Sampler, input.lightPosition1,
            invShadowMapSize1.xy);
    }
    else
    {
        shadowing = ShadowPcf(
            shadowMap2, shadowMap2Sampler, input.lightPosition2,
            invShadowMapSize2.xy);
    }
    float3 litColour = diffuseSample.rgb * (0.65 + 0.35 * shadowing);
    return float4(litColour, diffuseSample.a) * input.colour;
}

#else
#error A grass shader stage selector is required
#endif
