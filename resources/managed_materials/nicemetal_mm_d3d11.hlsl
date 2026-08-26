// Modern D3D11 port of the historical NiceMetal Cg shader family.
// Each program definition selects exactly one stage and material variant.

#if defined(NICEMETAL_VERTEX_LIT)

float4 lightPosition;
float3 eyePosition;
float4x4 worldviewproj;

struct NiceMetalLitVertexInput
{
    float4 position : POSITION;
    float3 normal : NORMAL;
    float4 colour : COLOR0;
    float2 uv0 : TEXCOORD0;
};

struct NiceMetalLitVertexOutput
{
    float4 clipPosition : SV_Position;
    float4 colour : COLOR0;
    float4 objectPosition : TEXCOORD0;
    float3 objectNormal : TEXCOORD1;
    float4 lightPosition : TEXCOORD2;
    float3 eyePosition : TEXCOORD3;
    float2 uv0 : TEXCOORD4;
};

NiceMetalLitVertexOutput NiceMetalLitVS(NiceMetalLitVertexInput input)
{
    NiceMetalLitVertexOutput output;
    output.clipPosition = mul(worldviewproj, input.position);
    output.colour = input.colour;
    output.objectPosition = input.position;
    output.objectNormal = input.normal;
    output.lightPosition = lightPosition;
    output.eyePosition = eyePosition;
    output.uv0 = input.uv0;
    return output;
}

#elif defined(NICEMETAL_VERTEX_REFLECTION)

float3 camPosition;
float4x4 world;
float4x4 worldViewProj;

struct NiceMetalReflectionVertexInput
{
    float4 position : POSITION;
    float3 normal : NORMAL;
    float4 colour : COLOR0;
    float2 uv0 : TEXCOORD0;
};

struct NiceMetalReflectionVertexOutput
{
    float4 clipPosition : SV_Position;
    float2 uv0 : TEXCOORD0;
    float3 viewDirection : TEXCOORD1;
    float3 worldNormal : TEXCOORD2;
    float4 colour : COLOR0;
};

NiceMetalReflectionVertexOutput NiceMetalReflectionVS(
    NiceMetalReflectionVertexInput input)
{
    NiceMetalReflectionVertexOutput output;
    output.clipPosition = mul(worldViewProj, input.position);
    output.uv0 = input.uv0;
    output.colour = input.colour;
    output.worldNormal = mul((float3x3)world, input.normal);
    output.viewDirection = mul(
        (float3x3)world, input.position.xyz - camPosition);
    return output;
}

#elif defined(NICEMETAL_FRAGMENT_LIT)

float4 lightDiffuse;
float4 lightSpecular;
float exponent;
float4 ambient;

Texture2D diffusetex : register(t0);
SamplerState diffusetexSampler : register(s0);
Texture2D speculartex : register(t1);
SamplerState speculartexSampler : register(s1);
#if !defined(NICEMETAL_NO_DAMAGE) && !defined(NICEMETAL_SIMPLE)
Texture2D diffusedmgtex : register(t2);
SamplerState diffusedmgtexSampler : register(s2);
#endif

struct NiceMetalLitVertexOutput
{
    float4 clipPosition : SV_Position;
    float4 colour : COLOR0;
    float4 objectPosition : TEXCOORD0;
    float3 objectNormal : TEXCOORD1;
    float4 lightPosition : TEXCOORD2;
    float3 eyePosition : TEXCOORD3;
    float2 uv0 : TEXCOORD4;
};

float4 niceMetalLit(float normalDotLight, float normalDotHalf, float power)
{
    float diffuse = max(normalDotLight, 0.0);
    float specular = normalDotLight > 0.0
        ? pow(max(normalDotHalf, 0.0), power)
        : 0.0;
    return float4(1.0, diffuse, specular, 1.0);
}

float4 NiceMetalLitPS(NiceMetalLitVertexOutput input) : SV_Target
{
    float3 surfaceNormal = normalize(input.objectNormal);
    float3 eyeDirection = normalize(
        input.eyePosition - input.objectPosition.xyz);
    float3 lightDirection = normalize(
        input.lightPosition.xyz
        - (input.objectPosition * input.lightPosition.w).xyz);
    float3 halfAngle = normalize(lightDirection + eyeDirection);
    float4 lighting = niceMetalLit(
        dot(lightDirection, surfaceNormal),
        dot(halfAngle, surfaceNormal),
        exponent);

    float4 diffuseSample = diffusetex.Sample(diffusetexSampler, input.uv0);
    float4 textColour;
    float4 specColour;
#if defined(NICEMETAL_SIMPLE)
    textColour = diffuseSample;
    specColour = speculartex.Sample(speculartexSampler, input.uv0);
#elif defined(NICEMETAL_NO_DAMAGE)
    textColour = diffuseSample * (1.0 - input.colour.b / 3.0);
    specColour = speculartex.Sample(speculartexSampler, input.uv0)
        + input.colour.b / 3.0 - input.colour.a / 2.0;
#else
    textColour = diffuseSample * (1.0 - input.colour.a);
    textColour += diffusedmgtex.Sample(diffusedmgtexSampler, input.uv0)
        * input.colour.a;
    textColour *= 1.0 - input.colour.b / 3.0;
    specColour = speculartex.Sample(speculartexSampler, input.uv0)
        + input.colour.b / 3.0 - input.colour.a / 2.0;
#endif

    float4 outputColour = lerp(
        lightDiffuse * textColour * lighting.y + textColour * ambient,
        lightSpecular * lighting.z,
        specColour);
#if defined(NICEMETAL_TRANSPARENT)
    outputColour.a = diffuseSample.a;
#else
    outputColour.a = 1.0;
#endif
    return outputColour;
}

#elif defined(NICEMETAL_FRAGMENT_REFLECTION)

Texture2D speculartex : register(t0);
SamplerState speculartexSampler : register(s0);
TextureCube cubeMap : register(t1);
SamplerState cubeMapSampler : register(s1);

struct NiceMetalReflectionVertexOutput
{
    float4 clipPosition : SV_Position;
    float2 uv0 : TEXCOORD0;
    float3 viewDirection : TEXCOORD1;
    float3 worldNormal : TEXCOORD2;
    float4 colour : COLOR0;
};

float4 NiceMetalReflectionPS(
    NiceMetalReflectionVertexOutput input) : SV_Target
{
    float3 surfaceNormal = normalize(input.worldNormal);
    float3 reflectedDirection = reflect(
        input.viewDirection, surfaceNormal);
    reflectedDirection.z = -reflectedDirection.z;

    float4 reflectedColour = cubeMap.Sample(
        cubeMapSampler, reflectedDirection);
    float4 emissiveColour = speculartex.Sample(
        speculartexSampler, input.uv0);
#if !defined(NICEMETAL_NO_VERTEX_COLOUR)
    emissiveColour += input.colour.b / 3.0 - input.colour.a / 2.0;
#endif
    float4 outputColour = reflectedColour * emissiveColour;
    outputColour.a = 1.0;
    return outputColour;
}

#else
#error A NiceMetal shader stage selector is required
#endif
