#include "field_renderer.hpp"

#include "constants.hpp"

#include <algorithm>

namespace
{
    GLuint createTexture3D()
    {
        GLuint texture = 0;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_3D, texture);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_3D, 0);
        return texture;
    }

    // Premultiplied-alpha blending without depth writes, for passes drawn below full opacity.
    void beginTranslucent(float opacity)
    {
        if (opacity >= 1.0f)
            return;
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
    }

    void endTranslucent()
    {
        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
    }
}

FieldRenderer::FieldRenderer(const std::filesystem::path& sliceVertexShader,
                             const std::filesystem::path& sliceFragmentShader,
                             const std::filesystem::path& volumeVertexShader,
                             const std::filesystem::path& volumeFragmentShader,
                             const std::filesystem::path& streamlineVertexShader,
                             const std::filesystem::path& streamlineFragmentShader,
                             const std::filesystem::path& lineVertexShader,
                             const std::filesystem::path& lineFragmentShader)
    : m_sliceShader(sliceVertexShader, sliceFragmentShader),
      m_volumeShader(volumeVertexShader, volumeFragmentShader),
      m_streamlineShader(streamlineVertexShader, streamlineFragmentShader),
      m_lineShader(lineVertexShader, lineFragmentShader)
{
    glGenVertexArrays(1, &m_vao);

    glGenVertexArrays(1, &m_lineVao);
    glGenBuffers(1, &m_lineVbo);
    glBindVertexArray(m_lineVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_lineVbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(glm::vec4), nullptr);
    glBindVertexArray(0);
    m_fieldTexture = createTexture3D();
    m_solidTexture = createTexture3D();

    m_sliceShader.use();
    m_sliceShader.setInt("uField", 0);
    m_sliceShader.setInt("uSolid", 1);

    m_volumeShader.use();
    m_volumeShader.setInt("uField", 0);
    m_volumeShader.setInt("uSolid", 1);

    m_lineShader.use();
    m_lineShader.setVec3("uColor", constants::colors::domainBox);
}

FieldRenderer::~FieldRenderer()
{
    glDeleteTextures(1, &m_fieldTexture);
    glDeleteTextures(1, &m_solidTexture);
    glDeleteVertexArrays(1, &m_vao);
    glDeleteBuffers(1, &m_lineVbo);
    glDeleteVertexArrays(1, &m_lineVao);
}

void FieldRenderer::update(const FieldSnapshot& field, const StreamlineSnapshot& streamlines)
{
    if (streamlines.version != m_lineVersion)
    {
        m_lineVersion = streamlines.version;
        const Streamlines& lines = streamlines.lines;
        glBindBuffer(GL_ARRAY_BUFFER, m_lineVbo);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(lines.points.size() * sizeof(glm::vec4)),
                     lines.points.data(), GL_DYNAMIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        m_lineFirst.assign(lines.first.begin(), lines.first.end());
        m_lineCount.assign(lines.count.begin(), lines.count.end());
    }

    if (field.version == m_version)
        return;
    m_version = field.version;
    m_domain  = field.domain;

    const glm::ivec3 n = field.domain.size;
    if (field.values.size() != static_cast<std::size_t>(n.x) * n.y * n.z || field.solid.size() != field.values.size())
        return;

    // Rows of the 8-bit mask are not 4-byte aligned in general.
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    const bool resized = n != m_textureSize;
    glBindTexture(GL_TEXTURE_3D, m_fieldTexture);
    if (resized)
        glTexImage3D(GL_TEXTURE_3D, 0, GL_R32F, n.x, n.y, n.z, 0, GL_RED, GL_FLOAT, field.values.data());
    else
        glTexSubImage3D(GL_TEXTURE_3D, 0, 0, 0, 0, n.x, n.y, n.z, GL_RED, GL_FLOAT, field.values.data());

    glBindTexture(GL_TEXTURE_3D, m_solidTexture);
    if (resized)
        glTexImage3D(GL_TEXTURE_3D, 0, GL_R8, n.x, n.y, n.z, 0, GL_RED, GL_UNSIGNED_BYTE, field.solid.data());
    else
        glTexSubImage3D(GL_TEXTURE_3D, 0, 0, 0, 0, n.x, n.y, n.z, GL_RED, GL_UNSIGNED_BYTE, field.solid.data());
    glBindTexture(GL_TEXTURE_3D, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

    m_textureSize = n;
}

void FieldRenderer::drawSlice(const glm::mat4& viewProjection, const ViewSettings& view, bool diverging)
{
    if (m_textureSize == glm::ivec3(0))
        return;

    // The slice is seen from both sides. Below full opacity it is blended over the scene
    // and leaves the depth buffer alone, so what's behind it stays visible.
    glDisable(GL_CULL_FACE);
    beginTranslucent(view.opacity);

    m_sliceShader.use();
    m_sliceShader.setMat4("uViewProjection", viewProjection);
    m_sliceShader.setVec3("uDomainOrigin", m_domain.origin);
    m_sliceShader.setVec3("uDomainExtent", m_domain.extent());
    m_sliceShader.setInt("uSliceAxis", view.sliceAxis);
    m_sliceShader.setFloat("uSlicePosition", view.slicePosition);
    m_sliceShader.setVec2("uRange", view.range);
    m_sliceShader.setInt("uDiverging", diverging ? 1 : 0);
    m_sliceShader.setFloat("uOpacity", view.opacity);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_3D, m_fieldTexture);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_3D, m_solidTexture);

    glBindVertexArray(m_vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);

    glBindTexture(GL_TEXTURE_3D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_3D, 0);
    endTranslucent();
    glEnable(GL_CULL_FACE);
}

