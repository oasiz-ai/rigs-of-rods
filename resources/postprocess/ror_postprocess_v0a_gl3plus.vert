#version 330 core

/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

// Ogre::VES_POSITION is bound to location zero by GL3Plus.
layout(location = 0) in vec4 vertex;

uniform mat4 uWorldViewProj;

out vec2 vUv;

void main()
{
    gl_Position = uWorldViewProj * vertex;

    // Match OGRE's standard compositor quad convention on core-profile GL.
    vec2 inPosition = sign(vertex.xy);
    vUv = (vec2(inPosition.x, -inPosition.y) + 1.0) * 0.5;
}
