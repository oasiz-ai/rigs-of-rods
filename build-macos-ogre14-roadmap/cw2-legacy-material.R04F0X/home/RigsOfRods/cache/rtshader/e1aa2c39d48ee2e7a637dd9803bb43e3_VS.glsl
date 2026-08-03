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
uniform	mat3	normal_matrix;
uniform	mat4	worldview_matrix;
uniform	mat4	texture_matrix;
uniform	mat4	texture_matrix1;

//-----------------------------------------------------------------------------
//                         MAIN
//-----------------------------------------------------------------------------
IN(vec4	vertex, POSITION)
IN(vec3	normal, NORMAL)
IN(vec4	uv0, TEXCOORD0)
OUT(vec3	vsTexcoord_0, 0)
OUT(vec3	vsTexcoord_1, 1)
OUT(vec2	vsTexcoord_2, 2)
OUT(vec2	vsTexcoord_3, 3)
void main(void) {
	vec4	lColor_0;
	vec4	lColor_1;

	gl_Position	=	mul(worldviewproj_matrix, vertex);
	lColor_0	=	vec4(1.00000,1.00000,1.00000,1.00000);
	lColor_1	=	vec4(0.00000,0.00000,0.00000,0.00000);
	vsTexcoord_0	=	mul(normal_matrix, normal);
	FFP_Transform(worldview_matrix, vertex, vsTexcoord_1);
	vsTexcoord_2	=	uv0.xy;
	FFP_TransformTexCoord(texture_matrix, vsTexcoord_2, vsTexcoord_2);
	vsTexcoord_3	=	uv0.xy;
	FFP_TransformTexCoord(texture_matrix1, vsTexcoord_3, vsTexcoord_3);
}

