#include "streamlines.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

void traceStreamlines(const MacGrid& grid, const glm::vec3& freeStream, bool hasObstacle, int resolution,
                      const Grid3<float>& colourField, Streamlines& out)
{
    out.points.clear();
    out.first.clear();
    out.count.clear();

    const float speed = glm::length(freeStream);
    if (speed < 1e-6f || resolution < 1 || colourField.size() != grid.cells())
        return;

    const Domain&   domain = grid.domain;
    const float     h      = domain.h;
    const glm::vec3 n      = freeStream / speed;

    const glm::vec3 boxMin = domain.origin;
    const glm::vec3 boxMax = domain.origin + domain.extent();

    // The plane sits one cell upstream of the domain's most upstream corner.
    const glm::vec3 centre     = domain.center();
    const float     halfLength = 0.5f * glm::dot(glm::abs(n), domain.extent());  // box half-extent along n
    const glm::vec3 planePoint = centre - n * (halfLength + h);

    // Orthonormal axes in the plane.
    const glm::vec3 helper = std::abs(n.x) < 0.9f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
    const glm::vec3 u      = glm::normalize(glm::cross(n, helper));
    const glm::vec3 v      = glm::cross(n, u);

    // Cover the domain's cross-section: the extent of its corners projected onto the plane.
    glm::vec2 rectMin(std::numeric_limits<float>::max()), rectMax(std::numeric_limits<float>::lowest());
    for (int c = 0; c < 8; ++c)
    {
        const glm::vec3 corner = domain.origin + glm::vec3(c & 1, (c >> 1) & 1, (c >> 2) & 1) * domain.extent();
        const glm::vec2 q(glm::dot(corner - planePoint, u), glm::dot(corner - planePoint, v));
        rectMin = glm::min(rectMin, q);
        rectMax = glm::max(rectMax, q);
    }

    const glm::vec3 extent    = domain.extent();
    const float     step      = 0.5f * h;
    const int       maxPoints = static_cast<int>(2.0f * glm::length(extent) / step) + 2;
    const int       seeds     = resolution * resolution;

    auto inside = [&](const glm::vec3& p)
    { return domain.contains(p) && (!hasObstacle || grid.phiAt(p) >= 0.0f); };

    // Unit direction of the flow, or false where it stagnates.
    auto direction = [&](const glm::vec3& p, glm::vec3& d)
    {
        const glm::vec3 vel = grid.velocityAt(p);
        const float     len = glm::length(vel);
        if (len < 1e-3f * speed)
            return false;
        d = vel / len;
        return true;
    };

    std::vector<std::vector<glm::vec4>> lines(seeds);
#pragma omp parallel for schedule(dynamic, 8)
    for (int s = 0; s < seeds; ++s)
    {
        const float a = rectMin.x + (rectMax.x - rectMin.x) * ((s % resolution) + 0.5f) / resolution;
        const float b = rectMin.y + (rectMax.y - rectMin.y) * ((s / resolution) + 0.5f) / resolution;
        const glm::vec3 seed = planePoint + a * u + b * v;

        // Where the straight free-stream path from the seed enters the box (slab test).
        float tEnter = 0.0f, tExit = std::numeric_limits<float>::max();
        for (int axis = 0; axis < 3; ++axis)
        {
            if (std::abs(n[axis]) < 1e-12f)
            {
                if (seed[axis] < boxMin[axis] || seed[axis] > boxMax[axis])
                    tExit = -1.0f;
                continue;
            }
            const float t0 = (boxMin[axis] - seed[axis]) / n[axis];
            const float t1 = (boxMax[axis] - seed[axis]) / n[axis];
            tEnter = std::max(tEnter, std::min(t0, t1));
            tExit  = std::min(tExit, std::max(t0, t1));
        }
        if (tEnter >= tExit)
            continue;  // misses the domain
        glm::vec3 p = seed + n * (tEnter + 1e-3f * h);
        if (!inside(p))
            continue;

        std::vector<glm::vec4>& line = lines[s];
        line.emplace_back(seed, grid.sampleCell(colourField, seed));
        line.emplace_back(p, grid.sampleCell(colourField, p));
        while (static_cast<int>(line.size()) < maxPoints)
        {
            // Midpoint (RK2) step of fixed length along the flow direction.
            glm::vec3 d1, d2;
            if (!direction(p, d1) || !direction(p + 0.5f * step * d1, d2))
                break;
            p += step * d2;
            if (!inside(p))
                break;
            line.emplace_back(p, grid.sampleCell(colourField, p));
        }
        if (line.size() < 2)
            line.clear();
    }

    for (const std::vector<glm::vec4>& line : lines)
    {
        if (line.empty())
            continue;
        out.first.push_back(static_cast<int>(out.points.size()));
        out.count.push_back(static_cast<int>(line.size()));
        out.points.insert(out.points.end(), line.begin(), line.end());
    }
}
