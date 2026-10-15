#include "screen_quad.hpp"

#include <utility>

ScreenQuad::ScreenQuad()
{
    // Two triangles as a strip, counter-clockwise so back-face culling keeps them.
    constexpr float corners[] = {
        -1.0f, -1.0f,
         1.0f, -1.0f,
        -1.0f,  1.0f,
         1.0f,  1.0f,
    };

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(corners), corners, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);

    glBindVertexArray(0);
}

ScreenQuad::~ScreenQuad()
{
    release();
}

ScreenQuad::ScreenQuad(ScreenQuad&& other) noexcept
    : m_vao(std::exchange(other.m_vao, 0)),
      m_vbo(std::exchange(other.m_vbo, 0))
{
}

ScreenQuad& ScreenQuad::operator=(ScreenQuad&& other) noexcept
{
    if (this != &other)
    {
        release();
        m_vao = std::exchange(other.m_vao, 0);
        m_vbo = std::exchange(other.m_vbo, 0);
    }
    return *this;
}

void ScreenQuad::draw() const
{
    glBindVertexArray(m_vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
}

void ScreenQuad::release()
{
    // glDelete* silently ignores 0, so moved-from quads are safe to destroy.
    glDeleteBuffers(1, &m_vbo);
    glDeleteVertexArrays(1, &m_vao);
    m_vao = m_vbo = 0;
}
