cmake_minimum_required(VERSION 3.24)
project(FluidSimulation LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ---------------------------------------------------------------------------
# Dependencies: fetched and built from source at configure time, so the only
# prerequisites are a C++ compiler, CMake and Git.
# ---------------------------------------------------------------------------
include(FetchContent)

set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)

FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
    GIT_SHALLOW    TRUE
)

FetchContent_Declare(glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.1
    GIT_SHALLOW    TRUE
)

# Docking branch: panels can be docked/tiled inside the main window.
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.92.9b-docking
    GIT_SHALLOW    TRUE
)

# nanosvg (SVG parser + rasteriser, header-only) has no releases, so pin a commit.
# SOURCE_SUBDIR points at a folder without a CMakeLists.txt so only the headers are used.
FetchContent_Declare(nanosvg
    GIT_REPOSITORY https://github.com/memononen/nanosvg.git
    GIT_TAG        239e102ec2c691f2902e20ace2ed36ee4a35cfe6
    SOURCE_SUBDIR  headers-only
)

FetchContent_MakeAvailable(glfw glm imgui nanosvg)

# glad (OpenGL 4.6 core loader) is pre-generated and vendored in external/glad.
add_library(glad STATIC external/glad/src/gl.c)
target_include_directories(glad PUBLIC external/glad/include)

# Dear ImGui has no CMake build of its own, so compile its sources directly.
add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
    ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
)
target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${imgui_SOURCE_DIR}/misc/cpp
)
target_link_libraries(imgui PUBLIC glfw)

find_package(OpenGL REQUIRED)

# Optional: the solver's grid loops run in parallel when OpenMP is available.
find_package(OpenMP)

# ---------------------------------------------------------------------------
# Fluid solver: no OpenGL calls, so the headless tests can link it too.
# ---------------------------------------------------------------------------
add_library(fluid_solver STATIC
    src/solver/fluid_solver.cpp
    src/solver/linear_solver.cpp
    src/solver/mac_grid.cpp
    src/solver/sdf.cpp
    src/solver/streamlines.cpp
)
target_include_directories(fluid_solver PUBLIC src external/glad/include)
target_link_libraries(fluid_solver PUBLIC glm::glm)
if(OpenMP_CXX_FOUND)
    target_link_libraries(fluid_solver PUBLIC OpenMP::OpenMP_CXX)
endif()

# ---------------------------------------------------------------------------
# Application
# ---------------------------------------------------------------------------
add_executable(fluid_sim
    src/main.cpp
    src/gui.cpp
    src/simulation_info.cpp
    src/window_style.cpp
    src/camera/camera.cpp
    src/input/camera_controller.cpp
    src/input/mesh_picker.cpp
    src/meshes/mesh.cpp
    src/meshes/obj_loader.cpp
    src/meshes/raycast.cpp
    src/meshes/shader_program.cpp
    src/platform/file_dialog.cpp
    src/render/field_renderer.cpp
    src/render/renderer.cpp
    src/render/screen_quad.cpp
)
target_include_directories(fluid_sim PRIVATE src ${nanosvg_SOURCE_DIR}/src)
target_link_libraries(fluid_sim PRIVATE fluid_solver glad glfw glm::glm imgui OpenGL::GL)

if(WIN32)
    target_link_libraries(fluid_sim PRIVATE dwmapi comdlg32)  # title bar colour, file dialog
endif()

# Copy runtime assets (icons, shaders, meshes) next to the executable. Runs on
# every build so editing e.g. a shader is picked up without recompiling.
add_custom_target(copy_assets ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:fluid_sim>/assets
    COMMENT "Copying assets"
)
add_dependencies(fluid_sim copy_assets)

# Headless solver checks: ./solver_tests assets/meshes/aerofoil.obj (or `ctest`).
add_executable(solver_tests
    tests/solver_tests.cpp
    src/meshes/obj_loader.cpp
    src/meshes/raycast.cpp
)
target_link_libraries(solver_tests PRIVATE fluid_solver)
enable_testing()
add_test(NAME solver_tests
         COMMAND solver_tests ${CMAKE_SOURCE_DIR}/assets/meshes/aerofoil.obj)

if(MINGW)
    # Ship self-contained .exe files (no libstdc++/libgcc DLLs needed next to them).
    target_link_options(fluid_sim PRIVATE -static)
    target_link_options(solver_tests PRIVATE -static)
endif()
