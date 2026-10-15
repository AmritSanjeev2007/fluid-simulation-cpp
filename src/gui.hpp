#pragma once

#include <array>
#include <string>

struct GLFWwindow;
class SimulationInfo;

// Owns the Dear ImGui context and draws the application's panels.
class Gui
{
public:
    Gui(GLFWwindow* window, const char* glslVersion);
    ~Gui();

    Gui(const Gui&)            = delete;
    Gui& operator=(const Gui&) = delete;

    void beginFrame();
    void draw(SimulationInfo& info);
    void render();

    // True while ImGui is consuming keyboard input (e.g. typing in a text field).
    bool wantsKeyboard() const;

    // True while the cursor is over (or dragging) an ImGui window.
    bool wantsMouse() const;

private:
    void drawPerformance();
    void drawControls(SimulationInfo& info);
    void drawMesh(SimulationInfo& info);
    void drawFluid(SimulationInfo& info);
    void drawSolver(SimulationInfo& info);
    void drawDiagnostics(const SimulationInfo& info);
    void drawView(SimulationInfo& info);

    // Loads an .obj file into the scene, or sets m_uploadError and leaves the scene alone.
    void uploadMesh(SimulationInfo& info, const std::string& path);

    GLFWwindow* m_window;

    // Rolling history of frame times for the graph.
    std::array<float, 120> m_frameTimes{};
    int                    m_frameIndex = 0;

    std::string m_uploadPath;   // typed path, where there is no native file dialog
    std::string m_uploadError;  // why the last upload failed, shown under the button
};
