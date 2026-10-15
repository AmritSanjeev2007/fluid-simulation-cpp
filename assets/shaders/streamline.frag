#version 410 core

in float vValue;

uniform vec2 uRange;      // values mapped to the two ends of the colour map
uniform int  uDiverging;  // 1: blue-white-red, 0: viridis
uniform float uOpacity;   // overall opacity

out vec4 fragColor;

// Keep in sync with src/render/colormap.hpp.
vec3 viridis(float t)
{
    const vec3 c0 = vec3(0.2777273272, 0.0054073445, 0.3340998053);
    const vec3 c1 = vec3(0.1050930431, 1.4046135299, 1.3845901626);
    const vec3 c2 = vec3(-0.3308618287, 0.2148475595, 0.0950951630);
    const vec3 c3 = vec3(-4.6342304990, -5.7991009734, -19.3324409563);
    const vec3 c4 = vec3(6.2282699363, 14.1799333668, 56.6905526007);
    const vec3 c5 = vec3(4.7763849977, -13.7451453777, -65.3530326334);
    const vec3 c6 = vec3(-5.4354558559, 4.6458526122, 26.3124352496);
    return c0 + t * (c1 + t * (c2 + t * (c3 + t * (c4 + t * (c5 + t * c6)))));
}

vec3 coolWarm(float t)
{
    const vec3 cool  = vec3(0.230, 0.299, 0.754);
    const vec3 white = vec3(0.865, 0.865, 0.865);
    const vec3 warm  = vec3(0.706, 0.016, 0.150);
    return t < 0.5 ? mix(cool, white, 2.0 * t) : mix(white, warm, 2.0 * t - 1.0);
}

void main()
{
    float t   = clamp((vValue - uRange.x) / max(uRange.y - uRange.x, 1e-20), 0.0, 1.0);
    fragColor = vec4(uDiverging != 0 ? coolWarm(t) : viridis(t), 1.0) * uOpacity;  // premultiplied
}
