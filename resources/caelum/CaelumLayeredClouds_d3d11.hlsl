// Modern D3D11 port of Caelum's layered, animated cloud shader.

#if defined(CAELUM_LAYERED_CLOUDS_VERTEX)

float4x4 worldViewProj;
float4x4 worldMatrix;
float3 sunDirection;

struct CaelumLayeredCloudsVertexInput
{
    float4 position : POSITION;
    float2 uv : TEXCOORD0;
};

struct CaelumLayeredCloudsVertexOutput
{
    float4 clipPosition : SV_Position;
    float2 uv : TEXCOORD0;
    float3 relativePosition : TEXCOORD1;
    float sunGlow : TEXCOORD2;
    float4 worldPosition : TEXCOORD3;
};

CaelumLayeredCloudsVertexOutput CaelumLayeredCloudsVS(
    CaelumLayeredCloudsVertexInput input)
{
    CaelumLayeredCloudsVertexOutput output;
    output.clipPosition = mul(worldViewProj, input.position);
    output.worldPosition = mul(worldMatrix, input.position);
    output.uv = input.uv;
    output.relativePosition = normalize(input.position.xyz);
    output.sunGlow = dot(
        output.relativePosition,
        normalize(-sunDirection));
    return output;
}

#elif defined(CAELUM_LAYERED_CLOUDS_FRAGMENT)

Texture2D cloud_shape1 : register(t0);
SamplerState cloud_shape1Sampler : register(s0);
Texture2D cloud_shape2 : register(t1);
SamplerState cloud_shape2Sampler : register(s1);
Texture2D cloud_detail : register(t2);
SamplerState cloud_detailSampler : register(s2);

float cloudMassInvScale;
float cloudDetailInvScale;
float2 cloudMassOffset;
float2 cloudDetailOffset;
float cloudMassBlend;
float cloudDetailBlend;
float cloudCoverageThreshold;
float4 sunLightColour;
float4 sunSphereColour;
float4 fogColour;
float4 sunDirection;
float cloudSharpness;
float cloudThickness;
float3 camera_position;
float3 fadeDistMeasurementVector;
float layerHeight;
float cloudUVFactor;
float heightRedFactor;
float nearFadeDist;
float farFadeDist;

struct CaelumLayeredCloudsVertexOutput
{
    float4 clipPosition : SV_Position;
    float2 uv : TEXCOORD0;
    float3 relativePosition : TEXCOORD1;
    float sunGlow : TEXCOORD2;
    float4 worldPosition : TEXCOORD3;
};

float CaelumCloudIntensity(float2 position)
{
    float2 finalMassOffset = cloudMassOffset + position;
    float mass = lerp(
        cloud_shape1.Sample(
            cloud_shape1Sampler,
            finalMassOffset * cloudMassInvScale).r,
        cloud_shape2.Sample(
            cloud_shape2Sampler,
            finalMassOffset * cloudMassInvScale).r,
        cloudMassBlend);
    float detail = cloud_detail.Sample(
        cloud_detailSampler,
        (cloudDetailOffset + position) * cloudDetailInvScale).r;
    mass = (mass + detail * cloudDetailBlend) / (1.0 + cloudDetailBlend);
    return max(0.0, mass - cloudCoverageThreshold);
}

float3 CaelumYuvFromRgb(float3 colour)
{
    return float3(
        dot(colour, float3(0.299, 0.587, 0.114)),
        dot(colour, float3(-0.14713, -0.28886, 0.436)),
        dot(colour, float3(0.615, -0.51499, -0.10001)));
}

float3 CaelumRgbFromYuv(float3 colour)
{
    return float3(
        dot(colour, float3(1.0, 0.0, 1.13983)),
        dot(colour, float3(1.0, -0.39465, -0.58060)),
        dot(colour, float3(1.0, 2.03211, 0.0)));
}

float3 CaelumMagicColourMix(float3 intensityColour, float3 chromaColour)
{
    float3 intensityYuv = CaelumYuvFromRgb(intensityColour);
    float3 chromaYuv = CaelumYuvFromRgb(chromaColour);
    return saturate(CaelumRgbFromYuv(float3(
        intensityYuv.x,
        chromaYuv.y,
        chromaYuv.z)));
}

float4 CaelumOldCloudColour(float2 uv, float sunGlow)
{
    float intensity = CaelumCloudIntensity(uv);
    float alpha = saturate(exp(cloudSharpness * intensity) - 1.0);
    float shine = pow(saturate(sunGlow), 8.0) / 4.0;
    float3 sunColour = sunLightColour.rgb * 1.5;
    float3 cloudColour = fogColour.rgb * (1.0 - intensity / 3.0);
    float thickness = saturate(
        0.8 - exp(-cloudThickness * (intensity + 0.2 - shine)));
    return float4(lerp(sunColour, cloudColour, thickness), alpha);
}

float4 CaelumLayeredCloudsPS(
    CaelumLayeredCloudsVertexOutput input) : SV_Target
{
    float2 uv = input.uv * cloudUVFactor;
    float4 colour = CaelumOldCloudColour(uv, input.sunGlow);
    colour.r += layerHeight / heightRedFactor;

    float distanceFromCamera = length(
        (input.worldPosition.xyz - camera_position)
        * fadeDistMeasurementVector);
    float alphaModifier = 1.0;
    if (distanceFromCamera > nearFadeDist)
    {
        alphaModifier = saturate(
            (farFadeDist - distanceFromCamera)
            / (farFadeDist - nearFadeDist));
    }
    float alpha = colour.a * alphaModifier;

    float3 cloudDirection = normalize(
        float3(input.worldPosition.x, layerHeight, input.worldPosition.y)
        - camera_position);
    float angleDifference = saturate(
        dot(cloudDirection, normalize(sunDirection.xyz)));
    float3 illuminatedColour = lerp(
        colour.rgb,
        CaelumMagicColourMix(colour.rgb, sunSphereColour.rgb),
        angleDifference);
    colour.rgb = lerp(illuminatedColour, colour.rgb, alpha);
    colour.a = alpha;
    return colour;
}

#else
#error A Caelum layered-cloud shader stage selector is required
#endif
