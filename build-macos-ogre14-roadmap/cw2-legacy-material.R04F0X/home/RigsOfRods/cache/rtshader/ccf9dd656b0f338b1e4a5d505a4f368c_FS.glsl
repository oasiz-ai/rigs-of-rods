OGRE_NATIVE_GLSL_VERSION_DIRECTIVE
//-----------------------------------------------------------------------------
//                         PROGRAM DEPENDENCIES
//-----------------------------------------------------------------------------
#define USE_OGRE_FROM_FUTURE
#include <OgreUnifiedShader.h>
#include "RTSLib_Colour.glsl"
#include "SGXLib_PerPixelLighting.glsl"
#include "FFPLib_Texturing.glsl"

//-----------------------------------------------------------------------------
//                         GLOBAL PARAMETERS
//-----------------------------------------------------------------------------

uniform	vec4	derived_ambient_light_colour;
uniform	vec4	surface_emissive_colour;
uniform	vec4	derived_scene_colour;
uniform	float	surface_shininess;
uniform	vec4	light_position_view_space_array[3];
uniform	vec4	light_direction_view_space_array[3];
uniform	vec4	light_attenuation_array[3];
uniform	vec4	spotlight_params_array[3];
uniform	vec4	derived_light_diffuse_colour_array[3];
uniform	vec4	derived_light_specular_colour_array[3];

//-----------------------------------------------------------------------------
//                         MAIN
//-----------------------------------------------------------------------------
IN(vec3	vsTexcoord_0, 0)
IN(vec3	vsTexcoord_1, 1)
void main(void) {
	vec4	lColor_0;
	vec4	lColor_1;

	lColor_0	=	vec4(1.00000,1.00000,1.00000,1.00000);
	lColor_1	=	vec4(0.00000,0.00000,0.00000,0.00000);
	gl_FragColor	=	lColor_0;
	gl_FragColor	=	derived_scene_colour;
	evaluateLight(vsTexcoord_0, vsTexcoord_1, light_position_view_space_array[int(0.00000)], light_attenuation_array[int(0.00000)], light_direction_view_space_array[int(0.00000)], spotlight_params_array[int(0.00000)], derived_light_diffuse_colour_array[int(0.00000)], gl_FragColor.xyz, derived_light_specular_colour_array[int(0.00000)], surface_shininess, lColor_1.xyz);
	evaluateLight(vsTexcoord_0, vsTexcoord_1, light_position_view_space_array[int(1.00000)], light_attenuation_array[int(1.00000)], light_direction_view_space_array[int(1.00000)], spotlight_params_array[int(1.00000)], derived_light_diffuse_colour_array[int(1.00000)], gl_FragColor.xyz, derived_light_specular_colour_array[int(1.00000)], surface_shininess, lColor_1.xyz);
	evaluateLight(vsTexcoord_0, vsTexcoord_1, light_position_view_space_array[int(2.00000)], light_attenuation_array[int(2.00000)], light_direction_view_space_array[int(2.00000)], spotlight_params_array[int(2.00000)], derived_light_diffuse_colour_array[int(2.00000)], gl_FragColor.xyz, derived_light_specular_colour_array[int(2.00000)], surface_shininess, lColor_1.xyz);
	lColor_0	=	gl_FragColor;
	gl_FragColor.xyz	=	gl_FragColor.xyz+lColor_1.xyz;
	COLOUR_TRANSFER(gl_FragColor);
}

