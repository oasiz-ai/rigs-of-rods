OGRE_NATIVE_GLSL_VERSION_DIRECTIVE
//-----------------------------------------------------------------------------
//                         PROGRAM DEPENDENCIES
//-----------------------------------------------------------------------------
#define USE_OGRE_FROM_FUTURE
#include <OgreUnifiedShader.h>
#include "RTSLib_Colour.glsl"
#include "SGXLib_IntegratedPSSM.glsl"
#include "SGXLib_PerPixelLighting.glsl"
#include "FFPLib_Texturing.glsl"

//-----------------------------------------------------------------------------
//                         GLOBAL PARAMETERS
//-----------------------------------------------------------------------------

SAMPLER2DSHADOW(shadow_map1, 1);
SAMPLER2DSHADOW(shadow_map2, 2);
SAMPLER2DSHADOW(shadow_map3, 3);
SAMPLERCUBE(gTextureSampler0, 0);
uniform	vec4	pssm_split_points0;
uniform	vec4	inverse_texture_size1;
uniform	vec4	inverse_texture_size2;
uniform	vec4	inverse_texture_size3;
uniform	vec4	derived_ambient_light_colour;
uniform	vec4	surface_emissive_colour;
uniform	vec4	derived_scene_colour;
uniform	vec4	light_position_view_space_array[13];
uniform	vec4	light_direction_view_space_array[13];
uniform	vec4	light_attenuation_array[13];
uniform	vec4	spotlight_params_array[13];
uniform	vec4	derived_light_diffuse_colour_array[13];

