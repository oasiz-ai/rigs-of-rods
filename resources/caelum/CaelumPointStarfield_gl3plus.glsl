#version 330 core

// Modern GL3Plus port of Caelum's magnitude-scaled point starfield.

#if defined(CAELUM_POINT_STAR_VERTEX)

in vec4 vertex;
in vec3 uv0;

uniform mat4 worldviewproj_matrix;
uniform float mag_scale;
uniform float mag0_size;
uniform float min_size;
uniform float max_size;
uniform float render_target_flipping;
uniform float aspect_ratio;

out vec2 caelumStarUv;
out vec4 caelumStarColour;

void main()
{
    gl_Position = worldviewproj_matrix * vertex;
    caelumStarUv = uv0.xy;

    float size = exp(mag_scale * uv0.z) * mag0_size;
    float fade = clamp(size / min_size, 0.0, 1.0);
    caelumStarColour = vec4(1.0, 1.0, 1.0, fade * fade);
    size = clamp(size, min_size, max_size);
    gl_Position.xy += gl_Position.w * uv0.xy
        * vec2(size, size * aspect_ratio * render_target_flipping);
}

#elif defined(CAELUM_POINT_STAR_FRAGMENT)

in vec2 caelumStarUv;
in vec4 caelumStarColour;

out vec4 fragColour;

void main()
{
    float squaredLength = dot(caelumStarUv, caelumStarUv);
    fragColour = caelumStarColour;
    fragColour.a *= 1.5 * exp(-(squaredLength * 8.0));
}

#else
#error "A Caelum point-star shader stage selector is required"
#endif
