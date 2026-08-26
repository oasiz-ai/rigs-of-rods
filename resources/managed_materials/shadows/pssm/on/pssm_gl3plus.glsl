#version 330 core

// Modern GL3Plus port of the managed-material PSSM caster and receiver.
// Each GPU program definition selects exactly one stage.

#if defined(PSSM_CASTER_VERTEX)

in vec4 vertex;
in vec3 uv0;

uniform mat4 wvpMat;

out vec2 pssmDepth;
out vec2 pssmUv;

void main()
{
    gl_Position = wvpMat * vertex;
    pssmDepth = gl_Position.zw;
    gl_Position.z = max(gl_Position.z, 0.0);
    pssmUv = uv0.xy;
}

#elif defined(PSSM_CASTER_FRAGMENT)

in vec2 pssmDepth;
in vec2 pssmUv;

out vec4 fragColour;

void main()
{
    float finalDepth = pssmDepth.x / pssmDepth.y;
    fragColour = vec4(finalDepth, finalDepth, finalDepth, 1.0);
}

#elif defined(PSSM_CASTER_ALPHA_FRAGMENT)

in vec2 pssmDepth;
in vec2 pssmUv;

uniform sampler2D alphaMap;

out vec4 fragColour;

void main()
{
    float finalDepth = pssmDepth.x / pssmDepth.y;
    float alpha = texture(alphaMap, pssmUv).a;
    if (alpha <= 0.5)
    {
        discard;
    }
    fragColour = vec4(finalDepth, finalDepth, finalDepth, alpha);
}

#elif defined(PSSM_RECEIVER_VERTEX)

in vec4 vertex;
in vec3 normal;
in vec3 uv0;

uniform vec4 lightPosition;
uniform vec3 eyePosition;
uniform mat4 worldViewProjMatrix;
uniform mat4 texWorldViewProjMatrix0;
uniform mat4 texWorldViewProjMatrix1;
uniform mat4 texWorldViewProjMatrix2;

out vec3 pssmUv;
out vec3 pssmLightDirection;
out vec3 pssmHalfAngle;
out vec4 pssmLightPosition0;
out vec4 pssmLightPosition1;
out vec4 pssmLightPosition2;
out vec3 pssmNormal;

void main()
{
    gl_Position = worldViewProjMatrix * vertex;
    pssmUv = vec3(uv0.xy, gl_Position.z);
    pssmLightDirection = normalize(
        lightPosition.xyz - vertex.xyz * lightPosition.w);
    vec3 eyeDirection = normalize(eyePosition - vertex.xyz);
    pssmHalfAngle = normalize(eyeDirection + pssmLightDirection);
    pssmLightPosition0 = texWorldViewProjMatrix0 * vertex;
    pssmLightPosition1 = texWorldViewProjMatrix1 * vertex;
    pssmLightPosition2 = texWorldViewProjMatrix2 * vertex;
    pssmNormal = normal;
}

#elif defined(PSSM_RECEIVER_FRAGMENT)

in vec3 pssmUv;
in vec3 pssmLightDirection;
in vec3 pssmHalfAngle;
in vec4 pssmLightPosition0;
in vec4 pssmLightPosition1;
in vec4 pssmLightPosition2;
in vec3 pssmNormal;

uniform vec4 invShadowMapSize0;
uniform vec4 invShadowMapSize1;
uniform vec4 invShadowMapSize2;
uniform vec4 pssmSplitPoints;
uniform sampler2D diffuse;
uniform sampler2D specular;
uniform sampler2D normalMap;
uniform sampler2D shadowMap0;
uniform sampler2D shadowMap1;
uniform sampler2D shadowMap2;
uniform vec4 lightDiffuse;
uniform vec4 lightSpecular;
uniform vec4 ambient;

out vec4 fragColour;

float pssmShadowPcf(
    sampler2D shadowMap,
    vec4 shadowMapPosition,
    vec2 offset)
{
    shadowMapPosition /= shadowMapPosition.w;
    vec2 uv = shadowMapPosition.xy;
    vec3 sampleOffset = vec3(offset, -offset.x) * 0.3;

    // The texture projection matrix supplies the legacy render-system bias.
    // Do not add a second receiver bias here.
    float shadowDepth = shadowMapPosition.z;
    float coverage = shadowDepth <= texture(
        shadowMap, uv - sampleOffset.xy).r ? 1.0 : 0.0;
    coverage += shadowDepth <= texture(
        shadowMap, uv + sampleOffset.xy).r ? 1.0 : 0.0;
    coverage += shadowDepth <= texture(
        shadowMap, uv + sampleOffset.zy).r ? 1.0 : 0.0;
    coverage += shadowDepth <= texture(
        shadowMap, uv - sampleOffset.zy).r ? 1.0 : 0.0;

    // Preserve the legacy attenuation: four taps intentionally divide by 5.
    return coverage / 5.0;
}

void main()
{
    float shadowing;
    if (pssmUv.z <= pssmSplitPoints.y)
    {
        shadowing = pssmShadowPcf(
            shadowMap0, pssmLightPosition0, invShadowMapSize0.xy);
    }
    else if (pssmUv.z <= pssmSplitPoints.z)
    {
        shadowing = pssmShadowPcf(
            shadowMap1, pssmLightPosition1, invShadowMapSize1.xy);
    }
    else
    {
        shadowing = pssmShadowPcf(
            shadowMap2, pssmLightPosition2, invShadowMapSize2.xy);
    }

    vec3 lightVector = normalize(pssmLightDirection);
    vec3 halfAngle = normalize(pssmHalfAngle);
    vec4 diffuseColour = texture(diffuse, pssmUv.xy);
    vec4 specularColour = texture(specular, pssmUv.xy);
    float shininess = specularColour.a;
    specularColour.a = 1.0;

    float nDotL = dot(pssmNormal, lightVector);
    float nDotH = dot(pssmNormal, halfAngle);
    float diffuseTerm = max(nDotL, 0.0);
    float specularTerm = (nDotL < 0.0 || nDotH < 0.0)
        ? 0.0
        : pow(nDotH, shininess * 52.0);
    float shadowScale = 0.3 + 0.7 * shadowing;

    fragColour = diffuseColour * clamp(
        ambient + lightDiffuse * diffuseTerm * shadowScale,
        0.0,
        1.0);
    fragColour += lightSpecular * specularColour
        * specularTerm * shadowScale;
    fragColour.a = diffuseColour.a;
}

#else
#error "A managed PSSM shader stage selector is required"
#endif
