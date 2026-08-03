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

//-----------------------------------------------------------------------------
//                         MAIN
//-----------------------------------------------------------------------------
IN(vec4	vertex, POSITION)
IN(vec4	colour, COLOR)
IN(vec4	uv0, TEXCOORD0)
OUT(vec4	vsColor_0, 0)
OUT(vec2	vsTexcoord_0, 1)
void main(void) {
	vec4	lColor_0;

	gl_Position	=	mul(worldviewproj_matrix, vertex);
	vec4 local_colour = colour;
	ENABLE_LINEAR_COLOUR(local_colour);
	vsColor_0	=	local_colour;
	lColor_0	=	vec4(0.00000,0.00000,0.00000,0.00000);
	vsTexcoord_0	=	uv0.xy;
}

