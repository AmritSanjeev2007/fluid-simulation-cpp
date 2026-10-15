#pragma once

#include "meshes/shader_program.hpp"
#include "simulation_info.hpp"

#include <glad/gl.h>
#include <glm/glm.hpp>

#include <filesystem>
#include <vector>

// Draws the simulation's fields: a colour-mapped slice through the domain, a
// transparent volume of the whole domain or streamlines, and the domain's wireframe box. Field values live in 3D textures that are re-uploaded
// only when the FieldSnapshot's version changes.
class FieldRenderer
{
public:
    // Throws std::runtime_error if the shaders fail to load.
    FieldRenderer(const std::filesystem::path& sliceVertexShader, const std::filesystem::path& sliceFragmentShader,
                  const std::filesystem::path& volumeVertexShader, const std::filesystem::path& volumeFragmentShader,
                  const std::filesystem::path& streamlineVertexShader,
                  const std::filesystem::path& streamlineFragmentShader,
                  const std::filesystem::path& lineVertexShader, const std::filesystem::path& lineFragmentShader);
    ~FieldRenderer();

    FieldRenderer(const FieldRenderer&)            = delete;
    FieldRenderer& operator=(const FieldRenderer&) = delete;

    // Uploads the field and the streamlines if they changed since the last call.
    void update(const FieldSnapshot& field, const StreamlineSnapshot& streamlines);

    void drawSlice(const glm::mat4& viewProjection, const ViewSettings& view, bool diverging);

    // Ray-marches the whole domain with a colour ramp whose opacity goes from 0 at the low end
    // of the range to 0.5 at the high end, reached over the domain's longest side and scaled
    // by view.volumeDensity. Draw after the meshes: rays stop at the obstacle, and the result
    // is blended on top.
    void drawVolume(const glm::mat4& viewProjection, const ViewSettings& view, bool diverging);
    // Streamlines as 1-pixel lines coloured by the field, depth-tested against the meshes.
    void drawStreamlines(const glm::mat4& viewProjection, const ViewSettings& view, bool diverging);

    void drawDomain(const glm::mat4& viewProjection);

private:
    ShaderProgram m_sliceShader;
    ShaderProgram m_volumeShader;
    ShaderProgram m_streamlineShader;
    ShaderProgram m_lineShader;
    GLuint        m_vao          = 0;  // empty: vertices come from gl_VertexID
    GLuint        m_fieldTexture = 0;
    GLuint        m_solidTexture = 0;
    glm::ivec3    m_textureSize{ 0 };
    int           m_version = -1;

    GLuint             m_lineVao = 0;
    GLuint             m_lineVbo = 0;
    int                m_lineVersion = -1;
    std::vector<GLint>   m_lineFirst;
    std::vector<GLsizei> m_lineCount;
    Domain        m_domain;
};
