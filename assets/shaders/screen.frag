#version 410 core

// Full-screen overlay. Draws the world axes gizmo (like Blender's viewport gizmo) in the
// top-right corner and discards every fragment outside the gizmo.

uniform vec2  uResolution;     // framebuffer size, pixels
uniform mat3  uViewRotation;   // world -> view space rotation (upper 3x3 of the view matrix)
uniform float uGizmoRadius;    // pixels, half the side of the gizmo's bounding box
uniform float uGizmoMargin;    // pixels between the bounding box and the top-right corner
uniform vec3  uAxisColors[3];  // X, Y, Z

out vec4 fragColor;  // premultiplied alpha

// Sizes in units of uGizmoRadius.
const float bubbleRadius = 0.2;
const float axisLength   = 1.0 - bubbleRadius;
const float lineWidth    = 0.035;  // half width
const float glyphWidth   = 0.025;  // half width of the X/Y/Z letter strokes

float segmentDistance(vec2 p, vec2 a, vec2 b)
{
    vec2  pa = p - a;
    vec2  ba = b - a;
    float h  = clamp(dot(pa, ba) / max(dot(ba, ba), 1e-6), 0.0, 1.0);
    return length(pa - ba * h);
}

// Distance to the letter of an axis, in a bubble's local space ([-1, 1] across the bubble).
float glyphDistance(int axis, vec2 q)
{
    if (axis == 0)  // X
        return min(segmentDistance(q, vec2(-0.4, -0.5), vec2(0.4, 0.5)),
                   segmentDistance(q, vec2(-0.4, 0.5), vec2(0.4, -0.5)));
    if (axis == 1)  // Y
        return min(min(segmentDistance(q, vec2(-0.4, 0.5), vec2(0.0, 0.0)),
                       segmentDistance(q, vec2(0.4, 0.5), vec2(0.0, 0.0))),
                   segmentDistance(q, vec2(0.0, 0.0), vec2(0.0, -0.5)));
    // Z
    return min(min(segmentDistance(q, vec2(-0.4, 0.5), vec2(0.4, 0.5)),
                   segmentDistance(q, vec2(0.4, 0.5), vec2(-0.4, -0.5))),
               segmentDistance(q, vec2(-0.4, -0.5), vec2(0.4, -0.5)));
}

// Composites a shape with signed distance d (negative inside) over dst, anti-aliased over aa.
vec4 over(vec4 dst, float d, float aa, vec3 color, float alpha)
{
    float a = alpha * (1.0 - smoothstep(-aa, aa, d));
    return vec4(color * a, a) + dst * (1.0 - a);
}

void main()
{
    vec2 centre = uResolution - vec2(uGizmoMargin + uGizmoRadius);
    vec2 p      = (gl_FragCoord.xy - centre) / uGizmoRadius;
    if (any(greaterThan(abs(p), vec2(1.0))))
        discard;

    float aa = 1.0 / uGizmoRadius;  // one pixel

    // Tips of the six half-axes in view space: +X, +Y, +Z, -X, -Y, -Z.
    vec3 tips[6];
    int  order[6];
    for (int i = 0; i < 3; ++i)
    {
        tips[i]     = uViewRotation[i];
        tips[i + 3] = -uViewRotation[i];
    }
    for (int i = 0; i < 6; ++i)
        order[i] = i;

    // Painter's algorithm: draw the farthest (most negative view z) first.
    for (int i = 0; i < 5; ++i)
        for (int j = 0; j < 5 - i; ++j)
            if (tips[order[j]].z > tips[order[j + 1]].z)
            {
                int t        = order[j];
                order[j]     = order[j + 1];
                order[j + 1] = t;
            }

    vec4 result = vec4(0.0);
    for (int k = 0; k < 6; ++k)
    {
        int  i        = order[k];
        int  axis     = i % 3;
        bool positive = i < 3;
        vec3 color    = uAxisColors[axis];
        vec2 tip      = tips[i].xy * axisLength;

        if (positive)
        {
            result = over(result, segmentDistance(p, vec2(0.0), tip) - lineWidth, aa, color, 1.0);
            result = over(result, length(p - tip) - bubbleRadius, aa, color, 1.0);
            vec2 q = (p - tip) / bubbleRadius;
            result = over(result, glyphDistance(axis, q) * bubbleRadius - glyphWidth, aa, vec3(0.0), 0.85);
        }
        else
        {
            // Negative axes: a darker bubble with a coloured rim and no line.
            float d = length(p - tip) - bubbleRadius;
            result  = over(result, d, aa, color * 0.35, 0.85);
            result  = over(result, abs(d + 0.02) - 0.02, aa, color, 1.0);
        }
    }

    if (result.a < 1.0 / 255.0)
        discard;
    fragColor = result;
}
