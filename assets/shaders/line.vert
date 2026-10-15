#version 410 core

// The 12 edges of an axis-aligned box, generated from gl_VertexID (draw 24 vertices as lines).

uniform mat4 uViewProjection;
uniform vec3 uBoxMin;
uniform vec3 uBoxMax;

// Pairs of corner indices; bit 0 of a corner selects max x, bit 1 max y, bit 2 max z.
const int edges[24] = int[24](0, 1, 2, 3, 4, 5, 6, 7,
                              0, 2, 1, 3, 4, 6, 5, 7,
                              0, 4, 1, 5, 2, 6, 3, 7);

void main()
{
    int  corner = edges[gl_VertexID];
    vec3 t      = vec3(corner & 1, (corner >> 1) & 1, (corner >> 2) & 1);
    gl_Position = uViewProjection * vec4(mix(uBoxMin, uBoxMax, t), 1.0);
}
