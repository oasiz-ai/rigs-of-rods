#version 330 core

/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

uniform sampler2D uScene;
uniform vec4 uInvTextureSize;

in vec2 vUv;
out vec4 fragColor;

const float ROR_V0A_EXPOSURE = 1.08;
const float ROR_V0A_CONTRAST = 1.04;
const float ROR_V0A_SATURATION = 1.03;
const float ROR_V0A_SHOULDER = 0.12;
const float ROR_V0A_FXAA_EDGE_THRESHOLD = 1.0 / 8.0;
const float ROR_V0A_FXAA_EDGE_THRESHOLD_MIN = 1.0 / 24.0;
const float ROR_V0A_FXAA_BLEND_LIMIT = 0.75;
const float ROR_V0A_LUMA_RED = 0.2126;
const float ROR_V0A_LUMA_GREEN = 0.7152;
const float ROR_V0A_LUMA_BLUE = 0.0722;

float rorV0ALuma(vec3 color)
{
    return color.r * ROR_V0A_LUMA_RED
        + color.g * ROR_V0A_LUMA_GREEN
        + color.b * ROR_V0A_LUMA_BLUE;
}

vec3 rorV0AColorCurve(vec3 inputColor)
{
    vec3 exposed = inputColor * ROR_V0A_EXPOSURE;
    float luminance = rorV0ALuma(exposed);
    vec3 saturated = luminance
        + (exposed - vec3(luminance)) * ROR_V0A_SATURATION;
    vec3 contrasted = (saturated - vec3(0.5)) * ROR_V0A_CONTRAST
        + vec3(0.5);

    vec3 positive = max(contrasted, vec3(0.0));
    vec3 resolved = (1.0 + ROR_V0A_SHOULDER) * positive
        / (vec3(1.0) + ROR_V0A_SHOULDER * positive);
    return clamp(resolved, vec3(0.0), vec3(1.0));
}

vec3 rorV0AResolveFxaa(
    vec3 center,
    vec3 north,
    vec3 south,
    vec3 east,
    vec3 west)
{
    float centerLuma = rorV0ALuma(center);
    float northLuma = rorV0ALuma(north);
    float southLuma = rorV0ALuma(south);
    float eastLuma = rorV0ALuma(east);
    float westLuma = rorV0ALuma(west);
    float minimumLuma = min(
        centerLuma,
        min(min(northLuma, southLuma), min(eastLuma, westLuma)));
    float maximumLuma = max(
        centerLuma,
        max(max(northLuma, southLuma), max(eastLuma, westLuma)));
    float lumaRange = maximumLuma - minimumLuma;
    float edgeThreshold = max(
        ROR_V0A_FXAA_EDGE_THRESHOLD_MIN,
        maximumLuma * ROR_V0A_FXAA_EDGE_THRESHOLD);
    if (lumaRange <= edgeThreshold)
    {
        return center;
    }

    float northSouthGradient = abs(
        northLuma + southLuma - 2.0 * centerLuma);
    float eastWestGradient = abs(
        eastLuma + westLuma - 2.0 * centerLuma);
    vec3 neighborAverage;
    if (northSouthGradient >= eastWestGradient)
    {
        neighborAverage = (north + south) * 0.5;
    }
    else
    {
        neighborAverage = (east + west) * 0.5;
    }

    float blend = clamp(
        (lumaRange - edgeThreshold) / lumaRange,
        0.0,
        ROR_V0A_FXAA_BLEND_LIMIT);
    vec3 resolved = center + (neighborAverage - center) * blend;
    return clamp(resolved, vec3(0.0), vec3(1.0));
}

void main()
{
    vec2 texel = uInvTextureSize.xy;
    vec4 centerSample = texture(uScene, vUv);
    vec3 center = rorV0AColorCurve(centerSample.rgb);
    vec3 north = rorV0AColorCurve(
        texture(uScene, vUv + vec2(0.0, -texel.y)).rgb);
    vec3 south = rorV0AColorCurve(
        texture(uScene, vUv + vec2(0.0, texel.y)).rgb);
    vec3 east = rorV0AColorCurve(
        texture(uScene, vUv + vec2(texel.x, 0.0)).rgb);
    vec3 west = rorV0AColorCurve(
        texture(uScene, vUv + vec2(-texel.x, 0.0)).rgb);

    fragColor = vec4(
        rorV0AResolveFxaa(center, north, south, east, west),
        centerSample.a);
}
