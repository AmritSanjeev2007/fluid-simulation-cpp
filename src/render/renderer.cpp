#include "renderer.hpp"

#include "camera/camera.hpp"
#include "constants.hpp"
#include "meshes/mesh.hpp"

#include <glad/gl.h>

#include <cmath>

Renderer::Renderer(const std::filesystem::path& meshVertexShader, const std::filesystem::path& meshFragmentShader,
                   const std::filesystem::path& screenVertexShader, const std::filesystem::path& screenFragmentShader)
    : m_meshShader(meshVertexShader, meshFragmentShader),
      m_screenShader(screenVertexShader, screenFragmentShader)
{
    // Lighting is constant for now, so upload it once.
    m_meshShader.use();
    m_meshShader.setVec3("uColor", constants::colors::mesh);
    m_meshShader.setVec3("uLightDirection", glm::normalize(constants::lighting::direction));
    m_meshShader.setVec3("uLightColor", constants::lighting::color);
    m_meshShader.setFloat("uAmbient", constants::lighting::ambient);
    m_meshShader.setVec3("uOutlineColor", constants::colors::hoverOutline);

    m_screenShader.use();
    m_screenShader.setVec3("uAxisColors[0]", constants::colors::axisX);
    m_screenShader.setVec3("uAxisColors[1]", constants::colors::axisY);
    m_screenShader.setVec3("uAxisColors[2]", constants::colors::axisZ);
}

void Renderer::beginFrame(int framebufferWidth, int framebufferHeight, const glm::vec3& clearColor,
                          const Camera& camera)
{
    glViewport(0, 0, framebufferWidth, framebufferHeight);
    glClearColor(clearColor.r, clearColor.g, clearColor.b, 1.0f);
    glStencilMask(0xFF);  // glClear only clears the bits the mask lets through
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    // ImGui's renderer changes these, so restore them every frame.
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glDisable(GL_BLEND);

    m_framebufferSize = { static_cast<float>(framebufferWidth), static_cast<float>(framebufferHeight) };

    const float aspect = static_cast<float>(framebufferWidth) / static_cast<float>(framebufferHeight);
    m_viewProjection = camera.viewProjection(aspect);
}

void Renderer::setMeshMatrices(const glm::mat4& model)
{
    m_meshShader.use();
    m_meshShader.setMat4("uModel", model);
    m_meshShader.setMat4("uViewProjection", m_viewProjection);
    m_meshShader.setMat3("uNormalMatrix", glm::transpose(glm::inverse(glm::mat3(model))));
}

void Renderer::draw(const Mesh& mesh, const glm::mat4& model)
{
    setMeshMatrices(model);
    mesh.draw();
}

void Renderer::drawOutline(const Mesh& mesh, const glm::mat4& model, float dpiScale)
{
    setMeshMatrices(model);

    // Both faces count, so open meshes seen from behind get an outline too.
    glDisable(GL_CULL_FACE);
    glDepthMask(GL_FALSE);
    glEnable(GL_STENCIL_TEST);

    // 1. Mark every pixel the mesh covers, hidden or not, in the stencil buffer.
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glStencilMask(0xFF);
    glStencilFunc(GL_ALWAYS, 1, 0xFF);
    glStencilOp(GL_KEEP, GL_REPLACE, GL_REPLACE);
    mesh.draw();

    // 2. Draw flat-coloured copies shifted a few pixels in eight directions, only outside the
    //    mark. Together they form a band of even width around the silhouette, with no gaps at
    //    sharp corners (unlike pushing vertices out along their normals).
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glStencilMask(0x00);
    glStencilFunc(GL_NOTEQUAL, 1, 0xFF);
    m_meshShader.setInt("uOutline", 1);
    const glm::vec2 pixel = 2.0f / glm::max(m_framebufferSize, glm::vec2(1.0f));  // one pixel in NDC
    const float     width = constants::outline::width * dpiScale;
    for (int d = 0; d < 8; ++d)
    {
        const float     angle = glm::radians(45.0f * static_cast<float>(d));
        const glm::vec2 shift = glm::vec2(std::cos(angle), std::sin(angle)) * width * pixel;
        m_meshShader.setVec2("uScreenOffset", shift);
        mesh.draw();
    }

    m_meshShader.setInt("uOutline", 0);
    m_meshShader.setVec2("uScreenOffset", glm::vec2(0.0f));
    glStencilMask(0xFF);
    glDisable(GL_STENCIL_TEST);
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
}

void Renderer::drawOverlay(const Camera& camera, float dpiScale)
{
    // Always on top of the scene; the fragment shader outputs premultiplied alpha.
    // beginFrame() restores the depth test, and ImGui sets its own blending.
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    m_screenShader.use();
    m_screenShader.setVec2("uResolution", m_framebufferSize);
    m_screenShader.setMat3("uViewRotation", glm::mat3(camera.view()));
    m_screenShader.setFloat("uGizmoRadius", constants::gizmo::radius * dpiScale);
    m_screenShader.setFloat("uGizmoMargin", constants::gizmo::margin * dpiScale);
    m_screenQuad.draw();

    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}
