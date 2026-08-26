// Modern D3D11 port of the historical general shader family.
// Each GPU program definition selects exactly one stage.

#if defined(GENERAL_VERTEX_AMBIENT)

float4x4 wvpMat;

struct GeneralAmbientVertexInput
{
    float4 position : POSITION;
    float3 uv0 : TEXCOORD0;
};

struct GeneralAmbientVertexOutput
{
    float4 clipPosition : SV_Position;
    float2 uv : TEXCOORD0;
};

GeneralAmbientVertexOutput GeneralAmbientVS(GeneralAmbientVertexInput input)
{
    GeneralAmbientVertexOutput output;
    output.clipPosition = mul(wvpMat, input.position);
    output.uv = input.uv0.xy;
    return output;
}

#elif defined(GENERAL_FRAGMENT_AMBIENT)

float3 ambient;
float4 matDif;

Texture2D diffuseMap : register(t0);
SamplerState diffuseMapSampler : register(s0);

struct GeneralAmbientVertexOutput
{
    float4 clipPosition : SV_Position;
    float2 uv : TEXCOORD0;
};

float4 GeneralAmbientPS(GeneralAmbientVertexOutput input) : SV_Target
{
    float4 diffuseTex = diffuseMap.Sample(diffuseMapSampler, input.uv);
    return float4(ambient * matDif.rgb * diffuseTex.rgb, diffuseTex.a);
}

#elif defined(GENERAL_VERTEX_RENDER)

float4x4 wMat;
float4x4 wvpMat;

struct GeneralRenderVertexInput
{
    float4 position : POSITION;
    float3 normal : NORMAL;
    float4 colour : COLOR0;
    float3 uv0 : TEXCOORD0;
};

struct GeneralRenderVertexOutput
{
    float4 clipPosition : SV_Position;
    float3 uv : TEXCOORD0;
    float4 worldPosition : TEXCOORD1;
    float3 objectNormal : TEXCOORD2;
    float4 colour : COLOR0;
};

GeneralRenderVertexOutput GeneralRenderVS(GeneralRenderVertexInput input)
{
    GeneralRenderVertexOutput output;
    output.clipPosition = mul(wvpMat, input.position);
    output.uv = input.uv0;
    output.worldPosition = mul(wMat, input.position);
    output.objectNormal = input.normal;
    output.colour = input.colour;
    return output;
}

#elif defined(GENERAL_FRAGMENT_RENDER) || defined(GENERAL_FRAGMENT_RENDER_GRASS)

#if defined(GENERAL_FRAGMENT_RENDER)
float3 ambient;
float4 matDif;
#endif

struct GeneralRenderVertexOutput
{
    float4 clipPosition : SV_Position;
    float3 uv : TEXCOORD0;
    float4 worldPosition : TEXCOORD1;
    float3 objectNormal : TEXCOORD2;
    float4 colour : COLOR0;
};

#if defined(GENERAL_FRAGMENT_RENDER)
float4 GeneralRenderPS(GeneralRenderVertexOutput input) : SV_Target
{
    float bridge = input.colour.r;
    float pipe = input.colour.g;
    float normalY = abs(input.objectNormal.y);

    float power = lerp(lerp(8.0, 8.0, bridge), 4.0, pipe);
    float terrain = lerp(lerp(1.0, 0.0, bridge), 0.0, pipe);
    float diffuse = 1.0 - lerp(
        1.0 - lerp(pow(normalY, power), pow(normalY, power), pipe),
        pow(1.0 - 2.0 * acos(normalY) / 3.141592654, power),
        terrain);
    float3 litColour = ambient + diffuse * matDif.rgb;

    float3 colour = lerp(
        lerp(
            float3(1.0, 1.0, 1.0) * litColour,
            float3(0.0, 0.8, 1.0) * (0.4 + 0.7 * litColour),
            bridge),
        float3(1.0, 0.8, 0.0) * (0.2 + litColour),
        pipe);
    return float4(colour, 1.0);
}
#else
float4 GeneralRenderGrassPS(GeneralRenderVertexOutput input) : SV_Target
{
    float bridge = input.colour.a;
    return float4(bridge, bridge, bridge, bridge);
}
#endif

