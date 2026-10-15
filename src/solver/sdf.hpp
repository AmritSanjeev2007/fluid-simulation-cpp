#pragma once

#include "grid3.hpp"
#include "mac_grid.hpp"
#include "meshes/mesh.hpp"

#include <glm/glm.hpp>

// Signed distance from each cell centre of the domain to a closed triangle mesh,
// negative inside. Distances are exact within a few cells of the surface and
// clamped to +-(band + 1) * h further away; the sign comes from ray-crossing parity
// along +x, so the mesh must be closed (watertight).
void buildSdf(const MeshData& mesh, const glm::mat4& model, const Domain& domain, Grid3<float>& phi);

// World-space bounding box of a mesh under a model matrix.
void meshBounds(const MeshData& mesh, const glm::mat4& model, glm::vec3& lo, glm::vec3& hi);
