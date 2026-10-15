#pragma once

#include <glad/gl.h>

// A quad covering the whole screen, for full-screen passes (e.g. overlays drawn in the fragment shader).
// Vertices are in normalised device coordinates at attribute location 0. Move-only; frees its buffers on destruction.
class ScreenQuad
{
public:
    ScreenQuad();
    ~ScreenQuad();

    ScreenQuad(const ScreenQuad&)            = delete;
    ScreenQuad& operator=(const ScreenQuad&) = delete;
    ScreenQuad(ScreenQuad&& other) noexcept;
    ScreenQuad& operator=(ScreenQuad&& other) noexcept;

    void draw() const;

private:
    void release();

    GLuint m_vao = 0;
    GLuint m_vbo = 0;
};
