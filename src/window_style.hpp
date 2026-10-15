#pragma once

#include <glm/glm.hpp>

#include <filesystem>

struct GLFWwindow;

// Rasterises an SVG at several icon sizes and sets it as the window icon.
// Returns false (and leaves the default icon) if the file can't be loaded.
bool setWindowIconFromSvg(GLFWwindow* window, const std::filesystem::path& svgPath);

// Colours the native title bar. Only supported on Windows 11+; a no-op elsewhere.
void setTitleBarColor(GLFWwindow* window, const glm::vec3& background, const glm::vec3& text);
