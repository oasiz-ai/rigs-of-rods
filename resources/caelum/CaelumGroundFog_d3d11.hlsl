// Modern D3D11 port of Caelum's exponentially height-decaying fog.

#if defined(CAELUM_GROUND_FOG_VERTEX)

float4x4 worldViewProj;
float4x4 world;

struct CaelumGroundFogVertexInput
{
    float4 position : POSITION;
};

struct CaelumGroundFogVertexOutput
{
    float4 clipPosition : SV_Position;
    float3 worldPosition : TEXCOORD0;
};

CaelumGroundFogVertexOutput CaelumGroundFogVS(
    CaelumGroundFogVertexInput input)
{
    CaelumGroundFogVertexOutput output;
    output.clipPosition = mul(worldViewProj, input.position);
    output.worldPosition = mul(world, input.position).xyz;
    return output;
}

#elif defined(CAELUM_GROUND_FOG_FRAGMENT)

float3 camPos;
float4 fogColour;
float fogDensity;
float fogVerticalDecay;
float fogGroundLevel;

struct CaelumGroundFogVertexOutput
{
    float4 clipPosition : SV_Position;
    float3 worldPosition : TEXCOORD0;
};

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

float4 CaelumGroundFogPS(CaelumGroundFogVertexOutput input) : SV_Target
{
    float fog = CaelumExpGroundFog(
        length(camPos - input.worldPosition),
        camPos.y,
        input.worldPosition.y,
        fogDensity,
        fogVerticalDecay,
        fogGroundLevel);
    return float4(fogColour.rgb, fog);
}

#elif defined(CAELUM_GROUND_FOG_DOME_VERTEX)

float4x4 worldViewProj;

struct CaelumGroundFogDomeVertexInput
{
    float4 position : POSITION;
};

struct CaelumGroundFogDomeVertexOutput
{
    float4 clipPosition : SV_Position;
    float3 relativePosition : TEXCOORD0;
};

CaelumGroundFogDomeVertexOutput CaelumGroundFogDomeVS(
    CaelumGroundFogDomeVertexInput input)
{
    CaelumGroundFogDomeVertexOutput output;
    output.clipPosition = mul(worldViewProj, input.position);
    output.relativePosition = normalize(input.position.xyz);
    return output;
}

#elif defined(CAELUM_GROUND_FOG_DOME_FRAGMENT)

float cameraHeight;
float4 fogColour;
float fogDensity;
float fogVerticalDecay;
float fogGroundLevel;

struct CaelumGroundFogDomeVertexOutput
{
    float4 clipPosition : SV_Position;
    float3 relativePosition : TEXCOORD0;
};

float CaelumExpGroundFogInfinite(
    float inverseViewSine,
    float startHeight,
    float density,
    float verticalDecay,
    float baseLevel)
{
    return 1.0 - exp(
        -density * inverseViewSine
        * exp(verticalDecay * (baseLevel - startHeight))
        / verticalDecay);
}

float4 CaelumGroundFogDomePS(
    CaelumGroundFogDomeVertexOutput input) : SV_Target
{
    float inverseViewSine = 1.0 / input.relativePosition.y;
    float fog;
    if (fogVerticalDecay < 1e-7)
    {
        fog = 0.0;
    }
    else if (inverseViewSine < 0.0)
    {
        fog = 1.0;
    }
    else
    {
        fog = saturate(CaelumExpGroundFogInfinite(
            inverseViewSine,
            cameraHeight,
            fogDensity,
            fogVerticalDecay,
            fogGroundLevel));
    }
    return float4(fogColour.rgb, fog);
}

#else
#error A Caelum ground-fog shader stage selector is required
#endif
