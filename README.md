# Fluid Simulation C++
A 3D fluid simulation program that can take in "closed" 3D mesh and simulate fluid mechanics around it.

## Dependencies

You don't install any libraries by hand. CMake downloads and builds them automatically the first time you configure:

| Library | Purpose | How it's provided |
|---|---|---|
| [GLFW](https://www.glfw.org/) 3.4 | Window, input, OpenGL context | `FetchContent` (downloaded at configure time) |
| [GLM](https://github.com/g-truc/glm) 1.0.1 | Vector/matrix math | `FetchContent` (downloaded at configure time) |
| [Dear ImGui](https://github.com/ocornut/imgui) 1.92.9b (docking) | In-app GUI / parameter panel | `FetchContent` (downloaded at configure time) |
| [nanosvg](https://github.com/memononen/nanosvg) | Rasterises the SVG window icon at runtime | `FetchContent` (downloaded at configure time) |
| [glad](https://github.com/Dav1dde/glad) | OpenGL 4.6 core function loader | Pre-generated in `external/glad` |

You only need a **C++20 compiler**, **CMake ≥ 3.24**, **Ninja**, and **Git**.

## Project layout

```
src/
  main.cpp              window creation and main loop
  constants.hpp         window settings and colour constants
  simulation_info.*     SimulationInfo: parameters, run state and mesh data the UI edits; drives the solver
  gui.*                 Gui: Dear ImGui setup and the "Simulation" panel
  window_style.*        SVG window icon and native title bar colour
  camera/
    camera.*            Camera: orbit camera (pivot, distance, yaw, pitch) -> view/projection matrices
  input/
    camera_controller.* CameraController: Blender-style mouse controls for the Camera
    mesh_picker.*       MeshPicker: finds the mesh under the cursor (hover outline) and selects it on click
  meshes/
    mesh.*              Vertex / MeshData types and the GPU Mesh class (VAO/VBO/EBO)
    obj_loader.*        Wavefront .obj parser -> MeshData
    raycast.*           ray from a screen point, ray-mesh intersection (for picking)
    shader_program.*    loads, compiles and links GLSL shader programs
  platform/
    file_dialog.*       native "open .obj" dialog (Windows)
  render/
    renderer.*          Renderer: directional-light shading of meshes, hover outline, then the screen overlay pass
    screen_quad.*       ScreenQuad: full-screen quad for overlay passes (screen.vert/.frag draw the axes gizmo)
    field_renderer.*    FieldRenderer: colour-mapped slice (field.*), transparent volume (volume.*) or streamlines (streamline.*), domain box (line.*)
    colormap.hpp        viridis and blue-white-red colour maps (CPU copy for the GUI colour bar)
  solver/               the fluid solver (no OpenGL); the design is in solver.md
    solver_parameters.hpp FluidParameters, SolverParameters and obstacle thermal settings
    grid3.hpp           Grid3<T>: flat 3D array with trilinear sampling
    mac_grid.*          Domain and MacGrid: staggered velocity, pressure, temperature, obstacle SDF
    sdf.*               signed distance field of a closed mesh on the grid
    linear_solver.*     StencilMatrix and MIC(0)-preconditioned conjugate gradient
    fluid_solver.*      FluidSolver: advection, buoyancy, diffusion, boundaries, pressure projection
    streamlines.*       streamlines traced from a seed plane upstream of the mesh
tests/
  solver_tests.cpp      headless checks (still water, uniform flow, sphere, heated sphere, two obstacles, picking)
assets/                 runtime assets (icons, shaders, meshes), copied next to the executable on build
external/glad/          pre-generated OpenGL loader
```

## Setup

### Windows (MSYS2 UCRT64) — recommended

1. Install [MSYS2](https://www.msys2.org/).
2. Open the **MSYS2 UCRT64** terminal (`C:\msys64\ucrt64.exe`) and install the toolchain:
   ```bash
   pacman -S --needed git mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja
   ```
3. (Optional) Add `C:\msys64\ucrt64\bin` to your Windows `PATH` so you can build from PowerShell or VS Code.

### Windows (Visual Studio)

Install Visual Studio 2022 with the "Desktop development with C++" workload. It includes CMake and Ninja. Open the folder in Visual Studio, or build from the *Developer PowerShell*.

### Linux

```bash
# Debian/Ubuntu
sudo apt install build-essential cmake ninja-build git \
    libgl1-mesa-dev libwayland-dev libxkbcommon-dev xorg-dev
```

### macOS

```bash
xcode-select --install
brew install cmake ninja
```
(macOS supports OpenGL up to 4.1.)

## Build & run

```bash
git clone https://github.com/AmritSanjeev2007/fluid-simulation-cpp.git
cd fluid-simulation-cpp
cmake --preset default        # configure (downloads GLFW + GLM on first run)
cmake --build --preset default
./build/release/fluid_sim     # fluid_sim.exe on Windows
```

For a debug build, use `--preset debug`. The output goes to `build/debug/`.

To check the solver without opening a window, run `./build/release/solver_tests assets/meshes/aerofoil.obj` (or `ctest --test-dir build/release`).

### Running a simulation

The solver starts paused with still water and no mesh. Click **Mesh > Upload mesh...** and choose a Wavefront `.obj` file (only `.obj` files are accepted; on Linux and macOS, type its path and press **Upload**). You can upload several meshes; each one is an obstacle to the flow. Click a mesh in the view (or its name under **Mesh**) to select it, and **Mesh properties** shows and edits the selected mesh. Clicking empty space clears the selection, and the mesh under the cursor gets an orange outline.

Set **Fluid > Velocity** (for the aerofoil, `0, 0, 1` m/s blows along its chord), press **Play**, and pick what to show under **View > Field**, as a **Slice**, a transparent **Volume** of the whole domain, or **Streamlines** (their density is set by **Streamline resolution**). Changing the mesh transform, grid resolution, domain padding or the direction of the velocity rebuilds the domain and restarts the simulation.

### Viewport controls

| Input | Action |
|---|---|
| Middle mouse drag | Orbit around the pivot |
| Shift + middle mouse drag | Pan |
| Scroll wheel | Zoom |
| Left click | Select the mesh under the cursor (empty space clears the selection) |

### VS Code

Install the **CMake Tools** extension and open the folder. It detects `CMakePresets.json` automatically.
