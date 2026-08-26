// Modern D3D11 port of Caelum's sky-dome and distance-haze shaders.

#if defined(CAELUM_SKYDOME_VERTEX)

float lightAbsorption;
float4x4 worldViewProj;
float3 sunDirection;

struct CaelumSkyDomeVertexInput
{
    float4 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
};

struct CaelumSkyDomeVertexOutput
{
    float4 clipPosition : SV_Position;
    float4 colour : COLOR0;
    float2 uv : TEXCOORD0;
    float incidenceAngleCos : TEXCOORD1;
    float sunY : TEXCOORD2;
    float3 normal : TEXCOORD3;
};

CaelumSkyDomeVertexOutput CaelumSkyDomeVS(
    CaelumSkyDomeVertexInput input)
{
    CaelumSkyDomeVertexOutput output;
    float3 normalizedSunDirection = normalize(sunDirection);
    float3 normalizedNormal = normalize(input.normal);
    float cosine = dot(-normalizedSunDirection, normalizedNormal);
    output.incidenceAngleCos = -cosine;
    output.sunY = -normalizedSunDirection.y;
    output.clipPosition = mul(worldViewProj, input.position);
    output.colour = float4(1.0, 1.0, 1.0, 1.0);
    output.uv = input.uv;
    output.normal = -normalizedNormal;
    return output;
}

#elif defined(CAELUM_SKYDOME_FRAGMENT_HAZE) \
    || defined(CAELUM_SKYDOME_FRAGMENT_NO_HAZE)

Texture2D gradientsMap : register(t0);
SamplerState gradientsMapSampler : register(s0);
Texture1D atmRelativeDepth : register(t1);
SamplerState atmRelativeDepthSampler : register(s1);

float4 hazeColour;
float offset;

struct CaelumSkyDomeVertexOutput
{
    float4 clipPosition : SV_Position;
    float4 colour : COLOR0;
    float2 uv : TEXCOORD0;
    float incidenceAngleCos : TEXCOORD1;
    float sunY : TEXCOORD2;
    float3 normal : TEXCOORD3;
};

float CaelumBias(float biasValue, float value)
{
    return pow(value, log(biasValue) / log(0.5));
}

float4 CaelumSunlightInscatter(
    float4 sunColour,
    float absorption,
    float incidenceAngleCos,
    float sunlightScatteringFactor)
{
    float scatteredSunlight = CaelumBias(
        sunlightScatteringFactor * 0.5,
        incidenceAngleCos);
    sunColour *= (1.0 - absorption)
        * float4(0.9, 0.5, 0.09, 1.0);
    return sunColour * scatteredSunlight;
}

float CaelumFogExp(float distanceValue, float density)
{
    return 1.0 - saturate(pow(2.71828, -distanceValue * density));
}

float4 CaelumSkyDomePS(CaelumSkyDomeVertexOutput input) : SV_Target
{
    float4 sunColour = float4(3.0, 3.0, 3.0, 1.0);

#if defined(CAELUM_SKYDOME_FRAGMENT_HAZE)
    float fogDensity = 15.0;
    float inverseHazeHeight = 100.0;
    float haze = CaelumFogExp(
        pow(saturate(1.0 - input.normal.y), inverseHazeHeight),
        fogDensity);
#endif

    float4 colour = gradientsMap.Sample(
        gradientsMapSampler,
        input.uv + float2(offset, 0.0)) * input.colour;

    if (input.incidenceAngleCos > 0.0)
    {
        float sunlightScatteringFactor = 0.05;
        float sunlightScatteringLossFactor = 0.1;
        float atmosphereLightAbsorptionFactor = 0.1;
        colour.rgb += CaelumSunlightInscatter(
            sunColour,
            saturate(
                atmosphereLightAbsorptionFactor
                * (1.0 - atmRelativeDepth.Sample(
                    atmRelativeDepthSampler,
                    input.sunY).r)),
            saturate(input.incidenceAngleCos),
            sunlightScatteringFactor).rgb
            * (1.0 - sunlightScatteringLossFactor);
    }

#if defined(CAELUM_SKYDOME_FRAGMENT_HAZE)
    float4 opaqueHazeColour = float4(hazeColour.rgb, 1.0);
    colour = colour * (1.0 - haze) + opaqueHazeColour * haze;
#endif

    return colour;
}

