#version 410 core

in vec3 vNormal;

uniform vec3  uColor;
uniform vec3  uLightDirection;  // direction the light travels, world space, normalised
uniform vec3  uLightColor;
uniform float uAmbient;
uniform bool  uOutline;       // draw flat uOutlineColor instead of the lit surface
uniform vec3  uOutlineColor;

out vec4 fragColor;

void main()
{
    if (uOutline)
    {
        fragColor = vec4(uOutlineColor, 1.0);
        return;
    }

    vec3  normal  = normalize(vNormal);
    float diffuse = max(dot(normal, -uLightDirection), 0.0);
    vec3  light   = uLightColor * (uAmbient + (1.0 - uAmbient) * diffuse);
    fragColor     = vec4(uColor * light, 1.0);
}
