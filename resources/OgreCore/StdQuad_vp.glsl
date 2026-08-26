#version 150

in vec4 vertex;
out gl_PerVertex { vec4 gl_Position; };
out vec2 uv;

uniform mat4 worldViewProj;

void main()                    
{
	gl_Position = worldViewProj * vertex;
	
	vec2 inPos = sign(vertex.xy);
	
	uv = (vec2(inPos.x, -inPos.y) + 1.0)/2.0;
}
