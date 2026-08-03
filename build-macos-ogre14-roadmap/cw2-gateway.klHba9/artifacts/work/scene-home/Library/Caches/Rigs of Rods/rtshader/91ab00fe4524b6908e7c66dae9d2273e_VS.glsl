OGRE_NATIVE_GLSL_VERSION_DIRECTIVE
//-----------------------------------------------------------------------------
//                         PROGRAM DEPENDENCIES
//-----------------------------------------------------------------------------
#define USE_OGRE_FROM_FUTURE
#include <OgreUnifiedShader.h>
#include "FFPLib_Transform.glsl"
#include "SGXLib_PerPixelLighting.glsl"

//-----------------------------------------------------------------------------
//                         GLOBAL PARAMETERS
//-----------------------------------------------------------------------------

uniform	mat4	worldviewproj_matrix;
uniform	mat3	normal_matrix;
uniform	mat4	worldview_matrix;

//-----------------------------------------------------------------------------
//                         MAIN
//-----------------------------------------------------------------------------
IN(vec4	vertex, POSITION)
IN(vec4	uv0, TEXCOORD0)
OUT(vec2	vsTexcoord_0, 0)
OUT(vec3	vsTexcoord_1, 1)
void main(void) {

	gl_Position	=	mul(worldviewproj_matrix, vertex);
	FFP_Transform(worldview_matrix, vertex, vsTexcoord_1);
	vsTexcoord_0	=	uv0.xy;
}

