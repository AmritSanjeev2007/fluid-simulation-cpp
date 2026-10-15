#pragma once

#include <glm/glm.hpp>

struct GLFWwindow;
class SimulationInfo;

// Finds the mesh under the mouse cursor each frame, and selects it on a left click.
// Clicking empty space clears the selection.
class MeshPicker
{
public:
    explicit MeshPicker(GLFWwindow* window);

    // Call once per frame, after ImGui's NewFrame and any camera movement. While the UI wants
    // the mouse (cursor over a panel) nothing is hovered and clicks are left to the UI.
    void update(const glm::mat4& viewProjection, SimulationInfo& info, bool uiWantsMouse);

    // Id of the mesh under the cursor, or 0 for none.
    int hoveredMeshId() const { return m_hoveredId; }

private:
    GLFWwindow* m_window;
    int         m_hoveredId   = 0;
    bool        m_leftWasDown = false;
};
