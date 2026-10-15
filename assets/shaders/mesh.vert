#version 410 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;

uniform mat4 uModel;
uniform mat4 uViewProjection;
uniform mat3 uNormalMatrix;  // inverse-transpose of uModel's upper 3x3
uniform vec2 uScreenOffset;  // shift in normalised device coordinates (the hover outline's copies)

out vec3 vNormal;  // world space

void main()
{
    vNormal     = uNormalMatrix * aNormal;
    gl_Position = uViewProjection * uModel * vec4(aPosition, 1.0);
    gl_Position.xy += uScreenOffset * gl_Position.w;  // after the divide by w this is a fixed screen shift
}
