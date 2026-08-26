#version 330 core

// Modern GL3Plus port of Caelum's texture-preserving lunar phase mask.

#if defined(CAELUM_PHASE_MOON_FRAGMENT)

in vec2 uv0;

uniform float phase;
uniform sampler2D moonDisc;

out vec4 fragColour;

float caelumMoonPhaseFactor(vec2 uv, float phaseValue)
{
    float alpha = 1.0;
    float signedReferenceX = uv.x - 0.5;
    float referenceY = abs(uv.y - 0.5);
    float referenceXForY = sqrt(0.25 - referenceY * referenceY);
    float minimumX = -referenceXForY;
    float maximumX = referenceXForY;
    float firstBoundary =
        (maximumX - minimumX) * (phaseValue / 2.0) + minimumX;
    float secondBoundary =
        (maximumX - minimumX) * phaseValue + minimumX;
    if (signedReferenceX < firstBoundary)
    {
        alpha = 0.0;
    }
    else if (
        signedReferenceX < secondBoundary
        && firstBoundary != secondBoundary)
    {
        alpha = (signedReferenceX - firstBoundary)
            / (secondBoundary - firstBoundary);
    }
    return alpha;
}

void main()
{
    vec4 colour = texture(moonDisc, uv0);
    float alpha = caelumMoonPhaseFactor(uv0, phase);
    float luminance = dot(colour.rgb, vec3(0.3333));
    colour.a = min(colour.a, luminance * alpha);
    colour.rgb /= luminance;
    fragColour = colour;
}

#else
#error "The Caelum phase-moon fragment selector is required"
#endif
