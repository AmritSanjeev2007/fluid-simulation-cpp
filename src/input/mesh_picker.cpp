#include "mesh_picker.hpp"

#include "meshes/raycast.hpp"
#include "simulation_info.hpp"

#include <GLFW/glfw3.h>

MeshPicker::MeshPicker(GLFWwindow* window)
    : m_window(window)
{
    // A click shorter than a frame would otherwise be missed by polling.
    glfwSetInputMode(window, GLFW_STICKY_MOUSE_BUTTONS, GLFW_TRUE);
}

void MeshPicker::update(const glm::mat4& viewProjection, SimulationInfo& info, bool uiWantsMouse)
{
    const bool leftDown    = glfwGetMouseButton(m_window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    const bool leftPressed = leftDown && !m_leftWasDown;
    m_leftWasDown = leftDown;

    m_hoveredId = 0;
    if (uiWantsMouse)
        return;

    // Cursor and window size are both in screen coordinates, so this holds on high-DPI displays too.
    double x = 0.0, y = 0.0;
    int    width = 0, height = 0;
    glfwGetCursorPos(m_window, &x, &y);
    glfwGetWindowSize(m_window, &width, &height);
    if (width <= 0 || height <= 0)
        return;
    const glm::vec2 ndc = { 2.0f * static_cast<float>(x) / static_cast<float>(width) - 1.0f,
                            1.0f - 2.0f * static_cast<float>(y) / static_cast<float>(height) };
    const Ray ray = screenRay(ndc, glm::inverse(viewProjection));

    float nearest = 0.0f;
    for (const SceneMesh& mesh : info.meshes())
    {
        const auto hit = raycast(mesh.data, mesh.info.transform.modelMatrix(), ray);
        if (hit && (m_hoveredId == 0 || *hit < nearest))
        {
            nearest     = *hit;
            m_hoveredId = mesh.info.id;
        }
    }

    if (leftPressed)
        info.selectMesh(m_hoveredId);
}
