OGRE_NATIVE_GLSL_VERSION_DIRECTIVE
//-----------------------------------------------------------------------------
//                         PROGRAM DEPENDENCIES
//-----------------------------------------------------------------------------
#define USE_OGRE_FROM_FUTURE
#include <OgreUnifiedShader.h>
#include "FFPLib_Transform.glsl"
#include "RTSLib_Colour.glsl"
#include "FFPLib_Texturing.glsl"

//-----------------------------------------------------------------------------
//                         GLOBAL PARAMETERS
//-----------------------------------------------------------------------------

uniform	mat4	worldviewproj_matrix;
uniform	mat4	texture_worldviewproj_matrix_array[3];

//-----------------------------------------------------------------------------
//                         MAIN
//-----------------------------------------------------------------------------
IN(vec4	vertex, POSITION)
IN(vec4	uv0, TEXCOORD0)
OUT(vec4	vsTexcoord_0, 0)
OUT(vec4	vsTexcoord_1, 1)
OUT(vec4	vsTexcoord_2, 2)
OUT(vec3	vsTexcoord_3, 3)
void main(void) {
	vec4	lColor_0;
	vec4	lColor_1;

	gl_Position	=	mul(worldviewproj_matrix, vertex);
	lColor_0	=	vec4(1.00000,1.00000,1.00000,1.00000);
	lColor_1	=	vec4(0.00000,0.00000,0.00000,0.00000);
	vsTexcoord_3	=	uv0.xyz;
	vsTexcoord_0	=	mul(texture_worldviewproj_matrix_array[int(0.00000)], vertex);
	vsTexcoord_1	=	mul(texture_worldviewproj_matrix_array[int(1.00000)], vertex);
	vsTexcoord_2	=	mul(texture_worldviewproj_matrix_array[int(2.00000)], vertex);
}

