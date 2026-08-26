#version 330 core

// Modern GL3Plus port of Caelum's exponentially height-decaying fog.

#if defined(CAELUM_GROUND_FOG_VERTEX)

in vec4 vertex;

uniform mat4 worldViewProj;
uniform mat4 world;

out vec3 caelumWorldPosition;

void main()
{
    gl_Position = worldViewProj * vertex;
    caelumWorldPosition = (world * vertex).xyz;
}

#elif defined(CAELUM_GROUND_FOG_FRAGMENT)

in vec3 caelumWorldPosition;

uniform vec3 camPos;
uniform vec4 fogColour;
uniform float fogDensity;
uniform float fogVerticalDecay;
uniform float fogGroundLevel;

out vec4 fragColour;

float caelumExpDiv(float value)
{
    if (abs(value) < 0.0001)
    {
        return 1.0;
    }
    return (exp(value) - 1.0) / value;
}

float caelumExpGroundFog(
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
        * caelumExpDiv(-verticalDecay * deltaHeight));
}

void main()
{
    float fog = caelumExpGroundFog(
        length(camPos - caelumWorldPosition),
        camPos.y,
        caelumWorldPosition.y,
        fogDensity,
        fogVerticalDecay,
        fogGroundLevel);
    fragColour = vec4(fogColour.rgb, fog);
}

#elif defined(CAELUM_GROUND_FOG_DOME_VERTEX)

in vec4 vertex;

uniform mat4 worldViewProj;

out vec3 caelumRelativePosition;

void main()
{
    gl_Position = worldViewProj * vertex;
    caelumRelativePosition = normalize(vertex.xyz);
}

#elif defined(CAELUM_GROUND_FOG_DOME_FRAGMENT)

in vec3 caelumRelativePosition;

uniform float cameraHeight;
uniform vec4 fogColour;
uniform float fogDensity;
uniform float fogVerticalDecay;
uniform float fogGroundLevel;

out vec4 fragColour;

float caelumExpGroundFogInfinite(
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

void main()
{
    float inverseViewSine = 1.0 / caelumRelativePosition.y;
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
        fog = clamp(
            caelumExpGroundFogInfinite(
                inverseViewSine,
                cameraHeight,
                fogDensity,
                fogVerticalDecay,
                fogGroundLevel),
            0.0,
            1.0);
    }
    fragColour = vec4(fogColour.rgb, fog);
}

#else
#error "A Caelum ground-fog shader stage selector is required"
#endif
