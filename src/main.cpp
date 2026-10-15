#include "camera/camera.hpp"
#include "constants.hpp"
#include "gui.hpp"
#include "input/camera_controller.hpp"
#include "input/mesh_picker.hpp"
#include "meshes/mesh.hpp"
#include "render/field_renderer.hpp"
#include "render/renderer.hpp"
#include "simulation_info.hpp"
#include "window_style.hpp"

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <unordered_map>

// Keeps one GPU mesh per scene mesh: uploads new ones and frees deleted ones.
static void syncGpuMeshes(const SimulationInfo& info, std::unordered_map<int, Mesh>& gpuMeshes)
{
    std::erase_if(gpuMeshes, [&](const auto& entry)
    {
        for (const SceneMesh& mesh : info.meshes())
            if (mesh.info.id == entry.first)
                return false;
        return true;
    });
    for (const SceneMesh& mesh : info.meshes())
        if (!gpuMeshes.contains(mesh.info.id))
            gpuMeshes.emplace(mesh.info.id, Mesh(mesh.data));
}

static void glfwErrorCallback(int code, const char* description)
{
    std::cerr << "GLFW error " << code << ": " << description << '\n';
}

// Everything that needs a live OpenGL context. Returns the process exit code.
static int run(GLFWwindow* window, const char* glslVersion, const std::filesystem::path& exeDir)
{
    Gui gui(window, glslVersion);
    SimulationInfo info;

    // Declared after gui so its GLFW callback is installed after (and removed before) ImGui's.
    Camera           camera;
    CameraController cameraController(window, camera);
    MeshPicker       meshPicker(window);

    Renderer renderer(exeDir / constants::assets::meshVertexShader,
                      exeDir / constants::assets::meshFragmentShader,
                      exeDir / constants::assets::screenVertexShader,
                      exeDir / constants::assets::screenFragmentShader);
    FieldRenderer fieldRenderer(exeDir / constants::assets::fieldVertexShader,
                                exeDir / constants::assets::fieldFragmentShader,
                                exeDir / constants::assets::volumeVertexShader,
                                exeDir / constants::assets::volumeFragmentShader,
                                exeDir / constants::assets::streamlineVertexShader,
                                exeDir / constants::assets::streamlineFragmentShader,
                                exeDir / constants::assets::lineVertexShader,
                                exeDir / constants::assets::lineFragmentShader);

    // GPU copies of the scene's meshes, by mesh id. Meshes are added with "Upload mesh...".
    std::unordered_map<int, Mesh> gpuMeshes;

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        int width = 0, height = 0;
        glfwGetFramebufferSize(window, &width, &height);
        if (width == 0 || height == 0)
        {
            // Minimised: nothing to draw, so sleep until something happens.
            glfwWaitEvents();
            continue;
        }

        // Only quit on Escape when ImGui isn't using the keyboard (e.g. typing in a field).
        if (!gui.wantsKeyboard() && glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
            glfwSetWindowShouldClose(window, GLFW_TRUE);

        info.update();

        gui.beginFrame();
        cameraController.update(gui.wantsMouse());
        meshPicker.update(camera.viewProjection(static_cast<float>(width) / static_cast<float>(height)), info,
                          gui.wantsMouse());
        gui.draw(info);
        syncGpuMeshes(info, gpuMeshes);

        float dpiScale = 1.0f;
        glfwGetWindowContentScale(window, &dpiScale, nullptr);

        renderer.beginFrame(width, height, info.backgroundColor, camera);
        for (const SceneMesh& mesh : info.meshes())
            renderer.draw(gpuMeshes.at(mesh.info.id), mesh.info.transform.modelMatrix());

        fieldRenderer.update(info.field(), info.streamlines());
        if (info.view.showDomain)
            fieldRenderer.drawDomain(renderer.viewProjection());
        if (info.view.display == FieldDisplay::Slice)
            fieldRenderer.drawSlice(renderer.viewProjection(), info.view, info.fieldIsDiverging());
        else if (info.view.display == FieldDisplay::Volume)
            fieldRenderer.drawVolume(renderer.viewProjection(), info.view, info.fieldIsDiverging());
        else if (info.view.display == FieldDisplay::Streamlines)
            fieldRenderer.drawStreamlines(renderer.viewProjection(), info.view, info.fieldIsDiverging());

        // After the field, which would otherwise paint over it (the outline writes no depth).
        for (const SceneMesh& mesh : info.meshes())
            if (mesh.info.id == meshPicker.hoveredMeshId())
                renderer.drawOutline(gpuMeshes.at(mesh.info.id), mesh.info.transform.modelMatrix(), dpiScale);

        renderer.drawOverlay(camera, dpiScale);

        gui.render();
        glfwSwapBuffers(window);
    }
    return EXIT_SUCCESS;
}

int main(int, char* argv[])
{
    // Assets are copied next to the executable at build time, so resolve them
    // relative to it rather than to the current working directory.
    const std::filesystem::path exeDir = std::filesystem::absolute(argv[0]).parent_path();

    glfwSetErrorCallback(glfwErrorCallback);
    if (!glfwInit())
        return EXIT_FAILURE;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
    glfwWindowHint(GLFW_STENCIL_BITS, 8);  // for the hover outline
    const char* glslVersion = "#version 460";
#ifdef __APPLE__
    // macOS tops out at OpenGL 4.1.
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    glslVersion = "#version 410";
#endif

    GLFWwindow* window = glfwCreateWindow(constants::window::width, constants::window::height,
                                          constants::window::title, nullptr, nullptr);
    if (!window)
    {
        glfwTerminate();
        return EXIT_FAILURE;
    }

    setWindowIconFromSvg(window, exeDir / constants::assets::windowIcon);
    setTitleBarColor(window, constants::colors::titleBar, constants::colors::titleBarText);

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    if (!gladLoadGL(glfwGetProcAddress))
    {
        std::cerr << "Failed to load OpenGL functions\n";
        glfwTerminate();
        return EXIT_FAILURE;
    }

    std::cout << "OpenGL " << glGetString(GL_VERSION) << '\n'
              << "Renderer: " << glGetString(GL_RENDERER) << std::endl;

    // run() owns all GL resources, so they are released before the context is destroyed.
    int exitCode = EXIT_FAILURE;
    try
    {
        exitCode = run(window, glslVersion, exeDir);
    }
    catch (const std::exception& e)
    {
        std::cerr << "Fatal error: " << e.what() << '\n';
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    return exitCode;
}
