// Modern D3D11 port of the historical reflection/refraction water shaders.
// Each program definition selects exactly one stage and material variant.

#if defined(FRESNEL_WATER_VERTEX)

float4x4 worldViewProjMatrix;
float3 eyePosition;
float fresnelBias;
float fresnelScale;
float fresnelPower;
float timeVal;
float scale;
float scroll;
float noise;

struct FresnelWaterVertexInput
{
    float4 position : POSITION;
    float3 normal : NORMAL;
    float2 uv0 : TEXCOORD0;
};

struct FresnelWaterVertexOutput
{
    float4 clipPosition : SV_Position;
    float fresnelFactor : COLOR0;
    float3 noiseCoord : TEXCOORD0;
    float4 projectionCoord : TEXCOORD1;
};

FresnelWaterVertexOutput FresnelWaterVS(FresnelWaterVertexInput input)
{
    FresnelWaterVertexOutput output;
    output.clipPosition = mul(worldViewProjMatrix, input.position);

    // Preserve the original projective scale/bias transform, including its
    // render-texture Y flip, without relying on matrix constructor order.
    output.projectionCoord = float4(
        0.5 * output.clipPosition.x + 0.5 * output.clipPosition.w,
        -0.5 * output.clipPosition.y + 0.5 * output.clipPosition.w,
        0.5 * output.clipPosition.z + 0.5 * output.clipPosition.w,
        output.clipPosition.w);

    output.noiseCoord.xy = (input.uv0 + timeVal * scroll) * scale;
    output.noiseCoord.z = noise * timeVal;

    float3 eyeDirection = normalize(input.position.xyz - eyePosition);
    output.fresnelFactor = fresnelBias + fresnelScale * pow(
        1.0 + dot(eyeDirection, input.normal), fresnelPower);
    return output;
}

#elif defined(FRESNEL_WATER_FRAGMENT_FULL)

float distortionRange;
float4 tintColour;

Texture3D noiseMap : register(t0);
SamplerState noiseMapSampler : register(s0);
Texture2D reflectMap : register(t1);
SamplerState reflectMapSampler : register(s1);
Texture2D refractMap : register(t2);
SamplerState refractMapSampler : register(s2);

struct FresnelWaterVertexOutput
{
    float4 clipPosition : SV_Position;
    float fresnelFactor : COLOR0;
    float3 noiseCoord : TEXCOORD0;
    float4 projectionCoord : TEXCOORD1;
};

float4 FresnelWaterFullPS(FresnelWaterVertexOutput input) : SV_Target
{
    const float3 yOffset = float3(0.31, 0.58, 0.23);
    float2 distortion;
    distortion.x = noiseMap.Sample(noiseMapSampler, input.noiseCoord).x;
    distortion.y = noiseMap.Sample(
        noiseMapSampler, input.noiseCoord + yOffset).x;
    distortion = (distortion * 2.0 - 1.0) * distortionRange;

    float2 projectedUv = input.projectionCoord.xy
        / input.projectionCoord.w;
    projectedUv += distortion;

    float4 reflectionColour = reflectMap.Sample(
        reflectMapSampler, projectedUv);
    float4 refractionColour = refractMap.Sample(
        refractMapSampler, projectedUv) + tintColour;
    return lerp(refractionColour, reflectionColour, input.fresnelFactor);
}

#elif defined(FRESNEL_WATER_FRAGMENT_REFLECTION)

float distortionRange;
float4 tintColour;

Texture3D noiseMap : register(t0);
SamplerState noiseMapSampler : register(s0);
Texture2D reflectMap : register(t1);
SamplerState reflectMapSampler : register(s1);

struct FresnelWaterVertexOutput
{
    float4 clipPosition : SV_Position;
    float fresnelFactor : COLOR0;
    float3 noiseCoord : TEXCOORD0;
    float4 projectionCoord : TEXCOORD1;
};

float4 FresnelWaterReflectionPS(
    FresnelWaterVertexOutput input) : SV_Target
{
    const float3 yOffset = float3(0.31, 0.58, 0.23);
    float2 distortion;
    distortion.x = noiseMap.Sample(noiseMapSampler, input.noiseCoord).x;
    distortion.y = noiseMap.Sample(
        noiseMapSampler, input.noiseCoord + yOffset).x;
    distortion = (distortion * 2.0 - 1.0) * distortionRange;

    float2 projectedUv = input.projectionCoord.xy
        / input.projectionCoord.w;
    projectedUv += distortion;

    float4 reflectionColour = reflectMap.Sample(
        reflectMapSampler, projectedUv);
    float reflectionFactor = 0.33 + input.fresnelFactor;
    float4 outputColour = lerp(
        tintColour, reflectionColour, reflectionFactor);
    outputColour.a = reflectionFactor;
    return outputColour;
}

#else
#error A Fresnel water shader stage selector is required
#endif
