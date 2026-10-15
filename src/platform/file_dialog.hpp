#pragma once

#include <filesystem>
#include <optional>

struct GLFWwindow;

// True where openObjFileDialog() can show the system's file dialog (Windows). Elsewhere the
// GUI asks for a path instead.
bool hasNativeFileDialog();

// Shows the system's open-file dialog, listing only Wavefront .obj files, as a modal child of
// owner. Returns the chosen file, or nothing if the user cancelled or there is no native dialog.
std::optional<std::filesystem::path> openObjFileDialog(GLFWwindow* owner);
