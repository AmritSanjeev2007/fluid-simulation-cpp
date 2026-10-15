#version 410 core

layout(location = 0) in vec4 aPoint;  // xyz = world position, w = field value

uniform mat4 uViewProjection;

out float vValue;

void main()
{
    vValue      = aPoint.w;
    gl_Position = uViewProjection * vec4(aPoint.xyz, 1.0);
}
