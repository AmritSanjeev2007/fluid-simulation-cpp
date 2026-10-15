#pragma once

#include <glm/glm.hpp>

#include <algorithm>

// Colour maps for field display. Keep in sync with the GLSL copies in assets/shaders/field.frag and volume.frag.
namespace colormap
{
    // Sequential, for magnitudes (polynomial fit of matplotlib's viridis).
    inline glm::vec3 viridis(float t)
    {
        const glm::vec3 c0 = { 0.2777273272f, 0.0054073445f, 0.3340998053f };
        const glm::vec3 c1 = { 0.1050930431f, 1.4046135299f, 1.3845901626f };
        const glm::vec3 c2 = { -0.3308618287f, 0.2148475595f, 0.0950951630f };
        const glm::vec3 c3 = { -4.6342304990f, -5.7991009734f, -19.3324409563f };
        const glm::vec3 c4 = { 6.2282699363f, 14.1799333668f, 56.6905526007f };
        const glm::vec3 c5 = { 4.7763849977f, -13.7451453777f, -65.3530326334f };
        const glm::vec3 c6 = { -5.4354558559f, 4.6458526122f, 26.3124352496f };
        return c0 + t * (c1 + t * (c2 + t * (c3 + t * (c4 + t * (c5 + t * c6)))));
    }

    // Diverging blue-white-red, for values on either side of a reference (endpoints of Moreland's cool-warm).
    inline glm::vec3 coolWarm(float t)
    {
        const glm::vec3 cool  = { 0.230f, 0.299f, 0.754f };
        const glm::vec3 white = { 0.865f, 0.865f, 0.865f };
        const glm::vec3 warm  = { 0.706f, 0.016f, 0.150f };
        return t < 0.5f ? glm::mix(cool, white, 2.0f * t) : glm::mix(white, warm, 2.0f * t - 1.0f);
    }

    // Opacity of the volume view's ramp: transparent at the low end, 0.5 at the high end.
    // Keep in sync with assets/shaders/volume.frag.
    inline float volumeOpacity(float t) { return 0.5f * std::clamp(t, 0.0f, 1.0f); }

    inline glm::vec3 sample(bool diverging, float t)
    {
        t = std::clamp(t, 0.0f, 1.0f);
        return diverging ? coolWarm(t) : viridis(t);
    }
}
