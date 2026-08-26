#version 330 core

// Modern GL3Plus port of the historical general shader family.
// Each GPU program definition selects exactly one stage.

#if defined(GENERAL_VERTEX_AMBIENT)

in vec4 vertex;
in vec3 uv0;

uniform mat4 wvpMat;

out vec2 generalUv;

void main()
{
    gl_Position = wvpMat * vertex;
    generalUv = uv0.xy;
}

#elif defined(GENERAL_FRAGMENT_AMBIENT)

in vec2 generalUv;

uniform vec3 ambient;
uniform vec4 matDif;
uniform sampler2D diffuseMap;

out vec4 fragColour;

void main()
{
    vec4 diffuseTex = texture(diffuseMap, generalUv);
    fragColour = vec4(ambient * matDif.rgb * diffuseTex.rgb, diffuseTex.a);
}

#elif defined(GENERAL_VERTEX_RENDER)

in vec4 vertex;
in vec3 normal;
in vec4 colour;
in vec3 uv0;

uniform mat4 wMat;
uniform mat4 wvpMat;

out vec3 generalUv;
out vec4 generalWorldPosition;
out vec3 generalObjectNormal;
out vec4 generalColour;

void main()
{
    gl_Position = wvpMat * vertex;
    generalUv = uv0;
    generalWorldPosition = wMat * vertex;
    generalObjectNormal = normal;
    generalColour = colour;
}

#elif defined(GENERAL_FRAGMENT_RENDER)

in vec3 generalUv;
in vec4 generalWorldPosition;
in vec3 generalObjectNormal;
in vec4 generalColour;

uniform vec3 ambient;
uniform vec4 matDif;

out vec4 fragColour;

void main()
{
    float bridge = generalColour.r;
    float pipe = generalColour.g;
    float normalY = abs(generalObjectNormal.y);

    float power = mix(mix(8.0, 8.0, bridge), 4.0, pipe);
    float terrain = mix(mix(1.0, 0.0, bridge), 0.0, pipe);
    float diffuse = 1.0 - mix(
        1.0 - mix(pow(normalY, power), pow(normalY, power), pipe),
        pow(1.0 - 2.0 * acos(normalY) / 3.141592654, power),
        terrain);
    vec3 litColour = ambient + diffuse * matDif.rgb;

    vec3 colour = mix(
        mix(
            vec3(1.0) * litColour,
            vec3(0.0, 0.8, 1.0) * (0.4 + 0.7 * litColour),
            bridge),
        vec3(1.0, 0.8, 0.0) * (0.2 + litColour),
        pipe);
    fragColour = vec4(colour, 1.0);
}

#elif defined(GENERAL_FRAGMENT_RENDER_GRASS)

in vec3 generalUv;
in vec4 generalWorldPosition;
in vec3 generalObjectNormal;
in vec4 generalColour;

out vec4 fragColour;

void main()
{
    float bridge = generalColour.a;
    fragColour = vec4(vec3(bridge), bridge);
}

#elif defined(GENERAL_VERTEX_DIFFUSE)

in vec4 vertex;
in vec3 normal;
in vec3 tangent;
in vec4 colour;
in vec3 uv0;

uniform mat4 wMat;
uniform mat4 wvpMat;
uniform vec4 fogParams;

out vec3 generalUv;
out vec4 generalWorldPosition;
out vec3 generalObjectNormal;
out vec3 generalObjectTangent;
out vec3 generalObjectBitangent;
out vec4 generalColour;

void main()
{
    gl_Position = wvpMat * vertex;
    generalUv = uv0;
    generalWorldPosition = wMat * vertex;
    generalObjectNormal = normal;
    generalObjectTangent = tangent;
    generalObjectBitangent = cross(tangent, normal);
    generalColour = colour;
    generalWorldPosition.w = clamp(
        fogParams.x * (gl_Position.z - fogParams.y) * fogParams.w,
        0.0,
        1.0);
}

#elif defined(GENERAL_FRAGMENT_DIFFUSE) || defined(GENERAL_FRAGMENT_ENV)

