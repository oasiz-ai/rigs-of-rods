#version 330 core

// GL3Plus replacement for the historical StdQuad Cg vertex-program family.
// The program script selects exactly one public coordinate-layout variant.

in vec4 vertex;

uniform mat4 worldViewProj;

#if defined(STDQUAD_BASE)
out vec2 uv;
#elif defined(STDQUAD_TEX2)
out vec2 uv;
out vec2 uv1;
#elif defined(STDQUAD_TEX2A)
out vec2 uv;
out vec2 uv1;
#elif defined(STDQUAD_TEX3)
out vec2 uv;
out vec2 uv1;
out vec2 uv2;
#elif defined(STDQUAD_TEX4)
out vec2 uv;
out vec2 uv1;
out vec2 uv2;
out vec2 uv3;
#else
#error "A StdQuad coordinate-layout selector is required"
#endif

void main()
{
    // worldViewProj retains OGRE's render-system-specific clip transform.
    gl_Position = worldViewProj * vertex;

    // The incoming corners can contain texel offsets. The historical shader
    // intentionally discarded those offsets before deriving image-space UVs.
    vec2 signedPosition = sign(vertex.xy);
    vec2 imageUv = (vec2(signedPosition.x, -signedPosition.y) + 1.0) * 0.5;
    uv = imageUv;

#if defined(STDQUAD_TEX2)
    uv1 = imageUv;
#elif defined(STDQUAD_TEX2A)
    uv1 = signedPosition;
#elif defined(STDQUAD_TEX3)
    uv1 = imageUv;
    uv2 = imageUv;
#elif defined(STDQUAD_TEX4)
    uv1 = imageUv;
    uv2 = imageUv;
    uv3 = imageUv;
#endif
}
