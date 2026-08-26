#version 330 core

// Modern GL3Plus port of Caelum's depth rendering and atmospheric composer.

#if defined(CAELUM_DEPTH_COMPOSER_FRAGMENT)

in vec2 caelumScreenPos;

uniform mat4 invViewProjMatrix;
uniform vec4 worldCameraPos;
uniform float groundFogDensity;
uniform float groundFogVerticalDecay;
uniform float groundFogBaseLevel;
uniform vec4 groundFogColour;
uniform vec3 hazeColour;
uniform vec3 sunDirection;
uniform sampler2D screenTexture;
uniform sampler2D depthTexture;
uniform sampler1D atmRelativeDepth;

out vec4 fragColour;

#if defined(CAELUM_DEPTH_EXP_GROUND_FOG)
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
#endif

#if defined(CAELUM_DEPTH_SKY_DOME_HAZE)
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

vec4 caelumCalculateHaze(
    vec3 worldPosition,
    vec3 worldCameraPosition,
    vec3 baseHazeColour,
    vec3 directionToSun)
{
    float haze = length(worldCameraPosition - worldPosition);
    float incidenceAngleCos = dot(
        -directionToSun,
        normalize(worldPosition - worldCameraPosition));
    float sunY = -directionToSun.y;
    vec4 sunColour = vec4(3.0, 2.5, 1.0, 1.0);
    float atmosphereLightAbsorptionFactor = 0.1;
    float fogDensity = 15.0;
    haze = caelumFogExp(
        haze * 0.005,
        atmosphereLightAbsorptionFactor);
    float inverseHazeHeight = 100.0;
    float hazeAbsorption = caelumFogExp(
        pow(1.0 - sunY, inverseHazeHeight),
        fogDensity);

    if (incidenceAngleCos > 0.0)
    {
        float sunlightScatteringFactor = 0.1;
        float sunlightScatteringLossFactor = 0.3;
        vec4 inscatter = caelumSunlightInscatter(
            sunColour,
            clamp(
                (1.0 - texture(atmRelativeDepth, sunY).r)
                * hazeAbsorption,
                0.0,
                1.0),
            clamp(incidenceAngleCos, 0.0, 1.0),
            sunlightScatteringFactor)
            * (1.0 - sunlightScatteringLossFactor);
        baseHazeColour =
            baseHazeColour * (1.0 - inscatter.a)
            + inscatter.rgb * inscatter.a * haze;
    }
    return vec4(baseHazeColour, haze);
}
#endif

void main()
{
    vec4 inputColour = texture(screenTexture, caelumScreenPos);
    float inputDepth = texture(depthTexture, caelumScreenPos).r;
    vec4 devicePosition = vec4(
        caelumScreenPos.x * 2.0 - 1.0,
        1.0 - caelumScreenPos.y * 2.0,
        inputDepth,
        1.0);
    vec4 worldPosition = invViewProjMatrix * devicePosition;
    worldPosition /= worldPosition.w;

    vec4 colour = inputColour;

#if defined(CAELUM_DEPTH_DEBUG_RENDER)
    colour = worldPosition * vec4(0.001, 0.01, 0.001, 1.0);
#endif

#if defined(CAELUM_DEPTH_EXP_GROUND_FOG)
    float fogFactor = caelumExpGroundFog(
        length(worldCameraPos - worldPosition),
        worldCameraPos.y,
        worldPosition.y,
        groundFogDensity,
        groundFogVerticalDecay,
        groundFogBaseLevel);
    colour = mix(colour, groundFogColour, fogFactor);
#endif

#if defined(CAELUM_DEPTH_SKY_DOME_HAZE)
    vec4 hazeValue = caelumCalculateHaze(
        worldPosition.xyz,
        worldCameraPos.xyz,
        hazeColour,
        sunDirection);
    colour.rgb = mix(colour.rgb, hazeValue.rgb, hazeValue.a);
#endif

    fragColour = colour;
}

#elif defined(CAELUM_DEPTH_RENDER_VERTEX)

in vec4 vertex;

uniform mat4 wvpMatrix;

out vec4 caelumDepthMagic;

void main()
{
    gl_Position = wvpMatrix * vertex;
    caelumDepthMagic = gl_Position;
}

#elif defined(CAELUM_DEPTH_RENDER_FRAGMENT)

in vec4 caelumDepthMagic;

out vec4 fragColour;

void main()
{
    fragColour = vec4(caelumDepthMagic.z / caelumDepthMagic.w);
}

#elif defined(CAELUM_DEPTH_ALPHA_VERTEX)

in vec4 vertex;
in vec4 uv0;

uniform mat4 wvpMatrix;

out vec4 caelumDepthUv;
out vec4 caelumDepthMagic;

void main()
{
    gl_Position = wvpMatrix * vertex;
    caelumDepthMagic = gl_Position;
    caelumDepthUv = uv0;
}

#elif defined(CAELUM_DEPTH_ALPHA_FRAGMENT)

in vec4 caelumDepthUv;
in vec4 caelumDepthMagic;

uniform sampler2D mainTex;

out vec4 fragColour;

void main()
{
    float alpha = texture(mainTex, caelumDepthUv.xy).a;
    float depth = caelumDepthMagic.z / caelumDepthMagic.w;
    fragColour = vec4(depth, depth, depth, alpha);
}

#else
#error "A Caelum depth shader stage selector is required"
#endif
