OGRE_NATIVE_GLSL_VERSION_DIRECTIVE
//-----------------------------------------------------------------------------
//                         PROGRAM DEPENDENCIES
//-----------------------------------------------------------------------------
#define USE_OGRE_FROM_FUTURE
#include <OgreUnifiedShader.h>
#include "FFPLib_Transform.glsl"
#include "SGXLib_NormalMap.glsl"
#include "SGXLib_IntegratedPSSM.glsl"
#include "TerrainSurface.glsl"
#include "SGXLib_PerPixelLighting.glsl"

//-----------------------------------------------------------------------------
//                         GLOBAL PARAMETERS
//-----------------------------------------------------------------------------

SAMPLER2D(globalNormal0, 0);
SAMPLER2D(blendTex1, 1);
SAMPLER2D(difftex2, 2);
uniform	vec4	uvMul0;
uniform	mat3	normal_matrix;
uniform	vec4	derived_ambient_light_colour;
uniform	vec4	surface_emissive_colour;
uniform	vec4	derived_scene_colour;
uniform	float	surface_shininess;
uniform	vec4	light_position_view_space_array[1];
uniform	vec4	light_direction_view_space_array[1];
uniform	vec4	light_attenuation_array[1];
uniform	vec4	spotlight_params_array[1];
uniform	vec4	derived_light_diffuse_colour_array[1];
uniform	vec4	light_specular_colour_power_scaled_array[1];

//-----------------------------------------------------------------------------
//                         MAIN
//-----------------------------------------------------------------------------
IN(f32vec2	vsTexcoord_0, 0)
IN(vec3	vsTexcoord_1, 1)
void main(void) {
	vec3	lTexcoord_0;
	vec4	lColor_1;
	vec4	diffuseSpec;
	vec3	TSnormal;
	vec4	texTmp;
	vec4	lColor_5;
	vec4	blendWeight1;
	mat3	TBN;

	gl_FragColor	=	vec4(1.00000,1.00000,1.00000,1.00000);
	SGX_FetchNormal(globalNormal0, vsTexcoord_0, lTexcoord_0);
	lTexcoord_0	=	mul(normal_matrix, lTexcoord_0);
	lColor_5	=	vec4(0.00000,0.00000,0.00000,0.00000);
	blendWeight1	=	texture2D(blendTex1, vsTexcoord_0);
	SGX_CalculateTerrainTBN(lTexcoord_0, normal_matrix, TBN);
	diffuseSpec	=	vec4(0.00000,0.00000,0.00000,0.00000);
	TSnormal	=	vec3(0.00000,0.00000,1.00000);
	blendTerrainLayer(1.00000, vsTexcoord_0, uvMul0.x, difftex2, diffuseSpec);
	lColor_1	=	diffuseSpec.w*vec4(1.00000,1.00000,1.00000,1.00000);
	gl_FragColor	=	derived_scene_colour;
	evaluateLight(lTexcoord_0, vsTexcoord_1, light_position_view_space_array[int(0.00000)], light_attenuation_array[int(0.00000)], light_direction_view_space_array[int(0.00000)], spotlight_params_array[int(0.00000)], derived_light_diffuse_colour_array[int(0.00000)], gl_FragColor.xyz, lColor_1, light_specular_colour_power_scaled_array[int(0.00000)], surface_shininess, lColor_5.xyz);
	lColor_1	=	gl_FragColor;
	gl_FragColor	=	diffuseSpec*gl_FragColor;
	gl_FragColor.xyz	=	gl_FragColor.xyz+lColor_5.xyz;
}

