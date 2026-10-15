#pragma once

#include "mesh.hpp"

#include <glm/glm.hpp>

#include <optional>

// A half-line in world space: origin + t * direction for t >= 0.
struct Ray
{
    glm::vec3 origin{ 0.0f };
    glm::vec3 direction{ 0.0f, 0.0f, -1.0f };  // need not be normalised
};

// The ray through a point on the screen. ndc is in normalised device coordinates
// (-1..1, +y up) and inverseViewProjection undoes the camera's view-projection matrix.
Ray screenRay(const glm::vec2& ndc, const glm::mat4& inverseViewProjection);

// Distance along the ray (in units of ray.direction) to the nearest triangle of the mesh
// under the given model matrix, or nothing if the ray misses. Both faces of a triangle count.
std::optional<float> raycast(const MeshData& mesh, const glm::mat4& model, const Ray& ray);
