#pragma once

#include "grid3.hpp"
#include "mac_grid.hpp"

#include <glm/glm.hpp>

#include <vector>

// Polylines through the velocity field, stored back to back.
struct Streamlines
{
    std::vector<glm::vec4> points;  // xyz = world position, w = value of the colour field there
    std::vector<int>       first;   // index of each line's first point
    std::vector<int>       count;   // number of points in each line
};

// Traces streamlines downstream from a resolution x resolution grid of seeds on a plane
// perpendicular to the free stream, one cell outside the domain box upstream of its most
// upstream corner, and spanning the domain's cross-section. Outside the domain the flow is
// the free stream, so each line runs straight from its seed to where it enters the box and
// is traced through the velocity field from there. Seeds whose line misses the box are skipped.
// Each point carries the value of colourField (cell-centred) at its position.
void traceStreamlines(const MacGrid& grid, const glm::vec3& freeStream, bool hasObstacle, int resolution,
                      const Grid3<float>& colourField, Streamlines& out);