void FieldRenderer::drawVolume(const glm::mat4& viewProjection, const ViewSettings& view, bool diverging)
{
    if (m_textureSize == glm::ivec3(0))
        return;

    // One full-screen pass, blended over the scene. Occlusion by the mesh comes from the
    // rays stopping at solid cells, so the depth test is off.
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    m_volumeShader.use();
    m_volumeShader.setMat4("uInverseViewProjection", glm::inverse(viewProjection));
    m_volumeShader.setVec3("uDomainOrigin", m_domain.origin);
    m_volumeShader.setVec3("uDomainExtent", m_domain.extent());
    m_volumeShader.setFloat("uCellSize", m_domain.h);
    const glm::vec3 extent = m_domain.extent();
    m_volumeShader.setFloat("uReferenceLength", std::max({ extent.x, extent.y, extent.z }));
    m_volumeShader.setVec2("uRange", view.range);
    m_volumeShader.setInt("uDiverging", diverging ? 1 : 0);
    m_volumeShader.setFloat("uDensity", view.volumeDensity);
    m_volumeShader.setFloat("uOpacity", view.opacity);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_3D, m_fieldTexture);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_3D, m_solidTexture);

    glBindVertexArray(m_vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);

    glBindTexture(GL_TEXTURE_3D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_3D, 0);
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
}

void FieldRenderer::drawStreamlines(const glm::mat4& viewProjection, const ViewSettings& view, bool diverging)
{
    if (m_lineFirst.empty())
        return;

    m_streamlineShader.use();
    m_streamlineShader.setMat4("uViewProjection", viewProjection);
    m_streamlineShader.setVec2("uRange", view.range);
    m_streamlineShader.setInt("uDiverging", diverging ? 1 : 0);
    m_streamlineShader.setFloat("uOpacity", view.opacity);

    beginTranslucent(view.opacity);
    glBindVertexArray(m_lineVao);
    glMultiDrawArrays(GL_LINE_STRIP, m_lineFirst.data(), m_lineCount.data(), static_cast<GLsizei>(m_lineFirst.size()));
    glBindVertexArray(0);
    endTranslucent();
}

void FieldRenderer::drawDomain(const glm::mat4& viewProjection)
{
    if (m_textureSize == glm::ivec3(0))
        return;

    m_lineShader.use();
    m_lineShader.setMat4("uViewProjection", viewProjection);
    m_lineShader.setVec3("uBoxMin", m_domain.origin);
    m_lineShader.setVec3("uBoxMax", m_domain.origin + m_domain.extent());

    glBindVertexArray(m_vao);
    glDrawArrays(GL_LINES, 0, 24);
    glBindVertexArray(0);
}