#elif defined(GENERAL_VERTEX_DIFFUSE)

float4x4 wMat;
float4x4 wvpMat;
float4 fogParams;

struct GeneralDiffuseVertexInput
{
    float4 position : POSITION;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
    float4 colour : COLOR0;
    float3 uv0 : TEXCOORD0;
};

struct GeneralDiffuseVertexOutput
{
    float4 clipPosition : SV_Position;
    float3 uv : TEXCOORD0;
    float4 worldPosition : TEXCOORD1;
    float3 objectNormal : TEXCOORD2;
    float3 objectTangent : TEXCOORD3;
    float3 objectBitangent : TEXCOORD4;
    float4 colour : COLOR0;
};

GeneralDiffuseVertexOutput GeneralDiffuseVS(GeneralDiffuseVertexInput input)
{
    GeneralDiffuseVertexOutput output;
    output.clipPosition = mul(wvpMat, input.position);
    output.uv = input.uv0;
    output.worldPosition = mul(wMat, input.position);
    output.objectNormal = input.normal;
    output.objectTangent = input.tangent;
    output.objectBitangent = cross(input.tangent, input.normal);
    output.colour = input.colour;
    output.worldPosition.w = saturate(
        fogParams.x * (output.clipPosition.z - fogParams.y) * fogParams.w);
    return output;
}

#elif defined(GENERAL_FRAGMENT_DIFFUSE) || defined(GENERAL_FRAGMENT_ENV)

float3 ambient;
float3 lightDif0;
float3 lightSpec0;
float4 matDif;
float4 matSpec;
float matShininess;
float3 fogColor;
float4 lightPos0;
float3 camPos;
float4x4 iTWMat;
#if defined(GENERAL_FRAGMENT_ENV)
float4 envPars;
#endif

Texture2D diffuseMap : register(t0);
SamplerState diffuseMapSampler : register(s0);
Texture2D normalMap : register(t1);
SamplerState normalMapSampler : register(s1);
#if defined(GENERAL_FRAGMENT_ENV)
TextureCube envMap : register(t2);
SamplerState envMapSampler : register(s2);
#endif

struct GeneralDiffuseVertexOutput
{
    float4 clipPosition : SV_Position;
    float3 uv : TEXCOORD0;
    float4 worldPosition : TEXCOORD1;
    float3 objectNormal : TEXCOORD2;
    float3 objectTangent : TEXCOORD3;
    float3 objectBitangent : TEXCOORD4;
    float4 colour : COLOR0;
};

float3 generalMappedNormal(GeneralDiffuseVertexOutput input)
{
    float3 tangentNormal = normalMap.Sample(
        normalMapSampler, input.uv.xy).xyz * 2.0 - 1.0;
    float3 objectNormal =
        input.objectTangent * tangentNormal.x
        + input.objectBitangent * tangentNormal.y
        + input.objectNormal * tangentNormal.z;
    return normalize(mul((float3x3)iTWMat, objectNormal));
}

#if defined(GENERAL_FRAGMENT_DIFFUSE)
float4 GeneralDiffusePS(GeneralDiffuseVertexOutput input) : SV_Target
#else
float4 GeneralEnvironmentPS(GeneralDiffuseVertexOutput input) : SV_Target
#endif
{
    float3 lightDirection = normalize(
        lightPos0.xyz - lightPos0.w * input.worldPosition.xyz);
    float3 mappedNormal = generalMappedNormal(input);
    float diffuse = max(dot(lightDirection, mappedNormal), 0.0);

    float3 cameraDirection = normalize(camPos - input.worldPosition.xyz);
    float3 halfVector = normalize(lightDirection + cameraDirection);
    float specular = pow(
        max(dot(mappedNormal, halfVector), 0.0),
        matShininess);

    float4 diffuseTex = diffuseMap.Sample(diffuseMapSampler, input.uv.xy);
    float3 diffuseColour =
        diffuse * lightDif0 * matDif.rgb * diffuseTex.rgb;
    float3 specularColour = specular * lightSpec0 * matSpec.rgb;
    float3 colour =
        diffuseTex.rgb * ambient + diffuseColour + specularColour;

#if defined(GENERAL_FRAGMENT_ENV)
    float4 environmentTex = envMap.Sample(
        envMapSampler,
        reflect(-cameraDirection, mappedNormal));
    colour = envPars.x * colour + envPars.y * environmentTex.rgb;
#endif

    colour = lerp(colour, fogColor, input.worldPosition.w);
    return float4(colour, diffuseTex.a);
}

