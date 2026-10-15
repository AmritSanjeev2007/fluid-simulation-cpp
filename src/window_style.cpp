#include "window_style.hpp"

#include <GLFW/glfw3.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwmapi.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#endif

#define NANOSVG_IMPLEMENTATION
#include <nanosvg.h>
#define NANOSVGRAST_IMPLEMENTATION
#include <nanosvgrast.h>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

bool setWindowIconFromSvg(GLFWwindow* window, const std::filesystem::path& svgPath)
{
#ifdef __APPLE__
    // macOS windows have no per-window icon; the app bundle's icon is used instead.
    (void)window;
    (void)svgPath;
    return false;
#else
    std::ifstream file(svgPath, std::ios::binary);
    if (!file)
    {
        std::cerr << "Could not open window icon: " << svgPath.string() << '\n';
        return false;
    }
    // nsvgParse modifies its input, so it needs a mutable, null-terminated buffer.
    std::string svgText((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    NSVGimage* svg = nsvgParse(svgText.data(), "px", 96.0f);
    if (!svg || svg->width <= 0.0f || svg->height <= 0.0f)
    {
        std::cerr << "Could not parse window icon: " << svgPath.string() << '\n';
        nsvgDelete(svg);
        return false;
    }

    NSVGrasterizer* rasterizer = nsvgCreateRasterizer();

    // Provide several sizes; the OS picks the best fit for the title bar, taskbar, Alt+Tab, etc.
    constexpr int sizes[] = { 16, 24, 32, 48, 64, 128, 256 };
    std::vector<std::vector<unsigned char>> pixels;
    std::vector<GLFWimage> images;
    pixels.reserve(std::size(sizes));

    for (int size : sizes)
    {
        // Fit the SVG inside a square, centred, preserving aspect ratio.
        const float scale   = size / std::max(svg->width, svg->height);
        const float offsetX = (size - svg->width * scale) * 0.5f;
        const float offsetY = (size - svg->height * scale) * 0.5f;

        auto& buffer = pixels.emplace_back(static_cast<size_t>(size) * size * 4, 0);
        nsvgRasterize(rasterizer, svg, offsetX, offsetY, scale, buffer.data(), size, size, size * 4);
        images.push_back(GLFWimage{ size, size, buffer.data() });
    }

    glfwSetWindowIcon(window, static_cast<int>(images.size()), images.data());

    nsvgDeleteRasterizer(rasterizer);
    nsvgDelete(svg);
    return true;
#endif
}

void setTitleBarColor(GLFWwindow* window, const glm::vec3& background, const glm::vec3& text)
{
#ifdef _WIN32
    // Defined here because older SDK / MinGW headers may not declare them.
    constexpr DWORD dwmwaCaptionColor = 35; // DWMWA_CAPTION_COLOR
    constexpr DWORD dwmwaTextColor    = 36; // DWMWA_TEXT_COLOR

    auto toColorRef = [](const glm::vec3& c) {
        auto channel = [](float v) { return static_cast<BYTE>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
        return RGB(channel(c.r), channel(c.g), channel(c.b));
    };

    HWND hwnd = glfwGetWin32Window(window);
    const COLORREF caption   = toColorRef(background);
    const COLORREF captionFg = toColorRef(text);
    // Fails harmlessly on Windows 10 and earlier, which keep the default title bar.
    DwmSetWindowAttribute(hwnd, dwmwaCaptionColor, &caption, sizeof(caption));
    DwmSetWindowAttribute(hwnd, dwmwaTextColor, &captionFg, sizeof(captionFg));
#else
    (void)window;
    (void)background;
    (void)text;
#endif
}
