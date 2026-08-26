#version 330 core

// Modern GL3Plus port of the animated, PSSM-shadowed grass shader.

#if defined(GRASS_VERTEX)

in vec4 vertex;
in vec4 colour;
in vec3 uv0;

uniform float time;
uniform float frequency;
uniform vec4 direction;
uniform mat4 worldViewProj;
uniform vec3 camPos;
uniform float fadeRange;
uniform mat4 texWorldViewProjMatrix0;
uniform mat4 texWorldViewProjMatrix1;
uniform mat4 texWorldViewProjMatrix2;

out vec4 grassColour;
out vec3 grassUv;
out vec4 grassLightPosition0;
out vec4 grassLightPosition1;
out vec4 grassLightPosition2;

void main()
{
    vec4 position = vertex;
    float distanceToCamera = distance(camPos.xz, position.xz);
    grassColour.rgb = colour.rgb;
    grassColour.a = 2.0 - (2.0 * distanceToCamera / fadeRange);
    float originalX = position.x;
    if (uv0.y == 0.0)
    {
        float offset = sin(time + originalX * frequency);
        position += direction * offset;
    }
    gl_Position = worldViewProj * position;
    grassUv = uv0;
    grassUv.z = gl_Position.z;
    grassLightPosition0 = texWorldViewProjMatrix0 * position;
    grassLightPosition1 = texWorldViewProjMatrix1 * position;
    grassLightPosition2 = texWorldViewProjMatrix2 * position;
}

#elif defined(GRASS_FRAGMENT)

in vec4 grassColour;
in vec3 grassUv;
in vec4 grassLightPosition0;
in vec4 grassLightPosition1;
in vec4 grassLightPosition2;

uniform sampler2D diffuseMap;
uniform sampler2D shadowMap0;
uniform sampler2D shadowMap1;
uniform sampler2D shadowMap2;
uniform vec4 invShadowMapSize0;
uniform vec4 invShadowMapSize1;
uniform vec4 invShadowMapSize2;
uniform vec4 pssmSplitPoints;

out vec4 fragColour;

float shadowPcf(sampler2D shadowMap, vec4 shadowMapPosition, vec2 offset)
{
    shadowMapPosition /= shadowMapPosition.w;
    vec2 uv = shadowMapPosition.xy;
    vec3 sampleOffset = vec3(offset, -offset.x) * 0.3;
    float depth = shadowMapPosition.z;
    float shadow = depth <= texture(shadowMap, uv - sampleOffset.xy).r
        ? 1.0 : 0.0;
    shadow += depth <= texture(shadowMap, uv + sampleOffset.xy).r
        ? 1.0 : 0.0;
    shadow += depth <= texture(shadowMap, uv + sampleOffset.zy).r
        ? 1.0 : 0.0;
    shadow += depth <= texture(shadowMap, uv - sampleOffset.zy).r
        ? 1.0 : 0.0;
    return shadow * 0.25;
}

void main()
{
    vec4 diffuseSample = texture(diffuseMap, grassUv.xy);
    float shadowing;
    if (grassUv.z <= pssmSplitPoints.y)
    {
        shadowing = shadowPcf(
            shadowMap0, grassLightPosition0, invShadowMapSize0.xy);
    }
    else if (grassUv.z <= pssmSplitPoints.z)
    {
        shadowing = shadowPcf(
            shadowMap1, grassLightPosition1, invShadowMapSize1.xy);
    }
    else
    {
        shadowing = shadowPcf(
            shadowMap2, grassLightPosition2, invShadowMapSize2.xy);
    }
    vec3 litColour = diffuseSample.rgb * (0.65 + 0.35 * shadowing);
    fragColour = vec4(litColour, diffuseSample.a) * grassColour;
}

#else
#error "A grass shader stage selector is required"
#endif
