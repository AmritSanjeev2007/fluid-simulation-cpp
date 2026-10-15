#include "mac_grid.hpp"

void MacGrid::allocate(const Domain& d)
{
    domain = d;
    const glm::ivec3 n = d.size;
    for (int axis = 0; axis < 3; ++axis)
    {
        glm::ivec3 faceCount = n;
        faceCount[axis] += 1;
        velocity[axis].resize(faceCount, 0.0f);
    }
    pressure.resize(n, 0.0f);
    temperature.resize(n, 0.0f);
    phi.resize(n, 0.0f);
    solid.resize(n, 0);
}

float MacGrid::sampleFace(int axis, const Grid3<float>& field, const glm::vec3& p) const
{
    glm::vec3 index = (p - domain.origin) / domain.h - 0.5f;
    index[axis] += 0.5f;
    return sampleTrilinear(field, index);
}

float MacGrid::sampleCell(const Grid3<float>& field, const glm::vec3& p) const
{
    return sampleTrilinear(field, (p - domain.origin) / domain.h - 0.5f);
}

glm::vec3 MacGrid::velocityAt(const glm::vec3& p) const
{
    return { sampleFace(0, velocity[0], p), sampleFace(1, velocity[1], p), sampleFace(2, velocity[2], p) };
}

glm::vec3 MacGrid::phiGradient(const glm::vec3& p) const
{
    const float h = domain.h;
    glm::vec3 g;
    for (int a = 0; a < 3; ++a)
    {
        glm::vec3 e(0.0f);
        e[a] = h;
        g[a] = (phiAt(p + e) - phiAt(p - e)) / (2.0f * h);
    }
    return g;
}
