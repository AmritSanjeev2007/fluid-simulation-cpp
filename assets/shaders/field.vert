#version 410 core

// An axis-aligned slice through the simulation domain. The quad's corners come from
// gl_VertexID (draw 4 vertices as a triangle strip), so no vertex buffer is needed.

uniform mat4  uViewProjection;
uniform vec3  uDomainOrigin;   // world-space minimum corner
uniform vec3  uDomainExtent;   // world-space size
uniform int   uSliceAxis;      // 0 = X, 1 = Y, 2 = Z: the axis the slice is perpendicular to
uniform float uSlicePosition;  // 0..1 across the domain along that axis

out vec3 vTexCoord;  // 0..1 across the domain on every axis

void main()
{
    vec2 corner = vec2(gl_VertexID & 1, gl_VertexID >> 1);
    vec3 t;
    if (uSliceAxis == 0)
        t = vec3(uSlicePosition, corner.x, corner.y);
    else if (uSliceAxis == 1)
        t = vec3(corner.x, uSlicePosition, corner.y);
    else
        t = vec3(corner.x, corner.y, uSlicePosition);

    vTexCoord   = t;
    gl_Position = uViewProjection * vec4(uDomainOrigin + t * uDomainExtent, 1.0);
}
