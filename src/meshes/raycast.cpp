#include "raycast.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace
{
    // Moller-Trumbore ray-triangle intersection; returns t or a negative number on a miss.
    float intersectTriangle(const Ray& ray, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c)
    {
        const glm::vec3 ab  = b - a;
        const glm::vec3 ac  = c - a;
        const glm::vec3 p   = glm::cross(ray.direction, ac);
        const float     det = glm::dot(ab, p);
        if (std::abs(det) < 1e-12f)
            return -1.0f;  // parallel to the triangle
        const float     inv = 1.0f / det;
        const glm::vec3 s   = ray.origin - a;
        const float     u   = glm::dot(s, p) * inv;
        if (u < 0.0f || u > 1.0f)
            return -1.0f;
        const glm::vec3 q = glm::cross(s, ab);
        const float     v = glm::dot(ray.direction, q) * inv;
        if (v < 0.0f || u + v > 1.0f)
            return -1.0f;
        return glm::dot(ac, q) * inv;
    }

    // Slab test against an axis-aligned box; true if the ray enters it at t >= 0.
    bool hitsBox(const Ray& ray, const glm::vec3& lo, const glm::vec3& hi)
    {
        float tNear = 0.0f;
        float tFar  = std::numeric_limits<float>::max();
        for (int a = 0; a < 3; ++a)
        {
            if (std::abs(ray.direction[a]) < 1e-12f)
            {
                if (ray.origin[a] < lo[a] || ray.origin[a] > hi[a])
                    return false;
                continue;
            }
            float t0 = (lo[a] - ray.origin[a]) / ray.direction[a];
            float t1 = (hi[a] - ray.origin[a]) / ray.direction[a];
            if (t0 > t1)
                std::swap(t0, t1);
            tNear = std::max(tNear, t0);
            tFar  = std::min(tFar, t1);
            if (tNear > tFar)
                return false;
        }
        return true;
    }
}

Ray screenRay(const glm::vec2& ndc, const glm::mat4& inverseViewProjection)
{
    glm::vec4 nearPoint = inverseViewProjection * glm::vec4(ndc, -1.0f, 1.0f);
    glm::vec4 farPoint  = inverseViewProjection * glm::vec4(ndc, 1.0f, 1.0f);
    nearPoint /= nearPoint.w;
    farPoint /= farPoint.w;
    return Ray{ glm::vec3(nearPoint), glm::vec3(farPoint - nearPoint) };
}

std::optional<float> raycast(const MeshData& mesh, const glm::mat4& model, const Ray& ray)
{
    if (mesh.indices.empty())
        return std::nullopt;

    // Intersect in the mesh's own space: t is the same there because the map is affine.
    const glm::mat4 inverse = glm::inverse(model);
    const Ray local{ glm::vec3(inverse * glm::vec4(ray.origin, 1.0f)),
                     glm::vec3(inverse * glm::vec4(ray.direction, 0.0f)) };

    glm::vec3 lo(std::numeric_limits<float>::max()), hi(std::numeric_limits<float>::lowest());
    for (const Vertex& v : mesh.vertices)
    {
        lo = glm::min(lo, v.position);
        hi = glm::max(hi, v.position);
    }
    if (!hitsBox(local, lo, hi))
        return std::nullopt;

    float nearest = std::numeric_limits<float>::max();
    for (std::size_t t = 0; t + 2 < mesh.indices.size(); t += 3)
    {
        const float hit = intersectTriangle(local, mesh.vertices[mesh.indices[t]].position,
                                            mesh.vertices[mesh.indices[t + 1]].position,
                                            mesh.vertices[mesh.indices[t + 2]].position);
        if (hit >= 0.0f)
            nearest = std::min(nearest, hit);
    }
    if (nearest == std::numeric_limits<float>::max())
        return std::nullopt;
    return nearest;
}
