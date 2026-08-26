#version 330 core

// Modern GL3Plus port of the historical reflection/refraction water shaders.
// Each program definition selects exactly one stage and material variant.

#if defined(FRESNEL_WATER_VERTEX)

in vec4 vertex;
in vec3 normal;
in vec2 uv0;

uniform mat4 worldViewProjMatrix;
uniform vec3 eyePosition;
uniform float fresnelBias;
uniform float fresnelScale;
uniform float fresnelPower;
uniform float timeVal;
uniform float scale;
uniform float scroll;
uniform float noise;

out float fresnelFactor;
out vec3 noiseCoord;
out vec4 projectionCoord;

void main()
{
    gl_Position = worldViewProjMatrix * vertex;

    // Preserve the original projective scale/bias transform, including its
    // render-texture Y flip, without relying on matrix constructor order.
    projectionCoord = vec4(
        0.5 * gl_Position.x + 0.5 * gl_Position.w,
        -0.5 * gl_Position.y + 0.5 * gl_Position.w,
        0.5 * gl_Position.z + 0.5 * gl_Position.w,
        gl_Position.w);

    noiseCoord.xy = (uv0 + timeVal * scroll) * scale;
    noiseCoord.z = noise * timeVal;

    vec3 eyeDirection = normalize(vertex.xyz - eyePosition);
    fresnelFactor = fresnelBias + fresnelScale * pow(
        max(1.0 + dot(eyeDirection, normal), 0.0), fresnelPower);
}

#elif defined(FRESNEL_WATER_FRAGMENT_FULL)

in float fresnelFactor;
in vec3 noiseCoord;
in vec4 projectionCoord;

uniform float distortionRange;
uniform vec4 tintColour;
uniform sampler3D noiseMap;
uniform sampler2D reflectMap;
uniform sampler2D refractMap;

out vec4 fragColour;

void main()
{
    const vec3 yOffset = vec3(0.31, 0.58, 0.23);
    vec2 distortion;
    distortion.x = texture(noiseMap, noiseCoord).x;
    distortion.y = texture(noiseMap, noiseCoord + yOffset).x;
    distortion = (distortion * 2.0 - 1.0) * distortionRange;

    vec2 projectedUv = projectionCoord.xy / projectionCoord.w;
    projectedUv += distortion;

    vec4 reflectionColour = texture(reflectMap, projectedUv);
    vec4 refractionColour = texture(refractMap, projectedUv) + tintColour;
    fragColour = mix(refractionColour, reflectionColour, fresnelFactor);
}

#elif defined(FRESNEL_WATER_FRAGMENT_REFLECTION)

in float fresnelFactor;
in vec3 noiseCoord;
in vec4 projectionCoord;

uniform float distortionRange;
uniform vec4 tintColour;
uniform sampler3D noiseMap;
uniform sampler2D reflectMap;

out vec4 fragColour;

void main()
{
    const vec3 yOffset = vec3(0.31, 0.58, 0.23);
    vec2 distortion;
    distortion.x = texture(noiseMap, noiseCoord).x;
    distortion.y = texture(noiseMap, noiseCoord + yOffset).x;
    distortion = (distortion * 2.0 - 1.0) * distortionRange;

    vec2 projectedUv = projectionCoord.xy / projectionCoord.w;
    projectedUv += distortion;

    vec4 reflectionColour = texture(reflectMap, projectedUv);
    float reflectionFactor = 0.33 + fresnelFactor;
    fragColour = mix(tintColour, reflectionColour, reflectionFactor);
    fragColour.a = reflectionFactor;
}

#else
#error "A Fresnel water shader stage selector is required"
#endif
