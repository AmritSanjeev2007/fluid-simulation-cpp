#include "gui.hpp"

#include "constants.hpp"
#include "meshes/obj_loader.hpp"
#include "platform/file_dialog.hpp"
#include "render/colormap.hpp"
#include "simulation_info.hpp"

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <utility>

namespace
{
    ImVec4 toImVec4(const glm::vec3& c) { return ImVec4(c.r, c.g, c.b, 1.0f); }

    // Sizes are in font units so the UI scales with the DPI-scaled font.
    float em(float n) { return n * ImGui::GetFontSize(); }

    constexpr float kelvinOffset = 273.15f;

    // Edits a temperature stored in kelvin as degrees Celsius.
    bool sliderCelsius(const char* label, float& kelvin, float min, float max)
    {
        float celsius = kelvin - kelvinOffset;
        if (!ImGui::SliderFloat(label, &celsius, min, max, "%.1f C"))
            return false;
        kelvin = celsius + kelvinOffset;
        return true;
    }

    const char* fieldName(DisplayField field)
    {
        switch (field)
        {
        case DisplayField::VelocityMagnitude:   return "Velocity magnitude";
        case DisplayField::Pressure:            return "Pressure";
        case DisplayField::TotalPressure:       return "Total pressure (with hydrostatic)";
        case DisplayField::PressureCoefficient: return "Pressure coefficient Cp";
        case DisplayField::Temperature:         return "Temperature";
        }
        return "";
    }

    const char* fieldUnit(DisplayField field)
    {
        switch (field)
        {
        case DisplayField::VelocityMagnitude:   return "m/s";
        case DisplayField::Pressure:
        case DisplayField::TotalPressure:       return "Pa";
        case DisplayField::PressureCoefficient: return "";
        case DisplayField::Temperature:         return "C";
        }
        return "";
    }

    // Horizontal colour bar for the current colour map, with the range written underneath.
    // With fadeIn, the bar's opacity ramps from 0 to 0.5 like the volume view's.
    void colorBar(bool diverging, bool fadeIn, glm::vec2 range, const char* unit)
    {
        constexpr int segments = 32;
        const float   width    = ImGui::GetContentRegionAvail().x;
        const float   height   = em(0.9f);
        const ImVec2  origin   = ImGui::GetCursorScreenPos();
        ImDrawList*   draw     = ImGui::GetWindowDrawList();
        for (int s = 0; s < segments; ++s)
        {
            const float t0 = static_cast<float>(s) / segments;
            const float t1 = static_cast<float>(s + 1) / segments;
            ImVec4 c0 = toImVec4(colormap::sample(diverging, t0));
            ImVec4 c1 = toImVec4(colormap::sample(diverging, t1));
            if (fadeIn)
            {
                c0.w = colormap::volumeOpacity(t0);
                c1.w = colormap::volumeOpacity(t1);
            }
            const ImU32 left  = ImGui::GetColorU32(c0);
            const ImU32 right = ImGui::GetColorU32(c1);
            const float x0    = origin.x + width * s / segments;
            const float x1    = origin.x + width * (s + 1) / segments;
            draw->AddRectFilledMultiColor(ImVec2(x0, origin.y), ImVec2(x1, origin.y + height), left, right, right, left);
        }
        ImGui::Dummy(ImVec2(width, height));
        ImGui::Text("%.4g %s", range.x, unit);
        char maxText[48];
        std::snprintf(maxText, sizeof(maxText), "%.4g %s", range.y, unit);
        ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::CalcTextSize(maxText).x);
        ImGui::TextUnformatted(maxText);
    }
}

Gui::Gui(GLFWwindow* window, const char* glslVersion)
    : m_window(window)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;
    ImGui::StyleColorsDark();

    // Scale the UI for high-DPI monitors.
    float dpiScale = 1.0f;
    glfwGetWindowContentScale(window, &dpiScale, nullptr);
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(dpiScale);
    style.FontScaleDpi = dpiScale;

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glslVersion);
}

