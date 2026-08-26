// Modern D3D11 port of the managed-material PSSM caster and receiver.
// Each GPU program definition selects exactly one stage.

#if defined(PSSM_CASTER_VERTEX)

float4x4 wvpMat;

struct PssmCasterVertexInput
{
    float4 position : POSITION;
    float3 uv0 : TEXCOORD0;
};

struct PssmCasterVertexOutput
{
    float4 clipPosition : SV_Position;
    float2 depth : TEXCOORD0;
    float2 uv : TEXCOORD1;
};

PssmCasterVertexOutput PssmShadowCasterVS(PssmCasterVertexInput input)
{
    PssmCasterVertexOutput output;
    output.clipPosition = mul(wvpMat, input.position);
    output.depth = output.clipPosition.zw;
    output.clipPosition.z = max(output.clipPosition.z, 0.0);
    output.uv = input.uv0.xy;
    return output;
}

#elif defined(PSSM_CASTER_FRAGMENT)

struct PssmCasterVertexOutput
{
    float4 clipPosition : SV_Position;
    float2 depth : TEXCOORD0;
    float2 uv : TEXCOORD1;
};

float4 PssmShadowCasterPS(PssmCasterVertexOutput input) : SV_Target
{
    float finalDepth = input.depth.x / input.depth.y;
    return float4(finalDepth, finalDepth, finalDepth, 1.0);
}

#elif defined(PSSM_CASTER_ALPHA_FRAGMENT)

Texture2D alphaMap : register(t0);
SamplerState alphaMapSampler : register(s0);

struct PssmCasterVertexOutput
{
    float4 clipPosition : SV_Position;
    float2 depth : TEXCOORD0;
    float2 uv : TEXCOORD1;
};

float4 PssmShadowCasterAlphaPS(PssmCasterVertexOutput input) : SV_Target
{
    float finalDepth = input.depth.x / input.depth.y;
    float alpha = alphaMap.Sample(alphaMapSampler, input.uv).a;
    clip(alpha > 0.5 ? 1.0 : -1.0);
    return float4(finalDepth, finalDepth, finalDepth, alpha);
}

#elif defined(PSSM_RECEIVER_VERTEX)

float4 lightPosition;
float3 eyePosition;
float4x4 worldViewProjMatrix;
float4x4 texWorldViewProjMatrix0;
float4x4 texWorldViewProjMatrix1;
float4x4 texWorldViewProjMatrix2;

struct PssmReceiverVertexInput
{
    float4 position : POSITION;
    float3 normal : NORMAL;
    float3 uv0 : TEXCOORD0;
};

struct PssmReceiverVertexOutput
{
    float4 clipPosition : SV_Position;
    float3 uv : TEXCOORD0;
    float3 lightDirection : TEXCOORD1;
    float3 halfAngle : TEXCOORD2;
    float4 lightPosition0 : TEXCOORD3;
    float4 lightPosition1 : TEXCOORD4;
    float4 lightPosition2 : TEXCOORD5;
    float3 normal : TEXCOORD6;
};

PssmReceiverVertexOutput PssmShadowReceiverVS(
    PssmReceiverVertexInput input)
{
    PssmReceiverVertexOutput output;
    output.clipPosition = mul(worldViewProjMatrix, input.position);
    output.uv = float3(input.uv0.xy, output.clipPosition.z);
    output.lightDirection = normalize(
        lightPosition.xyz - input.position.xyz * lightPosition.w);
    float3 eyeDirection = normalize(eyePosition - input.position.xyz);
    output.halfAngle = normalize(eyeDirection + output.lightDirection);
    output.lightPosition0 = mul(texWorldViewProjMatrix0, input.position);
    output.lightPosition1 = mul(texWorldViewProjMatrix1, input.position);
    output.lightPosition2 = mul(texWorldViewProjMatrix2, input.position);
    output.normal = input.normal;
    return output;
}

#elif defined(PSSM_RECEIVER_FRAGMENT)

float4 invShadowMapSize0;
float4 invShadowMapSize1;
float4 invShadowMapSize2;
float4 pssmSplitPoints;
float4 lightDiffuse;
float4 lightSpecular;
float4 ambient;

