#pragma once

#include <glad/gl.h>
#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

// Interleaved vertex layout uploaded to the GPU.
// Attribute locations: 0 = position, 1 = normal, 2 = texCoord.
struct Vertex
{
    glm::vec3 position{ 0.0f };
    glm::vec3 normal{ 0.0f };
    glm::vec2 texCoord{ 0.0f };
};

// CPU-side triangle mesh: what loaders produce and what Mesh uploads.
struct MeshData
{
    std::vector<Vertex>        vertices;
    std::vector<std::uint32_t> indices;  // three per triangle
};

// GPU triangle mesh (VAO + vertex/index buffers). Move-only; frees its buffers on destruction.
class Mesh
{
public:
    Mesh(const std::vector<Vertex>& vertices, const std::vector<std::uint32_t>& indices);
    explicit Mesh(const MeshData& data) : Mesh(data.vertices, data.indices) {}
    ~Mesh();

    Mesh(const Mesh&)            = delete;
    Mesh& operator=(const Mesh&) = delete;
    Mesh(Mesh&& other) noexcept;
    Mesh& operator=(Mesh&& other) noexcept;

    void draw() const;

    GLsizei indexCount() const { return m_indexCount; }

private:
    void release();

    GLuint  m_vao        = 0;
    GLuint  m_vbo        = 0;
    GLuint  m_ebo        = 0;
    GLsizei m_indexCount = 0;
};
