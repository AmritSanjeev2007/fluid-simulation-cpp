#include "file_dialog.hpp"

#include <GLFW/glfw3.h>

#include <iterator>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#endif

bool hasNativeFileDialog()
{
#ifdef _WIN32
    return true;
#else
    return false;
#endif
}

std::optional<std::filesystem::path> openObjFileDialog(GLFWwindow* owner)
{
#ifdef _WIN32
    wchar_t file[MAX_PATH * 4] = L"";

    OPENFILENAMEW dialog{};
    dialog.lStructSize  = sizeof(dialog);
    dialog.hwndOwner    = owner ? glfwGetWin32Window(owner) : nullptr;
    dialog.lpstrFilter  = L"Wavefront OBJ (*.obj)\0*.obj\0";
    dialog.nFilterIndex = 1;
    dialog.lpstrFile    = file;
    dialog.nMaxFile     = static_cast<DWORD>(std::size(file));
    dialog.lpstrTitle   = L"Upload mesh";
    dialog.lpstrDefExt  = L"obj";
    // NOCHANGEDIR: the dialog would otherwise change the process's working directory.
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_HIDEREADONLY;

    if (!GetOpenFileNameW(&dialog))
        return std::nullopt;  // cancelled (or failed, which CommDlgExtendedError would explain)
    return std::filesystem::path(file);
#else
    (void)owner;
    return std::nullopt;
#endif
}
