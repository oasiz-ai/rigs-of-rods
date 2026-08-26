#version 330 core

// Modern GL3Plus full-screen vertex path for Caelum compositors.

#if defined(CAELUM_MINIMAL_COMPOSITOR_VERTEX)

in vec4 vertex;

uniform mat4 worldviewproj_matrix;

out vec2 caelumScreenPos;

void main()
{
    gl_Position = worldviewproj_matrix * vertex;
    vec2 signedPosition = sign(vertex.xy);
    caelumScreenPos = (vec2(signedPosition.x, -signedPosition.y) + 1.0) * 0.5;
}

#else
#error "The Caelum minimal-compositor vertex selector is required"
#endif
