#version 330 core

// Modern GL3Plus port of Caelum's layered, animated cloud shader.

#if defined(CAELUM_LAYERED_CLOUDS_VERTEX)

in vec4 vertex;
in vec2 uv0;

uniform mat4 worldViewProj;
uniform mat4 worldMatrix;
uniform vec3 sunDirection;

out vec2 caelumCloudUv;
out vec3 caelumCloudRelativePosition;
out float caelumCloudSunGlow;
out vec4 caelumCloudWorldPosition;

void main()
{
    gl_Position = worldViewProj * vertex;
    caelumCloudWorldPosition = worldMatrix * vertex;
    caelumCloudUv = uv0;
    caelumCloudRelativePosition = normalize(vertex.xyz);
    caelumCloudSunGlow = dot(
        caelumCloudRelativePosition,
        normalize(-sunDirection));
}

#elif defined(CAELUM_LAYERED_CLOUDS_FRAGMENT)

in vec2 caelumCloudUv;
in vec3 caelumCloudRelativePosition;
in float caelumCloudSunGlow;
in vec4 caelumCloudWorldPosition;

uniform sampler2D cloud_shape1;
uniform sampler2D cloud_shape2;
uniform sampler2D cloud_detail;

uniform float cloudMassInvScale;
uniform float cloudDetailInvScale;
uniform vec2 cloudMassOffset;
uniform vec2 cloudDetailOffset;
uniform float cloudMassBlend;
uniform float cloudDetailBlend;
uniform float cloudCoverageThreshold;
uniform vec4 sunLightColour;
uniform vec4 sunSphereColour;
uniform vec4 fogColour;
uniform vec4 sunDirection;
uniform float cloudSharpness;
uniform float cloudThickness;
uniform vec3 camera_position;
uniform vec3 fadeDistMeasurementVector;
uniform float layerHeight;
uniform float cloudUVFactor;
uniform float heightRedFactor;
uniform float nearFadeDist;
uniform float farFadeDist;

out vec4 fragColour;

float caelumCloudIntensity(vec2 position)
{
    vec2 finalMassOffset = cloudMassOffset + position;
    float mass = mix(
        texture(
            cloud_shape1,
            finalMassOffset * cloudMassInvScale).r,
        texture(
            cloud_shape2,
            finalMassOffset * cloudMassInvScale).r,
        cloudMassBlend);
    float detail = texture(
        cloud_detail,
        (cloudDetailOffset + position) * cloudDetailInvScale).r;
    mass = (mass + detail * cloudDetailBlend) / (1.0 + cloudDetailBlend);
    return max(0.0, mass - cloudCoverageThreshold);
}

vec3 caelumYuvFromRgb(vec3 colour)
{
    return vec3(
        dot(colour, vec3(0.299, 0.587, 0.114)),
        dot(colour, vec3(-0.14713, -0.28886, 0.436)),
        dot(colour, vec3(0.615, -0.51499, -0.10001)));
}

vec3 caelumRgbFromYuv(vec3 colour)
{
    return vec3(
        dot(colour, vec3(1.0, 0.0, 1.13983)),
        dot(colour, vec3(1.0, -0.39465, -0.58060)),
        dot(colour, vec3(1.0, 2.03211, 0.0)));
}

vec3 caelumMagicColourMix(vec3 intensityColour, vec3 chromaColour)
{
    vec3 intensityYuv = caelumYuvFromRgb(intensityColour);
    vec3 chromaYuv = caelumYuvFromRgb(chromaColour);
    return clamp(
        caelumRgbFromYuv(vec3(intensityYuv.x, chromaYuv.yz)),
        0.0,
        1.0);
}

vec4 caelumOldCloudColour(vec2 uv, float sunGlow)
{
    float intensity = caelumCloudIntensity(uv);
    float alpha = clamp(exp(cloudSharpness * intensity) - 1.0, 0.0, 1.0);
    float shine = pow(clamp(sunGlow, 0.0, 1.0), 8.0) / 4.0;
    vec3 sunColour = sunLightColour.rgb * 1.5;
    vec3 cloudColour = fogColour.rgb * (1.0 - intensity / 3.0);
    float thickness = clamp(
        0.8 - exp(-cloudThickness * (intensity + 0.2 - shine)),
        0.0,
        1.0);
    return vec4(mix(sunColour, cloudColour, thickness), alpha);
}

void main()
{
    vec2 uv = caelumCloudUv * cloudUVFactor;
    vec4 colour = caelumOldCloudColour(uv, caelumCloudSunGlow);
    colour.r += layerHeight / heightRedFactor;

    float distanceFromCamera = length(
        (caelumCloudWorldPosition.xyz - camera_position)
        * fadeDistMeasurementVector);
    float alphaModifier = 1.0;
    if (distanceFromCamera > nearFadeDist)
    {
        alphaModifier = clamp(
            (farFadeDist - distanceFromCamera)
            / (farFadeDist - nearFadeDist),
            0.0,
            1.0);
    }
    float alpha = colour.a * alphaModifier;

    vec3 cloudDirection = normalize(
        vec3(
            caelumCloudWorldPosition.x,
            layerHeight,
            caelumCloudWorldPosition.y)
        - camera_position);
    float angleDifference = clamp(
        dot(cloudDirection, normalize(sunDirection.xyz)),
        0.0,
        1.0);
    vec3 illuminatedColour = mix(
        colour.rgb,
        caelumMagicColourMix(colour.rgb, sunSphereColour.rgb),
        angleDifference);
    colour.rgb = mix(illuminatedColour, colour.rgb, alpha);
    colour.a = alpha;
    fragColour = colour;
}

#else
#error "A Caelum layered-cloud shader stage selector is required"
#endif
