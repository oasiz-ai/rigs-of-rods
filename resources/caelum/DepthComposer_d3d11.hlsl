// Modern D3D11 port of Caelum's depth rendering and atmospheric composer.

#if defined(CAELUM_DEPTH_COMPOSER_FRAGMENT)

float4x4 invViewProjMatrix;
float4 worldCameraPos;
float groundFogDensity;
float groundFogVerticalDecay;
float groundFogBaseLevel;
float4 groundFogColour;
float3 hazeColour;
float3 sunDirection;

Texture2D screenTexture : register(t0);
SamplerState screenTextureSampler : register(s0);
Texture2D depthTexture : register(t1);
SamplerState depthTextureSampler : register(s1);
Texture1D atmRelativeDepth : register(t2);
SamplerState atmRelativeDepthSampler : register(s2);

struct CaelumCompositorFragmentInput
{
    float4 clipPosition : SV_Position;
    float2 screenPosition : TEXCOORD0;
};

#if defined(CAELUM_DEPTH_EXP_GROUND_FOG)
float CaelumExpDiv(float value)
{
    if (abs(value) < 0.0001)
    {
        return 1.0;
    }
    return (exp(value) - 1.0) / value;
}

float CaelumExpGroundFog(
    float distanceThroughFog,
    float startHeight,
    float endHeight,
    float density,
    float verticalDecay,
    float baseLevel)
{
    float deltaHeight = endHeight - startHeight;
    return 1.0 - exp(
        -density * distanceThroughFog
        * exp(verticalDecay * (baseLevel - startHeight))
        * CaelumExpDiv(-verticalDecay * deltaHeight));
}
#endif

#if defined(CAELUM_DEPTH_SKY_DOME_HAZE)
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

float4 CaelumCalculateHaze(
    float3 worldPosition,
    float3 worldCameraPosition,
    float3 baseHazeColour,
    float3 directionToSun)
{
    float haze = length(worldCameraPosition - worldPosition);
    float incidenceAngleCos = dot(
        -directionToSun,
        normalize(worldPosition - worldCameraPosition));
    float sunY = -directionToSun.y;
    float4 sunColour = float4(3.0, 2.5, 1.0, 1.0);
    float atmosphereLightAbsorptionFactor = 0.1;
    float fogDensity = 15.0;
    haze = CaelumFogExp(
        haze * 0.005,
        atmosphereLightAbsorptionFactor);
    float inverseHazeHeight = 100.0;
    float hazeAbsorption = CaelumFogExp(
        pow(max(1.0 - sunY, 0.0), inverseHazeHeight),
        fogDensity);

    if (incidenceAngleCos > 0.0)
    {
        float sunlightScatteringFactor = 0.1;
        float sunlightScatteringLossFactor = 0.3;
        float4 inscatter = CaelumSunlightInscatter(
            sunColour,
            saturate(
                (1.0 - atmRelativeDepth.Sample(
                    atmRelativeDepthSampler,
                    sunY).r) * hazeAbsorption),
            saturate(incidenceAngleCos),
            sunlightScatteringFactor)
            * (1.0 - sunlightScatteringLossFactor);
        baseHazeColour =
            baseHazeColour * (1.0 - inscatter.a)
            + inscatter.rgb * inscatter.a * haze;
    }
    return float4(baseHazeColour, haze);
}
#endif

float4 CaelumDepthComposerPS(
    CaelumCompositorFragmentInput input) : SV_Target
{
    float4 inputColour = screenTexture.Sample(
        screenTextureSampler,
        input.screenPosition);
    float inputDepth = depthTexture.Sample(
        depthTextureSampler,
        input.screenPosition).r;
    float4 devicePosition = float4(
        input.screenPosition.x * 2.0 - 1.0,
        1.0 - input.screenPosition.y * 2.0,
        inputDepth,
        1.0);
    float4 worldPosition = mul(invViewProjMatrix, devicePosition);
    worldPosition /= worldPosition.w;

    float4 colour = inputColour;

#if defined(CAELUM_DEPTH_DEBUG_RENDER)
    colour = worldPosition * float4(0.001, 0.01, 0.001, 1.0);
#endif

#if defined(CAELUM_DEPTH_EXP_GROUND_FOG)
    float fogFactor = CaelumExpGroundFog(
        length(worldCameraPos - worldPosition),
        worldCameraPos.y,
        worldPosition.y,
        groundFogDensity,
        groundFogVerticalDecay,
        groundFogBaseLevel);
    colour = lerp(colour, groundFogColour, fogFactor);
#endif

#if defined(CAELUM_DEPTH_SKY_DOME_HAZE)
    float4 hazeValue = CaelumCalculateHaze(
        worldPosition.xyz,
        worldCameraPos.xyz,
        hazeColour,
        sunDirection);
    colour.rgb = lerp(colour.rgb, hazeValue.rgb, hazeValue.a);
#endif

    return colour;
}

#elif defined(CAELUM_DEPTH_RENDER_VERTEX)

float4x4 wvpMatrix;

struct CaelumDepthRenderVertexInput
{
    float4 position : POSITION;
};

struct CaelumDepthRenderVertexOutput
{
    float4 clipPosition : SV_Position;
    float4 magic : TEXCOORD0;
};

CaelumDepthRenderVertexOutput CaelumDepthRenderVS(
    CaelumDepthRenderVertexInput input)
{
    CaelumDepthRenderVertexOutput output;
    output.clipPosition = mul(wvpMatrix, input.position);
    output.magic = output.clipPosition;
    return output;
}

#elif defined(CAELUM_DEPTH_RENDER_FRAGMENT)

struct CaelumDepthRenderVertexOutput
{
    float4 clipPosition : SV_Position;
    float4 magic : TEXCOORD0;
};

float4 CaelumDepthRenderPS(
    CaelumDepthRenderVertexOutput input) : SV_Target
{
    return input.magic.z / input.magic.w;
}

#elif defined(CAELUM_DEPTH_ALPHA_VERTEX)

float4x4 wvpMatrix;

struct CaelumDepthAlphaVertexInput
{
    float4 position : POSITION;
    float4 texcoord : TEXCOORD0;
};

struct CaelumDepthAlphaVertexOutput
{
    float4 clipPosition : SV_Position;
    float4 texcoord : TEXCOORD0;
    float4 magic : TEXCOORD1;
};

CaelumDepthAlphaVertexOutput CaelumDepthAlphaVS(
    CaelumDepthAlphaVertexInput input)
{
    CaelumDepthAlphaVertexOutput output;
    output.clipPosition = mul(wvpMatrix, input.position);
    output.magic = output.clipPosition;
    output.texcoord = input.texcoord;
    return output;
}

#elif defined(CAELUM_DEPTH_ALPHA_FRAGMENT)

Texture2D mainTex : register(t0);
SamplerState mainTexSampler : register(s0);

struct CaelumDepthAlphaVertexOutput
{
    float4 clipPosition : SV_Position;
    float4 texcoord : TEXCOORD0;
    float4 magic : TEXCOORD1;
};

float4 CaelumDepthAlphaPS(
    CaelumDepthAlphaVertexOutput input) : SV_Target
{
    float alpha = mainTex.Sample(mainTexSampler, input.texcoord.xy).a;
    float depth = input.magic.z / input.magic.w;
    return float4(depth, depth, depth, alpha);
}

#else
#error A Caelum depth shader stage selector is required
#endif
