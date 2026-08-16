#version 150

precision highp int;
precision highp float;

in vec4 position;
in vec4 uv0;
in vec4 colour;
uniform float YFlipScale;

out vec4 outUV0;
out vec4 outColor;

out gl_PerVertex
{
	vec4 gl_Position;
	float gl_PointSize;
	float gl_ClipDistance[];
};

// Texturing vertex program for the MyGUI OGRE backend.
void main()
{
	vec4 vpos = position;
	vpos.y *= YFlipScale;
	gl_Position = vpos;
	outUV0 = uv0;
	outColor = colour;
}