Texture2D shadowMap0 : register(t0);
SamplerState shadowMap0Sampler : register(s0);
Texture2D shadowMap1 : register(t1);
SamplerState shadowMap1Sampler : register(s1);
Texture2D shadowMap2 : register(t2);
SamplerState shadowMap2Sampler : register(s2);
Texture2D diffuse : register(t3);
SamplerState diffuseSampler : register(s3);
Texture2D specular : register(t4);
SamplerState specularSampler : register(s4);
Texture2D normalMap : register(t5);
SamplerState normalMapSampler : register(s5);

struct PssmReceiverVertexOutput
{
    float4 clipPosition : SV_Position;
    float3 uv : TEXCOORD0;
    float3 lightDirection : TEXCOORD1;
    float3 halfAngle : TEXCOORD2;
    float4 lightPosition0 : TEXCOORD3;
    float4 lightPosition1 : TEXCOORD4;
    float4 lightPosition2 : TEXCOORD5;
    float3 normal : TEXCOORD6;
};

float PssmShadowPcf(
    Texture2D shadowMap,
    SamplerState shadowSampler,
    float4 shadowMapPosition,
    float2 offset)
{
    shadowMapPosition /= shadowMapPosition.w;
    float2 uv = shadowMapPosition.xy;
    float3 sampleOffset = float3(offset, -offset.x) * 0.3;

    // The texture projection matrix supplies the legacy render-system bias.
    // Do not add a second receiver bias here.
    float shadowDepth = shadowMapPosition.z;
    float coverage = shadowDepth <= shadowMap.Sample(
        shadowSampler, uv - sampleOffset.xy).r ? 1.0 : 0.0;
    coverage += shadowDepth <= shadowMap.Sample(
        shadowSampler, uv + sampleOffset.xy).r ? 1.0 : 0.0;
    coverage += shadowDepth <= shadowMap.Sample(
        shadowSampler, uv + sampleOffset.zy).r ? 1.0 : 0.0;
    coverage += shadowDepth <= shadowMap.Sample(
        shadowSampler, uv - sampleOffset.zy).r ? 1.0 : 0.0;

    // Preserve the legacy attenuation: four taps intentionally divide by 5.
    return coverage / 5.0;
}

float4 PssmShadowReceiverPS(PssmReceiverVertexOutput input) : SV_Target
{
    float shadowing;
    if (input.uv.z <= pssmSplitPoints.y)
    {
        shadowing = PssmShadowPcf(
            shadowMap0,
            shadowMap0Sampler,
            input.lightPosition0,
            invShadowMapSize0.xy);
    }
    else if (input.uv.z <= pssmSplitPoints.z)
    {
        shadowing = PssmShadowPcf(
            shadowMap1,
            shadowMap1Sampler,
            input.lightPosition1,
            invShadowMapSize1.xy);
    }
    else
    {
        shadowing = PssmShadowPcf(
            shadowMap2,
            shadowMap2Sampler,
            input.lightPosition2,
            invShadowMapSize2.xy);
    }

    float3 lightVector = normalize(input.lightDirection);
    float3 halfAngle = normalize(input.halfAngle);
    float4 diffuseColour = diffuse.Sample(diffuseSampler, input.uv.xy);
    float4 specularColour = specular.Sample(
        specularSampler, input.uv.xy);
    float shininess = specularColour.a;
    specularColour.a = 1.0;

    float nDotL = dot(input.normal, lightVector);
    float nDotH = dot(input.normal, halfAngle);
    float diffuseTerm = max(nDotL, 0.0);
    float specularTerm = (nDotL < 0.0 || nDotH < 0.0)
        ? 0.0
        : pow(nDotH, shininess * 52.0);
    float shadowScale = 0.3 + 0.7 * shadowing;

    float4 colour = diffuseColour * saturate(
        ambient + lightDiffuse * diffuseTerm * shadowScale);
    colour += lightSpecular * specularColour
        * specularTerm * shadowScale;
    colour.a = diffuseColour.a;
    return colour;
}

#else
#error A managed PSSM shader stage selector is required
#endif
