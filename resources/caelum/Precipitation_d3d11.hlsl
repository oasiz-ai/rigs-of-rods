// Modern D3D11 port of Caelum's three-layer precipitation compositor.

#if defined(CAELUM_PRECIPITATION_VERTEX)

float4x4 worldviewproj_matrix;

struct CaelumPrecipitationVertexInput
{
    float4 position : POSITION;
};

struct CaelumPrecipitationVertexOutput
{
    float4 clipPosition : SV_Position;
    float2 screenPosition : TEXCOORD0;
};

CaelumPrecipitationVertexOutput CaelumPrecipitationVS(
    CaelumPrecipitationVertexInput input)
{
    CaelumPrecipitationVertexOutput output;
    output.clipPosition = mul(worldviewproj_matrix, input.position);
    float2 signedPosition = sign(input.position.xy);
    output.screenPosition =
        (float2(signedPosition.x, -signedPosition.y) + 1.0) * 0.5;
    return output;
}

#elif defined(CAELUM_PRECIPITATION_FRAGMENT)

Texture2D scene : register(t0);
SamplerState sceneSampler : register(s0);
Texture2D samplerPrec : register(t1);
SamplerState precipitationSampler : register(s1);

float intensity;
float4 ambient_light_colour;
float4 corner1;
float4 corner2;
float4 corner3;
float4 corner4;
float4 deltaX;
float4 deltaY;
float4 precColor;

struct CaelumPrecipitationVertexOutput
{
    float4 clipPosition : SV_Position;
    float2 screenPosition : TEXCOORD0;
};

float2 CaelumCylindricalCoordinates(float4 direction)
{
    float radius = 0.5;
    direction *= radius / pow(length(direction.xz), 0.33);
    return float2(-atan2(direction.z, direction.x), -direction.y);
}

float CaelumPrecipitationAlpha(
    float2 cylindricalCoordinates,
    float layerIntensity,
    float2 delta)
{
    float4 precipitation = samplerPrec.Sample(
        precipitationSampler,
        cylindricalCoordinates - delta);
    return precipitation.g < layerIntensity ? precipitation.r : 1.0;
}

float4 CaelumPrecipitationPS(
    CaelumPrecipitationVertexOutput input) : SV_Target
{
    float2 screenPosition = input.screenPosition;
    float4 eyeDirection = lerp(
        lerp(corner1, corner3, screenPosition.y),
        lerp(corner2, corner4, screenPosition.y),
        screenPosition.x);
    float4 sceneColour = scene.Sample(sceneSampler, screenPosition);
    float2 cylindricalCoordinates =
        CaelumCylindricalCoordinates(eyeDirection);
    float firstLayer = CaelumPrecipitationAlpha(
        cylindricalCoordinates,
        intensity / 4.0,
        float2(deltaX.x, deltaY.x));
    float secondLayer = CaelumPrecipitationAlpha(
        cylindricalCoordinates,
        intensity / 4.0,
        float2(deltaX.y, deltaY.y));
    float thirdLayer = CaelumPrecipitationAlpha(
        cylindricalCoordinates,
        intensity / 4.0,
        float2(deltaX.z, deltaY.z));
    float precipitation = min(min(firstLayer, secondLayer), thirdLayer);
    return lerp(precColor, sceneColour, precipitation);
}

#else
#error A Caelum precipitation shader stage selector is required
#endif