in vec3 generalUv;
in vec4 generalWorldPosition;
in vec3 generalObjectNormal;
in vec3 generalObjectTangent;
in vec3 generalObjectBitangent;
in vec4 generalColour;

uniform vec3 ambient;
uniform vec3 lightDif0;
uniform vec3 lightSpec0;
uniform vec4 matDif;
uniform vec4 matSpec;
uniform float matShininess;
uniform vec3 fogColor;
uniform vec4 lightPos0;
uniform vec3 camPos;
uniform mat4 iTWMat;
uniform sampler2D diffuseMap;
uniform sampler2D normalMap;
#if defined(GENERAL_FRAGMENT_ENV)
uniform vec4 envPars;
uniform samplerCube envMap;
#endif

out vec4 fragColour;

vec3 generalMappedNormal()
{
    vec3 tangentNormal = texture(normalMap, generalUv.xy).xyz * 2.0 - 1.0;
    vec3 objectNormal = mat3(
        generalObjectTangent,
        generalObjectBitangent,
        generalObjectNormal) * tangentNormal;
    return normalize(mat3(iTWMat) * objectNormal);
}

void main()
{
    vec3 lightDirection = normalize(
        lightPos0.xyz - lightPos0.w * generalWorldPosition.xyz);
    vec3 mappedNormal = generalMappedNormal();
    float diffuse = max(dot(lightDirection, mappedNormal), 0.0);

    vec3 cameraDirection = normalize(camPos - generalWorldPosition.xyz);
    vec3 halfVector = normalize(lightDirection + cameraDirection);
    float specular = pow(
        max(dot(mappedNormal, halfVector), 0.0),
        matShininess);

    vec4 diffuseTex = texture(diffuseMap, generalUv.xy);
    vec3 diffuseColour =
        diffuse * lightDif0 * matDif.rgb * diffuseTex.rgb;
    vec3 specularColour = specular * lightSpec0 * matSpec.rgb;
    vec3 colour =
        diffuseTex.rgb * ambient + diffuseColour + specularColour;

#if defined(GENERAL_FRAGMENT_ENV)
    vec4 environmentTex = texture(
        envMap,
        reflect(-cameraDirection, mappedNormal));
    colour = envPars.x * colour + envPars.y * environmentTex.rgb;
#endif

    colour = mix(colour, fogColor, generalWorldPosition.w);
    fragColour = vec4(colour, diffuseTex.a);
}

#elif defined(GENERAL_VERTEX_SHADOW)

in vec4 vertex;
in vec3 normal;
in vec3 tangent;
in vec4 colour;
in vec3 uv0;

uniform mat4 wMat;
uniform mat4 wvpMat;
uniform vec4 fogParams;
uniform mat4 texWVPMat0;
uniform mat4 texWVPMat1;
uniform mat4 texWVPMat2;

out vec3 generalUv;
out vec4 generalWorldPosition;
out vec3 generalObjectNormal;
out vec3 generalObjectTangent;
out vec3 generalObjectBitangent;
out vec4 generalColour;
out vec4 generalLightPosition0;
out vec4 generalLightPosition1;
out vec4 generalLightPosition2;

void main()
{
    gl_Position = wvpMat * vertex;
    generalUv = vec3(uv0.xy, gl_Position.z);
    generalWorldPosition = wMat * vertex;
    generalObjectNormal = normal;
    generalObjectTangent = tangent;
    generalObjectBitangent = cross(tangent, normal);
    generalColour = colour;
    generalWorldPosition.w = clamp(
        fogParams.x * (gl_Position.z - fogParams.y) * fogParams.w,
        0.0,
        1.0);
    generalLightPosition0 = texWVPMat0 * vertex;
    generalLightPosition1 = texWVPMat1 * vertex;
    generalLightPosition2 = texWVPMat2 * vertex;
}

#elif defined(GENERAL_FRAGMENT_SHADOW) || defined(GENERAL_FRAGMENT_SHADOW_ALPHA)

in vec3 generalUv;
in vec4 generalWorldPosition;
in vec3 generalObjectNormal;
in vec3 generalObjectTangent;
in vec3 generalObjectBitangent;
in vec4 generalColour;
in vec4 generalLightPosition0;
in vec4 generalLightPosition1;
in vec4 generalLightPosition2;

