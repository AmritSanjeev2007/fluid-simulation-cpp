#include "mesh.hpp"

#include <cstddef>
#include <utility>

Mesh::Mesh(const std::vector<Vertex>& vertices, const std::vector<std::uint32_t>& indices)
    : m_indexCount(static_cast<GLsizei>(indices.size()))
{
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glGenBuffers(1, &m_ebo);

    glBindVertexArray(m_vao);

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(Vertex)),
                 vertices.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(indices.size() * sizeof(std::uint32_t)),
                 indices.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<const void*>(offsetof(Vertex, position)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<const void*>(offsetof(Vertex, normal)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<const void*>(offsetof(Vertex, texCoord)));

    // The element buffer binding is stored in the VAO, so only unbind the VAO.
    glBindVertexArray(0);
}

Mesh::~Mesh()
{
    release();
}

Mesh::Mesh(Mesh&& other) noexcept
    : m_vao(std::exchange(other.m_vao, 0)),
      m_vbo(std::exchange(other.m_vbo, 0)),
      m_ebo(std::exchange(other.m_ebo, 0)),
      m_indexCount(std::exchange(other.m_indexCount, 0))
{
}

Mesh& Mesh::operator=(Mesh&& other) noexcept
{
    if (this != &other)
    {
        release();
        m_vao        = std::exchange(other.m_vao, 0);
        m_vbo        = std::exchange(other.m_vbo, 0);
        m_ebo        = std::exchange(other.m_ebo, 0);
        m_indexCount = std::exchange(other.m_indexCount, 0);
    }
    return *this;
}

void Mesh::draw() const
{
    glBindVertexArray(m_vao);
    glDrawElements(GL_TRIANGLES, m_indexCount, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

void Mesh::release()
{
    // glDelete* silently ignores 0, so moved-from meshes are safe to destroy.
    glDeleteBuffers(1, &m_ebo);
    glDeleteBuffers(1, &m_vbo);
    glDeleteVertexArrays(1, &m_vao);
    m_vao = m_vbo = m_ebo = 0;
    m_indexCount = 0;
}
