#include "shader_program.hpp"

#include <glm/gtc/type_ptr.hpp>

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace
{
    std::string readFile(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file)
            throw std::runtime_error("Could not open shader file: " + path.string());
        std::ostringstream contents;
        contents << file.rdbuf();
        return contents.str();
    }

    GLuint compileShader(GLenum type, const std::filesystem::path& path)
    {
        const std::string source = readFile(path);
        const char* sourcePtr = source.c_str();

        GLuint shader = glCreateShader(type);
        glShaderSource(shader, 1, &sourcePtr, nullptr);
        glCompileShader(shader);

        GLint success = GL_FALSE;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
        if (!success)
        {
            GLint logLength = 0;
            glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
            std::string log(static_cast<std::size_t>(logLength), '\0');
            glGetShaderInfoLog(shader, logLength, nullptr, log.data());
            glDeleteShader(shader);
            throw std::runtime_error("Failed to compile " + path.string() + ":\n" + log);
        }
        return shader;
    }
}

ShaderProgram::ShaderProgram(const std::filesystem::path& vertexPath, const std::filesystem::path& fragmentPath)
{
    const GLuint vertex = compileShader(GL_VERTEX_SHADER, vertexPath);
    GLuint fragment = 0;
    try
    {
        fragment = compileShader(GL_FRAGMENT_SHADER, fragmentPath);
    }
    catch (...)
    {
        glDeleteShader(vertex);
        throw;
    }

    m_program = glCreateProgram();
    glAttachShader(m_program, vertex);
    glAttachShader(m_program, fragment);
    glLinkProgram(m_program);

    // The program keeps the compiled code; the shader objects are no longer needed.
    glDetachShader(m_program, vertex);
    glDetachShader(m_program, fragment);
    glDeleteShader(vertex);
    glDeleteShader(fragment);

    GLint success = GL_FALSE;
    glGetProgramiv(m_program, GL_LINK_STATUS, &success);
    if (!success)
    {
        GLint logLength = 0;
        glGetProgramiv(m_program, GL_INFO_LOG_LENGTH, &logLength);
        std::string log(static_cast<std::size_t>(logLength), '\0');
        glGetProgramInfoLog(m_program, logLength, nullptr, log.data());
        glDeleteProgram(m_program);
        throw std::runtime_error("Failed to link " + vertexPath.string() + " + " + fragmentPath.string() + ":\n" + log);
    }
}

ShaderProgram::~ShaderProgram()
{
    glDeleteProgram(m_program);
}

ShaderProgram::ShaderProgram(ShaderProgram&& other) noexcept
    : m_program(std::exchange(other.m_program, 0)),
      m_uniformLocations(std::move(other.m_uniformLocations))
{
}

ShaderProgram& ShaderProgram::operator=(ShaderProgram&& other) noexcept
{
    if (this != &other)
    {
        glDeleteProgram(m_program);
        m_program          = std::exchange(other.m_program, 0);
        m_uniformLocations = std::move(other.m_uniformLocations);
    }
    return *this;
}

void ShaderProgram::use() const
{
    glUseProgram(m_program);
}

void ShaderProgram::setInt(const std::string& name, int value)
{
    glUniform1i(uniformLocation(name), value);
}

void ShaderProgram::setFloat(const std::string& name, float value)
{
    glUniform1f(uniformLocation(name), value);
}

void ShaderProgram::setVec2(const std::string& name, const glm::vec2& value)
{
    glUniform2fv(uniformLocation(name), 1, glm::value_ptr(value));
}

void ShaderProgram::setVec3(const std::string& name, const glm::vec3& value)
{
    glUniform3fv(uniformLocation(name), 1, glm::value_ptr(value));
}

void ShaderProgram::setMat3(const std::string& name, const glm::mat3& value)
{
    glUniformMatrix3fv(uniformLocation(name), 1, GL_FALSE, glm::value_ptr(value));
}

void ShaderProgram::setMat4(const std::string& name, const glm::mat4& value)
{
    glUniformMatrix4fv(uniformLocation(name), 1, GL_FALSE, glm::value_ptr(value));
}

GLint ShaderProgram::uniformLocation(const std::string& name)
{
    auto it = m_uniformLocations.find(name);
    if (it == m_uniformLocations.end())
        it = m_uniformLocations.emplace(name, glGetUniformLocation(m_program, name.c_str())).first;
    return it->second;
}