Gui::~Gui()
{
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void Gui::beginFrame()
{
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // Lets panels dock to the window edges while keeping the centre see-through for the 3D view.
    ImGui::DockSpaceOverViewport(0, nullptr, ImGuiDockNodeFlags_PassthruCentralNode);
}

void Gui::render()
{
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

bool Gui::wantsKeyboard() const
{
    return ImGui::GetIO().WantCaptureKeyboard;
}

bool Gui::wantsMouse() const
{
    return ImGui::GetIO().WantCaptureMouse;
}

void Gui::draw(SimulationInfo& info)
{
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(em(28), 0), ImGuiCond_FirstUseEver);
    ImGui::Begin("Simulation");

    drawPerformance();
    drawControls(info);
    drawMesh(info);
    drawFluid(info);
    drawSolver(info);
    drawDiagnostics(info);
    drawView(info);

    ImGui::End();
}

void Gui::drawPerformance()
{
    const ImGuiIO& io = ImGui::GetIO();
    m_frameTimes[m_frameIndex] = 1000.0f * io.DeltaTime;
    m_frameIndex = (m_frameIndex + 1) % static_cast<int>(m_frameTimes.size());

    ImGui::Text("%.1f FPS (%.2f ms/frame)", io.Framerate, 1000.0f / io.Framerate);
    ImGui::PlotLines("##frametimes", m_frameTimes.data(), static_cast<int>(m_frameTimes.size()),
                     m_frameIndex, "frame time (ms)", 0.0f, 33.3f, ImVec2(-1, em(4)));
}

void Gui::drawControls(SimulationInfo& info)
{
    ImGui::SeparatorText("Controls");
    if (ImGui::Button(info.isPaused() ? "Play" : "Pause", ImVec2(em(6), 0)))
        info.togglePaused();
    ImGui::SameLine();
    ImGui::BeginDisabled(!info.isPaused());
    if (ImGui::Button("Step", ImVec2(em(6), 0)))
        info.requestStep();
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Reset", ImVec2(em(6), 0)))
        info.reset();
    ImGui::Text("t = %.3f s   steps = %d", info.elapsedTime(), info.stepCount());
}

void Gui::uploadMesh(SimulationInfo& info, const std::string& path)
{
    const std::filesystem::path file(reinterpret_cast<const char8_t*>(path.c_str()));
    if (!isObjPath(file))
    {
        m_uploadError = "Only Wavefront .obj files can be uploaded.";
        return;
    }
    try
    {
        MeshData data = loadObj(file);
        if (data.indices.empty())
        {
            m_uploadError = "The file has no faces.";
            return;
        }
        const std::u8string stem = file.stem().u8string();
        info.addMesh(std::string(stem.begin(), stem.end()), std::move(data));
        m_uploadError.clear();
    }
    catch (const std::exception& e)
    {
        m_uploadError = e.what();
    }
}

void Gui::drawMesh(SimulationInfo& info)
{
    ImGui::SeparatorText("Mesh");

    if (hasNativeFileDialog())
    {
        if (ImGui::Button("Upload mesh...", ImVec2(-1, 0)))
        {
            if (const auto file = openObjFileDialog(m_window))
            {
                const std::u8string path = file->u8string();
                uploadMesh(info, std::string(path.begin(), path.end()));
            }
        }
        ImGui::SetItemTooltip("Add a Wavefront .obj mesh to the scene.");
    }
    else
    {
        // No system file dialog on this platform: type the path instead.
        ImGui::SetNextItemWidth(-em(6));
        const bool entered = ImGui::InputTextWithHint("##meshpath", "path/to/mesh.obj", &m_uploadPath,
                                                      ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        if ((ImGui::Button("Upload", ImVec2(-1, 0)) || entered) && !m_uploadPath.empty())
            uploadMesh(info, m_uploadPath);
    }
    if (!m_uploadError.empty())
        ImGui::TextColored(toImVec4(constants::colors::dangerActive), "%s", m_uploadError.c_str());

    // Every mesh in the scene; clicking one here selects it, just like clicking it in the view.
    for (const SceneMesh& mesh : info.meshes())
    {
        ImGui::PushID(mesh.info.id);
        const char* name = mesh.info.name.empty() ? "(unnamed)" : mesh.info.name.c_str();
        if (ImGui::Selectable(name, mesh.info.id == info.selectedMeshId()))
            info.selectMesh(mesh.info.id);
        ImGui::PopID();
    }

    if (!ImGui::CollapsingHeader("Mesh properties", ImGuiTreeNodeFlags_DefaultOpen))
        return;

    MeshInfo* selected = info.selectedMesh();
    if (!selected)
    {
        ImGui::TextDisabled(info.meshes().empty() ? "Upload a mesh to edit its properties."
                                                  : "Click a mesh to edit its properties.");
        return;
    }

    MeshInfo&      mesh      = *selected;
    MeshTransform& transform = mesh.transform;

    ImGui::PushID(mesh.id);
    ImGui::InputTextWithHint("Name", "unnamed", &mesh.name);
    ImGui::DragFloat3("Position", &transform.position.x, 0.01f, 0.0f, 0.0f, "%.2f m");
    ImGui::DragFloat3("Rotation", &transform.rotation.x, 0.5f, -180.0f, 180.0f, "%.1f deg");
    ImGui::DragFloat3("Scale", &transform.scale.x, 0.01f, 0.001f, 1000.0f, "%.3f");

    ObstacleThermal& thermal = mesh.thermal;
    bool insulated = thermal.boundary == ThermalBoundary::Insulated;
    if (ImGui::Checkbox("Insulated surface", &insulated))
        thermal.boundary = insulated ? ThermalBoundary::Insulated : ThermalBoundary::FixedTemperature;
    ImGui::BeginDisabled(insulated);
    sliderCelsius("Surface temperature", thermal.temperature, -20.0f, 120.0f);
    ImGui::EndDisabled();

    ImGui::PushStyleColor(ImGuiCol_Button,        toImVec4(constants::colors::danger));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, toImVec4(constants::colors::dangerHovered));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  toImVec4(constants::colors::dangerActive));
    const bool remove = ImGui::Button("Delete mesh", ImVec2(-1, 0));
    ImGui::PopStyleColor(3);
    ImGui::PopID();

    // Last, since it invalidates `mesh`.
    if (remove)
        info.deleteMesh(mesh.id);
}