uniform vec3 ambient;
uniform vec3 lightDif0;
uniform vec3 lightSpec0;
uniform vec4 matDif;
uniform vec4 matSpec;
uniform float matShininess;
uniform vec3 fogColor;
uniform vec4 lightPos0;
uniform vec3 camPos;
uniform mat4 iTWMat;
uniform vec4 invShadowMapSize0;
uniform vec4 invShadowMapSize1;
uniform vec4 invShadowMapSize2;
uniform vec4 pssmSplitPoints;
uniform sampler2D diffuseMap;
#if defined(GENERAL_FRAGMENT_SHADOW_ALPHA)
uniform sampler2D diffuseAlpha;
#endif
uniform sampler2D normalMap;
uniform sampler2D shadowMap0;
uniform sampler2D shadowMap1;
uniform sampler2D shadowMap2;

out vec4 fragColour;

vec3 generalMappedShadowNormal()
{
    vec3 tangentNormal = texture(normalMap, generalUv.xy).xyz * 2.0 - 1.0;
    vec3 objectNormal = mat3(
        generalObjectTangent,
        generalObjectBitangent,
        generalObjectNormal) * tangentNormal;
    return normalize(mat3(iTWMat) * objectNormal);
}

float generalShadowPcf(
    sampler2D shadowMap,
    vec4 shadowMapPosition,
    vec2 offset)
{
    shadowMapPosition /= shadowMapPosition.w;
    vec2 uv = shadowMapPosition.xy;
    vec3 sampleOffset = vec3(offset, -offset.x) * 0.3;
    float shadowDepth = shadowMapPosition.z;

    float coverage = shadowDepth <= texture(
        shadowMap, uv - sampleOffset.xy).r ? 1.0 : 0.0;
    coverage += shadowDepth <= texture(
        shadowMap, uv + sampleOffset.xy).r ? 1.0 : 0.0;
    coverage += shadowDepth <= texture(
        shadowMap, uv + sampleOffset.zy).r ? 1.0 : 0.0;
    coverage += shadowDepth <= texture(
        shadowMap, uv - sampleOffset.zy).r ? 1.0 : 0.0;
    return coverage * 0.25;
}

void main()
{
    vec3 lightDirection = normalize(
        lightPos0.xyz - lightPos0.w * generalWorldPosition.xyz);
    vec3 mappedNormal = generalMappedShadowNormal();
    float diffuse = max(dot(lightDirection, mappedNormal), 0.0);

    vec3 cameraDirection = normalize(camPos - generalWorldPosition.xyz);
    vec3 halfVector = normalize(lightDirection + cameraDirection);
    float specular = pow(
        max(dot(mappedNormal, halfVector), 0.0),
        matShininess);

    vec4 diffuseTex = texture(diffuseMap, generalUv.xy);
#if defined(GENERAL_FRAGMENT_SHADOW_ALPHA)
    diffuseTex.a = texture(diffuseAlpha, generalUv.xy).g;
#endif

    vec3 diffuseColour =
        diffuse * lightDif0 * matDif.rgb * diffuseTex.rgb;
    vec3 specularColour = specular * lightSpec0 * matSpec.rgb;

    float shadowing;
    if (generalUv.z <= pssmSplitPoints.y)
    {
        shadowing = generalShadowPcf(
            shadowMap0, generalLightPosition0, invShadowMapSize0.xy);
    }
    else if (generalUv.z <= pssmSplitPoints.z)
    {
        shadowing = generalShadowPcf(
            shadowMap1, generalLightPosition1, invShadowMapSize1.xy);
    }
    else
    {
        shadowing = generalShadowPcf(
            shadowMap2, generalLightPosition2, invShadowMapSize2.xy);
    }

    vec3 colour = diffuseTex.rgb * ambient;
    colour += diffuseColour * (0.25 + 0.75 * shadowing);
    colour += specularColour * shadowing;
    colour = mix(colour, fogColor, generalWorldPosition.w);
    fragColour = vec4(colour, diffuseTex.a);
}

#else
#error "A general shader stage selector is required"
#endif
