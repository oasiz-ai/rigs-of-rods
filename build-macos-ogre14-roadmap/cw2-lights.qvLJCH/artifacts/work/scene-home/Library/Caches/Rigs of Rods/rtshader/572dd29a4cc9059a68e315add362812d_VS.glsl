OGRE_NATIVE_GLSL_VERSION_DIRECTIVE
//-----------------------------------------------------------------------------
//                         PROGRAM DEPENDENCIES
//-----------------------------------------------------------------------------
#define USE_OGRE_FROM_FUTURE
#include <OgreUnifiedShader.h>
#include "FFPLib_Transform.glsl"
#include "TerrainTransforms.glsl"
#include "RTSLib_Colour.glsl"
#include "FFPLib_Texturing.glsl"

//-----------------------------------------------------------------------------
//                         GLOBAL PARAMETERS
//-----------------------------------------------------------------------------

uniform	mat4	worldviewproj_matrix;
uniform	vec2	custom1001;

//-----------------------------------------------------------------------------
//                         MAIN
//-----------------------------------------------------------------------------
IN(vec4	vertex, POSITION)
IN(vec4	uv1, TEXCOORD1)
IN(vec4	uv0, TEXCOORD0)
OUT(vec2	vsTexcoord_0, 0)
void main(void) {
	vec4	lColor_0;
	vec4	lColor_1;

	vsTexcoord_0	=	uv0.xy;
	gl_Position	=	mul(worldviewproj_matrix, vertex);
	applyLODMorph(uv1.xy, custom1001, gl_Position.y);
	lColor_0	=	vec4(1.00000,1.00000,1.00000,1.00000);
	lColor_1	=	vec4(0.00000,0.00000,0.00000,0.00000);
}