void Gui::drawFluid(SimulationInfo& info)
{
    FluidParameters& fluid = info.fluid;

    ImGui::SeparatorText("Fluid");
    ImGui::SliderFloat("Viscosity", &fluid.viscosity, 0.0f, 0.1f, "%.4f Pa s", ImGuiSliderFlags_Logarithmic);
    ImGui::SetItemTooltip("Dynamic viscosity. Water at 20 C is 0.001 Pa s.");
    ImGui::SliderFloat("Density", &fluid.density, 1.0f, 2000.0f, "%.0f kg/m^3");
    ImGui::DragFloat3("Gravity", &fluid.gravity.x, 0.05f, -50.0f, 50.0f, "%.2f");
    ImGui::DragFloat3("Velocity", &fluid.velocity.x, 0.01f, 0.0f, 0.0f, "%.2f m/s");
    ImGui::SetItemTooltip("Free-stream velocity: the flow entering the domain.\n"
                          "Changing its direction rebuilds the domain and restarts the simulation.");

    if (ImGui::CollapsingHeader("Temperature"))
    {
        sliderCelsius("Ambient", fluid.ambientTemperature, -20.0f, 120.0f);
        ImGui::SliderFloat("Diffusivity", &fluid.thermalDiffusivity, 1e-8f, 1e-2f, "%.2e m^2/s",
                           ImGuiSliderFlags_Logarithmic);
        ImGui::Checkbox("Buoyancy", &fluid.buoyancy);
        ImGui::BeginDisabled(!fluid.buoyancy);
        ImGui::SliderFloat("Expansion", &fluid.thermalExpansion, 1e-6f, 1e-1f, "%.2e 1/K",
                           ImGuiSliderFlags_Logarithmic);
        ImGui::EndDisabled();
    }
}

void Gui::drawSolver(SimulationInfo& info)
{
    SolverParameters& solver = info.solver;

    ImGui::SeparatorText("Solver");
    ImGui::SliderFloat("Time step", &solver.timeStep, 0.001f, 0.05f, "%.4f s");
    ImGui::SliderInt("Grid resolution", &solver.gridResolution, 16, 256);
    ImGui::SetItemTooltip("Cells along the longest side of the domain. Changing it restarts the simulation.");
    ImGui::SliderInt("Iterations", &solver.solverIterations, 1, 500, "%d", ImGuiSliderFlags_Logarithmic);
    ImGui::SetItemTooltip("Maximum iterations of each linear solve (pressure, diffusion).");
    ImGui::SliderFloat("Tolerance", &solver.pressureTolerance, 1e-6f, 1e-1f, "%.1e", ImGuiSliderFlags_Logarithmic);
    ImGui::SetItemTooltip("Relative residual at which a linear solve stops early.");
    ImGui::SliderFloat("Domain padding", &solver.domainPadding, 0.25f, 4.0f, "%.2f x");
    ImGui::SetItemTooltip("Space around the mesh, in multiples of its size.");
    ImGui::SliderFloat("Wake length", &solver.wakeLength, 0.0f, 8.0f, "%.2f x");
    ImGui::SetItemTooltip("Extra space downstream of the mesh, in multiples of its size.");
}

