#version 330 core

// Modern GL3Plus port of Caelum's sky-dome and distance-haze shaders.

#if defined(CAELUM_SKYDOME_VERTEX)

in vec4 vertex;
in vec3 normal;
in vec2 uv0;

uniform float lightAbsorption;
uniform mat4 worldViewProj;
uniform vec3 sunDirection;

out vec4 caelumSkyColour;
out vec2 caelumSkyUv;
out float caelumSkyIncidenceAngleCos;
out float caelumSkySunY;
out vec3 caelumSkyNormal;

void main()
{
    vec3 normalizedSunDirection = normalize(sunDirection);
    vec3 normalizedNormal = normalize(normal);
    float cosine = dot(-normalizedSunDirection, normalizedNormal);
    caelumSkyIncidenceAngleCos = -cosine;
    caelumSkySunY = -normalizedSunDirection.y;
    gl_Position = worldViewProj * vertex;
    caelumSkyColour = vec4(1.0);
    caelumSkyUv = uv0;
    caelumSkyNormal = -normalizedNormal;
}

#elif defined(CAELUM_SKYDOME_FRAGMENT_HAZE) || defined(CAELUM_SKYDOME_FRAGMENT_NO_HAZE)

in vec4 caelumSkyColour;
in vec2 caelumSkyUv;
in float caelumSkyIncidenceAngleCos;
in float caelumSkySunY;
in vec3 caelumSkyNormal;

uniform sampler2D gradientsMap;
uniform sampler1D atmRelativeDepth;
uniform vec4 hazeColour;
uniform float offset;

out vec4 fragColour;

float caelumBias(float biasValue, float value)
{
    return pow(value, log(biasValue) / log(0.5));
}

vec4 caelumSunlightInscatter(
    vec4 sunColour,
    float absorption,
    float incidenceAngleCos,
    float sunlightScatteringFactor)
{
    float scatteredSunlight = caelumBias(
        sunlightScatteringFactor * 0.5,
        incidenceAngleCos);
    sunColour *= (1.0 - absorption) * vec4(0.9, 0.5, 0.09, 1.0);
    return sunColour * scatteredSunlight;
}

float caelumFogExp(float distanceValue, float density)
{
    return 1.0 - clamp(
        pow(2.71828, -distanceValue * density),
        0.0,
        1.0);
}

void main()
{
    vec4 sunColour = vec4(3.0, 3.0, 3.0, 1.0);

#if defined(CAELUM_SKYDOME_FRAGMENT_HAZE)
    float fogDensity = 15.0;
    float inverseHazeHeight = 100.0;
    float haze = caelumFogExp(
        pow(clamp(1.0 - caelumSkyNormal.y, 0.0, 1.0), inverseHazeHeight),
        fogDensity);
#endif

    vec4 colour = texture(
        gradientsMap,
        caelumSkyUv + vec2(offset, 0.0)) * caelumSkyColour;

    if (caelumSkyIncidenceAngleCos > 0.0)
    {
        float sunlightScatteringFactor = 0.05;
        float sunlightScatteringLossFactor = 0.1;
        float atmosphereLightAbsorptionFactor = 0.1;
        colour.rgb += caelumSunlightInscatter(
            sunColour,
            clamp(
                atmosphereLightAbsorptionFactor
                * (1.0 - texture(
                    atmRelativeDepth,
                    caelumSkySunY).r),
                0.0,
                1.0),
            clamp(caelumSkyIncidenceAngleCos, 0.0, 1.0),
            sunlightScatteringFactor).rgb
            * (1.0 - sunlightScatteringLossFactor);
    }

#if defined(CAELUM_SKYDOME_FRAGMENT_HAZE)
    vec4 opaqueHazeColour = vec4(hazeColour.rgb, 1.0);
    colour = colour * (1.0 - haze) + opaqueHazeColour * haze;
#endif

    fragColour = colour;
}

#elif defined(CAELUM_HAZE_VERTEX)

in vec4 vertex;
in vec3 normal;

uniform mat4 worldViewProj;
uniform vec4 camPos;
uniform vec3 sunDirection;

out float caelumHazeDistance;
out vec2 caelumHazeSunlight;

void main()
{
    vec3 normalizedSunDirection = normalize(sunDirection);
    gl_Position = worldViewProj * vertex;
    caelumHazeDistance = length(camPos - vertex);
    caelumHazeSunlight.x = dot(
        -normalizedSunDirection,
        normalize(vertex.xyz - camPos.xyz));
    caelumHazeSunlight.y = -normalizedSunDirection.y;
}

#elif defined(CAELUM_HAZE_FRAGMENT)

in float caelumHazeDistance;
in vec2 caelumHazeSunlight;

uniform sampler1D atmRelativeDepth;
uniform sampler2D gradientsMap;
uniform vec4 fogColour;

out vec4 fragColour;

float caelumBias(float biasValue, float value)
{
    return pow(value, log(biasValue) / log(0.5));
}

vec4 caelumSunlightInscatter(
    vec4 sunColour,
    float absorption,
    float incidenceAngleCos,
    float sunlightScatteringFactor)
{
    float scatteredSunlight = caelumBias(
        sunlightScatteringFactor * 0.5,
        incidenceAngleCos);
    sunColour *= (1.0 - absorption) * vec4(0.9, 0.5, 0.09, 1.0);
    return sunColour * scatteredSunlight;
}

float caelumFogExp(float distanceValue, float density)
{
    return 1.0 - clamp(
        pow(2.71828, -distanceValue * density),
        0.0,
        1.0);
}

void main()
{
    float incidenceAngleCos = caelumHazeSunlight.x;
    float sunY = caelumHazeSunlight.y;
    vec4 sunColour = vec4(3.0, 2.5, 1.0, 1.0);
    float atmosphereLightAbsorptionFactor = 0.1;
    float fogDensity = 15.0;
    float haze = caelumFogExp(
        caelumHazeDistance * 0.005,
        atmosphereLightAbsorptionFactor);
    float inverseHazeHeight = 100.0;
    float hazeAbsorption = caelumFogExp(
        pow(1.0 - sunY, inverseHazeHeight),
        fogDensity);

    vec4 hazeOutput = fogColour;
    if (incidenceAngleCos > 0.0)
    {
        float sunlightScatteringFactor = 0.1;
        float sunlightScatteringLossFactor = 0.3;
        vec4 sunlightInscatterColour = caelumSunlightInscatter(
            sunColour,
            clamp(
                (1.0 - texture(atmRelativeDepth, sunY).r)
                * hazeAbsorption,
                0.0,
                1.0),
            clamp(incidenceAngleCos, 0.0, 1.0),
            sunlightScatteringFactor)
            * (1.0 - sunlightScatteringLossFactor);
        hazeOutput.rgb =
            hazeOutput.rgb * (1.0 - sunlightInscatterColour.a)
            + sunlightInscatterColour.rgb
            * sunlightInscatterColour.a * haze;
    }
    hazeOutput.a = haze;
    fragColour = hazeOutput;
}

#else
#error "A Caelum sky-dome shader stage selector is required"
#endif
