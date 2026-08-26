#version 330 core

// Modern GL3Plus port of the historical NiceMetal Cg shader family.
// Each program definition selects exactly one stage and material variant.

#if defined(NICEMETAL_VERTEX_LIT)

in vec4 vertex;
in vec3 normal;
in vec4 colour;
in vec2 uv0;

uniform vec4 lightPosition;
uniform vec3 eyePosition;
uniform mat4 worldviewproj;

out vec4 nicemetalColour;
out vec4 nicemetalObjectPosition;
out vec3 nicemetalObjectNormal;
out vec4 nicemetalLightPosition;
out vec3 nicemetalEyePosition;
out vec2 nicemetalUv0;

void main()
{
    gl_Position = worldviewproj * vertex;
    nicemetalColour = colour;
    nicemetalObjectPosition = vertex;
    nicemetalObjectNormal = normal;
    nicemetalLightPosition = lightPosition;
    nicemetalEyePosition = eyePosition;
    nicemetalUv0 = uv0;
}

#elif defined(NICEMETAL_VERTEX_REFLECTION)

in vec4 vertex;
in vec3 normal;
in vec4 colour;
in vec2 uv0;

uniform vec3 camPosition;
uniform mat4 world;
uniform mat4 worldViewProj;

out vec2 nicemetalUv0;
out vec3 nicemetalViewDirection;
out vec3 nicemetalWorldNormal;
out vec4 nicemetalColour;

void main()
{
    gl_Position = worldViewProj * vertex;
    nicemetalUv0 = uv0;
    nicemetalColour = colour;
    nicemetalWorldNormal = mat3(world) * normal;
    nicemetalViewDirection = mat3(world) * (vertex.xyz - camPosition);
}

#elif defined(NICEMETAL_FRAGMENT_LIT)

in vec4 nicemetalColour;
in vec4 nicemetalObjectPosition;
in vec3 nicemetalObjectNormal;
in vec4 nicemetalLightPosition;
in vec3 nicemetalEyePosition;
in vec2 nicemetalUv0;

uniform vec4 lightDiffuse;
uniform vec4 lightSpecular;
uniform float exponent;
uniform vec4 ambient;
uniform sampler2D diffusetex;
uniform sampler2D speculartex;
#if !defined(NICEMETAL_NO_DAMAGE) && !defined(NICEMETAL_SIMPLE)
uniform sampler2D diffusedmgtex;
#endif

out vec4 fragColour;

vec4 nicemetalLit(float normalDotLight, float normalDotHalf, float power)
{
    float diffuse = max(normalDotLight, 0.0);
    float specular = normalDotLight > 0.0
        ? pow(max(normalDotHalf, 0.0), power)
        : 0.0;
    return vec4(1.0, diffuse, specular, 1.0);
}

void main()
{
    vec3 surfaceNormal = normalize(nicemetalObjectNormal);
    vec3 eyeDirection = normalize(
        nicemetalEyePosition - nicemetalObjectPosition.xyz);
    vec3 lightDirection = normalize(
        nicemetalLightPosition.xyz
        - (nicemetalObjectPosition * nicemetalLightPosition.w).xyz);
    vec3 halfAngle = normalize(lightDirection + eyeDirection);
    vec4 lighting = nicemetalLit(
        dot(lightDirection, surfaceNormal),
        dot(halfAngle, surfaceNormal),
        exponent);

    vec4 diffuseSample = texture(diffusetex, nicemetalUv0);
    vec4 textColour;
    vec4 specColour;
#if defined(NICEMETAL_SIMPLE)
    textColour = diffuseSample;
    specColour = texture(speculartex, nicemetalUv0);
#elif defined(NICEMETAL_NO_DAMAGE)
    textColour = diffuseSample * (1.0 - nicemetalColour.b / 3.0);
    specColour = texture(speculartex, nicemetalUv0)
        + nicemetalColour.b / 3.0 - nicemetalColour.a / 2.0;
#else
    textColour = diffuseSample * (1.0 - nicemetalColour.a);
    textColour += texture(diffusedmgtex, nicemetalUv0)
        * nicemetalColour.a;
    textColour *= 1.0 - nicemetalColour.b / 3.0;
    specColour = texture(speculartex, nicemetalUv0)
        + nicemetalColour.b / 3.0 - nicemetalColour.a / 2.0;
#endif

    fragColour = mix(
        lightDiffuse * textColour * lighting.y + textColour * ambient,
        lightSpecular * lighting.z,
        specColour);
#if defined(NICEMETAL_TRANSPARENT)
    fragColour.a = diffuseSample.a;
#else
    fragColour.a = 1.0;
#endif
}

#elif defined(NICEMETAL_FRAGMENT_REFLECTION)

in vec2 nicemetalUv0;
in vec3 nicemetalViewDirection;
in vec3 nicemetalWorldNormal;
#if !defined(NICEMETAL_NO_VERTEX_COLOUR)
in vec4 nicemetalColour;
#endif

uniform sampler2D speculartex;
uniform samplerCube cubeMap;

out vec4 fragColour;

void main()
{
    vec3 surfaceNormal = normalize(nicemetalWorldNormal);
    vec3 reflectedDirection = reflect(
        nicemetalViewDirection, surfaceNormal);
    reflectedDirection.z = -reflectedDirection.z;

    vec4 reflectedColour = texture(cubeMap, reflectedDirection);
    vec4 emissiveColour = texture(speculartex, nicemetalUv0);
#if !defined(NICEMETAL_NO_VERTEX_COLOUR)
    emissiveColour += nicemetalColour.b / 3.0
        - nicemetalColour.a / 2.0;
#endif
    fragColour = reflectedColour * emissiveColour;
    fragColour.a = 1.0;
}

#else
#error "A NiceMetal shader stage selector is required"
#endif
