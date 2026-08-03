OGRE_NATIVE_GLSL_VERSION_DIRECTIVE
//-----------------------------------------------------------------------------
//                         PROGRAM DEPENDENCIES
//-----------------------------------------------------------------------------
#define USE_OGRE_FROM_FUTURE
#include <OgreUnifiedShader.h>
#include "FFPLib_Transform.glsl"
#include "RTSLib_Colour.glsl"
#include "SGXLib_PerPixelLighting.glsl"
#include "FFPLib_Texturing.glsl"

//-----------------------------------------------------------------------------
//                         GLOBAL PARAMETERS
//-----------------------------------------------------------------------------

uniform	mat4	worldviewproj_matrix;
uniform	mat4	texture_worldviewproj_matrix_array[3];
uniform	mat3	normal_matrix;
uniform	mat4	worldview_matrix;
uniform	mat4	world_matrix;
uniform	mat4	inverse_transpose_world_matrix;
uniform	vec3	camera_position;

//-----------------------------------------------------------------------------
//                         MAIN
//-----------------------------------------------------------------------------
IN(vec4	vertex, POSITION)
IN(vec3	normal, NORMAL)
OUT(vec4	vsTexcoord_0, 0)
OUT(vec4	vsTexcoord_1, 1)
OUT(vec4	vsTexcoord_2, 2)
OUT(vec3	vsTexcoord_3, 3)
OUT(vec3	vsTexcoord_4, 4)
OUT(vec3	vsTexcoord_5, 5)
void main(void) {
	vec4	lColor_0;
	vec4	lColor_1;

	gl_Position	=	mul(worldviewproj_matrix, vertex);
	lColor_0	=	vec4(1.00000,1.00000,1.00000,1.00000);
	lColor_1	=	vec4(0.00000,0.00000,0.00000,0.00000);
	vsTexcoord_3	=	mul(normal_matrix, normal);
	FFP_Transform(worldview_matrix, vertex, vsTexcoord_4);
	FFP_GenerateTexCoord_EnvMap_Reflect(world_matrix, inverse_transpose_world_matrix, camera_position, normal, vertex, vsTexcoord_5);
	vsTexcoord_0	=	mul(texture_worldviewproj_matrix_array[int(0.00000)], vertex);
	vsTexcoord_1	=	mul(texture_worldviewproj_matrix_array[int(1.00000)], vertex);
	vsTexcoord_2	=	mul(texture_worldviewproj_matrix_array[int(2.00000)], vertex);
}