#elif defined(GENERAL_VERTEX_SHADOW)

float4x4 wMat;
float4x4 wvpMat;
float4 fogParams;
float4x4 texWVPMat0;
float4x4 texWVPMat1;
float4x4 texWVPMat2;

struct GeneralShadowVertexInput
{
    float4 position : POSITION;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
    float4 colour : COLOR0;
    float3 uv0 : TEXCOORD0;
};

struct GeneralShadowVertexOutput
{
    float4 clipPosition : SV_Position;
    float3 uv : TEXCOORD0;
    float4 worldPosition : TEXCOORD1;
    float3 objectNormal : TEXCOORD2;
    float3 objectTangent : TEXCOORD3;
    float3 objectBitangent : TEXCOORD4;
    float4 colour : COLOR0;
    float4 lightPosition0 : TEXCOORD5;
    float4 lightPosition1 : TEXCOORD6;
    float4 lightPosition2 : TEXCOORD7;
};

GeneralShadowVertexOutput GeneralShadowVS(GeneralShadowVertexInput input)
{
    GeneralShadowVertexOutput output;
    output.clipPosition = mul(wvpMat, input.position);
    output.uv = float3(input.uv0.xy, output.clipPosition.z);
    output.worldPosition = mul(wMat, input.position);
    output.objectNormal = input.normal;
    output.objectTangent = input.tangent;
    output.objectBitangent = cross(input.tangent, input.normal);
    output.colour = input.colour;
    output.worldPosition.w = saturate(
        fogParams.x * (output.clipPosition.z - fogParams.y) * fogParams.w);
    output.lightPosition0 = mul(texWVPMat0, input.position);
    output.lightPosition1 = mul(texWVPMat1, input.position);
    output.lightPosition2 = mul(texWVPMat2, input.position);
    return output;
}

#elif defined(GENERAL_FRAGMENT_SHADOW) || defined(GENERAL_FRAGMENT_SHADOW_ALPHA)

float3 ambient;
float3 lightDif0;
float3 lightSpec0;
float4 matDif;
float4 matSpec;
float matShininess;
float3 fogColor;
float4 lightPos0;
float3 camPos;
float4x4 iTWMat;
float4 invShadowMapSize0;
float4 invShadowMapSize1;
float4 invShadowMapSize2;
float4 pssmSplitPoints;

Texture2D diffuseMap : register(t0);
SamplerState diffuseMapSampler : register(s0);
#if defined(GENERAL_FRAGMENT_SHADOW_ALPHA)
Texture2D diffuseAlpha : register(t1);
SamplerState diffuseAlphaSampler : register(s1);
Texture2D normalMap : register(t2);
SamplerState normalMapSampler : register(s2);
Texture2D shadowMap0 : register(t3);
SamplerState shadowMap0Sampler : register(s3);
Texture2D shadowMap1 : register(t4);
SamplerState shadowMap1Sampler : register(s4);
Texture2D shadowMap2 : register(t5);
SamplerState shadowMap2Sampler : register(s5);
#else
Texture2D normalMap : register(t1);
SamplerState normalMapSampler : register(s1);
Texture2D shadowMap0 : register(t2);
SamplerState shadowMap0Sampler : register(s2);
Texture2D shadowMap1 : register(t3);
SamplerState shadowMap1Sampler : register(s3);
Texture2D shadowMap2 : register(t4);
SamplerState shadowMap2Sampler : register(s4);
#endif

