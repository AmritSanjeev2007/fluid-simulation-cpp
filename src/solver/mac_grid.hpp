#pragma once

#include "grid3.hpp"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>

// Axis-aligned simulation box divided into cubic cells of side h.
struct Domain
{
    glm::vec3  origin{ 0.0f };  // world-space minimum corner
    float      h = 1.0f;        // cell size, m
    glm::ivec3 size{ 0 };       // cells per axis

    glm::vec3 extent() const { return glm::vec3(size) * h; }
    glm::vec3 center() const { return origin + 0.5f * extent(); }
    glm::vec3 cellCenter(int i, int j, int k) const { return origin + (glm::vec3(i, j, k) + 0.5f) * h; }
    bool      contains(const glm::vec3& p) const
    {
        const glm::vec3 hi = origin + extent();
        return glm::all(glm::greaterThanEqual(p, origin)) && glm::all(glm::lessThanEqual(p, hi));
    }
};

// Condition on one face of the domain box (see solver.md, "Domain boundary conditions").
enum class BoundaryType : std::uint8_t
{
    Wall,     // u.n = 0, slip
    Inflow,   // far field: u = free-stream velocity, T = ambient
    Outflow,  // open: p = 0, zero normal gradient of u and T
};

// Domain faces, indexed 2 * axis + side: 0 = -x, 1 = +x, 2 = -y, 3 = +y, 4 = -z, 5 = +z.
using DomainBoundaries = std::array<BoundaryType, 6>;

// Staggered (MAC) grid: velocity components on face centres, scalars on cell centres.
class MacGrid
{
public:
    Domain           domain;
    DomainBoundaries boundaries{};

    std::array<Grid3<float>, 3> velocity;  // u, v, w on x-, y- and z-faces
    Grid3<float>                pressure;     // motion pressure, Pa
    Grid3<float>                temperature;  // K
    Grid3<float>                phi;          // signed distance to the obstacle, m (negative inside)
    Grid3<std::uint8_t>         solid;        // 1 for cells inside the obstacle

    void allocate(const Domain& d);

    glm::ivec3 cells() const { return domain.size; }
    glm::ivec3 faces(int axis) const { return velocity[axis].size(); }

    // Out-of-range cells count as fluid.
    bool cellSolid(int i, int j, int k) const { return solid.inRange(i, j, k) && solid(i, j, k) != 0; }

    // A face is solid if either cell it separates is solid.
    bool faceSolid(int axis, int i, int j, int k) const
    {
        glm::ivec3 below(i, j, k);
        below[axis] -= 1;
        return cellSolid(i, j, k) || cellSolid(below.x, below.y, below.z);
    }

    glm::vec3 facePosition(int axis, int i, int j, int k) const
    {
        glm::vec3 offset(0.5f);
        offset[axis] = 0.0f;
        return domain.origin + (glm::vec3(i, j, k) + offset) * domain.h;
    }

    // Trilinear samples at world positions (clamped to the grid).
    float     sampleFace(int axis, const Grid3<float>& field, const glm::vec3& p) const;
    float     sampleCell(const Grid3<float>& field, const glm::vec3& p) const;
    glm::vec3 velocityAt(const glm::vec3& p) const;

    float     phiAt(const glm::vec3& p) const { return sampleCell(phi, p); }
    glm::vec3 phiGradient(const glm::vec3& p) const;
};
