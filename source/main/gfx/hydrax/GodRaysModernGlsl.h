/*
--------------------------------------------------------------------------------
This source file is part of Hydrax.

Copyright (C) 2008 Xavier Verguin Gonzalez

This program is free software; you can redistribute it and/or modify it under
the terms of the GNU Lesser General Public License as published by the Free
Software Foundation; either version 2 of the License, or (at your option) any
later version.
--------------------------------------------------------------------------------
*/

#ifndef _Hydrax_GodRaysModernGlsl_H_
#define _Hydrax_GodRaysModernGlsl_H_

namespace Hydrax
{
namespace ModernGlsl
{
    inline const char* godRaysVertexSource(bool objectsIntersections)
    {
        if (objectsIntersections)
        {
            return R"HYDRAX_GLSL(#version 330 core

layout(location = 0) in vec4 vertex;

uniform mat4 uWorld;
uniform mat4 uTexViewProj;
uniform mat4 uWorldViewProj;

out vec3 positionWorld;
out vec4 projectionUv;

void main()
{
    gl_Position = uWorldViewProj * vertex;
    vec4 worldPosition = uWorld * vertex;
    positionWorld = worldPosition.xyz;
    projectionUv = uTexViewProj * worldPosition;
}
)HYDRAX_GLSL";
        }

        return R"HYDRAX_GLSL(#version 330 core

layout(location = 0) in vec4 vertex;

uniform mat4 uWorldViewProj;

void main()
{
    gl_Position = uWorldViewProj * vertex;
}
)HYDRAX_GLSL";
    }

    inline const char* godRaysFragmentSource(
        bool objectsIntersections,
        bool causticsDepthChannel)
    {
        if (objectsIntersections && causticsDepthChannel)
        {
            return R"HYDRAX_GLSL(#version 330 core

uniform vec3 uLightPosition;
uniform float uLightFarClipDistance;
uniform sampler2D uDepthMap;

in vec3 positionWorld;
in vec4 projectionUv;

layout(location = 0) out vec4 fragmentColor;

void main()
{
    vec2 depthUv = projectionUv.xy / projectionUv.w;
    float depth = texture(uDepthMap, depthUv).r;
    float rayDepth = clamp(
        length(positionWorld - uLightPosition) / uLightFarClipDistance,
        0.0,
        1.0);
    fragmentColor = depth < rayDepth
        ? vec4(0.0, 0.0, 0.0, 1.0)
        : vec4(0.0, 0.0, 0.1, 1.0);
}
)HYDRAX_GLSL";
        }
        if (objectsIntersections)
        {
            return R"HYDRAX_GLSL(#version 330 core

uniform vec3 uLightPosition;
uniform float uLightFarClipDistance;
uniform sampler2D uDepthMap;

in vec3 positionWorld;
in vec4 projectionUv;

layout(location = 0) out vec4 fragmentColor;

void main()
{
    vec2 depthUv = projectionUv.xy / projectionUv.w;
    float depth = texture(uDepthMap, depthUv).r;
    float rayDepth = clamp(
        length(positionWorld - uLightPosition) / uLightFarClipDistance,
        0.0,
        1.0);
    fragmentColor = depth < rayDepth
        ? vec4(0.0, 0.0, 0.0, 1.0)
        : vec4(0.0, 0.1, 0.0, 1.0);
}
)HYDRAX_GLSL";
        }
        if (causticsDepthChannel)
        {
            return R"HYDRAX_GLSL(#version 330 core

layout(location = 0) out vec4 fragmentColor;

void main()
{
    fragmentColor = vec4(0.0, 0.0, 0.1, 1.0);
}
)HYDRAX_GLSL";
        }

        return R"HYDRAX_GLSL(#version 330 core

layout(location = 0) out vec4 fragmentColor;

void main()
{
    fragmentColor = vec4(0.0, 0.1, 0.0, 1.0);
}
)HYDRAX_GLSL";
    }

    inline const char* godRaysDepthVertexSource()
    {
        return R"HYDRAX_GLSL(#version 330 core

layout(location = 0) in vec4 vertex;

uniform mat4 uWorld;
uniform mat4 uWorldViewProj;

out vec3 positionWorld;

void main()
{
    gl_Position = uWorldViewProj * vertex;
    positionWorld = (uWorld * vertex).xyz;
}
)HYDRAX_GLSL";
    }

    inline const char* godRaysDepthFragmentSource()
    {
        return R"HYDRAX_GLSL(#version 330 core

uniform vec3 uLightPosition;
uniform float uLightFarClipDistance;

in vec3 positionWorld;

layout(location = 0) out vec4 fragmentColor;

void main()
{
    float depth = clamp(
        length(positionWorld - uLightPosition) / uLightFarClipDistance,
        0.0,
        1.0);
    fragmentColor = vec4(depth, 0.0, 0.0, 0.0);
}
)HYDRAX_GLSL";
    }
}
}

#endif