struct GeneralShadowVertexOutput
{
    float4 clipPosition : SV_Position;
    float3 uv : TEXCOORD0;
    float4 worldPosition : TEXCOORD1;
    float3 objectNormal : TEXCOORD2;
    float3 objectTangent : TEXCOORD3;
    float3 objectBitangent : TEXCOORD4;
    float4 colour : COLOR0;
    float4 lightPosition0 : TEXCOORD5;
    float4 lightPosition1 : TEXCOORD6;
    float4 lightPosition2 : TEXCOORD7;
};

float3 generalMappedShadowNormal(GeneralShadowVertexOutput input)
{
    float3 tangentNormal = normalMap.Sample(
        normalMapSampler, input.uv.xy).xyz * 2.0 - 1.0;
    float3 objectNormal =
        input.objectTangent * tangentNormal.x
        + input.objectBitangent * tangentNormal.y
        + input.objectNormal * tangentNormal.z;
    return normalize(mul((float3x3)iTWMat, objectNormal));
}

float generalShadowPcf(
    Texture2D shadowMap,
    SamplerState shadowSampler,
    float4 shadowMapPosition,
    float2 offset)
{
    shadowMapPosition /= shadowMapPosition.w;
    float2 uv = shadowMapPosition.xy;
    float3 sampleOffset = float3(offset, -offset.x) * 0.3;
    float shadowDepth = shadowMapPosition.z;

    float coverage = shadowDepth <= shadowMap.Sample(
        shadowSampler, uv - sampleOffset.xy).r ? 1.0 : 0.0;
    coverage += shadowDepth <= shadowMap.Sample(
        shadowSampler, uv + sampleOffset.xy).r ? 1.0 : 0.0;
    coverage += shadowDepth <= shadowMap.Sample(
        shadowSampler, uv + sampleOffset.zy).r ? 1.0 : 0.0;
    coverage += shadowDepth <= shadowMap.Sample(
        shadowSampler, uv - sampleOffset.zy).r ? 1.0 : 0.0;
    return coverage * 0.25;
}

#if defined(GENERAL_FRAGMENT_SHADOW)
float4 GeneralShadowPS(GeneralShadowVertexOutput input) : SV_Target
#else
float4 GeneralShadowAlphaPS(GeneralShadowVertexOutput input) : SV_Target
#endif
{
    float3 lightDirection = normalize(
        lightPos0.xyz - lightPos0.w * input.worldPosition.xyz);
    float3 mappedNormal = generalMappedShadowNormal(input);
    float diffuse = max(dot(lightDirection, mappedNormal), 0.0);

    float3 cameraDirection = normalize(camPos - input.worldPosition.xyz);
    float3 halfVector = normalize(lightDirection + cameraDirection);
    float specular = pow(
        max(dot(mappedNormal, halfVector), 0.0),
        matShininess);

    float4 diffuseTex = diffuseMap.Sample(diffuseMapSampler, input.uv.xy);
#if defined(GENERAL_FRAGMENT_SHADOW_ALPHA)
    diffuseTex.a = diffuseAlpha.Sample(diffuseAlphaSampler, input.uv.xy).g;
#endif

    float3 diffuseColour =
        diffuse * lightDif0 * matDif.rgb * diffuseTex.rgb;
    float3 specularColour = specular * lightSpec0 * matSpec.rgb;

    float shadowing;
    if (input.uv.z <= pssmSplitPoints.y)
    {
        shadowing = generalShadowPcf(
            shadowMap0,
            shadowMap0Sampler,
            input.lightPosition0,
            invShadowMapSize0.xy);
    }
    else if (input.uv.z <= pssmSplitPoints.z)
    {
        shadowing = generalShadowPcf(
            shadowMap1,
            shadowMap1Sampler,
            input.lightPosition1,
            invShadowMapSize1.xy);
    }
    else
    {
        shadowing = generalShadowPcf(
            shadowMap2,
            shadowMap2Sampler,
            input.lightPosition2,
            invShadowMapSize2.xy);
    }

    float3 colour = diffuseTex.rgb * ambient;
    colour += diffuseColour * (0.25 + 0.75 * shadowing);
    colour += specularColour * shadowing;
    colour = lerp(colour, fogColor, input.worldPosition.w);
    return float4(colour, diffuseTex.a);
}

#else
#error A general shader stage selector is required
#endif
