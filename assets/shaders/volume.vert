#version 410 core

// Full-screen quad from gl_VertexID (draw 4 vertices as a triangle strip). The fragment
// shader casts a ray through the simulation domain for every pixel.

out vec2 vNdc;

void main()
{
    vNdc        = vec2(gl_VertexID & 1, gl_VertexID >> 1) * 2.0 - 1.0;
    gl_Position = vec4(vNdc, 0.0, 1.0);
}