void Gui::drawDiagnostics(const SimulationInfo& info)
{
    if (!ImGui::CollapsingHeader("Diagnostics"))
        return;

    const SolverDiagnostics& d = info.fluidSolver().diagnostics();
    ImGui::Text("Grid %d x %d x %d, cell %.3g m", d.gridSize.x, d.gridSize.y, d.gridSize.z, d.cellSize);
    ImGui::Text("Step %.1f ms, %d substep%s, CFL %.2f", d.stepMilliseconds, d.substeps, d.substeps == 1 ? "" : "s", d.cfl);
    if (d.substepsCapped)
        ImGui::TextColored(toImVec4(constants::colors::dangerActive), "Flow too fast for the time step: reduce it");
    ImGui::Text("Pressure: %d iterations, residual %.1e", d.pressureIterations, d.pressureResidual);
    ImGui::Text("Max divergence %.2e 1/s", d.maxDivergence);
    if (std::isfinite(d.reynolds))
        ImGui::Text("Reynolds number %.3g", d.reynolds);
    else
        ImGui::TextUnformatted("Reynolds number: inviscid");
}

void Gui::drawView(SimulationInfo& info)
{
    ImGui::SeparatorText("View");
    ViewSettings& view = info.view;

    if (ImGui::BeginCombo("Field", fieldName(view.field)))
    {
        for (DisplayField field : { DisplayField::VelocityMagnitude, DisplayField::Pressure, DisplayField::TotalPressure,
                                    DisplayField::PressureCoefficient, DisplayField::Temperature })
            if (ImGui::Selectable(fieldName(field), field == view.field))
                view.field = field;
        ImGui::EndCombo();
    }

    int display = static_cast<int>(view.display);
    ImGui::Combo("Display", &display, "Off\0Slice\0Volume\0Streamlines\0");
    view.display = static_cast<FieldDisplay>(display);
    ImGui::SliderFloat("Opacity", &view.opacity, 0.0f, 1.0f, "%.2f");
    ImGui::SetItemTooltip("Overall opacity of the slice, volume and streamlines.");

    if (view.display == FieldDisplay::Slice)
    {
        ImGui::TextUnformatted("Slice axis");
        ImGui::SameLine();
        ImGui::RadioButton("X", &view.sliceAxis, 0);
        ImGui::SameLine();
        ImGui::RadioButton("Y", &view.sliceAxis, 1);
        ImGui::SameLine();
        ImGui::RadioButton("Z", &view.sliceAxis, 2);
        ImGui::SliderFloat("Slice position", &view.slicePosition, 0.0f, 1.0f, "%.2f");
    }
    else if (view.display == FieldDisplay::Volume)
    {
        ImGui::SliderFloat("Volume density", &view.volumeDensity, 0.1f, 10.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
        ImGui::SetItemTooltip("Scales the opacity. At 1, fluid at the top of the range is 0.5 opaque\n"
                              "over the length of the domain, and transparent at the bottom.");
    }

    ImGui::BeginDisabled(view.display != FieldDisplay::Streamlines);
    ImGui::SliderInt("Streamline resolution", &view.streamlineResolution, 2, 64);
    ImGui::SetItemTooltip("Streamlines start from a square grid of this many seeds per side, on a plane\n"
                          "just upstream of the domain box, perpendicular to the free-stream velocity.");
    ImGui::EndDisabled();
    if (view.display == FieldDisplay::Streamlines && glm::length(info.fluid.velocity) < 1e-6f)
        ImGui::TextDisabled("Set a free-stream velocity to see streamlines.");

    ImGui::Checkbox("Auto range", &view.autoRange);
    ImGui::BeginDisabled(view.autoRange);
    ImGui::DragFloat2("Range", &view.range.x, 0.01f * std::max(std::abs(view.range.y - view.range.x), 1e-3f));
    ImGui::EndDisabled();
    colorBar(info.fieldIsDiverging(), view.display == FieldDisplay::Volume, view.range, fieldUnit(view.field));

    ImGui::Checkbox("Domain box", &view.showDomain);
    ImGui::ColorEdit3("Background", &info.backgroundColor.x);
}
