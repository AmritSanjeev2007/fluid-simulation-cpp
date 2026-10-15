#include "sdf.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace
{
    constexpr int bandCells = 3;

    // Closest point on triangle abc to p (Ericson, "Real-Time Collision Detection", 5.1.5).
    glm::vec3 closestPointOnTriangle(const glm::vec3& p, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c)
    {
        const glm::vec3 ab = b - a, ac = c - a, ap = p - a;
        const float d1 = glm::dot(ab, ap), d2 = glm::dot(ac, ap);
        if (d1 <= 0.0f && d2 <= 0.0f)
            return a;

        const glm::vec3 bp = p - b;
        const float d3 = glm::dot(ab, bp), d4 = glm::dot(ac, bp);
        if (d3 >= 0.0f && d4 <= d3)
            return b;

        const float vc = d1 * d4 - d3 * d2;
        if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f)
            return a + ab * (d1 / (d1 - d3));

        const glm::vec3 cp = p - c;
        const float d5 = glm::dot(ab, cp), d6 = glm::dot(ac, cp);
        if (d6 >= 0.0f && d5 <= d6)
            return c;

        const float vb = d5 * d2 - d1 * d6;
        if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f)
            return a + ac * (d2 / (d2 - d6));

        const float va = d3 * d6 - d5 * d4;
        if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f)
            return b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));

        const float denom = 1.0f / (va + vb + vc);
        return a + ab * (vb * denom) + ac * (vc * denom);
    }

    // If the line {(x, y, z) : x free} crosses triangle abc, returns true and the x
    // of the crossing. Only the triangle's projection onto the yz-plane matters.
    bool rayCrossingX(double y, double z, const glm::dvec3& a, const glm::dvec3& b, const glm::dvec3& c, double& x)
    {
        // Signed areas of the yz-projected sub-triangles (barycentric weights).
        const double wa = (b.y - y) * (c.z - z) - (b.z - z) * (c.y - y);
        const double wb = (c.y - y) * (a.z - z) - (c.z - z) * (a.y - y);
        const double wc = (a.y - y) * (b.z - z) - (a.z - z) * (b.y - y);
        if ((wa < 0.0 || wb < 0.0 || wc < 0.0) && (wa > 0.0 || wb > 0.0 || wc > 0.0))
            return false;
        const double sum = wa + wb + wc;
        if (sum == 0.0)
            return false;  // triangle seen edge-on
        x = (wa * a.x + wb * b.x + wc * c.x) / sum;
        return true;
    }
}

void meshBounds(const MeshData& mesh, const glm::mat4& model, glm::vec3& lo, glm::vec3& hi)
{
    lo = glm::vec3(std::numeric_limits<float>::max());
    hi = glm::vec3(std::numeric_limits<float>::lowest());
    for (const Vertex& v : mesh.vertices)
    {
        const glm::vec3 p = glm::vec3(model * glm::vec4(v.position, 1.0f));
        lo = glm::min(lo, p);
        hi = glm::max(hi, p);
    }
}

void buildSdf(const MeshData& mesh, const glm::mat4& model, const Domain& domain, Grid3<float>& phi)
{
    const glm::ivec3 n = domain.size;
    const float      h = domain.h;
    const float      far = (bandCells + 1) * h;
    phi.resize(n, std::numeric_limits<float>::max());

    std::vector<glm::vec3> world(mesh.vertices.size());
    for (std::size_t v = 0; v < world.size(); ++v)
        world[v] = glm::vec3(model * glm::vec4(mesh.vertices[v].position, 1.0f));

    const std::size_t triangleCount = mesh.indices.size() / 3;

    // Unsigned distance in a narrow band around each triangle.
    for (std::size_t t = 0; t < triangleCount; ++t)
    {
        const glm::vec3& a = world[mesh.indices[3 * t + 0]];
        const glm::vec3& b = world[mesh.indices[3 * t + 1]];
        const glm::vec3& c = world[mesh.indices[3 * t + 2]];
        const glm::vec3 lo = (glm::min(a, glm::min(b, c)) - domain.origin) / h - 0.5f;
        const glm::vec3 hi = (glm::max(a, glm::max(b, c)) - domain.origin) / h - 0.5f;
        const glm::ivec3 i0 = glm::max(glm::ivec3(glm::floor(lo)) - bandCells, glm::ivec3(0));
        const glm::ivec3 i1 = glm::min(glm::ivec3(glm::ceil(hi)) + bandCells, n - 1);
        for (int k = i0.z; k <= i1.z; ++k)
            for (int j = i0.y; j <= i1.y; ++j)
                for (int i = i0.x; i <= i1.x; ++i)
                {
                    const glm::vec3 p = domain.cellCenter(i, j, k);
                    const float     d = glm::length(p - closestPointOnTriangle(p, a, b, c));
                    float&          cell = phi(i, j, k);
                    cell = std::min(cell, d);
                }
    }
    for (float& d : phi.values())
        d = std::min(d, far);

    // Sign: count crossings of a ray along +x through each row of cell centres. The
    // ray is nudged by an irrational fraction of a cell so it never passes exactly
    // through a mesh vertex or edge at grid-aligned coordinates.
    std::vector<std::vector<double>> crossings(static_cast<std::size_t>(n.y) * n.z);
    const double jitterY = 1.0e-4 * 0.5772156649 * h;
    const double jitterZ = 1.0e-4 * 0.3183098862 * h;
    for (std::size_t t = 0; t < triangleCount; ++t)
    {
        const glm::dvec3 a = world[mesh.indices[3 * t + 0]];
        const glm::dvec3 b = world[mesh.indices[3 * t + 1]];
        const glm::dvec3 c = world[mesh.indices[3 * t + 2]];
        const double yLo = std::min({ a.y, b.y, c.y }), yHi = std::max({ a.y, b.y, c.y });
        const double zLo = std::min({ a.z, b.z, c.z }), zHi = std::max({ a.z, b.z, c.z });
        const int j0 = std::max(0, static_cast<int>(std::floor((yLo - domain.origin.y) / h - 0.5)));
        const int j1 = std::min(n.y - 1, static_cast<int>(std::ceil((yHi - domain.origin.y) / h - 0.5)));
        const int k0 = std::max(0, static_cast<int>(std::floor((zLo - domain.origin.z) / h - 0.5)));
        const int k1 = std::min(n.z - 1, static_cast<int>(std::ceil((zHi - domain.origin.z) / h - 0.5)));
        for (int k = k0; k <= k1; ++k)
            for (int j = j0; j <= j1; ++j)
            {
                const double y = domain.origin.y + (j + 0.5) * h + jitterY;
                const double z = domain.origin.z + (k + 0.5) * h + jitterZ;
                double x;
                if (rayCrossingX(y, z, a, b, c, x))
                    crossings[j + static_cast<std::size_t>(n.y) * k].push_back(x);
            }
    }

    for (int k = 0; k < n.z; ++k)
        for (int j = 0; j < n.y; ++j)
        {
            std::vector<double>& xs = crossings[j + static_cast<std::size_t>(n.y) * k];
            std::sort(xs.begin(), xs.end());
            std::size_t passed = 0;
            for (int i = 0; i < n.x; ++i)
            {
                const double x = domain.origin.x + (i + 0.5) * h;
                while (passed < xs.size() && xs[passed] < x)
                    ++passed;
                if (passed % 2 == 1)
                    phi(i, j, k) = -phi(i, j, k);
            }
        }
}
