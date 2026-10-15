#pragma once

#include <glad/gl.h>
#include <glm/glm.hpp>

#include <filesystem>
#include <string>
#include <unordered_map>

// A linked vertex + fragment shader program loaded from GLSL source files.
// Move-only; deletes the program on destruction.
class ShaderProgram
{
public:
    // Throws std::runtime_error with the compiler/linker log on failure.
    ShaderProgram(const std::filesystem::path& vertexPath, const std::filesystem::path& fragmentPath);
    ~ShaderProgram();

    ShaderProgram(const ShaderProgram&)            = delete;
    ShaderProgram& operator=(const ShaderProgram&) = delete;
    ShaderProgram(ShaderProgram&& other) noexcept;
    ShaderProgram& operator=(ShaderProgram&& other) noexcept;

    void use() const;

    // Setters require the program to be in use (call use() first).
    // Unknown uniform names are ignored, as OpenGL does for location -1.
    void setInt(const std::string& name, int value);
    void setFloat(const std::string& name, float value);
    void setVec2(const std::string& name, const glm::vec2& value);
    void setVec3(const std::string& name, const glm::vec3& value);
    void setMat3(const std::string& name, const glm::mat3& value);
    void setMat4(const std::string& name, const glm::mat4& value);

    GLuint id() const { return m_program; }

private:
    GLint uniformLocation(const std::string& name);

    GLuint m_program = 0;
    std::unordered_map<std::string, GLint> m_uniformLocations;
};
