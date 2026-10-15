#pragma once

#include "meshes/shader_program.hpp"
#include "render/screen_quad.hpp"

#include <glm/glm.hpp>

#include <filesystem>

class Camera;
class Mesh;

// Draws meshes as solid surfaces lit by a single directional light, plus a
// full-screen overlay pass (the world axes gizmo) on top of them.
class Renderer
{
public:
    // Throws std::runtime_error if the shaders fail to load.
    Renderer(const std::filesystem::path& meshVertexShader, const std::filesystem::path& meshFragmentShader,
             const std::filesystem::path& screenVertexShader, const std::filesystem::path& screenFragmentShader);

    // Clears the framebuffer and takes this frame's view-projection matrix from the camera.
    void beginFrame(int framebufferWidth, int framebufferHeight, const glm::vec3& clearColor,
                    const Camera& camera);

    void draw(const Mesh& mesh, const glm::mat4& model);

    // Draws an outline around a mesh already drawn this frame (the one under the mouse).
    // dpiScale scales its width. Uses the stencil buffer.
    void drawOutline(const Mesh& mesh, const glm::mat4& model, float dpiScale);

    // This frame's camera matrix, for other passes drawing into the 3D view.
    const glm::mat4& viewProjection() const { return m_viewProjection; }

    // Draws the world axes gizmo over everything drawn so far. Call after all meshes and
    // before the UI, so the UI stays on top. dpiScale scales the gizmo's size.
    void drawOverlay(const Camera& camera, float dpiScale);

private:
    void setMeshMatrices(const glm::mat4& model);

    ShaderProgram m_meshShader;
    ShaderProgram m_screenShader;
    ScreenQuad    m_screenQuad;
    glm::mat4     m_viewProjection{ 1.0f };
    glm::vec2     m_framebufferSize{ 0.0f };
};
