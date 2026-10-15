#version 410 core

layout(location = 0) in vec2 aPosition;  // normalised device coordinates

void main()
{
    gl_Position = vec4(aPosition, 0.0, 1.0);
}
