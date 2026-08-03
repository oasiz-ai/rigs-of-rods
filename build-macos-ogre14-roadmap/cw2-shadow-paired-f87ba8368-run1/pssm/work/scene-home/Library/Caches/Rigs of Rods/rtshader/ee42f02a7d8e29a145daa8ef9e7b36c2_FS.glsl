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
SAMPLER2D(normtex3, 3);
SAMPLER2DSHADOW(shadow_map4, 4);
SAMPLER2DSHADOW(shadow_map5, 5);
SAMPLER2DSHADOW(shadow_map6, 6);
uniform	vec4	uvMul0;
uniform	mat3	normal_matrix;
uniform	vec4	pssm_split_points1;
uniform	vec4	inverse_texture_size4;
uniform	vec4	inverse_texture_size5;
uniform	vec4	inverse_texture_size6;
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
IN(vec4	vsTexcoord_1, 1)
IN(vec4	vsTexcoord_2, 2)
IN(vec4	vsTexcoord_3, 3)
IN(vec3	vsTexcoord_4, 4)
void main(void) {
	vec3	lTexcoord_0;
	vec4	lColor_1;
	vec4	diffuseSpec;
	vec3	TSnormal;
	vec4	texTmp;
	vec4	lColor_5;
	vec4	blendWeight1;
	mat3	TBN;
	float	lShadowFactor[1];
	float	fdepth;

	gl_FragColor	=	vec4(1.00000,1.00000,1.00000,1.00000);
	SGX_FetchNormal(globalNormal0, vsTexcoord_0, lTexcoord_0);
	lTexcoord_0	=	mul(normal_matrix, lTexcoord_0);
	lColor_5	=	vec4(0.00000,0.00000,0.00000,0.00000);
	blendWeight1	=	texture2D(blendTex1, vsTexcoord_0);
	SGX_CalculateTerrainTBN(lTexcoord_0, normal_matrix, TBN);
	diffuseSpec	=	vec4(0.00000,0.00000,0.00000,0.00000);
	TSnormal	=	vec3(0.00000,0.00000,1.00000);
	blendTerrainLayer(1.00000, vsTexcoord_0, uvMul0.x, normtex3, TSnormal, difftex2, diffuseSpec);
	transformToTS(TSnormal, normal_matrix, lTexcoord_0);
	lColor_1	=	diffuseSpec.w*vec4(1.00000,1.00000,1.00000,1.00000);
	fdepth	=	gl_FragCoord.z;
	SGX_ComputeShadowFactor_PSSM3(fdepth, pssm_split_points1, vsTexcoord_1, shadow_map4, inverse_texture_size4.xy, vsTexcoord_2, shadow_map5, inverse_texture_size5.xy, vsTexcoord_3, shadow_map6, inverse_texture_size6.xy, lShadowFactor[int(0.00000)]);
	gl_FragColor	=	derived_scene_colour;
	evaluateLight(lTexcoord_0, vsTexcoord_4, light_position_view_space_array[int(0.00000)], light_attenuation_array[int(0.00000)], light_direction_view_space_array[int(0.00000)], spotlight_params_array[int(0.00000)], derived_light_diffuse_colour_array[int(0.00000)], gl_FragColor.xyz, lColor_1, light_specular_colour_power_scaled_array[int(0.00000)], surface_shininess, lColor_5.xyz, lShadowFactor[int(0.00000)]);
	lColor_1	=	gl_FragColor;
	gl_FragColor	=	diffuseSpec*gl_FragColor;
	gl_FragColor.xyz	=	gl_FragColor.xyz+lColor_5.xyz;
}

