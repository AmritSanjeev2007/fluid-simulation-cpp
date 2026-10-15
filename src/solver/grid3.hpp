#pragma once

#include <glm/glm.hpp>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <vector>

// Dense 3D array stored flat with x fastest: index = i + nx * (j + ny * k).
template <typename T>
class Grid3
{
public:
    Grid3() = default;
    Grid3(glm::ivec3 size, T value = T{}) { resize(size, value); }

    void resize(glm::ivec3 size, T value = T{})
    {
        m_size = size;
        m_data.assign(static_cast<std::size_t>(size.x) * size.y * size.z, value);
    }

    void fill(T value) { std::fill(m_data.begin(), m_data.end(), value); }

    glm::ivec3  size() const { return m_size; }
    int         nx() const { return m_size.x; }
    int         ny() const { return m_size.y; }
    int         nz() const { return m_size.z; }
    std::size_t count() const { return m_data.size(); }

    bool inRange(int i, int j, int k) const
    {
        return i >= 0 && j >= 0 && k >= 0 && i < m_size.x && j < m_size.y && k < m_size.z;
    }

    std::size_t index(int i, int j, int k) const
    {
        assert(inRange(i, j, k));
        return static_cast<std::size_t>(i) + static_cast<std::size_t>(m_size.x) * (j + static_cast<std::size_t>(m_size.y) * k);
    }

    T&       operator()(int i, int j, int k) { return m_data[index(i, j, k)]; }
    const T& operator()(int i, int j, int k) const { return m_data[index(i, j, k)]; }

    T&       operator[](std::size_t n) { return m_data[n]; }
    const T& operator[](std::size_t n) const { return m_data[n]; }

    std::vector<T>&       values() { return m_data; }
    const std::vector<T>& values() const { return m_data; }

    void swap(Grid3& other) noexcept
    {
        std::swap(m_size, other.m_size);
        m_data.swap(other.m_data);
    }

private:
    glm::ivec3     m_size{ 0 };
    std::vector<T> m_data;
};

// Trilinear interpolation at fractional index coordinates; positions outside the
// grid are clamped to its edge values.
inline float sampleTrilinear(const Grid3<float>& grid, glm::vec3 p)
{
    const glm::ivec3 n = grid.size();
    int   i0[3];
    float t[3];
    for (int a = 0; a < 3; ++a)
    {
        if (n[a] < 2)
        {
            i0[a] = 0;
            t[a]  = 0.0f;
            continue;
        }
        const float c = std::clamp(p[a], 0.0f, static_cast<float>(n[a] - 1));
        i0[a] = std::min(static_cast<int>(std::floor(c)), n[a] - 2);
        t[a]  = c - static_cast<float>(i0[a]);
    }
    const int i1 = n.x < 2 ? i0[0] : i0[0] + 1;
    const int j1 = n.y < 2 ? i0[1] : i0[1] + 1;
    const int k1 = n.z < 2 ? i0[2] : i0[2] + 1;

    auto lerp = [](float a, float b, float s) { return a + (b - a) * s; };
    const float c00 = lerp(grid(i0[0], i0[1], i0[2]), grid(i1, i0[1], i0[2]), t[0]);
    const float c10 = lerp(grid(i0[0], j1, i0[2]), grid(i1, j1, i0[2]), t[0]);
    const float c01 = lerp(grid(i0[0], i0[1], k1), grid(i1, i0[1], k1), t[0]);
    const float c11 = lerp(grid(i0[0], j1, k1), grid(i1, j1, k1), t[0]);
    return lerp(lerp(c00, c10, t[1]), lerp(c01, c11, t[1]), t[2]);
}
