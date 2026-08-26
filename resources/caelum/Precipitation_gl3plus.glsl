#version 330 core

// Modern GL3Plus port of Caelum's three-layer precipitation compositor.

#if defined(CAELUM_PRECIPITATION_VERTEX)

in vec4 vertex;

uniform mat4 worldviewproj_matrix;

out vec2 caelumPrecipitationScreenPos;

void main()
{
    gl_Position = worldviewproj_matrix * vertex;
    vec2 signedPosition = sign(vertex.xy);
    caelumPrecipitationScreenPos =
        (vec2(signedPosition.x, -signedPosition.y) + 1.0) * 0.5;
}

#elif defined(CAELUM_PRECIPITATION_FRAGMENT)

in vec2 caelumPrecipitationScreenPos;

uniform sampler2D scene;
uniform sampler2D samplerPrec;
uniform float intensity;
uniform vec4 ambient_light_colour;
uniform vec4 corner1;
uniform vec4 corner2;
uniform vec4 corner3;
uniform vec4 corner4;
uniform vec4 deltaX;
uniform vec4 deltaY;
uniform vec4 precColor;

out vec4 fragColour;

vec2 caelumCylindricalCoordinates(vec4 direction)
{
    float radius = 0.5;
    direction *= radius / pow(length(direction.xz), 0.33);
    return vec2(-atan(direction.z, direction.x), -direction.y);
}

float caelumPrecipitationAlpha(
    vec2 cylindricalCoordinates,
    float layerIntensity,
    vec2 delta)
{
    vec4 precipitation = texture(
        samplerPrec,
        cylindricalCoordinates - delta);
    return precipitation.g < layerIntensity ? precipitation.r : 1.0;
}

void main()
{
    vec2 screenPosition = caelumPrecipitationScreenPos;
    vec4 eyeDirection = mix(
        mix(corner1, corner3, screenPosition.y),
        mix(corner2, corner4, screenPosition.y),
        screenPosition.x);
    vec4 sceneColour = texture(scene, screenPosition);
    vec2 cylindricalCoordinates =
        caelumCylindricalCoordinates(eyeDirection);
    float firstLayer = caelumPrecipitationAlpha(
        cylindricalCoordinates,
        intensity / 4.0,
        vec2(deltaX.x, deltaY.x));
    float secondLayer = caelumPrecipitationAlpha(
        cylindricalCoordinates,
        intensity / 4.0,
        vec2(deltaX.y, deltaY.y));
    float thirdLayer = caelumPrecipitationAlpha(
        cylindricalCoordinates,
        intensity / 4.0,
        vec2(deltaX.z, deltaY.z));
    float precipitation = min(min(firstLayer, secondLayer), thirdLayer);
    fragColour = mix(precColor, sceneColour, precipitation);
}

#else
#error "A Caelum precipitation shader stage selector is required"
#endif