#elif defined(CAELUM_HAZE_VERTEX)

float4x4 worldViewProj;
float4 camPos;
float3 sunDirection;

struct CaelumHazeVertexInput
{
    float4 position : POSITION;
    float3 normal : NORMAL;
};

struct CaelumHazeVertexOutput
{
    float4 clipPosition : SV_Position;
    float hazeDistance : TEXCOORD0;
    float2 sunlight : TEXCOORD1;
};

CaelumHazeVertexOutput CaelumHazeVS(CaelumHazeVertexInput input)
{
    CaelumHazeVertexOutput output;
    float3 normalizedSunDirection = normalize(sunDirection);
    output.clipPosition = mul(worldViewProj, input.position);
    output.hazeDistance = length(camPos - input.position);
    output.sunlight.x = dot(
        -normalizedSunDirection,
        normalize(input.position.xyz - camPos.xyz));
    output.sunlight.y = -normalizedSunDirection.y;
    return output;
}

#elif defined(CAELUM_HAZE_FRAGMENT)

Texture1D atmRelativeDepth : register(t0);
SamplerState atmRelativeDepthSampler : register(s0);
Texture2D gradientsMap : register(t1);
SamplerState gradientsMapSampler : register(s1);

float4 fogColour;

struct CaelumHazeVertexOutput
{
    float4 clipPosition : SV_Position;
    float hazeDistance : TEXCOORD0;
    float2 sunlight : TEXCOORD1;
};

float CaelumBias(float biasValue, float value)
{
    return pow(value, log(biasValue) / log(0.5));
}

float4 CaelumSunlightInscatter(
    float4 sunColour,
    float absorption,
    float incidenceAngleCos,
    float sunlightScatteringFactor)
{
    float scatteredSunlight = CaelumBias(
        sunlightScatteringFactor * 0.5,
        incidenceAngleCos);
    sunColour *= (1.0 - absorption)
        * float4(0.9, 0.5, 0.09, 1.0);
    return sunColour * scatteredSunlight;
}

float CaelumFogExp(float distanceValue, float density)
{
    return 1.0 - saturate(pow(2.71828, -distanceValue * density));
}

float4 CaelumHazePS(CaelumHazeVertexOutput input) : SV_Target
{
    float incidenceAngleCos = input.sunlight.x;
    float sunY = input.sunlight.y;
    float4 sunColour = float4(3.0, 2.5, 1.0, 1.0);
    float atmosphereLightAbsorptionFactor = 0.1;
    float fogDensity = 15.0;
    float haze = CaelumFogExp(
        input.hazeDistance * 0.005,
        atmosphereLightAbsorptionFactor);
    float inverseHazeHeight = 100.0;
    float hazeAbsorption = CaelumFogExp(
        pow(1.0 - sunY, inverseHazeHeight),
        fogDensity);

    float4 hazeOutput = fogColour;
    if (incidenceAngleCos > 0.0)
    {
        float sunlightScatteringFactor = 0.1;
        float sunlightScatteringLossFactor = 0.3;
        float4 sunlightInscatterColour = CaelumSunlightInscatter(
            sunColour,
            saturate(
                (1.0 - atmRelativeDepth.Sample(
                    atmRelativeDepthSampler,
                    sunY).r) * hazeAbsorption),
            saturate(incidenceAngleCos),
            sunlightScatteringFactor)
            * (1.0 - sunlightScatteringLossFactor);
        hazeOutput.rgb =
            hazeOutput.rgb * (1.0 - sunlightInscatterColour.a)
            + sunlightInscatterColour.rgb
            * sunlightInscatterColour.a * haze;
    }
    hazeOutput.a = haze;
    return hazeOutput;
}

#else
#error A Caelum sky-dome shader stage selector is required
#endif