//-----------------------------------------------------------------------------
//                         MAIN
//-----------------------------------------------------------------------------
IN(vec4	vsTexcoord_0, 0)
IN(vec4	vsTexcoord_1, 1)
IN(vec4	vsTexcoord_2, 2)
IN(vec3	vsTexcoord_3, 3)
IN(vec3	vsTexcoord_4, 4)
IN(vec3	vsTexcoord_5, 5)
void main(void) {
	vec4	lColor_0;
	vec4	lColor_1;
	float	lShadowFactor[1];
	float	fdepth;
	vec4	texel_0;

	lColor_0	=	vec4(1.00000,1.00000,1.00000,1.00000);
	lColor_1	=	vec4(0.00000,0.00000,0.00000,0.00000);
	gl_FragColor	=	lColor_0;
	fdepth	=	gl_FragCoord.z;
	SGX_ComputeShadowFactor_PSSM3(fdepth, pssm_split_points0, vsTexcoord_0, shadow_map1, inverse_texture_size1.xy, vsTexcoord_1, shadow_map2, inverse_texture_size2.xy, vsTexcoord_2, shadow_map3, inverse_texture_size3.xy, lShadowFactor[int(0.00000)]);
	gl_FragColor	=	derived_scene_colour;
	evaluateLight(vsTexcoord_3, vsTexcoord_4, light_position_view_space_array[int(0.00000)], light_attenuation_array[int(0.00000)], light_direction_view_space_array[int(0.00000)], spotlight_params_array[int(0.00000)], derived_light_diffuse_colour_array[int(0.00000)], gl_FragColor.xyz, lShadowFactor[int(0.00000)]);
	evaluateLight(vsTexcoord_3, vsTexcoord_4, light_position_view_space_array[int(1.00000)], light_attenuation_array[int(1.00000)], light_direction_view_space_array[int(1.00000)], spotlight_params_array[int(1.00000)], derived_light_diffuse_colour_array[int(1.00000)], gl_FragColor.xyz, 1.00000);
	evaluateLight(vsTexcoord_3, vsTexcoord_4, light_position_view_space_array[int(2.00000)], light_attenuation_array[int(2.00000)], light_direction_view_space_array[int(2.00000)], spotlight_params_array[int(2.00000)], derived_light_diffuse_colour_array[int(2.00000)], gl_FragColor.xyz, 1.00000);
	evaluateLight(vsTexcoord_3, vsTexcoord_4, light_position_view_space_array[int(3.00000)], light_attenuation_array[int(3.00000)], light_direction_view_space_array[int(3.00000)], spotlight_params_array[int(3.00000)], derived_light_diffuse_colour_array[int(3.00000)], gl_FragColor.xyz, 1.00000);
	evaluateLight(vsTexcoord_3, vsTexcoord_4, light_position_view_space_array[int(4.00000)], light_attenuation_array[int(4.00000)], light_direction_view_space_array[int(4.00000)], spotlight_params_array[int(4.00000)], derived_light_diffuse_colour_array[int(4.00000)], gl_FragColor.xyz, 1.00000);
	evaluateLight(vsTexcoord_3, vsTexcoord_4, light_position_view_space_array[int(5.00000)], light_attenuation_array[int(5.00000)], light_direction_view_space_array[int(5.00000)], spotlight_params_array[int(5.00000)], derived_light_diffuse_colour_array[int(5.00000)], gl_FragColor.xyz, 1.00000);
	evaluateLight(vsTexcoord_3, vsTexcoord_4, light_position_view_space_array[int(6.00000)], light_attenuation_array[int(6.00000)], light_direction_view_space_array[int(6.00000)], spotlight_params_array[int(6.00000)], derived_light_diffuse_colour_array[int(6.00000)], gl_FragColor.xyz, 1.00000);
	evaluateLight(vsTexcoord_3, vsTexcoord_4, light_position_view_space_array[int(7.00000)], light_attenuation_array[int(7.00000)], light_direction_view_space_array[int(7.00000)], spotlight_params_array[int(7.00000)], derived_light_diffuse_colour_array[int(7.00000)], gl_FragColor.xyz, 1.00000);
	evaluateLight(vsTexcoord_3, vsTexcoord_4, light_position_view_space_array[int(8.00000)], light_attenuation_array[int(8.00000)], light_direction_view_space_array[int(8.00000)], spotlight_params_array[int(8.00000)], derived_light_diffuse_colour_array[int(8.00000)], gl_FragColor.xyz, 1.00000);
	evaluateLight(vsTexcoord_3, vsTexcoord_4, light_position_view_space_array[int(9.00000)], light_attenuation_array[int(9.00000)], light_direction_view_space_array[int(9.00000)], spotlight_params_array[int(9.00000)], derived_light_diffuse_colour_array[int(9.00000)], gl_FragColor.xyz, 1.00000);
	evaluateLight(vsTexcoord_3, vsTexcoord_4, light_position_view_space_array[int(10.0000)], light_attenuation_array[int(10.0000)], light_direction_view_space_array[int(10.0000)], spotlight_params_array[int(10.0000)], derived_light_diffuse_colour_array[int(10.0000)], gl_FragColor.xyz, 1.00000);
	evaluateLight(vsTexcoord_3, vsTexcoord_4, light_position_view_space_array[int(11.0000)], light_attenuation_array[int(11.0000)], light_direction_view_space_array[int(11.0000)], spotlight_params_array[int(11.0000)], derived_light_diffuse_colour_array[int(11.0000)], gl_FragColor.xyz, 1.00000);
	evaluateLight(vsTexcoord_3, vsTexcoord_4, light_position_view_space_array[int(12.0000)], light_attenuation_array[int(12.0000)], light_direction_view_space_array[int(12.0000)], spotlight_params_array[int(12.0000)], derived_light_diffuse_colour_array[int(12.0000)], gl_FragColor.xyz, 1.00000);
	lColor_0	=	gl_FragColor;
	texel_0	=	textureCube(gTextureSampler0, vsTexcoord_5);
	ENABLE_LINEAR_COLOUR(texel_0);
	gl_FragColor	=	texel_0*gl_FragColor;
	gl_FragColor.xyz	=	gl_FragColor.xyz+lColor_1.xyz;
	COLOUR_TRANSFER(gl_FragColor);
}

