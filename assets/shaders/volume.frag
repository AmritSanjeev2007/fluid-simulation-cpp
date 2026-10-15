#version 410 core

// Ray-marches the field through the domain, front to back. The colour ramp's opacity
// rises from 0 at the low end of the range to 0.5 at the high end. That opacity is what
// a ray picks up crossing the domain's longest side through uniform fluid (times
// uDensity), so the picture doesn't turn into fog at high grid resolutions. Rays stop at
// the obstacle so the mesh drawn earlier shows behind the fluid in front of it.

in vec2 vNdc;

uniform mat4      uInverseViewProjection;
uniform vec3      uDomainOrigin;   // world-space minimum corner
uniform vec3      uDomainExtent;   // world-space size
uniform float     uCellSize;       // world-space size of one cell
uniform float     uReferenceLength; // path length over which the ramp's opacity is reached
uniform sampler3D uField;          // cell-centred field values (R32F)
uniform sampler3D uSolid;          // 1/255 inside the obstacle, 0 in fluid (R8)
uniform vec2      uRange;          // values mapped to the two ends of the colour map
uniform int       uDiverging;      // 1: blue-white-red, 0: viridis
uniform float     uDensity;        // opacity multiplier, per unit of path
uniform float     uOpacity;        // overall opacity of the result

out vec4 fragColor;

const float maxOpacity   = 0.5;   // opacity at the high end of the ramp
const float stepsPerCell = 2.0;
const int   maxSteps     = 2048;

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
    // World-space ray through this pixel, starting on the near plane.
    vec4 nearPoint = uInverseViewProjection * vec4(vNdc, -1.0, 1.0);
    vec4 farPoint  = uInverseViewProjection * vec4(vNdc, 1.0, 1.0);
    vec3 origin    = nearPoint.xyz / nearPoint.w;
    vec3 direction = normalize(farPoint.xyz / farPoint.w - origin);

    // Slab test against the domain box.
    vec3 boxMin = uDomainOrigin;
    vec3 boxMax = uDomainOrigin + uDomainExtent;
    vec3 inv    = 1.0 / direction;
    vec3 t0     = (boxMin - origin) * inv;
    vec3 t1     = (boxMax - origin) * inv;
    float tNear = max(max(min(t0.x, t1.x), min(t0.y, t1.y)), max(min(t0.z, t1.z), 0.0));
    float tFar  = min(min(max(t0.x, t1.x), max(t0.y, t1.y)), max(t0.z, t1.z));
    if (tNear >= tFar)
        discard;

    float step       = uCellSize / stepsPerCell;
    float span       = max(uRange.y - uRange.x, 1e-20);
    float transmit   = 1.0;  // fraction of light still passing
    vec3  color      = vec3(0.0);
    float t          = tNear + 0.5 * step;
    for (int i = 0; i < maxSteps && t < tFar && transmit > 0.01; ++i, t += step)
    {
        vec3 uvw = (origin + t * direction - uDomainOrigin) / uDomainExtent;
        if (texture(uSolid, uvw).r * 255.0 > 0.5)
            break;  // hit the obstacle

        float value = texture(uField, uvw).r;
        float ramp  = clamp((value - uRange.x) / span, 0.0, 1.0);
        float alpha = clamp(maxOpacity * ramp * uDensity, 0.0, 0.999);
        alpha       = 1.0 - pow(1.0 - alpha, step / uReferenceLength);  // this step's share

        vec3 sampleColor = uDiverging != 0 ? coolWarm(ramp) : viridis(ramp);
        color    += transmit * alpha * sampleColor;
        transmit *= 1.0 - alpha;
    }

    // Premultiplied alpha.
    fragColor = vec4(color, 1.0 - transmit) * uOpacity;
}
